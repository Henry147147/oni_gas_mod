#include "mixed_atmospheres/atmosphere.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma {
void AtmosphereSettings::validate() const {
    for (double x : {minimum_oxygen_fraction, maximum_oxygen_fraction, minimum_oxygen_partial_pressure,
                     maximum_co2_fraction, maximum_co2_partial_pressure}) require_finite(x, "atmosphere setting");
    if (minimum_oxygen_fraction <= 0 || maximum_oxygen_fraction < minimum_oxygen_fraction ||
        maximum_oxygen_fraction > 1 || minimum_oxygen_partial_pressure <= 0 ||
        maximum_co2_fraction < 0 || maximum_co2_fraction > 1 || maximum_co2_partial_pressure < 0)
        throw std::invalid_argument("Invalid atmosphere settings");
}

Breathability assess_atmosphere(const SpeciesRegistry& r, const MixtureInventory& inventory,
                               double volume, const AtmosphereSettings& s) {
    s.validate();
    inventory.validate(r);
    require_finite(volume, "volume");
    if (volume <= 0) throw std::invalid_argument("Volume must be positive");
    Breathability result;
    const auto fail = [&](BreathingFailure f) { result.failures |= static_cast<std::uint32_t>(f); };
    const double moles = inventory.gas_moles(r);
    if (moles == 0) { fail(BreathingFailure::vacuum); return result; }
    result.pressure = inventory.pressure(r, volume);
    const auto fraction = [&](const char* id) { const auto i = r.index(id); return inventory.masses[i] / r[i].molar_mass / moles; };
    result.oxygen_fraction = fraction("oxygen");
    result.co2_fraction = fraction("carbon_dioxide");
    result.oxygen_partial_pressure = result.oxygen_fraction * result.pressure;
    result.co2_partial_pressure = result.co2_fraction * result.pressure;
    // Inclusive boundaries with a few ulps of arithmetic tolerance, not a
    // gameplay-sized relaxation of configured limits.
    // A unit-sized absolute tolerance would hide ultra-trace hazards. Keep
    // arithmetic tolerance relative to the compared limits/measurements.
    const auto below = [](double a, double b) { return a < b && b - a > 16 * std::numeric_limits<double>::epsilon() * std::max(std::abs(a), std::abs(b)); };
    const auto exceeds = [&](double limit, double value, double present_mass) { return present_mass > 0 && (limit == 0 || below(limit, value)); };
    if (below(result.oxygen_fraction, s.minimum_oxygen_fraction)) fail(BreathingFailure::oxygen_fraction_low);
    if (s.hard_mode && below(s.maximum_oxygen_fraction, result.oxygen_fraction)) fail(BreathingFailure::oxygen_fraction_high);
    if (below(result.oxygen_partial_pressure, s.minimum_oxygen_partial_pressure)) fail(BreathingFailure::oxygen_partial_pressure_low);
    const double co2_mass = inventory.masses[r.index("carbon_dioxide")];
    if (exceeds(s.maximum_co2_fraction, result.co2_fraction, co2_mass)) fail(BreathingFailure::carbon_dioxide_fraction_high);
    if (exceeds(s.maximum_co2_partial_pressure, result.co2_partial_pressure, co2_mass)) fail(BreathingFailure::carbon_dioxide_partial_pressure_high);
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (r[i].phase != Phase::gas || r[i].max_mole_fraction >= 1) continue;
        if (exceeds(r[i].max_mole_fraction, inventory.masses[i] / r[i].molar_mass / moles, inventory.masses[i])) fail(BreathingFailure::contaminant);
    }
    return result;
}

MixtureInventory make_air(const SpeciesRegistry& r, double volume, double t, double p, double o2) {
    for (double value : {volume, t, p, o2}) require_finite(value, "air parameter");
    if (volume <= 0 || t <= 0 || p < 0 || o2 < 0 || o2 > 1) throw std::invalid_argument("Invalid air parameters");
    MixtureInventory result(r.size());
    const double moles = p * volume / (gas_constant * t);
    result.masses[r.index("oxygen")] = moles * o2 * r[r.index("oxygen")].molar_mass;
    result.masses[r.index("nitrogen")] = moles * (1 - o2) * r[r.index("nitrogen")].molar_mass;
    result.set_temperature(r, t);
    result.validate(r);
    return result;
}
}
