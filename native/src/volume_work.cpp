#include "mixed_atmospheres/volume_work.hpp"
#include "mixed_atmospheres/pressure.hpp"
#include "occupied_material_work_detail.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <limits>
#include <sstream>
#include <iomanip>

namespace ma {
namespace {
struct Closure {
    double free_volume, exponent, thermal_energy, initial_pressure;
};
struct Response {
    double work_pressure, endpoint_pressure, compliance, energy_change;
};
// Error-free TwoSum for finite additions (gradual underflow), followed by a
// compensated reduction. The residual contains three terms per 1-D cell;
// the bound covers rounding in its correction and final addition, without a
// unit floor or a bound proportional to mutually cancelling face sweeps.
struct VolumeSum {
    double sum = 0, correction = 0, error_magnitudes = 0;
    void add(double value) {
        const double next = sum + value, recovered = next - sum;
        const double error = (sum - (next - recovered)) + (value - recovered);
        sum = next; correction += error; error_magnitudes += std::abs(error);
    }
    [[nodiscard]] double value() const { return sum + correction; }
    [[nodiscard]] double roundoff_bound() const {
        return 4 * std::numeric_limits<double>::epsilon() * (error_magnitudes + std::abs(value()));
    }
};

Response response(const Closure& state, double volume_change) {
    const double x = volume_change / state.free_volume;
    if (!std::isfinite(x) || x <= -1) throw std::domain_error("Pressure work exhausted gas headspace");
    const double logarithm = std::log1p(x), k = state.exponent;
    const double change = state.thermal_energy * std::expm1(-k * logarithm);
    const double endpoint = state.initial_pressure * std::exp(-(k + 1) * logarithm);
    double work, stiffness;
    if (std::abs(x) * (1 + k) < 1e-3) {
        // Secant of U(V) and its derivative near dV=0, without subtracting
        // almost equal pressures. f = sum (-1)^n (k+1)...(k+n)x^n/(n+1)!.
        double coefficient = 1, power = 1, f = 1, derivative = 0;
        for (int n = 1; n <= 8; ++n) {
            coefficient *= -(k + n) / (n + 1);
            derivative += n * coefficient * power;
            power *= x;
            f += coefficient * power;
        }
        work = state.initial_pressure * f;
        stiffness = -state.initial_pressure / state.free_volume * derivative;
    } else {
        work = -change / volume_change;
        stiffness = (work - endpoint) / volume_change;
    }
    require_finite(change, "adiabatic energy change");
    require_finite(endpoint, "adiabatic endpoint pressure");
    require_finite(work, "adiabatic work pressure");
    require_finite(stiffness, "adiabatic pressure stiffness");
    if (work <= 0 || endpoint <= 0 || stiffness <= 0)
        throw std::domain_error("Unsupported adiabatic state");
    const double compliance = 1 / stiffness;
    require_finite(compliance, "adiabatic compliance");
    if (compliance == 0) throw std::domain_error("Adiabatic compliance underflow");
    return {work, endpoint, compliance, change};
}
}

MaterialPressureStep advance_material_pressure(const SpeciesRegistry& registry,
    std::span<const MaterialCell> cells, std::span<const AcousticFace> faces, double dt,
    bool project_incompressible_velocity, PressurePreconditioner preconditioner) {
    require_finite(dt, "material pressure timestep");
    if (dt < 0 || cells.empty()) throw std::invalid_argument("Invalid material pressure domain or timestep");
    const auto count = cells.size();
    std::vector<Closure> closures(count);
    std::vector<double> volume_change(count, 0), work(count), compliance(count), residual(count);
    std::vector<double> volume_scale(count);
    std::vector<std::size_t> parents(count);
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](std::size_t i) {
        while (parents[i] != i) { parents[i] = parents[parents[i]]; i = parents[i]; }
        return i;
    };
    MaterialPressureStep result;
    result.cells.assign(cells.begin(), cells.end());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& cell = cells[i];
        cell.mixture.validate(registry);
        require_finite(cell.volume_m3, "material cell volume");
        require_finite(cell.incompressible_pressure_pa, "incompressible pressure");
        if (cell.volume_m3 <= 0 || cell.mixture.mass() == 0 || cell.incompressible_pressure_pa < 0)
            throw std::invalid_argument("Material pressure requires positive occupied cells");
        for (std::size_t s = 0; s < registry.size(); ++s)
            if (cell.mixture.masses[s] != 0 && registry[s].phase == Phase::solid)
                throw std::domain_error("Material pressure does not move fixed solids");
        const double n_r = cell.mixture.gas_moles(registry) * gas_constant;
        const double headspace = cell.volume_m3 - cell.mixture.condensed_volume(registry);
        if (n_r == 0) {
            if (std::abs(headspace) > 1e-12 * cell.volume_m3)
                throw std::domain_error("Partial liquid fill requires a free-surface treatment");
            work[i] = cell.incompressible_pressure_pa;
        } else {
            if (headspace <= 0) throw std::domain_error("Gas has no headspace");
            const double capacity = cell.mixture.heat_capacity(registry);
            const double thermal = capacity * cell.mixture.temperature(registry);
            require_finite(thermal, "material thermal energy");
            closures[i] = {headspace, n_r / capacity, thermal, n_r * cell.mixture.temperature(registry) / headspace};
            work[i] = response(closures[i], 0).work_pressure;
        }
        volume_scale[i] = n_r == 0 ? cell.volume_m3 : headspace;
    }
    std::vector<PressureFace> matrix_faces;
    matrix_faces.reserve(faces.size());
    std::vector<double> initial_outflow(count, 0), initial_flow_scale(count, 0);
    for (const auto& face : faces) {
        require_finite(face.area_m2, "material face area");
        require_finite(face.inertia_kg, "material face inertia");
        require_finite(face.velocity_m_per_s, "material face velocity");
        require_finite(face.acceleration_m_per_s2, "material acceleration");
        if (face.left >= count || face.right >= count || face.left == face.right || face.area_m2 <= 0 || face.inertia_kg <= 0)
            throw std::invalid_argument("Invalid material pressure face");
        require_finite(0.5 * face.inertia_kg * face.velocity_m_per_s * face.velocity_m_per_s, "material initial kinetic energy");
        parents[root(face.right)] = root(face.left);
        const double flow = face.area_m2 * face.velocity_m_per_s;
        require_finite(flow, "material initial volume flux");
        initial_outflow[face.left] += flow; initial_outflow[face.right] -= flow;
        initial_flow_scale[face.left] += std::abs(flow); initial_flow_scale[face.right] += std::abs(flow);
        const double weight = 0.5 * dt * face.area_m2 * (dt * face.area_m2 / face.inertia_kg);
        require_finite(weight, "material pressure coefficient");
        matrix_faces.push_back({face.left, face.right, weight});
    }
    for (std::size_t i = 0; i < count; ++i) {
        require_finite(initial_outflow[i], "material initial outflow");
        require_finite(initial_flow_scale[i], "material initial volume flux scale");
        if (!project_incompressible_velocity && closures[i].free_volume == 0 &&
            std::abs(initial_outflow[i]) > 1e-12 + 1e-10 * initial_flow_scale[i])
            throw std::domain_error("Initial velocity violates incompressible volume continuity");
    }
    std::vector<bool> compressible(count, false);
    std::vector<double> gauge_scale(count, 0);
    for (std::size_t i = 0; i < count; ++i)
        if (closures[i].free_volume > 0) compressible[root(i)] = true;
    for (const auto& face : matrix_faces)
        gauge_scale[root(face.left)] = std::max(gauge_scale[root(face.left)], face.conductance);

    const auto evaluate = [&](const std::vector<double>& changes, std::vector<double>& pressures,
                              std::vector<double>& capacities, std::vector<double>& errors) {
        double norm = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (closures[i].free_volume > 0) {
                const auto local = response(closures[i], changes[i]);
                pressures[i] = local.work_pressure;
                capacities[i] = local.compliance;
            } else capacities[i] = 0;
            errors[i] = changes[i];
        }
        for (const auto& face : faces) {
            // Evaluate gravity with the old pressure force before multiplying
            // by dt. A balanced hydrostatic state has exactly zero drive.
            const double force = face.area_m2 * (pressures[face.left] - pressures[face.right]) +
                                 face.inertia_kg * face.acceleration_m_per_s2;
            const double swept = dt * face.area_m2 * (face.velocity_m_per_s + 0.5 * dt * force / face.inertia_kg);
            require_finite(swept, "material swept volume");
            errors[face.left] -= swept;
            errors[face.right] += swept;
        }
        for (std::size_t i = 0; i < count; ++i) {
            require_finite(errors[i], "material volume residual");
            norm = std::max(norm, std::abs(errors[i]) / volume_scale[i]);
        }
        require_finite(norm, "material relative volume residual");
        return norm;
    };
    double norm = evaluate(volume_change, work, compliance, residual);
    constexpr double tolerance = 2e-12;
    while (norm > tolerance) {
        if (result.nonlinear_iterations == 60)
            throw std::runtime_error("Material pressure did not converge; reduce timestep");
        auto diagonal = compliance;
        for (std::size_t i = 0; i < count; ++i) if (root(i) == i && !compressible[i])
            diagonal[i] = gauge_scale[i] > 0 ? gauge_scale[i] : 1;
        const PressureSystem matrix(std::move(diagonal), matrix_faces, preconditioner);
        const auto solved = matrix.solve(residual, std::vector<double>(count), 1e-12, 1e-15);
        result.pressure_iterations += solved.iterations;
        result.maximum_linear_residual_norm = std::max(result.maximum_linear_residual_norm, solved.residual_norm);
        result.maximum_linear_residual_ratio = std::max(result.maximum_linear_residual_ratio,
            solved.residual_norm / solved.residual_threshold);
        if (solved.iterations > 0) {
            if (solved.preconditioner_used == PressurePreconditioner::incomplete_cholesky) ++result.incomplete_cholesky_solves;
            else ++result.jacobi_solves;
        }
        if (solved.preconditioner_fallback) ++result.preconditioner_fallbacks;
        double fraction = 1;
        for (std::size_t i = 0; i < count; ++i) if (closures[i].free_volume > 0) {
            const double change = -compliance[i] * solved.pressure[i];
            if (change < 0) fraction = std::min(fraction, 0.8 * (closures[i].free_volume + volume_change[i]) / -change);
        }
        bool accepted = false;
        for (int attempt = 0; attempt < 40; ++attempt) {
            auto next_volume = volume_change, next_work = work;
            std::vector<double> next_compliance(count), next_residual(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (closures[i].free_volume > 0) next_volume[i] -= fraction * compliance[i] * solved.pressure[i];
                else next_work[i] += fraction * solved.pressure[i];
            }
            const double next_norm = evaluate(next_volume, next_work, next_compliance, next_residual);
            if (next_norm <= tolerance || next_norm < norm * (1 - 1e-4 * fraction)) {
                volume_change = std::move(next_volume); work = std::move(next_work);
                compliance = std::move(next_compliance); residual = std::move(next_residual);
                norm = next_norm; accepted = true; break;
            }
            fraction *= 0.5;
        }
        if (!accepted) throw std::runtime_error("Material pressure line search failed; reduce timestep");
        ++result.nonlinear_iterations;
    }
    long double energy_change = 0, body_work = 0, energy_scale = 0;
    result.work_pressures_pa = work;
    result.endpoint_pressures_pa.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = result.cells[i];
        if (closures[i].free_volume > 0) {
            const auto local = response(closures[i], volume_change[i]);
            cell.volume_m3 += volume_change[i];
            require_finite(cell.volume_m3, "updated material volume");
            cell.mixture.energy += local.energy_change;
            const double actual_pressure = cell.mixture.pressure(registry, cell.volume_m3);
            if (std::abs(actual_pressure - local.endpoint_pressure) > 1e-9 * local.endpoint_pressure)
                throw std::domain_error("Insufficient precision for material headspace or thermal energy");
            result.endpoint_pressures_pa[i] = local.endpoint_pressure;
            energy_change += static_cast<long double>(cell.mixture.energy) - cells[i].mixture.energy;
            // Formation energies can dwarf sensible energy; using their
            // magnitude would conceal pressure-work errors in liquid water.
            energy_scale += closures[i].thermal_energy;
        } else {
            if (work[i] < 0) throw std::domain_error("Liquid tension requires a cavitation model");
            cell.incompressible_pressure_pa = work[i];
            result.endpoint_pressures_pa[i] = work[i];
            energy_scale += cells[i].mixture.heat_capacity(registry) * cells[i].mixture.temperature(registry);
        }
        cell.mixture.validate(registry);
        result.maximum_volume_residual_m3 = std::max(result.maximum_volume_residual_m3, std::abs(residual[i]));
    }
    for (const auto& face : faces) {
        const double force = face.area_m2 * (work[face.left] - work[face.right]) +
                             face.inertia_kg * face.acceleration_m_per_s2;
        const double next = face.velocity_m_per_s + dt * force / face.inertia_kg;
        require_finite(next, "material updated velocity");
        require_finite(0.5 * face.inertia_kg * next * next, "material updated kinetic energy");
        result.velocities_m_per_s.push_back(next);
        const long double mean = 0.5L * (static_cast<long double>(next) + face.velocity_m_per_s);
        energy_change += face.inertia_kg * (static_cast<long double>(next) - face.velocity_m_per_s) * mean;
        body_work += dt * face.inertia_kg * face.acceleration_m_per_s2 * mean;
        energy_scale += face.inertia_kg * (static_cast<long double>(next) * next +
                                         static_cast<long double>(face.velocity_m_per_s) * face.velocity_m_per_s);
    }
    result.body_force_work_j = static_cast<double>(body_work);
    result.energy_balance_error_j = static_cast<double>(energy_change - body_work);
    require_finite(result.body_force_work_j, "material body work");
    require_finite(result.energy_balance_error_j, "material energy residual");
    require_finite(static_cast<double>(energy_scale), "material energy scale");
    if (std::abs(energy_change - body_work) > 1e-8L + 2e-11L * energy_scale)
        throw std::runtime_error("Material pressure failed total-energy balance");
    return result;
}

OccupiedMaterialPressureStep detail::advance_occupied_material_pressure_with_work(const FluidThermodynamics& model,
    std::span<const MaterialCell> cells, std::span<const OccupiedMaterialNode> nodes, double dt,
    PressurePreconditioner preconditioner, LiquidFillRoundoffPolicy roundoff_policy, double tolerance,
    experimental::ReferenceRootOptions isentropic_root_options,const OccupiedWorkLaw& custom_work) {
    require_finite(dt, "occupied material timestep");
    if (dt <= 0 || cells.empty()) throw std::invalid_argument("Occupied material pressure requires cells and positive time");
    if (!std::isfinite(tolerance) || tolerance < 8 * std::numeric_limits<double>::epsilon() || tolerance > 5e-13)
        throw std::invalid_argument("Occupied pressure volume tolerance is outside the supported stricter range");
    if (roundoff_policy != LiquidFillRoundoffPolicy::strict_eos && roundoff_policy != LiquidFillRoundoffPolicy::balanced_local_roundoff)
        throw std::invalid_argument("Invalid liquid fill representability policy");
    const auto count = cells.size();
    std::vector<experimental::ReferenceIsentrope> anchors;
    std::vector<bool> roundoff_anchor;
    anchors.reserve(count);
    for (const auto& cell : cells) {
        if (cell.mixture.mass() <= 0) throw std::invalid_argument("Vacuum has no pressure unknown");
        anchors.push_back(model.reference().make_isentrope(model.to_reference(cell.mixture, cell.volume_m3)));
        roundoff_anchor.push_back(roundoff_policy == LiquidFillRoundoffPolicy::balanced_local_roundoff &&
            model.liquid_fill_resolution(anchors.back().reference_cell()).within_roundoff);
    }
    std::vector<PressureFace> edges;
    std::vector<double> boundary_diagonal(count, 0);
    for (const auto& node : nodes) {
        const bool left = node.left != no_material_cell, right = node.right != no_material_cell;
        for (const double value : {node.area_m2, node.inertia_kg, node.velocity_m_per_s, node.acceleration_m_per_s2})
            require_finite(value, "occupied node input");
        if ((!left && !right) || (left && node.left >= count) || (right && node.right >= count) ||
            (left && right && node.left == node.right) || node.area_m2 <= 0 || node.inertia_kg <= 0 ||
            (node.fixed_zero_velocity && ((left && right) || node.velocity_m_per_s != 0)))
            throw std::invalid_argument("Invalid occupied material node");
        if (node.fixed_zero_velocity) continue;
        const double weight = 0.5 * dt * node.area_m2 * (dt * node.area_m2 / node.inertia_kg);
        if (!std::isfinite(weight) || weight <= 0)
            throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied pressure coefficient is unrepresentable");
        if (left && right) edges.push_back({node.left, node.right, weight});
        else boundary_diagonal[left ? node.left : node.right] += weight;
    }
    struct TrialVoid final : std::domain_error { TrialVoid() : std::domain_error("Occupied trial creates internal liquid void") {} };
    bool saw_void = false;
    std::vector<double> changes(count, 0), work(count), compliance(count), residual(count);
    std::vector<double> endpoint_volumes(count), root_arithmetic_roundoff(count);
    std::vector<bool> roundoff_trial(count, false);
    const auto void_message = [&] {
        std::ostringstream message; message << std::setprecision(17) << "Occupied pressure root requires internal cavitation/gap creation";
        for (std::size_t i = 0; i < count; ++i) if (!anchors[i].reference_state().has_gas)
            message << "; cell=" << i << " p0=" << anchors[i].reference_state().mechanical_pressure_pa <<
                " V0=" << cells[i].volume_m3 << " dV=" << changes[i] << " residual=" << residual[i];
        return message.str();
    };
    const auto evaluate = [&](const std::vector<double>& delta, std::vector<double>& pressures,
                              std::vector<double>& capacities, std::vector<double>& errors) {
        double norm = 0;
        std::vector<VolumeSum> sums(count);
        for (std::size_t i = 0; i < count; ++i) {
            experimental::ReferenceIsentropicResponse response;
            double derivative = 0;
            if (delta[i] == 0) response = model.reference().isentropic_response(anchors[i], delta[i], isentropic_root_options);
            else {
                auto evaluated = model.reference().isentropic_work(anchors[i], delta[i], isentropic_root_options);
                derivative = evaluated.work_pressure_volume_derivative_pa_per_m3;
                response = std::move(evaluated.response);
            }
            endpoint_volumes[i] = response.endpoint.volume_m3;
            const double void_budget = 32 * std::numeric_limits<double>::epsilon() * response.endpoint.volume_m3;
            if (response.state.vacuum_volume_m3 > void_budget) { saw_void = true; throw TrialVoid(); }
            const bool roundoff_fill = roundoff_anchor[i] && model.liquid_fill_resolution(response.endpoint).within_roundoff;
            roundoff_trial[i] = roundoff_fill;
            pressures[i] = roundoff_fill ? 0 : response.work_pressure_pa;
            if (delta[i] == 0 || (!response.state.has_gas && response.work_pressure_pa == 0))
                derivative = -0.5 / model.compression_compliance(anchors[i]);
            if (custom_work) {
                if (roundoff_fill) throw std::invalid_argument("A constitutive work override requires the strict occupied EOS branch");
                const auto local=custom_work(i,delta[i],anchors[i],response,derivative);
                for (const double value : {local.pressure_pa,local.derivative_pa_per_m3,local.requested_energy_change_j,local.stored_energy_j})
                    require_finite(value,"occupied constitutive pressure work");
                if (local.pressure_pa<0 || local.derivative_pa_per_m3>=0)
                    throw std::domain_error("Occupied constitutive work requires nonnegative pressure and a negative tangent");
                pressures[i]=local.pressure_pa;derivative=local.derivative_pa_per_m3;
            }
            capacities[i] = -1 / derivative;
            if (!std::isfinite(capacities[i]) || capacities[i] <= 0)
                throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied work compliance is unrepresentable");
            sums[i].add(delta[i]);
        }
        for (const auto& node : nodes) {
            if (node.fixed_zero_velocity) continue;
            const double pl = node.left == no_material_cell ? 0 : pressures[node.left];
            const double pr = node.right == no_material_cell ? 0 : pressures[node.right];
            const double force = node.area_m2 * (pl - pr) + node.inertia_kg * node.acceleration_m_per_s2;
            // Use the same represented endpoint velocity and midpoint
            // displacement as FluidWorld's geometry update. Algebraically
            // u0 + dt*force/(2I) is equivalent, but a different rounding path
            // can dominate the local volume budget of a rapidly growing cut.
            const double next = node.velocity_m_per_s + dt * force / node.inertia_kg;
            const double swept = node.area_m2 * (dt * (0.5 * node.velocity_m_per_s + 0.5 * next));
            require_finite(swept, "occupied swept volume");
            if (node.left != no_material_cell) sums[node.left].add(-swept);
            if (node.right != no_material_cell) sums[node.right].add(swept);
        }
        for (std::size_t i = 0; i < count; ++i) {
            errors[i] = sums[i].value(); root_arithmetic_roundoff[i] = sums[i].roundoff_bound();
            const double accuracy = roundoff_trial[i] ? 5e-13 / (2 * std::numeric_limits<double>::epsilon()) : 1;
            norm = std::max(norm, std::abs(errors[i]) / endpoint_volumes[i] * accuracy);
        }
        require_finite(norm, "occupied volume residual");
        return norm;
    };
    OccupiedMaterialPressureStep result;
    double norm = evaluate(changes, work, compliance, residual);
    // Reuse the line-search candidate arrays across iterations and rejected
    // trials. Accepted buffers swap roles; none alias the caller's cells.
    std::vector<double> next_changes(count), next_work(count), next_compliance(count), next_residual(count);
    while (norm > tolerance) {
        if (result.nonlinear_iterations == 80) {
            if (saw_void) throw FluidFatalError(FluidFatalCode::unsupported_topology, void_message());
            throw std::domain_error("Occupied pressure root did not converge; retry smaller timestep");
        }
        auto diagonal = compliance;
        for (std::size_t i = 0; i < count; ++i) diagonal[i] += boundary_diagonal[i];
        const PressureSystem matrix(std::move(diagonal), edges, preconditioner);
        const auto solved = matrix.solve(residual, std::vector<double>(count), 1e-12, 0);
        result.pressure_iterations += solved.iterations;
        result.maximum_linear_residual_norm = std::max(result.maximum_linear_residual_norm, solved.residual_norm);
        if (solved.residual_threshold > 0) result.maximum_linear_residual_ratio = std::max(result.maximum_linear_residual_ratio,
            solved.residual_norm / solved.residual_threshold);
        if (solved.iterations > 0) {
            if (solved.preconditioner_used == PressurePreconditioner::incomplete_cholesky) ++result.incomplete_cholesky_solves;
            else ++result.jacobi_solves;
        }
        if (solved.preconditioner_fallback) ++result.preconditioner_fallbacks;
        bool accepted = false;
        double fraction = 1;
        for (int attempt = 0; attempt < 48; ++attempt) {
            std::copy(changes.begin(), changes.end(), next_changes.begin());
            for (std::size_t i = 0; i < count; ++i) next_changes[i] -= fraction * compliance[i] * solved.pressure[i];
            try {
                const double next_norm = evaluate(next_changes, next_work, next_compliance, next_residual);
                if (next_norm <= tolerance || next_norm < norm * (1 - 1e-4 * fraction)) {
                    changes.swap(next_changes); work.swap(next_work);
                    compliance.swap(next_compliance); residual.swap(next_residual);
                    norm = next_norm; accepted = true; break;
                }
            } catch (const TrialVoid&) {
                // A trial may overstep the physical branch. Backtrack before
                // declaring that the actual root needs unsupported topology.
            } catch (const experimental::ReferenceError& error) {
                if (error.code() == experimental::ReferenceErrorCode::numerical_range ||
                    error.code() == experimental::ReferenceErrorCode::incompatible_entropy_reference ||
                    error.code() == experimental::ReferenceErrorCode::invalid_model ||
                    error.code() == experimental::ReferenceErrorCode::unsupported_derivative)
                    throw FluidFatalError(FluidFatalCode::numerical_precision, error.what());
            }
            fraction *= 0.5;
        }
        if (!accepted) {
            if (saw_void) throw FluidFatalError(FluidFatalCode::unsupported_topology, void_message());
            throw std::domain_error("Occupied pressure line search failed; retry smaller timestep");
        }
        ++result.nonlinear_iterations;
    }
    result.cells.assign(cells.begin(), cells.end());
    result.work_pressures_pa = work; result.volume_changes_m3 = changes; result.volume_residuals_m3 = residual;
    result.endpoint_pressures_pa.resize(count);
    result.zero_work_substituted.resize(count, 0);
    long double energy_change = 0, scale = 0, body_work = 0, work_roundoff = 0;
    for (std::size_t i = 0; i < count; ++i) {
        experimental::ReferenceIsentropicResponse response;double derivative=0;
        if(custom_work && changes[i]!=0) {
            auto evaluated=model.reference().isentropic_work(anchors[i],changes[i],isentropic_root_options);
            derivative=evaluated.work_pressure_volume_derivative_pa_per_m3;response=std::move(evaluated.response);
        } else response=model.reference().isentropic_response(anchors[i],changes[i],isentropic_root_options);
        const bool substituted = roundoff_anchor[i] && model.liquid_fill_resolution(response.endpoint).within_roundoff;
        auto& cell = result.cells[i];
        cell.volume_m3 = response.endpoint.volume_m3;
        double energy_update = substituted ? 0 : response.internal_energy_change_j;
        bool resolves_energy=response.endpoint_energy_resolves_change;
        if (custom_work) {
            if (substituted) throw std::invalid_argument("Constitutive work cannot substitute an elastic endpoint");
            if(changes[i]==0 || (!response.state.has_gas && response.work_pressure_pa==0))
                derivative=-.5/model.compression_compliance(anchors[i]);
            const auto local=custom_work(i,changes[i],anchors[i],response,derivative);
            if (local.pressure_pa!=work[i]) throw std::runtime_error("Accepted constitutive pressure work changed during publication");
            energy_update=local.requested_energy_change_j;cell.mixture.energy=local.stored_energy_j;resolves_energy=!local.unresolved_energy_update;
        } else cell.mixture.energy += energy_update;
        if (substituted) {
            result.zero_work_substituted[i] = 1; ++result.zero_work_substitutions;
            result.maximum_omitted_pressure_pa = std::max(result.maximum_omitted_pressure_pa,
                std::max(anchors[i].reference_state().mechanical_pressure_pa,
                    std::max(response.work_pressure_pa, response.state.mechanical_pressure_pa)));
            const double bound = std::abs(response.internal_energy_change_j);
            const double local_scale = anchors[i].reference_state().thermal_energy_j + anchors[i].reference_state().elastic_energy_j;
            if (bound > 64 * std::numeric_limits<double>::epsilon() * local_scale)
                throw FluidFatalError(FluidFatalCode::numerical_precision, "Liquid fill roundoff approximation has material local elastic work");
            result.omitted_elastic_work_bound_j += bound;
        }
        const auto state = model.evaluate(cell.mixture, cell.volume_m3);
        result.endpoint_pressures_pa[i] = state.mechanical_pressure_pa;
        const long double actual = static_cast<long double>(cell.mixture.energy) - cells[i].mixture.energy;
        energy_change += actual; work_roundoff += actual - energy_update;
        if (!resolves_energy) ++result.unresolved_energy_updates;
        if (!response.endpoint_volume_resolves_change) ++result.unresolved_volume_updates;
        const double volume_roundoff = std::abs(response.represented_volume_change_m3 - response.volume_change_m3);
        result.maximum_volume_roundoff_m3 = std::max(result.maximum_volume_roundoff_m3, volume_roundoff);
        VolumeSum addition; addition.add(cells[i].volume_m3); addition.add(changes[i]); addition.add(-cell.volume_m3);
        const double addition_roundoff = std::abs(addition.value());
        result.maximum_volume_addition_roundoff_m3 = std::max(result.maximum_volume_addition_roundoff_m3, addition_roundoff);
        result.maximum_root_arithmetic_roundoff_m3 = std::max(result.maximum_root_arithmetic_roundoff_m3, root_arithmetic_roundoff[i]);
        result.maximum_endpoint_relative_volume_residual = std::max(result.maximum_endpoint_relative_volume_residual,
            std::abs(residual[i]) / cell.volume_m3);
        const double endpoint_budget = 32 * std::numeric_limits<double>::epsilon() * cell.volume_m3;
        if (volume_roundoff > endpoint_budget || addition_roundoff > endpoint_budget || root_arithmetic_roundoff[i] > endpoint_budget)
            throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied pressure volume rounding exceeds local geometric resolution");
        scale += anchors[i].reference_state().thermal_energy_j + state.thermal_energy_j +
            anchors[i].reference_state().elastic_energy_j + state.elastic_energy_j;
        result.maximum_volume_residual_m3 = std::max(result.maximum_volume_residual_m3, std::abs(residual[i]));
    }
    for (const auto& node : nodes) {
        const double pl = node.left == no_material_cell ? 0 : work[node.left];
        const double pr = node.right == no_material_cell ? 0 : work[node.right];
        const double force = node.area_m2 * (pl - pr) + node.inertia_kg * node.acceleration_m_per_s2;
        const double next = node.fixed_zero_velocity ? 0 : node.velocity_m_per_s + dt * force / node.inertia_kg;
        require_finite(next, "occupied updated node velocity");
        result.velocities_m_per_s.push_back(next);
        const long double mean = 0.5L * (static_cast<long double>(next) + node.velocity_m_per_s);
        energy_change += node.inertia_kg * (static_cast<long double>(next) - node.velocity_m_per_s) * mean;
        body_work += dt * node.inertia_kg * node.acceleration_m_per_s2 * mean;
        result.body_impulse_kg_m_per_s += dt * node.inertia_kg * node.acceleration_m_per_s2;
        if (node.fixed_zero_velocity) result.wall_reaction_impulse_kg_m_per_s -= dt * force;
        scale += node.inertia_kg * (static_cast<long double>(next) * next +
            static_cast<long double>(node.velocity_m_per_s) * node.velocity_m_per_s);
    }
    result.body_force_work_j = static_cast<double>(body_work);
    result.energy_balance_error_j = static_cast<double>(energy_change - body_work);
    result.pressure_work_roundoff_j = static_cast<double>(work_roundoff);
    if (!std::isfinite(result.energy_balance_error_j) || std::abs(energy_change - body_work) > 2e-11L * scale)
        throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied pressure failed represented total-energy balance");
    return result;
}
OccupiedMaterialPressureStep advance_occupied_material_pressure(const FluidThermodynamics& model,
    std::span<const MaterialCell> cells,std::span<const OccupiedMaterialNode> nodes,double dt,
    PressurePreconditioner preconditioner,LiquidFillRoundoffPolicy roundoff_policy,double tolerance,
    experimental::ReferenceRootOptions options) {
    return detail::advance_occupied_material_pressure_with_work(model,cells,nodes,dt,preconditioner,roundoff_policy,tolerance,options,{});
}
}
