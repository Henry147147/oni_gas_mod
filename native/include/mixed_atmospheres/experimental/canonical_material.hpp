#pragma once

#include "canonical_material_diagnostics.hpp"
#include "retained_scalar.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Finite experimental material-coordinate component. No world dispatch, remap,
// game ABI, scheduler, save format or default build dependency selects it.
namespace ma::experimental {
inline constexpr std::size_t canonical_owner_count = 7;
inline constexpr std::size_t canonical_max_nodes = 33;
inline constexpr std::size_t canonical_max_pieces = 32;
inline constexpr std::size_t canonical_max_species = 4;
using CanonicalVector = std::vector<double>;
using CanonicalMatrix = std::vector<CanonicalVector>;
using CanonicalSpecies = std::array<double, canonical_max_species>;

enum class CanonicalBoundaryKind { free, fixed, prescribed_velocity };
struct CanonicalBoundary {
    CanonicalBoundaryKind kind = CanonicalBoundaryKind::free;
    double velocity_m_per_s = 0;
    double exterior_pressure_pa = 0; // A constant nonnegative dead load, including zero.
};
struct CanonicalReferencePiece {
    std::size_t left = 0, right = 0, owner = 0;
    CanonicalSpecies density_left{}, density_right{}; // Reference kg/m3, in profile order.
    bool operator==(const CanonicalReferencePiece&) const = default;
};
enum class CanonicalInitialEnergy { stored_internal_energy, temperature };
struct CanonicalInitialization {
    SpeciesRegistry registry;
    std::string profile_version, expected_model_metadata;
    // A bounded printable token associated with full model bytes by the caller's
    // diagnostic capture. The token alone does not authenticate a model.
    std::string diagnostic_model_identity;
    std::vector<ReferenceMaterial> materials;
    CanonicalVector reference_nodes_m, positions_m, velocities_m_per_s;
    std::vector<CanonicalReferencePiece> pieces;
    std::array<std::uint64_t, canonical_owner_count> owner_ids{};
    CanonicalInitialEnergy energy = CanonicalInitialEnergy::stored_internal_energy;
    // Only the explicitly selected array is used; the other must remain zero.
    std::array<double, canonical_owner_count> stored_internal_energy_j{}, temperatures_k{};
    double area_m2 = 1, completed_time_seconds = 0;
    double gravity_down_m_per_s2 = 0; // Toward the negative material coordinate.
    CanonicalBoundary left, right;
};
struct CanonicalGeometryMeasure {
    RetainedScalarDifference length_m;
    // Exact area-scaled products of the length certificate. This derived
    // measure, not its scalar EOS projection, comes from the primary nodes.
    std::array<double, 8> volume_terms_m3{};
    std::size_t volume_term_count = 0;
    double projected_volume_m3 = 0;
    // Exact signed residual expression: sum(volume terms) - projection.
    // Never replace this expression by an independently rounded scalar zero.
    std::array<double, 9> projection_residual_terms_m3{};
};
struct CanonicalPrimaryValues {
    std::vector<RetainedScalar> relative_positions_m, momentum;
    std::array<RetainedScalar, canonical_owner_count> internal_energy_j{};
    RetainedScalar origin_m, completed_time_seconds;
};
struct CanonicalPieceReading {
    CanonicalGeometryMeasure geometry, reference_geometry;
    double jacobian = 0, mass_kg = 0;
    double first_mass_moment = 0, second_mass_moment = 0;
    double momentum = 0, position_velocity_product = 0, kinetic_energy_j = 0;
    CanonicalSpecies density_left{}, density_right{}, masses_kg{};
    std::array<double, 3> consistent_mass{}; // LL, LR, RR; no diagonal lumping.
    std::array<double, 2> local_momentum{}; // Derived, never independent primary state.
};
struct CanonicalOwnerReading {
    CanonicalSpecies masses_kg{};
    double mass_kg = 0, first_mass_moment = 0, second_mass_moment = 0;
    double momentum = 0, position_velocity_product = 0;
    double resolved_kinetic_energy_j = 0, bulk_kinetic_energy_j = 0, mechanical_variance_j = 0;
};
struct CanonicalInitializationLedger {
    std::array<double, canonical_owner_count> caloric_energy_j{}, explicitly_added_elastic_energy_j{};
    std::array<double, canonical_owner_count> represented_elastic_energy_change_j{}, energy_addition_roundoff_j{};
    double requested_kinetic_energy_j = 0, represented_kinetic_energy_j = 0;
    double kinetic_arithmetic_difference_j = 0, maximum_quadrature_ratio = 0;
    double maximum_thermodynamic_ratio = 0, maximum_linear_ratio = 0;
    CanonicalVector requested_velocity_m_per_s;
    // Flattened node-major rows, n*n entries. Products and sum remainders
    // preserve the finite initialization account for the actual stored pi.
    std::vector<std::array<double, 2>> momentum_product_terms, momentum_sum_remainders;
};
struct CanonicalFiniteExpression {
    // An exact signed real sum of known binary64 operands. This is diagnostic
    // data, never another primary state or an adaptive precision fallback.
    CanonicalVector terms;
    double projection = 0;
};
struct CanonicalExpressionRecord {
    std::string name;
    std::uint32_t node = canonical_absent_index, owner = canonical_absent_index;
    CanonicalFiniteExpression expression;
};
struct CanonicalStoredUpdate {
    std::string name;
    std::uint32_t node = canonical_absent_index, owner = canonical_absent_index;
    RetainedScalar before, increment;
    // Exact sum certificate, followed by the exact completed-state change.
    // before + increment = update.value + update.roundoff_remainders.
    RetainedScalarDifference update, represented_change;
};
struct CanonicalMomentumConstruction {
    std::uint32_t node = canonical_absent_index;
    std::vector<std::array<double, 2>> product_terms, sum_remainders;
    RetainedScalar value;
};
struct CanonicalRetainedStepLedger {
    RetainedScalarDifference origin_change, time_change;
    std::vector<RetainedScalarDifference> relative_position_change, momentum_change, velocity_change, internal_energy_change;
    std::vector<CanonicalFiniteExpression> displacement, volume_change;
    std::vector<CanonicalStoredUpdate> updates;
    std::vector<CanonicalMomentumConstruction> constrained_momentum;
    std::vector<CanonicalExpressionRecord> expressions;
};
struct CanonicalStepReport {
    double seconds = 0, represented_seconds = 0;
    double force_residual_ratio = 0, linear_residual_ratio = 0;
    double maximum_quadrature_ratio = 0, maximum_thermodynamic_ratio = 0, maximum_work_oracle_ratio = 0;
    double old_kinetic_energy_j = 0, new_kinetic_energy_j = 0;
    double energy_error_j = 0, energy_budget_j = 0, accounting_remainder_j = 0, accounting_bound_j = 0;
    double momentum_residual_work_j = 0, kinematic_defect_work_j = 0, kinetic_defect_j = 0;
    double trial_residual_work_j = 0; // Labelled trial diagnostic, not the old z.R0 energy interpretation.
    double exterior_work_left_j = 0, exterior_work_right_j = 0;
    double reaction_work_left_j = 0, reaction_work_right_j = 0;
    double gravitational_potential_change_j = 0, gravity_work_j = 0, gravity_arithmetic_defect_j = 0;
    double gravity_impulse = 0;
    double exterior_impulse_left = 0, exterior_impulse_right = 0, reaction_impulse_left = 0, reaction_impulse_right = 0;
    double momentum_error = 0, momentum_accounting_remainder = 0, momentum_accounting_bound = 0;
    double first_moment_change = 0, first_moment_error = 0, first_moment_accounting_remainder = 0, first_moment_bound = 0;
    double position_velocity_change = 0, position_velocity_accounting_remainder = 0, position_velocity_bound = 0;
    double relative_position_velocity_change = 0, origin_position_velocity_change = 0;
    double position_velocity_compatibility_change = 0, origin_compatibility_change = 0;
    double total_homogeneous_entropy_change = 0;
    std::size_t newton_updates = 0, backtracks = 0, factorizations = 0, constitutive_charges = 0;
    bool prescribed_displacements_exact = false;
    CanonicalVector requested_displacement, represented_displacement, scaled_residual, force_residual, force_scales;
    CanonicalVector old_style_trial_residual;
    CanonicalVector compatibility0, compatibility1, decoded_product0, decoded_product1;
    CanonicalVector delta_velocity, mass_delta_velocity, requested_impulse, represented_impulse;
    CanonicalVector free_addition_defect, constrained_linearity_defect, constrained_join_defect, constrained_impulse_defect;
    CanonicalVector constrained_linearity_bound, constrained_join_bound, constrained_impulse_bound;
    CanonicalVector pressure_force, exterior_force, gravity_force, reaction, actual_force, momentum_residual, kinematic_defect;
    CanonicalVector requested_volume_change, represented_volume_change, incidence_volume_change, volume_storage_difference;
    CanonicalVector balanced_energy_change, represented_energy_change, stored_energy_defect, secant_defect, volume_work_defect, owner_work_defect;
    CanonicalVector work_pressure, work_stiffness, entropy_change, entropy_cache_difference;
    CanonicalVector eos_represented_volume_change, eos_represented_energy_change;
    CanonicalVector cap_impulse_left, cap_impulse_right, cap_work_left, cap_work_right;
    std::vector<bool> endpoint_volume_resolves, endpoint_energy_resolves; // EOS endpoint flags, distinct from actual geometry storage.
    CanonicalRetainedStepLedger retained;
};
struct CanonicalSnapshot {
    // These allocations are actually const and deep owned. No mutable caller
    // model/registry alias is accepted by the public initialization interface.
    std::shared_ptr<const FluidThermodynamics> thermodynamics;
    std::shared_ptr<const SpeciesRegistry> registry;
    std::string model_metadata, diagnostic_model_identity;
    CanonicalPrimaryValues primary;
    std::array<CanonicalGeometryMeasure, canonical_owner_count> owner_geometry;
    // Scalar positions/momentum/time/origin and cell U/V are explicitly derived
    // projections for readouts and the current EOS interface. They are not
    // independently mutable authorities for the retained pressure step.
    CanonicalVector reference_nodes_m, positions_m, velocities_m_per_s, momentum;
    CanonicalMatrix consistent_mass;
    std::vector<CanonicalReferencePiece> reference_pieces;
    std::vector<CanonicalPieceReading> pieces;
    std::array<std::uint64_t, canonical_owner_count> owner_ids{};
    std::array<std::size_t, canonical_owner_count> owner_left{}, owner_right{};
    std::array<ReferenceConservedCell, canonical_owner_count> cells;
    std::array<ReferenceEquilibriumState, canonical_owner_count> thermodynamics_readings;
    std::array<CanonicalOwnerReading, canonical_owner_count> owners;
    CanonicalBoundary left, right;
    double area_m2 = 1, origin_high_m = 0, origin_low_m = 0, completed_time_seconds = 0;
    double gravity_down_m_per_s2 = 0;
    double resolved_kinetic_energy_j = 0, cumulative_energy_error_j = 0;
    std::uint64_t completed_steps = 0;
    CanonicalInitialEnergy initialization_energy = CanonicalInitialEnergy::stored_internal_energy;
    CanonicalInitializationLedger initialization;
    CanonicalStepReport last_step;
};

class CanonicalMaterialState;
class CanonicalReadView {
public:
    CanonicalReadView(const CanonicalReadView&) noexcept = default;
    CanonicalReadView& operator=(const CanonicalReadView&) noexcept = default;
    // Moves retain the source view too; every observable view stays owning/valid.
    CanonicalReadView(CanonicalReadView&& other) noexcept : data_(other.data_) {}
    CanonicalReadView& operator=(CanonicalReadView&& other) noexcept { data_ = other.data_; return *this; }
    [[nodiscard]] const CanonicalSnapshot& snapshot() const noexcept { return *data_; }
private:
    friend class CanonicalMaterialState;
    explicit CanonicalReadView(std::shared_ptr<const CanonicalSnapshot> data) noexcept : data_(std::move(data)) {}
    std::shared_ptr<const CanonicalSnapshot> data_;
};
struct CanonicalResourceCounts {
    std::size_t top_level_queries = 0, constitutive_charges = 0, oracle_iterations = 0;
    std::size_t factorizations = 0, newton_updates = 0, backtracks = 0;
    std::uint64_t predicates = 0;
};
namespace canonical_detail { class Kernel; }
class CanonicalBudgetContext {
public:
    CanonicalBudgetContext() = default;
    CanonicalBudgetContext(const CanonicalBudgetContext&) = delete;
    CanonicalBudgetContext& operator=(const CanonicalBudgetContext&) = delete;
    [[nodiscard]] CanonicalResourceCounts counts() const noexcept { return counts_; }
private:
    friend class canonical_detail::Kernel;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    CanonicalResourceCounts counts_;
};
class CanonicalMaterialState {
public:
    CanonicalMaterialState() = default; // An uninitialized output placeholder, never implicit vacuum.
    [[nodiscard]] bool initialized() const noexcept { return static_cast<bool>(data_); }
    [[nodiscard]] std::optional<CanonicalReadView> read_view() const noexcept;
private:
    friend void initialize_canonical_material_into(const CanonicalInitialization&, CanonicalMaterialState&, CanonicalBudgetContext&, CanonicalFailureWitness&);
    friend void advance_canonical_material_into(const CanonicalMaterialState&, double, CanonicalMaterialState&, CanonicalBudgetContext&, CanonicalFailureWitness&);
    std::shared_ptr<const CanonicalSnapshot> data_;
};
void initialize_canonical_material_into(const CanonicalInitialization& input, CanonicalMaterialState& output,
    CanonicalBudgetContext& budget, CanonicalFailureWitness& witness);
// Availability of this standalone retained implementation. This does not select
// a game runtime or establish production/colony acceptance.
[[nodiscard]] bool canonical_material_advance_ready() noexcept;
// One fixed-h operation with strong input/previous-output preservation. There is
// no timestep retry, scheduler registration, variance heat or endpoint fallback.
void advance_canonical_material_into(const CanonicalMaterialState& input, double seconds, CanonicalMaterialState& output,
    CanonicalBudgetContext& budget, CanonicalFailureWitness& witness);
// Complete diagnostic bytes only, not a save/restart protocol; no EOS query.
[[nodiscard]] std::vector<std::byte> canonical_material_diagnostic_bytes(const CanonicalReadView& view);
}
