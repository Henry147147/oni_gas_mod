#pragma once
#include "mixed_atmospheres/mac_compressive_work.hpp"

namespace ma::experimental::compression_detail {
// Private completion of one constitutive response. The pressure caller supplies
// its immutable source anchor and already validated isentropic response/tangent;
// the public entry validates and constructs those inputs itself.
[[nodiscard]] MacCompressiveWork finish(const FluidThermodynamics&,const MixtureInventory&,
    const ReferenceIsentrope&,const ReferenceIsentropicResponse&,double isentropic_work_derivative,
    double spacing,double dt,MacEnergy linear);
}
