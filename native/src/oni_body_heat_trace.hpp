#pragma once
#include "oni_capture_file.hpp"

// Optional observation of the pinned 744825 frame-message boundary.
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
#if defined(__linux__) || defined(_WIN32)
#include <xmmintrin.h>
#endif

namespace ma::oni::body_heat_trace {

struct Body {
    std::uint32_t temperature_bits=0, capacity_per_cell_bits=0;
    std::int32_t area=0;
    bool valid=false;
};

// Native ABI views, reached only after the backend validates the image and
// original hook entries. Windows offsets include MSVC vector's first-pointer
// slot: SimData.buildingHeatExchange+0x4d0, CompactedVector data+8/handles+72,
// HandleVector items+32/versions+64, and each vector's first pointer+8 (PDB).
struct LinuxLayout {
    static constexpr bool supported=true;
    static constexpr std::size_t data_first=0x438,data_last=0x440;
    static constexpr std::size_t mapping_first=0x480,mapping_last=0x488;
    static constexpr std::size_t generation_first=0x498,generation_last=0x4a0;
    static constexpr std::size_t messages_first=0x1b8,messages_last=0x1c0;
};
struct WindowsLayout {
    static constexpr bool supported=true;
    static constexpr std::size_t data_first=0x4e0,data_last=0x4e8;
    static constexpr std::size_t mapping_first=0x540,mapping_last=0x548;
    static constexpr std::size_t generation_first=0x560,generation_last=0x568;
    static constexpr std::size_t messages_first=0x250,messages_last=0x258;
};
struct UnsupportedLayout {static constexpr bool supported=false;};
#if defined(__linux__)
using NativeLayout=LinuxLayout;
#elif defined(_WIN32)
using NativeLayout=WindowsLayout;
#else
using NativeLayout=UnsupportedLayout;
#endif

namespace detail {
template<class T> T read(const void* source,std::size_t offset) noexcept {
    T result{};std::memcpy(&result,static_cast<const std::byte*>(source)+offset,sizeof(result));return result;
}
inline std::size_t count(const void* first,const void* last,std::size_t stride,std::size_t maximum) noexcept {
    const auto begin=reinterpret_cast<std::uintptr_t>(first),end=reinterpret_cast<std::uintptr_t>(last);
    if(!stride || !begin || end<begin || (end-begin)%stride || (end-begin)/stride>maximum)return 0;
    return (end-begin)/stride;
}
struct Preserve {
    std::fenv_t environment{};int error=errno;bool saved=false;
#if defined(__linux__) || defined(_WIN32)
    unsigned csr=_mm_getcsr();
#endif
    Preserve() noexcept {saved=std::fegetenv(&environment)==0;}
    ~Preserve() {
        if(saved)(void)std::fesetenv(&environment);
#if defined(__linux__) || defined(_WIN32)
        _mm_setcsr(csr);
#endif
        errno=error;
    }
};
struct FileSink {
    static const char* path() noexcept {
        static const auto* setting=std::getenv("MA_ONI_BODY_HEAT_TRACE");return setting;
    }
    static bool enabled() noexcept {return path()!=nullptr && *path()!=0;}
    static std::FILE* file() noexcept {
        static std::FILE* output=[] {
            auto* opened=ma::oni::capture_file(path(),false);
            if(opened)std::fputs("native_frame\tbody_handle\tdelta_kj_bits\tminimum_k_bits\tmaximum_k_bits\tbefore_k_bits\tafter_k_bits\tcapacity_per_cell_kj_k_bits\tarea\tvalid\n",opened);
            return opened;
        }();
        return output;
    }
};
}

template<class Layout> inline Body read_body(const void* sim,std::int32_t handle) noexcept {
    Body result;
    if constexpr(Layout::supported) {
    if(!sim || (static_cast<std::uint32_t>(handle)&0x00f00000U))return result;
    const auto index=static_cast<std::uint32_t>(handle)&0x00ffffffU;
    const auto generation=static_cast<std::uint32_t>(handle)>>24;
    const auto* generations=detail::read<const std::uint8_t*>(sim,Layout::generation_first);
    const auto* generation_end=detail::read<const std::uint8_t*>(sim,Layout::generation_last);
    if(index>=detail::count(generations,generation_end,1,1U<<20) || generations[index]!=generation)return result;
    const auto* mapping=detail::read<const std::int32_t*>(sim,Layout::mapping_first);
    const auto* mapping_end=detail::read<const std::int32_t*>(sim,Layout::mapping_last);
    if(index>=detail::count(mapping,mapping_end,4,1U<<20) || mapping[index]<0)return result;
    const auto* data=detail::read<const std::byte*>(sim,Layout::data_first);
    const auto* data_end=detail::read<const std::byte*>(sim,Layout::data_last);
    if(static_cast<std::size_t>(mapping[index])>=detail::count(data,data_end,44,1U<<20))return result;
    const auto* record=data+static_cast<std::size_t>(mapping[index])*44;
    const auto width=static_cast<std::int64_t>(detail::read<std::int32_t>(record,36))-detail::read<std::int32_t>(record,28);
    const auto height=static_cast<std::int64_t>(detail::read<std::int32_t>(record,40))-detail::read<std::int32_t>(record,32);
    if(width<=0 || height<=0 || width>1048576 || height>1048576 || width*height>1048576)return result;
    result.temperature_bits=detail::read<std::uint32_t>(record,0);
    result.capacity_per_cell_bits=detail::read<std::uint32_t>(record,16);
    result.area=static_cast<std::int32_t>(width*height);result.valid=true;
    } else {(void)sim;(void)handle;}
    return result;
}
inline Body body(const void* sim,std::int32_t handle) noexcept {return read_body<NativeLayout>(sim,handle);}

template<class Layout,class Sink=detail::FileSink> class BasicFrame {
    struct Message {std::int32_t handle;std::uint32_t energy,minimum,maximum;Body before;};
    std::uint64_t ordinal_;
    const void* sim_;
    std::vector<Message> messages_;
public:
    static bool enabled() noexcept {return Layout::supported && Sink::enabled();}
    BasicFrame(std::uint64_t ordinal,const void* frame,const void* sim) noexcept:ordinal_(ordinal),sim_(sim) {
        detail::Preserve preserve;
        try {
            if constexpr(Layout::supported) {
            if(!frame || !sim)return;
            const auto* first=detail::read<const std::byte*>(frame,Layout::messages_first);
            const auto* last=detail::read<const std::byte*>(frame,Layout::messages_last);
            const auto n=detail::count(first,last,16,4096);messages_.reserve(n);
            for(std::size_t i=0;i<n;++i) {
                const auto* record=first+i*16;const auto handle=detail::read<std::int32_t>(record,0);
                messages_.push_back({handle,detail::read<std::uint32_t>(record,4),detail::read<std::uint32_t>(record,8),
                    detail::read<std::uint32_t>(record,12),read_body<Layout>(sim,handle)});
            }
            }
        }catch(...) {messages_.clear();}
    }
    ~BasicFrame() {
        detail::Preserve preserve;
        if constexpr(Layout::supported) if(auto* output=Sink::file()) {
            for(const auto& message:messages_) {
                const auto after=read_body<Layout>(sim_,message.handle);
                std::fprintf(output,"%llu\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%d\t%u\n",
                    static_cast<unsigned long long>(ordinal_),message.handle,message.energy,message.minimum,message.maximum,
                    message.before.temperature_bits,after.temperature_bits,message.before.capacity_per_cell_bits,message.before.area,
                    (message.before.valid?1U:0U)|(after.valid?2U:0U));
            }
            (void)std::fflush(output);
        }
        std::vector<Message>().swap(messages_); // release inside preserved environment
    }
    BasicFrame(const BasicFrame&)=delete;BasicFrame& operator=(const BasicFrame&)=delete;
};
using Frame=BasicFrame<NativeLayout>;
}
