#pragma once
#include "mixed_atmospheres/oni_domain.h"
#include "oni_sha256.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace ma::oni::actor_checkpoint {
// Durable identities deliberately contain no native handle, callback index,
// session, thread or frame. The managed owner must remap those after loading.
struct Identity {
    std::uint64_t owner{};
    std::uint32_t prefab{}, component{};
    auto operator<=>(const Identity&) const = default;
};
struct State {
    Identity identity;
    std::uint32_t kind{}, cell{}, species{};
    double mass{}, interval{}, temperature{}, maximum_density{}, elapsed{}, remainder{};
    bool blocked{};
    auto key() const { return std::tuple(kind,identity); }
};
struct Image {
    MAOniDomainConfig geometry{};
    std::vector<State> actors;
    std::vector<std::uint8_t> physical;
};
constexpr std::array<std::uint8_t,8> magic{'M','A','A','C','T','R','0','1'};
constexpr std::size_t header_bytes=64, record_bytes=88, digest_bytes=64;
constexpr std::size_t maximum_bytes=256U<<20;
inline void require(bool condition,const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
inline bool has_magic(std::span<const std::uint8_t> value) {
    return value.size()>=magic.size() && std::equal(magic.begin(),magic.end(),value.begin());
}
inline void validate(const State& value) {
    require(value.identity.owner && value.identity.prefab && value.identity.component<=65535 &&
        (value.kind==1 || value.kind==2) && value.species<MA_ONI_DOMAIN_SPECIES,
        "Invalid durable actor identity, kind or species");
    for (auto number : {value.mass,value.interval,value.temperature,value.maximum_density,value.elapsed,value.remainder})
        require(std::isfinite(number),"Nonfinite durable actor state");
    require(value.mass>=0 && value.elapsed>=0 && value.remainder>=0,"Negative durable actor rate or timing state");
    if (value.kind==1) require(value.elapsed==0 && !value.blocked,"Consumer has emitter timing or blocked state");
    else {
        require(value.remainder==0,"Emitter has a consumer rounding remainder");
        if (value.mass>0) require(value.interval>0 && value.temperature>=1 && value.maximum_density>0 &&
            value.mass==static_cast<double>(static_cast<float>(value.mass)),"Invalid durable emitter configuration");
    }
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void u32(std::uint32_t value) { for (unsigned i=0;i<4;++i) bytes.push_back(static_cast<std::uint8_t>(value>>(8*i))); }
    void u64(std::uint64_t value) { for (unsigned i=0;i<8;++i) bytes.push_back(static_cast<std::uint8_t>(value>>(8*i))); }
    void number(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position{};
    std::uint64_t integer(unsigned count) {
        require(position<=bytes.size() && count<=bytes.size()-position,"Truncated durable actor image");
        std::uint64_t value=0;
        for (unsigned i=0;i<count;++i) value|=static_cast<std::uint64_t>(bytes[position++])<<(8*i);
        return value;
    }
    std::uint32_t u32() { return static_cast<std::uint32_t>(integer(4)); }
    std::uint64_t u64() { return integer(8); }
    double number() { return std::bit_cast<double>(u64()); }
};
inline void write_geometry(Writer& out,const MAOniDomainConfig& g) {
    for (auto v : {g.struct_size,g.abi_version,g.flags,g.native_grid_width,g.offset_x,g.offset_y,g.width,g.height,g.scenario,g.reserved}) out.u32(v);
}
inline MAOniDomainConfig read_geometry(Reader& in) {
    return {in.u32(),in.u32(),in.u32(),in.u32(),in.u32(),in.u32(),in.u32(),in.u32(),in.u32(),in.u32()};
}
inline bool same_geometry(const MAOniDomainConfig& a,const MAOniDomainConfig& b) {
    Writer first,second; write_geometry(first,a); write_geometry(second,b); return first.bytes==second.bytes;
}
inline std::vector<std::uint8_t> encode(const Image& image) {
    require(!image.actors.empty() && image.actors.size()<=256 && image.physical.size()>=32 &&
        image.physical.size()<=maximum_bytes-header_bytes-digest_bytes-record_bytes*image.actors.size(),
        "Durable actor image exceeds its supported bounds");
    auto actors=image.actors;
    std::sort(actors.begin(),actors.end(),[](const auto& a,const auto& b) { return a.key()<b.key(); });
    Writer out; out.bytes.insert(out.bytes.end(),magic.begin(),magic.end());
    out.u32(1);out.u32(static_cast<std::uint32_t>(actors.size()));write_geometry(out,image.geometry);
    out.u32(static_cast<std::uint32_t>(image.physical.size()));out.u32(0);
    for (std::size_t i=0;i<actors.size();++i) {
        const auto& a=actors[i];validate(a);
        require(i==0 || actors[i-1].key()<a.key(),"Repeated durable actor identity");
        out.u64(a.identity.owner);out.u32(a.identity.prefab);out.u32(a.identity.component);
        out.u32(a.kind);out.u32(a.cell);out.u32(a.species);out.u32(0);
        for (auto v : {a.mass,a.interval,a.temperature,a.maximum_density,a.elapsed,a.remainder}) out.number(v);
        out.u32(a.blocked ? 1 : 0);out.u32(0);
    }
    out.bytes.insert(out.bytes.end(),image.physical.begin(),image.physical.end());
    const auto hash=hash_memory(out.bytes.data(),out.bytes.size());out.bytes.insert(out.bytes.end(),hash.begin(),hash.end());
    return out.bytes;
}
inline Image decode(std::span<const std::uint8_t> bytes) {
    require(bytes.size()>=header_bytes+record_bytes+32+digest_bytes && bytes.size()<=maximum_bytes && has_magic(bytes),
        "Invalid durable actor image framing");
    const auto body=bytes.first(bytes.size()-digest_bytes);
    const auto hash=hash_memory(body.data(),body.size());
    require(std::equal(hash.begin(),hash.end(),bytes.end()-digest_bytes),"Durable actor image checksum differs");
    Reader in{body,8};require(in.u32()==1,"Unsupported durable actor image version");
    const auto count=in.u32();require(count && count<=256,"Invalid durable actor count");
    Image result;result.geometry=read_geometry(in);const auto physical_bytes=in.u32();
    require(in.u32()==0 && physical_bytes>=32 && header_bytes+record_bytes*count+physical_bytes==body.size(),
        "Durable actor image sizes or reserved fields differ");
    result.actors.reserve(count);
    for (std::uint32_t index=0;index<count;++index) {
        State a;a.identity={in.u64(),in.u32(),in.u32()};a.kind=in.u32();a.cell=in.u32();a.species=in.u32();
        require(in.u32()==0,"Durable actor record has reserved flags");
        a.mass=in.number();a.interval=in.number();a.temperature=in.number();a.maximum_density=in.number();a.elapsed=in.number();a.remainder=in.number();
        const auto blocked=in.u32();require(blocked<=1 && in.u32()==0,"Invalid durable actor blocked state or reserved flags");a.blocked=blocked!=0;
        validate(a);require(result.actors.empty() || result.actors.back().key()<a.key(),"Durable actor identities are repeated or unordered");
        result.actors.push_back(a);
    }
    result.physical.assign(body.begin()+in.position,body.end());return result;
}
}
