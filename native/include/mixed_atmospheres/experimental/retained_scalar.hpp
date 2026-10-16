#pragma once
#include "canonical_material_diagnostics.hpp"
#include <array>

namespace ma::experimental {
struct RetainedScalarUpdate;
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
// Both functions require the same read-only nearest/FTZ-off/DAZ-off environment
// as canonical diagnostics. Normal binary64 operands/results or exact zero only.
// A fresh caller witness receives the first typed failure; inputs never mutate.
[[nodiscard]] RetainedScalar make_retained_scalar(double high, double low, CanonicalFailureWitness& witness);
[[nodiscard]] RetainedScalarUpdate add_retained_scalar(const RetainedScalar& input, double increment,
    CanonicalFailureWitness& witness);
}
