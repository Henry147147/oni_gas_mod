#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace ma {
struct ThermalBody { double heat_capacity_j_per_k, temperature_k; };
struct ThermalContact { std::size_t first, second; double conductance_w_per_k; };
struct ThermalResult {
    std::vector<double> temperatures_k;
    std::vector<double> energy_changes_j;
    double entropy_produced_j_per_k = 0;
    std::size_t iterations = 0;
};

// Series thermal resistances on either side of a face. No contact resistance,
// radiation, phase change or convective boundary-layer closure is included.
[[nodiscard]] double thermal_conductance(double first_conductivity, double second_conductivity,
                                         double first_distance, double second_distance, double area);

// Closed-network, constant-capacity backward-Euler heat conduction. Pairwise
// energy transfers conserve energy; results are quoted without mutating any
// native/game state. An adapter must commit both ends of every transfer once.
// Empty/vacuum cells are omitted, never assigned fictitious heat capacity.
[[nodiscard]] ThermalResult conduct_heat(std::span<const ThermalBody> bodies,
                                         std::span<const ThermalContact> contacts, double seconds);
}
