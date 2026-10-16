#pragma once
#include "canonical_material_diagnostics.hpp"
#include <array>

namespace ma::experimental {
struct RetainedScalarUpdate;
struct RetainedScalarProduct;
struct RetainedScalarDifference;
// An owning two-component stored value. The low component is retained state,
// not a discarded error estimate. No mutable component reference is exposed.
class RetainedScalar {
public:
    RetainedScalar() noexcept = default;
    [[nodiscard]] double high() const noexcept { return high_; }
    [[nodiscard]] double low() const noexcept { return low_; }
private:
    friend RetainedScalar make_retained_scalar(double, double, CanonicalFailureWitness&);
    friend RetainedScalarUpdate add_retained_scalar(const RetainedScalar&, double, CanonicalFailureWitness&);
    friend RetainedScalarProduct multiply_retained_scalar(const RetainedScalar&, double, CanonicalFailureWitness&);
    friend RetainedScalarDifference subtract_retained_scalar(const RetainedScalar&, const RetainedScalar&, CanonicalFailureWitness&);
    RetainedScalar(double high, double low) noexcept : high_(high), low_(low) {}
    double high_ = 0, low_ = 0;
};
struct RetainedScalarUpdate {
    RetainedScalar value;
    double requested_increment = 0;
    std::array<double, 2> represented_increment{};
    // Exact signed third component from the finite addition. It is explicitly
    // returned for accounting; no claim that a two-part value is arbitrary precision.
    double roundoff_remainder = 0;
};
struct RetainedScalarProduct {
    RetainedScalar value;
    // Exact products of each input component with the coefficient, in order
    // high-product, high-error, low-product, low-error. No component is hidden.
    std::array<double, 4> product_terms{};
    // A finite two-component result need not hold the exact full product.
    // These four signed components remain explicit accounting operands:
    // coefficient * (input.high + input.low) = value.high + value.low + sum(remainders).
    std::array<double, 4> roundoff_remainders{};
};
struct RetainedScalarDifference {
    RetainedScalar value;
    // A bounded exact difference certificate, in increasing magnitude. Only
    // expansion_count entries are present; unused entries are exact zero.
    std::array<double, 4> expansion_terms{};
    std::size_t expansion_count = 0;
    // Terms below the two stored components remain explicit accounting data:
    // (left.high + left.low) - (right.high + right.low)
    //   = value.high + value.low + remainder[0] + remainder[1].
    std::array<double, 2> roundoff_remainders{};
};
// These functions require the same read-only nearest/FTZ-off/DAZ-off environment
// as canonical diagnostics. Normal binary64 operands/results or exact zero only.
// A fresh caller witness receives the first typed failure; inputs never mutate.
[[nodiscard]] RetainedScalar make_retained_scalar(double high, double low, CanonicalFailureWitness& witness);
[[nodiscard]] RetainedScalarUpdate add_retained_scalar(const RetainedScalar& input, double increment,
    CanonicalFailureWitness& witness);
// Explicit FMA recovers each multiplication error. A zero FMA result is
// admitted only when an integer significand check proves an exact product;
// otherwise it could conceal an underflowed error. No FMA contraction is used.
[[nodiscard]] RetainedScalarProduct multiply_retained_scalar(const RetainedScalar& input, double coefficient,
    CanonicalFailureWitness& witness);
// Cancels the full stored pairs, rather than subtracting their rounded high
// components. The fixed four-term expansion is an operation certificate, not
// a variable-precision stored state or a fallback after a failed operation.
[[nodiscard]] RetainedScalarDifference subtract_retained_scalar(const RetainedScalar& left, const RetainedScalar& right,
    CanonicalFailureWitness& witness);
}
