#pragma once
// Diagnostic-only release request. The managed fixture atomically publishes
// this marker before queuing gate removal. Physical state is never modified.
#include "mixed_atmospheres/oni_colony.h"
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ma::oni::colony::detail {
class ReleaseCapture {
public:
    std::string path;
    void configure(const char* requested) { path = requested ? requested : ""; cells.clear();expected.clear(); done = false; }
    bool ready(const MAOniColonyConfig& config, std::span<const std::uint8_t> walls,
               const std::function<bool(std::size_t,std::uint32_t,float,float)>& matches = {}) {
        if (path.empty() || done) return false;
        if (cells.empty()) {
            std::ifstream in(path + ".request", std::ios::binary);
            if (!in) {
                if (std::filesystem::exists(path + ".request")) throw std::runtime_error("Could not read release capture request");
                return false;
            }
            const auto check = [](bool ok) { if (!ok) throw std::invalid_argument("Invalid release capture request"); };
            char magic[8]{}; in.read(magic,8);
            const auto version=std::string(magic,8);
            check(in.good() && (version=="MAREL001" || version=="MAREL002"));
            const auto u32 = [&] {
                std::array<unsigned char,4> bytes{}; in.read(reinterpret_cast<char*>(bytes.data()),4); check(in.good());
                std::uint32_t value=0; for (unsigned i=0;i<4;++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8*i);
                return value;
            };
            const std::array<std::uint32_t,6> geometry{config.grid_width,config.grid_height,config.offset_x,config.offset_y,config.width,config.height};
            check(config.grid_width>0 && config.width>0);
            for (const auto dimension : geometry) check(u32() == dimension);
            const auto count=u32(); check(count>0 && count<=4096 && count<=walls.size());
            std::vector<std::size_t> requested; requested.reserve(count);
            std::vector<Expected> values;values.reserve(count);
            for (std::uint32_t i=0;i<count;++i) {
                const auto cell=u32(), x=cell%config.grid_width, y=cell/config.grid_width;
                check(x>=config.offset_x && x-config.offset_x<config.width && y>=config.offset_y && y-config.offset_y<config.height);
                const auto local=static_cast<std::size_t>(x-config.offset_x)+(y-config.offset_y)*config.width;
                for (const auto previous : requested) check(previous!=local);
                requested.push_back(local);
                if(version=="MAREL002") {
                    const auto native=u32();const auto mass=std::bit_cast<float>(u32()),temperature=std::bit_cast<float>(u32());
                    check(native<=65535 && std::isfinite(mass) && mass>0 && std::isfinite(temperature) && temperature>0);
                    values.push_back({native,mass,temperature});
                }
            }
            check(in.peek()==std::char_traits<char>::eof()); cells=std::move(requested);expected=std::move(values);
        }
        for (const auto cell : cells) if (cell>=walls.size() || walls[cell]) return false;
        if(!expected.empty()) {
            if(!matches)return false;
            for(std::size_t i=0;i<cells.size();++i)
                if(!matches(cells[i],expected[i].native,expected[i].mass,expected[i].temperature))return false;
        }
        return true;
    }
    void captured() noexcept { done=true; }
private:
    std::vector<std::size_t> cells;
    struct Expected {std::uint32_t native;float mass,temperature;};
    std::vector<Expected> expected;
    bool done=false;
};
}
