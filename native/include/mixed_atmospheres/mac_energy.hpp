#pragma once
#include "mac_exponent.hpp"
#include <array>
#include <cstddef>
#include <vector>
#include <utility>

namespace ma::experimental {
// Read-only entry guard. Call once on the executing thread before a batch of
// energy operations; no rounding/FTZ/DAZ/exception setting is changed here.
void require_mac_energy_environment();
struct MacEnergyProjection { double value = 0; bool rounded = false; };
// Range arithmetic: mantissa * 2^exponent, with a binary64 significand and an
// independent exponent. MacScaled supplies a unit-neutral name for explicit
// canonical/flux fields as well as derived energy and accounting operands.
class MacEnergy {
public:
    MacEnergy() noexcept = default;
    [[nodiscard]] static MacEnergy from_double(double);
    [[nodiscard]] static MacEnergy from_parts(double mantissa, MacExponent exponent);
    [[nodiscard]] static MacEnergy kinetic(double inertia, double momentum);
    [[nodiscard]] static MacEnergy kinetic(double inertia, MacEnergy momentum);
    [[nodiscard]] static MacEnergy variance(double mass, double velocity_difference);
    [[nodiscard]] static MacEnergy variance(MacEnergy mass, MacEnergy velocity_difference);
    [[nodiscard]] MacEnergy shared(double part, double total) const;
    [[nodiscard]] MacEnergy shared(MacEnergy part, MacEnergy total) const;
    [[nodiscard]] MacEnergy multiplied(double factor) const;
    [[nodiscard]] MacEnergy shifted(int power_of_two) const;
    [[nodiscard]] MacEnergy negated() const noexcept;
    [[nodiscard]] MacEnergy magnitude() const noexcept;
    // Nonnegative square root without projecting the exponent or value.
    [[nodiscard]] MacEnergy square_root() const;
    [[nodiscard]] bool is_zero() const noexcept { return mantissa_ == 0; }
    [[nodiscard]] double mantissa() const noexcept { return mantissa_; }
    [[nodiscard]] const MacExponent& exponent() const noexcept { return exponent_; }
    // Projection is explicit: below-range/subnormal rounding is reported.
    // Overflow rejects, preserving the previous upper representability limit.
    [[nodiscard]] MacEnergyProjection project() const;
    [[nodiscard]] double require_representable() const;
private:
    double mantissa_ = 0;
    MacExponent exponent_{};
    MacEnergy(double mantissa, MacExponent exponent) noexcept : mantissa_(mantissa), exponent_(std::move(exponent)) {}
};
struct MacEnergyPair { MacEnergy high, low; };
// Error-free addition of the represented operands, including exponent gaps
// larger than the binary64 range. All floating operations use normal operands.
[[nodiscard]] MacEnergyPair add_mac_energy(MacEnergy, MacEnergy);
class MacEnergySum {
public:
    void add(MacEnergy);
    void add(double);
    [[nodiscard]] std::size_t size() const noexcept { return used_; }
    [[nodiscard]] MacEnergy component(std::size_t) const;
    [[nodiscard]] int sign() const noexcept;
    // A rounded significand estimate. Components retain the exact additive
    // expansion; callers must not infer an exact sum from the estimate alone.
    [[nodiscard]] MacEnergy value() const;
private:
    // Four ordinary components stay inline. Promotion retains the same full
    // expansion for subnormals, range extension and larger component counts.
    std::array<double, 4> local_{};
    std::vector<MacEnergy> extended_;
    std::size_t used_ = 0;
    bool extended_mode_ = false;
    [[nodiscard]] MacEnergy at(std::size_t) const noexcept;
    void put(std::size_t, MacEnergy);
    [[nodiscard]] bool try_native_add(double);
    void promote();
    void add_extended(MacEnergy);
};
class MacEnergyAccount {
public:
    void add(MacEnergy);
    void add(double value) { add(MacEnergy::from_double(value)); }
    [[nodiscard]] MacEnergy residual() const { return signed_.value(); }
    [[nodiscard]] MacEnergy bound() const { return absolute_.value().shifted(-45); }
    // Compare exact additive expansions at 128 binary64 epsilons of the
    // actual represented operands. No absolute unit floor or projected zero.
    [[nodiscard]] bool within_bound() const;
    [[nodiscard]] const MacEnergySum& terms() const noexcept { return signed_; }
private:
    MacEnergySum signed_, absolute_;
};
}
