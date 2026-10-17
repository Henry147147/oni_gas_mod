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
}
int main() {
    try { std::cout<<std::setprecision(17); identity(false,false); identity(false,true);
        identity(true,false); identity(true,true); wakes(); return 0; }
    catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
