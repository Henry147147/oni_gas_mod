#include "mac_fixture.hpp"
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr auto mixed=MacFluidTransferTopology::occupied_mixture;
void require(bool condition,const char* why) { if (!condition) throw std::runtime_error(why); }
void scaled(MacScaled value) { std::cout << ' ' << value.mantissa() << ' ' << value.exponent(); }
void snapshot(int id,int stage,const MacClosedWorld& world) {
    const auto& state=world.state(); const auto& grid=state.grid;
    std::cout << "WORLD " << id << ' ' << stage << ' ' << grid.width << ' ' << grid.height << ' '
        << world.settings().gravity_m_per_s2 << ' ' << world.time_s() << ' ' << world.revision() << '\n';
    for (std::size_t i=0;i<state.cells.size();++i) {
        const auto& cell=state.cells[i]; const auto reading=world.thermodynamics().evaluate(cell.mixture,grid.volume_m3());
        std::cout << "CELL " << id << ' ' << stage << ' ' << i << ' ' << cell.mixture.energy << ' '
            << reading.temperature_k << ' ' << reading.mechanical_pressure_pa << ' ' << reading.gas_volume_m3 << ' '
            << reading.liquid_volume_m3 << ' ' << reading.entropy_j_per_k;
        for (const auto mass : cell.mixture.masses) std::cout << ' ' << mass;
        std::cout << '\n';
    }
    const auto inertia=mac_face_inertias(world.registry(),state,world.thermodynamics());
    for (int axis=0;axis<2;++axis) {
        const auto values=axis==0 ? mac_field_values(state.momentum_x,state.full_momentum_x) : mac_field_values(state.momentum_y,state.full_momentum_y);
        const auto& masses=axis==0 ? inertia.x : inertia.y;
        for (std::size_t i=0;i<values.size();++i) {
            std::cout << "FACE " << id << ' ' << stage << ' ' << axis << ' ' << i << ' ' << masses[i];
            scaled(values[i]);std::cout << '\n';
        }
    }
}
MacClosedWorld owner(const testing::Setup& fixture,double water) {
    auto state=fixture.state(2,2,false);
    for (auto& cell : state.cells) {
        cell.passive_quantities.clear();cell.mixture=fixture.mixture(1,water==1 ? 2.2e8 : 100000,300,water);
    }
    state.momentum_x[state.grid.x_face(1,0)]=.25;state.momentum_x[state.grid.x_face(1,1)]=-.125;
    state.momentum_y[state.grid.y_face(0,1)]=.0625;state.momentum_y[state.grid.y_face(1,1)]=-.1875;
    MacClosedSettings settings;settings.gravity_m_per_s2=9.81;
    return {fixture.registry,fixture.eos,std::move(state),settings};
}
void accepted(int id,const MacClosedWorld& source,const MacFluidTransfer& edit) {
    const auto held=source.checkpoint();const auto result=source.with_fluid_transfers(std::span(&edit,1),mixed);
    require(source.checkpoint()==held && result.world.time_s()==source.time_s() && result.world.revision()==source.revision()+1,
        "Mixed transfer changed its immutable source or physical clock");
    const auto& packet=result.parcels.at(0);const auto& before=source.state();const auto& after=result.world.state();
    require(packet.mass_kg==edit.mass_kg,"Mixed transfer changed the requested parcel mass");
    for (std::size_t cell=0;cell<before.cells.size();++cell) for (std::size_t species=0;species<source.registry().size();++species) {
        const double original=before.cells[cell].mixture.masses[species];
        require(after.cells[cell].mixture.masses[species]==(cell==edit.cell_index && species==edit.species_index ?
            original+(edit.inject ? edit.mass_kg : -edit.mass_kg) : original),"Mixed transfer changed an unrelated constituent or trace");
    }
    if (edit.inject)
        require(before.momentum_x==after.momentum_x && before.momentum_y==after.momentum_y && packet.resolved_kinetic_energy.is_zero(),
            "At-rest mixed injection supplied momentum or kinetic energy");
    else
        require(!packet.resolved_kinetic_energy.is_zero() && !packet.momentum_x.is_zero() && !packet.momentum_y.is_zero() &&
            packet.potential_energy.require_representable()==edit.mass_kg*9.81*.5,"Mixed removal discarded carried mechanics or potential");
    const auto reading=result.world.thermodynamics().evaluate(after.cells[edit.cell_index].mixture,1);
    require(reading.has_gas && reading.gas_volume_m3>0 && reading.liquid_volume_m3>0 && !reading.has_vacuum &&
        std::abs(reading.gas_volume_m3+reading.liquid_volume_m3-1)<2e-10,"Mixed transfer lost occupied gas/liquid volume closure");
    const auto restored=MacClosedWorld::from_checkpoint(source.registry(),std::make_shared<const FluidThermodynamics>(source.thermodynamics()),result.world.checkpoint());
    require(restored.checkpoint()==result.world.checkpoint() && restored.advanced(.00001).world.checkpoint()==result.world.advanced(.00001).world.checkpoint(),
        "Mixed transfer checkpoint continuation changed an authoritative field");
    std::cout << "CASE " << id << ' ' << edit.cell_index << ' ' << edit.species_index << ' ' << edit.inject << ' ' << edit.mass_kg << ' ' << edit.temperature_k << '\n';
    snapshot(id,0,source);snapshot(id,1,result.world);
    std::cout << "PARCEL " << id << ' ' << packet.mass_kg << ' ' << packet.temperature_k << ' ' << packet.internal_energy_j;
    scaled(packet.resolved_kinetic_energy);scaled(packet.potential_energy);scaled(packet.momentum_x);scaled(packet.momentum_y);std::cout << '\n';
}
void rejections(const testing::Setup& fixture) {
    const auto source=owner(fixture,.25);const auto held=source.checkpoint();
    const auto oxygen=fixture.registry.index("oxygen"),water=fixture.registry.index("water");
    const MacFluidTransfer valid{0,oxygen,true,.05,310};
    for (const auto& invalid : {MacFluidTransfer{0,water,true,4,1e12},MacFluidTransfer{100,water,true,4,310},
            MacFluidTransfer{0,water,true,-1,310},MacFluidTransfer{0,water,true,std::numeric_limits<double>::quiet_NaN(),310}}) {
        bool rejected=false;const std::array edits{valid,invalid};
        try { (void)source.with_fluid_transfers(edits,mixed); } catch (const std::exception&) { rejected=true; }
        require(rejected && source.checkpoint()==held,"A late mixed-transfer rejection published an earlier edit");
    }
    bool policy_rejected=false;
    try { (void)source.with_fluid_transfers(std::span(&valid,1),static_cast<MacFluidTransferTopology>(-1)); }
    catch (const std::invalid_argument&) { policy_rejected=true; }
    require(policy_rejected && source.checkpoint()==held,"Unknown mixture topology policy was admitted");
    bool default_rejected=false;
    try { (void)source.with_fluid_transfers(std::span(&valid,1)); } catch (const std::invalid_argument&) { default_rejected=true; }
    require(default_rejected && source.checkpoint()==held,"The single-phase default silently changed admission");
    const auto liquid=owner(fixture,1);const auto liquid_held=liquid.checkpoint();const MacFluidTransfer void_edit{0,water,false,200,0};
    bool void_rejected=false;
    try { (void)liquid.with_fluid_transfers(std::span(&void_edit,1),mixed); } catch (const std::exception&) { void_rejected=true; }
    require(void_rejected && liquid.checkpoint()==liquid_held,"Mixed policy admitted a liquid free surface or changed its source");
    auto state=fixture.state(2,2,false,true);const MacClosedWorld passive(fixture.registry,fixture.eos,state);const auto passive_held=passive.checkpoint();
    bool passive_rejected=false;
    try { (void)passive.with_fluid_transfers(std::span(&valid,1),mixed); } catch (const std::invalid_argument&) { passive_rejected=true; }
    require(passive_rejected && passive.checkpoint()==passive_held,"Mixed policy discarded unadapted passive quantities");
    std::cout << "REJECTIONS 8\n";
}
}
int main() {
    try {
        std::cout << std::setprecision(17);const auto& fixture=testing::setup();
        for (std::size_t i=0;i<fixture.registry.size();++i) {
            const auto& species=fixture.registry[i];std::cout << "SPECIES " << i << ' ' << species.id << ' ' << species.cv() << ' '
                << species.internal_energy(0) << ' ' << species.molar_mass << '\n';
        }
        const auto water=fixture.registry.index("water"),oxygen=fixture.registry.index("oxygen");const auto wet=owner(fixture,.25);
        accepted(0,wet,{0,oxygen,true,.05,315});accepted(1,wet,{0,water,true,4,310});
        accepted(2,wet,{0,oxygen,false,.02,0});accepted(3,wet,{0,water,false,1.5,0});
        accepted(4,owner(fixture,0),{0,water,true,4,310});accepted(5,owner(fixture,1),{0,oxygen,true,.05,300});
        rejections(fixture);std::cout << "PASS six occupied mixed-phase point transfers and eight atomic rejections\n";return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
