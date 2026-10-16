#include "mixed_atmospheres/passive_quantities.hpp"
#include "mixed_atmospheres/species.hpp"
#include <algorithm>
#include <stdexcept>

namespace ma {
void validate_passive_quantities(std::span<const PassiveQuantity> values) {
    if (values.size() > 65537) throw std::invalid_argument("Too many passive identities in one fluid cell");
    std::uint64_t previous = 0;
    for (const auto& value : values) {
        require_finite(value.quantity, "passive quantity");
        if (value.identity <= previous || value.identity > radiation_passive_identity || value.quantity <= 0)
            throw std::invalid_argument("Passive quantities require distinct sorted known identities and positive values");
        previous = value.identity;
    }
    if (values.size() > 65536 && values.back().identity != radiation_passive_identity)
        throw std::invalid_argument("Too many passive disease identities in one fluid cell");
}
PassiveQuantities scale_passive_quantities(std::span<const PassiveQuantity> values, double fraction) {
    validate_passive_quantities(values);
    require_finite(fraction, "passive transfer fraction");
    if (fraction < 0 || fraction > 1) throw std::invalid_argument("Invalid passive transfer fraction");
    PassiveQuantities result;
    if (fraction == 0) return result;
    result.reserve(values.size());
    for (const auto& value : values) {
        const double quantity = value.quantity * fraction;
        require_finite(quantity, "transferred passive quantity");
        if (quantity == 0) throw std::domain_error("Positive passive transfer is unrepresentable");
        result.push_back({value.identity, quantity});
    }
    return result;
}
void add_passive_quantities(PassiveQuantities& target, std::span<const PassiveQuantity> source, double fraction) {
    const auto incoming = scale_passive_quantities(source, fraction);
    if (incoming.empty()) return;
    PassiveQuantities result; result.reserve(target.size() + incoming.size());
    std::size_t i = 0, j = 0;
    while (i < target.size() || j < incoming.size()) {
        if (j == incoming.size() || (i < target.size() && target[i].identity < incoming[j].identity)) result.push_back(target[i++]);
        else if (i == target.size() || incoming[j].identity < target[i].identity) result.push_back(incoming[j++]);
        else {
            const double sum = target[i].quantity + incoming[j].quantity; require_finite(sum, "combined passive quantity");
            result.push_back({target[i].identity, sum}); ++i; ++j;
        }
    }
    validate_passive_quantities(result); target = std::move(result);
}
}
