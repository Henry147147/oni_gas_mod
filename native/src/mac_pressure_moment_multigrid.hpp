#pragma once
#include "mac_pressure_multigrid.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace ma::experimental::mac_kernel {
// Explicit scalar operations keep ordinary complex arithmetic portable and
// avoid implementation-specific complex multiply/divide contraction paths.
// Matrix/iteration certificates reject nonfinite results before publication.
struct MomentComplex {
    double re = 0, im = 0;
    [[nodiscard]] bool finite() const { return std::isfinite(re) && std::isfinite(im); }
    [[nodiscard]] double magnitude() const {
        const double large = std::max(std::abs(re), std::abs(im)), small = std::min(std::abs(re), std::abs(im));
        if (large == 0 || !std::isfinite(large)) return large;
        const double ratio = small / large; return large * std::sqrt(1 + ratio * ratio);
    }
    MomentComplex& operator+=(MomentComplex v) { re += v.re; im += v.im; return *this; }
    MomentComplex& operator-=(MomentComplex v) { re -= v.re; im -= v.im; return *this; }
    friend MomentComplex operator+(MomentComplex a, MomentComplex b) { return {a.re + b.re, a.im + b.im}; }
    friend MomentComplex operator-(MomentComplex a, MomentComplex b) { return {a.re - b.re, a.im - b.im}; }
    friend MomentComplex operator*(MomentComplex a, MomentComplex b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
    friend MomentComplex operator*(double a, MomentComplex b) { return {a * b.re, a * b.im}; }
    friend MomentComplex operator/(MomentComplex a, MomentComplex b) {
        if (std::abs(b.re) >= std::abs(b.im)) {
            const double ratio = b.im / b.re, denominator = b.re + b.im * ratio;
            return {(a.re + a.im * ratio) / denominator, (a.im - a.re * ratio) / denominator};
        }
        const double ratio = b.re / b.im, denominator = b.im + b.re * ratio;
        return {(a.re * ratio + a.im) / denominator, (a.im * ratio - a.re) / denominator};
    }
};

// A fixed symmetric V-cycle for (comp + lambda H), lambda=(1+i sqrt(3))/2.
// It uses the existing geometric topology; all physical fluxes remain in the
// pressure root. No time-stepping or authoritative state is kept here.
class PressureMomentMultigrid {
public:
    using BlockExecutor = interval_detail::PressureMultigrid::BlockExecutor;
    struct SolveResult { bool converged = false, flagged = false; std::size_t iterations = 0, cycles = 0; double residual = 0; };
    explicit PressureMomentMultigrid(std::shared_ptr<const interval_detail::PressureMultigridTopology>);
    void set_executor(BlockExecutor fn, const void* context) { executor_ = fn; executor_context_ = context; }
    [[nodiscard]] const auto& topology() const { return *topology_; }
    [[nodiscard]] bool prepare(std::span<const double> compliance, std::span<const double> diagonal,
        std::span<const double> conductance_x, std::span<const double> conductance_y);
    [[nodiscard]] SolveResult solve(std::span<const double> ra, std::span<const double> rb,
        std::span<double> da, std::span<double> db, double rtol, double atol, std::size_t maximum = 512);
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
private:
    struct Values {
        std::vector<MomentComplex> mass, inverse, rhs, correction, scratch;
        std::vector<double> conductance;
    };
    struct DotPart { std::array<double, 3> sum{}; double maximum = 0; };
    struct Dot {
        MomentComplex value; double scale = 0, maximum = 0;
        [[nodiscard]] bool determined() const;
    };
    std::shared_ptr<const interval_detail::PressureMultigridTopology> topology_;
    std::vector<Values> values_;
    std::vector<double> compliance_;
    std::vector<MomentComplex> lu_, x_, r_, z_, p_, Ap_;
    std::vector<DotPart> parts_;
    BlockExecutor executor_ = nullptr; const void* executor_context_ = nullptr;
    const MomentComplex coupling_{.5, std::sqrt(3.) / 2};
    bool prepared_ = false;
    template<class F> void blocks(std::size_t count, F&& fn) {
        if (count == 0) return;
        if (!executor_) { for (std::size_t b = 0; b < count; ++b) fn(b); return; }
        executor_(executor_context_, count, [](const void* job, std::size_t block) {
            (*static_cast<const std::remove_reference_t<F>*>(job))(block);
        }, &fn);
    }
    template<class F> void rows(std::size_t count, F&& fn) {
        blocks((count + 255) / 256, [&](std::size_t block) {
            for (auto i = block * 256; i < std::min(count, block * 256 + 256); ++i) fn(i);
        });
    }
    [[nodiscard]] MomentComplex residual(std::size_t level, std::size_t node) const;
    void smooth(std::size_t level, bool upward);
    void cycle(std::size_t level);
    void apply(std::span<const MomentComplex>, std::span<MomentComplex>);
    void precondition(SolveResult&);
    [[nodiscard]] Dot dot(std::span<const MomentComplex>, std::span<const MomentComplex>, bool conjugate);
    [[nodiscard]] double norm(std::span<const MomentComplex>);
};

struct Context;
struct LinearSystem;
void solve_pressure_moments_coupled(Context&, const LinearSystem&, const std::vector<double>& compliance,
    const std::vector<double>& ra, const std::vector<double>& rb, std::vector<double>& da, std::vector<double>& db,
    double rtol, double atol, bool first_linearization);
}
