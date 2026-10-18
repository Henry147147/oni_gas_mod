#pragma once
#include "oni_actor_checkpoint.hpp"

namespace ma::oni::configured_domain {
// Physical identity of a configured (scenario 7) session. It wraps the
// unchanged MACCLS01 solver image with the exact caller geometry, physics
// record and solid mask; the MAC image alone omits phase admission. Legacy
// scenarios never produce or accept this wrapper. Durable actor images retain
// it as their opaque physical section, so their geometry check covers it too.
struct Image {
    MAOniDomainConfig geometry{};
    MAOniDomainPhysics physics{};
    std::vector<std::uint8_t> mask, physical;
};
constexpr std::array<std::uint8_t,8> magic{'M','A','D','C','F','G','0','1'};
constexpr std::size_t header_bytes=96,digest_bytes=64,maximum_bytes=256U<<20;
using actor_checkpoint::require;
inline bool has_magic(std::span<const std::uint8_t> value) {
    return value.size()>=magic.size() && std::equal(magic.begin(),magic.end(),value.begin());
}
inline void write_physics(actor_checkpoint::Writer& out,const MAOniDomainPhysics& p) {
    out.u32(p.struct_size);out.u32(p.abi_version);out.u32(p.flags);out.u32(p.reserved);
    out.number(p.gravity_m_per_s2);out.number(p.shear_viscosity_pa_s);out.number(p.bulk_viscosity_pa_s);
}
// Bitwise: -0 and +0, or two NaN payloads, are different configurations.
inline bool same_physics(const MAOniDomainPhysics& a,const MAOniDomainPhysics& b) {
    actor_checkpoint::Writer first,second;write_physics(first,a);write_physics(second,b);return first.bytes==second.bytes;
}
inline bool same_configuration(const Image& image,const MAOniDomainConfig& geometry,const MAOniDomainPhysics& physics,
    std::span<const std::uint8_t> mask) {
    return actor_checkpoint::same_geometry(image.geometry,geometry) && same_physics(image.physics,physics) &&
        std::equal(image.mask.begin(),image.mask.end(),mask.begin(),mask.end());
}
inline std::vector<std::uint8_t> encode(const Image& image) {
    const auto cells=static_cast<std::uint64_t>(image.geometry.width)*image.geometry.height;
    require(cells && cells<=MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS && image.mask.size()==cells && image.physical.size()>=32 &&
        image.physical.size()<=maximum_bytes-header_bytes-digest_bytes-image.mask.size(),"Configured domain image exceeds its supported bounds");
    require(std::all_of(image.mask.begin(),image.mask.end(),[](auto value) {return value<=1;}),"Configured domain mask is not binary");
    actor_checkpoint::Writer out;out.bytes.reserve(header_bytes+image.mask.size()+image.physical.size()+digest_bytes);
    out.bytes.insert(out.bytes.end(),magic.begin(),magic.end());
    out.u32(1);out.u32(static_cast<std::uint32_t>(cells));actor_checkpoint::write_geometry(out,image.geometry);
    // Header offsets 56..87: physics flags, reserved and three binary64 values.
    out.u32(image.physics.flags);out.u32(image.physics.reserved);
    out.number(image.physics.gravity_m_per_s2);out.number(image.physics.shear_viscosity_pa_s);out.number(image.physics.bulk_viscosity_pa_s);
    out.u32(static_cast<std::uint32_t>(image.physical.size()));out.u32(0);
    out.bytes.insert(out.bytes.end(),image.mask.begin(),image.mask.end());
    out.bytes.insert(out.bytes.end(),image.physical.begin(),image.physical.end());
    const auto digest=hash_memory(out.bytes.data(),out.bytes.size());out.bytes.insert(out.bytes.end(),digest.begin(),digest.end());
    return out.bytes;
}
inline Image decode(std::span<const std::uint8_t> bytes) {
    require(bytes.size()>=header_bytes+1+32+digest_bytes && bytes.size()<=maximum_bytes && has_magic(bytes),
        "Invalid configured domain image framing");
    const auto body=bytes.first(bytes.size()-digest_bytes);const auto digest=hash_memory(body.data(),body.size());
    require(std::equal(digest.begin(),digest.end(),bytes.end()-digest_bytes),"Configured domain image checksum differs");
    actor_checkpoint::Reader in{body,8};require(in.u32()==1,"Unsupported configured domain image version");
    const auto cells=in.u32();Image result;result.geometry=actor_checkpoint::read_geometry(in);
    result.physics.struct_size=sizeof(MAOniDomainPhysics);result.physics.abi_version=MA_ONI_DOMAIN_CONFIGURED_ABI_VERSION;
    result.physics.flags=in.u32();result.physics.reserved=in.u32();
    result.physics.gravity_m_per_s2=in.number();result.physics.shear_viscosity_pa_s=in.number();result.physics.bulk_viscosity_pa_s=in.number();
    const auto physical_bytes=in.u32();
    require(in.u32()==0 && in.position==header_bytes && cells && cells<=MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS &&
        static_cast<std::uint64_t>(result.geometry.width)*result.geometry.height==cells && physical_bytes>=32 &&
        header_bytes+static_cast<std::uint64_t>(cells)+physical_bytes==body.size(),"Configured domain image sizes or reserved fields differ");
    result.mask.assign(body.begin()+header_bytes,body.begin()+header_bytes+cells);
    require(std::all_of(result.mask.begin(),result.mask.end(),[](auto value) {return value<=1;}),"Configured domain mask is not binary");
    result.physical.assign(body.begin()+header_bytes+cells,body.end());return result;
}
}
