#include "mixed_atmospheres/mac_primary.hpp"
#include <bit>
#include <cstdint>
#include <limits>

namespace ma::experimental {
namespace {
double nearest_high(double high, MacEnergy low) {
    if (low.is_zero()) return high;
    const double direction = low.mantissa() > 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
    const double adjacent = std::nextafter(high, direction);
    const double width = std::isfinite(adjacent) ? std::abs(adjacent - high) : std::abs(high - std::nextafter(high, 0.));
    MacEnergySum comparison; comparison.add(low.magnitude());
    comparison.add(MacEnergy::from_double(width).shifted(-1).negated());
    const int sign = comparison.sign();
    return sign > 0 || (sign == 0 && (std::bit_cast<std::uint64_t>(high) & 1U) != 0) ? adjacent : high;
}
}
long double mac_energy_long_double(MacEnergy value) {
    if (value.is_zero()) return 0;
    constexpr int minimum = std::numeric_limits<long double>::min_exponent - std::numeric_limits<long double>::digits;
    if (value.exponent() < MacExponent(minimum)) return std::copysign(0.L, value.mantissa());
    if (value.exponent() > MacExponent(std::numeric_limits<long double>::max_exponent))
        throw std::overflow_error("MAC energy diagnostic exceeds long-double range");
    return std::ldexp(static_cast<long double>(value.mantissa()), value.exponent().to_int_exact());
}
MacEnergy mac_stored_remainder_value(const MacStoredRemainder& remainder) {
    if (!std::isfinite(remainder.projected)) throw std::invalid_argument("Nonfinite MAC remainder projection");
    if (remainder.full.is_zero()) return MacEnergy::from_double(remainder.projected);
    const auto projection = remainder.full.project();
    if (!projection.rounded || std::bit_cast<std::uint64_t>(projection.value) != std::bit_cast<std::uint64_t>(remainder.projected))
        throw std::invalid_argument("Noncanonical MAC remainder projection");
    return remainder.full;
}
bool canonical_mac_primary(double value, const MacStoredRemainder& remainder) {
    if (!std::isfinite(value)) return false;
    if (remainder.full.is_zero()) return canonical_mac_primary(value, remainder.projected);
    const auto low = mac_stored_remainder_value(remainder);
    return nearest_high(value, low) == value;
}
MacPrimaryFullWrite store_mac_primary_remainder(const MacEnergySum& total) {
    MacPrimaryFullWrite result; result.value = total.value().project().value;
    MacEnergySum correction = total; correction.add(-result.value);
    result.high_rounded = correction.sign() != 0;
    auto low = correction.value();
    bool normalized = false;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        const double next = nearest_high(result.value, low);
        if (next == result.value) { normalized = true; break; }
        if (!std::isfinite(next)) throw std::overflow_error("MAC primary remainder normalization overflow");
        low = add_mac_energy(low, MacEnergy::from_double(result.value - next)).high;
        result.value = next;
    }
    if (!normalized) throw std::runtime_error("MAC primary remainder did not normalize");
    const auto projection = low.project(); result.remainder.projected = projection.value;
    if (projection.rounded) result.remainder.full = low;
    result.discarded = total; result.discarded.add(-result.value); result.discarded.add(low.negated());
    return result;
}
}
