#pragma once
#include "oni_body_heat_trace.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

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
struct Cell {
    std::uint16_t element[2]{};
    std::uint32_t mass[2]{},temperature[2]{};
    bool valid=false;
};
inline Cell cell(const void* sim,std::uint32_t public_cell,bool windows) noexcept {
    Cell result;if(!sim)return result;
    using body_heat_trace::detail::read;
    const auto shape=context(sim,windows);
    if(shape.width<=2||shape.height<=2||std::uint64_t(public_cell)>=std::uint64_t(shape.width-2)*(shape.height-2))return result;
    const auto index=public_cell%std::uint32_t(shape.width-2)+1+
        (std::uint64_t(public_cell/std::uint32_t(shape.width-2))+1)*std::uint32_t(shape.width);
    const std::array<std::size_t,3> offsets=windows?std::array<std::size_t,3>{8,72,40}:std::array<std::size_t,3>{0,48,24};
    for(unsigned b=0;b<2;++b) {
        const auto* arrays=read<const std::byte*>(sim,32+8*b);if(!arrays)return {};
        for(unsigned field=0;field<3;++field) {
            const auto* first=read<const std::byte*>(arrays,offsets[field]);const auto* last=read<const std::byte*>(arrays,offsets[field]+8);
            const auto stride=field==0?2U:4U;
            if(index>=body_heat_trace::detail::count(first,last,stride,1U<<30))return {};
            if(field==0)result.element[b]=read<std::uint16_t>(first,index*stride);
            else if(field==1)result.mass[b]=read<std::uint32_t>(first,index*stride);
            else result.temperature[b]=read<std::uint32_t>(first,index*stride);
        }
    }
    result.valid=true;return result;
}
class Frame {
    std::uint64_t ordinal_;
    const void* sim_;
    std::uint32_t seconds_;
    Context before_;
    bool observe_cells_;
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
    static const std::vector<std::uint32_t>& cells() {
        static const auto request=[] {
            std::vector<std::uint32_t> result;const auto* text=std::getenv("MA_ONI_RUNTIME_CELLS");
            while(text&&*text) {
                if(*text<'0'||*text>'9')throw std::invalid_argument("Invalid private native runtime cell request");
                char* end{};const auto value=std::strtoull(text,&end,10);
                if(end==text||(*end!=0&&*end!=',')||value>std::numeric_limits<std::uint32_t>::max()||result.size()==4096)
                    throw std::invalid_argument("Invalid private native runtime cell request");
                result.push_back(static_cast<std::uint32_t>(value));text=*end?end+1:end;
            }
            std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
        }();return request;
    }
    static std::FILE* cell_file() {
        static auto* output=[] {
            const auto name=std::string(path())+".cells";auto* opened=ma::oni::capture_file(name.c_str(),false);
            if(opened)std::fputs("frame\tphase\tcell\tvalid\telement_old\telement_new\tmass_old_bits\tmass_new_bits\ttemperature_old_bits\ttemperature_new_bits\n",opened);
            return opened;
        }();return output;
    }
    void observe_cells(unsigned phase) noexcept {
        if(!observe_cells_)return;
        body_heat_trace::detail::Preserve preserve;
        try {
            if(cells().empty())return;
            if(auto* output=cell_file()) {
                for(const auto id:cells()) {
                    const auto state=cell(sim_,id,windows);
                    std::fprintf(output,"%llu\t%u\t%u\t%u\t%u\t%u\t%08x\t%08x\t%08x\t%08x\n",
                        static_cast<unsigned long long>(ordinal_),phase,id,state.valid?1U:0U,
                        static_cast<unsigned>(state.element[0]),static_cast<unsigned>(state.element[1]),
                        state.mass[0],state.mass[1],state.temperature[0],state.temperature[1]);
                }
                (void)std::fflush(output);
            }
        }catch(...){std::fputs("[Mixed Atmospheres] Private native runtime cell observation failed.\n",stderr);}
    }
public:
    static bool enabled() noexcept {return path()!=nullptr&&*path()!=0;}
    Frame(std::uint64_t ordinal,const void* sim,float seconds,bool observe_cells=false) noexcept:
        ordinal_(ordinal),sim_(sim),seconds_(std::bit_cast<std::uint32_t>(seconds)),before_(context(sim,windows)),observe_cells_(observe_cells) {this->observe_cells(0);}
    ~Frame() {
        body_heat_trace::detail::Preserve preserve;
        observe_cells(1);
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
