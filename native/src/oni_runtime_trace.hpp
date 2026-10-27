#pragma once
#include "oni_body_heat_trace.hpp"

// Optional private context observation at the already pinned native frame
// hook. These values are native control state, never fluid solver state.
namespace ma::oni::runtime_trace {
struct Context {
    std::int32_t width{},height{},cells{},iterate{},displacement{};
    std::uint32_t random{};
    std::uint16_t tick{};
};
inline Context context(const void* sim,bool windows) noexcept {
    if(!sim)return {};
    using body_heat_trace::detail::read;
    return {read<std::int32_t>(sim,0),read<std::int32_t>(sim,4),read<std::int32_t>(sim,8),
        read<std::int32_t>(sim,16),read<std::int32_t>(sim,20),read<std::uint32_t>(sim,12),
        read<std::uint16_t>(sim,windows?160:144)};
}
class Frame {
    std::uint64_t ordinal_;
    const void* sim_;
    std::uint32_t seconds_;
    Context before_;
#if defined(_WIN32)
    static constexpr bool windows=true;
#else
    static constexpr bool windows=false;
#endif
    static const char* path() noexcept {static const auto* setting=std::getenv("MA_ONI_RUNTIME_CONTEXT_TRACE");return setting;}
    static std::FILE* file() noexcept {
        static std::FILE* output=[] {
            auto* opened=ma::oni::capture_file(path(),false);
            if(opened)std::fputs("frame\tseconds_bits\twidth\theight\tcells\trandom_before\trandom_after\titerate_before\titerate_after\tdisplacement_before\tdisplacement_after\ttick_before\ttick_after\n",opened);
            return opened;
        }();return output;
    }
public:
    static bool enabled() noexcept {return path()!=nullptr&&*path()!=0;}
    Frame(std::uint64_t ordinal,const void* sim,float seconds) noexcept:
        ordinal_(ordinal),sim_(sim),seconds_(std::bit_cast<std::uint32_t>(seconds)),before_(context(sim,windows)) {}
    ~Frame() {
        body_heat_trace::detail::Preserve preserve;
        const auto after=context(sim_,windows);
        if(auto* output=file()) {
            std::fprintf(output,"%llu\t%08x\t%d\t%d\t%d\t%08x\t%08x\t%d\t%d\t%d\t%d\t%u\t%u\n",
                static_cast<unsigned long long>(ordinal_),seconds_,before_.width,before_.height,before_.cells,
                before_.random,after.random,before_.iterate,after.iterate,before_.displacement,after.displacement,
                static_cast<unsigned>(before_.tick),static_cast<unsigned>(after.tick));
            (void)std::fflush(output);
        }
    }
    Frame(const Frame&)=delete;Frame& operator=(const Frame&)=delete;
};
}
