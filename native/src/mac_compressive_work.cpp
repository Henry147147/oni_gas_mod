#include "mixed_atmospheres/mac_compressive_work.hpp"
#include <cmath>
#include <limits>

namespace ma::experimental {
namespace {
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
MacEnergy product(MacEnergy a, MacEnergy b) {
    return (b.mantissa()<0 ? a.negated() : a).shared(b.magnitude(),MacEnergy::from_double(1));
}
MacEnergy sum(MacEnergy a, MacEnergy b) { MacEnergySum s; s.add(a); s.add(b); return s.value(); }
bool above(MacEnergy a, MacEnergy b) { MacEnergySum s; s.add(a); s.add(b.negated()); return s.sign()>0; }
}
MacCompressiveWork evaluate_mac_compressive_work(const FluidThermodynamics& model,
    const MixtureInventory& source, double volume, double delta, double spacing, double dt,
    MacEnergy linear, ReferenceRootOptions options) {
    require_mac_energy_environment();
    if (!std::isfinite(volume) || volume<=0 || !std::isfinite(delta) || !std::isfinite(spacing) || spacing<=0 ||
        !std::isfinite(dt) || dt<=0 || linear.mantissa()<0)
        throw std::invalid_argument("Invalid compressive pressure-work inputs");
    model.validate_inventory(source);
    const double mass=source.mass();
    if (!(mass>0) || !std::isfinite(mass)) throw std::invalid_argument("Compressive pressure work requires occupied material");
    const auto anchor=model.reference().make_isentrope(model.to_reference(source,volume));
    MacCompressiveWork result;
    result.isentropic=model.reference().isentropic_response(anchor,delta,options);
    const auto& iso=result.isentropic;
    if (iso.state.vacuum_volume_m3>32*std::numeric_limits<double>::epsilon()*iso.endpoint.volume_m3)
        throw FluidFatalError(FluidFatalCode::unsupported_topology,"Compressive pressure work cannot create a liquid void");

    const auto density=MacEnergy::from_double(mass).shared(1,volume);
    const auto speed=MacEnergy::from_double(spacing).shared(1,dt);
    const auto a=product(product(density,linear),speed);
    const auto b=product(product(density,speed),speed);
    if (delta<0) {
        const auto s=MacEnergy::from_double(-delta).shared(1,volume);
        result.stress_pa=sum(product(a,s),product(product(b,s),s));
        result.stress_volume_derivative_pa_per_m3=sum(a,product(b,s).multiplied(2)).shared(1,volume).negated();
        result.dissipated_heat_j=result.stress_pa.multiplied(-delta);
    } else if (delta==0) result.stress_volume_derivative_pa_per_m3=a.shared(1,volume).negated();
    result.total_work_pressure_pa=sum(MacEnergy::from_double(iso.work_pressure_pa),result.stress_pa);
    const double derivative=delta==0 || (!iso.state.has_gas && iso.work_pressure_pa==0) ?
        -.5/model.compression_compliance(anchor) : model.reference().isentropic_work_pressure_derivative(anchor,delta,options);
    result.work_pressure_volume_derivative_pa_per_m3=sum(MacEnergy::from_double(derivative),result.stress_volume_derivative_pa_per_m3);
    if (result.work_pressure_volume_derivative_pa_per_m3.mantissa()>=0)
        precision("Compressive pressure work has no strictly negative tangent");
    result.requested_energy_change_j=sum(MacEnergy::from_double(iso.internal_energy_change_j),result.dissipated_heat_j);
    MacEnergySum updated; updated.add(source.energy); updated.add(iso.internal_energy_change_j); updated.add(result.dissipated_heat_j);
    result.mixture=source; result.mixture.energy=updated.value().project().value;
    result.state=model.evaluate(result.mixture,iso.endpoint.volume_m3);

    MacEnergySum represented; represented.add(result.mixture.energy); represented.add(-source.energy);
    represented.add(result.requested_energy_change_j.negated());
    result.unresolved_energy_update=above(represented.value().magnitude(),result.requested_energy_change_j.magnitude().shifted(-45));
    MacEnergyAccount energy;
    energy.add(result.mixture.energy); energy.add(-source.energy);
    energy.add(result.total_work_pressure_pa.multiplied(delta));
    if (!energy.within_bound()) precision("Compressive actual stored U and pressure work exceed the original operand bound");
    result.energy_residual_j=energy.residual(); result.energy_bound_j=energy.bound();
    MacEnergySum entropy,allowance;
    entropy.add(result.state.entropy_j_per_k); entropy.add(-anchor.reference_state().entropy_j_per_k);
    allowance.add(MacEnergy::from_double(std::abs(source.energy)).shared(1,anchor.reference_state().temperature_k).shifted(-45));
    allowance.add(MacEnergy::from_double(std::abs(result.mixture.energy)).shared(1,result.state.temperature_k).shifted(-45));
    result.entropy_change_j_per_k=entropy.value(); result.entropy_allowance_j_per_k=allowance.value();
    if (above(result.entropy_change_j_per_k.negated(),result.entropy_allowance_j_per_k))
        precision("Compressive actual stored-state entropy falls below the original stored-U allowance");
    return result;
}
}
