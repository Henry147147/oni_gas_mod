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
    for (std::size_t i=0;i<state.cells.size();++i) {
        value.add(state.cells[i].mixture.energy);
        value.add(MacEnergy::from_double(state.cells[i].mixture.mass()).multiplied(world.settings().gravity_m_per_s2)
            .multiplied((static_cast<double>(i/state.grid.width)+.5)*state.grid.spacing_m));
    }
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
        balance.add(packet.injected ? packet.potential_energy : packet.potential_energy.negated());
        if (!packet.injected) balance.add(packet.resolved_kinetic_energy.negated());
    }
    for (std::size_t i=0;i<next.size();++i) balance.add(next.component(i).negated());
    require(balance.within_bound(),"Independent boundary U+K account failed");
}
void liquid_controls(const ma::testing::Setup& fixture) {
    auto state=fixture.state(2,2,false);
    const auto water=fixture.registry.index("water"),oxygen=fixture.registry.index("oxygen");
    for (auto& cell : state.cells) {
        cell.passive_quantities.clear(); cell.mixture=fixture.mixture(1,2.2e8,300,1);
    }
    state.momentum_x[state.grid.x_face(1,0)]=30; state.momentum_x[state.grid.x_face(1,1)]=-20;
    state.momentum_y[state.grid.y_face(0,1)]=10; state.momentum_y[state.grid.y_face(1,1)]=-15;
    MacClosedSettings settings; settings.gravity_m_per_s2=9.81;
    const MacClosedWorld source(fixture.registry,fixture.eos,state,settings);
    const auto held=source.checkpoint();
    const MacFluidTransfer injection{0,water,true,4,310};
    const auto injected=source.with_fluid_transfers(std::span(&injection,1)); account(source,injected);
    // Independent analytic U of a parcel at the declared pre-edit pressure.
    // e_elastic = K/rho0 * (log(rho/rho0) + rho0/rho - 1).
    const double elastic=2.2e9/1000*(std::log(1.1)+1/1.1-1);
    const double expected=4*(fixture.registry[water].internal_energy(310)+elastic);
    require(std::abs(injected.parcels[0].internal_energy_j-expected)<=16*std::numeric_limits<double>::epsilon()*std::abs(expected),
        "Liquid injection omitted or recomputed the declared parcel compression energy");
    require(injected.world.state().momentum_x==state.momentum_x && injected.world.state().momentum_y==state.momentum_y,
        "At-rest liquid injection added canonical momentum");
    require(injected.world.state().cells[0].mixture.masses[water]==1104 && injected.world.time_s()==source.time_s(),
        "Liquid injection changed the wrong mass or advanced time");
    const auto& before=injected.world.state();
    const auto old_u=before.cells[0].mixture.energy;
    const MacFluidTransfer removal{0,water,false,2,0};
    const auto removed=injected.world.with_fluid_transfers(std::span(&removal,1)); account(injected.world,removed);
    const auto& parcel=removed.parcels[0];
    require(std::abs(parcel.internal_energy_j-2*old_u/1104)<=16*std::numeric_limits<double>::epsilon()*std::abs(parcel.internal_energy_j),
        "Liquid extraction did not carry local specific internal energy");
    require(parcel.resolved_kinetic_energy.mantissa()>0 && !parcel.momentum_x.is_zero() && !parcel.momentum_y.is_zero() &&
        parcel.potential_energy.require_representable()==2*9.81*.5,"Liquid extraction discarded carried mechanics or potential");
    const auto old_i=mac_face_inertias(fixture.registry,before,*fixture.eos);
    const auto next_i=mac_face_inertias(fixture.registry,removed.world.state(),*fixture.eos);
    const auto face=state.grid.x_face(1,0);
    const double expected_j=before.momentum_x[face]*next_i.x[face]/old_i.x[face];
    require(std::abs(removed.world.state().momentum_x[face]-expected_j)<=16*std::numeric_limits<double>::epsilon()*std::abs(expected_j),
        "Liquid extraction changed the remaining face velocity");
    require(removed.world.revision()==source.revision()+2 && source.checkpoint()==held,"Liquid transfers changed the immutable source");
    const auto restored=MacClosedWorld::from_checkpoint(fixture.registry,fixture.eos,removed.world.checkpoint());
    require(restored.advanced(.0001).world.checkpoint()==removed.world.advanced(.0001).world.checkpoint(),
        "Liquid transfer checkpoint lost an authoritative field");
    for (const auto& invalid : {MacFluidTransfer{0,water,false,200,0},MacFluidTransfer{0,water,false,1100,0},
            MacFluidTransfer{0,oxygen,true,.1,300}}) {
        const std::array batch{injection,invalid}; bool rejected=false;
        try { (void)source.with_fluid_transfers(batch); } catch (const std::exception&) { rejected=true; }
        require(rejected && source.checkpoint()==held,"Later invalid liquid transfer escaped atomic topology rejection");
    }
    bool legacy_rejected=false;
    try { (void)source.with_gas_transfers(std::span(&injection,1)); } catch (const std::invalid_argument&) { legacy_rejected=true; }
    require(legacy_rejected,"Gas-only interface silently admitted liquid transfers");
    auto gas=fixture.state(2,2,false); for (auto& cell : gas.cells) cell.passive_quantities.clear();
    const MacClosedWorld gas_world(fixture.registry,fixture.eos,gas);
    bool mixed_rejected=false;
    try { (void)gas_world.with_fluid_transfers(std::span(&injection,1)); } catch (const std::invalid_argument&) { mixed_rejected=true; }
    require(mixed_rejected,"Liquid injection silently created a gas/liquid topology");
}
void batch_controls(const ma::testing::Setup& fixture) {
    auto state=fixture.state(4,2,false);
    auto x=mac_field_values(state.momentum_x,state.full_momentum_x);
    auto y=mac_field_values(state.momentum_y,state.full_momentum_y);
    x[state.grid.x_face(1,0)]=MacScaled::from_parts(.75,MacExponent::from_decimal("-4096"));
    x[state.grid.x_face(2,0)]=MacScaled::from_double(.25);
    y[state.grid.y_face(1,1)]=MacScaled::from_double(-.125);
    store_mac_field(state.momentum_x,state.full_momentum_x,x);
    store_mac_field(state.momentum_y,state.full_momentum_y,y);
    const MacClosedWorld source(fixture.registry,fixture.eos,state);
    const auto saved=source.checkpoint(); const auto oxygen=fixture.registry.index("oxygen");
    const std::array edits{MacFluidTransfer{0,oxygen,true,.01,300},MacFluidTransfer{1,oxygen,false,.002,0},
        MacFluidTransfer{0,oxygen,false,.003,0},MacFluidTransfer{1,oxygen,true,.004,301}};
    const auto batched=source.with_fluid_transfers(edits); account(source,batched);
    auto serial=source;
    const auto same=[](const MacScaled& a,const MacScaled& b) {
        return a.mantissa()==b.mantissa() && a.exponent()==b.exponent();
    };
    for (std::size_t n=0;n<edits.size();++n) {
        const auto next=serial.with_fluid_transfers(std::span(&edits[n],1));
        const auto& a=batched.parcels[n]; const auto& b=next.parcels.front();
        require(a.mass_kg==b.mass_kg && a.internal_energy_j==b.internal_energy_j &&
            same(a.resolved_kinetic_energy,b.resolved_kinetic_energy) && same(a.momentum_x,b.momentum_x) && same(a.momentum_y,b.momentum_y),
            "Fluid batch changed parcel mechanics or edit ordering");
        serial=next.world;
    }
    require(ma::testing::fingerprint(serial.state())==ma::testing::fingerprint(batched.world.state()) &&
        !batched.world.state().full_momentum_x.empty() && source.checkpoint()==saved,
        "Fluid batch lost full-range momentum or changed an overlapping edit");
    const auto cold=MacClosedWorld::from_checkpoint(fixture.registry,fixture.eos,saved).with_fluid_transfers(edits);
    require(cold.world.checkpoint()==batched.world.checkpoint(),"Fluid batch changed cold continuation");
    const MacFluidTransfer zero{0,oxygen,false,0,0};
    const auto unchanged=source.with_fluid_transfers(std::span(&zero,1));
    require(ma::testing::fingerprint(unchanged.world.state())==ma::testing::fingerprint(source.state()) &&
        unchanged.world.revision()==source.revision()+1,"Zero-mass batch changed a canonical field");
    const auto passive=source.with_passive_edits({});
    require(&passive.state()==&source.state() && passive.revision()==source.revision()+1 &&
        passive.time_s()==source.time_s() && source.checkpoint()==saved,
        "Empty passive edit lost immutable sharing or its revision");
    auto bad=edits; bad.back().cell_index=state.cells.size(); bool refused=false;
    try { (void)source.with_fluid_transfers(bad); } catch (const std::invalid_argument&) { refused=true; }
    require(refused && source.checkpoint()==saved,"Invalid fluid batch escaped rollback");
}
void unchanged_faces(const ma::testing::Setup& fixture) {
    auto state=fixture.state(4,2,false);
    state.grid.closed_x.assign(state.momentum_x.size(),0); state.grid.closed_y.assign(state.momentum_y.size(),0);
    const auto face=state.grid.x_face(1,0); state.momentum_x[face]=.25;
    auto full=mac_field_values(state.momentum_x,state.full_momentum_x);
    full[state.grid.x_face(2,0)]=MacScaled::from_parts(.75,MacExponent(-4096));
    store_mac_field(state.momentum_x,state.full_momentum_x,full);
    const MacClosedWorld source(fixture.registry,fixture.eos,state); const auto image=source.checkpoint();
    const auto next=source.with_closed_faces(state.grid.closed_x,state.grid.closed_y);
    require(&next.state()==&source.state() && next.revision()==source.revision()+1 && next.time_s()==source.time_s() &&
        ma::testing::fingerprint(next.state())==ma::testing::fingerprint(source.state()) && source.checkpoint()==image,
        "Unchanged closed faces lost sharing, full-range momentum, revision or immutable input");
    const auto cold=MacClosedWorld::from_checkpoint(fixture.registry,fixture.eos,next.checkpoint());
    require(cold.checkpoint()==next.checkpoint(),"Unchanged closed faces changed saved primary fields");
    auto mask=state.grid.closed_x; mask[face]=1;
    const auto closed=source.with_closed_faces(mask,state.grid.closed_y);
    require(closed.state().momentum_x[face]==0 && source.state().momentum_x[face]==.25,
        "Changed face mask skipped momentum closure");
    auto zero=fixture.state(2,2,false); zero.grid.closed_x.assign(zero.momentum_x.size(),0); zero.grid.closed_y.assign(zero.momentum_y.size(),0);
    const auto zface=zero.grid.x_face(1,0); zero.momentum_x[zface]=-0.;
    const MacClosedWorld negative(fixture.registry,fixture.eos,zero);
    const auto normalized=negative.with_closed_faces(zero.grid.closed_x,zero.grid.closed_y);
    require(std::signbit(negative.state().momentum_x[zface]) && !std::signbit(normalized.state().momentum_x[zface]),
        "Unchanged faces bypassed negative-zero normalization");
    zero.momentum_x[zface]=.125; zero.full_momentum_x=mac_field_values(zero.momentum_x,{});
    const MacClosedWorld redundant(fixture.registry,fixture.eos,zero);
    const auto compact=redundant.with_closed_faces(zero.grid.closed_x,zero.grid.closed_y);
    require(!redundant.state().full_momentum_x.empty() && compact.state().full_momentum_x.empty(),
        "Unchanged faces bypassed redundant full-field normalization");
    bool refused=false;
    try { (void)source.with_closed_faces({1},{}); } catch (const std::invalid_argument&) { refused=true; }
    require(refused && source.checkpoint()==image,"Invalid unchanged-face request escaped rollback");
    const MacPassiveEdit huge{0,99,std::numeric_limits<double>::max()};
    const auto first=source.with_passive_edits(std::span(&huge,1));
    const auto invalid=first.with_passive_edits(std::span(&huge,1));
    refused=false;
    try { (void)invalid.with_closed_faces(state.grid.closed_x,state.grid.closed_y); }
    catch (const std::invalid_argument&) { refused=true; }
    require(refused,"Unchanged faces bypassed source inventory admission");
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
        liquid_controls(fixture);
        batch_controls(fixture);
        unchanged_faces(fixture);
        std::cout << "PASS unchanged closed faces, revision/sharing, full-range and legacy normalization\n";
        std::cout << "PASS batched canonical momentum, overlapping/full-range transfers, cold continuation and empty passive sharing\n";
        std::cout << "PASS immutable gas/liquid point transfers, elastic parcel U, carried mechanics/potential, checkpoint continuation and atomic topology rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
