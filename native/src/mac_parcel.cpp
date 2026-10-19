#include "mixed_atmospheres/mac_parcel.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>

namespace ma::experimental {
[[noreturn]] void mac_field_precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
namespace {
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
}
MacScaled mac_parcel_volume(const MacPrimalParcel& parcel) {
    if (!std::isfinite(parcel.reference_volume_m3) || parcel.reference_volume_m3 < 0 ||
        !std::isfinite(parcel.volume_m3))
        throw std::invalid_argument("Invalid MAC parcel reference/actual volume");
    const auto volume = mac_projected_value(parcel.volume_m3, parcel.full_volume_m3);
    if (volume.mantissa() <= 0) throw std::invalid_argument("Nonpositive MAC full parcel volume");
    return volume;
}
MacScaled mac_parcel_quantity(const MacPrimalParcel& parcel, double quantity) {
    return mac_parcel_quantity(parcel,MacScaled::from_double(quantity));
}
MacScaled mac_parcel_quantity(const MacPrimalParcel& parcel, MacScaled value) {
    const auto volume = mac_parcel_volume(parcel);
    if (parcel.reference_volume_m3 == 0 && !parcel.full_volume_m3.is_zero()) throw std::invalid_argument("Full MAC parcel needs a normalized prototype");
    return parcel.reference_volume_m3 == 0 ? value : value.shared(volume, MacScaled::from_double(parcel.reference_volume_m3));
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
        MacScaledSum sum; sum.add(mac_parcel_quantity(parcel, value).shared(MacScaled::from_double(volume), mac_parcel_volume(parcel)));
        return store_mac_primary(sum, nonnegative);
    };
    for (auto& mass : result.mixture.masses) mass = normalized(mass, true);
    MacEnergySum energy;
    const auto scale = MacScaled::from_double(volume);
    energy.add(mac_parcel_quantity(parcel,result.mixture.energy).shared(scale,mac_parcel_volume(parcel)));
    energy.add(mac_parcel_quantity(parcel,mac_stored_remainder_value(result.energy_remainder_j)).shared(scale,mac_parcel_volume(parcel)));
    (void)store_mac_cell_internal_energy(result,energy);
    for (auto& passive : result.passive_quantities) passive.quantity = normalized(passive.quantity, true);
    return result;
}
MacScaled mac_parcel_mass_scaled(const MacPrimalParcel& parcel) {
    const auto volume = mac_parcel_volume(parcel);
    if (parcel.reference_volume_m3 == 0 && !parcel.full_volume_m3.is_zero())
        throw std::invalid_argument("Full MAC parcel needs a normalized prototype");
    const auto reference = MacScaled::from_double(parcel.reference_volume_m3);
    MacScaledSum total;
    for (const auto mass : parcel.contents.mixture.masses) {
        if (!std::isfinite(mass) || mass < 0) throw std::invalid_argument("Invalid MAC parcel constituent mass");
        const auto value = MacScaled::from_double(mass);
        total.add(parcel.reference_volume_m3 == 0 ? value : value.shared(volume, reference));
    }
    if (total.sign() == 0) {
        if (parcel.contents.mixture.energy != 0 || !parcel.contents.energy_remainder_j.is_zero() || !parcel.contents.passive_quantities.empty())
            throw std::invalid_argument("MAC vacuum parcel contains energy or passive inventory");
        return {};
    }
    const auto result = mac_scaled_primary(total);
    if (!(result.mantissa() > 0)) precision("Positive MAC parcel bulk mass is unrepresentable");
    return result;
}
double mac_parcel_mass(const MacPrimalParcel& parcel) { return mac_parcel_mass_scaled(parcel).require_representable(); }
}
