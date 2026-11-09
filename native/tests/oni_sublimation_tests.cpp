// Natural sublimation control (docs/colony-owner.md#natural-sublimation).
// Made-up materials and rules, not game data. ONI's own DoSublimation is
// modelled here from its documented behaviour; the adapter calls it on the
// owner's shadow exactly as the native backend does.
#include "../src/oni_colony.hpp"
#include "../src/oni_sublimation.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace ma::oni::colony;
namespace sub = ma::oni::sublimation;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
constexpr std::uint32_t oxygen = 3, polluted_oxygen = 4, polluted_water = 5, vacuum = 9,
    oxylite = 20, polluted_dirt = 21, rock = 22, odd = 23, odd_product = 24;
constexpr std::uint32_t W = 5, H = 4, P = W + 2;
MAOniColonyMaterial material(const char* id, std::int32_t hash, std::uint32_t index, bool gas, double molar, double c, double density = 0) {
    MAOniColonyMaterial m{}; m.struct_size = sizeof(m); m.abi_version = MA_ONI_COLONY_ABI_VERSION; m.element_hash = hash; m.native_index = index;
    m.phase = gas ? 1 : 2; m.molar_mass_kg_per_mol = molar; m.specific_heat_j_per_kg_k = c; m.conductivity_w_per_m_k = gas ? .02 : .5;
    m.density_kg_per_m3 = density; m.low_temperature_k = gas ? 50 : 250; m.high_temperature_k = gas ? 1e4 : 700;
    std::snprintf(m.id, sizeof(m.id), "%s", id);
    return m;
}
std::uint32_t public_cell(std::uint32_t x, std::uint32_t y) { return x + y * W; }
std::size_t native_cell(std::uint32_t x, std::uint32_t y) { return (x + 1) + (y + 1) * P; }
MAOniColonySublimation record(std::uint32_t element, std::uint32_t kind, std::uint32_t target, float rate, float efficiency, float probability, float offgas) {
    MAOniColonySublimation r{}; r.struct_size = sizeof(r); r.abi_version = MA_ONI_COLONY_SUBLIMATION_VERSION;
    r.element_index = element; r.kind = kind; r.target_index = target;
    r.rate_kg_per_s = rate; r.efficiency = efficiency; r.probability = probability; r.offgas_fraction = offgas;
    return r;
}
const std::vector<MAOniColonySublimation> records{
    record(oxylite, MA_ONI_COLONY_SUBLIMATES_SOLID, oxygen, .4f, .5f, 1, 0),
    record(polluted_dirt, MA_ONI_COLONY_SUBLIMATES_SOLID, polluted_oxygen, .1f, .2f, 1, 0),
    record(odd, MA_ONI_COLONY_SUBLIMATES_SOLID, odd_product, .4f, .5f, 1, 0),
    record(polluted_water, MA_ONI_COLONY_SUBLIMATES_LIQUID, polluted_oxygen, 0, 1, 1, .001f)};

// ONI's DoSublimation as the adapter meets it (docs/colony-owner.md#natural-sublimation):
// the gate, the four neighbours above, right, left and below, a take of
// rate x 0.2 s, a tile used up converting in place, the pressure cap for the
// cell's own element, germs moving in proportion.
struct Model { NativeCells* n = nullptr; std::uint32_t seed = 1; int calls = 0; };
bool model_sublimation(void* data, void*, int cell, const void* definition) {
    auto& m = *static_cast<Model*>(data); ++m.calls; auto& n = *m.n;
    check((static_cast<const std::uint8_t*>(definition)[4] & 3u) == 0, "the adapter passes a vacuum definition: no displacement");
    const auto g = static_cast<std::size_t>(cell);
    if (!(n.mass[1][g] < 1.8f)) return false;
    bool any = false;
    for (const auto c : {g + n.pitch, g + 1, g - 1, g - n.pitch}) {
        const auto e = n.element[1][c];
        const auto rule = std::find_if(records.begin(), records.end(), [&](const auto& r) { return r.element_index == e && r.kind == MA_ONI_COLONY_SUBLIMATES_SOLID; });
        if (rule == records.end() || sub::draw(m.seed) > rule->probability) continue;
        const float step = .2f * rule->rate_kg_per_s; float amount = std::min(step, n.mass[1][c]);
        if (step > n.mass[1][c] - amount) { n.element[1][c] = static_cast<std::uint16_t>(rule->target_index); n.mass[1][c] *= rule->efficiency; any = true; continue; }
        if (n.element[1][g] == rule->target_index) {
            const float made = rule->efficiency * amount, room = 1.8f - n.mass[1][g];
            if (made > room) amount *= room / made;
        } else n.element[1][g] = static_cast<std::uint16_t>(rule->target_index);
        const float produced = rule->efficiency * amount, total = n.mass[1][g] + produced;
        const int moved = n.disease[1][c] == 255 ? 0 : static_cast<int>(amount / n.mass[1][c] * static_cast<float>(n.disease_count[1][c]));
        n.temperature[1][g] = (n.mass[1][g] * n.temperature[1][g] + produced * n.temperature[1][c]) / total; n.mass[1][g] = total;
        if (moved > 0) { n.disease[1][g] = n.disease[1][c]; n.disease_count[1][g] += moved; }
        n.mass[1][c] -= amount; n.disease_count[1][c] -= moved;
        if (n.disease_count[1][c] <= 0) { n.disease[1][c] = 255; n.disease_count[1][c] = 0; }
        any = true;
    }
    return any;
}

double species(std::uint32_t native) {
    double total = 0;
    for (std::uint32_t c = 0; c < W * H; ++c) {
        std::uint32_t indices[8]{}; double masses[8]{};
        const auto count = composition(c, indices, masses, 8);
        for (std::uint32_t k = 0; k < count; ++k) if (indices[k] == native) total += masses[k];
    }
    return total;
}

void rule_control() {
    std::uint32_t seed = 1;
    check(sub::draw(seed) == 41.f / 32767.f && seed == 2745024u, "the generator is ONI's (the C runtime's first value is 41)");
    const sub::Rule water{sub::Kind::liquid, 7, 0, 1, .01f, .001f};
    auto o = sub::offgas(water, 900, 0, .1f);
    check(o.kind == sub::Offgas::Kind::surface && std::abs(o.amount - .9f) < 1e-6f && o.produced == o.amount, "a deep liquid gives off its fraction");
    o = sub::offgas(water, 900, 0, 1.5f);
    check(o.kind == sub::Offgas::Kind::surface && std::abs(o.produced - .3f) < 1e-6f && o.amount < .9f, "the cap above holds the product");
    o = sub::offgas(water, 2000, 0, 0);
    check(o.amount == 1 && o.produced == 1, "one event takes at most a kilogram");
    o = sub::offgas(water, .005f, 0, 0);
    check(o.kind == sub::Offgas::Kind::film && o.amount == .005f && o.from_below == 0, "a film converts whole");
    o = sub::offgas(water, 5, 900, 0);
    check(o.kind == sub::Offgas::Kind::surface && std::abs(o.amount - .9f) < 1e-6f, "a shallow surface over a deeper cell gives off the deeper cell's share");
    o = sub::offgas(water, .5f, 1000, 0);
    check(o.kind == sub::Offgas::Kind::film && o.amount == .5f && std::abs(o.from_below - .5f) < 1e-6f, "a shallow surface used up takes the rest from below");
    const sub::Rule rock_rule{sub::Kind::solid, 3, .4f, .5f, 1, 0};
    auto y = sub::yield(rock_rule, vacuum, oxylite, 200, 2, 1000, oxylite, 199.92f, 2, 999);
    check(!y.converted && y.removed == 200.f - 199.92f && y.gas == .5 * y.removed && y.germs == 1, "a take yields half its mass and its germs");
    y = sub::yield(rock_rule, vacuum, oxylite, .1f, 255, 0, oxygen, .05f, 255, 0);
    check(y.converted && y.gas == 0, "a tile used up converts in place");
    y = sub::yield(rock_rule, vacuum, oxylite, 200, 255, 0, oxylite, 200, 255, 0);
    check(y.removed == 0 && y.gas == 0, "an unchanged tile yields nothing");
    std::cout << "PASS sublimation rules: generator, off-gassing, film, cap, yield\n";
}

struct Case {
    const char* name; std::uint32_t tile; float tile_kg; float gas_kg; std::uint8_t impermeable;
    double oxygen_after; bool called, used_up;
};

void colony_control() {
    // y3 rock; y2 rock, gas G, rock, gas A, rock; y1 rock, tile T, rock,
    // polluted water L, rock; y0 rock. T sublimates only into G (its other
    // neighbours are rock); L off-gasses into A.
    const std::array<Case, 7> cases{{
        {"take", oxylite, 200, .1f, 0, .14, true, false},
        {"cap", oxylite, 200, 1.79f, 0, 1.8, true, false},
        {"at cap", oxylite, 200, 1.8f, 0, 1.8, false, false},
        {"vacuum", oxylite, 200, 0, 0, .04, true, false},
        {"gas-impermeable vacuum", oxylite, 200, 0, 1, 0, false, false},
        {"used up", oxylite, .1f, .1f, 0, .15, true, true},
        {"product the owner cannot hold", odd, 200, .1f, 0, .1, false, false}}};
    for (const auto& k : cases) {
        set_sublimation(records);
        MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = 1;
        cfg.flags = 12; cfg.vacuum_index = vacuum; cfg.grid_width = cfg.width = W; cfg.grid_height = cfg.height = H; cfg.maximum_timestep_s = .1;
        std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .032, 1005),
            material("ContaminatedOxygen", 12, polluted_oxygen, true, .032, 1005),
            material("DirtyWater", 13, polluted_water, false, .018, 4179, 1000)};
        std::vector<MAOniColonyCell> cells(W * H, MAOniColonyCell{rock, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
        cells[public_cell(1, 2)] = k.gas_kg > 0 ? MAOniColonyCell{oxygen, MA_ONI_COLONY_FLUID, k.gas_kg, 300, 0, 0} : MAOniColonyCell{vacuum, MA_ONI_COLONY_FLUID, 0, 0, 0, 0};
        cells[public_cell(3, 2)] = {oxygen, MA_ONI_COLONY_FLUID, .1f, 300, 0, 0};
        cells[public_cell(3, 1)] = {polluted_water, MA_ONI_COLONY_FLUID, 900, 300, 0, 0};
        start(cfg, mats, cells, 1);
        std::array<std::vector<std::uint16_t>, 2> element; std::array<std::vector<float>, 2> mass, temperature;
        std::array<std::vector<std::uint8_t>, 2> disease; std::array<std::vector<std::int32_t>, 2> germs;
        std::vector<std::uint8_t> properties(P * (H + 2), 0); std::vector<float> flow(P * (H + 2), 0);
        NativeCells native{}; native.pitch = P; native.rows = H + 2;
        for (int b = 0; b < 2; ++b) {
            element[b].assign(P * (H + 2), static_cast<std::uint16_t>(rock)); mass[b].assign(P * (H + 2), 1000); temperature[b].assign(P * (H + 2), 300);
            disease[b].assign(P * (H + 2), 255); germs[b].assign(P * (H + 2), 0);
            element[b][native_cell(1, 1)] = static_cast<std::uint16_t>(k.tile); mass[b][native_cell(1, 1)] = k.tile_kg; temperature[b][native_cell(1, 1)] = 310;
            disease[b][native_cell(1, 1)] = 2; germs[b][native_cell(1, 1)] = 1000000;
            // ONI holds the owned cells as fluid (a solid there would be closed).
            for (const auto c : {native_cell(1, 2), native_cell(3, 2), native_cell(3, 1)}) element[b][c] = static_cast<std::uint16_t>(vacuum);
            native.element[b] = element[b].data(); native.mass[b] = mass[b].data(); native.temperature[b] = temperature[b].data();
            native.disease[b] = disease[b].data(); native.disease_count[b] = germs[b].data();
        }
        properties[native_cell(1, 2)] = k.impermeable;
        write_shadow(native);
        const auto shown_element = element[1][native_cell(1, 2)];
        const float shown_mass = mass[1][native_cell(1, 2)], shown_temperature = temperature[1][native_cell(1, 2)];
        std::uint32_t seed = 7;
        SublimationFrame frame{native, properties.data(), flow.data(), &seed};
        arm_sublimation(frame);
        Model model; model.n = &native;
        sublimate(&model, nullptr, static_cast<int>(native_cell(1, 2)), &model_sublimation);
        sublimate(&model, nullptr, static_cast<int>(native_cell(3, 1)), &model_sublimation);
        sublimate(&model, nullptr, static_cast<int>(native_cell(3, 2)), &model_sublimation); // no solid beside it
        disarm_sublimation();
        sublimate(&model, nullptr, static_cast<int>(native_cell(1, 2)), &model_sublimation); // disarmed: nothing
        check(model.calls == (k.called ? 1 : 0), "ONI's step runs once for a cell beside a sublimating tile, only while armed");
        check(element[1][native_cell(1, 2)] == shown_element && mass[1][native_cell(1, 2)] == shown_mass &&
            temperature[1][native_cell(1, 2)] == shown_temperature && disease[1][native_cell(1, 2)] == 255 && germs[1][native_cell(1, 2)] == 0,
            "the receiving cell's shadow is put back");
        const float tile_after = mass[1][native_cell(1, 1)];
        capture_shadow(native);
        advance(1, .2, 2); wait();
        const auto st = status();
        const double water_taken = .9, produced_water = .9;
        // ONI's take, as the native arrays recorded it; a tile used up became
        // half its mass of oxygen in its own cell.
        const double expected = !k.called ? static_cast<double>(k.gas_kg) : k.used_up ? static_cast<double>(k.gas_kg) + static_cast<double>(k.tile_kg * .5f) :
            static_cast<double>(k.gas_kg) + .5 * (static_cast<double>(k.tile_kg) - tile_after);
        check(std::abs(expected - k.oxygen_after) < 1e-3, "the case's oxygen is as documented");
        const double held = species(oxygen) - .1; // the water pocket's oxygen
        if (std::abs(held - expected) > 1e-6) {
            std::fprintf(stderr, "%s: oxygen %.9g expected %.9g (tile %.9g -> %.9g)\n", k.name, held, expected, static_cast<double>(k.tile_kg), static_cast<double>(tile_after));
            check(false, "the tile's oxygen reached the owner");
        }
        if (k.name == std::string("cap")) check(std::abs(held - 1.8) < 1e-5, "the receiving cell is held at the pressure cap (to the tile mass's binary32 step)");
        check(std::abs(species(polluted_water) - (900 - water_taken)) < 1e-6 && std::abs(species(polluted_oxygen) - produced_water) < 1e-6,
            "polluted water gives off polluted oxygen at its surface");
        check(std::abs(flow[native_cell(3, 1)] - .9f) < 1e-6f, "the inspector's flow shows the liquid's take");
        check(k.used_up == (st.opened_cells == 1), "a used-up tile opens to the owner");
        check(st.maximum_relative_mass_error < 1e-12 && st.maximum_relative_energy_error < 1e-12 && st.state == 1, "ledgers close");
        const auto totals = sublimation_totals();
        check(totals.events == (k.called ? 2u : 1u) && totals.tiles_used_up == (k.used_up ? 1u : 0u) &&
            std::abs(totals.liquid_kg - water_taken) < 1e-6, "totals count the session's events");
        if (k.name == std::string("take")) {
            std::vector<std::uint32_t> all(W * H); for (std::uint32_t c = 0; c < W * H; ++c) all[c] = c;
            MAOniColonyPassiveStamp stamp{}; std::vector<MAOniColonyPassiveRow> passives;
            check(read_passives(0, 0, all, stamp, passives), "passive snapshot available");
            double carried = 0; for (const auto& row : passives) if (row.identity == 3) carried += row.quantity;
            check(carried == 400, "the tile's germs ride its gas into the owner");
        }
        if (k.called && !k.used_up) check(germs[1][native_cell(1, 1)] < 1000000 && (k.gas_kg >= 1.79f || germs[1][native_cell(1, 1)] == 1000000 - 400) && std::abs(totals.tile_kg - (static_cast<double>(k.tile_kg) - tile_after)) < 1e-9,
            "the tile gives up mass and germs");
        stop();
    }
    set_sublimation({});
    std::cout << "PASS natural sublimation: take, cap, vacuum, impermeable, used up, unheld product, polluted water, ledgers\n";
}

// The adapter's cost in post-processing, on a Terra-sized 256 x 384 grid:
// every cell owned gas except one row in eight of rock with an oxylite tile in
// every 32nd cell. Times one pass over every owned cell, armed and disarmed.
void cost_control() {
    constexpr std::uint32_t GW = 256, GH = 384, NP = GW + 2;
    set_sublimation(records);
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = 1;
    cfg.flags = 12; cfg.vacuum_index = vacuum; cfg.grid_width = cfg.width = GW; cfg.grid_height = cfg.height = GH; cfg.maximum_timestep_s = .1;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .032, 1005),
        material("ContaminatedOxygen", 12, polluted_oxygen, true, .032, 1005), material("DirtyWater", 13, polluted_water, false, .018, 4179, 1000)};
    std::vector<MAOniColonyCell> cells(GW * GH, MAOniColonyCell{oxygen, MA_ONI_COLONY_FLUID, 1, 300, 0, 0});
    for (std::uint32_t y = 0; y < GH; y += 8) for (std::uint32_t x = 0; x < GW; ++x) cells[x + y * GW] = {rock, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
    start(cfg, mats, cells, 1);
    std::array<std::vector<std::uint16_t>, 2> element; std::array<std::vector<float>, 2> mass, temperature;
    std::array<std::vector<std::uint8_t>, 2> disease; std::array<std::vector<std::int32_t>, 2> germs;
    std::vector<std::uint8_t> properties(NP * (GH + 2), 0); std::vector<float> flow(NP * (GH + 2), 0);
    NativeCells native{}; native.pitch = NP; native.rows = GH + 2;
    for (int b = 0; b < 2; ++b) {
        element[b].assign(NP * (GH + 2), static_cast<std::uint16_t>(rock)); mass[b].assign(NP * (GH + 2), 1000); temperature[b].assign(NP * (GH + 2), 300);
        disease[b].assign(NP * (GH + 2), 255); germs[b].assign(NP * (GH + 2), 0);
        for (std::uint32_t y = 0; y < GH; ++y) for (std::uint32_t x = 0; x < GW; ++x) {
            const auto c = (x + 1) + (y + 1) * NP;
            if (y % 8) element[b][c] = static_cast<std::uint16_t>(vacuum);
            else if (x % 32 == 0) element[b][c] = static_cast<std::uint16_t>(oxylite);
        }
        native.element[b] = element[b].data(); native.mass[b] = mass[b].data(); native.temperature[b] = temperature[b].data();
        native.disease[b] = disease[b].data(); native.disease_count[b] = germs[b].data();
    }
    write_shadow(native);
    std::uint32_t seed = 7; Model model; model.n = &native;
    const auto pass = [&] {
        const auto begun = std::chrono::steady_clock::now();
        for (std::uint32_t y = 0; y < GH; ++y) if (y % 8) for (std::uint32_t x = 0; x < GW; ++x)
            sublimate(&model, nullptr, static_cast<int>((x + 1) + (y + 1) * NP), &model_sublimation);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
    };
    // The first arm of a session scans the rectangle for candidates.
    const auto scan_begun = std::chrono::steady_clock::now();
    arm_sublimation(SublimationFrame{native, properties.data(), flow.data(), &seed});
    const double scan = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scan_begun).count();
    disarm_sublimation();
    double armed = 1e9, disarmed = 1e9;
    for (int k = 0; k < 5; ++k) {
        disarm_sublimation(); disarmed = std::min(disarmed, pass());
        arm_sublimation(SublimationFrame{native, properties.data(), flow.data(), &seed}); armed = std::min(armed, pass());
        disarm_sublimation(); capture_shadow(native);
    }
    std::printf("natural sublimation cost: %u owned cells, %d model steps; armed %.3f ms, disarmed %.3f ms per frame (one thread); "
        "candidate scan %.3f ms every %d frames\n", GW * (GH - GH / 8), model.calls, armed, disarmed, scan, 5);
    check(model.calls == 5 * 2 * static_cast<int>(GW / 32) * static_cast<int>(GH / 8) - 5 * static_cast<int>(GW / 32), "ONI's step runs only beside the tiles");
    stop(); set_sublimation({});
}
}

void natural_sublimation_control() { rule_control(); colony_control(); }
void natural_sublimation_cost() { cost_control(); }
