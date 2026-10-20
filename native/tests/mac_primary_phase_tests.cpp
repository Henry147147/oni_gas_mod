#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <iomanip>

namespace {
using namespace ma::testing;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Water {
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> eos;

    Water() {
        const auto standard = SpeciesRegistry::standard();
        std::vector<ReferenceMaterial> materials;
        for (const char* id : {"water", "steam", "nitrogen"}) {
            const auto& species = standard[standard.index(id)];
            registry.add(species);
            ReferenceMaterial material;
            material.id = id;
            material.phase = species.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            material.molar_mass_kg_per_mol = species.molar_mass;
            material.cv_j_per_kg_k = species.cv();
            material.energy_offset_j_per_kg = species.internal_energy(0);
            material.minimum_temperature_k = 1;
            material.maximum_temperature_k = 1e6;
            material.zero_pressure_density_kg_per_m3 = species.phase == Phase::gas ? 0 : species.density;
            material.reference_bulk_modulus_pa = species.phase == Phase::gas ? 0 : 2.2e9;
            material.maximum_density_ratio = species.phase == Phase::gas ? 1 : 2;
            material.provenance = "Primary-U phase audit observation";
            materials.push_back(material);
        }
        materials[1].entropy_offset_j_per_kg_k = mac_saturation_entropy_offset(materials[0], materials[1], 373.15, 101325);
        eos = std::make_shared<const FluidThermodynamics>(registry, "primary-u-phase-observation-v1", std::move(materials));
    }
};

void caloric_observation(const Water& water, double temperature) {
    std::vector<double> masses(water.registry.size());
    masses[water.registry.index("water")] = 498.5;
    masses[water.registry.index("steam")] = 101325 * .5 * .018015 / (gas_constant * temperature);
    MacTransportState source;
    source.grid = {1, 1, 1, 1, false, false, {0}};
    source.cells.push_back({water.eos->initialize_at_temperature(masses, temperature, 1).mixture, {}});
    source.momentum_x.assign(2, 0);
    source.momentum_y.assign(2, 0);
    const MacSaturationPair pair{water.registry.index("water"), water.registry.index("steam")};
    const auto base = advance_mac_phase_change(water.registry, *water.eos, source, std::span(&pair, 1));
    check(base.report.evaporating_cells + base.report.condensing_cells == 1, "Phase audit control did not convert material");
    const double high = source.cells[0].mixture.energy;
    const double quantum = std::nextafter(high, std::numeric_limits<double>::infinity()) - high;
    for (const double sign : {-1., 1.}) {
        const double remainder = sign * quantum * .25;
        source.cells[0].energy_remainder_j = {remainder, {}};
        const auto carried = advance_mac_phase_change(water.registry, *water.eos, source, std::span(&pair, 1));
        const auto before = water.eos->evaluate(source.cells[0].mixture, 1);
        const auto after = water.eos->evaluate(carried.state.cells[0].mixture, 1);
        const long double correction = static_cast<long double>(remainder) * (1.L / after.temperature_k - 1.L / before.temperature_k);
        const double expected = static_cast<double>(static_cast<long double>(base.report.entropy_change_j_per_k) + correction);
        const double ulp = std::nextafter(expected, std::numeric_limits<double>::infinity()) - expected;
        check(same_mac_stored_remainder(source.cells[0].energy_remainder_j, carried.state.cells[0].energy_remainder_j) &&
            source.cells[0].mixture.energy == carried.state.cells[0].mixture.energy, "Phase change lost stored high/low U");
        check(base.state.cells[0].mixture.masses == carried.state.cells[0].mixture.masses, "Read-only U observation changed the phase root");
        check(base.report.entropy_allowance_j_per_k == carried.report.entropy_allowance_j_per_k, "U observation changed the entropy allowance");
        std::cout << "PUECASE " << remainder << ' ' << before.temperature_k << ' ' << after.temperature_k << ' '
            << base.report.entropy_change_j_per_k << ' ' << carried.report.entropy_change_j_per_k << ' ' << expected << ' ' << ulp << '\n';
        check(std::abs(correction) >= 32 * ulp, "Phase U observation control is below its output resolution");
        check(std::abs(carried.report.entropy_change_j_per_k - expected) <= 4 * ulp, "Phase entropy audit omitted the carried-U caloric correction");
    }
    const auto tiny = MacEnergy::from_parts(.5, MacExponent(-2000));
    source.cells[0].energy_remainder_j = {tiny.project().value, tiny};
    const auto flagged = advance_mac_phase_change(water.registry, *water.eos, source, std::span(&pair, 1));
    check(same_mac_stored_remainder(source.cells[0].energy_remainder_j, flagged.state.cells[0].energy_remainder_j),
        "Phase change flattened a flagged U remainder");
}
}

int main() {
    try {
        require_mac_energy_environment();
        std::cout << std::setprecision(20);
        const Water water;
        caloric_observation(water, 380);
        caloric_observation(water, 360);
        std::cout << "PASS signed caloric U observations, retained state and flagged remainders\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
