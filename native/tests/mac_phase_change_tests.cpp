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
        std::cout << "MAC phase-change tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
