#pragma once
// Original adapter wire format. Carries scaled mechanics without projecting
// them into binary64 or choosing a physical admission/rounding policy.
#include "mixed_atmospheres/mac_scaled.hpp"
#include "mixed_atmospheres/mac_energy.hpp"
#include "mixed_atmospheres/passive_quantities.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace ma::oni::colony::detail {
inline constexpr std::size_t maximum_parcel_wire_bytes=16u*1024u*1024u;
inline constexpr std::uint32_t maximum_parcel_entries=65536;
struct MovingParcelInput {
    std::uint32_t public_cell=0,native_index=0;
    double mass_kg=0;
    experimental::MacEnergy internal_energy_j,kinetic_energy_j,potential_energy_j;
    experimental::MacScaled momentum_x,momentum_y;
    PassiveQuantities passives;
};
struct ParcelWireReader {
    std::span<const std::uint8_t> bytes;std::size_t at=0;
    static void require(bool condition,const char* message) { if(!condition)throw std::invalid_argument(message); }
    std::span<const std::uint8_t> raw(std::size_t count) {
        require(count<=bytes.size()-at,"Truncated moving parcel wire");
        const auto result=bytes.subspan(at,count);at+=count;return result;
    }
    std::uint32_t u32() { const auto b=raw(4);std::uint32_t result=0;for(unsigned i=0;i<4;++i)result|=std::uint32_t(b[i])<<(8*i);return result; }
    std::uint64_t u64() { const auto b=raw(8);std::uint64_t result=0;for(unsigned i=0;i<8;++i)result|=std::uint64_t(b[i])<<(8*i);return result; }
    double f64() { return std::bit_cast<double>(u64()); }
    experimental::MacEnergy scaled() {
        const auto mantissa=f64();const auto count=u32();
        require(count>0 && count<=experimental::MacExponent::maximum_decimal_characters,"Invalid parcel exponent size");
        const auto b=raw(count);const std::string_view text(reinterpret_cast<const char*>(b.data()),b.size());
        experimental::MacExponent exponent;
        try { exponent=experimental::MacExponent::from_decimal(text); }
        catch(const FluidFatalError& error) {
            if(error.code()!=FluidFatalCode::numerical_precision)throw;
            throw std::invalid_argument("Parcel exponent exceeds the supported wire range");
        }
        require(exponent.to_string()==text,"Noncanonical parcel exponent");
        require(std::isfinite(mantissa) && (mantissa==0 ? std::bit_cast<std::uint64_t>(mantissa)==0 && exponent.sign()==0 :
            std::abs(mantissa)>=.5 && std::abs(mantissa)<1),"Noncanonical parcel significand");
        return experimental::MacEnergy::from_parts(mantissa,exponent);
    }
    experimental::MacEnergySum sum() {
        const auto count=u32();
        require(count<=65536 && count<=(bytes.size()-at)/13,"Invalid parcel credit component count");
        experimental::MacEnergySum result;
        for(std::uint32_t i=0;i<count;++i) {const auto value=scaled();require(!value.is_zero(),"Zero parcel credit component");result.add(value);}
        return result;
    }
};
inline std::vector<MovingParcelInput> decode_moving_parcels(std::span<const std::uint8_t> bytes) {
    using Reader=ParcelWireReader;
    Reader::require(bytes.size()>=12 && bytes.size()<=maximum_parcel_wire_bytes,"Invalid moving parcel wire size");
    Reader reader{bytes};const auto magic=reader.raw(8);
    Reader::require(std::string_view(reinterpret_cast<const char*>(magic.data()),8)=="MAPARC01","Invalid moving parcel magic");
    const auto count=reader.u32();
    Reader::require(count>0 && count<=maximum_parcel_entries && count<=(bytes.size()-12)/85,"Invalid moving parcel count");
    std::vector<MovingParcelInput> result;result.reserve(count);
    std::pair<std::uint32_t,std::uint32_t> previous{};
    for(std::uint32_t i=0;i<count;++i) {
        MovingParcelInput item;item.public_cell=reader.u32();item.native_index=reader.u32();item.mass_kg=reader.f64();
        const std::pair key{item.public_cell,item.native_index};
        Reader::require((i==0 || key>previous) && std::isfinite(item.mass_kg) && item.mass_kg>0,"Invalid or unordered moving parcel");
        previous=key;
        item.internal_energy_j=reader.scaled();item.kinetic_energy_j=reader.scaled();item.potential_energy_j=reader.scaled();
        item.momentum_x=reader.scaled();item.momentum_y=reader.scaled();
        Reader::require(item.kinetic_energy_j.mantissa()>=0,"Negative parcel kinetic energy");
        const auto passives=reader.u32();
        Reader::require(passives<=65537 && passives<=(bytes.size()-reader.at)/16,"Invalid moving parcel passive count");
        std::uint64_t last=0;item.passives.reserve(passives);
        for(std::uint32_t j=0;j<passives;++j) {
            const auto identity=reader.u64();const auto quantity=reader.f64();
            Reader::require(identity>last && std::isfinite(quantity) && quantity>0,"Invalid moving parcel passive");
            last=identity;item.passives.push_back({identity,quantity});
        }
        result.push_back(std::move(item));
    }
    Reader::require(reader.at==bytes.size(),"Trailing moving parcel bytes");
    return result;
}
struct ParcelWireWriter {
    std::vector<std::uint8_t> bytes;
    std::size_t limit=maximum_parcel_wire_bytes;
    void raw(std::string_view text) {
        ParcelWireReader::require(bytes.size()<=limit && text.size()<=limit-bytes.size(),"Moving parcel wire is too large");
        bytes.insert(bytes.end(),text.begin(),text.end());
    }
    void u32(std::uint32_t v) { for(unsigned i=0;i<4;++i) {const auto b=static_cast<char>(v>>(8*i));raw(std::string_view(&b,1));} }
    void u64(std::uint64_t v) { for(unsigned i=0;i<8;++i) {const auto b=static_cast<char>(v>>(8*i));raw(std::string_view(&b,1));} }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void scaled(const experimental::MacEnergy& value) {
        f64(value.mantissa());const auto exponent=value.exponent().to_string();
        u32(static_cast<std::uint32_t>(exponent.size()));raw(exponent);
    }
    void sum(const experimental::MacEnergySum& value) {
        ParcelWireReader::require(value.size()<=65536,"Too many parcel credit components");
        u32(static_cast<std::uint32_t>(value.size()));
        for(std::size_t i=0;i<value.size();++i)scaled(value.component(i));
    }
};
inline std::vector<std::uint8_t> encode_moving_parcels(std::span<const MovingParcelInput> items) {
    ParcelWireReader::require(!items.empty() && items.size()<=maximum_parcel_entries,"Invalid moving parcel count");
    ParcelWireWriter writer;writer.raw("MAPARC01");writer.u32(static_cast<std::uint32_t>(items.size()));
    for(const auto& item : items) {
        writer.u32(item.public_cell);writer.u32(item.native_index);writer.f64(item.mass_kg);
        writer.scaled(item.internal_energy_j);writer.scaled(item.kinetic_energy_j);writer.scaled(item.potential_energy_j);
        writer.scaled(item.momentum_x);writer.scaled(item.momentum_y);
        ParcelWireReader::require(item.passives.size()<=65537,"Invalid moving parcel passive count");
        writer.u32(static_cast<std::uint32_t>(item.passives.size()));
        for(const auto& passive : item.passives) {writer.u64(passive.identity);writer.f64(passive.quantity);}
    }
    // The same complete validation applies to native producers and callers.
    (void)decode_moving_parcels(writer.bytes);
    return std::move(writer.bytes);
}
}
