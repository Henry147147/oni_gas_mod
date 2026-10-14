#include "mixed_atmospheres/pressure.hpp"
#include "mixed_atmospheres/species.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ma {
namespace {
double dot(std::span<const double> a, std::span<const double> b) {
    // Accumulate reduction scalars more accurately; the fields stay binary64.
    long double result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) result += static_cast<long double>(a[i]) * b[i];
    const double value = static_cast<double>(result);
    require_finite(value, "pressure reduction");
    return value;
}
void check_vector(std::span<const double> values, std::size_t count) {
    if (values.size() != count) throw std::invalid_argument("Pressure vector size mismatch");
    for (double value : values) require_finite(value, "pressure vector value");
}
}

PressureSystem::PressureSystem(std::vector<double> compliance, std::vector<PressureFace> faces)
    : compliance_(std::move(compliance)), inverse_diagonal_(compliance_), faces_(std::move(faces)) {
    const auto count = compliance_.size();
    if (count == 0) throw std::invalid_argument("Empty pressure system");
    std::vector<std::size_t> parent(count);
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t node) {
        while (parent[node] != node) { parent[node] = parent[parent[node]]; node = parent[node]; }
        return node;
    };
    for (double c : compliance_) {
        require_finite(c, "pressure compliance");
        if (c < 0) throw std::invalid_argument("Negative pressure compliance");
    }
    for (const auto& face : faces_) {
        require_finite(face.conductance, "pressure face conductance");
        if (face.left >= count || face.right >= count || face.left == face.right || face.conductance < 0)
            throw std::invalid_argument("Invalid pressure face");
        if (face.conductance == 0) continue;
        parent[root(face.right)] = root(face.left);
        inverse_diagonal_[face.left] += face.conductance;
        inverse_diagonal_[face.right] += face.conductance;
    }
    std::vector<bool> anchored(count, false);
    for (std::size_t i = 0; i < count; ++i) if (compliance_[i] > 0) anchored[root(i)] = true;
    for (std::size_t i = 0; i < count; ++i) {
        if (!anchored[root(i)]) throw std::invalid_argument("Pressure component has no compressibility or pressure reference");
        require_finite(inverse_diagonal_[i], "pressure diagonal");
        inverse_diagonal_[i] = 1 / inverse_diagonal_[i];
        require_finite(inverse_diagonal_[i], "pressure preconditioner");
    }
}

void PressureSystem::multiply(std::span<const double> input, std::span<double> output) const {
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = compliance_[i] * input[i];
    for (const auto& face : faces_) {
        const double flux = face.conductance * (input[face.left] - input[face.right]);
        output[face.left] += flux;
        output[face.right] -= flux;
    }
}

std::vector<double> PressureSystem::apply(std::span<const double> pressure) const {
    check_vector(pressure, compliance_.size());
    std::vector<double> result(pressure.size());
    multiply(pressure, result);
    check_vector(result, pressure.size());
    return result;
}

PressureSolveResult PressureSystem::solve(std::span<const double> rhs, std::span<const double> initial,
                                        double relative, double absolute, std::size_t maximum) const {
    check_vector(rhs, compliance_.size());
    check_vector(initial, compliance_.size());
    require_finite(relative, "relative pressure tolerance");
    require_finite(absolute, "absolute pressure tolerance");
    if (relative < 0 || absolute < 0 || (relative == 0 && absolute == 0) || maximum == 0)
        throw std::invalid_argument("Invalid pressure solve limits");
    const auto count = rhs.size();
    PressureSolveResult result{std::vector<double>(initial.begin(), initial.end()), 0, 0};
    std::vector<double> residual(count), z(count), direction(count), product(count);
    const double threshold = std::max(absolute, relative * std::sqrt(dot(rhs, rhs)));
    require_finite(threshold, "pressure convergence threshold");
    const auto true_residual = [&] {
        multiply(result.pressure, product);
        for (std::size_t i = 0; i < count; ++i) residual[i] = rhs[i] - product[i];
        result.residual_norm = std::sqrt(dot(residual, residual));
    };
    true_residual();
    if (result.residual_norm <= threshold) return result;
    for (std::size_t i = 0; i < count; ++i) z[i] = direction[i] = inverse_diagonal_[i] * residual[i];
    double previous = dot(residual, z);
    while (result.iterations < maximum) {
        multiply(direction, product);
        const double denominator = dot(direction, product);
        if (denominator <= 0 || previous <= 0) throw std::runtime_error("Pressure solve lost positive definiteness");
        const double alpha = previous / denominator;
        require_finite(alpha, "pressure solve step");
        for (std::size_t i = 0; i < count; ++i) {
            result.pressure[i] += alpha * direction[i];
            residual[i] -= alpha * product[i];
        }
        ++result.iterations;
        result.residual_norm = std::sqrt(dot(residual, residual));
        if (result.residual_norm <= threshold) {
            // A recursively small CG residual alone can be misleading for a
            // stiff system. Check the original equation before publishing.
            true_residual();
            if (result.residual_norm <= threshold) return result;
            for (std::size_t i = 0; i < count; ++i) z[i] = direction[i] = inverse_diagonal_[i] * residual[i];
            previous = dot(residual, z);
            continue;
        }
        for (std::size_t i = 0; i < count; ++i) z[i] = inverse_diagonal_[i] * residual[i];
        const double next = dot(residual, z), beta = next / previous;
        require_finite(beta, "pressure solve recurrence");
        for (std::size_t i = 0; i < count; ++i) direction[i] = z[i] + beta * direction[i];
        previous = next;
    }
    throw std::runtime_error("Pressure solve did not converge; no state published");
}
}
