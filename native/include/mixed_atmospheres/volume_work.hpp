#pragma once
#include "acoustics.hpp"
#include "mixture.hpp"
#include "pressure.hpp"
#include "fluid_thermodynamics.hpp"
#include <limits>
#include <cstdint>

namespace ma {
struct MaterialCell {
    MixtureInventory mixture;
    double volume_m3 = 1;
    // Used only for a completely filled incompressible liquid cell. Gas and
    // gas/liquid cells derive their pressure from the thermodynamic inventory.
    double incompressible_pressure_pa = 101325;
};

struct MaterialPressureStep {
    std::vector<MaterialCell> cells;
    std::vector<double> velocities_m_per_s;
    std::vector<double> work_pressures_pa; // time-discrete pressure in p dV
    std::vector<double> endpoint_pressures_pa;
    double body_force_work_j = 0;
    double energy_balance_error_j = 0;
    double maximum_volume_residual_m3 = 0;
    std::size_t nonlinear_iterations = 0;
    std::size_t pressure_iterations = 0;
    double maximum_linear_residual_norm = 0;
    double maximum_linear_residual_ratio = 0;
    std::size_t incomplete_cholesky_solves = 0, jacobi_solves = 0, preconditioner_fallbacks = 0;
};

// Reversible pressure-work substep on material (moving-volume) cells, with
// constant-capacity ideal gases and incompressible liquids at a common T.
// Species stay in their material cells. A discrete pressure gradient and
// midpoint face velocity conserve internal + staggered kinetic energy, with
// body-force work accounted separately. Cell entropy remains constant.
//
// This is NOT an Eulerian world step: geometry, conservative remapping of the
// staggered momentum, shocks/dissipation, vacuum, free surfaces, phase change,
// and immiscible separation remain the caller's responsibility. Faces must
// have compatible areas and fixed material inertias. Failure changes no input.
// project_incompressible_velocity admits non-solenoidal initial face velocity
// and enforces zero MIDPOINT divergence. It is used by the Eulerian remap
// integrator, where cell-to-face reconstruction need not be solenoidal; it is
// a reflecting midpoint projection, not dissipative backward-Euler projection.
// The default retains the stricter standalone material-state validation.
[[nodiscard]] MaterialPressureStep advance_material_pressure(
    const SpeciesRegistry& registry, std::span<const MaterialCell> cells,
    std::span<const AcousticFace> faces, double seconds,
    bool project_incompressible_velocity = false,
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic);

inline constexpr std::size_t no_material_cell = std::numeric_limits<std::size_t>::max();
// Oriented 1-D material node. Missing left/right material is exact vacuum.
// A wall has one material neighbor, fixed_zero_velocity=true, and supplies a
// reaction impulse. Every node's inertia is its adjacent half-cell masses.
struct OccupiedMaterialNode {
    std::size_t left = no_material_cell, right = no_material_cell;
    double area_m2 = 1, inertia_kg = 0, velocity_m_per_s = 0, acceleration_m_per_s2 = 0;
    bool fixed_zero_velocity = false;
};
struct OccupiedMaterialPressureStep : MaterialPressureStep {
    std::vector<double> volume_changes_m3;
    std::vector<double> volume_residuals_m3;
    double wall_reaction_impulse_kg_m_per_s = 0, body_impulse_kg_m_per_s = 0;
    double pressure_work_roundoff_j = 0;
    double maximum_volume_roundoff_m3 = 0;
    double maximum_volume_addition_roundoff_m3 = 0, maximum_root_arithmetic_roundoff_m3 = 0;
    double maximum_endpoint_relative_volume_residual = 0;
    std::size_t unresolved_energy_updates = 0, unresolved_volume_updates = 0;
    std::vector<std::uint8_t> zero_work_substituted;
    std::size_t zero_work_substitutions = 0;
    double maximum_omitted_pressure_pa = 0, omitted_elastic_work_bound_j = 0;
};
// Explicit model-aware overload: no legacy caloric/nominal-volume validation,
// no vacuum unknowns, and no fake material beyond a traction-free boundary.
// The work-root options control the nested isentropic inversion. A caller
// requesting near-roundoff volume closure must resolve that inversion more
// accurately than the reference model's ordinary pressure tolerance.
[[nodiscard]] OccupiedMaterialPressureStep advance_occupied_material_pressure(
    const FluidThermodynamics& thermodynamics, std::span<const MaterialCell> cells,
    std::span<const OccupiedMaterialNode> nodes, double seconds,
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic,
    LiquidFillRoundoffPolicy liquid_fill_roundoff = LiquidFillRoundoffPolicy::strict_eos,
    double maximum_relative_volume_residual = 5e-13,
    experimental::ReferenceRootOptions isentropic_root_options = {});
}
