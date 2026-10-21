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
    std::vector<long double> shared_face_energy_j{};
};

// Series thermal resistances on either side of a face. No contact resistance,
// radiation, phase change or convective boundary-layer closure is included.
[[nodiscard]] double thermal_conductance(double first_conductivity, double second_conductivity,
                                         double first_distance, double second_distance, double area);

// Free-molecular limit of a gas cell's conductivity. Once a rarefied gas's
// mean free path approaches the cell width, molecules carry heat across the
// cell without colliding: per kelvin of difference at most about its
// volumetric heat capacity times mean molecular speed times width / 4. A
// continuum gas is far below this cap (air at 1 Pa in a 1 m cell: about 40
// times its conductivity), so only a near-vacuum is limited.
[[nodiscard]] double free_molecular_conductivity(double heat_capacity_j_per_k, double volume_m3,
                                                 double temperature_k, double molar_mass_kg_per_mol, double width_m);

// Closed-network, constant-capacity backward-Euler heat conduction. Pairwise
// energy transfers conserve energy; results are quoted without mutating any
// native/game state. An adapter must commit both ends of every transfer once.
// Empty/vacuum cells are omitted, never assigned fictitious heat capacity.
[[nodiscard]] ThermalResult conduct_heat(std::span<const ThermalBody> bodies,
                                         std::span<const ThermalContact> contacts, double seconds);
}
