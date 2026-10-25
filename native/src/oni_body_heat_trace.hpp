#pragma once
#include "oni_capture_file.hpp"

// Optional observation of the pinned Linux744825 frame-message boundary.
// Observe the actual native capacity, rather than reconstructing it from a
// prefab definition. No message, body, solver state or floating-point state
// is changed. This runs inside the backend's already validated frame hook.
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(__linux__)
#include <xmmintrin.h>
#endif

namespace ma::oni::body_heat_trace {

struct Body {
    std::uint32_t temperature_bits=0, capacity_per_cell_bits=0;
    std::int32_t area=0;
    bool valid=false;
};

namespace detail {
template<class T> T read(const void* source,std::size_t offset) noexcept {
    T result{};std::memcpy(&result,static_cast<const std::byte*>(source)+offset,sizeof(result));return result;
}
inline std::size_t count(const void* first,const void* last,std::size_t stride,std::size_t maximum) noexcept {
    const auto begin=reinterpret_cast<std::uintptr_t>(first),end=reinterpret_cast<std::uintptr_t>(last);
    if(!begin || end<begin || (end-begin)%stride || (end-begin)/stride>maximum)return 0;
    return (end-begin)/stride;
}
struct Preserve {
    std::fenv_t environment{};int error=errno;bool saved=false;
#if defined(__linux__)
    unsigned csr=_mm_getcsr();
#endif
    Preserve() noexcept {saved=std::fegetenv(&environment)==0;}
    ~Preserve() {
        if(saved)(void)std::fesetenv(&environment);
#if defined(__linux__)
        _mm_setcsr(csr);
#endif
        errno=error;
    }
};
}

// The backend verifies the whole native image and every original hook entry
// before this layout is reachable. These are native ABI views, never owners.
inline Body body(const void* sim,std::int32_t handle) noexcept {
    Body result;
#if defined(__linux__)
    if(!sim || (static_cast<std::uint32_t>(handle)&0x00f00000U))return result;
    const auto index=static_cast<std::uint32_t>(handle)&0x00ffffffU;
    const auto generation=static_cast<std::uint32_t>(handle)>>24;
    const auto* generations=detail::read<const std::uint8_t*>(sim,0x498);
    const auto* generation_end=detail::read<const std::uint8_t*>(sim,0x4a0);
    if(index>=detail::count(generations,generation_end,1,1U<<20) || generations[index]!=generation)return result;
    const auto* mapping=detail::read<const std::int32_t*>(sim,0x480);
    const auto* mapping_end=detail::read<const std::int32_t*>(sim,0x488);
    if(index>=detail::count(mapping,mapping_end,4,1U<<20) || mapping[index]<0)return result;
    const auto* data=detail::read<const std::byte*>(sim,0x438);
    const auto* data_end=detail::read<const std::byte*>(sim,0x440);
    if(static_cast<std::size_t>(mapping[index])>=detail::count(data,data_end,44,1U<<20))return result;
    const auto* record=data+static_cast<std::size_t>(mapping[index])*44;
    const auto width=static_cast<std::int64_t>(detail::read<std::int32_t>(record,36))-detail::read<std::int32_t>(record,28);
    const auto height=static_cast<std::int64_t>(detail::read<std::int32_t>(record,40))-detail::read<std::int32_t>(record,32);
    if(width<=0 || height<=0 || width>1048576 || height>1048576 || width*height>1048576)return result;
    result.temperature_bits=detail::read<std::uint32_t>(record,0);
    result.capacity_per_cell_bits=detail::read<std::uint32_t>(record,16);
    result.area=static_cast<std::int32_t>(width*height);result.valid=true;
#else
    (void)sim;(void)handle;
#endif
    return result;
}

class Frame {
    struct Message {std::int32_t handle;std::uint32_t energy,minimum,maximum;Body before;};
    std::uint64_t ordinal_;
    const void* sim_;
    std::vector<Message> messages_;
    static const char* path() noexcept {
        static const auto* setting=std::getenv("MA_ONI_BODY_HEAT_TRACE");return setting;
    }
    static std::FILE* file() noexcept {
        static std::FILE* output=[] {
            auto* opened=ma::oni::capture_file(path(),false);
            if(opened)std::fputs("native_frame\tbody_handle\tdelta_kj_bits\tminimum_k_bits\tmaximum_k_bits\tbefore_k_bits\tafter_k_bits\tcapacity_per_cell_kj_k_bits\tarea\tvalid\n",opened);
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
            const auto* first=detail::read<const std::byte*>(frame,0x1b8);
            const auto* last=detail::read<const std::byte*>(frame,0x1c0);
            const auto n=detail::count(first,last,16,4096);messages_.reserve(n);
            for(std::size_t i=0;i<n;++i) {
                const auto* record=first+i*16;const auto handle=detail::read<std::int32_t>(record,0);
                messages_.push_back({handle,detail::read<std::uint32_t>(record,4),detail::read<std::uint32_t>(record,8),
                    detail::read<std::uint32_t>(record,12),body(sim,handle)});
            }
        }catch(...) {messages_.clear();}
    }
    ~Frame() {
        detail::Preserve preserve;
        if(auto* output=file()) {
            for(const auto& message:messages_) {
                const auto after=body(sim_,message.handle);
                std::fprintf(output,"%llu\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%d\t%u\n",
                    static_cast<unsigned long long>(ordinal_),message.handle,message.energy,message.minimum,message.maximum,
                    message.before.temperature_bits,after.temperature_bits,message.before.capacity_per_cell_bits,message.before.area,
                    (message.before.valid?1U:0U)|(after.valid?2U:0U));
            }
            (void)std::fflush(output);
        }
        std::vector<Message>().swap(messages_); // release inside preserved environment
    }
    Frame(const Frame&)=delete;Frame& operator=(const Frame&)=delete;
};
}
