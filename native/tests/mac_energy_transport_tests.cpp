#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
MacMassFluxes sweeps(const MacTransportState& state,double x,double y) {
    const auto& g=state.grid;return {std::vector<double>(g.x_width()*g.height,x),std::vector<double>(g.width*g.y_height(),y)};
}
void export_materials() {
    const auto& set=ma::testing::setup();
    for(const auto& m:set.eos->reference().materials()) std::cout<<"MATERIAL "<<set.registry.index(m.id)<<' '<<
        (m.phase==ReferencePhase::ideal_gas ? 0 : 1)<<' '<<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<' '<<m.energy_offset_j_per_kg<<' '<<
        m.zero_pressure_density_kg_per_m3<<' '<<m.reference_bulk_modulus_pa<<'\n';
}
void tiny_kinetic() {
    const auto& set=ma::testing::setup();auto source=set.state(3,2,true);
    for(std::size_t i=0;i<source.momentum_x.size();++i)source.momentum_x[i]=1e-200*(1+.2*std::sin(static_cast<double>(i)));
    for(std::size_t i=0;i<source.momentum_y.size();++i)source.momentum_y[i]=-1e-200*(1+.1*std::cos(static_cast<double>(i)));
    const auto held=ma::testing::fingerprint(source);const std::vector<double> volumes(source.cells.size(),1);
    const auto plan=reconstruct_mac_parcels(set.registry,*set.eos,source,volumes,sweeps(source,.1,.05));
    const auto result=advance_mac_parcel_transport(set.registry,source,*set.eos,plan);
    check(ma::testing::fingerprint(source)==held,"Wide transport changed source");
    check(result.report.rounded_energy_projections>0&&result.report.unresolved_heat_updates==0&&ma::testing::has_primary_energy_remainder(result.state),"Tiny kinetic/heat projections or primary remainders were hidden");
    check(!result.report.mixing_heat_wide_j.is_zero()&&result.report.mixing_heat_j==0,"Tiny mixing heat was not retained separately");
    for(const auto* report:{&result.report.x,&result.report.y})for(const auto& e:report->energies)
        check(!e.old_kinetic.is_zero()&&!e.new_kinetic.is_zero(),"Positive derived kinetic state was erased");
    ma::testing::export_transport(0,source,result,plan);
}
void tiny_parcel() {
    const auto& set=ma::testing::setup();auto source=set.state(2,2,true);
    const auto ni=set.registry.index("nitrogen"),he=set.registry.index("helium");constexpr double mass=3.0517578125e-5;
    for(auto& c:source.cells){std::vector<double> m(set.registry.size());m[ni]=mass;m[he]=mass*1e-28;
        c.mixture=set.eos->initialize_at_temperature(m,300,1).mixture;c.passive_quantities={{17,c.mixture.mass()},{UINT64_C(0x80000000),c.mixture.mass()*1e-30}};}
    source.momentum_x[0]=2.1665e-147;const auto held=ma::testing::fingerprint(source);
    const auto plan=reconstruct_mac_parcels(set.registry,*set.eos,source,std::vector<double>(4,1),sweeps(source,3.6197301863874292e-151/mass,0));
    const auto result=advance_mac_parcel_transport(set.registry,source,*set.eos,plan);
    check(ma::testing::fingerprint(source)==held&&result.state.momentum_x[1]>0,"Tiny representable momentum was lost or source changed");
    check(result.report.x.energies[1].transported_kinetic.mantissa()>0&&result.report.x.transported_kinetic[1]==0,"Original derived parcel range failure remains");
    check(!result.report.energy_residual_wide_j.is_zero()&&result.report.energy_residual_j==0,"Below-range actual energy residual was hidden");
    check(result.report.unresolved_heat_updates==0&&ma::testing::has_primary_energy_remainder(result.state),"Below-range U update was not retained");
    ma::testing::export_transport(1,source,result,plan);
}
void tiny_closed_reversal() {
    const auto& set=ma::testing::setup();auto source=set.state(2,1,false);const auto ni=set.registry.index("nitrogen"),he=set.registry.index("helium");
    for(std::size_t i=0;i<2;++i){std::vector<double> m(set.registry.size());m[ni]=(i==0 ? 1 : 2)*1e-200;m[he]=m[ni]*1e-28;
        auto& c=source.cells[i];c.mixture=set.eos->initialize_at_temperature(m,300,1).mixture;
        c.passive_quantities={{17,c.mixture.mass()},{UINT64_C(0x80000000),c.mixture.mass()*1e-30}};}
    constexpr double dt=1e-104;const double pl=set.eos->evaluate(source.cells[0].mixture,1).mechanical_pressure_pa;
    const double pr=set.eos->evaluate(source.cells[1].mixture,1).mechanical_pressure_pa;
    source.momentum_x[1]=-.5*(dt*(pl-pr));const auto held=ma::testing::fingerprint(source);
    MacClosedSettings settings;settings.maximum_timestep_s=dt;settings.compression_viscosity=MacCompressionViscosity::none;
    settings.shock_heat=MacShockHeatRegularization::none; // Primitive pressure/transport reference.
    const MacClosedWorld original(set.registry,set.eos,source,settings);
    const auto substep=advance_mac_closed_substep(set.registry,*set.eos,source,dt,settings);
    check(std::all_of(substep.volume_fluxes_m3.x.begin(),substep.volume_fluxes_m3.x.end(),[](double q){return q==0;}),"Fine pressure reversal moved volume");
    check(substep.state.momentum_x[1]==-source.momentum_x[1]&&substep.rounded_energy_projections>0,"Tiny canonical pressure reversal or energy report failed");
    const auto result=original.advanced(dt,4);
    check(original.time_s()==0&&original.revision()==1&&result.world.time_s()==dt&&result.world.revision()==2&&
        ma::testing::fingerprint(original.state())==held&&ma::testing::fingerprint(source)==held&&
        ma::testing::fingerprint(result.world.state())==ma::testing::fingerprint(substep.state),"Wide-energy owner failed clock/revision/source identity");
    check(result.report.substeps==1&&result.report.rejected_attempts==0&&result.report.rounded_energy_projections>0,"Tiny owner retried or hid projections");
    ma::testing::export_case(2,source,substep,dt);
    std::cout<<"EINTERVAL 2 "<<result.report.substeps<<' '<<result.report.rejected_attempts<<' '<<result.world.time_s()<<' '<<result.world.revision()<<' '<<
        result.report.rounded_energy_projections<<' '<<result.report.energy_residual_wide_j.mantissa()<<' '<<result.report.energy_residual_wide_j.exponent()<<' '<<
        result.report.energy_bound_wide_j.mantissa()<<' '<<result.report.energy_bound_wide_j.exponent()<<'\n';
}
void primary_rejection() {
    const auto& set=ma::testing::setup();auto source=set.state(2,2,true);
    source.cells[0].passive_quantities[1].quantity=std::numeric_limits<double>::denorm_min();const auto held=ma::testing::fingerprint(source);
    bool caught=false;try{static_cast<void>(reconstruct_mac_parcels(set.registry,*set.eos,source,std::vector<double>(4,1),sweeps(source,.125,0)));}
    catch(const FluidFatalError& e){caught=e.code()==FluidFatalCode::numerical_precision;}
    check(caught&&ma::testing::fingerprint(source)==held,"Derived-energy repair changed primary positive-transfer rejection");
}
}
int main(){try{
    require_mac_energy_environment();
    std::cout<<std::setprecision(17);export_materials();
    tiny_kinetic();std::cout<<"PASS tiny derived K and heat retain primary momentum\n";
    tiny_parcel();std::cout<<"PASS derived parcel and residual below binary64 range\n";
    tiny_closed_reversal();std::cout<<"PASS warm thin-gas pressure reversal and atomic owner\n";
    primary_rejection();std::cout<<"PASS unrepresentable positive primary passive still rejects atomically\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
