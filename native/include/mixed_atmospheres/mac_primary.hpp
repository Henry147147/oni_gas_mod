#pragma once
#include <cmath>
#include <stdexcept>

namespace ma::experimental {
// A primary value and its carried storage remainder. The two parts are
// nonoverlapping binary64 values; callers audit any discarded third part.
struct MacPrimaryWrite {
    double value = 0, remainder = 0, discarded = 0;
    bool high_rounded = false;
};
namespace primary_detail {
struct Pair { double high, low; };
inline Pair two_sum(double a, double b) {
    const double high = a + b;
    if (!std::isfinite(high)) throw std::overflow_error("MAC primary update overflow");
    const double z = high - a;
    return {high, (a - (high - z)) + (b - z)};
}
}
// Requires the usual MAC nearest-even/FTZ-off environment, checked once by the
// caller's batch entry. No range object is needed for ordinary primary writes.
[[nodiscard]] inline bool canonical_mac_primary(double value, double remainder) {
    if (!std::isfinite(value) || !std::isfinite(remainder)) return false;
    if (remainder == 0) return true;
    const double high = value + remainder;
    if (!std::isfinite(high) || high != value) return false;
    const double z = high - value;
    return (value - (high - z)) + (remainder - z) == remainder;
}
[[nodiscard]] inline MacPrimaryWrite add_mac_primary(double value, double remainder, double increment) {
    if (!canonical_mac_primary(value, remainder) || !std::isfinite(increment))
        throw std::invalid_argument("Invalid MAC primary value/remainder/increment");
    const auto a = primary_detail::two_sum(value, increment);
    if (remainder == 0)
        return {a.high == 0 ? 0 : a.high, a.low == 0 ? 0 : a.low, 0, a.low != 0};
    const auto b = primary_detail::two_sum(remainder, a.low);
    const auto c = primary_detail::two_sum(a.high, b.high);
    if (b.low == 0)
        return {c.high == 0 ? 0 : c.high, c.low == 0 ? 0 : c.low, 0, a.low != 0};
    const auto d = primary_detail::two_sum(c.low, b.low);
    const auto e = primary_detail::two_sum(c.high, d.high);
    return {e.high == 0 ? 0 : e.high, e.low == 0 ? 0 : e.low, d.low, a.low != 0};
}
}
