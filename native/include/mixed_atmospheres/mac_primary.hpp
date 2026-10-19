#pragma once
#include "mac_scaled.hpp"
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
// A projected correction, with an authoritative range value only when that
// projection rounds. Ordinary production writes use the scalar helper above.
struct MacStoredRemainder {
    double projected = 0;
    MacScaled full{};
    [[nodiscard]] bool is_zero() const noexcept { return projected == 0 && full.is_zero(); }
};
[[nodiscard]] inline bool same_mac_stored_remainder(const MacStoredRemainder& a, const MacStoredRemainder& b) {
    if (a.projected != b.projected) return false;
    if (a.full.is_zero() && b.full.is_zero()) return true;
    return a.full.mantissa() == b.full.mantissa() && a.full.exponent() == b.full.exponent();
}
struct MacPrimaryFullWrite {
    double value = 0;
    MacStoredRemainder remainder;
    MacEnergySum discarded;
    bool high_rounded = false;
};
[[nodiscard]] MacEnergy mac_stored_remainder_value(const MacStoredRemainder&);
[[nodiscard]] long double mac_energy_long_double(MacEnergy);
[[nodiscard]] bool canonical_mac_primary(double, const MacStoredRemainder&);
// Reference or flagged-range storage: retain the projected high part and a
// range-capable correction. The caller audits every discarded component.
[[nodiscard]] MacPrimaryFullWrite store_mac_primary_remainder(const MacEnergySum&);

}
