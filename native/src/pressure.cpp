#include "mixed_atmospheres/pressure.hpp"
#include "mixed_atmospheres/species.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <thread>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

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
    [[nodiscard]] const std::vector<std::size_t>& offsets() const noexcept { return offsets_; }
    [[nodiscard]] std::size_t column(std::size_t k) const noexcept { return lower_[k].column; }
    [[nodiscard]] double value(std::size_t k) const noexcept { return lower_[k].value; }
    [[nodiscard]] double inverse_pivot(std::size_t row) const noexcept { return inverse_pivots_[row]; }
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

// Parallel solve support (PressureParallelPlan).
template<class F> void run_tasks(const PressureParallelPlan& plan, std::size_t count, const F& body) {
    if (count == 0) return;
    plan.executor(plan.executor_context, count,
        [](const void* context, std::size_t task) { (*static_cast<const F*>(context))(task); }, &body);
}
void await_progress(const std::atomic<std::size_t>& progress, std::size_t target) {
    for (unsigned spin = 0; progress.load(std::memory_order_acquire) < target; ++spin) {
#if defined(__x86_64__) || defined(_M_X64)
        if (spin < 256) { _mm_pause(); continue; }
#endif
        std::this_thread::yield();
    }
}
// Block sums of one dot product, added in block order.
double finish_dot(const std::vector<long double>& partial) {
    long double result = 0;
    for (const auto value : partial) result += value;
    const double value = static_cast<double>(result);
    require_finite(value, "pressure reduction");
    return value;
}
// IC(0) sweeps in the plan's order. The forward rows gather their lower
// entries in factor order; the backward rows gather, in descending row order,
// the updates the serial sweep scatters into them.
class ParallelSweeps {
    const IncompleteCholesky& factor_;
    const PressureParallelPlan& plan_;
    std::vector<std::size_t> upper_offsets_, upper_rows_;
    std::vector<double> upper_values_;
    std::unique_ptr<std::atomic<std::size_t>[]> forward_, backward_;
public:
    ParallelSweeps(const IncompleteCholesky& factor, const PressureParallelPlan& plan, std::size_t count)
        : factor_(factor), plan_(plan), forward_(new std::atomic<std::size_t>[plan.bands]),
          backward_(new std::atomic<std::size_t>[plan.bands]) {
        const auto& offsets = factor.offsets();
        const auto tasks = plan.bands * plan.chunks;
        if (plan.bands == 0 || plan.chunks == 0 || plan.start.size() != tasks + 1 || plan.start.front() != 0 ||
            plan.start.back() != count || plan.order.size() != count)
            throw std::invalid_argument("Invalid pressure sweep plan");
        std::vector<std::size_t> task(count, tasks), position(count);
        for (std::size_t t = 0; t < tasks; ++t) {
            if (plan.start[t] > plan.start[t + 1]) throw std::invalid_argument("Invalid pressure sweep plan");
            for (auto k = plan.start[t]; k < plan.start[t + 1]; ++k) {
                const auto row = plan.order[k];
                if (row >= count || task[row] != tasks) throw std::invalid_argument("Invalid pressure sweep plan");
                task[row] = t; position[row] = k;
            }
        }
        for (std::size_t row = 0; row < count; ++row)
            for (auto k = offsets[row]; k < offsets[row + 1]; ++k) {
                const auto column = factor.column(k);
                const auto a = task[column], b = task[row];
                const bool ordered = a == b ? position[column] < position[row] :
                    a / plan.chunks <= b / plan.chunks && a % plan.chunks <= b % plan.chunks;
                if (!ordered) throw std::invalid_argument("Pressure sweep plan breaks an IC dependency");
            }
        upper_offsets_.assign(count + 1, 0);
        for (std::size_t k = 0; k < offsets[count]; ++k) ++upper_offsets_[factor.column(k) + 1];
        for (std::size_t row = 0; row < count; ++row) upper_offsets_[row + 1] += upper_offsets_[row];
        upper_rows_.resize(offsets[count]);
        upper_values_.resize(offsets[count]);
        auto cursor = upper_offsets_;
        for (auto row = count; row-- > 0;)
            for (auto k = offsets[row]; k < offsets[row + 1]; ++k) {
                const auto at = cursor[factor.column(k)]++;
                upper_rows_[at] = row; upper_values_[at] = factor.value(k);
            }
    }
    void apply(std::span<const double> residual, std::span<double> output) {
        const auto& plan = plan_; const auto& offsets = factor_.offsets();
        const auto bands = plan.bands, chunks = plan.chunks;
        for (std::size_t b = 0; b < bands; ++b) { forward_[b].store(0, std::memory_order_relaxed); backward_[b].store(0, std::memory_order_relaxed); }
        run_tasks(plan, bands, [&](std::size_t b) {
            for (std::size_t j = 0; j < chunks; ++j) {
                if (b > 0) await_progress(forward_[b - 1], j + 1);
                const auto t = b * chunks + j;
                for (auto k = plan.start[t]; k < plan.start[t + 1]; ++k) {
                    const auto row = plan.order[k];
                    double value = residual[row];
                    for (auto e = offsets[row]; e < offsets[row + 1]; ++e)
                        value -= factor_.value(e) * output[factor_.column(e)];
                    output[row] = value;
                }
                forward_[b].store(j + 1, std::memory_order_release);
            }
        });
        run_tasks(plan, bands, [&](std::size_t reversed) {
            const auto b = bands - 1 - reversed;
            for (std::size_t done = 0; done < chunks; ++done) {
                if (b + 1 < bands) await_progress(backward_[b + 1], done + 1);
                const auto t = b * chunks + (chunks - 1 - done);
                for (auto k = plan.start[t + 1]; k-- > plan.start[t];) {
                    const auto row = plan.order[k];
                    double value = output[row] * factor_.inverse_pivot(row);
                    for (auto e = upper_offsets_[row]; e < upper_offsets_[row + 1]; ++e)
                        value -= upper_values_[e] * output[upper_rows_[e]];
                    output[row] = value;
                }
                backward_[b].store(done + 1, std::memory_order_release);
            }
        });
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
                                        double relative, double absolute, std::size_t maximum,
                                        const PressureParallelPlan* plan) const {
    if (!plan) return solve(rhs, initial, relative, absolute, maximum);
    if (!plan->executor || plan->reduction_rows == 0) throw std::invalid_argument("Invalid pressure parallel plan");
    check_vector(rhs, compliance_.size());
    check_vector(initial, compliance_.size());
    require_finite(relative, "relative pressure tolerance");
    require_finite(absolute, "absolute pressure tolerance");
    if (relative < 0 || absolute < 0 || (relative == 0 && absolute == 0) || maximum == 0)
        throw std::invalid_argument("Invalid pressure solve limits");
    const auto count = rhs.size();
    const auto rows = plan->reduction_rows, blocks = (count + rows - 1) / rows;
    const auto block_end = [&](std::size_t block) { return std::min(count, (block + 1) * rows); };
    // Each row's faces in face order, with the sign of its flux.
    std::vector<std::size_t> incident_offsets(count + 1, 0), incident(2 * faces_.size());
    for (const auto& face : faces_) { ++incident_offsets[face.left + 1]; ++incident_offsets[face.right + 1]; }
    for (std::size_t i = 0; i < count; ++i) incident_offsets[i + 1] += incident_offsets[i];
    {
        auto cursor = incident_offsets;
        for (std::size_t f = 0; f < faces_.size(); ++f) {
            incident[cursor[faces_[f].left]++] = 2 * f;
            incident[cursor[faces_[f].right]++] = 2 * f + 1;
        }
    }
    // A row of the operator: compliance, then its faces' fluxes in face order.
    const auto row_product = [&](std::size_t i, const auto& input) {
        double value = compliance_[i] * input(i);
        for (auto k = incident_offsets[i]; k < incident_offsets[i + 1]; ++k) {
            const auto& face = faces_[incident[k] / 2];
            const double flux = face.conductance * (input(face.left) - input(face.right));
            if (incident[k] % 2 == 0) value += flux; else value -= flux;
        }
        return value;
    };
    std::vector<long double> partial(blocks);
    const auto reduce = [&](const auto& block_sum) {
        run_tasks(*plan, blocks, [&](std::size_t block) { partial[block] = block_sum(block * rows, block_end(block)); });
        return finish_dot(partial);
    };
    const auto pdot = [&](std::span<const double> a, std::span<const double> b) {
        return reduce([&](std::size_t first, std::size_t last) {
            long double result = 0;
            for (auto i = first; i < last; ++i) result += static_cast<long double>(a[i]) * b[i];
            return result;
        });
    };
    PressureSolveResult result{std::vector<double>(initial.begin(), initial.end()), 0, 0};
    std::vector<double> residual(count), z(count), direction(count), product(count), next(count);
    const double threshold = std::max(absolute, relative * std::sqrt(pdot(rhs, rhs)));
    require_finite(threshold, "pressure convergence threshold");
    result.residual_threshold = threshold;
    const auto true_residual = [&] {
        const auto& pressure = result.pressure;
        result.residual_norm = std::sqrt(reduce([&](std::size_t first, std::size_t last) {
            long double sum = 0;
            for (auto i = first; i < last; ++i) {
                product[i] = row_product(i, [&](std::size_t j) { return pressure[j]; });
                residual[i] = rhs[i] - product[i];
                sum += static_cast<long double>(residual[i]) * residual[i];
            }
            return sum;
        }));
    };
    true_residual();
    if (result.residual_norm <= threshold) return result;
    IncompleteCholesky factor;
    if (preconditioner_ == PressurePreconditioner::incomplete_cholesky ||
        (preconditioner_ == PressurePreconditioner::automatic && count >= 256)) {
        if (factor.factor(inverse_diagonal_, faces_)) result.preconditioner_used = PressurePreconditioner::incomplete_cholesky;
        else result.preconditioner_fallback = true;
    }
    std::optional<ParallelSweeps> sweeps;
    if (result.preconditioner_used == PressurePreconditioner::incomplete_cholesky) sweeps.emplace(factor, *plan, count);
    const auto precondition = [&] {
        if (sweeps) sweeps->apply(residual, z);
        else run_tasks(*plan, blocks, [&](std::size_t block) {
            for (auto i = block * rows; i < block_end(block); ++i) z[i] = inverse_diagonal_[i] * residual[i];
        });
    };
    // The next direction is z (restart) or z + beta * direction, formed in
    // the following product exactly as the serial update forms it.
    bool restart = true; double beta = 0;
    precondition();
    double previous = pdot(residual, z);
    while (result.iterations < maximum) {
        const double denominator = reduce([&](std::size_t first, std::size_t last) {
            const auto form = [&](std::size_t j) { return restart ? z[j] : z[j] + beta * direction[j]; };
            long double sum = 0;
            for (auto i = first; i < last; ++i) {
                next[i] = form(i);
                product[i] = row_product(i, form);
                sum += static_cast<long double>(next[i]) * product[i];
            }
            return sum;
        });
        direction.swap(next);
        if (denominator <= 0 || previous <= 0) throw std::runtime_error("Pressure solve lost positive definiteness");
        const double alpha = previous / denominator;
        require_finite(alpha, "pressure solve step");
        const double rr = reduce([&](std::size_t first, std::size_t last) {
            long double sum = 0;
            for (auto i = first; i < last; ++i) {
                result.pressure[i] += alpha * direction[i];
                residual[i] -= alpha * product[i];
                sum += static_cast<long double>(residual[i]) * residual[i];
            }
            return sum;
        });
        ++result.iterations;
        result.residual_norm = std::sqrt(rr);
        if (result.residual_norm <= threshold) {
            true_residual();
            if (result.residual_norm <= threshold) return result;
            precondition();
            restart = true;
            previous = pdot(residual, z);
            continue;
        }
        precondition();
        const double following = pdot(residual, z);
        beta = following / previous;
        require_finite(beta, "pressure solve recurrence");
        restart = false;
        previous = following;
    }
    throw std::runtime_error("Pressure solve did not converge; no state published");
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
