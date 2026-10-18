#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double dt=0x1p-10;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void scaled(MacEnergy value){std::cout<<' '<<value.mantissa()<<' '<<value.exponent();}
MacClosedSettings settings(){
    MacClosedSettings result;result.maximum_timestep_s=dt;
    result.compression_viscosity=MacCompressionViscosity::coupled_pressure;
    result.shock_heat=MacShockHeatRegularization::none;
    result.reconstruction=MacMaterialReconstruction::donor_cell;
    result.momentum_reconstruction=MacMomentumReconstruction::donor_cell;
    return result;
}
MacTransportState source(int material,double mass,int axis,MacScaled velocity,bool divergent,bool zero_energy=false) {
    const auto& set=ma::testing::setup();auto state=set.state(2,2,true);
    for(auto& cell:state.cells){
        if(material==0){
            std::vector<double> amounts(set.registry.size());amounts[set.registry.index("nitrogen")]=mass;
            amounts[set.registry.index("helium")]=mass*1e-28;
            cell.mixture=set.eos->initialize_at_temperature(amounts,300,1).mixture;
        }else cell.mixture=set.mixture(1,300000,300,material==1?.2:1);
        if(zero_energy)cell.mixture.energy=0;
        cell.passive_quantities={{17,cell.mixture.mass()},{UINT64_C(0x80000000),cell.mixture.mass()*1e-30}};
    }
    const auto inertia=mac_face_inertias(set.registry,state,*set.eos);
    const auto& m=axis==0?inertia.x:inertia.y;std::vector<MacScaled> j(m.size());
    for(std::size_t f=0;f<j.size();++f){
        const auto coordinate=axis==0?f%state.grid.x_width():f/state.grid.width;
        j[f]=velocity.multiplied(m[f]);if(divergent && coordinate%2)j[f]=j[f].negated();
    }
    store_mac_field(axis==0?state.momentum_x:state.momentum_y,axis==0?state.full_momentum_x:state.full_momentum_y,j);
    return state;
}
void run(int id,int material,double mass,int axis,MacScaled velocity,bool divergent) {
    const auto& set=ma::testing::setup();const auto input=source(material,mass,axis,velocity,divergent);
    const auto held=ma::testing::fingerprint(input);const auto options=settings();
    const auto step=advance_mac_closed_substep(set.registry,*set.eos,input,dt,options);
    check(step.compressive_responses.size()==input.cells.size(),"Range compression omitted its constitutive responses");
    const MacClosedWorld world(set.registry,set.eos,input,options);const auto checkpoint=world.checkpoint();
    const auto result=world.advanced(dt,8);const auto restored=MacClosedWorld::from_checkpoint(set.registry,set.eos,checkpoint);
    check(result.world.checkpoint()==restored.advanced(dt,8).world.checkpoint(),"Coupled range source restore changed continuation");
    const auto endpoint=MacClosedWorld::from_checkpoint(set.registry,set.eos,result.world.checkpoint());
    check(result.world.advanced(dt,8).world.checkpoint()==endpoint.advanced(dt,8).world.checkpoint(),"Coupled range endpoint restore changed continuation");
    check(result.report.substeps==1 && result.report.rejected_attempts==0 && world.checkpoint()==checkpoint && ma::testing::fingerprint(input)==held,
        "Coupled range changed its nominal clock or immutable source");
    if(id<6)check(step.rounded_pressure_velocity_inputs>0,"Range fixture missed incoming velocity projection");
    if(id!=7)check(!(axis==0?step.mass_fluxes_kg.full_x:step.mass_fluxes_kg.full_y).empty(),"Range fixture lost its full bulk mass flux");
    if(divergent)check(step.unresolved_compressive_work_updates>0 && result.report.unresolved_compressive_work_updates>0,
        "Actual full compressive work projection was not reported");
    for(const auto& c:step.state.cells)check(c.passive_quantities.back().quantity>0 &&
        (material==2 || c.mixture.masses[set.registry.index("helium")]>0),"Coupled range dropped a positive trace");
    if(!divergent)check(ma::testing::fingerprint(step.state)==held,"Uniform coupled range transport changed primary inventory or J");
    ma::testing::export_case(id,input,step,dt);
    std::cout<<"CRANGE "<<id<<' '<<axis<<' '<<divergent<<' '<<step.rounded_pressure_velocity_inputs<<' '
        <<step.unresolved_compressive_work_updates<<' '<<result.report.unresolved_compressive_work_updates<<' '
        <<static_cast<int>(options.compression_viscosity)<<' '<<step.compressive_responses.size();
    scaled(step.full_compressive_work_residual_j);scaled(step.full_compressive_work_bound_j);std::cout<<'\n';
    for(std::size_t i=0;i<input.cells.size();++i){
        const auto at=static_cast<std::size_t>(std::find(step.active_cells.begin(),step.active_cells.end(),i)-step.active_cells.begin());
        check(step.pressure.volume_changes_m3[at]==0,"Microscopic range control left its zero root-volume parameter");
        const auto& response=step.compressive_responses[at];check(response.stress_pa.is_zero() && response.dissipated_heat_j.is_zero(),
            "Zero root-volume parameter introduced compression heat");
        std::cout<<"CRANGECELL "<<id<<' '<<i<<' '<<step.pressure.work_pressures_pa[at];
        scaled(step.full_compressive_volume_changes_m3[i]);scaled(step.full_compressive_internal_work_j[i]);std::cout<<'\n';
    }
}
void active_pressure(){
    const auto& set=ma::testing::setup();auto input=source(0,1,0,{},false);
    input.cells[0].mixture=set.eos->initialize_at_temperature(input.cells[0].mixture.masses,300.3,1).mixture;
    auto full=mac_field_values(input.momentum_x,input.full_momentum_x);full[0]=MacScaled::from_parts(.75,-1200);
    store_mac_field(input.momentum_x,input.full_momentum_x,full);
    const auto held=ma::testing::fingerprint(input);const auto step=advance_mac_closed_substep(set.registry,*set.eos,input,dt,settings());
    std::size_t heated=0;for(const auto& response:step.compressive_responses)if(response.dissipated_heat_j.mantissa()>0)++heated;
    const MacClosedWorld world(set.registry,set.eos,input,settings());const auto checkpoint=world.checkpoint();
    const auto result=world.advanced(dt,8);
    check(step.rounded_pressure_velocity_inputs==1 && heated>0 && result.report.substeps==1 && result.report.rejected_attempts==0 &&
        ma::testing::fingerprint(input)==held && world.checkpoint()==checkpoint,"Active coupled stress with a full incoming velocity failed its nominal path or mutated its source");
    check(result.world.checkpoint()==MacClosedWorld::from_checkpoint(set.registry,set.eos,checkpoint).advanced(dt,8).world.checkpoint(),
        "Active coupled range source restore changed continuation");
    std::cout<<"CRANGEACTIVE "<<step.rounded_pressure_velocity_inputs<<' '<<heated<<' '<<result.report.substeps<<' '<<result.report.rejected_attempts<<'\n';
}
void rejection(){
    const auto& set=ma::testing::setup();const auto input=source(0,1,0,MacScaled::from_parts(.75,-1200),true,true);
    const auto held=ma::testing::fingerprint(input);bool rejected=false;
    try{(void)advance_mac_closed_substep(set.registry,*set.eos,input,dt,settings());}
    catch(const std::domain_error& e){rejected=std::string_view(e.what())=="Coupled pressure actual full cell work exceeds its operand bound";}
    check(rejected && ma::testing::fingerprint(input)==held,"Unstorable actual full work at zero U did not reject atomically");
    const MacClosedWorld world(set.registry,set.eos,input,settings());const auto checkpoint=world.checkpoint();rejected=false;
    try{(void)world.advanced(dt,1);}catch(const std::runtime_error&){rejected=true;}
    check(rejected && world.checkpoint()==checkpoint,"Rejected full-work retry published a partial owner");
}
}
int main(){try{
    require_mac_energy_environment();std::cout<<std::setprecision(17);const auto& set=ma::testing::setup();
    for(const auto& m:set.eos->reference().materials())std::cout<<"MATERIAL "<<set.registry.index(m.id)<<' '<<static_cast<int>(m.phase)<<' '
        <<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<' '<<m.energy_offset_j_per_kg<<' '<<m.zero_pressure_density_kg_per_m3<<' '<<m.reference_bulk_modulus_pa<<'\n';
    run(0,0,1,0,MacScaled::from_parts(.75,-1200),false);
    run(1,0,0x1p-600,1,MacScaled::from_parts(-.75,-1200),false);
    run(2,0,1,0,MacScaled::from_parts(.70000000000000007,-1040),false);
    run(3,0,1,0,MacScaled::from_parts(.75,-1200),true);
    run(4,1,1,1,MacScaled::from_parts(.75,-1200),true);
    run(5,2,1,0,MacScaled::from_parts(-.75,-1200),true);
    run(6,0,0x1p-600,0,MacScaled::from_parts(.75,-900),false);
    run(7,0,1,1,MacScaled::from_double(0x1p-10),false);
    active_pressure();
    std::cout<<"PASS full incoming velocity and midpoint sweeps in coupled gas, wet and liquid pressure\n";
    std::cout<<"PASS signed actual material work, explicit unresolved updates and positive traces\n";
    std::cout<<"PASS exact source and endpoint checkpoint continuation\n";
    rejection();std::cout<<"PASS actual unrepresentable work and shared-budget rollback reject atomically\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
