#include "mac_kernel_detail.hpp"
#include <bit>

namespace ma::experimental::mac_kernel {
void PrimaryRounding::addition(double before, double increment, double stored) {
    if (!std::isfinite(before) || !std::isfinite(increment) || !std::isfinite(stored)) {
        invalid = true; return;
    }
    // TwoSum observes the exact rounding error of the unchanged addition.
    const double z = stored - before;
    const double residual = (before - (stored - z)) + (increment - z);
    if (!std::isfinite(residual)) { invalid = true; return; }
    if (residual == 0) return;
    if (count == std::numeric_limits<std::size_t>::max()) precision("Production primary rounding counter overflow");
    ++count;
    const double magnitude = std::abs(residual);
    error.add(magnitude);
    const auto bits = std::bit_cast<std::uint64_t>(stored);
    const auto exponent = (bits >> 52) & UINT64_C(0x7ff);
    // Sums at/below the lowest normal binade are exact on the binary64
    // integer-subnormal grid. An observed error there is an invalid write.
    if (exponent < 2) { invalid = true; return; }
    const auto quantum_bits = exponent > 53 ? (exponent - 53) << 52 : UINT64_C(1) << (exponent - 2);
    const double half = std::bit_cast<double>(quantum_bits);
    half_ulps.add(half);
    if (magnitude > half || (magnitude == half && (bits & 1U))) invalid = true;
    // The predecessor gap at a normal power of two is half its successor
    // gap. Check that side too, while retaining a conservative charge.
    if (std::signbit(residual) != std::signbit(stored) && (bits & UINT64_C(0xfffffffffffff)) == 0 &&
        2 * magnitude > half) invalid = true;
}
void PrimaryRounding::merge(const PrimaryRounding& other) {
    if (other.count > std::numeric_limits<std::size_t>::max() - count) precision("Production primary rounding counter overflow");
    count += other.count; error.add(other.error); half_ulps.add(other.half_ulps);
    invalid = invalid || other.invalid;
}
double PrimaryRounding::allowance() const {
    if (count > UINT64_C(9007199254740992)) precision("Production primary rounding counter exceeds exact audit range");
    const double total = half_ulps.value();
    if (total == 0) return 0;
    // Positive DD sums: cover accumulation and final projection, then round
    // outward. Subnormal envelope terms are covered by the outward quantum.
    const double envelope = (64 * eps * eps * (static_cast<double>(count) + 1) + 4 * eps) * total;
    return std::nextafter(total + envelope, std::numeric_limits<double>::infinity());
}
bool PrimaryRounding::within_bound(double audited_bound) const {
    const double charged = allowance();
    return !invalid && std::isfinite(audited_bound) && audited_bound >= 0 &&
        std::isfinite(charged) && error.value() >= 0 && error.value() <= charged && charged <= audited_bound &&
        (count != 0 || (error.value() == 0 && charged == 0));
}
namespace {
void record_error(TransferRounding& ledger, const MacEnergySum& difference) {
    const int sign = difference.sign();
    if (sign == 0) return;
    if (ledger.count == std::numeric_limits<std::size_t>::max()) precision("Production parcel rounding counter overflow");
    ++ledger.count;
    for (std::size_t j = 0; j < difference.size(); ++j) {
        const auto term = difference.component(j);
        ledger.error.add(sign < 0 ? term.negated() : term);
    }
}
}
void TransferRounding::product(double a, double b, double stored) {
    if (a == 0 || b == 0 || std::abs(stored) > std::numeric_limits<double>::min()) return;
    require(std::isfinite(a) && std::isfinite(b) && std::isfinite(stored), "Nonfinite production parcel product");
    // The significands and all split-product terms/errors are normal. Dekker's
    // error-free two-product is independent of the platform libm FMA (whose
    // MinGW/Wine result failed the saved exact-fraction boundary control).
    // The unscaled product/error may be below binary64 range. Strict global
    // compiler flags prevent contraction or reassociation of this transform.
    int ea = 0, eb = 0;
    const double ma = std::frexp(a, &ea), mb = std::frexp(b, &eb);
    constexpr double splitter = 0x1.0000002p27; // 2^27 + 1
    const double ca = splitter * ma, cb = splitter * mb;
    const double ah = ca - (ca - ma), bh = cb - (cb - mb);
    const double al = ma - ah, bl = mb - bh;
    const double high = ma * mb;
    const double e1 = high - ah * bh;
    const double e2 = e1 - al * bh;
    const double e3 = e2 - ah * bl;
    const double low = al * bl - e3;
    MacEnergySum difference;
    difference.add(MacEnergy::from_double(high).shifted(ea + eb));
    difference.add(MacEnergy::from_double(low).shifted(ea + eb));
    difference.add(-stored);
    record_error(*this, difference); // Exactly representable subnormals do not count.
}
void TransferRounding::projection(MacEnergy value, double stored) {
    MacEnergySum difference; difference.add(value); difference.add(-stored);
    record_error(*this, difference);
}
void TransferRounding::merge(const TransferRounding& other) {
    if (other.count > std::numeric_limits<std::size_t>::max() - count) precision("Production parcel rounding counter overflow");
    count += other.count;
    for (std::size_t j = 0; j < other.error.size(); ++j) error.add(other.error.component(j));
}
MacEnergy TransferRounding::bound() const {
    if (count > UINT64_C(9007199254740992)) precision("Production parcel rounding counter exceeds exact audit range");
    return MacEnergy::from_double(static_cast<double>(count)).shifted(-1075);
}
bool TransferRounding::within_bound() const {
    MacEnergySum remaining; remaining.add(bound());
    for (std::size_t j = 0; j < error.size(); ++j) remaining.add(error.component(j).negated());
    return error.sign() >= 0 && remaining.sign() >= 0 && (count != 0 || error.sign() == 0);
}
}
