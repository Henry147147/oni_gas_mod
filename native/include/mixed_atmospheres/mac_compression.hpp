#pragma once
#include "mac_transport.hpp"

namespace ma::experimental {
// Exact-in-time relaxation of one compressing normal-velocity pair for
// q = rho * max(u_left-u_right, 0)^2 (one grid-spacing viscosity length).
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
};
[[nodiscard]] MacCompressionPair relax_mac_compression_pair(double inertia_left,
    double inertia_right, MacScaled momentum_left, MacScaled momentum_right,
    bool fixed_left, bool fixed_right, MacScaled integrated_density_area_s);
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
};
struct MacCompressionAdvance { MacTransportState state; MacCompressionReport report; };
// Separate occupied-grid component, initially excluded from the closed owner.
// Forward/reverse half sweeps are symmetric in time. Species/passives and cell
// volumes stay fixed; removed normal kinetic energy heats the enclosing cell.
[[nodiscard]] MacCompressionAdvance advance_mac_compression(const SpeciesRegistry&,
    const FluidThermodynamics&, const MacTransportState&, double seconds);
}
