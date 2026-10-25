#pragma once
// Private diagnostic only: admit an exact requested fill before native heat.
#include "oni_colony.hpp"
#include "oni_precise_receipt.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>

namespace ma::oni::colony::detail {
class FloorCapture {
    struct Cell { std::uint32_t index,element,mass,temperature; };
    std::string path;
    std::vector<Cell> cells;
    std::array<std::uint32_t,6> geometry{};
    bool loaded=false,finished=false;
public:
    void configure(const char* value) { path=value?value:""; cells.clear();loaded=finished=false; }
    bool enabled() const noexcept { return !path.empty() && !finished; }
    void stop() noexcept { finished=true; }
    // Constant-time filter for the requested arena, including unsampled cells.
    bool contains_native(int cell) const noexcept {
        if(!loaded || cell<0)return false;
        const auto pitch=geometry[0]+2;
        const auto nx=static_cast<std::uint32_t>(cell)%pitch,ny=static_cast<std::uint32_t>(cell)/pitch;
        return nx>geometry[2] && nx-geometry[2]<=geometry[4] &&
               ny>geometry[3] && ny-geometry[3]<=geometry[5];
    }
    bool observe(const NativeCells& source,std::uint64_t ticket,double seconds,
                 std::uint32_t phase=0,std::uint32_t contact_a=UINT32_MAX,std::uint32_t contact_b=UINT32_MAX) {
        if (!enabled()) return false;
        if(phase>1 || !ticket || !(seconds>0) || !std::isfinite(seconds))throw std::invalid_argument("Floor admission source epoch");
        if (!loaded) {
            std::ifstream input(path+".request",std::ios::binary|std::ios::ate);
            if (!input) return false;
            const auto size=input.tellg();
            if(size<36 || size>36+4096*16) throw std::invalid_argument("Floor admission request size");
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));input.seekg(0);
            input.read(reinterpret_cast<char*>(bytes.data()),size);
            if(!input || std::memcmp(bytes.data(),"MAFLRQ01",8)!=0) throw std::invalid_argument("Floor admission request framing");
            std::size_t at=8;
            const auto u32=[&] { if(at+4>bytes.size())throw std::invalid_argument("Truncated floor admission request");
                std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(bytes[at++])<<(8*i);return value; };
            for(auto& dimension:geometry)dimension=u32();
            const auto [gw,gh,x,y,w,h]=geometry;
            if(gw<2 || gh<2 || gw>65534 || gh>65534 || !w || !h ||
                static_cast<std::uint64_t>(x)+w>gw || static_cast<std::uint64_t>(y)+h>gh)
                throw std::invalid_argument("Floor admission geometry");
            const auto count=u32();
            if(!count || count>4096 || bytes.size()!=36+static_cast<std::size_t>(count)*16)
                throw std::invalid_argument("Floor admission cell count");
            std::set<std::uint32_t> seen;
            for(std::uint32_t i=0;i<count;++i) {
                const Cell cell{u32(),u32(),u32(),u32()};
                const auto cx=cell.index%gw,cy=cell.index/gw;
                const auto mass=std::bit_cast<float>(cell.mass),temperature=std::bit_cast<float>(cell.temperature);
                if(cx<x || cx>=x+w || cy<y || cy>=y+h || cell.element>65535 || !seen.insert(cell.index).second ||
                    !std::isfinite(mass) || mass<0 || !std::isfinite(temperature) || temperature<0)
                    throw std::invalid_argument("Floor admission cell");
                cells.push_back(cell);
            }
            loaded=true;
        }
        if(source.pitch!=geometry[0]+2 || source.rows!=geometry[1]+2)
            throw std::invalid_argument("Floor admission native grid differs");
        std::uint32_t matched=0;
        for(unsigned b=0;b<2;++b) {
            bool same=true;
            for(const auto& cell:cells) {
                const auto native=(cell.index%geometry[0]+1)+(cell.index/geometry[0]+1)*static_cast<std::size_t>(source.pitch);
                same &= source.element[b][native]==cell.element && std::bit_cast<std::uint32_t>(source.mass[b][native])==cell.mass &&
                    std::bit_cast<std::uint32_t>(source.temperature[b][native])==cell.temperature;
            }
            if(same) matched|=1u<<b;
        }
        // Without relying on a live active-buffer selector, a match in only
        // the stale counterpart cannot establish the actual physics input.
        if(matched!=3) return false;
        PreciseReceiptWriter wire;const char* magic=phase?"MAFLPR02":"MAFLPR01";wire.bytes.insert(wire.bytes.end(),magic,magic+8);
        for(auto dimension:geometry)wire.u32(dimension);
        wire.u64(ticket);wire.f64(seconds);wire.u32(matched);wire.u32(static_cast<std::uint32_t>(cells.size()));
        if(phase) {wire.u32(phase);wire.u32(contact_a);wire.u32(contact_b);}
        for(const auto& cell:cells) {
            wire.u32(cell.index);
            const auto native=(cell.index%geometry[0]+1)+(cell.index/geometry[0]+1)*static_cast<std::size_t>(source.pitch);
            for(unsigned b=0;b<2;++b) { wire.u32(source.element[b][native]);
                wire.u32(std::bit_cast<std::uint32_t>(source.mass[b][native]));wire.u32(std::bit_cast<std::uint32_t>(source.temperature[b][native])); }
        }
        auto* output=std::fopen(path.c_str(),"wbx");if(!output)throw std::runtime_error("Floor admission capture needs a fresh writable path");
        const auto written=std::fwrite(wire.bytes.data(),1,wire.bytes.size(),output);const auto closed=std::fclose(output);
        if(written!=wire.bytes.size() || closed!=0)throw std::runtime_error("Incomplete floor admission capture");
        finished=true;return true;
    }
};
}
