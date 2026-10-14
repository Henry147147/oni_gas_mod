#include "mixed_atmospheres/phase_change.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ma {
namespace {
void validate_pair(const SpeciesRegistry& r, std::size_t a, std::size_t b) {
    if (a == b || r[a].atoms != r[b].atoms || r[a].molar_mass != r[b].molar_mass)
        throw std::invalid_argument("Phase change must preserve chemical identity");
}

double energy_at(const SpeciesRegistry& r, const MixtureInventory& m, double t) {
    double result = 0;
    for (std::size_t i = 0; i < r.size(); ++i) result += m.masses[i] * r[i].internal_energy(t);
    return result;
}
}

VaporLiquidPair water_vapor_pair(const SpeciesRegistry& r) {
    return {r.index("water"), r.index("steam"), 373.15, 101325.0, 2257000.0, 273.15, 600.0};
}

void equilibrate_vapor_liquid(const SpeciesRegistry& r, MixtureInventory& inventory,
                             const VaporLiquidPair& pair, double volume) {
    inventory.validate(r);
    validate_pair(r, pair.liquid, pair.vapor);
    if (r[pair.liquid].phase != Phase::liquid || r[pair.vapor].phase != Phase::gas)
        throw std::invalid_argument("Expected liquid and gas phases");
    for (double value : {volume, pair.reference_temperature, pair.reference_pressure, pair.latent_heat,
                         pair.minimum_temperature, pair.maximum_temperature}) require_finite(value, "phase parameter");
    if (volume <= 0 || pair.reference_temperature <= 0 || pair.reference_pressure <= 0 || pair.latent_heat <= 0 ||
        pair.minimum_temperature <= 0 || pair.maximum_temperature <= pair.minimum_temperature)
        throw std::invalid_argument("Invalid phase equilibrium parameters");
    const double total = inventory.masses[pair.liquid] + inventory.masses[pair.vapor];
    if (total == 0) return;
    auto candidate = inventory;
    candidate.masses[pair.liquid] = 0;
    candidate.masses[pair.vapor] = 0;
    const double available_volume = volume - candidate.condensed_volume(r);
    const double liquid_density = r[pair.liquid].density;
    if (available_volume <= total / liquid_density)
        throw std::domain_error("Vapor equilibrium requires positive headspace even when fully condensed");
    const auto evaluate = [&](double t) {
        const double pressure = pair.reference_pressure * std::exp(-pair.latent_heat * r[pair.vapor].molar_mass /
            gas_constant * (1 / t - 1 / pair.reference_temperature));
        const double vapor_density = pressure * r[pair.vapor].molar_mass / (gas_constant * t);
        const double vapor_mass = vapor_density >= liquid_density ? total :
            std::clamp(vapor_density * (available_volume - total / liquid_density) /
                       (1 - vapor_density / liquid_density), 0.0, total);
        candidate.masses[pair.vapor] = vapor_mass;
        candidate.masses[pair.liquid] = total - vapor_mass;
        return energy_at(r, candidate, t);
    };
    double lo = pair.minimum_temperature, hi = pair.maximum_temperature;
    if (inventory.energy < evaluate(lo) || inventory.energy > evaluate(hi))
        throw std::domain_error("Phase equilibrium is outside the validated temperature bracket");
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double mid = (lo + hi) / 2;
        if (evaluate(mid) > inventory.energy) hi = mid; else lo = mid;
    }
    evaluate((lo + hi) / 2);
    candidate.energy = inventory.energy;
    candidate.validate(r);
    inventory = std::move(candidate);
}

void equilibrate_solid_liquid(const SpeciesRegistry& r, MixtureInventory& inventory,
                             std::size_t solid, std::size_t liquid, double melting_t) {
    inventory.validate(r);
    validate_pair(r, solid, liquid);
    require_finite(melting_t, "melting temperature");
    if (r[solid].phase != Phase::solid || r[liquid].phase != Phase::liquid || melting_t <= 0)
        throw std::invalid_argument("Invalid melting pair");
    const double total = inventory.masses[solid] + inventory.masses[liquid];
    if (total == 0) return;
    auto next = inventory;
    next.masses[solid] = total;
    next.masses[liquid] = 0;
    const double solid_energy = energy_at(r, next, melting_t);
    next.masses[solid] = 0;
    next.masses[liquid] = total;
    const double liquid_energy = energy_at(r, next, melting_t);
    if (liquid_energy <= solid_energy) throw std::invalid_argument("Melting must require positive latent energy");
    const double liquid_fraction = std::clamp((inventory.energy - solid_energy) /
                                              (liquid_energy - solid_energy), 0.0, 1.0);
    next.masses[liquid] = total * liquid_fraction;
    next.masses[solid] = total - next.masses[liquid];
    next.validate(r);
    inventory = std::move(next);
}
}
