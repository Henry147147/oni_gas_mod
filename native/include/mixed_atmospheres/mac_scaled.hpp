#pragma once
#include "mac_energy.hpp"

namespace ma::experimental {
// Unit-neutral names for the unchanged significand/exponent arithmetic used by
// energy accounts. Parcel contributions may use these numbers without making
// a binary64 projection. Primary cell storage remains explicitly binary64.
using MacScaled = MacEnergy;
using MacScaledSum = MacEnergySum;
using MacScaledAccount = MacEnergyAccount;
}
