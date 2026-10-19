#include "mac_kernel_detail.hpp"

namespace ma::experimental::mac_kernel {
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
