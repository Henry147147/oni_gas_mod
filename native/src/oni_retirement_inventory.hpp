#pragma once
// A world retirement crosses the same exact-inventory boundary as pipe
// emptying. Prepare its complete inventories before committing any removal.
// This wire is transient; it is never part of an owner checkpoint.
#include "oni_parcel_wire.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <climits>
#include <map>

namespace ma::oni::colony::detail {
struct RetirementInventory {
    using Sum=experimental::MacEnergySum;
    std::map<std::uint32_t,Sum> masses;
    std::map<std::uint64_t,Sum> germs;
    Sum internal_energy,kinetic,potential,momentum_x,momentum_y,remainder;

    void add(const experimental::MacFluidParcel& p,std::uint32_t native) {
        masses[native].add(p.mass_kg);internal_energy.add(p.internal_energy_j);
        kinetic.add(p.resolved_kinetic_energy);potential.add(p.potential_energy);
        momentum_x.add(p.momentum_x);momentum_y.add(p.momentum_y);
        // Observe the actual removal, including fractional quantities the
        // binary64 carried share alone could not represent.
        for(const auto& q:p.passives_before)germs[q.identity].add(q.quantity);
        for(const auto& q:p.passives_after)germs[q.identity].add(-q.quantity);
    }
    static void amount(ParcelWireWriter& wire,const Sum& value) {
        ParcelWireReader::require(value.size()<=4096,"Retirement inventory expansion is too large");
        wire.u32(static_cast<std::uint32_t>(value.size()));
        for(std::size_t i=0;i<value.size();++i) {
            const auto projected=value.component(i).project();
            ParcelWireReader::require(!projected.rounded && projected.value!=0,
                "Retirement inventory cannot project an exact expansion atom");
            wire.f64(projected.value);
        }
    }
    void write(ParcelWireWriter& wire) const {
        const auto count=[](const auto& entries) {
            return static_cast<std::uint32_t>(std::count_if(entries.begin(),entries.end(),[](const auto& e){return e.second.sign()!=0;}));
        };
        wire.u32(count(masses));
        for(const auto& [id,mass]:masses)if(mass.sign()!=0) {
            ParcelWireReader::require(id<=INT32_MAX && mass.sign()>0,"Invalid retirement constituent");
            wire.u32(id);amount(wire,mass);
        }
        amount(wire,internal_energy);
        ParcelWireReader::require(count(germs)<=256,"Too many retirement germ identities");wire.u32(count(germs));
        for(const auto& [id,population]:germs)if(population.sign()!=0) {
            ParcelWireReader::require(id>0 && id<=static_cast<std::uint64_t>(INT32_MAX)+1 && population.sign()>0,
                "Invalid retirement germ inventory");
            wire.u32(static_cast<std::uint32_t>(id-1));amount(wire,population);
        }
        wire.sum(kinetic);wire.sum(potential);wire.sum(momentum_x);wire.sum(momentum_y);amount(wire,remainder);
    }
};
}
