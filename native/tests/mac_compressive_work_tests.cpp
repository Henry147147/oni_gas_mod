#include "mac_compressive_fixture.hpp"
#include "../src/mac_material_entropy_detail.hpp"
#include <iomanip>
#include <utility>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void near(MacEnergy actual,MacEnergy expected,const char* why) {
    MacEnergyAccount a;a.add(actual);a.add(expected.negated());check(a.within_bound(),why);
}
void definitions(int profile,const FluidThermodynamics& model) {
    ma::testing::export_compressive_definitions(profile,model);
}
MacCompressiveWork record(int id,int profile,const FluidThermodynamics& model,const MixtureInventory& source,
    double v,double dv,double h,double dt,MacEnergy linear) {
    const auto held=source;
    const auto before=model.evaluate(source,v);
    ReferenceRootOptions options;options.relative_pressure_tolerance=std::numeric_limits<double>::epsilon()/4;
    const auto r=evaluate_mac_compressive_work(model,source,v,dv,h,dt,linear,options);
    check(source.masses==held.masses && source.energy==held.energy,"Compressive work mutated source");
    check(r.mixture.masses==source.masses,"Compressive work changed constituent inventory");
    check(r.stress_pa.mantissa()>=0 && r.dissipated_heat_j.mantissa()>=0,"Negative compressive stress or dissipation");
    check(r.state.temperature_k>0 && r.work_pressure_volume_derivative_pa_per_m3.mantissa()<0,"Invalid compressive endpoint or tangent");
    if(dv>=0) check(r.stress_pa.is_zero() && r.dissipated_heat_j.is_zero(),"Expansion produced compressive heat");
    else check(!r.stress_pa.is_zero() && !r.dissipated_heat_j.is_zero(),"Positive compression lost its stress or heat");
    ma::testing::export_compressive_response(id,profile,source,before,v,dv,h,dt,linear,r);return r;
}
void ordinary() {
    const auto& set=ma::testing::setup();const auto source=set.mixture(1,100000,300);
    const auto a=record(0,0,*set.eos,source,1,-.125,1,.01,MacEnergy::from_double(50));
    near(a.stress_pa,MacEnergy::from_double(source.mass()*(50*100*.125+10000*.125*.125)),"Dry gas stress differs from declared law");
    near(a.dissipated_heat_j,a.stress_pa.multiplied(.125),"Dry gas heat is not the stress's work");
    check(a.state.temperature_k>a.isentropic.state.temperature_k,"Compression did not heat above the same isentrope");
    record(1,0,*set.eos,source,1,.125,1,.01,MacEnergy::from_double(50));
    const auto zero=record(2,0,*set.eos,source,1,0,1,.01,MacEnergy::from_double(50));
    check(zero.mixture.energy==source.energy && zero.state.entropy_j_per_k==set.eos->evaluate(source,1).entropy_j_per_k,
        "Zero change modified actual material state");
}
void liquids() {
    const auto& set=ma::testing::setup();
    record(3,0,*set.eos,set.mixture(1,300000,300,.2),1,-.0125,1,.01,MacEnergy::from_double(25));
    record(4,0,*set.eos,set.mixture(1,300000,300,1),1,-.000125,1,.01,MacEnergy::from_double(25));
    record(5,0,*set.eos,set.mixture(1,300000,300,.2),1,.0125,1,.01,MacEnergy::from_double(25));
}
FluidThermodynamics shifted_reference(double shift) {
    const auto& set=ma::testing::setup();auto defs=set.eos->reference().materials();SpeciesRegistry shifted;
    for(std::size_t i=0;i<set.registry.size();++i) {
        auto species=set.registry[i];species.formation_enthalpy-=shift*species.molar_mass;shifted.add(std::move(species));
    }
    for(auto& d:defs) d.energy_offset_j_per_kg=shifted[shifted.index(d.id)].internal_energy(0);
    return FluidThermodynamics(shifted,"compressive-negative-reference-v1",std::move(defs));
}
void negative_reference() {
    const auto& set=ma::testing::setup();const auto model=shifted_reference(1e4);definitions(1,model);
    const auto original=set.mixture(1,100000,300);const auto source=model.initialize_at_temperature(original.masses,300,1).mixture;
    check(source.energy<0,"Negative reference control does not have negative U");
    const auto r=record(6,1,model,source,1,-.125,1,.01,MacEnergy::from_double(50));
    check(r.mixture.energy<0 && r.state.temperature_k>300,"Reference energy was floored or thermal work was lost");
}
void tangent() {
    const auto& set=ma::testing::setup();const auto source=set.mixture(1,100000,300);double last=-1;
    for(int i=0;i<5;++i) {
        const double dv=-static_cast<double>(i)/32;
        const auto r=record(7+i,0,*set.eos,source,1,dv,1,.01,MacEnergy::from_double(50));
        const double stress=r.stress_pa.project().value;check(stress>last,"Compressive stress is not monotone");last=stress;
        if(i>0) {
            const double step=1e-6;
            const auto left=evaluate_mac_compressive_work(*set.eos,source,1,dv-step,1,.01,MacEnergy::from_double(50));
            const auto right=evaluate_mac_compressive_work(*set.eos,source,1,dv+step,1,.01,MacEnergy::from_double(50));
            const double fd=(right.total_work_pressure_pa.project().value-left.total_work_pressure_pa.project().value)/(2*step);
            const double derivative=r.work_pressure_volume_derivative_pa_per_m3.project().value;
            check(std::abs(fd-derivative)<1e-6*std::abs(derivative),"Compressive work tangent disagrees with an independent finite difference");
        }
    }
}
void scaled_geometry() {
    const auto& set=ma::testing::setup();int id=12;
    for(const double h:{1e-12,1.0,1e12}) {
        const double v=h*h*h;const auto source=set.mixture(v,100000,300);
        const auto r=record(id++,0,*set.eos,source,v,-.125*v,h,.01*h,MacEnergy::from_double(50));
        near(r.stress_pa,MacEnergy::from_double((source.mass()/v)*(50*100*.125+10000*.125*.125)),"Compressive stress changed under equivalent geometric scaling");
    }
}
void subrange() {
    const auto& set=ma::testing::setup();const auto source=set.mixture(1,100000,300);
    const auto r=record(15,0,*set.eos,source,1,-1e-200,1,.01,MacEnergy::from_double(50));
    check(!r.dissipated_heat_j.is_zero() && r.dissipated_heat_j.project().rounded && r.dissipated_heat_j.project().value==0 &&
        r.unresolved_energy_update && !r.isentropic.endpoint_volume_resolves_change && r.mixture.energy==source.energy,
        "Subrange compression heat or unresolved canonical update was discarded");
}
void entropy_reference_precision() {
    // Exact input retained from the failed separation64 Newton trial. Its
    // actual U crosses the caloric reference while thermal energy stays large.
    const auto& set=ma::testing::setup();MixtureInventory source(set.registry.size());
    source.masses[set.registry.index("nitrogen")]=0.00016762971735930963;
    source.masses[set.registry.index("helium")]=1.6762971735930964e-32;
    source.energy=0.2881186565689019;
    constexpr double volume=0.000244140625;
    const auto r=record(16,0,*set.eos,source,volume,6.2390787779808469e-06,.015625,2.0884559497703817e-06,
        MacEnergy::from_parts(.84580507210591793,MacExponent(3)));
    MacEnergySum admitted;admitted.add(r.entropy_change_j_per_k);admitted.add(r.entropy_allowance_j_per_k);
    check(admitted.sign()>=0 && r.mixture.energy<0,"Actual reference-crossing state lost the original entropy allowance");
    const auto initial=set.eos->evaluate(source,volume);
    for(int probe=0;probe<2;++probe) {
        auto endpoint=source;if(probe)endpoint.energy-=.001;
        const auto end=set.eos->evaluate(endpoint,volume);
        const auto ds=entropy_detail::change(*set.eos,source,volume,initial,endpoint,volume,end);
        MacEnergySum allowance;allowance.add(MacEnergy::from_double(std::abs(source.energy)).shared(1,initial.temperature_k).shifted(-45));
        allowance.add(MacEnergy::from_double(std::abs(endpoint.energy)).shared(1,end.temperature_k).shifted(-45));
        MacEnergySum lower;lower.add(ds);lower.add(allowance.value());
        check(probe ? lower.sign()<0 : ds.is_zero(),"Relative entropy hides real cooling or changes an identical state");
        std::cout<<"CWENTROPY "<<probe<<" 16 "<<endpoint.energy<<' '<<volume<<' '<<end.mechanical_pressure_pa<<' '<<
            end.temperature_k<<' '<<end.entropy_j_per_k<<' '<<ds.mantissa()<<' '<<ds.exponent()<<' '<<
            allowance.value().mantissa()<<' '<<allowance.value().exponent()<<'\n';
    }
}
void invalid() {
    const auto& set=ma::testing::setup();const auto source=set.mixture(1,100000,300);const auto held=source;int rejected=0;
    const auto rejects=[&](auto call) {
        bool bad=false;try { call(); } catch(const std::exception&) {bad=true;}
        check(bad && source.masses==held.masses && source.energy==held.energy,"Invalid compressive input was accepted or changed source");++rejected;
    };
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,source,0,0,1,.01,{}));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,source,1,std::numeric_limits<double>::quiet_NaN(),1,.01,{}));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,source,1,0,0,.01,{}));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,source,1,0,1,0,{}));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,source,1,0,1,.01,MacEnergy::from_double(-1)));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,source,1,-1,1,.01,{}));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,MixtureInventory(set.registry.size()),1,0,1,.01,{}));});
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(*set.eos,set.mixture(1,300000,300,1),1,.1,1,.01,{}));});
    auto defs=set.eos->reference().materials();for(auto& d:defs)d.maximum_temperature_k=300.1;
    const FluidThermodynamics limited(set.registry,"compressive-late-temperature-rejection-v1",std::move(defs));
    const auto liquid=limited.initialize_at_temperature(set.mixture(1,300000,300,1).masses,300,1).mixture;
    // Its isentropic zero-expansion liquid endpoint stays at 300 K; only the
    // subsequent positive stress work leaves this declared model's domain.
    const auto anchor=limited.reference().make_isentrope(limited.to_reference(liquid,1));
    check(limited.reference().isentropic_response(anchor,-.000125).state.temperature_k<300.1,"Late-rejection control already has an invalid isentrope");
    rejects([&]{static_cast<void>(evaluate_mac_compressive_work(limited,liquid,1,-.000125,1,1e-8,{}));});
    // Retain the previous failed -1e8 J/kg fixture as an explicit rejection:
    // it cannot meet the existing per-species sensible-energy precision rule.
    rejects([&]{const auto coarse=shifted_reference(1e8);const auto bad=coarse.initialize_at_temperature(source.masses,300,1).mixture;
        static_cast<void>(evaluate_mac_compressive_work(coarse,bad,1,-.125,1,.01,{}));});
    check(rejected==10,"Compressive rejection coverage differs");std::cout<<"CWREJECTED "<<rejected<<'\n';
}
}
int main() {try {
    require_mac_energy_environment();std::cout<<std::setprecision(17);definitions(0,*ma::testing::setup().eos);
    ordinary();std::cout<<"PASS dry gas compression expansion and zero work\n";
    liquids();std::cout<<"PASS wet and compressed liquid actual stored work\n";
    negative_reference();std::cout<<"PASS negative formation reference remains physical\n";
    tangent();std::cout<<"PASS monotone stress and complete work tangent\n";
    scaled_geometry();std::cout<<"PASS equivalent geometry retains intensive stress\n";
    subrange();std::cout<<"PASS subrange positive heat and unresolved U remain explicit\n";
    entropy_reference_precision();std::cout<<"PASS actual relative entropy across zero reference U and genuine cooling\n";
    invalid();std::cout<<"PASS input void and late EOS rejection leave source unchanged\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
