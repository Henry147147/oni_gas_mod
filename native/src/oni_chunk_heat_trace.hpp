#pragma once

// Private, optional observation of the pinned Linux744825 ElementChunk
// registry and its absolute-temperature messages. The existing ProcessFrame
// hook supplies both endpoints; no new hook or native state write is added.
#include "oni_body_heat_trace.hpp"
#include <limits>

namespace ma::oni::chunk_heat_trace {
namespace detail=ma::oni::body_heat_trace::detail;

struct Chunk {
    std::uint32_t temperature_bits=0,capacity_bits=0;
    std::int32_t cell=-1;
    bool valid=false;
};

inline Chunk chunk(const void* sim,std::int32_t handle) noexcept {
    Chunk result;
#if defined(__linux__)
    if(!sim || (static_cast<std::uint32_t>(handle)&0x00f00000U))return result;
    const auto index=static_cast<std::uint32_t>(handle)&0x00ffffffU;
    const auto generation=static_cast<std::uint32_t>(handle)>>24;
    const auto* generations=detail::read<const std::uint8_t*>(sim,0x3f8);
    const auto* generation_end=detail::read<const std::uint8_t*>(sim,0x400);
    if(index>=detail::count(generations,generation_end,1,1U<<20) || generations[index]!=generation)return result;
    const auto* mapping=detail::read<const std::int32_t*>(sim,0x3e0);
    const auto* mapping_end=detail::read<const std::int32_t*>(sim,0x3e8);
    if(index>=detail::count(mapping,mapping_end,4,1U<<20) || mapping[index]<0)return result;
    const auto* data=detail::read<const std::byte*>(sim,0x398);
    const auto* data_end=detail::read<const std::byte*>(sim,0x3a0);
    if(static_cast<std::size_t>(mapping[index])>=detail::count(data,data_end,44,1U<<20))return result;
    const auto* record=data+static_cast<std::size_t>(mapping[index])*44;
    result.cell=detail::read<std::int32_t>(record,0);
    result.temperature_bits=detail::read<std::uint32_t>(record,4);
    result.capacity_bits=detail::read<std::uint32_t>(record,8);result.valid=true;
#else
    (void)sim;(void)handle;
#endif
    return result;
}

class Frame {
    struct Message {
        std::int32_t handle;
        std::uint32_t temperature,capacity,moves,energy,adjusters,removals;
        Chunk before;
    };
    std::uint64_t ordinal_;
    const void* sim_;
    std::uint32_t registrations_=0;
    std::vector<Message> messages_;
    static const char* path() noexcept {
        static const auto* setting=std::getenv("MA_ONI_CHUNK_HEAT_TRACE");return setting;
    }
    // A malformed vector is explicitly marked, never reported as no work.
    static std::uint32_t count_messages(const void* frame,std::size_t offset,std::size_t stride,
        const std::int32_t* handle=nullptr) noexcept {
        const auto* first=detail::read<const std::byte*>(frame,offset);
        const auto* last=detail::read<const std::byte*>(frame,offset+8);
        if(first==last)return 0;
        const auto n=detail::count(first,last,stride,65536);
        if(!n)return std::numeric_limits<std::uint32_t>::max();
        if(!handle)return static_cast<std::uint32_t>(n);
        std::uint32_t result=0;
        for(std::size_t i=0;i<n;++i)if(detail::read<std::int32_t>(first+i*stride,0)==*handle)++result;
        return result;
    }
    static std::FILE* file() noexcept {
        static std::FILE* output=[] {
            auto* opened=ma::oni::capture_file(path(),false);
            if(opened)std::fputs("native_frame\tmessage_index\tchunk_handle\trequested_k_bits\trequested_capacity_kj_k_bits\tbefore_k_bits\tbefore_capacity_kj_k_bits\tafter_k_bits\tafter_capacity_kj_k_bits\tbefore_cell\tafter_cell\tvalid\tframe_registrations\tmoves\tenergy_messages\tadjuster_messages\tremovals\n",opened);
            return opened;
        }();
        return output;
    }
public:
    static bool enabled() noexcept {
#if defined(__linux__)
        return path()!=nullptr && *path()!=0;
#else
        return false;
#endif
    }
    Frame(std::uint64_t ordinal,const void* frame,const void* sim) noexcept:ordinal_(ordinal),sim_(sim) {
        detail::Preserve preserve;
        try {
            if(!frame || !sim)return;
            registrations_=count_messages(frame,0x230,32);
            const auto* first=detail::read<const std::byte*>(frame,0x248);
            const auto* last=detail::read<const std::byte*>(frame,0x250);
            const auto n=detail::count(first,last,12,65536);messages_.reserve(n);
            for(std::size_t i=0;i<n;++i) {
                const auto* record=first+i*12;const auto handle=detail::read<std::int32_t>(record,0);
                messages_.push_back({handle,detail::read<std::uint32_t>(record,4),detail::read<std::uint32_t>(record,8),
                    count_messages(frame,0x278,8,&handle),count_messages(frame,0x290,8,&handle),
                    count_messages(frame,0x2a8,16,&handle),count_messages(frame,0x260,8,&handle),chunk(sim,handle)});
            }
        }catch(...) {messages_.clear();}
    }
    ~Frame() {
        detail::Preserve preserve;
        if(auto* output=file()) {
            std::size_t index=0;
            for(const auto& message:messages_) {
                const auto after=chunk(sim_,message.handle);
                std::fprintf(output,"%llu\t%zu\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\n",
                    static_cast<unsigned long long>(ordinal_),index++,message.handle,message.temperature,message.capacity,
                    message.before.temperature_bits,message.before.capacity_bits,after.temperature_bits,after.capacity_bits,
                    message.before.cell,after.cell,(message.before.valid?1U:0U)|(after.valid?2U:0U),registrations_,
                    message.moves,message.energy,message.adjusters,message.removals);
            }
            (void)std::fflush(output);
        }
        std::vector<Message>().swap(messages_);
    }
    Frame(const Frame&)=delete;Frame& operator=(const Frame&)=delete;
};
}
