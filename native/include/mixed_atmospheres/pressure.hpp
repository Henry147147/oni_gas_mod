#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace ma {
struct PressureFace {
    std::size_t left, right;
    double conductance;
};
enum class PressurePreconditioner {
    automatic, // IC(0) for sufficiently large bounded-degree graphs; Jacobi otherwise.
    jacobi,
    incomplete_cholesky
};
struct PressureSolveResult {
    std::vector<double> pressure;
    std::size_t iterations = 0;
    double residual_norm = 0;
    double residual_threshold = 0;
    PressurePreconditioner preconditioner_used = PressurePreconditioner::jacobi;
    bool preconditioner_fallback = false;
};

// Optional parallel execution of one solve. Every row keeps the serial
// arithmetic: products gather a row's faces in face order, and the IC(0)
// sweeps visit rows in an order that respects their lower neighbours. Only
// the dot products differ from the serial solve: each sums fixed blocks of
// reduction_rows rows in long double and adds the block sums in order, so the
// result never depends on the worker count, and a system of at most
// reduction_rows rows reproduces the serial solve exactly.
struct PressureParallelPlan {
    using Task = void (*)(const void* context, std::size_t task);
    // Runs task(context, t) for every t in [0, count). Tasks may run
    // concurrently; a task never starts before every lower task has started.
    using Executor = void (*)(const void* executor_context, std::size_t count, Task, const void* context);
    Executor executor = nullptr;
    const void* executor_context = nullptr;
    std::size_t reduction_rows = 1024;
    // IC(0) sweep schedule. Task (band b, chunk j) holds the rows
    // order[start[b * chunks + j]] ... order[start[b * chunks + j + 1] - 1],
    // ascending. Each lower neighbour of a row lies earlier in the same task
    // or in a task (b', j') with b' <= b and j' <= j; the plan is checked.
    std::size_t bands = 0, chunks = 0;
    std::vector<std::size_t> order, start;
};

// Symmetric finite-volume pressure operator: A p = C p + sum_faces G(p_i-p_j).
// A face appears exactly once, so its two fluxes cancel. Zero face conductance
// represents an impermeable barrier. Every connected component must have
// positive compressibility/compliance somewhere, or an explicitly assembled
// pressure boundary contribution, to remove the constant-pressure nullspace.
// IC(0) is an optional SPD preconditioner, never a replacement operator. It
// keeps only original lower graph entries and accepts positive finite pivots;
// unsafe or high-degree factors fall back to Jacobi without shifting A.
// Existing callers default to Jacobi; the experimental FluidWorld explicitly
// selects automatic preconditioning. This class alone does not advance fluid.
class PressureSystem {
public:
    PressureSystem(std::vector<double> compliance, std::vector<PressureFace> faces,
                   PressurePreconditioner preconditioner = PressurePreconditioner::jacobi);
    [[nodiscard]] std::vector<double> apply(std::span<const double> pressure) const;
    [[nodiscard]] PressureSolveResult solve(std::span<const double> rhs,
                                            std::span<const double> initial,
                                            double relative_tolerance = 1e-9,
                                            double absolute_tolerance = 1e-11,
                                            std::size_t maximum_iterations = 2000) const;
    // The same solve with a parallel plan (null: the serial solve above).
    [[nodiscard]] PressureSolveResult solve(std::span<const double> rhs, std::span<const double> initial,
                                            double relative_tolerance, double absolute_tolerance,
                                            std::size_t maximum_iterations, const PressureParallelPlan* plan) const;
private:
    std::vector<double> compliance_, inverse_diagonal_;
    std::vector<PressureFace> faces_;
    PressurePreconditioner preconditioner_;
    void multiply(std::span<const double> input, std::span<double> output) const;
};
}
