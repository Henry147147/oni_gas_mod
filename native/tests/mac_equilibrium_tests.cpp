#include "mac_fixture.hpp"
#include <iomanip>
#include <iostream>

namespace {
using namespace ma;
using namespace ma::experimental;
using namespace ma::testing;
void check(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void identity(bool wet,bool walls) {
    const auto& set=setup(); auto state=set.state(4,3,false,false,walls);
    for (std::size_t i=0;i<state.cells.size();++i) if (!state.grid.walls[i])
        state.cells[i].mixture=set.mixture(1,wet?2.2e8:100000,300,wet?1:0);
    MacClosedSettings settings; settings.thermal_conduction=true;
    settings.viscosity.shear_pa_s=.01; settings.viscosity.bulk_pa_s=.01;
    const MacClosedWorld source(set.registry,set.eos,state,settings);
    const auto held=source.checkpoint(); const auto reference=source.advanced(.02);
    const auto fast=source.advanced(.02,1,MacExecutionPolicy::exact_equilibrium);
    check(fast.report.exact_equilibrium_intervals==1 && fast.report.pressure_iterations==0 &&
        fast.report.conduction_stages==0,"Uniform region did not skip actual stages");
    check(fast.world.checkpoint()==reference.world.checkpoint(),"Equilibrium differs from full reference execution");
    check(source.checkpoint()==held,"Equilibrium mutated its source");
    const auto restored=MacClosedWorld::from_checkpoint(set.registry,set.eos,fast.world.checkpoint());
    check(restored.advanced(.02,1,MacExecutionPolicy::exact_equilibrium).world.checkpoint()==
        fast.world.advanced(.02,1,MacExecutionPolicy::exact_equilibrium).world.checkpoint(),"Equilibrium reload differs");
    const MacCellHeat heat{0,.125,1000000};
    const auto changed=source.with_cell_heat(std::span(&heat,1));
    const auto awake=changed.advanced(.00001,1024,MacExecutionPolicy::exact_equilibrium);
    check(awake.report.exact_equilibrium_intervals==0 && awake.report.conduction_stages>0 &&
        (wet || awake.report.pressure_iterations>0),"Heat failed to wake solver");
    check(awake.world.checkpoint()==changed.advanced(.00001).world.checkpoint(),"Awakened heat differs from reference");
    std::cout<<"PASS equilibrium "<<wet<<' '<<walls<<'\n';
}
void wakes() {
    const auto& set=setup(); const auto original=set.state(3,2,true);
    for (int mode=0;mode<5;++mode) {
        auto state=original; MacClosedSettings settings;
        if (mode==0) state.cells[0].mixture.energy=std::nextafter(state.cells[0].mixture.energy,INFINITY);
        if (mode==1) { auto& mass=state.cells[0].mixture.masses[set.registry.index("helium")]; mass=std::nextafter(mass,INFINITY); }
        if (mode==2) state.momentum_x[1]=1e-40;
        if (mode==3) {
            state.full_momentum_x.resize(state.momentum_x.size());
            state.full_momentum_x[1]=MacScaled::from_parts(.5,-2000);
        }
        if (mode==4) { settings.gravity_m_per_s2=1; state.grid.periodic_y=false;
            state.momentum_y.assign(state.grid.width*state.grid.y_height(),0); }
        const MacClosedWorld owner(set.registry,set.eos,state,settings);
        // Tiny changes can explicitly reject in the reference arithmetic. The
        // fast execution must take the same path, including the same failure.
        bool fast_rejected=false,reference_rejected=false; std::vector<std::uint8_t> fast_bytes,reference_bytes;
        try { const auto fast=owner.advanced(.00001,1024,MacExecutionPolicy::exact_equilibrium);
            check(fast.report.exact_equilibrium_intervals==0,"Non-equilibrium was skipped"); fast_bytes=fast.world.checkpoint(); }
        catch(const std::exception& error) { if (std::string(error.what())=="Non-equilibrium was skipped") throw; fast_rejected=true; }
        try { reference_bytes=owner.advanced(.00001).world.checkpoint(); } catch(const std::exception&) { reference_rejected=true; }
        check(fast_rejected==reference_rejected && fast_bytes==reference_bytes,"Wake path differs from reference");
        std::cout<<"PASS wake "<<mode<<' '<<fast_rejected<<'\n';
    }
    const MacClosedWorld owner(set.registry,set.eos,original);
    for (int control=0;control<3;++control) {
        bool rejected=false;
        try { static_cast<void>(owner.advanced(control==0?-1:.1,control==1?0:1,
            control==2?static_cast<MacExecutionPolicy>(55):MacExecutionPolicy::exact_equilibrium)); }
        catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"Equilibrium bypassed input admission");
    }
    std::cout<<"PASS equilibrium admission\n";
}
void disconnected_regions() {
    const auto& set=setup();auto state=set.state(5,2,false);
    for(std::size_t i=0;i<state.cells.size();++i) {
        const auto x=i%5;
        if(x==2) {state.grid.walls[i]=1;state.cells[i]={MixtureInventory(set.registry.size()),{}};}
        else state.cells[i].mixture=set.mixture(1,x<2 ? 100000 : 2.2e8,x<2 ? 300 : 350,x<2 ? 0 : 1);
    }
    MacClosedSettings settings;settings.thermal_conduction=true;
    const MacClosedWorld source(set.registry,set.eos,state,settings);
    const auto fast=source.advanced(.02,1,MacExecutionPolicy::exact_equilibrium);
    check(fast.report.exact_equilibrium_intervals==1 && fast.world.checkpoint()==source.advanced(.02).world.checkpoint(),
        "Disconnected gas and liquid equilibria were not certified independently");
    state.grid.walls[2]=0;state.cells[2].mixture=set.mixture(1,100000,300);
    const MacClosedWorld opened(set.registry,set.eos,state,settings);
    const auto awake=opened.advanced(1e-6,1024,MacExecutionPolicy::exact_equilibrium);
    check(awake.report.exact_equilibrium_intervals==0 && awake.world.checkpoint()==opened.advanced(1e-6).world.checkpoint(),
        "Opening a boundary failed to invalidate disconnected equilibrium");
    std::cout<<"PASS disconnected equilibrium and changed-boundary wake\n";
}
void exact_hydrostatic() {
    const auto& set=setup();auto state=set.state(2,2,false);
    state.cells[0].mixture=state.cells[1].mixture=set.mixture(1,100000,300);
    double gravity=0;
    // Choose a representable exact discrete force balance; ordinary columns
    // with even a one-ULP residual must continue through the full timestep.
    for(int trial=1;trial<=64 && gravity==0;++trial) {
        state.cells[2].mixture=state.cells[3].mixture=set.mixture(1,100000-trial*16,300);
        const double difference=set.eos->evaluate(state.cells[0].mixture,1).mechanical_pressure_pa-
            set.eos->evaluate(state.cells[2].mixture,1).mechanical_pressure_pa;
        const double inertia=.5*state.cells[0].mixture.mass()+.5*state.cells[2].mixture.mass();
        const double candidate=difference/inertia;
        if(inertia*candidate==difference) gravity=candidate;
    }
    check(gravity>0,"Hydrostatic fixture did not find an exact represented force balance");
    MacClosedSettings settings;settings.gravity_m_per_s2=gravity;
    // Disable conduction: temperature readings can differ by one ULP even
    // though both inventories were initialized at the same requested T.
    const MacClosedWorld source(set.registry,set.eos,state,settings);
    const auto fast=source.advanced(.02,1,MacExecutionPolicy::exact_equilibrium);
    check(fast.report.exact_equilibrium_intervals==1 && fast.world.checkpoint()==source.advanced(.02).world.checkpoint(),
        "Exact hydrostatic certificate changed the reference continuation");
    check(fast.report.body_impulse_y<0 && fast.report.full_wall_impulse_y.require_representable()==-fast.report.body_impulse_y,
        "Hydrostatic skipping omitted the body-force/wall ledger");
    settings.gravity_m_per_s2=std::nextafter(gravity,INFINITY);
    // A quotient/product tie may mask the first ULP. Resolve a nonzero force.
    const double inertia=.5*state.cells[0].mixture.mass()+.5*state.cells[2].mixture.mass();
    while(inertia*settings.gravity_m_per_s2==inertia*gravity)
        settings.gravity_m_per_s2=std::nextafter(settings.gravity_m_per_s2,INFINITY);
    const MacClosedWorld changed(set.registry,set.eos,state,settings);
    const auto awake=changed.advanced(.00001,1024,MacExecutionPolicy::exact_equilibrium);
    check(awake.report.exact_equilibrium_intervals==0 && awake.world.checkpoint()==changed.advanced(.00001).world.checkpoint(),
        "A nonzero hydrostatic force residual was silently skipped");
    std::cout<<"PASS exact hydrostatic equilibrium, gravity ledger and nonzero-force wake\n";
}
void rounded_force_wakes() {
    const auto& set=setup();auto state=set.state(2,2,false);
    std::vector<double> masses(set.registry.size());masses[set.registry.index("nitrogen")]=1.25e-300;
    for(auto& cell:state.cells) {
        cell.mixture=set.eos->initialize_at_temperature(masses,300,1).mixture;
        cell.passive_quantities.clear();
    }
    const double lower=set.eos->evaluate(state.cells[0].mixture,1).mechanical_pressure_pa;
    const double inertia=state.cells[0].mixture.mass();double gravity=0;
    for(int trial=1;trial<=64 && gravity==0;++trial) {
        state.cells[2].mixture=state.cells[3].mixture=set.eos->initialize_at_temperature(masses,300-trial*0x1p-42,1).mixture;
        const double difference=lower-set.eos->evaluate(state.cells[2].mixture,1).mechanical_pressure_pa;
        const double candidate=difference/inertia;
        if(candidate<=0 || inertia*candidate!=difference) continue;
        const auto body=MacScaled::from_double(inertia).multiplied(candidate);
        MacScaledSum force;force.add(difference);force.add(body.negated());
        if(body.project().rounded && force.sign()!=0) gravity=candidate;
    }
    check(gravity>0,"Rounded-force fixture did not find a false binary64 hydrostatic balance");
    MacClosedSettings settings;settings.gravity_m_per_s2=gravity;
    settings.compression_viscosity=MacCompressionViscosity::none;settings.shock_heat=MacShockHeatRegularization::none;
    const MacClosedWorld source(set.registry,set.eos,state,settings);
    const auto awake=source.advanced(1e-4,1,MacExecutionPolicy::exact_equilibrium);
    check(awake.report.exact_equilibrium_intervals==0 && awake.world.checkpoint()==source.advanced(1e-4,1).world.checkpoint(),
        "Projected force cancellation silently certified a nonzero full-range residual");
    const auto momentum=mac_field_values(awake.world.state().momentum_y,awake.world.state().full_momentum_y);
    check(!momentum[state.grid.y_face(0,1)].is_zero(),"Full-range hydrostatic residual failed to advance momentum");
    std::cout<<"PASS projected subnormal force cancellation wakes the full solver\n";
}
void full_gravity_ledger() {
    const auto& set=setup();const auto state=set.state(2,1,false);
    MacClosedSettings settings;settings.gravity_m_per_s2=std::numeric_limits<double>::denorm_min();
    const MacClosedWorld source(set.registry,set.eos,state,settings);
    const auto fast=source.advanced(.02,1,MacExecutionPolicy::exact_equilibrium);
    const auto reference=source.advanced(.02);
    check(fast.report.exact_equilibrium_intervals==1 && fast.world.checkpoint()==reference.world.checkpoint(),
        "Rigid one-row equilibrium did not retain reference continuation");
    check(fast.report.body_impulse_y==0 && fast.report.full_body_impulse_y.mantissa()<0 &&
        fast.report.full_body_impulse_y.project().rounded && fast.report.rounded_gravity_projections==1,
        "Certified equilibrium lost its below-range body impulse");
    MacScaledSum reaction;reaction.add(fast.report.full_body_impulse_y);reaction.add(fast.report.full_wall_impulse_y);
    MacScaledAccount agreement;agreement.add(fast.report.full_body_impulse_y);agreement.add(reference.report.full_body_impulse_y.negated());
    check(reaction.sign()==0 && agreement.within_bound(),"Certified equilibrium changed the full gravity/wall ledger");
    std::cout<<"PASS certified equilibrium retains below-range body and wall impulse\n";
}
}
int main() {
    try { std::cout<<std::setprecision(17); identity(false,false); identity(false,true);
        identity(true,false); identity(true,true); wakes(); disconnected_regions(); exact_hydrostatic(); rounded_force_wakes(); full_gravity_ledger(); return 0; }
    catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
