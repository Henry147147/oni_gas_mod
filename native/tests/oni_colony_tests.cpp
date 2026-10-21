// Colony owner control (docs/colony-owner.md). Made-up materials and
// inventories, not game data.
#include "../src/oni_colony.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
MAOniColonyMaterial material(const char* id, std::int32_t hash, std::uint32_t index, bool gas, double molar, double c, double density = 0) {
    MAOniColonyMaterial m{}; m.struct_size = sizeof(m); m.abi_version = MA_ONI_COLONY_ABI_VERSION; m.element_hash = hash; m.native_index = index;
    m.phase = gas ? 1 : 2; m.molar_mass_kg_per_mol = molar; m.specific_heat_j_per_kg_k = c; m.conductivity_w_per_m_k = gas ? .02 : .5;
    m.density_kg_per_m3 = density; m.low_temperature_k = gas ? 50 : 250; m.high_temperature_k = gas ? 1e4 : 700;
    std::snprintf(m.id, sizeof(m.id), "%s", id);
    return m;
}
constexpr std::uint32_t oxygen = 3, carbon = 4, water = 5, vacuum = 9, W = 16, H = 12, GW = 20, GH = 15, X0 = 2, Y0 = 1;
std::uint32_t public_cell(std::uint32_t x, std::uint32_t y) { return (X0 + x) + (Y0 + y) * GW; }
int native_cell(std::uint32_t x, std::uint32_t y) { return static_cast<int>((X0 + x + 1) + (Y0 + y + 1) * (GW + 2)); }
}

int main() {
    try {
        const std::vector<MAOniColonyMaterial> materials{material("Oxygen", 11, oxygen, true, .016, 1000), material("CarbonDioxide", 12, carbon, true, .044, 850),
            material("Water", 13, water, false, .018, 4000, 1000)};
        MAOniColonyConfig config{}; config.struct_size = sizeof(config); config.abi_version = MA_ONI_COLONY_ABI_VERSION;
        config.flags = MA_ONI_COLONY_PHYSICS_CONDUCTION; config.vacuum_index = vacuum; config.grid_width = GW; config.grid_height = GH;
        config.offset_x = X0; config.offset_y = Y0; config.width = W; config.height = H; config.gravity_m_per_s2 = 9.81; config.maximum_timestep_s = .01;
        std::vector<MAOniColonyCell> cells(W * H);
        for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) {
            auto& c = cells[x + y * W];
            const bool rock = x == 0 || x == W - 1 || y == 0 || y == 8 || (x == 11 && y == 5);
            if (rock) c = {0, MA_ONI_COLONY_SOLID, 0, 0};
            else if (y >= 9) c = {vacuum, MA_ONI_COLONY_SPACE, 0, 0};
            else if (y <= 2 && x <= 7) c = {water, MA_ONI_COLONY_FLUID, 1000.f * (1.f + .01f * static_cast<float>(x % 3)), 290};
            else c = {x % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID, 1.2f + .1f * static_cast<float>(y), 300};
        }
        ma::oni::colony::start(config, materials, cells, 1);
        require(ma::oni::colony::active(), "colony active");
        // Suppression: owned fluid, solids, outside, and whole-rectangle transitions.
        using ma::oni::colony::suppress;
        require(suppress(MA_ONI_PRESSURE, native_cell(4, 4)) && !suppress(MA_ONI_PRESSURE, native_cell(0, 4)), "owned fluid cells only");
        require(!suppress(MA_ONI_PRESSURE, 0) && suppress(MA_ONI_STATE_TRANSITION, native_cell(0, 4)), "rectangle and transitions");
        // Transfers with receipts and a heat edit, committed at the next frame.
        const std::vector<MAOniColonyTransfer> transfers{{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, 7, 0, .5, 310},
            {public_cell(9, 5), carbon, MA_ONI_COLONY_EXTRACT, 2, 8, 0, 3, 0}, {public_cell(9, 5), 77, MA_ONI_COLONY_EXTRACT, 1, 9, 0, 1, 0},
            {public_cell(6, 10), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 0, 1e-3, 300}};
        ma::oni::colony::queue_transfers(transfers);
        const MAOniColonyHeat heat{public_cell(5, 1), 0, 2e5, 0};
        ma::oni::colony::queue_heat(std::span(&heat, 1));
        std::vector<std::uint8_t> image;
        std::vector<MAOniColonyCellView> views(W * H), continued(W * H);
        for (std::uint64_t frame = 1; frame <= 6; ++frame) {
            if (frame == 5) image = ma::oni::colony::checkpoint();
            ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait();
            const auto s = ma::oni::colony::status();
            if (s.state != 1) throw std::runtime_error(std::string("colony faulted: ") + s.error);
        }
        auto s = ma::oni::colony::status();
        MAOniColonyStatus st{}; ma::oni::colony::copy(st, continued.data(), static_cast<std::uint32_t>(continued.size()));
        std::cout << "frames " << s.completed_frames << " substeps " << s.substeps << " rejected " << s.rejected_attempts << " sink " << s.space_sink_kg
                  << " kg mass error " << s.maximum_relative_mass_error << " energy error " << s.maximum_relative_energy_error
                  << " reservoir " << s.reservoir_kg << " last " << s.last_advance_ms << " ms\n";
        require(s.completed_frames == 6 && s.committed_transfers == 4 && s.committed_heat == 1, "frames and commits");
        require(s.space_sink_kg > 0, "gas reaching space leaves through the sink");
        require(s.maximum_relative_mass_error < 1e-12 && s.maximum_relative_energy_error < 1e-9, "ledgers close");
        std::vector<MAOniColonyReceipt> receipts(8); const auto count = ma::oni::colony::copy_receipts(receipts.data(), 8);
        require(count == 3 && receipts[0].callback == 7 && receipts[0].mass_kg == .5f && receipts[0].kind == MA_ONI_COLONY_INJECT, "injection receipt");
        require(receipts[1].callback == 8 && receipts[1].mass_kg == 3.f && receipts[1].temperature_k > 250, "radius extraction receipt");
        require(receipts[2].callback == 9 && receipts[2].mass_kg == 0, "unknown element extracts nothing");
        ma::oni::colony::ack_receipts(receipts[2].sequence);
        require(ma::oni::colony::copy_receipts(nullptr, 0) == 0, "acknowledged");
        // Projection and composition.
        const auto& pool = continued[3 + 1 * W];
        require(pool.element_index == water && (pool.flags & MA_ONI_COLONY_VIEW_LIQUID) && pool.mass_kg > 999 && pool.pressure_pa > 0, "pool view");
        require(continued[0].flags == 0 && continued[5 + 10 * W].element_index == vacuum, "solid and space views");
        std::uint32_t indices[8]; double masses[8];
        const auto parts = ma::oni::colony::composition(public_cell(10, 6), indices, masses, 8);
        require(parts >= 1 && parts <= 3, "gas composition");
        // Exact continuation from the frame-5 checkpoint.
        ma::oni::colony::stop(); require(!ma::oni::colony::active(), "stopped");
        ma::oni::colony::restore(config, materials, image, 1);
        ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait(); ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
        require(!ma::oni::colony::busy(), "solver idle after its frames");
        ma::oni::colony::copy(st, views.data(), static_cast<std::uint32_t>(views.size()));
        require(st.state == 1 && st.completed_frames == 6, "restored frame count");
        require(std::memcmp(views.data(), continued.data(), views.size() * sizeof(MAOniColonyCellView)) == 0, "exact continuation");
        // Terrain: dig an interior wall beside gas, build over a gas cell.
        require(!suppress(MA_ONI_PRESSURE, native_cell(11, 5)) && suppress(MA_ONI_PRESSURE, native_cell(13, 6)), "terrain before");
        const std::vector<MAOniColonyTerrain> terrain{{public_cell(11, 5), MA_ONI_COLONY_OPEN}, {public_cell(13, 6), MA_ONI_COLONY_CLOSE}};
        ma::oni::colony::queue_terrain(terrain);
        ma::oni::colony::advance(7, .2, 2); ma::oni::colony::wait();
        ma::oni::colony::copy(st, views.data(), static_cast<std::uint32_t>(views.size()));
        if (st.state != 1) throw std::runtime_error(std::string("terrain faulted: ") + st.error);
        const auto& dug = views[11 + 5 * W]; const auto& built = views[13 + 6 * W]; const auto& beside = views[10 + 5 * W];
        std::cout << "dug " << dug.pressure_pa << " Pa beside " << beside.pressure_pa << " Pa, opened " << st.opened_cells << " closed " << st.closed_cells
                  << " mass error " << st.maximum_relative_mass_error << " energy error " << st.maximum_relative_energy_error << '\n';
        require(st.opened_cells == 1 && st.closed_cells == 1, "terrain counts");
        require((dug.flags & MA_ONI_COLONY_VIEW_GAS) && dug.pressure_pa > .5 * beside.pressure_pa, "dug cell refilled from its neighbours");
        require(built.flags == 0, "built cell is solid");
        require(suppress(MA_ONI_PRESSURE, native_cell(11, 5)) && !suppress(MA_ONI_PRESSURE, native_cell(13, 6)), "terrain ownership follows");
        require(st.maximum_relative_mass_error < 1e-12 && st.maximum_relative_energy_error < 1e-9, "terrain ledgers close");
        // A different catalogue rejects the image.
        ma::oni::colony::stop();
        auto altered = materials; altered[0].specific_heat_j_per_kg_k = 1001;
        bool rejected = false;
        try { ma::oni::colony::restore(config, altered, image, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !ma::oni::colony::active(), "catalogue mismatch rejects");
        auto corrupt = image; corrupt[corrupt.size() / 2] ^= 1; rejected = false;
        try { ma::oni::colony::restore(config, materials, corrupt, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "corrupt image rejects");
        // Frames posted while a step runs join the next step: no game time is lost.
        ma::oni::colony::restore(config, materials, image, 1);
        ma::oni::colony::advance(5, .2, 2); ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
        const auto merged = ma::oni::colony::status();
        require(merged.state == 1 && merged.native_ticket == 6 && std::abs(merged.time_seconds - s.time_seconds) < 1e-9, "queued frames keep game time");
        // A frame from the game thread faults the owner.
        ma::oni::colony::advance(7, .2, 1); ma::oni::colony::wait();
        require(ma::oni::colony::status().state == 2, "game-thread frame refused");
        ma::oni::colony::stop();
        std::cout << "COLONY PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "COLONY FAIL: " << e.what() << '\n';
        return 1;
    }
}
