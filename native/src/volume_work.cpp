#include "mixed_atmospheres/volume_work.hpp"
#include "mixed_atmospheres/pressure.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ma {
namespace {
struct Closure {
    double free_volume, exponent, thermal_energy, initial_pressure;
};
struct Response {
    double work_pressure, endpoint_pressure, compliance, energy_change;
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
    bool project_incompressible_velocity) {
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
        const PressureSystem matrix(std::move(diagonal), matrix_faces);
        const auto solved = matrix.solve(residual, std::vector<double>(count), 1e-12, 1e-15);
        result.pressure_iterations += solved.iterations;
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
}
