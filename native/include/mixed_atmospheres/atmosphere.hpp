#pragma once
#include "mixture.hpp"
#include <cstdint>

namespace ma {
enum class BreathingFailure : std::uint32_t {
    none = 0, vacuum = 1, oxygen_fraction_low = 2, oxygen_fraction_high = 4,
    oxygen_partial_pressure_low = 8, carbon_dioxide_fraction_high = 16,
    carbon_dioxide_partial_pressure_high = 32, contaminant = 64
};
struct AtmosphereSettings {
    bool hard_mode = true;
    double minimum_oxygen_fraction = 0.15;
    double maximum_oxygen_fraction = 0.35;
    double minimum_oxygen_partial_pressure = 15000;
    double maximum_co2_fraction = 0.005;
    double maximum_co2_partial_pressure = 500;
    void validate() const;
};
struct Breathability {
    std::uint32_t failures = 0;
    double pressure = 0;
    double oxygen_fraction = 0;
    double oxygen_partial_pressure = 0;
    double co2_fraction = 0;
    double co2_partial_pressure = 0;
    [[nodiscard]] bool breathable() const { return failures == 0; }
};
[[nodiscard]] Breathability assess_atmosphere(const SpeciesRegistry& registry, const MixtureInventory& inventory,
                                             double volume, const AtmosphereSettings& settings = {});
[[nodiscard]] MixtureInventory make_air(const SpeciesRegistry& registry, double volume,
                                       double temperature = 293.15, double pressure = 101325,
                                       double oxygen_fraction = 0.21);
}
