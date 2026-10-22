// Colony owner control (docs/colony-owner.md). Made-up materials and
// inventories, not game data.
#include "../src/oni_colony.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <algorithm>
#include <array>
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
constexpr std::uint32_t oxygen = 3, carbon = 4, water = 5, steam = 6, brine = 7, vacuum = 9, W = 16, H = 12, GW = 20, GH = 15, X0 = 2, Y0 = 1;
std::uint32_t public_cell(std::uint32_t x, std::uint32_t y) { return (X0 + x) + (Y0 + y) * GW; }
int native_cell(std::uint32_t x, std::uint32_t y) { return static_cast<int>((X0 + x + 1) + (Y0 + y + 1) * (GW + 2)); }
void projection_control() {
    using namespace ma::experimental;
    constexpr std::uint32_t width = 64, height = 35;
    const auto saved = mac_kernel_default_options();
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .016, 1000),
        material("CarbonDioxide", 12, carbon, true, .044, 850), material("Water", 13, water, false, .018, 4000, 1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index = vacuum; cfg.grid_width = cfg.width = width; cfg.grid_height = cfg.height = height;
    cfg.maximum_timestep_s = .01;
    std::vector<MAOniColonyCell> cells(width * height);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i % 13 == 0) cells[i] = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
        else if (i % 7 == 0) cells[i] = {vacuum, MA_ONI_COLONY_FLUID, 0, 0, 0, 0};
        else cells[i] = {i / width >= height - 3 ? water : i % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID,
            i / width >= height - 3 ? 1000.f : 1.2f, 300, 3, 100};
    }
    ma::oni::colony::start(cfg, mats, cells, 1);
    const auto source = ma::oni::colony::checkpoint();
    std::vector<MAOniColonyCellView> expected(cells.size()), views(cells.size());
    std::vector<std::uint8_t> endpoint;
    for (const auto threads : {1u, 12u}) {
        ma::oni::colony::stop(); ma::oni::colony::restore(cfg, mats, source, 1);
        auto options = saved; options.threads = threads; options.minimum_blocks_per_thread = 1; set_mac_kernel_default_options(options);
        ma::oni::colony::advance(1, .01, 2); ma::oni::colony::wait();
        MAOniColonyStatus st{}; ma::oni::colony::copy(st, views.data(), static_cast<std::uint32_t>(views.size()));
        require(st.state == 1 && st.completed_frames == 1 && st.maximum_relative_mass_error < 1e-12 &&
            st.maximum_relative_energy_error < 1e-9, "projection control fault or open ledger");
        if (threads == 1) { expected = views; endpoint = ma::oni::colony::checkpoint(); }
        else require(std::memcmp(expected.data(), views.data(), views.size() * sizeof(MAOniColonyCellView)) == 0 &&
            ma::oni::colony::checkpoint() == endpoint, "projected views or continuation depend on workers");
    }
    ma::oni::colony::stop(); set_mac_kernel_default_options(saved);
    std::cout << "PASS multi-block colony projection: view bytes, cold continuation, germs and ledgers\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "--projection-only") == 0) { projection_control(); return 0; }
        require(argc == 1, "Usage: ma_oni_colony_tests [--projection-only]");
        projection_control();
        std::vector<MAOniColonyMaterial> materials{material("Oxygen", 11, oxygen, true, .016, 1000), material("CarbonDioxide", 12, carbon, true, .044, 850),
            material("Water", 13, water, false, .018, 4000, 1000), material("Steam", 14, steam, true, .018, 4000),
            material("SaltWater", 15, brine, false, .021, 4100, 1100)};
        // Salt water boils to steam and leaves 7% salt (hash 777, a solid).
        materials[4].high_temperature_k = 372.5; materials[4].high_target = 14; materials[4].high_byproduct = 777; materials[4].high_byproduct_fraction = .07;
        // Water boils to steam at 372.5 K and steam condenses back: a saturation pair.
        materials[2].high_temperature_k = 372.5; materials[2].high_target = 14; materials[2].low_target = 99;
        materials[3].low_temperature_k = 372.5; materials[3].low_target = 13;
        MAOniColonyConfig config{}; config.struct_size = sizeof(config); config.abi_version = MA_ONI_COLONY_ABI_VERSION;
        config.flags = MA_ONI_COLONY_PHYSICS_CONDUCTION; config.vacuum_index = vacuum; config.grid_width = GW; config.grid_height = GH;
        config.offset_x = X0; config.offset_y = Y0; config.width = W; config.height = H; config.gravity_m_per_s2 = 9.81; config.maximum_timestep_s = .01;
        std::vector<MAOniColonyCell> cells(W * H);
        for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) {
            auto& c = cells[x + y * W];
            const bool rock = x == 0 || x == W - 1 || y == 0 || y == 8 || (x == 11 && y == 5);
            if (rock) c = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
            else if (y >= 9) c = {vacuum, MA_ONI_COLONY_SPACE, 0, 0, 0, 0};
            else if (y <= 2 && x <= 7) c = {water, MA_ONI_COLONY_FLUID, 1000.f * (1.f + .01f * static_cast<float>(x % 3)), 290, 0, 0};
            else c = {x % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID, 1.2f + .1f * static_cast<float>(y), 300, 0, 0};
        }
        ma::oni::colony::start(config, materials, cells, 1);
        require(ma::oni::colony::active(), "colony active");
        // Suppression: owned fluid, solids, outside, and whole-rectangle transitions.
        using ma::oni::colony::suppress;
        require(suppress(MA_ONI_PRESSURE, native_cell(4, 4)) && !suppress(MA_ONI_PRESSURE, native_cell(0, 4)), "owned fluid cells only");
        require(!suppress(MA_ONI_PRESSURE, 0) && !suppress(MA_ONI_STATE_TRANSITION, native_cell(0, 4)) &&
            suppress(MA_ONI_STATE_TRANSITION, native_cell(4, 4)), "rectangle and transitions");
        require(ma::oni::colony::allow_native_transition(native_cell(3, 1), water, 200) &&
            !ma::oni::colony::allow_native_transition(native_cell(3, 1), water, 400) &&
            !ma::oni::colony::allow_native_transition(native_cell(4, 4), steam, 300), "only freezing runs natively on owned cells");
        // Transfers with receipts and a heat edit, committed at the next frame.
        const std::vector<MAOniColonyTransfer> transfers{{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, 7, 0, .5, 310, 0},
            {public_cell(9, 5), carbon, MA_ONI_COLONY_EXTRACT, 2, 8, 0, 3, 0, 0}, {public_cell(9, 5), 77, MA_ONI_COLONY_EXTRACT, 1, 9, 0, 1, 0, 0},
            {public_cell(6, 10), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 0, 1e-3, 300, 0}};
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
        std::cout << "evaporated " << s.evaporated_kg << " kg condensed " << s.condensed_kg << " kg skipped " << s.skipped_relaxations << '\n';
        require(s.completed_frames == 6 && s.committed_transfers == 4 && s.committed_heat == 1, "frames and commits");
        // The pool's surface evaporates into the air above it toward saturation.
        require(s.evaporated_kg > 0 && s.skipped_relaxations == 0, "phase change at the pool surface");
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
        // Native shadow: a body warms one owned gas cell by 2 K, a native
        // process takes 0.1 kg from a mixed gas cell and empties another; all
        // become booked owner inputs, and the takes leave as gas mixtures.
        {
            const auto mix = [](std::uint32_t x, std::uint32_t y) {
                std::uint32_t idx[8]; double m[8]; std::array<double, 3> t{};
                const auto n = std::min<std::uint32_t>(ma::oni::colony::composition(public_cell(x, y), idx, m, 8), 8);
                for (std::uint32_t q = 0; q < n; ++q) if (idx[q] != water) t[idx[q] == oxygen ? 0 : idx[q] == carbon ? 1 : 2] += m[q];
                return t; // oxygen, carbon dioxide, steam
            };
            const auto gas_totals = [&] {
                std::array<double, 3> t{};
                for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) { const auto c = mix(x, y); for (int q = 0; q < 3; ++q) t[q] += c[q]; }
                return t;
            };
            // Seal (14, 7) off: native empties it, and no neighbour refills it.
            const MAOniColonyTerrain seal[] = {{public_cell(13, 7), MA_ONI_COLONY_CLOSE}, {public_cell(14, 6), MA_ONI_COLONY_CLOSE}};
            ma::oni::colony::queue_terrain(seal); ma::oni::colony::advance(8, .2, 2); ma::oni::colony::wait();
            const std::size_t N = (GW + 2) * (GH + 2);
            std::vector<std::uint16_t> el[2]; std::vector<float> mass[2], temperature[2];
            ma::oni::colony::NativeCells nc; nc.pitch = GW + 2; nc.rows = GH + 2;
            for (int b = 0; b < 2; ++b) {
                el[b].assign(N, 0); mass[b].assign(N, 0); temperature[b].assign(N, 300);
                for (std::size_t c = 0; c < N; ++c) if (ma::oni::colony::owns(static_cast<int>(c))) el[b][c] = vacuum;
                nc.element[b] = el[b].data(); nc.mass[b] = mass[b].data(); nc.temperature[b] = temperature[b].data();
            }
            ma::oni::colony::write_shadow(nc);
            const auto warm = static_cast<std::size_t>(native_cell(4, 4)), drawn = static_cast<std::size_t>(native_cell(5, 5));
            require(el[1][warm] != vacuum && mass[1][warm] > 0 && mass[1][drawn] > .5f, "shadow holds the owner's cells");
            const double expected = mass[1][warm] * (el[1][warm] == oxygen ? 1000. : 850.) * 2;
            const auto emptied = static_cast<std::size_t>(native_cell(14, 7));
            const auto drawn_mix = mix(5, 5), emptied_mix = mix(14, 7), totals = gas_totals();
            require(drawn_mix[0] > 0 && drawn_mix[1] > 0 && emptied_mix[0] > 0 && emptied_mix[1] > 0, "mixed gas cells for native takes");
            const float drawn_before = mass[1][drawn], emptied_shadow = mass[1][emptied];
            // The emptied cell keeps its element at no mass, as ONI leaves a
            // cell whose last gas it deleted.
            temperature[1][warm] += 2; mass[1][drawn] -= .1f; mass[1][emptied] = 0;
            const double taken = static_cast<double>(drawn_before) - mass[1][drawn];
            // Native froze a pool cell and melted a rock cell beside the gas.
            const auto frozen = static_cast<std::size_t>(native_cell(3, 1)), melted = static_cast<std::size_t>(native_cell(0, 4));
            require(el[1][frozen] == water && el[1][melted] == 0, "freeze and melt cells");
            const double frozen_mass = mass[1][frozen];
            el[1][frozen] = 1; el[1][melted] = water; mass[1][melted] = 500; temperature[1][melted] = 280;
            const auto before = ma::oni::colony::status();
            ma::oni::colony::capture_shadow(nc);
            ma::oni::colony::advance(9, .2, 2); ma::oni::colony::wait();
            const auto after = ma::oni::colony::status();
            std::cout << "shadow heat " << after.native_heat_j << " J (expected " << expected << ") taken " << after.native_mass_out_kg
                      << " kg changes " << after.native_changes << '\n';
            if (after.state != 1) throw std::runtime_error(std::string("shadow faulted: ") + after.error);
            // The take splits over the cell's gases (with the pool's vapour) by
            // mass; the emptied cell's minority gas leaves with its bulk.
            const auto left = gas_totals(); const double share = taken / (drawn_mix[0] + drawn_mix[1] + drawn_mix[2]);
            for (int q = 0; q < 2; ++q)
                require(std::abs(left[q] - (totals[q] - share * drawn_mix[q] - emptied_mix[q])) < 1e-9 * totals[q], "native takes leave as gas mixtures");
            const auto residue = mix(14, 7);
            require(residue[0] == 0 && residue[1] == 0 && residue[2] == 0, "an emptied cell keeps no gas");
            const double emptied_gas = emptied_mix[0] + emptied_mix[1] + emptied_mix[2];
            require(after.native_changes == 5 && std::abs(after.native_heat_j - expected) < 1e-6 * expected &&
                std::abs(after.native_mass_out_kg - taken - frozen_mass - emptied_shadow) < 1e-3 && after.committed_heat == before.committed_heat + 1 &&
                std::abs(after.extracted_kg - before.extracted_kg - taken - frozen_mass - emptied_gas) < 1e-3, "shadow changes are booked");
            require(after.closed_cells == before.closed_cells + 1 && after.opened_cells == before.opened_cells + 1 &&
                std::abs(after.injected_kg - before.injected_kg - 500) < 1e-3, "frozen cell closed, melted cell opened with its water");
            require(!suppress(MA_ONI_PRESSURE, native_cell(3, 1)) && suppress(MA_ONI_PRESSURE, native_cell(0, 4)), "ownership follows freeze and melt");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "shadow ledgers close");
            // Taking a sealed cell's last oxygen takes the rounding-sized
            // carbon dioxide trace beside it too.
            const MAOniColonyTransfer fill[] = {{public_cell(14, 7), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 0, .5, 300, 0},
                                                {public_cell(14, 7), carbon, MA_ONI_COLONY_INJECT, 0, -1, 0, 1e-12, 300, 0}};
            ma::oni::colony::queue_transfers(fill); ma::oni::colony::advance(10, .2, 2); ma::oni::colony::wait();
            const auto filled = mix(14, 7);
            require(filled[0] > .4 && filled[1] > 0, "sealed cell holds oxygen and a carbon dioxide trace");
            const MAOniColonyTransfer breath{public_cell(14, 7), oxygen, MA_ONI_COLONY_EXTRACT, 0, -1, 0, 1, 0, 0};
            ma::oni::colony::queue_transfers(std::span(&breath, 1)); ma::oni::colony::advance(11, .2, 2); ma::oni::colony::wait();
            const auto drained = mix(14, 7); const auto end = ma::oni::colony::status();
            require(end.state == 1 && drained[0] == 0 && drained[1] == 0 && drained[2] == 0, "the trace leaves with the last oxygen");
            require(end.maximum_relative_mass_error < 1e-12 && end.maximum_relative_energy_error < 1e-9, "trace ledgers close");
            // A queued close leaves the cell to ONI at once: the shadow keeps
            // the solid that replaced the gas. A solid gone again before the
            // owner closes the cell (falling sand) cancels the close.
            const auto falling = static_cast<std::size_t>(native_cell(10, 4));
            const MAOniColonyTerrain close{public_cell(10, 4), MA_ONI_COLONY_CLOSE};
            ma::oni::colony::queue_terrain(std::span(&close, 1));
            require(!ma::oni::colony::owns(native_cell(10, 4)) && !suppress(MA_ONI_PRESSURE, native_cell(10, 4)), "a closing cell is ONI's");
            for (int b = 0; b < 2; ++b) { el[b][falling] = 1; mass[b][falling] = 500; temperature[b][falling] = 300; }
            ma::oni::colony::write_shadow(nc);
            require(el[1][falling] == 1 && mass[1][falling] == 500, "the shadow keeps the solid in a closing cell");
            el[1][falling] = oxygen; mass[1][falling] = 1.2f;
            const auto held = ma::oni::colony::status();
            ma::oni::colony::capture_shadow(nc);
            require(ma::oni::colony::owns(native_cell(10, 4)), "the solid's departure cancels the close");
            ma::oni::colony::advance(12, .2, 2); ma::oni::colony::wait();
            const auto after_fall = ma::oni::colony::status();
            require(after_fall.state == 1 && after_fall.closed_cells == held.closed_cells && after_fall.injected_kg == held.injected_kg,
                "nothing closed or injected for the cancelled close");
        }
        // A different catalogue rejects the image.
        ma::oni::colony::stop();
        auto altered = materials; altered[0].specific_heat_j_per_kg_k = 1001;
        bool rejected = false;
        try { ma::oni::colony::restore(config, altered, image, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !ma::oni::colony::active(), "catalogue mismatch rejects");
        auto corrupt = image; corrupt[corrupt.size() / 2] ^= 1; rejected = false;
        try { ma::oni::colony::restore(config, materials, corrupt, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "corrupt image rejects");
        // Inputs in flight at a save: an injection survives the load without
        // its callback; an extraction answering a callback is asked again.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer queued[] = {{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, 42, 0, .5, 300, 0},
                                                  {public_cell(10, 5), oxygen, MA_ONI_COLONY_EXTRACT, 1, 43, 0, .3, 0, 0}};
            ma::oni::colony::queue_transfers(queued);
            const auto saved = ma::oni::colony::checkpoint();
            ma::oni::colony::stop(); ma::oni::colony::restore(config, materials, saved, 1);
            const auto before = ma::oni::colony::status();
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(8); const auto n = ma::oni::colony::copy_receipts(got.data(), 8);
            require(after.state == 1 && std::abs(after.injected_kg - before.injected_kg - .5) < 1e-6 && after.extracted_kg == before.extracted_kg && n == 0,
                "queued inputs survive a save without callbacks");
            ma::oni::colony::stop();
        }
        // Hot salt water converts to steam over steps and its salt leaves as debris.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer hot{public_cell(6, 6), brine, MA_ONI_COLONY_INJECT, 0, -1, 0, 20, 420, 0};
            ma::oni::colony::queue_transfers(std::span(&hot, 1));
            for (std::uint64_t frame = 5; frame <= 7; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(16); const auto n = ma::oni::colony::copy_receipts(got.data(), 16);
            double salt = 0; for (std::uint32_t q = 0; q < n; ++q) if (got[q].kind == MA_ONI_COLONY_BYPRODUCT && got[q].callback == 777) salt += got[q].mass_kg;
            std::cout << "converted " << after.converted_kg << " kg, salt " << salt << " kg state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && after.converted_kg > 1 && std::abs(salt - .07 * after.converted_kg) < 1e-3 * after.converted_kg, "salt water boils to steam and salt");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "conversion ledgers close");
            ma::oni::colony::stop();
        }
        // Cold water the projection hides (a puddle on rock under gas) freezes
        // into its solid as debris, booked like a conversion byproduct.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer cold{public_cell(11, 6), water, MA_ONI_COLONY_INJECT, 0, -1, 0, 5, 150, 0};
            ma::oni::colony::queue_transfers(std::span(&cold, 1));
            for (std::uint64_t frame = 5; frame <= 8; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(16); const auto n = ma::oni::colony::copy_receipts(got.data(), 16);
            double ice = 0; for (std::uint32_t q = 0; q < n; ++q) if (got[q].kind == MA_ONI_COLONY_BYPRODUCT && got[q].callback == 99) ice += got[q].mass_kg;
            std::cout << "frozen " << ice << " kg of 5 kg\n";
            require(after.state == 1 && ice > 2 && ice <= 5.0001, "hidden cold water freezes into debris");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "freezing ledgers close");
            ma::oni::colony::stop();
        }
        // A liquid trace below binary64's normal range (a rounding product
        // of the kernel) leaves the cell's reading as it was.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const double plain = ma::oni::colony::trace_reading_temperature(public_cell(4, 7), water, 0);
            const double traced = ma::oni::colony::trace_reading_temperature(public_cell(4, 7), water, 1e-321);
            std::cout << "subnormal water trace reads " << traced << " K against " << plain << " K\n";
            require(traced == plain, "a subnormal liquid trace keeps the cell readable");
            ma::oni::colony::stop();
        }
        // A cell whose reading the reference EOS refuses keeps its previous
        // view for the frame instead of faulting the owner.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            std::vector<MAOniColonyCellView> before(cells.size()), after(cells.size()); MAOniColonyStatus shown{};
            ma::oni::colony::copy(shown, before.data(), static_cast<std::uint32_t>(before.size()));
            ma::oni::colony::refuse_reading(public_cell(4, 7));
            ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
            ma::oni::colony::copy(shown, after.data(), static_cast<std::uint32_t>(after.size()));
            ma::oni::colony::refuse_reading(-1);
            const std::size_t k = 4 + 7 * W;
            std::cout << "refused reading: state " << shown.state << " " << shown.error << "\n";
            require(shown.state == 1 && std::memcmp(&before[k], &after[k], sizeof(MAOniColonyCellView)) == 0, "a refused reading keeps the cell's previous view");
            ma::oni::colony::stop();
        }
        // An injection too small to change its cell in binary64 (1e-14 kg on
        // a tonne of water) is dropped with a zero receipt instead of faulting.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer speck{public_cell(3, 1), water, MA_ONI_COLONY_INJECT, 0, 55, 0, 1e-14, 300, 0};
            ma::oni::colony::queue_transfers(std::span(&speck, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(8); const auto n = ma::oni::colony::copy_receipts(got.data(), 8);
            bool zero = false; for (std::uint32_t q = 0; q < n; ++q) zero = zero || (got[q].callback == 55 && got[q].mass_kg == 0);
            std::cout << "unrepresentable injection: state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && zero && after.maximum_relative_mass_error < 1e-12, "an unrepresentable injection is dropped with a zero receipt");
            ma::oni::colony::stop();
        }
        // A heat edit the cell cannot take (here more cooling than its gas
        // holds) stays queued, unbooked, instead of faulting the owner.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyHeat cold{public_cell(4, 7), 0, -1e12, 0};
            ma::oni::colony::queue_heat(std::span(&cold, 1));
            for (std::uint64_t frame = 5; frame <= 6; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::cout << "impossible heat edit: state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && after.maximum_relative_energy_error < 1e-9, "an untakeable heat edit is deferred, not faulted");
            ma::oni::colony::stop();
        }
        // A sealed cell's gas goes to the reservoir and seeps back when the
        // cell reopens.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTerrain walls[] = {{public_cell(13, 7), MA_ONI_COLONY_CLOSE}, {public_cell(14, 6), MA_ONI_COLONY_CLOSE}};
            ma::oni::colony::queue_terrain(walls);
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const MAOniColonyTerrain seal{public_cell(14, 7), MA_ONI_COLONY_CLOSE};
            ma::oni::colony::queue_terrain(std::span(&seal, 1));
            ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
            const auto sealed = ma::oni::colony::status();
            require(sealed.state == 1 && sealed.reservoir_kg > 1, "sealed gas went to the reservoir");
            const MAOniColonyTerrain dig{public_cell(14, 7), MA_ONI_COLONY_OPEN};
            ma::oni::colony::queue_terrain(std::span(&dig, 1));
            ma::oni::colony::advance(7, .2, 2); ma::oni::colony::wait();
            const auto reopened = ma::oni::colony::status();
            std::cout << "reservoir " << sealed.reservoir_kg << " kg, released " << reopened.released_kg << " kg\n";
            require(reopened.state == 1 && reopened.released_kg > 1 && reopened.reservoir_kg < 1e-6, "the reservoir seeps back");
            require(reopened.maximum_relative_mass_error < 1e-12 && reopened.maximum_relative_energy_error < 1e-9, "reservoir ledgers close");
            ma::oni::colony::stop();
        }
        // A pump takes the gas mixture around it: one receipt per constituent.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer pump{public_cell(9, 5), 0, MA_ONI_COLONY_EXTRACT_GAS, 2, 11, 0, 2, 0, 0};
            ma::oni::colony::queue_transfers(std::span(&pump, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            std::vector<MAOniColonyReceipt> got(8); const auto n = ma::oni::colony::copy_receipts(got.data(), 8);
            double total = 0; bool oxygen_seen = false, carbon_seen = false;
            for (std::uint32_t q = 0; q < n; ++q) if (got[q].callback == 11) {
                total += got[q].mass_kg; oxygen_seen |= got[q].native_index == oxygen; carbon_seen |= got[q].native_index == carbon;
                require(got[q].kind == MA_ONI_COLONY_EXTRACT_GAS && got[q].temperature_k > 250, "mixture receipt");
            }
            std::cout << "pumped " << total << " kg in " << n << " receipts\n";
            require(oxygen_seen && carbon_seen && std::abs(total - 2) < 1e-5, "the pump takes the mixture");
            require(ma::oni::colony::status().maximum_relative_mass_error < 1e-12, "pump ledger closes");
            ma::oni::colony::stop();
        }
        // ONI's element replacement with a fluid empties the cell first.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            std::uint32_t idx[8]; double m[8]; double held = 0;
            const auto n = ma::oni::colony::composition(public_cell(9, 6), idx, m, 8);
            for (std::uint32_t q = 0; q < n; ++q) held += m[q];
            const auto before = ma::oni::colony::status().extracted_kg;
            const MAOniColonyTerrain clear{public_cell(9, 6), MA_ONI_COLONY_CLEAR};
            ma::oni::colony::queue_terrain(std::span(&clear, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto cs = ma::oni::colony::status();
            std::cout << "cleared " << cs.extracted_kg - before << " kg of the cell's " << held << " kg\n";
            require(cs.state == 1 && std::abs(cs.extracted_kg - before - held) <= 1e-9 * held && cs.maximum_relative_mass_error < 1e-12,
                "a replaced cell loses its fluid");
            ma::oni::colony::stop();
        }
        // Germs injected with gas travel with it and leave with extracted mass.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            MAOniColonyTransfer sneeze{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 3, .5, 300, 1e6};
            ma::oni::colony::queue_transfers(std::span(&sneeze, 1));
            for (std::uint64_t frame = 5; frame <= 8; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            std::vector<MAOniColonyCellView> germ_views(W * H); MAOniColonyStatus gs{};
            ma::oni::colony::copy(gs, germ_views.data(), static_cast<std::uint32_t>(germ_views.size()));
            double total = 0; std::size_t holding = 0;
            for (const auto& v : germ_views) if (v.disease == 3) { total += v.disease_count; ++holding; }
            const MAOniColonyTransfer breath{public_cell(10, 5), oxygen, MA_ONI_COLONY_EXTRACT, 1, 77, 0, .05, 0, 0};
            ma::oni::colony::queue_transfers(std::span(&breath, 1));
            ma::oni::colony::advance(9, .2, 2); ma::oni::colony::wait();
            std::vector<MAOniColonyReceipt> got(16); const auto n = ma::oni::colony::copy_receipts(got.data(), 16);
            std::int32_t breathed = 0; for (std::uint32_t q = 0; q < n; ++q) if (got[q].callback == 77 && got[q].disease == 3) breathed += got[q].disease_count;
            std::cout << "germs " << total << " in " << holding << " cells, breathed " << breathed << '\n';
            require(gs.state == 1 && holding > 1 && std::abs(total - 1e6) <= 1e6 * 1e-3 && breathed > 0, "germs travel with gas and leave with it");
            ma::oni::colony::stop();
        }
        // An airflow tile column between a pool under air and thinner air
        // passes the gas but no liquid. The column is rock at import and
        // opens as tiles on the first frame.
        {
            auto small = config; small.width = 10; small.height = 8;
            std::vector<MAOniColonyCell> chamber(10 * 8);
            for (std::uint32_t y = 0; y < 8; ++y) for (std::uint32_t x = 0; x < 10; ++x) {
                auto& c = chamber[x + y * 10];
                if (x == 0 || x == 9 || y == 0 || y == 7 || x == 4) c = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
                else if (x < 4 && y <= 3) c = {water, MA_ONI_COLONY_FLUID, 1000, 290, 0, 0};
                else c = {oxygen, MA_ONI_COLONY_FLUID, x < 4 ? 1.2f : .4f, 290, 0, 0};
            }
            ma::oni::colony::start(small, materials, chamber, 1);
            const auto at = [&](std::uint32_t x, std::uint32_t y) { return (X0 + x) + (Y0 + y) * GW; };
            std::vector<MAOniColonyTerrain> tiles;
            for (std::uint32_t y = 1; y <= 6; ++y) { tiles.push_back({at(4, y), MA_ONI_COLONY_OPEN}); tiles.push_back({at(4, y), MA_ONI_COLONY_BLOCK_LIQUID}); }
            ma::oni::colony::queue_terrain(tiles);
            const auto right = [&](bool liquid, std::uint32_t from) {
                double total = 0; std::uint32_t idx[8]; double m[8];
                for (std::uint32_t y = 1; y <= 6; ++y) for (std::uint32_t x = from; x <= 8; ++x) {
                    const auto n = ma::oni::colony::composition(at(x, y), idx, m, 8);
                    for (std::uint32_t q = 0; q < n; ++q) if ((idx[q] == water) == liquid) total += m[q];
                }
                return total;
            };
            const double gas_before = right(false, 5);
            for (std::uint64_t frame = 1; frame <= 8; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::cout << "airflow tiles: liquid in or beyond " << right(true, 4) << " kg, gas beyond " << gas_before << " -> " << right(false, 5)
                      << " kg, state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && right(true, 4) == 0, "liquid crossed an airflow tile");
            require(right(false, 5) > gas_before + .5, "gas did not pass the airflow tiles");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "airflow tile ledgers close");
            ma::oni::colony::stop();
        }
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
