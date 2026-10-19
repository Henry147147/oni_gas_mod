#include "mixed_atmospheres/mac_primary.hpp"
#include <bit>
#include <cstdint>
#include <limits>

namespace ma::experimental {
namespace {
MacEnergy rounded_range(const MacEnergySum& total) {
    auto value=total.value();
    if(value.is_zero()) {
        if(total.sign()!=0)throw std::runtime_error("MAC range estimate lost a nonzero primary correction");
        return value;
    }
    for(unsigned attempt=0;attempt<8;++attempt) {
        auto residual=total;residual.add(value.negated());const int sign=residual.sign();
        if(sign==0)return value;
        int shift=0;
        const double mantissa=std::frexp(std::nextafter(value.mantissa(),sign>0 ?
            std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity()),&shift);
        const auto adjacent=MacEnergy::from_parts(mantissa,value.exponent()+shift);
        MacEnergySum width;width.add(adjacent);width.add(value.negated());
        MacEnergySum comparison;comparison.add(width.value().magnitude().shifted(-1).negated());
        for(std::size_t k=0;k<residual.size();++k)
            comparison.add(sign>0 ? residual.component(k) : residual.component(k).negated());
        if(comparison.sign()<0 || (comparison.sign()==0 && (std::bit_cast<std::uint64_t>(value.mantissa())&1U)==0))return value;
        value=adjacent;
    }
    throw std::runtime_error("MAC primary range rounding did not converge");
}
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
    if (remainder.full.is_zero()) return canonical_mac_primary(value, remainder.projected) &&
        (remainder.projected!=0 || !std::signbit(remainder.projected));
    const auto low = mac_stored_remainder_value(remainder);
    return nearest_high(value, low) == value;
}
MacPrimaryFullWrite store_mac_primary_remainder(const MacEnergySum& total) {
    MacPrimaryFullWrite result; result.value = rounded_range(total).project().value;
    MacEnergySum correction = total; correction.add(-result.value);
    result.high_rounded = correction.sign() != 0;
    auto low = rounded_range(correction);
    bool normalized = false;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        const double next = nearest_high(result.value, low);
        if (next == result.value) { normalized = true; break; }
        if (!std::isfinite(next)) throw std::overflow_error("MAC primary remainder normalization overflow");
        result.value = next;
        correction=total;correction.add(-result.value);low=rounded_range(correction);
    }
    if (!normalized) throw std::runtime_error("MAC primary remainder did not normalize");
    const auto projection = low.project(); result.remainder.projected = projection.value;
    if (projection.rounded) result.remainder.full = low;
    result.discarded = total; result.discarded.add(-result.value); result.discarded.add(low.negated());
    return result;
}
void audit_mac_primary_write(const MacPrimaryFullWrite& write) {
    const int sign = write.discarded.sign();
    if (sign == 0) return;
    const auto low = mac_stored_remainder_value(write.remainder);
    if (low.is_zero()) throw std::domain_error("MAC primary discarded a representable low part");
    MacEnergySum margin; margin.add(MacEnergy::from_parts(.5,low.exponent()-53));
    for (std::size_t k=0;k<write.discarded.size();++k)
        margin.add(sign<0 ? write.discarded.component(k) : write.discarded.component(k).negated());
    if (margin.sign()<0) throw std::domain_error("MAC primary discarded error exceeds half a low-part quantum");
}
void MacPrimaryRoundingCounts::observe(const MacPrimaryFullWrite& write) {
    audit_mac_primary_write(write);
    merge({static_cast<std::size_t>(write.high_rounded),static_cast<std::size_t>(write.discarded.sign()!=0)});
    for(std::size_t k=0;k<write.discarded.size();++k)error_j.add(write.discarded.component(k));
}
}
