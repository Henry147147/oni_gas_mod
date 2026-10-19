#pragma once
#include "mixed_atmospheres/mac_compressive_work.hpp"
#include <cmath>

namespace ma::experimental::entropy_detail {
inline MacEnergy multiply(MacEnergy a,MacEnergy b) {
    return (b.mantissa()<0 ? a.negated() : a).shared(b.magnitude(),MacEnergy::from_double(1));
}
inline MacEnergy logarithmic_increment(MacEnergy change,MacEnergy before) {
    if(before.mantissa()<=0)throw FluidFatalError(FluidFatalCode::numerical_precision,"Relative material entropy requires positive thermal energy");
    const auto ratio=change.shared(MacEnergy::from_double(1),before);
    if(ratio.is_zero())return {};
    if(ratio.exponent()<MacExponent(-26)) {
        // The omitted fourth-order log1p term is below one binary64 ULP.
        // Keep the full-exponent leading term even if its projection is zero.
        const double x=ratio.project().value;
        return ratio.multiplied(1-.5*x+x*x/3);
    }
    const double x=ratio.require_representable();
    if(!(x>-1))throw FluidFatalError(FluidFatalCode::numerical_precision,"Relative material entropy left its logarithmic domain");
    return MacEnergy::from_double(std::log1p(x));
}
inline MacEnergy logarithmic_ratio(double after,double before) {
    if(!std::isfinite(after) || !std::isfinite(before) || after<=0 || before<=0)
        throw FluidFatalError(FluidFatalCode::numerical_precision,"Relative material entropy requires positive finite EOS readings");
    MacEnergySum change;change.add(after);change.add(-before);
    const auto small=change.value().magnitude().shared(1,before);
    if(small.is_zero() || small.exponent()<MacExponent(0))
        return logarithmic_increment(change.value(),MacEnergy::from_double(before));
    return MacEnergy::from_double(std::log(after)-std::log(before));
}
// Constant constituent masses: dS=Cv*log(T1/T0)+nR*log(Vg1/Vg0).
// T1/T0 comes from the actual conserved caloric increment, avoiding both
// absolute entropy subtraction and a rounded-temperature subtraction: the
// thermal energy is U minus the caloric offsets (and, with liquid, minus the
// EOS elastic energy). The reported headspace nRT/p carries the EOS root's
// volume tolerance (2e-12 V), an absolute entropy error of about that
// tolerance times pV/T whatever the headspace. While liquid fills at most
// half of both endpoints the closure V - Vliquid is used instead: such
// gas-dominated cells have small stored-U allowances, which that residue
// exceeds for a small heat exchange beside trace liquid. Liquid-dominated
// cells keep the reported headspace, where V - Vliquid can cancel to zero.
template<class Before,class After>
inline MacEnergy change(const FluidThermodynamics& model,const MixtureInventory& source,double v0,
    const Before& before,const MixtureInventory& endpoint,double v1,const After& after) {
    if(source.masses!=endpoint.masses)throw std::invalid_argument("Relative material entropy requires unchanged constituents");
    const auto input=model.to_reference(source,v0);const auto& definitions=model.reference().materials();
    MacEnergySum capacity,gas,offset;bool liquid=false;
    for(std::size_t i=0;i<definitions.size();++i)if(input.masses_kg[i]>0) {
        const auto mass=MacEnergy::from_double(input.masses_kg[i]);const auto& material=definitions[i];
        capacity.add(mass.multiplied(material.cv_j_per_kg_k));offset.add(mass.multiplied(material.energy_offset_j_per_kg));
        if(material.phase==ReferencePhase::ideal_gas)gas.add(mass.shared(1,material.molar_mass_kg_per_mol).multiplied(reference_gas_constant));
        else liquid=true;
    }
    MacEnergySum thermal,increment;thermal.add(source.energy);thermal.add(offset.value().negated());
    increment.add(endpoint.energy);increment.add(-source.energy);
    if(liquid) { thermal.add(-before.elastic_energy_j);increment.add(-after.elastic_energy_j);increment.add(before.elastic_energy_j); }
    const auto thermal_log=logarithmic_increment(increment.value(),thermal.value());
    MacEnergySum result;result.add(multiply(capacity.value(),thermal_log));
    if(!gas.value().is_zero()) {
        const bool closure=liquid && before.liquid_volume_m3<=.5*v0 && after.liquid_volume_m3<=.5*v1;
        const double h0=!liquid ? v0 : closure ? v0-before.liquid_volume_m3 : before.gas_volume_m3;
        const double h1=!liquid ? v1 : closure ? v1-after.liquid_volume_m3 : after.gas_volume_m3;
        result.add(multiply(gas.value(),logarithmic_ratio(h1,h0)));
    }
    return result.value();
}
}
