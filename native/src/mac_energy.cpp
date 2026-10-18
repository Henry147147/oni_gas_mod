#include "mixed_atmospheres/mac_energy.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ma::experimental {
namespace {
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
void finite(double value) { if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite MAC energy operand"); }
double split(double value, int& power) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto biased = static_cast<int>((bits >> 52) & UINT64_C(0x7ff));
    if (biased != 0) {
        power = biased - 1022;
        return std::bit_cast<double>((bits & UINT64_C(0x800fffffffffffff)) | UINT64_C(0x3fe0000000000000));
    }
    return std::frexp(value, &power);
}
MacEnergy normalized(double mantissa, MacExponent power) {
    const auto value = MacEnergy::from_double(mantissa);
    if (value.is_zero()) return {};
    return MacEnergy::from_parts(value.mantissa(), power + value.exponent());
}
}
void require_mac_energy_environment() {
#if defined(_M_X64) || defined(__x86_64__)
    const auto control = _mm_getcsr();
    if (std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 &&
        std::numeric_limits<double>::max_exponent == 1024 && std::fegetround() == FE_TONEAREST &&
        (control & 0xe040U) == 0 && (control & 0x1f80U) == 0x1f80U) return;
#endif
    precision("MAC energy arithmetic requires binary64 x86-64 nearest rounding, FTZ/DAZ off and masked SSE exceptions");
}
MacEnergy MacEnergy::from_parts(double mantissa, MacExponent power) {
    finite(mantissa);
    if (mantissa == 0) {
        if (power != 0) throw std::invalid_argument("Zero MAC energy must have zero exponent");
        return {};
    }
    if (std::abs(mantissa) < .5 || std::abs(mantissa) >= 1) throw std::invalid_argument("MAC energy significand is not normalized");
    return {mantissa, std::move(power)};
}
MacEnergy MacEnergy::from_double(double value) {
    finite(value); if (value == 0) return {};
    // Scaling a normal binary64 by an exact power of two changes only its
    // exponent bits. Keep frexp for subnormals; it normalizes them exactly.
    int power = 0; const double mantissa = split(value, power);
    return {mantissa, power};
}
MacEnergy MacEnergy::kinetic(double inertia, double momentum) {
    finite(inertia); finite(momentum);
    if (inertia < 0 || (inertia == 0 && momentum != 0)) throw std::invalid_argument("MAC kinetic energy has no positive inertia");
    if (momentum == 0) return {};
    int ei = 0, ej = 0; const double mi = split(inertia, ei), mj = split(momentum, ej);
    return normalized(.5 * mj * (mj / mi), 2 * static_cast<std::int64_t>(ej) - ei);
}
MacEnergy MacEnergy::variance(double mass, double difference) {
    finite(mass); finite(difference); if (mass < 0) throw std::invalid_argument("Negative MAC variance mass");
    if (mass == 0 || difference == 0) return {};
    int em = 0, eu = 0; const double m = split(mass, em), u = split(difference, eu);
    return normalized(.5 * m * u * u, static_cast<std::int64_t>(em) + 2 * static_cast<std::int64_t>(eu));
}
MacEnergy MacEnergy::kinetic(double inertia, MacEnergy momentum) {
    finite(inertia);
    if (inertia < 0 || (inertia == 0 && !momentum.is_zero())) throw std::invalid_argument("MAC kinetic energy has no positive inertia");
    if (momentum.is_zero()) return {};
    int ei = 0; const double mi = split(inertia, ei), mj = momentum.mantissa();
    return normalized(.5 * mj * (mj / mi), momentum.exponent().doubled() - ei);
}
MacEnergy MacEnergy::variance(MacEnergy mass, MacEnergy difference) {
    if (mass.mantissa() < 0) throw std::invalid_argument("Negative MAC variance mass");
    if (mass.is_zero() || difference.is_zero()) return {};
    return normalized(.5 * mass.mantissa() * difference.mantissa() * difference.mantissa(),
        mass.exponent() + difference.exponent().doubled());
}
MacEnergy MacEnergy::shared(double part, double total) const {
    finite(part); finite(total); if (part < 0 || total <= 0) throw std::invalid_argument("Invalid MAC derived energy share");
    if (part == 0 || is_zero()) return {};
    if (part == total) return *this;
    int ep = 0, et = 0; const double p = split(part, ep), t = split(total, et);
    return normalized(mantissa_ * p / t, exponent_ + ep - et);
}
MacEnergy MacEnergy::shared(MacEnergy part, MacEnergy total) const {
    if (part.mantissa() < 0 || total.mantissa() <= 0) throw std::invalid_argument("Invalid MAC scaled share");
    if (part.is_zero() || is_zero()) return {};
    if (part.mantissa() == total.mantissa() && part.exponent() == total.exponent()) return *this;
    return normalized(mantissa_ * part.mantissa() / total.mantissa(),
        exponent_ + part.exponent() - total.exponent());
}
MacEnergy MacEnergy::multiplied(double factor) const {
    finite(factor); if (factor == 0 || is_zero()) return {};
    int power = 0; const double value = split(factor, power);
    return normalized(mantissa_ * value, exponent_ + power);
}
MacEnergy MacEnergy::shifted(int power) const {
    return is_zero() ? MacEnergy{} : from_parts(mantissa_, exponent_ + power);
}
MacEnergy MacEnergy::negated() const noexcept { return {-mantissa_, exponent_}; }
MacEnergy MacEnergy::magnitude() const noexcept { return {std::abs(mantissa_), exponent_}; }
MacEnergy MacEnergy::square_root() const {
    if (mantissa_ < 0) throw std::invalid_argument("Negative MAC square root operand");
    if (is_zero()) return {};
    return normalized(std::sqrt(exponent_.is_odd() ? 2 * mantissa_ : mantissa_), exponent_.halved_floor());
}
MacEnergyProjection MacEnergy::project() const {
    if (is_zero()) return {};
    if (exponent_ > 1024) precision("MAC derived energy exceeds binary64 upper range");
    if (exponent_ >= -1021) {
        // Every significand bit survives a normal projection. This path
        // cannot underflow, overflow or round, including DBL_MIN/DBL_MAX.
        const auto bits = std::bit_cast<std::uint64_t>(mantissa_);
        return {std::bit_cast<double>((bits & UINT64_C(0x800fffffffffffff)) |
            (static_cast<std::uint64_t>(exponent_.to_int_exact() + 1022) << 52)), false};
    }
    const double result = exponent_ < -1074 ? std::copysign(0., mantissa_) : std::ldexp(mantissa_, exponent_.to_int_exact());
    if (!std::isfinite(result)) precision("MAC derived energy exceeds binary64 upper range");
    const auto represented = from_double(result);
    return {result, represented.mantissa_ != mantissa_ || represented.exponent_ != exponent_};
}
double MacEnergy::require_representable() const {
    const auto projection = project();
    if (projection.rounded) precision("MAC derived energy is not exactly representable as binary64");
    return projection.value;
}
MacEnergyPair add_mac_energy(MacEnergy a, MacEnergy b) {
    if (a.is_zero()) return {b, {}};
    if (b.is_zero()) return {a, {}};
    if (a.exponent() < b.exponent() || (a.exponent() == b.exponent() && std::abs(a.mantissa()) < std::abs(b.mantissa()))) std::swap(a, b);
    const auto gap = a.exponent() - b.exponent();
    if (gap > 54) return {a, b};
    // The gap is at most 54; the aligned significand is always normal.
    const auto bits = std::bit_cast<std::uint64_t>(b.mantissa());
    const double x = a.mantissa(), y = std::bit_cast<double>((bits & UINT64_C(0x800fffffffffffff)) |
        (static_cast<std::uint64_t>(1022 - gap.to_int_exact()) << 52));
    const double sum = x + y, z = sum - x, error = (x - (sum - z)) + (y - z);
    return {normalized(sum, a.exponent()), normalized(error, a.exponent())};
}
MacEnergy MacEnergySum::at(std::size_t i) const noexcept { return i < local_.size() ? local_[i] : overflow_[i - local_.size()]; }
MacEnergy MacEnergySum::component(std::size_t i) const {
    if (i >= used_) throw std::out_of_range("MAC energy expansion component");
    return at(i);
}
void MacEnergySum::put(std::size_t i, MacEnergy value) {
    if (i < local_.size()) local_[i] = value;
    else {
        const auto index = i - local_.size();
        if (index == overflow_.size()) overflow_.push_back(value);
        else overflow_[index] = value;
    }
}
void MacEnergySum::add(MacEnergy value) {
    if (value.is_zero()) return;
    if (used_ >= 4096) precision("MAC derived energy expansion storage capacity exceeded");
    std::size_t written = 0;
    for (std::size_t i = 0; i < used_; ++i) {
        const auto next = add_mac_energy(value, at(i));
        if (!next.low.is_zero()) put(written++, next.low);
        value = next.high;
    }
    if (!value.is_zero()) put(written++, value);
    used_ = written;
}
int MacEnergySum::sign() const noexcept {
    return used_ == 0 ? 0 : at(used_ - 1).mantissa() < 0 ? -1 : 1;
}
MacEnergy MacEnergySum::value() const {
    MacEnergy result;
    for (std::size_t i = 0; i < used_; ++i) result = add_mac_energy(result, at(i)).high;
    return result;
}
void MacEnergyAccount::add(MacEnergy value) { signed_.add(value); absolute_.add(value.magnitude()); }
bool MacEnergyAccount::within_bound() const {
    MacEnergySum difference;
    for (std::size_t i = 0; i < absolute_.size(); ++i) difference.add(absolute_.component(i).shifted(-45));
    const int polarity = signed_.sign();
    for (std::size_t i = 0; i < signed_.size(); ++i) difference.add(polarity < 0 ? signed_.component(i) : signed_.component(i).negated());
    return difference.sign() >= 0;
}
}
