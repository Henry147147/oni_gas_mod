#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma::experimental {
namespace {
void require(bool condition,const char* message) { if (!condition) throw std::invalid_argument(message); }
struct Neighbors { std::size_t first,second; };
constexpr auto absent=std::numeric_limits<std::size_t>::max();
Neighbors adjacent(const MacGrid& grid,std::size_t axis,std::size_t face) {
    const auto x=axis == 0 ? face%grid.x_width() : face%grid.width;
    const auto y=axis == 0 ? face/grid.x_width() : face/grid.width;
    auto first=absent,second=absent;
    if (axis == 0) {
        if (x || grid.periodic_x) first=(x+grid.width-1)%grid.width+y*grid.width;
        if (x < grid.width) second=x+y*grid.width;
    } else {
        if (y || grid.periodic_y) first=x+((y+grid.height-1)%grid.height)*grid.width;
        if (y < grid.height) second=x+y*grid.width;
    }
    if (first != absent && grid.walls[first]) first=absent;
    if (second != absent && grid.walls[second]) second=absent;
    return {first,second};
}
MacEnergy potential(const MacGrid& grid,std::size_t cell,double mass,double gravity) {
    return MacEnergy::from_double(mass).multiplied(gravity).multiplied((static_cast<double>(cell/grid.width)+.5)*grid.spacing_m);
}
void account_state(MacEnergyAccount& energy,MacScaledAccount& px,MacScaledAccount& py,
    const SpeciesRegistry& registry,const FluidThermodynamics& eos,const MacTransportState& state,double gravity,bool negative) {
    const auto inertias=mac_face_inertias(registry,state,eos);
    const auto sign=[&](MacEnergy value) { return negative ? value.negated() : value; };
    for (std::size_t i=0;i<state.cells.size();++i) {
        account_mac_cell_internal_energy(energy,state.cells[i],negative ? -1 : 1);
        energy.add(sign(potential(state.grid,i,state.cells[i].mixture.mass(),gravity)));
    }
    for (std::size_t axis=0;axis<2;++axis) {
        const auto j=axis == 0 ? mac_field_values(state.momentum_x,state.full_momentum_x) : mac_field_values(state.momentum_y,state.full_momentum_y);
        const auto& inertia=axis == 0 ? inertias.x : inertias.y;
        for (std::size_t i=0;i<j.size();++i) {
            energy.add(sign(MacEnergy::kinetic(inertia[i],j[i])));
            (axis == 0 ? px : py).add(sign(j[i]));
        }
    }
}
}

MacGasTransferResult MacClosedWorld::with_gas_transfers(std::span<const MacGasTransfer> edits) const {
    for (const auto& edit : edits)
        require(edit.species_index < registry_.size() && registry_[edit.species_index].phase == Phase::gas,
            "Gas point transfers require a gas material");
    return with_fluid_transfers(edits);
}

MacFluidTransferResult MacClosedWorld::with_fluid_transfers(std::span<const MacFluidTransfer> edits) const {
    return with_fluid_transfers(edits,MacFluidTransferTopology::single_phase);
}

MacFluidTransferResult MacClosedWorld::with_fluid_transfers(std::span<const MacFluidTransfer> edits,MacFluidTransferTopology topology) const {
    require_mac_energy_environment();
    require(topology==MacFluidTransferTopology::single_phase || topology==MacFluidTransferTopology::occupied_mixture,
        "Unknown MAC fluid transfer topology policy");
    require(!edits.empty() && edits.size() <= 4096,"MAC fluid transfer batch must contain 1..4096 requests");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    require(state_.occupied_volumes_m3.empty(),"Fluid point transfers do not yet support partly filled void-phase cells");
    auto next=state_; std::vector<MacFluidParcel> parcels; parcels.reserve(edits.size());
    std::size_t unresolved=0;MacPrimaryRoundingCounts rounding;
    for (const auto& edit : edits) {
        require(edit.cell_index < next.cells.size() && !next.grid.walls[edit.cell_index] && edit.species_index < registry_.size() &&
            (registry_[edit.species_index].phase == Phase::gas || registry_[edit.species_index].phase == Phase::liquid) &&
            std::isfinite(edit.mass_kg) && edit.mass_kg >= 0 && std::isfinite(edit.temperature_k) &&
            (!edit.inject || edit.temperature_k > 0),"Invalid MAC fluid transfer request");
        auto& cell=next.cells[edit.cell_index];
        const bool liquid=registry_[edit.species_index].phase == Phase::liquid;
        require(cell.passive_quantities.empty(),"Fluid point transfers require an explicit passive-metadata adapter");
        if (topology==MacFluidTransferTopology::single_phase)
            for (std::size_t k=0;k<registry_.size();++k)
                require(cell.mixture.masses[k] == 0 || (liquid ? k == edit.species_index : registry_[k].phase == Phase::gas),
                    "Fluid point transfers require gas-only or one matching liquid material");
        const auto before=thermodynamics_->evaluate(cell.mixture,next.grid.volume_m3());
        require(!before.has_vacuum,"Fluid point transfers require an occupied cell without a void");
        const double mass=edit.inject ? edit.mass_kg : std::min(edit.mass_kg,cell.mixture.masses[edit.species_index]);
        const auto temperature=edit.inject ? edit.temperature_k : before.temperature_k;
        MacFluidParcel packet{edit.cell_index,edit.species_index,edit.inject,mass,temperature,0,{},{},{},{}};
        if (mass == 0) { parcels.push_back(packet); continue; }
        std::vector<double> contents(registry_.size()); contents[edit.species_index]=mass;
        if (liquid) {
            const auto& reference=thermodynamics_->reference();
            std::size_t profile=0;
            while (profile < reference.materials().size() && reference.materials()[profile].id != registry_[edit.species_index].id) ++profile;
            require(profile < reference.materials().size(),"Transferred liquid has no thermodynamic profile");
            const auto specific=edit.inject ? registry_[edit.species_index].internal_energy(temperature)+
                reference.liquid_response(profile,before.mechanical_pressure_pa).specific_elastic_energy_j_per_kg :
                before.specific_internal_energies_j_per_kg[profile];
            packet.internal_energy_j=mass*specific;
        } else packet.internal_energy_j=thermodynamics_->initialize_at_temperature(contents,temperature,next.grid.volume_m3()).mixture.energy;
        require(std::isfinite(packet.internal_energy_j),"Fluid parcel internal energy is outside the numerical range");
        packet.potential_energy=potential(next.grid,edit.cell_index,mass,settings_.gravity_m_per_s2);
        MacEnergyAccount energy; MacScaledAccount px,py;
        account_state(energy,px,py,registry_,*thermodynamics_,next,settings_.gravity_m_per_s2,false);
        const auto inertia0=mac_face_inertias(registry_,next,*thermodynamics_);
        auto jx=mac_field_values(next.momentum_x,next.full_momentum_x),jy=mac_field_values(next.momentum_y,next.full_momentum_y);
        const auto old_mass=cell.mixture.masses[edit.species_index];
        const auto signed_mass=edit.inject ? mass : -mass;
        cell.mixture.masses[edit.species_index]=old_mass+signed_mass;
        if (cell.mixture.masses[edit.species_index] == old_mass)
            throw FluidFatalError(FluidFatalCode::numerical_precision,"Fluid transfer cannot represent its constituent mass change");
        require(cell.mixture.mass() > 0,"Fluid removal would create an unsupported empty MAC cell");
        MacEnergyAccount mass_account; mass_account.add(old_mass); mass_account.add(signed_mass); mass_account.add(-cell.mixture.masses[edit.species_index]);
        require(mass_account.within_bound(),"Fluid transfer constituent account failed");
        const auto material_energy=edit.inject ? packet.internal_energy_j : -packet.internal_energy_j;
        (void)add_mac_cell_internal_energy(cell,MacEnergy::from_double(material_energy),&rounding);
        const auto inertia1=mac_face_inertias(registry_,next,*thermodynamics_);
        std::vector<MacEnergySum> heat(next.cells.size()); MacEnergySum carried_kinetic; MacScaledSum carried_px,carried_py;
        const auto x=edit.cell_index%next.grid.width,y=edit.cell_index/next.grid.width;
        for (std::size_t axis=0;axis<2;++axis) {
            auto& momenta=axis == 0 ? jx : jy;
            const auto& old_i=axis == 0 ? inertia0.x : inertia0.y;
            const auto& new_i=axis == 0 ? inertia1.x : inertia1.y;
            const std::array<std::size_t,2> faces=axis == 0 ? std::array<std::size_t,2>{next.grid.x_face(x,y),next.grid.x_face((x+1)%next.grid.x_width(),y)} :
                std::array<std::size_t,2>{next.grid.y_face(x,y),next.grid.y_face(x,(y+1)%next.grid.y_height())};
            for (const auto face : faces) {
                const auto old_j=momenta[face]; const auto old_k=MacEnergy::kinetic(old_i[face],old_j);
                MacEnergySum remainder; remainder.add(old_k);
                if (!edit.inject) {
                    const auto outgoing_j=old_j.shared(.5*mass,old_i[face]);
                    (axis == 0 ? carried_px : carried_py).add(outgoing_j);
                    momenta[face]=old_j.shared(new_i[face],old_i[face]);
                    const auto outgoing_k=old_k.shared(.5*mass,old_i[face]);
                    carried_kinetic.add(outgoing_k); remainder.add(outgoing_k.negated());
                }
                remainder.add(MacEnergy::kinetic(new_i[face],momenta[face]).negated());
                if (edit.inject && remainder.sign() < 0) throw FluidFatalError(FluidFatalCode::numerical_precision,"At-rest injection increased face kinetic energy");
                const auto neighbors=adjacent(next.grid,axis,face);
                for (const auto index : {neighbors.first,neighbors.second}) if (index != absent)
                    heat[index].add(remainder.value().shared(.5*next.cells[index].mixture.mass(),new_i[face]));
            }
        }
        packet.resolved_kinetic_energy=carried_kinetic.value(); packet.momentum_x=carried_px.value(); packet.momentum_y=carried_py.value();
        store_mac_field(next.momentum_x,next.full_momentum_x,jx); store_mac_field(next.momentum_y,next.full_momentum_y,jy);
        for (std::size_t i=0;i<heat.size();++i) if (heat[i].sign() != 0) {
            const auto old=next.cells[i]; MacEnergySum value=heat[i]; account_mac_cell_internal_energy(value,old);
            (void)store_mac_cell_internal_energy(next.cells[i],value,&rounding);
            MacEnergyAccount local; account_mac_cell_internal_energy(local,old);
            for (std::size_t k=0;k<heat[i].size();++k) local.add(heat[i].component(k));
            account_mac_cell_internal_energy(local,next.cells[i],-1);
            require(local.within_bound(),"Fluid transfer mixing heat account failed");
            if (same_mac_cell_internal_energy(old,next.cells[i])) ++unresolved;
        }
        energy.add(material_energy); energy.add(edit.inject ? packet.potential_energy : packet.potential_energy.negated());
        if (!edit.inject) { energy.add(packet.resolved_kinetic_energy.negated()); px.add(packet.momentum_x.negated()); py.add(packet.momentum_y.negated()); }
        account_state(energy,px,py,registry_,*thermodynamics_,next,settings_.gravity_m_per_s2,true);
        require(energy.within_bound() && px.within_bound() && py.within_bound(),"Fluid transfer U+K+potential/momentum account failed");
        require(!thermodynamics_->evaluate(cell.mixture,next.grid.volume_m3()).has_vacuum,
            "Fluid removal would create an unsupported liquid free surface");
        parcels.push_back(std::move(packet));
    }
    MacClosedWorld candidate(registry_,thermodynamics_,std::move(next),settings_,transport_);
    candidate.time_s_=time_s_; candidate.revision_=revision_+1;
    return {std::move(candidate),std::move(parcels),unresolved,rounding};
}
}
