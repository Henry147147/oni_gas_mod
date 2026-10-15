#include "mixed_atmospheres/occupied_staggered.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <type_traits>

namespace ma::experimental {
namespace {
constexpr double eps = std::numeric_limits<double>::epsilon();
[[noreturn]] void configuration(const char* message) { throw FluidFatalError(FluidFatalCode::unsupported_configuration, message); }
[[noreturn]] void precision(const char* message) { throw FluidFatalError(FluidFatalCode::numerical_precision, message); }
[[noreturn]] void topology(const char* message) { throw FluidFatalError(FluidFatalCode::unsupported_topology, message); }
void finite(double value) { if (!std::isfinite(value)) precision("Nonfinite retained staggered arithmetic"); }
void input_finite(double value) { if (!std::isfinite(value)) configuration("Nonfinite retained staggered input"); }
// Error-free sum, including when MSVC long double has the double width.
struct Sum {
    double high = 0, low = 0;
    void add(double value) {
        const double sum = high + value, z = sum - high;
        low += (high - (sum - z)) + (value - z); high = sum;
    }
    [[nodiscard]] double value() const { return high + low; }
};
void translate_origin(double& high, double& low, double displacement) {
    Sum sum; sum.add(high); sum.add(low); sum.add(displacement);
    finite(sum.high); finite(sum.low);
    const double normalized = sum.high + sum.low, z = normalized - sum.high;
    const double remainder = (sum.high - (normalized - z)) + (sum.low - z);
    high = normalized; low = remainder;
}
double multiply(double a, double b) {
    const double value = a * b; finite(value);
    if (a != 0 && b != 0 && value == 0) precision("Positive retained-state product is below representable range");
    return value;
}
double kinetic(double mass, double velocity) {
    if (velocity == 0) return 0;
    if (0.5 * velocity == 0) precision("Positive retained kinetic energy is below representable range");
    return multiply(multiply(mass, 0.5 * velocity), velocity);
}
double velocity_momentum(double mass, double velocity) { return velocity == 0 ? 0 : multiply(mass, velocity); }
double nodal_kinetic(const StaggeredNode& node) {
    if (node.momentum_kg_m_per_s != 0 && 0.5 * node.velocity_m_per_s() == 0)
        precision("Positive canonical kinetic energy is below representable range");
    return node.momentum_kg_m_per_s == 0 ? 0 : multiply(node.momentum_kg_m_per_s, 0.5 * node.velocity_m_per_s());
}
double volume(double area, double left, double right) {
    if (!(right > left)) precision("Retained occupied material interval is not positively representable");
    return multiply(area, right - left);
}
void supported_state(const ReferenceEquilibriumState& thermo, double occupied) {
    if (thermo.vacuum_volume_m3 > 32 * eps * occupied)
        topology("Retained material interval needs an unsupported internal vacuum gap");
}
void derive_cell(StaggeredCell& cell, const StaggeredNode& left, const StaggeredNode& right,
                 double area, const FluidThermodynamics& model) {
    cell.occupied_volume_m3 = volume(area, left.relative_position_m, right.relative_position_m);
    const double ul = left.velocity_m_per_s(), ur = right.velocity_m_per_s();
    finite(ul); finite(ur);
    const double mass = cell.mixture.mass(), mean = std::midpoint(ul, ur);
    cell.bulk_momentum_kg_m_per_s = velocity_momentum(mass, mean);
    const double difference = ur - ul; finite(difference);
    if (difference != 0 && 0.5 * difference == 0) precision("Positive mechanical variance is below representable range");
    cell.mechanical_variance_j = difference == 0 ? 0 : kinetic(mass, 0.5 * difference);
    cell.thermodynamics = model.evaluate(cell.mixture, cell.occupied_volume_m3);
    supported_state(cell.thermodynamics, cell.occupied_volume_m3);
}
bool boundary_valid(StaggeredBoundary value) {
    return value == StaggeredBoundary::traction_free || value == StaggeredBoundary::rigid_wall;
}
double checked_cast(long double value) { const double result = static_cast<double>(value); finite(result); return result; }
std::vector<double> checked_species_totals(std::span<const StaggeredCell> cells, std::size_t species_count) {
    std::vector<double> totals(species_count);
    for (const auto& cell : cells) for (std::size_t s = 0; s < species_count; ++s) {
        totals[s] += cell.mixture.masses[s];
        if (!std::isfinite(totals[s])) precision("Retained species mass total is outside finite numerical range");
    }
    return totals;
}
}

OccupiedStaggeredState initialize_occupied_staggered(const StaggeredInitialization& input) {
    if (!input.thermodynamics || !boundary_valid(input.left) || !boundary_valid(input.right) ||
        (input.energy != StaggeredInitializationEnergy::preserve_temperature &&
         input.energy != StaggeredInitializationEnergy::preserve_total_energy))
        configuration("Invalid retained staggered model, boundary or energy policy");
    for (const double x : {input.frame_origin_m, input.area_m2, input.gravity_m_per_s2}) input_finite(x);
    if (input.area_m2 <= 0 || input.gravity_m_per_s2 < 0) configuration("Invalid retained cross-section or gravity");
    const auto count = input.cells.size();
    if (input.positions_m.size() != (count == 0 ? 0 : count + 1) || input.velocities_m_per_s.size() != input.positions_m.size())
        topology("Retained staggered state requires all material nodes together");
    OccupiedStaggeredState state;
    // A shared_ptr<const T> alone permits a caller's shared_ptr<T> alias to
    // replace the underlying model later. Own a genuinely const deep copy,
    // including registry, EOS profile vectors and their index mappings.
    state.model_ = std::make_shared<const FluidThermodynamics>(*input.thermodynamics);
    state.metadata_ = state.model_->reference().metadata();
    state.area_ = input.area_m2; state.gravity_ = input.gravity_m_per_s2; state.origin_high_ = input.frame_origin_m;
    if (count == 0) return state;
    state.anchor_ = input.left == StaggeredBoundary::rigid_wall ? 0 :
        (input.right == StaggeredBoundary::rigid_wall ? count : 0);
    for (const double position : input.positions_m) input_finite(position);
    translate_origin(state.origin_high_, state.origin_low_, input.positions_m[state.anchor_]);
    std::vector<double> masses; masses.reserve(count);
    for (const auto& source : input.cells) {
        state.model_->validate_inventory(source.mixture); input_finite(source.bulk_momentum_kg_m_per_s);
        const double mass = source.mixture.mass();
        if (!(mass > 0) || !std::isfinite(mass)) topology("Empty or disconnected material cells are not retained intervals");
        masses.push_back(mass);
    }
    state.nodes_.reserve(count + 1); state.cells_.resize(count);
    for (std::size_t j = 0; j <= count; ++j) {
        input_finite(input.velocities_m_per_s[j]);
        const bool fixed = (j == 0 && input.left == StaggeredBoundary::rigid_wall) ||
                           (j == count && input.right == StaggeredBoundary::rigid_wall);
        if (fixed && input.velocities_m_per_s[j] != 0) configuration("Rigid material wall must have zero velocity");
        const double inertia = (j == 0 ? 0 : multiply(0.5, masses[j - 1])) +
                               (j == count ? 0 : multiply(0.5, masses[j]));
        finite(inertia); if (!(inertia > 0)) precision("Retained node inertia is not positive");
        const double position = input.positions_m[j] - input.positions_m[state.anchor_]; finite(position);
        state.nodes_.push_back({position, inertia, velocity_momentum(inertia, input.velocities_m_per_s[j]), fixed});
    }
    long double old_kinetic = 0, new_kinetic = 0, requested_internal = 0, represented_internal = 0;
    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = state.cells_[i]; cell.mixture = input.cells[i].mixture;
        derive_cell(cell, state.nodes_[i], state.nodes_[i + 1], state.area_, *state.model_);
        const double input_volume = volume(state.area_, input.positions_m[i], input.positions_m[i + 1]);
        if (std::abs(cell.occupied_volume_m3 - input_volume) > 32 * eps * input_volume)
            precision("Rebasing retained geometry loses local occupied volume");
        const double ul = state.nodes_[i].velocity_m_per_s(), ur = state.nodes_[i + 1].velocity_m_per_s();
        const double supplied = input.cells[i].bulk_momentum_kg_m_per_s;
        const long double exact_mean_momentum = static_cast<long double>(masses[i]) *
            (0.5L * ul + 0.5L * ur);
        const double bound = checked_cast(32 * static_cast<long double>(eps) *
            (0.5L * masses[i] * (std::abs(static_cast<long double>(ul)) + std::abs(static_cast<long double>(ur))) + std::abs(supplied)));
        if (std::abs(exact_mean_momentum - supplied) > bound)
            configuration("Supplied bulk momentum conflicts with retained material nodes");
        state.initialization_.maximum_momentum_arithmetic_bound_kg_m_per_s =
            std::max(state.initialization_.maximum_momentum_arithmetic_bound_kg_m_per_s, bound);
        const double prior_k = supplied == 0 ? 0 : multiply(supplied, 0.5 * (supplied / masses[i]));
        const double retained_k = 0.5 * kinetic(masses[i], ul) + 0.5 * kinetic(masses[i], ur);
        const long double difference = static_cast<long double>(retained_k) - prior_k;
        old_kinetic += prior_k; new_kinetic += retained_k;
        if (input.energy == StaggeredInitializationEnergy::preserve_total_energy) {
            const double change = checked_cast(-difference), previous = cell.mixture.energy;
            cell.mixture.energy += change; finite(cell.mixture.energy);
            const long double represented = static_cast<long double>(cell.mixture.energy) - previous;
            requested_internal += change; represented_internal += represented;
            if (std::abs(represented - change) > 32 * static_cast<long double>(eps) * std::abs(change))
                ++state.initialization_.unresolved_energy_conversions;
            derive_cell(cell, state.nodes_[i], state.nodes_[i + 1], state.area_, *state.model_);
        }
    }
    long double canonical_kinetic = 0, canonical_momentum = 0, supplied_momentum = 0;
    for (const auto& node : state.nodes_) { canonical_kinetic += nodal_kinetic(node); canonical_momentum += node.momentum_kg_m_per_s; }
    for (const auto& cell : input.cells) supplied_momentum += cell.bulk_momentum_kg_m_per_s;
    auto& ledger = state.initialization_;
    ledger.kinetic_partition_roundoff_j = checked_cast(canonical_kinetic - new_kinetic);
    if (std::abs(ledger.kinetic_partition_roundoff_j) > 128 * eps * (canonical_kinetic + new_kinetic))
        precision("Retained initialization fails the local nodal/cell kinetic identity");
    ledger.prior_bulk_kinetic_energy_j = checked_cast(old_kinetic);
    ledger.retained_kinetic_energy_j = checked_cast(canonical_kinetic);
    ledger.additional_mechanical_energy_j = checked_cast(canonical_kinetic - old_kinetic);
    ledger.requested_internal_energy_change_j = checked_cast(requested_internal);
    ledger.represented_internal_energy_change_j = checked_cast(represented_internal);
    ledger.declared_added_energy_j = input.energy == StaggeredInitializationEnergy::preserve_temperature ?
        ledger.additional_mechanical_energy_j : 0;
    ledger.represented_total_energy_change_j = checked_cast(canonical_kinetic - old_kinetic + represented_internal);
    ledger.momentum_reconciliation_kg_m_per_s = checked_cast(canonical_momentum - supplied_momentum);
    if (input.energy == StaggeredInitializationEnergy::preserve_total_energy &&
        std::abs(ledger.represented_total_energy_change_j) > 2e-11 *
            (std::abs(ledger.additional_mechanical_energy_j) + std::abs(ledger.represented_internal_energy_change_j))) {
        // Small conversions can be below the ULP of formation-referenced U.
        // Report them, but require the residual to remain arithmetic at the
        // actual caloric/elastic scale, without using formation as a floor.
        long double scale = 0;
        for (const auto& cell : state.cells_) scale += cell.thermodynamics.thermal_energy_j + cell.thermodynamics.elastic_energy_j;
        if (std::abs(ledger.represented_total_energy_change_j) > 64 * eps * scale)
            precision("Retained initialization conversion has material represented energy loss");
    }
    // Constituent masses do not change during this material-only experiment.
    // Reject undefined public mass totals before publishing the initial state.
    static_cast<void>(checked_species_totals(state.cells_, state.model_->registry_size()));
    return state;
}

void initialize_occupied_staggered_into(const StaggeredInitialization& input, OccupiedStaggeredState& output) {
    auto candidate = initialize_occupied_staggered(input);
    static_assert(std::is_nothrow_move_assignable_v<OccupiedStaggeredState>);
    output = std::move(candidate);
}

StaggeredTotals OccupiedStaggeredState::totals() const {
    if (!model_) configuration("Retained staggered state is uninitialized");
    StaggeredTotals result; result.masses_kg = checked_species_totals(cells_, model_->registry_size());
    long double u = 0, nodal_k = 0, bulk_k = 0, variance = 0, momentum = 0;
    for (const auto& cell : cells_) {
        u += cell.mixture.energy; variance += cell.mechanical_variance_j;
        if (cell.bulk_momentum_kg_m_per_s != 0) bulk_k += multiply(cell.bulk_momentum_kg_m_per_s,
            0.5 * (cell.bulk_momentum_kg_m_per_s / cell.mixture.mass()));
    }
    for (const auto& node : nodes_) {
        nodal_k += nodal_kinetic(node); momentum += node.momentum_kg_m_per_s;
    }
    result.internal_energy_j = checked_cast(u); result.nodal_kinetic_energy_j = checked_cast(nodal_k);
    result.bulk_kinetic_energy_j = checked_cast(bulk_k); result.mechanical_variance_j = checked_cast(variance);
    result.momentum_kg_m_per_s = checked_cast(momentum); return result;
}

StaggeredStepReport advance_occupied_staggered(OccupiedStaggeredState& state, double dt, PressurePreconditioner pc) {
    if (!state.model_) configuration("Retained staggered state is uninitialized");
    if (!std::isfinite(dt) || dt < 0) configuration("Invalid retained staggered timestep");
    if (pc != PressurePreconditioner::automatic && pc != PressurePreconditioner::jacobi && pc != PressurePreconditioner::incomplete_cholesky)
        configuration("Invalid retained pressure preconditioner");
    StaggeredStepReport report;
    if (dt == 0) return report;
    auto candidate = state;
    candidate.time_ += dt; finite(candidate.time_);
    if (candidate.time_ == state.time_) precision("Retained timestep cannot advance represented time");
    report.seconds = dt;
    report.represented_time_increment_seconds = candidate.time_ - state.time_;
    report.time_addition_roundoff_seconds = report.represented_time_increment_seconds - dt;
    if (state.cells_.empty()) { state = std::move(candidate); return report; }
    std::vector<MaterialCell> material; std::vector<OccupiedMaterialNode> nodes;
    material.reserve(state.cells_.size()); nodes.reserve(state.nodes_.size());
    for (const auto& cell : state.cells_) material.push_back({cell.mixture, cell.occupied_volume_m3, 0});
    for (std::size_t j = 0; j < state.nodes_.size(); ++j) {
        const auto& node = state.nodes_[j];
        nodes.push_back({j == 0 ? no_material_cell : j - 1, j == state.cells_.size() ? no_material_cell : j,
            state.area_, node.inertia_kg, node.velocity_m_per_s(), -state.gravity_, node.fixed});
    }
    const auto pressure = advance_occupied_material_pressure(*state.model_, material, nodes, dt, pc,
        LiquidFillRoundoffPolicy::strict_eos);
    std::vector<double> mean_velocities, displacements; mean_velocities.reserve(nodes.size()); displacements.reserve(nodes.size());
    long double kinetic_change = 0, momentum_change = 0, potential_change = 0, scale = 0, internal_change = 0;
    long double accounting_scale = 0, impulse_scale = 0, nonlinear_work = 0;
    for (std::size_t j = 0; j < nodes.size(); ++j) {
        const auto& previous = state.nodes_[j]; auto& next = candidate.nodes_[j];
        next.momentum_kg_m_per_s = velocity_momentum(next.inertia_kg, pressure.velocities_m_per_s[j]);
        const double v0 = previous.velocity_m_per_s(), v1 = next.velocity_m_per_s();
        report.maximum_canonical_velocity_roundoff_m_per_s = std::max(report.maximum_canonical_velocity_roundoff_m_per_s,
            std::abs(v1 - pressure.velocities_m_per_s[j]));
        if (std::abs(v1 - pressure.velocities_m_per_s[j]) > 32 * eps * std::max(std::abs(v1), std::abs(pressure.velocities_m_per_s[j])))
            precision("Retained canonical momentum cannot represent pressure velocity");
        mean_velocities.push_back(std::midpoint(v0, v1));
        const double displacement = mean_velocities.back() == 0 ? 0 : multiply(dt, mean_velocities.back());
        displacements.push_back(displacement);
        kinetic_change += static_cast<long double>(nodal_kinetic(next)) - nodal_kinetic(previous);
        momentum_change += static_cast<long double>(next.momentum_kg_m_per_s) - previous.momentum_kg_m_per_s;
        scale += nodal_kinetic(next) + nodal_kinetic(previous);
        accounting_scale += nodal_kinetic(next) + nodal_kinetic(previous);
        const double pl = j == 0 ? 0 : pressure.work_pressures_pa[j - 1];
        const double pr = j == state.cells_.size() ? 0 : pressure.work_pressures_pa[j];
        const double requested_impulse = previous.fixed ? 0 : dt * (state.area_ * (pl - pr) - previous.inertia_kg * state.gravity_);
        finite(requested_impulse);
        const long double actual_impulse = static_cast<long double>(next.momentum_kg_m_per_s) - previous.momentum_kg_m_per_s;
        if (std::abs(actual_impulse - requested_impulse) > 32 * static_cast<long double>(eps) * std::abs(requested_impulse))
            ++report.unresolved_momentum_updates;
        const long double individual_impulses = dt * (state.area_ *
            (std::abs(static_cast<long double>(pl)) + std::abs(static_cast<long double>(pr))) + next.inertia_kg * state.gravity_);
        impulse_scale += individual_impulses;
        accounting_scale += individual_impulses * std::abs(mean_velocities.back());
    }
    const double anchor_displacement = displacements[state.anchor_];
    translate_origin(candidate.origin_high_, candidate.origin_low_, anchor_displacement);
    if (anchor_displacement != 0 && candidate.origin_high_ == state.origin_high_ && candidate.origin_low_ == state.origin_low_)
        precision("Retained translation is unresolved in the compensated origin");
    Sum actual_translation; actual_translation.add(candidate.origin_high_); actual_translation.add(-state.origin_high_);
    actual_translation.add(candidate.origin_low_); actual_translation.add(-state.origin_low_);
    Sum origin_error = actual_translation; origin_error.add(-anchor_displacement);
    report.origin_displacement_roundoff_m = origin_error.value();
    if (std::abs(report.origin_displacement_roundoff_m) > 32 * eps * std::abs(anchor_displacement))
        precision("Compensated origin cannot represent the local material displacement");
    for (std::size_t j = 0; j < nodes.size(); ++j) {
        const double relative_delta = mean_velocities[j] == mean_velocities[state.anchor_] ? 0 :
            multiply(dt, mean_velocities[j] - mean_velocities[state.anchor_]);
        auto& position = candidate.nodes_[j].relative_position_m;
        position += relative_delta; finite(position);
        if (relative_delta != 0 && position == state.nodes_[j].relative_position_m) ++report.unresolved_position_updates;
    }
    for (std::size_t i = 0; i < state.cells_.size(); ++i) {
        const auto& old = state.cells_[i]; auto& next = candidate.cells_[i]; next.mixture = pressure.cells[i].mixture;
        derive_cell(next, candidate.nodes_[i], candidate.nodes_[i + 1], state.area_, *state.model_);
        Sum geometry; geometry.add(next.occupied_volume_m3); geometry.add(-old.occupied_volume_m3);
        geometry.add(-pressure.volume_changes_m3[i]); geometry.add(pressure.volume_residuals_m3[i]);
        const double error = geometry.value(); finite(error);
        report.signed_geometry_volume_residual_m3 += error; report.absolute_geometry_volume_residual_m3 += std::abs(error);
        if (std::abs(error) > 32 * eps * next.occupied_volume_m3)
            precision("Retained endpoint geometry exceeds its local-volume arithmetic bound");
        internal_change += static_cast<long double>(next.mixture.energy) - old.mixture.energy;
        nonlinear_work -= static_cast<long double>(pressure.work_pressures_pa[i]) * pressure.volume_residuals_m3[i];
        accounting_scale += std::abs(static_cast<long double>(next.mixture.energy) - old.mixture.energy) +
            std::abs(static_cast<long double>(pressure.work_pressures_pa[i]) * pressure.volume_changes_m3[i]);
        // Half-cell inertia is the quadrature of occupied centroid gravity.
        const long double delta_centroid = static_cast<long double>(actual_translation.high) + actual_translation.low +
            0.5L * (static_cast<long double>(candidate.nodes_[i].relative_position_m) - state.nodes_[i].relative_position_m) +
            0.5L * (static_cast<long double>(candidate.nodes_[i + 1].relative_position_m) - state.nodes_[i + 1].relative_position_m);
        potential_change += old.mixture.mass() * state.gravity_ * delta_centroid;
        scale += old.thermodynamics.thermal_energy_j + next.thermodynamics.thermal_energy_j +
            old.thermodynamics.elastic_energy_j + next.thermodynamics.elastic_energy_j;
    }
    const long double energy_error = internal_change + kinetic_change + potential_change;
    const long double momentum_error = momentum_change - pressure.body_impulse_kg_m_per_s - pressure.wall_reaction_impulse_kg_m_per_s;
    scale += std::abs(potential_change) + std::abs(pressure.body_force_work_j);
    if (std::abs(energy_error) > 2e-11L * scale) precision("Retained state fails represented mechanical energy balance");
    long double momentum_scale = impulse_scale;
    for (std::size_t j = 0; j < nodes.size(); ++j)
        momentum_scale += std::abs(state.nodes_[j].momentum_kg_m_per_s) + std::abs(candidate.nodes_[j].momentum_kg_m_per_s);
    report.momentum_arithmetic_bound_kg_m_per_s = checked_cast(64 * eps * momentum_scale);
    if (std::abs(momentum_error) > report.momentum_arithmetic_bound_kg_m_per_s) precision("Retained state fails represented momentum balance");
    report.nonlinear_pressure_residual_work_j = checked_cast(nonlinear_work);
    report.gravity_geometry_work_residual_j = checked_cast(potential_change + pressure.body_force_work_j);
    report.energy_accounting_remainder_j = checked_cast(energy_error - nonlinear_work - pressure.pressure_work_roundoff_j -
        report.gravity_geometry_work_residual_j);
    report.energy_accounting_arithmetic_bound_j = checked_cast(128 * eps * (accounting_scale + std::abs(pressure.pressure_work_roundoff_j)));
    if (std::abs(report.energy_accounting_remainder_j) > report.energy_accounting_arithmetic_bound_j)
        precision("Retained energy remainder exceeds individual force/work arithmetic budget");
    report.energy_balance_error_j = checked_cast(energy_error); report.momentum_balance_error_kg_m_per_s = checked_cast(momentum_error);
    report.represented_internal_energy_change_j = checked_cast(internal_change);
    report.represented_kinetic_energy_change_j = checked_cast(kinetic_change);
    report.represented_potential_energy_change_j = checked_cast(potential_change);
    report.body_force_work_j = pressure.body_force_work_j;
    report.body_impulse_kg_m_per_s = pressure.body_impulse_kg_m_per_s;
    report.wall_reaction_impulse_kg_m_per_s = pressure.wall_reaction_impulse_kg_m_per_s;
    report.maximum_endpoint_relative_volume_residual = pressure.maximum_endpoint_relative_volume_residual;
    report.maximum_volume_addition_roundoff_m3 = pressure.maximum_volume_addition_roundoff_m3;
    report.maximum_root_arithmetic_roundoff_m3 = pressure.maximum_root_arithmetic_roundoff_m3;
    report.pressure_work_roundoff_j = pressure.pressure_work_roundoff_j;
    report.unresolved_energy_updates = pressure.unresolved_energy_updates;
    report.unresolved_volume_updates = pressure.unresolved_volume_updates;
    report.nonlinear_iterations = pressure.nonlinear_iterations; report.pressure_iterations = pressure.pressure_iterations;
    candidate.cumulative_energy_error_ += report.energy_balance_error_j; finite(candidate.cumulative_energy_error_);
    candidate.cumulative_geometry_error_ += report.signed_geometry_volume_residual_m3; finite(candidate.cumulative_geometry_error_);
    static_assert(std::is_nothrow_move_assignable_v<OccupiedStaggeredState>);
    state = std::move(candidate); return report;
}
}
