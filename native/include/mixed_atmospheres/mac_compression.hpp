#pragma once
#include "mac_transport.hpp"

namespace ma::experimental {
enum class MacCompressionLaw { quadratic, acoustic_quadratic };
// Exact-in-time relaxation of one compressing normal-velocity pair for
// q = rho * (v^2 + linear_speed*v), v=max(u_left-u_right,0).
// Zero linear speed retains the original one-grid-spacing quadratic law.
// integrated_density_area_s = dt * area * rho = dt * cell_mass / spacing.
// Fixed endpoints exchange impulse with the wall and retain zero velocity.
struct MacCompressionPair {
    double inertia_left = 0, inertia_right = 0;
    bool fixed_left = false, fixed_right = false;
    MacScaled before_left, before_right, after_left, after_right;
    MacScaled integrated_density_area_s, closing_velocity, relaxation_number;
    MacScaled impulse, wall_impulse;
    MacEnergy heat;
    std::size_t unresolved_momentum_updates = 0;
    MacScaled linear_speed, linear_relaxation_number;
    MacScaled linear_relaxation_phi = MacScaled::from_double(1);
};
[[nodiscard]] MacCompressionPair relax_mac_compression_pair(double inertia_left,
    double inertia_right, MacScaled momentum_left, MacScaled momentum_right,
    bool fixed_left, bool fixed_right, MacScaled integrated_density_area_s,
    MacScaled linear_speed = {});
struct MacCompressionEvent {
    std::size_t axis = 0, cell = 0, sweep = 0;
    MacCompressionPair pair;
};
struct MacCompressionReport {
    std::vector<MacCompressionEvent> events;
    MacEnergy heat, energy_residual, energy_bound;
    MacScaled wall_impulse_x, wall_impulse_y;
    std::size_t unresolved_heat_updates = 0, unresolved_momentum_updates = 0;
    double entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    // Empty in the original quadratic mode; otherwise one frozen half-sound
    // speed per source cell (zero at solids), independent of deposited heat.
    std::vector<MacScaled> source_linear_speeds;
};
struct MacCompressionAdvance { MacTransportState state; MacCompressionReport report; };
// Quadratic remains the default component/owner law. Acoustic mode freezes
// source-cell half-sound speeds for the forward/reverse half sweeps; this
// freezing is a first-order treatment of their thermal dependence.
// Species/passives and cell volumes stay fixed; kinetic loss heats the cell.
[[nodiscard]] MacCompressionAdvance advance_mac_compression(const SpeciesRegistry&,
    const FluidThermodynamics&, const MacTransportState&, double seconds,
    MacCompressionLaw = MacCompressionLaw::quadratic);
}
