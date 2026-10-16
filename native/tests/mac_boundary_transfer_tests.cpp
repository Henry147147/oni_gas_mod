#include "mac_fixture.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace ma;
using namespace ma::experimental;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
MacEnergySum total(const MacClosedWorld& world) {
    MacEnergySum value; const auto& state=world.state();
    const auto inertia=mac_face_inertias(world.registry(),state,world.thermodynamics());
    for (const auto& cell : state.cells) value.add(cell.mixture.energy);
    const auto x=mac_field_values(state.momentum_x,state.full_momentum_x),y=mac_field_values(state.momentum_y,state.full_momentum_y);
    for (std::size_t i=0;i<x.size();++i) value.add(MacEnergy::kinetic(inertia.x[i],x[i]));
    for (std::size_t i=0;i<y.size();++i) value.add(MacEnergy::kinetic(inertia.y[i],y[i]));
    return value;
}
void account(const MacClosedWorld& before,const MacGasTransferResult& after) {
    MacEnergyAccount balance;
    const auto old=total(before),next=total(after.world);
    for (std::size_t i=0;i<old.size();++i) balance.add(old.component(i));
    for (const auto& packet : after.parcels) {
        balance.add(packet.injected ? packet.internal_energy_j : -packet.internal_energy_j);
        if (!packet.injected) balance.add(packet.resolved_kinetic_energy.negated());
    }
    for (std::size_t i=0;i<next.size();++i) balance.add(next.component(i).negated());
    require(balance.within_bound(),"Independent boundary U+K account failed");
}
}
int main() {
    try {
        const auto& fixture=ma::testing::setup(); auto state=fixture.state(2,2,false);
        for (auto& cell : state.cells) cell.passive_quantities.clear();
        state.momentum_x[state.grid.x_face(1,0)]=.25; state.momentum_x[state.grid.x_face(1,1)]=-.15;
        state.momentum_y[state.grid.y_face(0,1)]=.1; state.momentum_y[state.grid.y_face(1,1)]=-.2;
        auto source=MacClosedWorld(fixture.registry,fixture.eos,state).advanced(.0001).world;
        const auto held=ma::testing::fingerprint(source.state()); const auto oxygen=fixture.registry.index("oxygen"),helium=fixture.registry.index("helium");
        const MacGasTransfer injection{0,oxygen,true,.05,300};
        auto injected=source.with_gas_transfers(std::span(&injection,1)); account(source,injected);
        require(injected.world.time_s()==source.time_s() && injected.world.revision()==source.revision()+1,"Boundary exchange reset physical time/revision");
        require(injected.world.state().momentum_x==source.state().momentum_x && injected.world.state().momentum_y==source.state().momentum_y,
            "At-rest injection added canonical momentum");
        require(injected.parcels.size()==1 && injected.parcels[0].mass_kg==.05 && injected.parcels[0].resolved_kinetic_energy.is_zero(),"Injection parcel differs");
        require(injected.world.state().cells[0].mixture.masses[helium]==source.state().cells[0].mixture.masses[helium],"Injection lost trace gas");
        const MacGasTransfer removal{0,oxygen,false,.02,0};
        auto removed=injected.world.with_gas_transfers(std::span(&removal,1)); account(injected.world,removed);
        const auto& parcel=removed.parcels[0];
        require(parcel.mass_kg==.02 && parcel.resolved_kinetic_energy.mantissa()>0 && !parcel.momentum_x.is_zero() && !parcel.momentum_y.is_zero(),
            "Extraction discarded carried mechanics");
        require(removed.world.state().cells[0].mixture.masses[helium]==source.state().cells[0].mixture.masses[helium],"Selective removal touched traces");
        const auto& before=injected.world.state(); const auto& after=removed.world.state();
        const auto old_inertia=mac_face_inertias(fixture.registry,before,*fixture.eos),new_inertia=mac_face_inertias(fixture.registry,after,*fixture.eos);
        const auto face=state.grid.x_face(1,0);
        const auto expected=before.momentum_x[face]*new_inertia.x[face]/old_inertia.x[face];
        require(std::abs(after.momentum_x[face]-expected)<=16*std::numeric_limits<double>::epsilon()*std::abs(expected),"Extraction changed the remaining face velocity");
        const MacGasTransfer trace{0,helium,true,1e-29,300};
        auto traced=removed.world.with_gas_transfers(std::span(&trace,1)); account(removed.world,traced);
        require(traced.world.state().cells[0].mixture.masses[helium]>after.cells[0].mixture.masses[helium],"Trace injection disappeared");
        auto invalid=injection; invalid.cell_index=100;
        const std::array<MacGasTransfer,2> batch{injection,invalid}; bool rejected=false;
        try { (void)source.with_gas_transfers(batch); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && ma::testing::fingerprint(source.state())==held,"Invalid later transfer partially changed source");
        std::cout << "PASS immutable gas injection, carried extraction mechanics, trace preservation, accounts and batch rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
