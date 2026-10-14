#pragma once
#include "mixture.hpp"

namespace ma {
struct VaporLiquidPair {
    std::size_t liquid;
    std::size_t vapor;
    double reference_temperature;
    double reference_pressure;
    double latent_heat; // J/kg, Clausius-Clapeyron reference approximation
    double minimum_temperature;
    double maximum_temperature;
};

// Closed, rigid container with gas headspace. Uses vapor partial pressure and a
// bracketed energy solve. Does not represent dissolved gases or supercritical
// fluids. Unsupported temperatures/filled vessels reject without mutation.
void equilibrate_vapor_liquid(const SpeciesRegistry& registry, MixtureInventory& inventory,
                             const VaporLiquidPair& pair, double container_volume);
// Constant transition-temperature solid/liquid coexistence; energy determines
// melt fraction. Phases must have matching chemical composition.
void equilibrate_solid_liquid(const SpeciesRegistry& registry, MixtureInventory& inventory,
                             std::size_t solid, std::size_t liquid, double melting_temperature);
[[nodiscard]] VaporLiquidPair water_vapor_pair(const SpeciesRegistry& registry);
}
