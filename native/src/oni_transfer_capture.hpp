#pragma once
// Opt-in failed-edit evidence. The world format matches the existing owner
// flow dumps; its separate request is little endian and preserves binary64.
#include "oni_precise_receipt.hpp"
#include "mixed_atmospheres/oni_colony.h"
#include <cstdio>
#include <span>
#include <string>

namespace ma::oni::colony::detail {
inline bool capture_transfer(const char* path,std::span<const MAOniColonyMaterial> materials,
    std::span<const std::uint8_t> world,double seconds,const experimental::MacFluidTransfer& edit,
    std::uint64_t frame,std::uint32_t flags) noexcept {
    if (!path || !*path) return false;
    try {
        PreciseReceiptWriter request;
        constexpr char magic[]="MATREQ01"; request.bytes.insert(request.bytes.end(),magic,magic+8);
        request.u64(frame); request.u32(flags); request.u32(1); // occupied_mixture
        request.u64(edit.cell_index); request.u64(edit.species_index);
        request.u32(edit.inject?1u:0u); request.u32(edit.observe_passives?1u:0u);
        request.f64(edit.mass_kg); request.f64(edit.temperature_k);
        request.u32(static_cast<std::uint32_t>(edit.passives.size()));
        for (const auto& q : edit.passives) { request.u64(q.identity); request.f64(q.quantity); }
        auto* file=std::fopen(path,"wbx"); if (!file) return false;
        const auto count=static_cast<std::uint32_t>(materials.size());
        bool ok=std::fwrite(&count,4,1,file)==1 && std::fwrite(materials.data(),sizeof(MAOniColonyMaterial),count,file)==count &&
            std::fwrite(&seconds,8,1,file)==1 && std::fwrite(world.data(),1,world.size(),file)==world.size();
        ok=std::fclose(file)==0 && ok;
        if (!ok) return false;
        const auto sidecar=std::string(path)+".request";
        file=std::fopen(sidecar.c_str(),"wbx"); if (!file) return false;
        ok=std::fwrite(request.bytes.data(),1,request.bytes.size(),file)==request.bytes.size();
        return std::fclose(file)==0 && ok;
    } catch (...) { return false; }
}
}
