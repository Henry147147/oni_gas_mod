#include "mixed_atmospheres/mac_parcel.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>

namespace ma::experimental {
namespace {
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
}
MacScaled mac_parcel_quantity(const MacPrimalParcel& parcel, double quantity) {
    if (!std::isfinite(parcel.reference_volume_m3) || parcel.reference_volume_m3 < 0 ||
        !std::isfinite(parcel.volume_m3) || parcel.volume_m3 <= 0)
        throw std::invalid_argument("Invalid MAC parcel reference/actual volume");
    const auto value = MacScaled::from_double(quantity);
    return parcel.reference_volume_m3 == 0 ? value : value.shared(parcel.volume_m3, parcel.reference_volume_m3);
}
double store_mac_primary(const MacScaledSum& sum, bool nonnegative) {
    const int sign = sum.sign();
    if (nonnegative && sign < 0) throw std::domain_error("Negative MAC primary inventory");
    const double stored = sum.value().project().value;
    if (sign != 0 && (stored == 0 || (stored < 0 ? -1 : 1) != sign))
        precision("Nonzero accumulated MAC primary inventory is unrepresentable");
    MacScaledAccount residual;
    for (std::size_t i = 0; i < sum.size(); ++i) residual.add(sum.component(i));
    residual.add(-stored);
    if (!residual.within_bound()) precision("MAC primary storage rounding exceeds its operand bound");
    return stored;
}
MacMaterialCell mac_parcel_contents_at(const MacPrimalParcel& parcel, double volume) {
    if (!std::isfinite(volume) || volume <= 0) throw std::invalid_argument("Invalid normalized MAC parcel volume");
    MacMaterialCell result = parcel.contents;
    const auto normalized = [&](double value, bool nonnegative) {
        MacScaledSum sum; sum.add(mac_parcel_quantity(parcel, value).shared(volume, parcel.volume_m3));
        return store_mac_primary(sum, nonnegative);
    };
    for (auto& mass : result.mixture.masses) mass = normalized(mass, true);
    result.mixture.energy = normalized(result.mixture.energy, false);
    for (auto& passive : result.passive_quantities) passive.quantity = normalized(passive.quantity, true);
    return result;
}
double mac_parcel_mass(const MacPrimalParcel& parcel) {
    MacScaledSum total;
    for (const auto mass : parcel.contents.mixture.masses) total.add(mac_parcel_quantity(parcel, mass));
    const double result = store_mac_primary(total, true);
    if (!(result > 0)) precision("Positive MAC parcel bulk mass is unrepresentable");
    return result;
}
}
