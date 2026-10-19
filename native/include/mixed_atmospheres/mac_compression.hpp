#pragma once
#include "mac_transport.hpp"
#include <array>

namespace ma::experimental {
// monotone_pressure_sensed_acoustic is the pressure-sensed law with the
// monotonic limiter of Christensen (1990), in the form used by LULESH/ALE3D:
// linear term times (1 - phi), quadratic term times (1 - phi^2).
enum class MacCompressionLaw { quadratic, acoustic_quadratic, pressure_sensed_acoustic, monotone_pressure_sensed_acoustic };
// Dimensionless normalized pressure curvature. Exact zero for zero/affine
// triples; retains a positive ratio below binary64's exponent range.
[[nodiscard]] MacScaled mac_compression_pressure_sensor(double left_pa, double centre_pa, double right_pa);
// Monotonic limiter phi in [0, 1] for one cell along one axis. d is the cell's
// normal velocity jump u_right - u_left; dl and dr are its neighbours' jumps
// (zero where a wall, solid, empty cell or nonperiodic edge closes the stencil).
// phi = clamp(min((rl + rr)/2, 2 rl, 2 rr), 0, 1) with r = neighbour / d, and
// phi = 0 unless d < 0 (compression). Smooth compression (r near one) gives
// phi near one; a jump far above both neighbours or an alternating
// (ringing) pattern gives phi = 0. Ratios use full significand/exponent
// arithmetic and are clamped before projection.
[[nodiscard]] double mac_compression_monotone_limiter(MacScaled d, MacScaled dl, MacScaled dr);
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
// One immutable source-state coefficient construction shared by compression
// and its optional matching thermal regularization. No coefficients depend on
// heat deposited later within the call.
struct MacCompressionCoefficients {
    // Empty in quadratic mode; otherwise frozen half-sound speed per cell
    // (zero at solids), before any within-call heat deposition.
    std::vector<MacScaled> source_linear_speeds;
    std::vector<double> source_pressures_pa;
    std::array<std::vector<MacScaled>, 2> source_pressure_sensors;
    // Monotone law only: frozen per-axis limiter phi of each cell (zero at
    // solids, empty cells and expanding cells). Empty for the other laws.
    std::array<std::vector<double>, 2> source_monotone_limiters;
    [[nodiscard]] MacScaled linear_speed(std::size_t axis, std::size_t cell) const;
    // phi of the monotone law, or zero when no limiter was frozen.
    [[nodiscard]] double monotone_limiter(std::size_t axis, std::size_t cell) const;
};
[[nodiscard]] MacCompressionCoefficients freeze_mac_compression_coefficients(const SpeciesRegistry&,
    const FluidThermodynamics&, const MacTransportState&, MacCompressionLaw);
struct MacCompressionReport : MacCompressionCoefficients {
    std::vector<MacCompressionEvent> events;
    MacEnergy heat, energy_residual, energy_bound;
    MacScaled wall_impulse_x, wall_impulse_y;
    std::size_t unresolved_heat_updates = 0, unresolved_momentum_updates = 0;
    double entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
};
struct MacCompressionAdvance { MacTransportState state; MacCompressionReport report; };
// Quadratic remains the default separate component law. Acoustic mode freezes
// source-cell half-sound speeds for the forward/reverse half sweeps; this
// freezing is a first-order treatment of their thermal dependence.
// Species/passives and cell volumes stay fixed; kinetic loss heats the cell.
[[nodiscard]] MacCompressionAdvance advance_mac_compression(const SpeciesRegistry&,
    const FluidThermodynamics&, const MacTransportState&, double seconds,
    MacCompressionLaw = MacCompressionLaw::quadratic);
}
