#pragma once
#include "mac_transport.hpp"

namespace ma::experimental {
// Dynamic coefficients in Pa.s, frozen for this conductive/mechanical stage.
// These explicit surrogate values do not establish a production material law.
struct MacViscosity {
    double shear_pa_s = 0, bulk_pa_s = 0;
    void validate() const;
};
struct MacViscousDof {
    std::size_t axis = 0, face = 0;
    double inertia = 0;
    bool fixed = false;
    MacScaled gradient, before, after;
};
struct MacViscousMode {
    std::vector<MacViscousDof> dofs;
    MacScaled weight, strain, inverse_modal_inertia, relaxation, impulse;
    MacEnergy heat, energy_residual, energy_bound;
    MacScaled wall_impulse_x, wall_impulse_y;
    std::size_t unresolved_momentum_updates = 0;
};
// Exact rank-one stress relaxation, dJ/dt=-W*a*(a.u). Face inertia stays
// fixed. Every requested impulse and nonnegative kinetic-to-heat transfer is
// retained in range arithmetic, including below binary64's exponent range.
[[nodiscard]] MacViscousMode relax_mac_viscous_mode(std::span<const MacViscousDof>, MacScaled weight, double seconds);
struct MacViscousEvent {
    std::size_t kind = 0, location = 0, sweep = 0; // 0 normal difference, 1 divergence, 2 shear
    std::vector<std::size_t> heated_cells;
    MacViscousMode mode;
};
struct MacViscousReport {
    std::size_t modes = 0, unresolved_heat_updates = 0, unresolved_momentum_updates = 0;
    std::vector<MacViscousEvent> events;
    MacEnergy heat, energy_residual, energy_bound;
    MacScaled wall_impulse_x, wall_impulse_y;
    MacScaled momentum_residual_x, momentum_residual_y, momentum_bound_x, momentum_bound_y;
    // Rigorous fixed-mass/volume positive-heating bound Q_stored/T_after,
    // not a subtraction of two rounded EOS entropies.
    double entropy_lower_bound_j_per_k = 0;
};
struct MacViscousAdvance { MacTransportState state; MacViscousReport report; };
// Plane-strain Newtonian dissipation:
// mu*(du/dx-dv/dy)^2 + (mu/3+zeta)*(div u)^2 + mu*(du/dy+dv/dx)^2.
// Normal modes use cell centres; symmetric shear uses interior grid corners.
// Periodic or impermeable free-slip boundaries only. No inferred free surface,
// moving/no-slip solid coupling or temperature-dependent viscosity is supplied.
// Symmetric forward/reverse half sweeps retain masses/passives/volume and
// deposit each mode's kinetic loss into its actual supporting fluid cells.
[[nodiscard]] MacViscousAdvance advance_mac_viscosity(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, double seconds, MacViscosity);
}
