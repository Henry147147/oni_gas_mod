#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "mixed_atmospheres/passive_quantities.hpp"
#include <algorithm>
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
    const MacFaceInertias& inertias,const MacTransportState& state,double gravity,bool negative) {
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
    // Void phase (MAC_VACUUM.md, "Point transfers"): every cell is read at its
    // occupied volume. An empty cell may receive fluid and a removal may empty a
    // cell or open a liquid surface; liquid-only cells hold their liquid at zero
    // pressure when that leaves void.
    const bool voids=settings_.vacuum == MacVacuumRepresentation::void_phase;
    auto next=state_; std::vector<MacFluidParcel> parcels; parcels.reserve(edits.size());
    const double grid_volume=next.grid.volume_m3();
    if (voids && next.occupied_volumes_m3.empty()) {
        next.occupied_volumes_m3.assign(next.cells.size(),0);
        for (std::size_t i=0;i<next.cells.size();++i) if (!next.grid.walls[i] && next.cells[i].mixture.mass() > 0) next.occupied_volumes_m3[i]=grid_volume;
    }
    const auto& materials=thermodynamics_->reference().materials();
    const auto density=[&](std::size_t k) {
        for (const auto& m : materials) if (m.id == registry_[k].id) return m.zero_pressure_density_kg_per_m3;
        throw std::invalid_argument("Transferred liquid has no thermodynamic profile");
    };
    const auto nominal_liquid=[&](const MixtureInventory& mixture) {
        double volume=0;
        for (std::size_t k=0;k<registry_.size();++k) if (mixture.masses[k] != 0 && registry_[k].phase == Phase::liquid) volume+=mixture.masses[k]/density(k);
        return volume;
    };
    std::size_t unresolved=0;MacPrimaryRoundingCounts rounding;
    // One U+K+potential/momentum account spans the batch: the per-edit
    // accounts telescope, so the intermediate states need not be summed.
    // Face inertias (half the mass of each adjacent cell) change only on the
    // edited cell's four faces and are updated there.
    auto inertia=mac_face_inertias(registry_,next,*thermodynamics_);
    MacEnergyAccount energy; MacScaledAccount px,py;
    account_state(energy,px,py,inertia,next,settings_.gravity_m_per_s2,false);
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
        const bool was_empty=cell.mixture.mass() == 0;
        const double held=voids && !was_empty ? next.occupied_volumes_m3[edit.cell_index] : grid_volume;
        const auto before=thermodynamics_->evaluate(cell.mixture,held);
        require((voids && was_empty) || !before.has_vacuum,"Fluid point transfers require an occupied cell without a void");
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
        auto jx=mac_field_values(next.momentum_x,next.full_momentum_x),jy=mac_field_values(next.momentum_y,next.full_momentum_y);
        const auto old_mass=cell.mixture.masses[edit.species_index];
        const auto signed_mass=edit.inject ? mass : -mass;
        cell.mixture.masses[edit.species_index]=old_mass+signed_mass;
        if (cell.mixture.masses[edit.species_index] == old_mass)
            throw FluidFatalError(FluidFatalCode::numerical_precision,"Fluid transfer cannot represent its constituent mass change");
        require(voids || cell.mixture.mass() > 0,"Fluid removal would create an unsupported empty MAC cell");
        if (voids) {
            // Gas keeps the occupied volume (an empty cell's gas fills it); a
            // liquid-only cell holds its liquid at zero pressure when that leaves
            // void; added liquid enlarges a mixed cell's material.
            auto& occupied=next.occupied_volumes_m3[edit.cell_index]; const double nominal=nominal_liquid(cell.mixture);
            bool gas=false; for (std::size_t k=0;k<registry_.size();++k) gas=gas || (cell.mixture.masses[k] != 0 && registry_[k].phase == Phase::gas);
            // Gas injected into a gas-free liquid surface fills the void above
            // the liquid rather than compressing into the liquid's volume.
            const bool first_gas=edit.inject && !liquid && old_mass == 0 &&
                [&] { for (std::size_t k=0;k<registry_.size();++k) if (k != edit.species_index && cell.mixture.masses[k] != 0 && registry_[k].phase == Phase::gas) return false; return true; }();
            if (cell.mixture.mass() == 0) occupied=0;
            else if (!gas) occupied=std::min(grid_volume,nominal);
            else if (was_empty || first_gas) occupied=grid_volume;
            else if (liquid && edit.inject) occupied=std::min(grid_volume,occupied+mass/density(edit.species_index));
        }
        MacEnergyAccount mass_account; mass_account.add(old_mass); mass_account.add(signed_mass); mass_account.add(-cell.mixture.masses[edit.species_index]);
        require(mass_account.within_bound(),"Fluid transfer constituent account failed");
        // A removal that empties a cell takes exactly its stored energy, so the
        // empty cell holds none.
        const bool emptied=voids && !edit.inject && cell.mixture.mass() == 0;
        if (emptied) { MacEnergySum held_energy; account_mac_cell_internal_energy(held_energy,cell); packet.internal_energy_j=held_energy.value().project().value; }
        const auto material_energy=edit.inject ? packet.internal_energy_j : -packet.internal_energy_j;
        if (emptied) { cell.mixture.energy=0; cell.energy_remainder_j={}; }
        else (void)add_mac_cell_internal_energy(cell,MacEnergy::from_double(material_energy),&rounding);
        std::vector<std::pair<std::size_t,MacEnergySum>> heat; MacEnergySum carried_kinetic; MacScaledSum carried_px,carried_py;
        const auto x=edit.cell_index%next.grid.width,y=edit.cell_index/next.grid.width;
        const auto face_inertia=[&](std::size_t axis,std::size_t face) {
            const auto ends=adjacent(next.grid,axis,face);
            double value=0;
            for (const auto end : {ends.first,ends.second}) if (end != absent) {
                const double m=next.cells[end].mixture.mass(), half=.5*m;
                if (m > 0 && half == 0) throw FluidFatalError(FluidFatalCode::numerical_precision,"Positive MAC half mass is unrepresentable");
                value+=half;
            }
            require(std::isfinite(value),"Non-finite MAC face inertia");
            return value;
        };
        for (std::size_t axis=0;axis<2;++axis) {
            auto& momenta=axis == 0 ? jx : jy;
            auto& new_i=axis == 0 ? inertia.x : inertia.y;
            const std::array<std::size_t,2> faces=axis == 0 ? std::array<std::size_t,2>{next.grid.x_face(x,y),next.grid.x_face((x+1)%next.grid.x_width(),y)} :
                std::array<std::size_t,2>{next.grid.y_face(x,y),next.grid.y_face(x,(y+1)%next.grid.y_height())};
            std::array<double,2> old_i{};
            for (std::size_t q=0;q<2;++q) { old_i[q]=new_i[faces[q]]; new_i[faces[q]]=face_inertia(axis,faces[q]); }
            for (std::size_t q=0;q<2;++q) {
                const auto face=faces[q];
                const auto old_j=momenta[face]; const auto old_k=MacEnergy::kinetic(old_i[q],old_j);
                MacEnergySum remainder; remainder.add(old_k);
                if (!edit.inject) {
                    const auto outgoing_j=old_j.shared(.5*mass,old_i[q]);
                    (axis == 0 ? carried_px : carried_py).add(outgoing_j);
                    momenta[face]=old_j.shared(new_i[face],old_i[q]);
                    const auto outgoing_k=old_k.shared(.5*mass,old_i[q]);
                    carried_kinetic.add(outgoing_k); remainder.add(outgoing_k.negated());
                }
                remainder.add(MacEnergy::kinetic(new_i[face],momenta[face]).negated());
                if (edit.inject && remainder.sign() < 0) throw FluidFatalError(FluidFatalCode::numerical_precision,"At-rest injection increased face kinetic energy");
                // A face left without inertia (beside an emptied cell) keeps no
                // kinetic energy to share; the parcel carried it away.
                if (!(new_i[face] > 0)) { require(remainder.sign() <= 0 || edit.inject,"Emptied face retained kinetic energy"); continue; }
                const auto neighbors=adjacent(next.grid,axis,face);
                for (const auto index : {neighbors.first,neighbors.second}) if (index != absent) {
                    auto slot=std::find_if(heat.begin(),heat.end(),[&](const auto& h) { return h.first == index; });
                    if (slot == heat.end()) { heat.emplace_back(index,MacEnergySum{}); slot=heat.end()-1; }
                    slot->second.add(remainder.value().shared(.5*next.cells[index].mixture.mass(),new_i[face]));
                }
            }
        }
        // Cells in ascending order, as the full-grid pass applied them.
        std::sort(heat.begin(),heat.end(),[](const auto& a,const auto& b) { return a.first < b.first; });
        packet.resolved_kinetic_energy=carried_kinetic.value(); packet.momentum_x=carried_px.value(); packet.momentum_y=carried_py.value();
        store_mac_field(next.momentum_x,next.full_momentum_x,jx); store_mac_field(next.momentum_y,next.full_momentum_y,jy);
        for (const auto& [i,amount] : heat) if (amount.sign() != 0) {
            const auto old=next.cells[i]; MacEnergySum value=amount; account_mac_cell_internal_energy(value,old);
            (void)store_mac_cell_internal_energy(next.cells[i],value,&rounding);
            MacEnergyAccount local; account_mac_cell_internal_energy(local,old);
            for (std::size_t k=0;k<amount.size();++k) local.add(amount.component(k));
            account_mac_cell_internal_energy(local,next.cells[i],-1);
            require(local.within_bound(),"Fluid transfer mixing heat account failed");
            if (same_mac_cell_internal_energy(old,next.cells[i])) ++unresolved;
        }
        energy.add(material_energy); energy.add(edit.inject ? packet.potential_energy : packet.potential_energy.negated());
        if (!edit.inject) { energy.add(packet.resolved_kinetic_energy.negated()); px.add(packet.momentum_x.negated()); py.add(packet.momentum_y.negated()); }
        require((voids && cell.mixture.mass() == 0) ||
            !thermodynamics_->evaluate(cell.mixture,voids ? next.occupied_volumes_m3[edit.cell_index] : grid_volume).has_vacuum,
            "Fluid removal would create an unsupported liquid free surface");
        parcels.push_back(std::move(packet));
    }
    // The full recomputation validates every cell and must equal the
    // incrementally updated faces.
    const auto final_inertia=mac_face_inertias(registry_,next,*thermodynamics_);
    if (final_inertia.x != inertia.x || final_inertia.y != inertia.y)
        throw std::logic_error("Fluid transfer face inertias differ from their full recomputation");
    account_state(energy,px,py,final_inertia,next,settings_.gravity_m_per_s2,true);
    require(energy.within_bound() && px.within_bound() && py.within_bound(),"Fluid transfer U+K+potential/momentum account failed");
    MacClosedWorld candidate(registry_,thermodynamics_,std::move(next),settings_,transport_);
    candidate.time_s_=time_s_; candidate.revision_=revision_+1;
    return {std::move(candidate),std::move(parcels),unresolved,rounding};
}

MacClosedWorld MacClosedWorld::with_opened_cells(std::span<const std::size_t> cells) const {
    require_mac_energy_environment();
    require(!cells.empty() && cells.size() <= 4096,"MAC terrain batch must contain 1..4096 cells");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    auto next=state_;
    for (const auto i : cells) {
        require(i < next.cells.size() && next.grid.walls[i],"Only a wall can be opened");
        const auto& cell=next.cells[i];
        require(cell.mixture.mass() == 0 && cell.mixture.energy == 0 && cell.energy_remainder_j.is_zero() && cell.passive_quantities.empty(),
            "An opened wall must hold no material");
        next.grid.walls[i]=0;
        if (!next.occupied_volumes_m3.empty()) next.occupied_volumes_m3[i]=0;
    }
    MacClosedWorld candidate(registry_,thermodynamics_,std::move(next),settings_,transport_);
    candidate.time_s_=time_s_; candidate.revision_=revision_+1;
    return candidate;
}
MacClosedWorld MacClosedWorld::with_closed_cells(std::span<const std::size_t> cells) const {
    require_mac_energy_environment();
    require(!cells.empty() && cells.size() <= 4096,"MAC terrain batch must contain 1..4096 cells");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    auto next=state_;
    for (const auto i : cells) {
        require(i < next.cells.size() && !next.grid.walls[i],"Only an open cell can be closed");
        const auto& cell=next.cells[i];
        require(cell.mixture.mass() == 0 && cell.mixture.energy == 0 && cell.energy_remainder_j.is_zero() && cell.passive_quantities.empty(),
            "A closed cell must first be emptied");
    }
    const auto inertia=mac_face_inertias(registry_,next,*thermodynamics_);
    auto jx=mac_field_values(next.momentum_x,next.full_momentum_x),jy=mac_field_values(next.momentum_y,next.full_momentum_y);
    MacEnergyAccount energy; MacPrimaryRoundingCounts rounding;
    for (const auto i : cells) {
        const auto x=i%next.grid.width,y=i/next.grid.width;
        for (std::size_t axis=0;axis<2;++axis) {
            auto& momenta=axis == 0 ? jx : jy; const auto& inertias=axis == 0 ? inertia.x : inertia.y;
            const std::array<std::size_t,2> faces=axis == 0 ? std::array<std::size_t,2>{next.grid.x_face(x,y),next.grid.x_face((x+1)%next.grid.x_width(),y)} :
                std::array<std::size_t,2>{next.grid.y_face(x,y),next.grid.y_face(x,(y+1)%next.grid.y_height())};
            for (const auto face : faces) {
                if (!(inertias[face] > 0)) continue;
                const auto kinetic=MacEnergy::kinetic(inertias[face],momenta[face]);
                momenta[face]=MacScaled{};
                const auto carriers=adjacent(next.grid,axis,face);
                const auto neighbour=carriers.first == i ? carriers.second : carriers.first;
                if (neighbour == absent) { require(kinetic.is_zero(),"A closed face without a carrier held kinetic energy"); continue; }
                auto& target=next.cells[neighbour];
                account_mac_cell_internal_energy(energy,target);
                (void)add_mac_cell_internal_energy(target,kinetic,&rounding);
                energy.add(kinetic); account_mac_cell_internal_energy(energy,target,-1);
            }
        }
        next.grid.walls[i]=1;
        if (!next.occupied_volumes_m3.empty()) next.occupied_volumes_m3[i]=0;
    }
    require(energy.within_bound(),"MAC terrain closure energy account failed");
    store_mac_field(next.momentum_x,next.full_momentum_x,jx); store_mac_field(next.momentum_y,next.full_momentum_y,jy);
    MacClosedWorld candidate(registry_,thermodynamics_,std::move(next),settings_,transport_);
    candidate.time_s_=time_s_; candidate.revision_=revision_+1;
    return candidate;
}

MacLiquidSettling MacClosedWorld::with_liquid_settling(double fraction) const {
    require_mac_energy_environment();
    require(std::isfinite(fraction) && fraction >= 0 && fraction <= .25,"MAC liquid settling fraction must be in [0, 1/4]");
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("MAC closed revision overflow");
    MacLiquidSettlingReport report; const double gravity=settings_.gravity_m_per_s2;
    if (fraction == 0 || gravity == 0) return {*this,report};
    const auto& grid=state_.grid; const double volume=grid.volume_m3(); const auto species=registry_.size();
    const auto& materials=thermodynamics_->reference().materials();
    // Registry order: each constituent's EOS profile; each liquid's zero-pressure
    // density and phase (the first liquid of its nonzero group, else itself).
    std::vector<std::size_t> profile(species,absent),phase(species,absent); std::vector<double> rho(species,0);
    for (std::size_t k=0;k<species;++k) {
        for (std::size_t p=0;p<materials.size();++p) if (materials[p].id == registry_[k].id) profile[k]=p;
        if (registry_[k].phase != Phase::liquid || profile[k] == absent) continue;
        rho[k]=materials[profile[k]].zero_pressure_density_kg_per_m3; phase[k]=k;
        const auto group=registry_[k].liquid_phase_group;
        if (group != 0) for (std::size_t q=0;q<k;++q)
            if (registry_[q].phase == Phase::liquid && registry_[q].liquid_phase_group == group) { phase[k]=q; break; }
    }
    // A cell's lightest or heaviest liquid phase with its zero-pressure volume.
    struct Pick { std::size_t phase=absent; double density=0,volume=0; };
    std::vector<Pick> sums;
    const auto pick=[&](const MixtureInventory& m,bool lightest) {
        sums.clear();
        for (std::size_t k=0;k<species;++k) if (phase[k] != absent && m.masses[k] > 0) {
            auto it=std::find_if(sums.begin(),sums.end(),[&](const Pick& p) { return p.phase == phase[k]; });
            if (it == sums.end()) { sums.push_back({phase[k],0,0}); it=sums.end()-1; }
            it->density+=m.masses[k]; it->volume+=m.masses[k]/rho[k];
        }
        Pick best;
        for (auto p : sums) {
            p.density/=p.volume;
            if (best.phase == absent || (lightest ? p.density < best.density : p.density > best.density)) best=p;
        }
        return best;
    };
    const auto full=[&](std::size_t i) {
        return !grid.walls[i] && state_.cells[i].mixture.mass() > 0 &&
            (state_.occupied_volumes_m3.empty() || state_.occupied_volumes_m3[i] == volume);
    };
    struct Parcel { std::size_t from,to,phase; double share; };
    std::vector<Parcel> parcels;
    for (std::size_t y=0;y+1<grid.height;++y) for (std::size_t x=0;x<grid.width;++x) {
        const auto lower=x+y*grid.width,upper=lower+grid.width;
        if (!full(lower) || !full(upper)) continue;
        const auto light=pick(state_.cells[lower].mixture,true); if (light.phase == absent) continue;
        const auto heavy=pick(state_.cells[upper].mixture,false); if (heavy.phase == absent) continue;
        if (light.phase == heavy.phase || !(light.density < heavy.density)) continue;
        const double exchanged=fraction*std::min(light.volume,heavy.volume);
        parcels.push_back({lower,upper,light.phase,exchanged/light.volume}); parcels.push_back({upper,lower,heavy.phase,exchanged/heavy.volume});
        ++report.faces;
    }
    if (parcels.empty()) return {*this,report};
    // Each parcel leaves at its donor's EOS specific energies; a cell takes at
    // most a quarter of a phase out through each of its two vertical faces.
    auto next=state_; std::vector<MacEnergySum> change(next.cells.size()); std::vector<std::uint8_t> touched(next.cells.size(),0);
    std::vector<std::vector<double>> specific(next.cells.size());
    MacEnergySum released;
    for (const auto& parcel : parcels) {
        const auto& donor=state_.cells[parcel.from];
        if (specific[parcel.from].empty())
            specific[parcel.from]=thermodynamics_->evaluate(donor.mixture,mac_material_volume(state_,parcel.from)).specific_internal_energies_j_per_kg;
        double mass=0; MacEnergySum energy;
        for (std::size_t k=0;k<species;++k) if (phase[k] == parcel.phase && donor.mixture.masses[k] > 0) {
            auto& from=next.cells[parcel.from].mixture.masses[k];
            const double before=from; from=before-donor.mixture.masses[k]*parcel.share;
            const double moved=before-from; next.cells[parcel.to].mixture.masses[k]+=moved; mass+=moved;
            energy.add(MacEnergy::from_double(moved).multiplied(specific[parcel.from][profile[k]]));
        }
        const auto carried=energy.value();
        change[parcel.from].add(carried.negated()); change[parcel.to].add(carried);
        // Passives move with the parcel's share of its donor's mass.
        PassiveQuantities moved;
        for (const auto& q : donor.passive_quantities) {
            const double amount=q.quantity*(mass/donor.mixture.mass()); if (amount > 0) moved.push_back({q.identity,amount});
        }
        if (!moved.empty()) {
            add_passive_quantities(next.cells[parcel.to].passive_quantities,moved,1);
            auto& left=next.cells[parcel.from].passive_quantities;
            for (const auto& q : moved) {
                const auto it=std::find_if(left.begin(),left.end(),[&](const PassiveQuantity& p) { return p.identity == q.identity; });
                it->quantity-=q.quantity;
            }
            std::erase_if(left,[](const PassiveQuantity& p) { return !(p.quantity > 0); });
        }
        // Potential energy released by the move, half to each cell.
        MacEnergySum drop; drop.add(potential(grid,parcel.from,mass,gravity)); drop.add(potential(grid,parcel.to,mass,gravity).negated());
        const auto heat=drop.value().shifted(-1);
        change[parcel.from].add(heat); change[parcel.to].add(heat); released.add(drop.value());
        touched[parcel.from]=touched[parcel.to]=1; report.moved_kg+=mass;
    }
    // Momentum is unchanged; the faces whose inertia (half the mass of each
    // adjacent cell) changed book their kinetic change to their cells by mass share.
    const auto before=mac_face_inertias(registry_,state_,*thermodynamics_); auto after=before;
    for (std::size_t axis=0;axis<2;++axis) {
        auto& values=axis == 0 ? after.x : after.y;
        for (std::size_t face=0;face<values.size();++face) {
            const auto ends=adjacent(grid,axis,face);
            if (!((ends.first != absent && touched[ends.first]) || (ends.second != absent && touched[ends.second]))) continue;
            double value=0;
            for (const auto end : {ends.first,ends.second}) if (end != absent) value+=.5*next.cells[end].mixture.mass();
            values[face]=value;
        }
    }
    for (std::size_t axis=0;axis<2;++axis) {
        const auto j=axis == 0 ? mac_field_values(next.momentum_x,next.full_momentum_x) : mac_field_values(next.momentum_y,next.full_momentum_y);
        const auto& old_i=axis == 0 ? before.x : before.y; const auto& new_i=axis == 0 ? after.x : after.y;
        for (std::size_t face=0;face<j.size();++face) {
            if (old_i[face] == new_i[face] || j[face].is_zero()) continue;
            MacEnergySum remainder; remainder.add(MacEnergy::kinetic(old_i[face],j[face])); remainder.add(MacEnergy::kinetic(new_i[face],j[face]).negated());
            const auto ends=adjacent(grid,axis,face);
            for (const auto index : {ends.first,ends.second}) if (index != absent)
                change[index].add(remainder.value().shared(.5*next.cells[index].mixture.mass(),new_i[face]));
        }
    }
    MacPrimaryRoundingCounts rounding;
    for (std::size_t i=0;i<next.cells.size();++i) if (change[i].size() != 0) {
        MacEnergySum total=change[i]; account_mac_cell_internal_energy(total,state_.cells[i]);
        (void)store_mac_cell_internal_energy(next.cells[i],total,&rounding);
    }
    for (std::size_t i=0;i<next.cells.size();++i) if (touched[i]) {
        validate_passive_quantities(next.cells[i].passive_quantities);
        if (thermodynamics_->evaluate(next.cells[i].mixture,mac_material_volume(next,i)).has_vacuum)
            throw std::domain_error("MAC liquid settling opened a void");
    }
    if (const auto final_inertia=mac_face_inertias(registry_,next,*thermodynamics_); final_inertia.x != after.x || final_inertia.y != after.y)
        throw std::logic_error("Liquid settling face inertias differ from their full recomputation");
    MacEnergyAccount energy; MacScaledAccount px,py;
    account_state(energy,px,py,before,state_,gravity,false); account_state(energy,px,py,after,next,gravity,true);
    require(energy.within_bound(),"MAC liquid settling U+K+potential account failed");
    report.released_j=released.value().project().value;
    MacClosedWorld candidate(registry_,thermodynamics_,std::move(next),settings_,transport_);
    candidate.time_s_=time_s_; candidate.revision_=revision_+1;
    return {std::move(candidate),report};
}
}
