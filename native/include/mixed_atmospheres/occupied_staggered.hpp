#pragma once

#include "volume_work.hpp"
#include <memory>

// Restricted material-coordinate experiment. This library is not linked into
// ma_core/the game DLL and has no Eulerian remap, runtime mode or save format.
namespace ma::experimental {
enum class StaggeredBoundary { traction_free, rigid_wall };
enum class StaggeredInitializationEnergy { preserve_temperature, preserve_total_energy };
struct StaggeredRezonePlan;
struct StaggeredRemapCandidate;
class HybridStaggeredState;
class HybridRezonePlan;
struct HybridRezoneCandidate;

struct StaggeredInputCell {
    MixtureInventory mixture;
    double bulk_momentum_kg_m_per_s = 0;
};
struct StaggeredInitialization {
    // Read only during initialization; copied into an actually const owned
    // allocation so a mutable alias to this input cannot change retained EOS.
    std::shared_ptr<const FluidThermodynamics> thermodynamics;
    std::vector<StaggeredInputCell> cells;
    // N+1 strictly ordered material nodes, or no nodes for exact vacuum.
    // Positions are relative to frame_origin_m; they need not start at zero.
    std::vector<double> positions_m, velocities_m_per_s;
    double frame_origin_m = 0, area_m2 = 1, gravity_m_per_s2 = 0;
    StaggeredBoundary left = StaggeredBoundary::traction_free, right = StaggeredBoundary::traction_free;
    StaggeredInitializationEnergy energy = StaggeredInitializationEnergy::preserve_temperature;
};
struct StaggeredInitializationLedger {
    double prior_bulk_kinetic_energy_j = 0, retained_kinetic_energy_j = 0;
    double additional_mechanical_energy_j = 0;
    double requested_internal_energy_change_j = 0, represented_internal_energy_change_j = 0;
    // Explicitly added energy for preserve_temperature, roundoff residual for
    // preserve_total_energy. Neither policy silently overwrites conflicting P.
    double declared_added_energy_j = 0, represented_total_energy_change_j = 0;
    double kinetic_partition_roundoff_j = 0;
    double momentum_reconciliation_kg_m_per_s = 0, maximum_momentum_arithmetic_bound_kg_m_per_s = 0;
    std::size_t unresolved_energy_conversions = 0;
};
struct StaggeredNode {
    double relative_position_m = 0, inertia_kg = 0, momentum_kg_m_per_s = 0;
    bool fixed = false;
    [[nodiscard]] double velocity_m_per_s() const noexcept { return momentum_kg_m_per_s / inertia_kg; }
};
struct StaggeredCell {
    MixtureInventory mixture;
    double occupied_volume_m3 = 0;
    // Derived views only. Canonical momentum is retained on the material nodes.
    double bulk_momentum_kg_m_per_s = 0, mechanical_variance_j = 0;
    ReferenceEquilibriumState thermodynamics;
};
struct StaggeredTotals {
    std::vector<double> masses_kg;
    double internal_energy_j = 0, nodal_kinetic_energy_j = 0, bulk_kinetic_energy_j = 0;
    double mechanical_variance_j = 0, momentum_kg_m_per_s = 0;
};
struct StaggeredStepReport {
    double seconds = 0, energy_balance_error_j = 0, momentum_balance_error_kg_m_per_s = 0;
    double represented_internal_energy_change_j = 0, represented_kinetic_energy_change_j = 0;
    double represented_potential_energy_change_j = 0, body_force_work_j = 0;
    double body_impulse_kg_m_per_s = 0, wall_reaction_impulse_kg_m_per_s = 0;
    double signed_geometry_volume_residual_m3 = 0, absolute_geometry_volume_residual_m3 = 0;
    double maximum_endpoint_relative_volume_residual = 0;
    double maximum_volume_addition_roundoff_m3 = 0, maximum_root_arithmetic_roundoff_m3 = 0;
    double maximum_canonical_velocity_roundoff_m_per_s = 0, pressure_work_roundoff_j = 0;
    // Signed -sum(p_work * root_volume_residual), distinct from arithmetic
    // and from external/body work. It is never deposited as source heat.
    double nonlinear_pressure_residual_work_j = 0;
    double gravity_geometry_work_residual_j = 0, energy_accounting_remainder_j = 0;
    double energy_accounting_arithmetic_bound_j = 0, momentum_arithmetic_bound_kg_m_per_s = 0;
    double origin_displacement_roundoff_m = 0;
    double represented_time_increment_seconds = 0, time_addition_roundoff_seconds = 0;
    std::size_t unresolved_energy_updates = 0, unresolved_volume_updates = 0, unresolved_position_updates = 0;
    std::size_t unresolved_momentum_updates = 0;
    std::size_t nonlinear_iterations = 0, pressure_iterations = 0;
};

class OccupiedStaggeredState {
public:
    OccupiedStaggeredState() = default; // uninitialized output placeholder
    [[nodiscard]] bool initialized() const noexcept { return static_cast<bool>(model_); }
    [[nodiscard]] const std::shared_ptr<const FluidThermodynamics>& model() const noexcept { return model_; }
    [[nodiscard]] const std::string& model_metadata() const noexcept { return metadata_; }
    [[nodiscard]] std::span<const StaggeredCell> cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const StaggeredNode> nodes() const noexcept { return nodes_; }
    [[nodiscard]] double origin_high_m() const noexcept { return origin_high_; }
    [[nodiscard]] double origin_low_m() const noexcept { return origin_low_; }
    [[nodiscard]] double area_m2() const noexcept { return area_; }
    [[nodiscard]] double gravity_m_per_s2() const noexcept { return gravity_; }
    [[nodiscard]] double time_seconds() const noexcept { return time_; }
    [[nodiscard]] const StaggeredInitializationLedger& initialization_ledger() const noexcept { return initialization_; }
    [[nodiscard]] double cumulative_energy_balance_error_j() const noexcept { return cumulative_energy_error_; }
    [[nodiscard]] double cumulative_geometry_volume_residual_m3() const noexcept { return cumulative_geometry_error_; }
    [[nodiscard]] StaggeredTotals totals() const;
private:
    friend OccupiedStaggeredState initialize_occupied_staggered(const StaggeredInitialization&);
    friend StaggeredStepReport advance_occupied_staggered(OccupiedStaggeredState&, double, PressurePreconditioner);
    friend StaggeredRemapCandidate rezone_occupied_staggered(const OccupiedStaggeredState&, const StaggeredRezonePlan&);
    friend HybridRezoneCandidate rezone_hybrid_staggered(const HybridStaggeredState&, const HybridRezonePlan&);
    std::shared_ptr<const FluidThermodynamics> model_;
    std::string metadata_;
    std::vector<StaggeredCell> cells_;
    std::vector<StaggeredNode> nodes_;
    StaggeredInitializationLedger initialization_;
    double origin_high_ = 0, origin_low_ = 0, area_ = 1, gravity_ = 0, time_ = 0;
    double cumulative_energy_error_ = 0, cumulative_geometry_error_ = 0;
    std::size_t anchor_ = 0;
};
[[nodiscard]] OccupiedStaggeredState initialize_occupied_staggered(const StaggeredInitialization& input);
void initialize_occupied_staggered_into(const StaggeredInitialization& input, OccupiedStaggeredState& output);
// One material Lagrangian pressure/body-force step; no cell averaging, remap,
// artificial viscosity, shock treatment or retry loop. Any exception leaves
// cells, nodal state, geometry, time, model identity, caches and ledgers intact.
[[nodiscard]] StaggeredStepReport advance_occupied_staggered(OccupiedStaggeredState& state, double seconds,
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic);
}
