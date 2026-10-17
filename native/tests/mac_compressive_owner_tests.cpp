#include "mac_compressive_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;using namespace ma::experimental;
constexpr double dt=1e-5;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool fixed(const MacGrid& g,int axis,std::size_t f){
    const auto x=axis==0 ? f%g.x_width() : f%g.width,y=axis==0 ? f/g.x_width() : f/g.width;
    if(axis==0)return (!g.periodic_x && (x==0 || x==g.width)) ||
        g.walls[(x+g.width-1)%g.width+y*g.width] || g.walls[x%g.width+y*g.width];
    return (!g.periodic_y && (y==0 || y==g.height)) ||
        g.walls[x+((y+g.height-1)%g.height)*g.width] || g.walls[x+(y%g.height)*g.width];
}
MacClosedSettings settings(){
    MacClosedSettings s;s.maximum_timestep_s=dt;s.compression_viscosity=MacCompressionViscosity::coupled_pressure;
    s.shock_heat=MacShockHeatRegularization::none;s.reconstruction=MacMaterialReconstruction::swept_first_moment;
    s.momentum_reconstruction=MacMomentumReconstruction::swept_physical;return s;
}
MacTransportState source(bool wet=false,bool water=false,bool uniform=false){
    const auto& set=ma::testing::setup();auto s=set.state(wet ? 4U : 3U,wet ? 3U : 2U,!wet,wet,wet);
    for(std::size_t i=0;i<s.cells.size();++i)if(!s.grid.walls[i]){
        const double p=(water ? 300000 : 100000)*(uniform ? 1 : 1+.03*std::sin(static_cast<double>(i)));
        s.cells[i].mixture=set.mixture(1,p,uniform ? 300 : 300+std::cos(static_cast<double>(i)),water ? 1 : wet ? .2 : 0);
    }
    const auto inertia=mac_face_inertias(set.registry,s,*set.eos);
    for(int axis=0;axis<2;++axis){
        auto& j=axis==0 ? s.momentum_x : s.momentum_y;const auto& m=axis==0 ? inertia.x : inertia.y;
        for(std::size_t f=0;f<j.size();++f)if(!fixed(s.grid,axis,f))
            j[f]=m[f]*(water ? .001 : (axis==0 ? 1 : -.5)+(uniform ? 0 : .125*std::sin(static_cast<double>(f)+.2)));
    }
    return s;
}
int response_count=0;
void scene(int id,const MacTransportState& s,MacClosedSettings options,bool uniform=false){
    const auto& set=ma::testing::setup();const auto held=ma::testing::fingerprint(s);
    const auto r=advance_mac_closed_substep(set.registry,*set.eos,s,dt,options);
    check(ma::testing::fingerprint(s)==held && r.compressive_coefficients.has_value() &&
        r.compressive_responses.size()==r.active_cells.size(),"Coupled owner omitted accepted responses or mutated its source");
    const auto i0=mac_face_inertias(set.registry,s,*set.eos),i1=mac_face_inertias(set.registry,r.state,*set.eos);
    MacScaledSum tx,ty;
    for(const auto& x:r.transport.x.full_wall_impulse)tx.add(x);
    for(const auto& y:r.transport.y.full_wall_impulse)ty.add(y);
    std::cout<<"COCASE "<<id<<' '<<s.grid.width<<' '<<s.grid.height<<' '<<s.grid.periodic_x<<' '<<s.grid.periodic_y<<' '<<
        dt<<' '<<s.grid.spacing_m<<' '<<s.grid.depth_m<<' '<<options.gravity_m_per_s2<<' '<<r.pressure_wall_impulse_x<<' '<<
        r.pressure_wall_impulse_y<<' '<<r.body_impulse_y<<' '<<tx.value().require_representable()<<' '<<ty.value().require_representable()<<' '<<
        r.pressure_energy_bound_j<<' '<<r.energy_bound_j<<'\n';
    std::size_t positive=0;
    for(std::size_t i=0;i<s.cells.size();++i){
        std::cout<<"COWALL "<<id<<' '<<i<<' '<<static_cast<int>(s.grid.walls[i])<<'\n';
        for(int endpoint=0;endpoint<3;++endpoint){
            const auto& state=endpoint==0 ? s : endpoint==1 ? r.pressure_state : r.state;const auto& c=state.cells[i];
            const double v=endpoint==1 && !s.grid.walls[i] ? r.material_volumes_m3[i] : s.grid.volume_m3();
            ReferenceEquilibriumState reading;if(!s.grid.walls[i])reading=set.eos->evaluate(c.mixture,v);
            std::cout<<"COINV "<<id<<' '<<endpoint<<' '<<i<<' '<<v<<' '<<c.mixture.energy<<' '<<reading.mechanical_pressure_pa<<' '<<
                reading.temperature_k<<' '<<reading.entropy_j_per_k<<'\n';
            for(std::size_t species=0;species<c.mixture.masses.size();++species)if(c.mixture.masses[species]!=0)
                std::cout<<"COMASS "<<id<<' '<<endpoint<<' '<<i<<' '<<species<<' '<<c.mixture.masses[species]<<'\n';
            for(const auto& p:c.passive_quantities)std::cout<<"COPASSIVE "<<id<<' '<<endpoint<<' '<<i<<' '<<p.identity<<' '<<p.quantity<<'\n';
        }
    }
    for(std::size_t at=0;at<r.active_cells.size();++at){
        const auto cell=r.active_cells[at];const auto& response=r.compressive_responses[at];const int identifier=response_count++;
        check(response.mixture.energy==r.pressure_state.cells[cell].mixture.energy && response.mixture.masses==s.cells[cell].mixture.masses &&
            response.state.mechanical_pressure_pa==r.pressure.endpoint_pressures_pa[at],"Coupled owner published a stress as physical pressure or omitted heat");
        if(!response.dissipated_heat_j.is_zero())++positive;
        if(uniform)check(r.pressure.volume_changes_m3[at]==0 && response.dissipated_heat_j.is_zero(),"Uniform translation generated compressive heat");
        std::cout<<"COWORK "<<id<<' '<<cell<<' '<<identifier<<'\n';
        ma::testing::export_compressive_response(identifier,0,s.cells[cell].mixture,set.eos->evaluate(s.cells[cell].mixture,s.grid.volume_m3()),
            s.grid.volume_m3(),r.pressure.volume_changes_m3[at],s.grid.spacing_m,dt,r.compressive_linear_speeds[at],response);
    }
    check(uniform || positive>0,"Owner fixture did not exercise positive coupled heat");
    for(int axis=0;axis<2;++axis){
        const auto j0=axis==0 ? mac_field_values(s.momentum_x,s.full_momentum_x) : mac_field_values(s.momentum_y,s.full_momentum_y);
        const auto jp=axis==0 ? mac_field_values(r.pressure_state.momentum_x,r.pressure_state.full_momentum_x) : mac_field_values(r.pressure_state.momentum_y,r.pressure_state.full_momentum_y);
        const auto j1=axis==0 ? mac_field_values(r.state.momentum_x,r.state.full_momentum_x) : mac_field_values(r.state.momentum_y,r.state.full_momentum_y);
        const auto& m0=axis==0 ? i0.x : i0.y;const auto& m1=axis==0 ? i1.x : i1.y;
        const auto& sweep=axis==0 ? r.volume_fluxes_m3.x : r.volume_fluxes_m3.y;
        const auto& reactions=axis==0 ? r.transport.x.full_wall_impulse : r.transport.y.full_wall_impulse;
        for(std::size_t f=0;f<j0.size();++f)std::cout<<"COFACE "<<id<<' '<<axis<<' '<<f<<' '<<m0[f]<<' '<<m1[f]<<' '<<
            j0[f].require_representable()<<' '<<jp[f].require_representable()<<' '<<j1[f].require_representable()<<' '<<sweep[f]<<' '<<
            reactions[f].require_representable()<<'\n';
    }
}
void owner_clock(){
    const auto& set=ma::testing::setup();const auto s=source();const auto options=settings();const MacClosedWorld owner(set.registry,set.eos,s,options);
    const auto held=owner.checkpoint();auto manual=s;MacEnergySum heat;std::size_t events=0,unresolved=0;double time=0;const double endpoint=3*dt;
    while(time<endpoint){const double step_dt=std::min(dt,endpoint-time);auto step=advance_mac_closed_substep(set.registry,*set.eos,manual,step_dt,options);
        for(const auto& r:step.compressive_responses){heat.add(r.dissipated_heat_j);if(!r.dissipated_heat_j.is_zero())++events;if(r.unresolved_energy_update)++unresolved;}
        manual=std::move(step.state);time+=step_dt;}
    const auto actual=owner.advanced(endpoint);
    check(ma::testing::fingerprint(actual.world.state())==ma::testing::fingerprint(manual) && actual.report.substeps==3 &&
        actual.world.time_s()==endpoint && actual.world.revision()==owner.revision()+1 && owner.checkpoint()==held &&
        actual.report.compression_events==events && actual.report.unresolved_compression_heat_updates==unresolved &&
        actual.report.compression_heat_wide_j.mantissa()==heat.value().mantissa() && actual.report.compression_heat_wide_j.exponent()==heat.value().exponent(),
        "Owning compression differs from the unsplit pressure stages or double-counted heat");
    std::cout<<"COCLOCK "<<actual.report.substeps<<' '<<events<<' '<<unresolved<<'\n';
}
void checkpoints(){
    const auto& set=ma::testing::setup();int count=0;
    for(const bool wet:{false,true})for(int material=0;material<3;++material)for(int heat=0;heat<4;++heat){
        auto options=settings();options.reconstruction=static_cast<MacMaterialReconstruction>(material);
        options.shock_heat=static_cast<MacShockHeatRegularization>(heat);options.thermal_conduction=!wet;
        const auto initial=MacClosedWorld(set.registry,set.eos,source(wet),options).advanced(2*dt).world;const auto image=initial.checkpoint();
        const auto restored=MacClosedWorld::from_checkpoint(set.registry,set.eos,image);
        const auto expected=initial.advanced(2*dt),resumed=restored.advanced(2*dt);
        check(restored.settings().compression_viscosity==MacCompressionViscosity::coupled_pressure && restored.checkpoint()==image &&
            expected.world.checkpoint()==resumed.world.checkpoint() && expected.report.compression_events==resumed.report.compression_events &&
            expected.report.compression_heat_wide_j.mantissa()==resumed.report.compression_heat_wide_j.mantissa() &&
            expected.report.compression_heat_wide_j.exponent()==resumed.report.compression_heat_wide_j.exponent(),"Coupled checkpoint changed settings or exact continuation");
        std::cout<<"COCHECKPOINT "<<static_cast<int>(wet)<<' '<<material<<' '<<heat<<'\n';++count;
    }
    check(count==24,"Missing coupled checkpoint setting combination");
}
void rejection(){
    const auto& set=ma::testing::setup();auto s=source();auto options=settings();int count=0;
    const auto reject=[&](auto call){bool failed=false;try{call();}catch(const std::exception&){failed=true;}check(failed,"Unsupported coupled owner request accepted");++count;};
    options.compression_viscosity=static_cast<MacCompressionViscosity>(4);
    reject([&]{static_cast<void>(MacClosedWorld(set.registry,set.eos,s,options));});options=settings();
    auto full=mac_field_values(s.momentum_x,s.full_momentum_x);full[s.grid.x_face(1,0)]=MacScaled::from_parts(.5,MacExponent(-2000));
    store_mac_field(s.momentum_x,s.full_momentum_x,full);const MacClosedWorld tiny(set.registry,set.eos,s,options);const auto tiny_held=tiny.checkpoint();
    reject([&]{static_cast<void>(tiny.advanced(dt));});check(tiny.checkpoint()==tiny_held,"Rejected range projection changed canonical source");
    const MacClosedWorld owner(set.registry,set.eos,source(),options);const auto held=owner.checkpoint();
    reject([&]{static_cast<void>(owner.advanced(3*dt,1));});check(owner.checkpoint()==held && owner.time_s()==0,"Late interval failure published a partial candidate");
    std::cout<<"COREJECTED "<<count<<'\n';
}
}
int main(){try{
    require_mac_energy_environment();std::cout<<std::setprecision(17);ma::testing::export_compressive_definitions(0,*ma::testing::setup().eos);
    check(MacClosedSettings{}.compression_viscosity==MacCompressionViscosity::acoustic_quadratic,"Default compression mode changed");
    scene(0,source(),settings());std::cout<<"PASS coupled dry two-dimensional pressure and remap\n";
    auto gravity=settings();gravity.gravity_m_per_s2=9.81;scene(1,source(true),gravity);std::cout<<"PASS coupled wet gravity walls and traces\n";
    scene(2,source(false,true),settings());std::cout<<"PASS coupled filled water pressure and remap\n";
    scene(3,source(false,false,true),settings(),true);std::cout<<"PASS uniform translation has no coupled heat\n";
    owner_clock();std::cout<<"PASS unsplit owner clock and compression heat diagnostics\n";
    checkpoints();std::cout<<"PASS coupled checkpoint exact continuation across all 24 new settings\n";
    rejection();std::cout<<"PASS coupled range early and late atomic rejection\n";
    check(response_count==29,"Unexpected completed owner response count");return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
