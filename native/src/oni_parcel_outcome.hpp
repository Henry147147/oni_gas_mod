#pragma once
#include "oni_parcel_wire.hpp"
#include <string>

namespace ma::oni::colony::detail {
inline constexpr std::size_t maximum_parcel_outcome_bytes=64u*1024u*1024u;
struct ParcelPassiveCredit {std::uint64_t identity=0;experimental::MacEnergySum quantity;};
struct ParcelGridCredit {
    experimental::MacEnergySum mass_kg,internal_energy_j,kinetic_energy_j,potential_energy_j,momentum_x,momentum_y;
    std::vector<ParcelPassiveCredit> passives;
    experimental::MacEnergySum kinetic_mixing_heat_j,potential_work_j;
    experimental::MacScaled wall_impulse_x,wall_impulse_y;
    PassiveQuantities passives_before,passives_after;
};
struct MovingParcelOutcome {
    std::uint64_t sequence=0,frame=0,token=0,session=0;
    // Core refusal codes0..4;16 native publication range;17 receipt capacity.
    std::uint32_t refusal=0;std::string reason;
    std::vector<MovingParcelInput> consumed;
    std::vector<ParcelGridCredit> grid;
    experimental::MacEnergy energy_residual_j,energy_bound_j;
    experimental::MacScaled momentum_x_residual,momentum_x_bound,momentum_y_residual,momentum_y_bound;
};
inline void write_parcel_passives(ParcelWireWriter& writer,std::span<const PassiveQuantity> values) {
    ParcelWireReader::require(values.size()<=65537,"Too many observed parcel passives");
    writer.u32(static_cast<std::uint32_t>(values.size()));std::uint64_t last=0;
    for(const auto& value : values) {
        ParcelWireReader::require(value.identity>last && std::isfinite(value.quantity) && value.quantity>0,"Invalid observed parcel passive");
        writer.u64(value.identity);writer.f64(value.quantity);last=value.identity;
    }
}
inline PassiveQuantities read_parcel_passives(ParcelWireReader& reader) {
    const auto count=reader.u32();
    ParcelWireReader::require(count<=65537 && count<=(reader.bytes.size()-reader.at)/16,"Invalid observed passive count");
    PassiveQuantities result;result.reserve(count);std::uint64_t last=0;
    for(std::uint32_t i=0;i<count;++i) {
        const auto identity=reader.u64();const auto quantity=reader.f64();
        ParcelWireReader::require(identity>last && std::isfinite(quantity) && quantity>0,"Invalid observed parcel passive");
        result.push_back({identity,quantity});last=identity;
    }
    return result;
}
inline MovingParcelOutcome decode_moving_outcome(std::span<const std::uint8_t> bytes) {
    using Reader=ParcelWireReader;
    Reader::require(bytes.size()>=134 && bytes.size()<=maximum_parcel_outcome_bytes,"Invalid moving outcome size");
    Reader reader{bytes};const auto magic=reader.raw(8);
    Reader::require(std::string_view(reinterpret_cast<const char*>(magic.data()),8)=="MAPOUT01","Invalid moving outcome magic");
    MovingParcelOutcome result;result.sequence=reader.u64();result.frame=reader.u64();result.token=reader.u64();result.session=reader.u64();
    Reader::require(result.sequence && result.frame && result.token && result.session,"Invalid moving outcome identity");
    result.refusal=reader.u32();const auto reason_size=reader.u32();
    Reader::require((result.refusal<=4 || result.refusal==16 || result.refusal==17) && reason_size<=4096,"Invalid moving outcome refusal");
    const auto reason=reader.raw(reason_size);result.reason.assign(reinterpret_cast<const char*>(reason.data()),reason.size());
    const auto input_size=reader.u32();
    Reader::require(input_size<=maximum_parcel_wire_bytes,"Moving outcome consumed payload is too large");
    if(input_size)result.consumed=decode_moving_parcels(reader.raw(input_size));
    const auto count=reader.u32();
    Reader::require(count<=maximum_parcel_entries && count==result.consumed.size() &&
        (result.refusal ? count==0 && !result.reason.empty() : count>0),"Moving outcome status/credit count disagrees");
    result.grid.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        ParcelGridCredit credit;
        credit.mass_kg=reader.sum();credit.internal_energy_j=reader.sum();credit.kinetic_energy_j=reader.sum();
        credit.potential_energy_j=reader.sum();credit.momentum_x=reader.sum();credit.momentum_y=reader.sum();
        const auto passive_count=reader.u32();Reader::require(passive_count<=65537 && passive_count<=(bytes.size()-reader.at)/12,"Invalid credited passive count");
        credit.passives.reserve(passive_count);std::uint64_t last=0;
        for(std::uint32_t j=0;j<passive_count;++j) {
            const auto identity=reader.u64();Reader::require(identity>last,"Unordered credited passive");
            credit.passives.push_back({identity,reader.sum()});last=identity;
        }
        credit.kinetic_mixing_heat_j=reader.sum();credit.potential_work_j=reader.sum();
        credit.wall_impulse_x=reader.scaled();credit.wall_impulse_y=reader.scaled();
        credit.passives_before=read_parcel_passives(reader);credit.passives_after=read_parcel_passives(reader);
        result.grid.push_back(std::move(credit));
    }
    result.energy_residual_j=reader.scaled();result.energy_bound_j=reader.scaled();
    result.momentum_x_residual=reader.scaled();result.momentum_x_bound=reader.scaled();
    result.momentum_y_residual=reader.scaled();result.momentum_y_bound=reader.scaled();
    Reader::require(result.energy_bound_j.mantissa()>=0 && result.momentum_x_bound.mantissa()>=0 &&
        result.momentum_y_bound.mantissa()>=0 && reader.at==bytes.size(),"Invalid moving outcome bound or trailing bytes");
    return result;
}
inline std::vector<std::uint8_t> encode_moving_outcome(const MovingParcelOutcome& outcome) {
    ParcelWireWriter writer;writer.limit=maximum_parcel_outcome_bytes;writer.raw("MAPOUT01");
    writer.u64(outcome.sequence);writer.u64(outcome.frame);writer.u64(outcome.token);writer.u64(outcome.session);writer.u32(outcome.refusal);
    ParcelWireReader::require(outcome.reason.size()<=4096,"Moving refusal reason is too large");
    writer.u32(static_cast<std::uint32_t>(outcome.reason.size()));writer.raw(outcome.reason);
    if(outcome.consumed.empty())writer.u32(0);
    else {
        const auto input=encode_moving_parcels(outcome.consumed);writer.u32(static_cast<std::uint32_t>(input.size()));
        writer.raw(std::string_view(reinterpret_cast<const char*>(input.data()),input.size()));
    }
    ParcelWireReader::require(outcome.grid.size()<=maximum_parcel_entries,"Too many moving outcome credits");
    writer.u32(static_cast<std::uint32_t>(outcome.grid.size()));
    for(const auto& credit : outcome.grid) {
        for(const auto* value : {&credit.mass_kg,&credit.internal_energy_j,&credit.kinetic_energy_j,&credit.potential_energy_j,&credit.momentum_x,&credit.momentum_y})writer.sum(*value);
        ParcelWireReader::require(credit.passives.size()<=65537,"Too many credited passives");
        writer.u32(static_cast<std::uint32_t>(credit.passives.size()));std::uint64_t last=0;
        for(const auto& passive : credit.passives) {
            ParcelWireReader::require(passive.identity>last,"Unordered credited passive");
            writer.u64(passive.identity);writer.sum(passive.quantity);last=passive.identity;
        }
        writer.sum(credit.kinetic_mixing_heat_j);writer.sum(credit.potential_work_j);
        writer.scaled(credit.wall_impulse_x);writer.scaled(credit.wall_impulse_y);
        write_parcel_passives(writer,credit.passives_before);write_parcel_passives(writer,credit.passives_after);
    }
    for(const auto* value : {&outcome.energy_residual_j,&outcome.energy_bound_j,&outcome.momentum_x_residual,&outcome.momentum_x_bound,&outcome.momentum_y_residual,&outcome.momentum_y_bound})writer.scaled(*value);
    (void)decode_moving_outcome(writer.bytes);
    return std::move(writer.bytes);
}
}
