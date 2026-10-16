#pragma once
#include "mac_transport.hpp"

namespace ma::experimental {
struct MacShockHeatPair {
    double before_left_j = 0, before_right_j = 0, after_left_j = 0, after_right_j = 0;
    double temperature_left_k = 0, temperature_right_k = 0;
    double after_temperature_left_k = 0, after_temperature_right_k = 0;
    double capacity_left_j_per_k = 0, capacity_right_j_per_k = 0;
    MacEnergy integrated_conductance, relaxation_number, fraction;
    MacEnergy equilibrium_transfer, transfer; // signed left -> right
    MacEnergy energy_residual, energy_bound;
    double entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    std::size_t equilibrium_iterations = 0, unresolved_updates = 0;
    double equilibrium_temperature_left_k = 0, equilibrium_temperature_right_k = 0;
    MacEnergy equilibrium_temperature_residual, equilibrium_temperature_bound;
    std::size_t rejected_equilibrium_trials = 0;
};
// Conservative, monotone thermal relaxation at fixed masses and cell volumes.
// Exact constant-capacity two-body conduction. For a mixed gas/elastic-liquid
// EOS, solve the common-temperature energy partition, then relax towards it
// using the initial equilibrium capacities. That nonlinear extension is a
// first-order regularization, not an exact nonlinear heat-equation solution.
[[nodiscard]] MacShockHeatPair relax_mac_shock_heat_pair(const FluidThermodynamics&,
    const MixtureInventory& left, const MixtureInventory& right, double volume_left,
    double volume_right, MacEnergy integrated_conductance_j_per_k);

struct MacShockHeatEvent {
    std::size_t axis = 0, face = 0, left = 0, right = 0, sweep = 0;
    MacScaled closing_velocity;
    MacShockHeatPair pair;
};
struct MacShockHeatReport {
    std::vector<MacShockHeatEvent> events;
    std::vector<double> frozen_capacity_j_per_k;
    MacEnergy absolute_transfer, energy_residual, energy_bound;
    double entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    std::size_t unresolved_updates = 0;
};
struct MacShockHeatAdvance { MacTransportState state; MacShockHeatReport report; };
// Separate occupied-grid regularization. No owner/default dispatch yet.
// A face uses the greater compressive normal velocity difference in its two
// adjacent cells and their harmonic equilibrium capacity, divided by spacing.
// Coefficient one, no sound-speed term. Conductance is frozen per call; each
// face receives forward/reverse half steps, reversing the entire axis order.
// No heat crosses walls. Rest, expansion and pure tangential shear are inactive.
// Every species, passive, face J and geometric field remains unchanged.
[[nodiscard]] MacShockHeatAdvance advance_mac_shock_heat(const SpeciesRegistry&,
    const FluidThermodynamics&, const MacTransportState&, double seconds);
}
