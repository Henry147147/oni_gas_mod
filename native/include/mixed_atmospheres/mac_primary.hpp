#pragma once
#include "mac_scaled.hpp"
#include <cmath>
#include <limits>
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
void audit_mac_primary_write(const MacPrimaryFullWrite&);
struct MacPrimaryRoundingCounts {
    std::size_t rounded = 0, discarded = 0;
    MacEnergySum error_j{};
    void observe(const MacPrimaryFullWrite&);
    void merge(const MacPrimaryRoundingCounts& other) {
        if (other.rounded > std::numeric_limits<std::size_t>::max() - rounded ||
            other.discarded > std::numeric_limits<std::size_t>::max() - discarded)
            throw std::overflow_error("MAC primary rounding counter overflow");
        rounded += other.rounded; discarded += other.discarded;
        for(std::size_t k=0;k<other.error_j.size();++k)error_j.add(other.error_j.component(k));
    }
};

// Reference-account helpers. Production's normal arrays use scalar TwoSum and
// interval DD accounts directly, without constructing range values here.
template<class Account, class Cell>
void account_mac_cell_internal_energy(Account& account, const Cell& cell, double sign = 1) {
    account.add(sign * cell.mixture.energy);
    if (cell.energy_remainder_j.full.is_zero()) account.add(sign * cell.energy_remainder_j.projected);
    else account.add(cell.energy_remainder_j.full.multiplied(sign));
}
template<class Cell>
[[nodiscard]] MacPrimaryFullWrite store_mac_cell_internal_energy(Cell& cell, const MacEnergySum& total, MacPrimaryRoundingCounts* counts = nullptr) {
    auto write = store_mac_primary_remainder(total);
    if (counts) counts->observe(write); else audit_mac_primary_write(write);
    cell.mixture.energy = write.value; cell.energy_remainder_j = write.remainder;
    return write;
}
template<class Cell>
[[nodiscard]] MacPrimaryFullWrite add_mac_cell_internal_energy(Cell& cell, MacEnergy increment, MacPrimaryRoundingCounts* counts = nullptr) {
    if(!canonical_mac_primary(cell.mixture.energy,cell.energy_remainder_j))
        throw std::invalid_argument("Noncanonical incoming MAC primary-U state");
    MacEnergySum total; account_mac_cell_internal_energy(total, cell); total.add(increment);
    return store_mac_cell_internal_energy(cell, total, counts);
}
template<class Cell>
[[nodiscard]] bool same_mac_cell_internal_energy(const Cell& a, const Cell& b) {
    return a.mixture.energy == b.mixture.energy && same_mac_stored_remainder(a.energy_remainder_j, b.energy_remainder_j);
}
template<class Cell>
[[nodiscard]] MacEnergySum mac_cell_internal_energy_change(const Cell& before, const Cell& after) {
    MacEnergySum change; account_mac_cell_internal_energy(change, after); account_mac_cell_internal_energy(change, before, -1);
    return change;
}
template<class Cell>
[[nodiscard]] bool mac_cell_internal_energy_resolved(const Cell& before, const Cell& after, MacEnergy requested) {
    auto residual = mac_cell_internal_energy_change(before, after); residual.add(requested.negated());
    MacEnergySum budget; budget.add(requested.magnitude().shifted(-47));
    const int sign = residual.sign();
    for (std::size_t i = 0; i < residual.size(); ++i)
        budget.add(sign < 0 ? residual.component(i) : residual.component(i).negated());
    return budget.sign() >= 0;
}
template<class Account, class Cell>
void account_mac_cell_entropy_remainder(Account& account, const Cell& cell, double temperature, double sign = 1) {
    account.add(mac_stored_remainder_value(cell.energy_remainder_j).shared(1, temperature).multiplied(sign));
}

}
