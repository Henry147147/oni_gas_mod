#pragma once
// Owner-held resolution remainders, not another solver or an external queue.
// Only stationary, passive-free gas enters this inventory. The signed energy
// expansion owns every represented U+K+G term, even after its mass reaches zero.
#include "oni_parcel_wire.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <limits>

namespace ma::oni::colony::detail {
struct PrecisionParcel {
    std::uint32_t source=0,anchor=0,radius=2,native_index=0;
    double mass_kg=0,temperature_k=0;
    experimental::MacEnergySum energy_j;
};
inline void add_precision_sum(experimental::MacEnergySum& to,const experimental::MacEnergySum& from,int sign=1) {
    for(std::size_t i=0;i<from.size();++i)to.add(sign>0?from.component(i):from.component(i).negated());
}
inline long double precision_value(const experimental::MacEnergySum& sum) {
    long double value=0;
    for(std::size_t i=0;i<sum.size();++i) {
        const auto term=sum.component(i);
        ParcelWireReader::require(term.exponent()<=1024,"Precision inventory exceeds owner reporting range");
        const auto x=term.project().value;
        value+=static_cast<long double>(x);
    }
    ParcelWireReader::require(std::isfinite(value),"Precision inventory exceeds owner reporting range");
    return value;
}
inline void validate_precision_parcel(const PrecisionParcel& p) {
    ParcelWireReader::require(p.radius==2 && std::isfinite(p.mass_kg) && p.mass_kg>=0 &&
        !std::signbit(p.mass_kg) && std::isfinite(p.temperature_k) && p.temperature_k>0 &&
        (p.mass_kg>0 || p.energy_j.sign()!=0),"Invalid precision parcel");
    (void)precision_value(p.energy_j);
}
inline std::vector<std::uint8_t> encode_precision_inventory(std::span<const PrecisionParcel> entries) {
    ParcelWireWriter w;w.raw("MAPREC01");
    ParcelWireReader::require(entries.size()<=maximum_parcel_entries,"Too many precision parcels");
    w.u32(static_cast<std::uint32_t>(entries.size()));
    for(const auto& p:entries) {
        validate_precision_parcel(p);
        w.u32(p.source);w.u32(p.anchor);w.u32(p.radius);w.u32(p.native_index);w.f64(p.mass_kg);w.f64(p.temperature_k);w.sum(p.energy_j);
    }
    return std::move(w.bytes);
}
inline std::vector<PrecisionParcel> decode_precision_inventory(std::span<const std::uint8_t> bytes) {
    ParcelWireReader::require(bytes.size()>=12 && bytes.size()<=maximum_parcel_wire_bytes,"Invalid precision inventory size");
    ParcelWireReader r{bytes};const auto magic=r.raw(8);
    ParcelWireReader::require(std::string_view(reinterpret_cast<const char*>(magic.data()),8)=="MAPREC01","Invalid precision inventory signature");
    const auto count=r.u32();
    ParcelWireReader::require(count<=maximum_parcel_entries && count<=(bytes.size()-r.at)/36,"Invalid precision inventory count");
    std::vector<PrecisionParcel> result;result.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        PrecisionParcel p;p.source=r.u32();p.anchor=r.u32();p.radius=r.u32();p.native_index=r.u32();p.mass_kg=r.f64();p.temperature_k=r.f64();p.energy_j=r.sum();
        validate_precision_parcel(p);result.push_back(std::move(p));
    }
    ParcelWireReader::require(r.at==bytes.size(),"Trailing precision inventory bytes");
    // Reject noncanonical expansions, rather than silently normalizing saves.
    const auto canonical=encode_precision_inventory(result);
    ParcelWireReader::require(std::equal(bytes.begin(),bytes.end(),canonical.begin(),canonical.end()),"Noncanonical precision inventory");
    return result;
}
inline experimental::MacEnergy precision_potential(const experimental::MacClosedWorld& world,std::size_t anchor,double mass) {
    return experimental::MacEnergy::from_double(mass).multiplied(world.settings().gravity_m_per_s2)
        .multiplied((static_cast<double>(anchor/world.state().grid.width)+.5)*world.state().grid.spacing_m);
}
// A successful retry is an INTERNAL ownership move. Its exact grid credits
// debit this balance; callers must not book another external injection.
inline bool release_precision_parcel(experimental::MacClosedWorld& world,PrecisionParcel& held,
    std::size_t species,std::size_t anchor,std::span<const std::size_t> destinations) {
    using namespace experimental;
    for(const auto cell:destinations) {
        if(held.mass_kg>0) {
            // The core makes the same conservative scalar mass admission.
            // Avoid copying the entire colony for an immutable candidate that
            // cannot possibly advance this constituent's primary value.
            const double old=world.state().cells[cell].mixture.masses[species];
            double added=old+held.mass_kg;
            if(!std::isfinite(added) || added<=old)continue;
            MacEnergySum excess;excess.add(added);excess.add(-old);excess.add(-held.mass_kg);
            if(excess.sign()>0)added=std::nextafter(added,old);
            if(added<=old)continue;
            MacMovingParcel request;request.cell_index=cell;request.species_index=species;request.mass_kg=held.mass_kg;
            request.potential_energy_j=precision_potential(world,anchor,held.mass_kg);
            // The signed primary-write correction is NOT a material
            // temperature. Keep the admitted source thermoreference when a
            // much smaller mass remainder survives a partial credit.
            const auto& material=world.thermodynamics().reference().materials()[species];
            request.internal_energy_j=MacEnergy::from_double(held.mass_kg*
                (material.energy_offset_j_per_kg+material.cv_j_per_kg_k*held.temperature_k));
            auto result=world.with_moving_parcels(std::span(&request,1));
            if(!result.accepted())continue;
            const auto& credit=result.receipts.front();
            // The core's near-whole admission makes this subtraction exact
            // (Sterbenz). Verify it against the authoritative mass expansion.
            const double remaining=held.mass_kg-credit.consumed.mass_kg;
            MacEnergySum mass;mass.add(held.mass_kg);mass.add(-remaining);add_precision_sum(mass,credit.grid.mass_kg,-1);
            if(remaining<0 || mass.sign()!=0)continue;
            auto energy=held.energy_j;
            for(const auto* q:{&credit.grid.internal_energy_j,&credit.grid.kinetic_energy_j,&credit.grid.potential_energy_j})add_precision_sum(energy,*q,-1);
            // Stationary input has zero source momentum. Any wall impulse is
            // the existing core boundary law, not a hidden held momentum tail.
            auto jx=credit.grid.momentum_x,jy=credit.grid.momentum_y;
            jx.add(credit.wall_impulse_x.negated());jy.add(credit.wall_impulse_y.negated());
            if(jx.sign()!=0 || jy.sign()!=0)continue;
            world=std::move(result.world);held.mass_kg=remaining;held.energy_j=std::move(energy);return true;
        }
        if(held.energy_j.sign()==0)return false;
        // A primary energy write can round either way. Keep the signed
        // difference, committing only a strict reduction of its exact size.
        try {
            const double amount=held.energy_j.value().project().value;
            if(amount==0)continue;
            const double old=world.state().cells[cell].mixture.energy;
            if(old+amount==old)continue;
            const MacCellHeat heat{cell,amount,std::numeric_limits<double>::max()};
            auto result=world.with_cell_heat(std::span(&heat,1));
            MacEnergySum delta;delta.add(result.state().cells[cell].mixture.energy);delta.add(-world.state().cells[cell].mixture.energy);
            auto remaining=held.energy_j;add_precision_sum(remaining,delta,-1);
            auto reduction=held.energy_j;if(reduction.sign()<0){MacEnergySum positive;add_precision_sum(positive,reduction,-1);reduction=std::move(positive);}
            add_precision_sum(reduction,remaining,remaining.sign()<0?1:-1);
            if(delta.sign()==0 || reduction.sign()<=0)continue;
            world=std::move(result);held.energy_j=std::move(remaining);return true;
        } catch(const std::invalid_argument&) { /* Immutable refusal; retain every term. */ }
        catch(const std::domain_error&) { /* Recipient cannot accept this energy yet. */ }
    }
    return false;
}
}
