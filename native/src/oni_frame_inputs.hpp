#pragma once
#include "oni_actor_checkpoint.hpp"
#include <string>

namespace ma::oni::frame_inputs {
// An observation of accepted game inputs, not authority to alter a live owner.
// Preserve batch boundaries: a heat batch performs one physical revision.
struct Batch {
    std::vector<MAOniDomainHeat> heat;
    std::vector<MAOniDomainTransfer> transfers;
};
struct Image {
    std::uint32_t flags{}; // 1: actor configuration, 2: non-durable actors
    std::uint64_t session{},source_frame{},completed_frame{};
    double source_time{},seconds{};
    MAOniDomainConfig geometry{};
    std::string source_sha256;
    std::vector<Batch> batches;
};
constexpr std::array<std::uint8_t,8> magic{'M','A','F','R','I','N','0','1'};
constexpr std::size_t header_bytes=168,digest_bytes=64,maximum_bytes=1U<<20,maximum_batches=2048;
using actor_checkpoint::require;
inline std::size_t batch_size(const Batch& batch) {
    require(batch.heat.empty()!=batch.transfers.empty(),"Frame input batch must have exactly one command kind");
    const auto count=batch.heat.empty() ? batch.transfers.size() : batch.heat.size();
    require(count>0 && count<=256,"Frame input batch count exceeds its ABI");
    return 8+count*(batch.heat.empty() ? 64 : 48);
}
inline void validate(const Image& image) {
    require((image.flags&~3U)==0 && image.session && image.source_frame<UINT64_MAX &&
        image.completed_frame==image.source_frame+1 && std::isfinite(image.source_time) && image.source_time>=0 &&
        std::isfinite(image.seconds) && image.seconds>0 && image.seconds<=1,"Invalid frame input epoch, flags or clock");
    require(image.source_sha256.size()==64 && std::all_of(image.source_sha256.begin(),image.source_sha256.end(),
        [](char c) {return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid frame input source digest");
    require(image.batches.size()<=maximum_batches,"Frame input journal has too many batches");
    std::size_t bytes=header_bytes+digest_bytes,heat_count=0,transfer_count=0;
    for (const auto& batch:image.batches) {
        const auto size=batch_size(batch);require(size<=maximum_bytes-bytes,"Frame input journal exceeds its byte limit");bytes+=size;
        heat_count+=batch.heat.size();transfer_count+=batch.transfers.size();
        require(heat_count<=4096 && transfer_count<=4096,"Frame input command count exceeds its live reservation bounds");
        for (const auto& h:batch.heat)
            require(h.struct_size==48 && h.abi_version==1 && h.session==image.session && h.expected_frame==image.source_frame,
                "Frame heat command differs from its source epoch or ABI");
        for (const auto& t:batch.transfers)
            require(t.struct_size==64 && t.abi_version==MA_ONI_DOMAIN_TRANSFER_ABI_VERSION &&
                t.session==image.session && t.expected_frame==image.source_frame,"Frame transfer differs from its source epoch or ABI");
    }
}
inline std::vector<std::uint8_t> encode(const Image& image) {
    validate(image);actor_checkpoint::Writer out;out.bytes.insert(out.bytes.end(),magic.begin(),magic.end());
    out.u32(1);out.u32(image.flags);out.u64(image.session);out.u64(image.source_frame);out.u64(image.completed_frame);
    out.number(image.source_time);out.number(image.seconds);actor_checkpoint::write_geometry(out,image.geometry);
    out.u32(static_cast<std::uint32_t>(image.batches.size()));out.u32(0);
    out.bytes.insert(out.bytes.end(),image.source_sha256.begin(),image.source_sha256.end());
    for (const auto& batch:image.batches) {
        out.u32(batch.heat.empty() ? 2U : 1U);out.u32(static_cast<std::uint32_t>(batch.heat.empty() ? batch.transfers.size() : batch.heat.size()));
        for (const auto& h:batch.heat) {
            out.u32(h.struct_size);out.u32(h.abi_version);out.u64(h.session);out.u64(h.expected_frame);out.u32(h.public_cell);out.u32(h.source);
            out.number(h.joules);out.number(h.maximum_temperature_k);
        }
        for (const auto& t:batch.transfers) {
            out.u32(t.struct_size);out.u32(t.abi_version);out.u64(t.session);out.u64(t.expected_frame);
            out.u32(t.public_cell);out.u32(t.species);out.u32(t.kind);out.u32(t.flags);
            out.u32(std::bit_cast<std::uint32_t>(t.callback));out.u32(t.reserved);out.number(t.mass_kg);out.number(t.temperature_k);
        }
    }
    const auto digest=hash_memory(out.bytes.data(),out.bytes.size());out.bytes.insert(out.bytes.end(),digest.begin(),digest.end());return out.bytes;
}
inline Image decode(std::span<const std::uint8_t> bytes) {
    require(bytes.size()>=header_bytes+digest_bytes && bytes.size()<=maximum_bytes && std::equal(magic.begin(),magic.end(),bytes.begin()),
        "Invalid frame input journal framing");
    const auto body=bytes.first(bytes.size()-digest_bytes);const auto digest=hash_memory(body.data(),body.size());
    require(std::equal(digest.begin(),digest.end(),bytes.end()-digest_bytes),"Frame input journal checksum differs");
    actor_checkpoint::Reader in{body,8};require(in.u32()==1,"Unsupported frame input journal version");
    Image result;result.flags=in.u32();result.session=in.u64();result.source_frame=in.u64();result.completed_frame=in.u64();
    result.source_time=in.number();result.seconds=in.number();result.geometry=actor_checkpoint::read_geometry(in);
    const auto count=in.u32();require(count<=maximum_batches && in.u32()==0,"Invalid frame input batch count or reserved word");
    result.source_sha256.assign(body.begin()+104,body.begin()+header_bytes);in.position=header_bytes;
    for (std::uint32_t i=0;i<count;++i) {
        const auto kind=in.u32(),commands=in.u32();
        require((kind==1 || kind==2) && commands>0 && commands<=256,"Invalid frame input batch kind or command count");
        require(in.position<=body.size() && commands<=(body.size()-in.position)/(kind==1 ? 48U : 64U),"Truncated frame input batch");
        Batch batch;
        for (std::uint32_t j=0;j<commands;++j) {
            if (kind==1) {
                MAOniDomainHeat h{};h.struct_size=in.u32();h.abi_version=in.u32();h.session=in.u64();h.expected_frame=in.u64();
                h.public_cell=in.u32();h.source=in.u32();h.joules=in.number();h.maximum_temperature_k=in.number();batch.heat.push_back(h);
            } else {
                MAOniDomainTransfer t{};t.struct_size=in.u32();t.abi_version=in.u32();t.session=in.u64();t.expected_frame=in.u64();
                t.public_cell=in.u32();t.species=in.u32();t.kind=in.u32();t.flags=in.u32();t.callback=std::bit_cast<std::int32_t>(in.u32());
                t.reserved=in.u32();t.mass_kg=in.number();t.temperature_k=in.number();batch.transfers.push_back(t);
            }
        }
        result.batches.push_back(std::move(batch));
    }
    require(in.position==body.size(),"Frame input journal has trailing bytes");validate(result);return result;
}
inline std::vector<Batch> appended(const std::vector<Batch>& current,Batch batch) {
    require(current.size()<maximum_batches,"Pending frame input journal has too many batches");
    std::size_t bytes=header_bytes+digest_bytes+batch_size(batch);
    for (const auto& item:current) {const auto size=batch_size(item);require(size<=maximum_bytes-bytes,"Pending frame input byte limit exceeded");bytes+=size;}
    auto result=current;result.push_back(std::move(batch));return result;
}
}
