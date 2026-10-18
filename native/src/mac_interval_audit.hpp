#pragma once
#include "mixed_atmospheres/mac_energy.hpp"
#include <cmath>
#include <limits>

namespace ma::experimental::interval_detail {
// Normal-range double-double accumulation. The error envelope deliberately
// overestimates the discarded third component; close decisions replay all
// operands into the retained exact expansion instead of trusting this estimate.
struct Sum {
    double hi = 0, lo = 0;
    // Returning the discarded third component's presence lets state gathers
    // count rounding even when their surviving two components later cancel.
    bool add(double value) noexcept {
        const double s = hi + value, z = s - hi;
        const double e = (hi - (s - z)) + (value - z);
        const double t = lo + e, tz = t - lo;
        const bool discarded = (lo - (t - tz)) + (e - tz) != 0;
        const double h = s + t, q = h - s;
        lo = (s - (h - q)) + (t - q); hi = h;
        return discarded;
    }
    [[nodiscard]] double value() const noexcept { return hi + lo; }
};
struct StoredSum : Sum {
    bool discarded = false;
    void add(double value) noexcept { discarded = Sum::add(value) || discarded; }
};
struct Audit {
    bool passed = false, exact = false;
    MacEnergy residual, bound;
};
class Account {
public:
    void add(double operand) noexcept {
        ++count_; sum_.add(operand); scale_.add(std::abs(operand));
        if (!std::isfinite(operand) || (operand != 0 &&
            (std::abs(operand) < 0x1p-450 || std::abs(operand) > 0x1p450))) ordinary_ = false;
    }
    void add(MacEnergy operand) {
        const auto projected = operand.project(); add(projected.value);
        if (projected.rounded) ordinary_ = false;
    }
    template<class Replay> [[nodiscard]] Audit decide(Replay replay) const {
        constexpr double eps = std::numeric_limits<double>::epsilon();
        const double residual = sum_.value(), scale = scale_.value(), bound = 128 * eps * scale;
        if (ordinary_ && scale == 0) return {true, false, {}, {}};
        // With n < 2^26, the two-sum accumulation's O(n eps^2) error,
        // rounded scale, and this final comparison fit inside this envelope.
        const double error = 64 * eps * eps * (static_cast<double>(count_) + 1) * scale +
            16 * eps * (std::abs(residual) + bound);
        const double margin = bound - std::abs(residual);
        if (ordinary_ && count_ < (1U << 26) && std::isfinite(scale) && std::isfinite(residual) &&
            (margin > error || margin < -error))
            return {margin > error, false, MacEnergy::from_double(residual), MacEnergy::from_double(bound)};
        MacEnergyAccount exact; replay(exact);
        return {exact.within_bound(), true, exact.residual(), exact.bound()};
    }
private:
    Sum sum_, scale_;
    std::size_t count_ = 0;
    bool ordinary_ = true;
};
}
