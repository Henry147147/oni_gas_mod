#pragma once
#include "mac_transport.hpp"
#include "mac_scaled.hpp"

namespace ma::experimental {
// The actual extensive contribution represented by a parcel field. A nonzero
// reference volume means contents is a normalized prototype, not a projection
// of the smaller extensive parcel. All fields use the same volume ratio.
[[nodiscard]] MacScaled mac_parcel_quantity(const MacPrimalParcel&, double prototype_quantity);
[[nodiscard]] MacScaled mac_parcel_quantity(const MacPrimalParcel&, MacScaled prototype_quantity);
[[nodiscard]] MacScaled mac_parcel_volume(const MacPrimalParcel&);
[[nodiscard]] MacMaterialCell mac_parcel_contents_at(const MacPrimalParcel&, double volume_m3);
[[nodiscard]] double mac_parcel_mass(const MacPrimalParcel&);
[[nodiscard]] MacScaled mac_parcel_mass_scaled(const MacPrimalParcel&);
// Round only after accumulation. Positive/nonzero quantities may not disappear;
// the exact additive residual must satisfy the original 128-epsilon bound on
// actual operands. This rejects poorly resolved denormal inventories as before.
[[nodiscard]] double store_mac_primary(const MacScaledSum&, bool nonnegative);
}
