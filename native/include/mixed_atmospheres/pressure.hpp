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
private:
    std::vector<double> compliance_, inverse_diagonal_;
    std::vector<PressureFace> faces_;
    PressurePreconditioner preconditioner_;
    void multiply(std::span<const double> input, std::span<double> output) const;
};
}
