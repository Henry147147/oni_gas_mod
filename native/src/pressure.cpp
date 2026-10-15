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

// A no-fill LDL^T factor of the original graph's symmetric M-matrix. Only the
// preconditioner is factored: multiply() and the final true residual always use
// the original compliance and faces, including the caller's pressure gauge.
// Sorted lower rows support irregular graphs, parallel faces and triangles.
// Dense/high-degree graphs or unsafe pivots retain the bounded Jacobi path.
class IncompleteCholesky {
    struct Entry { std::size_t column; double value; };
    std::vector<std::size_t> offsets_;
    std::vector<Entry> lower_;
    std::vector<double> inverse_pivots_;
public:
    bool factor(std::span<const double> inverse_diagonal, std::span<const PressureFace> faces) {
        constexpr std::size_t maximum_lower_degree = 64;
        const auto count = inverse_diagonal.size();
        offsets_.assign(count + 1, 0);
        for (const auto& face : faces) if (face.conductance > 0) {
            const auto row = std::max(face.left, face.right);
            if (++offsets_[row + 1] > maximum_lower_degree) return false;
        }
        for (std::size_t row = 0; row < count; ++row) offsets_[row + 1] += offsets_[row];
        lower_.resize(offsets_.back());
        auto cursor = offsets_;
        for (const auto& face : faces) if (face.conductance > 0) {
            const auto row = std::max(face.left, face.right), column = std::min(face.left, face.right);
            lower_[cursor[row]++] = {column, -face.conductance};
        }
        // Combine parallel edges in place; the operator still retains every
        // original face and its conservative equal-and-opposite flux.
        std::size_t written = 0;
        for (std::size_t row = 0; row < count; ++row) {
            const auto begin = offsets_[row], end = offsets_[row + 1];
            std::sort(lower_.begin() + static_cast<std::ptrdiff_t>(begin),
                      lower_.begin() + static_cast<std::ptrdiff_t>(end),
                      [](const Entry& a, const Entry& b) { return a.column < b.column; });
            offsets_[row] = written;
            for (auto k = begin; k < end; ++k) {
                const auto entry = lower_[k];
                if (written > offsets_[row] && lower_[written - 1].column == entry.column)
                    lower_[written - 1].value += entry.value;
                else lower_[written++] = entry;
            }
        }
        offsets_[count] = written;
        lower_.resize(written);
        inverse_pivots_.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            const double diagonal = 1 / inverse_diagonal[row];
            double pivot = diagonal;
            for (auto k = offsets_[row]; k < offsets_[row + 1]; ++k) {
                auto& entry = lower_[k];
                double value = entry.value;
                auto left = offsets_[row], right = offsets_[entry.column];
                const auto right_end = offsets_[entry.column + 1];
                while (left < k && right < right_end) {
                    const auto& a = lower_[left]; const auto& b = lower_[right];
                    if (a.column < b.column) ++left;
                    else if (b.column < a.column) ++right;
                    else {
                        value -= a.value * b.value / inverse_pivots_[a.column];
                        ++left; ++right;
                    }
                }
                entry.value = value * inverse_pivots_[entry.column];
                pivot -= entry.value * value;
            }
            // No shifts, dropped physical constraints, or altered tolerances.
            // A tiny/cancelled pivot is safer with the existing diagonal PC.
            if (!std::isfinite(pivot) || pivot <= diagonal * 1e-12) return false;
            inverse_pivots_[row] = 1 / pivot;
            if (!std::isfinite(inverse_pivots_[row])) return false;
        }
        return true;
    }
    void apply(std::span<const double> residual, std::span<double> output) const {
        for (std::size_t row = 0; row < residual.size(); ++row) {
            double value = residual[row];
            for (auto k = offsets_[row]; k < offsets_[row + 1]; ++k)
                value -= lower_[k].value * output[lower_[k].column];
            output[row] = value;
        }
        for (std::size_t row = 0; row < residual.size(); ++row) output[row] *= inverse_pivots_[row];
        for (auto row = residual.size(); row-- > 0;) {
            const double value = output[row];
            for (auto k = offsets_[row]; k < offsets_[row + 1]; ++k)
                output[lower_[k].column] -= lower_[k].value * value;
        }
    }
};
}

PressureSystem::PressureSystem(std::vector<double> compliance, std::vector<PressureFace> faces,
                               PressurePreconditioner preconditioner)
    : compliance_(std::move(compliance)), inverse_diagonal_(compliance_), faces_(std::move(faces)),
      preconditioner_(preconditioner) {
    if (preconditioner != PressurePreconditioner::automatic && preconditioner != PressurePreconditioner::jacobi &&
        preconditioner != PressurePreconditioner::incomplete_cholesky)
        throw std::invalid_argument("Invalid pressure preconditioner");
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
    result.residual_threshold = threshold;
    const auto true_residual = [&] {
        multiply(result.pressure, product);
        for (std::size_t i = 0; i < count; ++i) residual[i] = rhs[i] - product[i];
        result.residual_norm = std::sqrt(dot(residual, residual));
    };
    true_residual();
    if (result.residual_norm <= threshold) return result;
    IncompleteCholesky factor;
    if (preconditioner_ == PressurePreconditioner::incomplete_cholesky ||
        (preconditioner_ == PressurePreconditioner::automatic && count >= 256)) {
        if (factor.factor(inverse_diagonal_, faces_)) result.preconditioner_used = PressurePreconditioner::incomplete_cholesky;
        else result.preconditioner_fallback = true;
    }
    const auto precondition = [&] {
        if (result.preconditioner_used == PressurePreconditioner::incomplete_cholesky) factor.apply(residual, z);
        else for (std::size_t i = 0; i < count; ++i) z[i] = inverse_diagonal_[i] * residual[i];
    };
    precondition();
    direction = z;
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
            precondition();
            direction = z;
            previous = dot(residual, z);
            continue;
        }
        precondition();
        const double next = dot(residual, z), beta = next / previous;
        require_finite(beta, "pressure solve recurrence");
        for (std::size_t i = 0; i < count; ++i) direction[i] = z[i] + beta * direction[i];
        previous = next;
    }
    throw std::runtime_error("Pressure solve did not converge; no state published");
}
}
