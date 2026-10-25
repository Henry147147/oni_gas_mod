#pragma once
#include "mixed_atmospheres/oni_conduit_contact.h"
#include <algorithm>
#include <cmath>

namespace ma::oni::conduit_contact {
// Binary32 compatibility law for the pinned Windows conduit contact. Windows
// inlines the three pure helpers; Linux retains its original-helper replay.
// Units: temperatures K, conductivities W/(m K), capacities kJ/K, work kJ.
// This is a projection boundary, independent of MacClosedWorld's arithmetic.
inline MAOniConduitContactResult replay(const MAOniConduitContactInput& v) {
    const float a=v.fluid_temperature_k,b=v.body_temperature_k;
    const float ca=v.fluid_heat_capacity_kj_k,cb=v.body_heat_capacity_kj_k;
    const float conductivity=(v.flags&1) ? std::min(v.fluid_conductivity,v.conduit_conductivity) :
        (v.fluid_conductivity+v.conduit_conductivity)*.5f;
    const float power=(conductivity*(a-b))*50.0f;
    const float proposed=(power*v.seconds)*.001f;
    const float ia=1.0f/ca,ib=1.0f/cb;
    const float low=std::min(a,b),high=std::max(a,b);
    // Limit work by both represented endpoint changes, never by an unexplained
    // residual. Multiplication by the rounded reciprocal is intentional.
    const float da=std::clamp(a-ia*proposed,low,high)-a;
    const float db=std::clamp(b+ib*proposed,low,high)-b;
    const float magnitude=std::min(std::abs(da)*ca,std::abs(db)*cb);
    const float heat=magnitude*(power>=0 ? 1.0f : -1.0f);
    const float into_fluid=-heat;
    float fluid=std::max(a+ia*into_fluid,0.0f);
    const float other=std::max(b-ib*into_fluid,0.0f);
    if ((fluid-other)*(a-b)<0) {
        const float total_inverse=1.0f/(ca+cb);
        fluid=((total_inverse*ca)*a)+((total_inverse*cb)*b);
    }
    const float body_delta=heat/cb;
    return {20,1,heat,fluid,b+body_delta};
}
}
