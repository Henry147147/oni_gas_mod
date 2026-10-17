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
// Pure gas uses the actual conserved caloric increment for T1/T0, avoiding
// both absolute entropy subtraction and a rounded-temperature subtraction.
// The mixed/liquid path reads the actual stored-state EOS temperatures and
// headspaces; no hypothetical isentropic endpoint substitutes for actual U.
inline MacEnergy change(const FluidThermodynamics& model,const MixtureInventory& source,double v0,
    const ReferenceEquilibriumState& before,const MixtureInventory& endpoint,double v1,
    const ReferenceEquilibriumState& after) {
    if(source.masses!=endpoint.masses)throw std::invalid_argument("Relative material entropy requires unchanged constituents");
    const auto input=model.to_reference(source,v0);const auto& definitions=model.reference().materials();
    MacEnergySum capacity,gas,offset;bool liquid=false;
    for(std::size_t i=0;i<definitions.size();++i)if(input.masses_kg[i]>0) {
        const auto mass=MacEnergy::from_double(input.masses_kg[i]);const auto& material=definitions[i];
        capacity.add(mass.multiplied(material.cv_j_per_kg_k));offset.add(mass.multiplied(material.energy_offset_j_per_kg));
        if(material.phase==ReferencePhase::ideal_gas)gas.add(mass.shared(1,material.molar_mass_kg_per_mol).multiplied(reference_gas_constant));
        else liquid=true;
    }
    MacEnergy thermal_log;
    if(!liquid) {
        MacEnergySum thermal,increment;thermal.add(source.energy);thermal.add(offset.value().negated());
        increment.add(endpoint.energy);increment.add(-source.energy);
        thermal_log=logarithmic_increment(increment.value(),thermal.value());
    } else thermal_log=logarithmic_ratio(after.temperature_k,before.temperature_k);
    MacEnergySum result;result.add(multiply(capacity.value(),thermal_log));
    if(!gas.value().is_zero())result.add(multiply(gas.value(),logarithmic_ratio(liquid ? after.gas_volume_m3 : v1,
        liquid ? before.gas_volume_m3 : v0)));
    return result.value();
}
}
