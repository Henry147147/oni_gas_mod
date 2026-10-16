#pragma once
#include "mac_scaled.hpp"
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

namespace ma::experimental {
[[noreturn]] void mac_field_precision(const char*);
// A full field is authoritative when present. Its binary64 projections must
// agree bit-for-bit; mutating only a projection produces an invalid input.
[[nodiscard]] inline std::vector<MacScaled> mac_field_values(const std::vector<double>& projected, const std::vector<MacScaled>& full) {
    if (!full.empty() && full.size() != projected.size()) throw std::invalid_argument("MAC full/projection field shape differs");
    std::vector<MacScaled> result; result.reserve(projected.size());
    for (std::size_t i = 0; i < projected.size(); ++i) {
        const auto value = full.empty() ? MacScaled::from_double(projected[i]) : full[i];
        if (!full.empty() && std::bit_cast<std::uint64_t>(value.project().value) != std::bit_cast<std::uint64_t>(projected[i]))
            throw std::invalid_argument("MAC full field differs from its explicit binary64 projection");
        result.push_back(value);
    }
    return result;
}
inline void store_mac_field(std::vector<double>& projected, std::vector<MacScaled>& full, std::span<const MacScaled> values) {
    std::vector<double> next; next.reserve(values.size()); bool rounded = false;
    for (auto value : values) { const auto p = value.project(); next.push_back(p.value); rounded = rounded || p.rounded; }
    projected = std::move(next);
    if (rounded) full.assign(values.begin(), values.end()); else full.clear();
}
[[nodiscard]] inline MacScaled mac_scaled_difference(MacScaled a, MacScaled b) {
    MacScaledSum sum; sum.add(a); sum.add(b.negated()); return sum.value();
}
[[nodiscard]] inline MacScaled mac_scaled_primary(const MacScaledSum& sum) {
    const auto value = sum.value();
    if (sum.sign() != 0 && (value.is_zero() || (value.mantissa() < 0 ? -1 : 1) != sum.sign()))
        mac_field_precision("MAC scaled primary lost its nonzero sign");
    static_cast<void>(value.project()); // Preserve the previous upper-range limit.
    MacScaledAccount account;
    for (std::size_t i = 0; i < sum.size(); ++i) account.add(sum.component(i));
    account.add(value.negated());
    if (!account.within_bound()) mac_field_precision("MAC scaled primary rounding exceeds its operand bound");
    return value;
}
}
