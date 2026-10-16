#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_thermal.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>
#include <iostream>

namespace {
using namespace ma; using namespace ma::experimental;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
MacTransportState pair(const ma::testing::Setup& setup,bool water=false) {
    auto state=setup.state(2,1,false);
    for (std::size_t i=0;i<2;++i) {
        std::vector<double> masses(setup.registry.size());
        masses[setup.registry.index(water ? "water" : "oxygen")]=water ? 1100 : static_cast<double>(i+1);
        if (!water) masses[setup.registry.index("helium")]=1e-28;
        state.cells[i].mixture=setup.eos->initialize_at_temperature(masses,i ? 320 : 280,1).mixture;
    }
    return state;
}
}
int main() {
    try {
        const auto& setup=ma::testing::setup(); auto source=pair(setup);
        source.momentum_x[source.grid.x_face(1,0)]=.1;
        const auto held=ma::testing::fingerprint(source);
        const auto ca=setup.eos->evaluate(source.cells[0].mixture,1).intrinsic_heat_capacity_j_per_k;
        const auto cb=setup.eos->evaluate(source.cells[1].mixture,1).intrinsic_heat_capacity_j_per_k;
        const auto conductivity=setup.registry[setup.registry.index("oxygen")].conductivity;
        const double dt=50,q=dt*conductivity*40/(1+dt*conductivity*(1/ca+1/cb));
        const auto result=advance_mac_conduction(setup.registry,*setup.eos,source,dt);
        const auto ta=setup.eos->evaluate(result.state.cells[0].mixture,1).temperature_k;
        const auto tb=setup.eos->evaluate(result.state.cells[1].mixture,1).temperature_k;
        require(std::abs(ta-(280+q/ca))<1e-9 && std::abs(tb-(320-q/cb))<1e-9,"Two-node conductive solve differs from independent backward-Euler formula");
        require(ta>280 && tb<320 && result.report.entropy_change_j_per_k>0 && result.report.contacts==1 &&
            result.report.redistributed_energy.mantissa()>0,"Conduction has no physical heat/entropy progress");
        double previous_error=0;
        const auto duration=10000.0,mean=(ca*280+cb*320)/(ca+cb);
        const auto contrast=40*std::exp(-conductivity*(1/ca+1/cb)*duration);
        const auto exact_cold=mean-cb/(ca+cb)*contrast;
        for (const auto count : {4,8,16}) {
            auto state=source;
            for (int i=0;i<count;++i) state=advance_mac_conduction(setup.registry,*setup.eos,state,duration/count).state;
            const auto error=std::abs(setup.eos->evaluate(state.cells[0].mixture,1).temperature_k-exact_cold);
            require(error>0 && (previous_error==0 || error<.6*previous_error),"Conductive time refinement does not approach the independent exponential solution");
            previous_error=error;
        }
        require(result.state.momentum_x==source.momentum_x && result.state.momentum_y==source.momentum_y &&
            ma::testing::fingerprint(source)==held,"Conduction changed canonical momentum or mutated source");
        for (std::size_t i=0;i<2;++i) {
            require(result.state.cells[i].mixture.masses==source.cells[i].mixture.masses,"Conduction changed constituent/trace mass");
            const auto& before=source.cells[i].passive_quantities; const auto& after=result.state.cells[i].passive_quantities;
            require(before.size()==after.size(),"Conduction changed passive coverage");
            for (std::size_t j=0;j<before.size();++j) require(before[j].identity==after[j].identity && before[j].quantity==after[j].quantity,
                "Conduction changed passive metadata");
        }
        auto insulated=source; insulated.grid.walls[1]=1;
        insulated.cells[1]={MixtureInventory(setup.registry.size()),{}};
        std::fill(insulated.momentum_x.begin(),insulated.momentum_x.end(),0);
        auto no_flux=advance_mac_conduction(setup.registry,*setup.eos,insulated,dt);
        require(no_flux.report.contacts==0 && no_flux.state.cells[0].mixture.energy==insulated.cells[0].mixture.energy,"Heat crossed a closed wall");
        auto water=pair(setup,true); MacClosedSettings off; off.maximum_timestep_s=.01;
        auto on=off; on.thermal_conduction=true;
        const auto control=MacClosedWorld(setup.registry,setup.eos,water,off).advanced(.1).world;
        const MacClosedWorld initial_conducting(setup.registry,setup.eos,water,on);
        const auto conducting=initial_conducting.advanced(.1);
        const auto cold=setup.eos->evaluate(conducting.world.state().cells[0].mixture,1).temperature_k;
        const auto warm=setup.eos->evaluate(conducting.world.state().cells[1].mixture,1).temperature_k;
        require(cold>280 && warm<320 && std::abs(setup.eos->evaluate(control.state().cells[0].mixture,1).temperature_k-280)<1e-8 &&
            conducting.report.conduction_stages==2*conducting.report.substeps && conducting.report.conduction_contacts>0 &&
            conducting.report.conductive_redistribution_j.mantissa()>0,"Owning interval omitted conductive half stages or control changed");
        require(conducting.world.time_s()==initial_conducting.time_s()+.1 && conducting.world.revision()==initial_conducting.revision()+1,
            "Conduction advanced an independent clock/revision");
        bool rejected=false; try { (void)advance_mac_conduction(setup.registry,*setup.eos,source,-1); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && ma::testing::fingerprint(source)==held,"Rejected conductive step mutated the source");
        std::cout<<"PASS conductive pair formula, immutable traces/momentum, insulation and integrated liquid heat\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
