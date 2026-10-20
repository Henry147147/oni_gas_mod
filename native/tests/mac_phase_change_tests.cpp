// Pressure-dependent liquid/vapour phase change (MAC_PHASE_CHANGE.md): the
// water/steam pair is calibrated to boil at 373.15 K and 1 atm, and the
// saturation curve follows from the EOS's own Gibbs energies.
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <cstring>
#include <iomanip>

namespace {
using namespace ma; using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Water {
    SpeciesRegistry registry; std::shared_ptr<const FluidThermodynamics> eos; std::size_t water = 0, steam = 0, air = 0;
    Water() {
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
        eos = std::make_shared<const FluidThermodynamics>(registry, "phase-change-tests-1", std::move(materials));
        water = registry.index("water"); steam = registry.index("steam"); air = registry.index("nitrogen");
    }
};
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
int main() {
    try {
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
        std::cout << "MAC phase-change tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
