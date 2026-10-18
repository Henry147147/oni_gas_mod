#pragma once
#include "mixed_atmospheres/mac_compressive_work.hpp"

namespace ma::experimental::compression_detail {
inline std::optional<double> pressure_increment(MacEnergy stress, std::optional<double> isentropic) {
    if (!isentropic) return std::nullopt;
    MacEnergySum sum; sum.add(*isentropic); sum.add(stress);
    const auto projected = sum.value().project();
    return projected.rounded ? std::nullopt : std::optional<double>(projected.value);
}
// Private completion of one constitutive response. The pressure caller supplies
// its immutable source anchor and already validated isentropic response/tangent;
// the public entry validates and constructs those inputs itself.
[[nodiscard]] MacCompressiveWork finish(const FluidThermodynamics&,const MixtureInventory&,
    const ReferenceIsentrope&,const ReferenceIsentropicResponse&,double isentropic_work_derivative,
    double spacing,double dt,MacEnergy linear);
}
