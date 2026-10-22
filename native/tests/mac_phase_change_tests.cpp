// Pressure-dependent liquid/vapour phase change (MAC_PHASE_CHANGE.md): the
// water/steam pair is calibrated to boil at 373.15 K and 1 atm, and the
// saturation curve follows from the EOS's own Gibbs energies.
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_primary.hpp"
#include "../src/mac_phase_flow.hpp"
#include <array>
#include <cstring>
#include <iomanip>
#include <tuple>

namespace {
using namespace ma; using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Water {
    SpeciesRegistry registry; std::shared_ptr<const FluidThermodynamics> eos; std::size_t water = 0, steam = 0, air = 0;
    Water(bool second_pair = false) {
        const auto standard = SpeciesRegistry::standard(); std::vector<ReferenceMaterial> materials;
        for (const char* id : {"water", "steam", "nitrogen"}) {
            const auto& sp = standard[standard.index(id)]; registry.add(sp); ReferenceMaterial m;
            m.id = id; m.phase = sp.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            m.molar_mass_kg_per_mol = sp.molar_mass; m.cv_j_per_kg_k = sp.cv(); m.energy_offset_j_per_kg = sp.internal_energy(0);
            m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e6;
            m.zero_pressure_density_kg_per_m3 = sp.phase == Phase::gas ? 0 : sp.density;
            m.reference_bulk_modulus_pa = sp.phase == Phase::gas ? 0 : 2.2e9; m.maximum_density_ratio = sp.phase == Phase::gas ? 1 : 2;
            m.provenance = "Phase-change test profile"; materials.push_back(m);
        }
        materials[1].entropy_offset_j_per_kg_k = mac_saturation_entropy_offset(materials[0], materials[1], 373.15, 101325);
        if (second_pair) for (std::size_t k = 0; k < 2; ++k) {
            auto sp = standard[standard.index(k == 0 ? "water" : "steam")];
            sp.id = k == 0 ? "water_b" : "steam_b"; registry.add(sp);
            auto material = materials[k]; material.id = sp.id; materials.push_back(material);
        }
        eos = std::make_shared<const FluidThermodynamics>(registry, "phase-change-tests-1", std::move(materials));
        water = registry.index("water"); steam = registry.index("steam"); air = registry.index("nitrogen");
    }
};
void parallel_control() {
    const Water s(true); constexpr std::size_t W = 64, H = 35;
    MacTransportState st; st.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H, 0)};
    const auto water_b = s.registry.index("water_b"), steam_b = s.registry.index("steam_b");
    for (std::size_t i = 0; i < W * H; ++i) {
        const double temperature = i % 3 == 0 ? 380 : i % 3 == 1 ? 360 : 300;
        std::vector<double> masses(s.registry.size(), 0); masses[s.water] = masses[water_b] = 249.25;
        if (i % 3 == 2) masses[s.air] = 101325 * .5 * s.registry[s.air].molar_mass / (gas_constant * temperature);
        else masses[s.steam] = masses[steam_b] = 101325 * .5 * s.registry[s.steam].molar_mass / (gas_constant * temperature);
        st.cells.push_back({s.eos->initialize_at_temperature(masses, temperature, 1).mixture, {}});
    }
    st.momentum_x.assign(st.grid.x_width() * H, 0); st.momentum_y.assign(W * st.grid.y_height(), 0);
    const std::array<MacSaturationPair, 2> pairs{{{s.water, s.steam}, {water_b, steam_b}}};
    const auto image = [&](const MacTransportState& state) { return MacClosedWorld(s.registry, s.eos, state).checkpoint(); };
    const auto held = image(st);
    const auto saved_options = mac_kernel_default_options();
    auto options = saved_options; options.threads = 1; options.minimum_blocks_per_thread = 1;
    set_mac_kernel_default_options(options);
    const auto serial = advance_mac_phase_change(s.registry, *s.eos, st, pairs, false, .18);
    const auto first = advance_mac_phase_change(s.registry, *s.eos, st, std::span(pairs).first(1), false, .18);
    const auto sequential = advance_mac_phase_change(s.registry, *s.eos, first.state, std::span(pairs).last(1), false, .18);
    check(image(serial.state) == image(sequential.state), "Cell-local phase work changed pair order");
    options.threads = 12; set_mac_kernel_default_options(options);
    const auto parallel = advance_mac_phase_change(s.registry, *s.eos, st, pairs, false, .18);
    const auto fields = [](const MacPhaseChangeReport& r) {
        return std::tie(r.evaporating_cells, r.condensing_cells, r.eos_evaluations, r.unevaluable_cells,
            r.evaporated_kg, r.condensed_kg, r.entropy_change_j_per_k, r.entropy_allowance_j_per_k);
    };
    check(image(parallel.state) == image(serial.state) && fields(parallel.report) == fields(serial.report),
        "Phase state or report depends on worker count");
    check(parallel.report.evaporating_cells > 0 && parallel.report.condensing_cells > 0 &&
        parallel.report.unevaluable_cells == 0 && image(st) == held, "Phase control did not convert both ways atomically");
    const std::array<std::size_t, 3> cells{{1, 1024, 1025}};
    auto expected = serial.state;
    for (std::size_t i = 0; i < st.cells.size(); ++i)
        if (std::find(cells.begin(), cells.end(), i) == cells.end()) expected.cells[i] = st.cells[i];
    const auto selected = advance_mac_phase_change_in_cells(s.registry, *s.eos, st, pairs, cells, .18);
    check(ma::testing::fingerprint(selected.state) == ma::testing::fingerprint(expected),
        "Selected-cell phase change touched another cell, U or momentum");
    options.threads = 1; set_mac_kernel_default_options(options);
    const auto selected_serial = advance_mac_phase_change_in_cells(s.registry, *s.eos, st, pairs, cells, .18);
    check(image(selected.state) == image(selected_serial.state) && fields(selected.report) == fields(selected_serial.report),
        "Selected-cell phase state or report depends on worker count");
    const auto owner = MacClosedWorld(s.registry, s.eos, st).with_phase_relaxation_in_cells(pairs, cells, .18).world;
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, owner.checkpoint());
    check(restored.with_phase_relaxation_in_cells(pairs, cells, .18).world.checkpoint() ==
        owner.with_phase_relaxation_in_cells(pairs, cells, .18).world.checkpoint(), "Selected-cell phase cold continuation changed");
    bool invalid = false;
    try { const std::size_t outside = st.cells.size(); (void)owner.with_phase_relaxation_in_cells(pairs, std::span(&outside, 1)); }
    catch (const std::invalid_argument&) { invalid = true; }
    check(invalid && image(st) == held, "Selected-cell phase admission or source atomicity failed");
    set_mac_kernel_default_options(saved_options);
    std::cout << "PASS ordered phase pairs: primary bytes, reports and one/many workers\n";
    std::cout << "PASS selected phase cells: other cells and U/J unchanged, workers, admission and cold continuation\n";
}
void cold_phase_flow_control() {
    const Water s;
    auto materials = s.eos->reference().materials();
    for (auto& material : materials) material.minimum_temperature_k = 270;
    const auto eos = std::make_shared<const FluidThermodynamics>(s.registry, "cold-phase-flow-control", materials);
    MacTransportState st; st.grid = {8, 1, 1, 1, false, false, std::vector<std::uint8_t>(8)};
    std::vector<double> masses(3); masses[s.steam] = .8;
    st.cells.assign(8, {MixtureInventory(3), {}});
    st.cells[0].mixture = eos->initialize_at_temperature(masses, 280, 1).mixture;
    st.occupied_volumes_m3.assign(8, 0); st.occupied_volumes_m3[0] = 1;
    st.momentum_x.assign(9, 0); st.momentum_y.assign(16, 0);
    MacClosedSettings settings; settings.vacuum = MacVacuumRepresentation::void_phase;
    settings.numerical_kernel = MacNumericalKernel::production;
    settings.subcell_void = MacSubcellVoidPolicy::expand_unresolved_gas;
    settings.step_control = MacStepControl::regional_predictive; settings.maximum_timestep_s = .005;
    settings.reconstruction = MacMaterialReconstruction::interface_plic;
    MacClosedWorld world(s.registry, eos, st, settings);
    const std::array<MacSaturationPair, 1> pairs{{{s.water, s.steam}}};
    const auto held = world.checkpoint();
    const auto ordinary = world.advanced(1e-6, 65536, MacExecutionPolicy::production);
    const auto same = advance_mac_phase_flow(world, 1e-6, 65536, pairs);
    check(same.world.checkpoint() == ordinary.world.checkpoint() && same.converted.empty(),
        "Phase-assisted flow changed an ordinary successful interval");
    bool failed = false;
    try { (void)world.advanced(.05, 65536, MacExecutionPolicy::production); }
    catch (const std::exception&) { failed = mac_kernel::failed_flow_attempts > 0; }
    check(failed, "Cold phase control did not reach the flow EOS floor");
    const auto serial = advance_mac_phase_flow(world, .05, 65536, pairs);
    check(serial.condensed_kg > 0 && world.checkpoint() == held,
        "Cold phase flow did not condense atomically");
    long double total = 0;
    for (const auto& cell : serial.world.state().cells) {
        total += cell.mixture.masses[s.water] + cell.mixture.masses[s.steam];
        if (cell.mixture.mass() > 0)
            check(eos->evaluate(cell.mixture, mac_material_volume(serial.world.state(), &cell - serial.world.state().cells.data())).temperature_k >= 270,
                "Cold phase recovery left the unchanged EOS domain");
    }
    check(std::abs(total - .8L) < 1e-14L, "Cold phase flow lost pair mass");
    const auto energy = [&](const MacClosedWorld& w) {
        MacEnergySum sum;
        for (const auto& cell : w.state().cells) account_mac_cell_internal_energy(sum, cell);
        const auto inertia = mac_face_inertias(s.registry, w.state(), *eos);
        const auto x = mac_field_values(w.state().momentum_x, w.state().full_momentum_x);
        const auto y = mac_field_values(w.state().momentum_y, w.state().full_momentum_y);
        for (std::size_t f = 0; f < x.size(); ++f) if (inertia.x[f] > 0) sum.add(MacEnergy::kinetic(inertia.x[f], x[f]));
        for (std::size_t f = 0; f < y.size(); ++f) if (inertia.y[f] > 0) sum.add(MacEnergy::kinetic(inertia.y[f], y[f]));
        return mac_energy_long_double(sum.value());
    };
    check(std::abs(energy(serial.world) - energy(world)) <= 0x1p-45L * std::abs(energy(world)),
        "Cold phase flow energy ledger did not close");
    for (std::size_t k = 0; k < masses.size(); ++k) {
        long double held_mass = 0;
        for (const auto& cell : serial.world.state().cells) held_mass += cell.mixture.masses[k];
        const auto scale = masses[k] + serial.converted_gross[k];
        check(std::abs(held_mass - masses[k] - serial.converted[k]) <= 0x1p-45L * scale,
            "Cold phase flow constituent conversion ledger did not close");
    }
    const auto options = mac_kernel_default_options(); auto many = options; many.threads = 12;
    set_mac_kernel_default_options(many);
    const auto parallel = advance_mac_phase_flow(world, .05, 65536, pairs);
    set_mac_kernel_default_options(options);
    check(parallel.world.checkpoint() == serial.world.checkpoint() && parallel.converted == serial.converted &&
        parallel.converted_gross == serial.converted_gross && parallel.substeps == serial.substeps &&
        parallel.rejected_attempts == serial.rejected_attempts, "Cold phase flow depends on worker count");
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, eos, serial.world.checkpoint());
    check(advance_mac_phase_flow(restored, .01, 65536, pairs).world.checkpoint() ==
        advance_mac_phase_flow(serial.world, .01, 65536, pairs).world.checkpoint(), "Cold phase flow continuation changed");
    bool exhausted = false;
    try { (void)advance_mac_phase_flow(world, .05, serial.substeps + serial.rejected_attempts - 1, pairs); }
    catch (const std::exception&) { exhausted = true; }
    check(exhausted && world.checkpoint() == held, "Cold phase flow exceeded its shared budget or changed its source");
    std::cout << "PASS cold phase flow: unchanged success, EOS floor, species/energy ledgers, workers, cold continuation and shared budget\n";
}
double vapour_pressure(const Water& s, const MixtureInventory& m, double volume) {
    const auto e = s.eos->evaluate(m, volume); return e.gas_pressure_pa * e.partial_volumes_m3[1] / e.gas_volume_m3;
}
void saturation_curve(const Water& s) {
    const double p100 = mac_saturation_pressure(*s.eos, 0, 1, 373.15, 101325);
    const double slope = (mac_saturation_pressure(*s.eos, 0, 1, 373.16, 101325) - mac_saturation_pressure(*s.eos, 0, 1, 373.14, 101325)) / .02;
    const double p25 = mac_saturation_pressure(*s.eos, 0, 1, 298.15, 3000), p150 = mac_saturation_pressure(*s.eos, 0, 1, 423.15, 476000);
    std::cout << std::setprecision(10) << "PCSAT p(373.15)=" << p100 << " dp/dT=" << slope << " p(298.15)=" << p25 << " p(423.15)=" << p150 << '\n';
    check(std::abs(p100 - 101325) <= 1e-8 * 101325, "Calibrated pair does not boil at 373.15 K and 1 atm");
    // Clausius-Clapeyron at the reference point: dp/dT = L p M / (R T^2), L the EOS's own latent heat.
    const auto& w = s.registry[s.water]; const auto& v = s.registry[s.steam];
    const double latent = v.internal_energy(373.15) + gas_constant * 373.15 / v.molar_mass - w.internal_energy(373.15) - 101325 / w.density;
    const double expected = latent * 101325 * v.molar_mass / (gas_constant * 373.15 * 373.15);
    check(std::abs(slope - expected) <= 1e-3 * expected, "Saturation slope departs from Clausius-Clapeyron");
    // Constant-capacity water: within 15% of steam tables (3.17 kPa, 476 kPa).
    check(std::abs(p25 / 3169 - 1) < .15 && std::abs(p150 / 476000 - 1) < .15, "Saturation curve departs from water");
}
// One cell after relaxation: U and the pair's mass are unchanged, entropy
// rises, and the vapour partial pressure matches saturation.
void relaxes(const Water& s, const char* name, std::vector<double> masses, double temperature, double volume, int expected_direction) {
    MacTransportState st; st.grid = {1, 1, std::sqrt(volume), 1, false, false, {0}};
    st.cells.push_back({s.eos->initialize_at_temperature(masses, temperature, volume).mixture, {}});
    st.momentum_x.assign(2, 0); st.momentum_y.assign(2, 0);
    const MacSaturationPair pair{s.water, s.steam};
    const auto r = advance_mac_phase_change(s.registry, *s.eos, st, std::span(&pair, 1));
    const auto& a = st.cells[0].mixture; const auto& b = r.state.cells[0].mixture;
    const auto ea = s.eos->evaluate(a, volume), eb = s.eos->evaluate(b, volume);
    const double moved = b.masses[s.steam] - a.masses[s.steam];
    std::cout << "PCCELL " << name << " moved_kg=" << moved << " T=" << ea.temperature_k << "->" << eb.temperature_k
              << " p=" << ea.mechanical_pressure_pa << "->" << eb.mechanical_pressure_pa << " dS=" << eb.entropy_j_per_k - ea.entropy_j_per_k << '\n';
    check(b.energy == a.energy && std::abs(b.masses[s.water] + b.masses[s.steam] - a.masses[s.water] - a.masses[s.steam]) <=
        4e-16 * (a.masses[s.water] + a.masses[s.steam]), "Phase change changed U or the pair's mass");
    check((moved > 0) - (moved < 0) == expected_direction, "Phase change moved mass the wrong way");
    if (expected_direction == 0) return;
    check(eb.entropy_j_per_k >= ea.entropy_j_per_k, "Phase change lowered entropy");
    if (b.masses[s.water] > 0 && b.masses[s.steam] > 0 && eb.has_gas) {
        const double pv = vapour_pressure(s, b, volume), sat = mac_saturation_pressure(*s.eos, 0, 1, eb.temperature_k, eb.mechanical_pressure_pa);
        check(std::abs(pv - sat) <= 1e-9 * sat, "Relaxed cell is not at saturation");
    }
}
// A small heated column on the production kernel: hot steam over water at
// saturation beside nitrogen. Sharp-interface boiling evaporates at the
// interface each substep; water plus steam is conserved, and a version-11
// checkpoint restores and continues byte for byte.
void production_column(const Water& s) {
    constexpr double h = 1e-4, volume = h * h, t_sat = 373.15, p0 = 101325;
    MacTransportState st; st.grid = {16, 1, h, 1, false, false, std::vector<std::uint8_t>(16)};
    const auto gas = [&](std::size_t k, double molar, double t, double fraction) { std::vector<double> m(3); m[k] = fraction * p0 * volume * molar / (gas_constant * t); return m; };
    for (std::size_t i = 0; i < 16; ++i) {
        std::vector<double> m(3); double t = t_sat;
        if (i < 4) { m = gas(s.steam, .018015, t = 383 - 2.0 * static_cast<double>(i), 1); }
        else if (i == 4) { m = gas(s.steam, .018015, t_sat, .5); m[s.water] = .5 * volume * s.registry[s.water].density * (1 + p0 / 2.2e9); }
        else if (i < 10) m[s.water] = volume * s.registry[s.water].density * (1 + p0 / 2.2e9);
        else m = gas(s.air, .028013, t_sat, 1);
        st.cells.push_back({s.eos->initialize_at_temperature(m, t, volume).mixture, {}});
    }
    const auto faces = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
    MacClosedSettings settings; settings.thermal_conduction = true; settings.phase_change = MacPhaseChange::saturation;
    settings.reconstruction = MacMaterialReconstruction::interface_plic; settings.maximum_timestep_s = 1e-5;
    MacClosedWorld world(s.registry, s.eos, st, settings);
    const auto pair_mass = [&](const MacTransportState& x) { long double m = 0; for (const auto& c : x.cells) m += c.mixture.masses[s.water] + c.mixture.masses[s.steam]; return m; };
    const long double initial = pair_mass(world.state());
    double evaporated = 0;
    std::vector<std::uint8_t> saved;
    for (int k = 0; k < 20; ++k) {
        auto next = world.advanced(1e-5, 65536, MacExecutionPolicy::production);
        evaporated += next.report.evaporated_kg; world = std::move(next.world);
        if (k == 9) saved = world.checkpoint();
    }
    const double drift = static_cast<double>(std::abs(pair_mass(world.state()) - initial) / initial);
    std::cout << "PCCOLUMN evaporated_kg=" << evaporated << " pair_mass_drift=" << drift << '\n';
    check(evaporated > 0 && drift <= 1e-12, "Production boiling did not evaporate or lost water and steam");
    std::uint32_t version = 0; std::memcpy(&version, saved.data() + 8, 4);
    check(version == 11, "Phase-change owner did not write checkpoint version 11");
    auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, saved);
    check(restored.checkpoint() == saved, "Version-11 checkpoint did not round-trip");
    for (int k = 10; k < 20; ++k) restored = restored.advanced(1e-5, 65536, MacExecutionPolicy::production).world;
    check(restored.checkpoint() == world.checkpoint(), "Restored phase-change owner did not continue byte for byte");
}
}

// Void phase (MAC_VACUUM.md, "Phase change"): water resting in vacuum at 300 K
// boils, its vapour filling the cell's void at saturation and cooling the water;
// the vapour then expands into the empty cells beside it and more water boils.
void vacuum_boiling(const Water& s) {
    constexpr double h = .1, volume = h * h * h;
    MacTransportState st; st.grid = {3, 1, h, h, false, false, std::vector<std::uint8_t>(3)};
    std::vector<double> m(3); m[s.water] = .5;
    st.cells = {{s.eos->initialize_at_temperature(m, 300, volume).mixture, {}}, {MixtureInventory(3), {}}, {MixtureInventory(3), {}}};
    st.occupied_volumes_m3 = {.5 / s.registry[s.water].density, 0, 0};
    st.momentum_x.assign(4, 0); st.momentum_y.assign(6, 0);
    MacClosedSettings settings; settings.vacuum = MacVacuumRepresentation::void_phase; settings.phase_change = MacPhaseChange::saturation;
    settings.reconstruction = MacMaterialReconstruction::limited_linear; settings.numerical_kernel = MacNumericalKernel::production;
    settings.maximum_timestep_s = 1e-4;
    MacClosedWorld world(s.registry, s.eos, st, settings);
    const auto pair_mass = [&](const MacTransportState& x) { long double t = 0; for (const auto& c : x.cells) t += c.mixture.masses[s.water] + c.mixture.masses[s.steam]; return t; };
    const long double initial = pair_mass(st); double first = 0;
    for (int k = 0; k < 5; ++k) {
        auto r = world.advanced(1e-3, 65536, MacExecutionPolicy::production); world = std::move(r.world);
        check(r.report.interval_reference_fallbacks == 0 && std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "Vacuum boiling interval failed");
        const auto& x = world.state();
        if (k == 0) {
            first = x.cells[0].mixture.masses[s.steam];
            const auto e = s.eos->evaluate(x.cells[0].mixture, mac_material_volume(x, 0));
            const double saturation = mac_saturation_pressure(*s.eos, 0, 1, e.temperature_k, e.mechanical_pressure_pa);
            std::cout << "PCVOID first interval: steam " << first << " kg, T " << e.temperature_k << " K, vapour " << vapour_pressure(s, x.cells[0].mixture, mac_material_volume(x, 0))
                      << " Pa (saturation " << saturation << " Pa), filled " << (mac_material_volume(x, 0) == volume) << '\n';
            check(first > 0 && mac_material_volume(x, 0) == volume && e.temperature_k < 300, "Water in vacuum did not boil into its void");
        }
    }
    const auto& x = world.state(); const double steam = x.cells[0].mixture.masses[s.steam] + x.cells[1].mixture.masses[s.steam] + x.cells[2].mixture.masses[s.steam];
    std::cout << "PCVOID after 5 ms: steam " << steam << " kg, beside " << x.cells[1].mixture.masses[s.steam] + x.cells[2].mixture.masses[s.steam]
              << " kg, pair mass drift " << static_cast<double>((pair_mass(x) - initial) / initial) << '\n';
    check(steam > first && x.cells[1].mixture.masses[s.steam] > 0 && std::abs(pair_mass(x) - initial) <= 1e-14L * initial, "Vacuum boiling did not spread or lost mass");
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "--cold-flow-only") == 0) { cold_phase_flow_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--parallel-only") == 0) { parallel_control(); return 0; }
        check(argc == 1, "Usage: ma_mac_phase_change_tests [--parallel-only]");
        const Water s; saturation_curve(s);
        const auto masses = [&](double water, double steam, double air) { std::vector<double> m(3); m[s.water] = water; m[s.steam] = steam; m[s.air] = air; return m; };
        // 1 m3: half water, half steam at 1 atm.
        const double steam_1atm_380 = 101325 * .5 * .018015 / (gas_constant * 380);
        relaxes(s, "superheated", masses(498.5, steam_1atm_380, 0), 380, 1, 1);
        relaxes(s, "supersaturated", masses(498.5, 101325 * .5 * .018015 / (gas_constant * 360), 0), 360, 1, -1);
        relaxes(s, "dry_air", masses(498.5, 0, 101325 * .5 * .028013 / (gas_constant * 300)), 300, 1, 1);
        // Exactly filled subcooled liquid stays liquid.
        relaxes(s, "subcooled_full", masses(997 * (1 + 101325 / 2.2e9), 0, 0), 350, 1, 0);
        production_column(s);
        vacuum_boiling(s);
        parallel_control();
        cold_phase_flow_control();
        std::cout << "MAC phase-change tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
