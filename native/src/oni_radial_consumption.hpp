#pragma once
#include "mixed_atmospheres/mac_energy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace ma::oni::radial {
struct Allocation { std::size_t cell;double mass; };
struct Plan { double mass=0;int quantum_exponent=0;std::vector<Allocation> allocations; };
inline void validate_sources(std::size_t width,std::size_t height,std::span<const std::uint8_t> walls,
    std::span<const double> masses,double requested) {
    ma::experimental::require_mac_energy_environment();
    if (!width || !height || width>256 || height>256 || width*height>256 || walls.size()!=width*height ||
        masses.size()!=walls.size() || !std::isfinite(requested) || requested<0 ||
        static_cast<double>(static_cast<float>(requested))!=requested)
        throw std::invalid_argument("Invalid regional consumption geometry or callback request");
    for (std::size_t cell=0;cell<masses.size();++cell)
        if (walls[cell]>1 || !std::isfinite(masses[cell]) || masses[cell]<0)
            throw std::invalid_argument("Invalid regional consumption source mass or wall");
}
// Binary32 is the callback storage format, not a physical constituent cutoff.
// All parcels share its final total's quantum, so their exact sum is itself
// binary32. Smaller per-cell residuals remain authoritative source inventory.
inline Plan allocate_order(std::span<const double> masses,std::span<const std::size_t> order,double requested) {
    using namespace ma::experimental;
    MacEnergySum available;
    for (const auto cell:order) available.add(masses[cell]);
    auto residual=available;residual.add(-requested);float target=static_cast<float>(requested);
    if (residual.sign()<0) {
        target=static_cast<float>(available.value().project().value);auto check=available;check.add(-static_cast<double>(target));
        if (check.sign()<0) target=std::nextafter(target,0.0F);
    }
    Plan result;if (target==0) return result;
    int exponent=0;(void)std::frexp(static_cast<double>(target),&exponent);
    result.quantum_exponent=std::max(-149,exponent-24);
    const auto units=static_cast<std::uint32_t>(std::ldexp(static_cast<double>(target),-result.quantum_exponent));
    auto remaining=units;
    for (const auto cell:order) {
        if (!remaining) break;
        const auto remaining_mass=std::ldexp(static_cast<double>(remaining),result.quantum_exponent);
        auto count=masses[cell]>=remaining_mass ? remaining : static_cast<std::uint32_t>(std::floor(std::ldexp(masses[cell],-result.quantum_exponent)));
        if (count && std::ldexp(static_cast<double>(count),result.quantum_exponent)>masses[cell]) --count;
        if (!count) continue;
        result.allocations.push_back({cell,std::ldexp(static_cast<double>(count),result.quantum_exponent)});remaining-=count;
    }
    result.mass=std::ldexp(static_cast<double>(units-remaining),result.quantum_exponent);return result;
}
inline Plan prepare(std::size_t width,std::size_t height,std::span<const std::uint8_t> walls,
    std::span<const double> masses,std::size_t centre,std::uint32_t radius,double requested) {
    validate_sources(width,height,walls,masses,requested);
    if (centre>=masses.size() || radius>255 || walls[centre])
        throw std::invalid_argument("Invalid radial consumption centre or radius");
    std::vector<std::pair<std::size_t,std::uint32_t>> search{{centre,0}};std::vector<bool> visited(masses.size());visited[centre]=true;
    for (std::size_t index=0;index<search.size();++index) {
        const auto [cell,distance]=search[index];if (distance==radius) continue;
        const auto x=cell%width,y=cell/width;
        const auto append=[&](std::size_t next) {if (!visited[next] && !walls[next]) {visited[next]=true;search.emplace_back(next,distance+1);}};
        if (x>0) append(cell-1);
        if (x+1<width) append(cell+1);
        if (y>0) append(cell-width);
        if (y+1<height) append(cell+width);
    }
    std::vector<std::size_t> order;order.reserve(search.size());
    for (const auto& [cell,distance]:search) {(void)distance;order.push_back(cell);}
    return allocate_order(masses,order,requested);
}
// The native rectangular overload anchors its lower-left cell and visits
// rows from bottom to top, left to right. Solids are skipped rather than
// connecting or blocking the rest of the explicitly selected footprint.
inline Plan prepare_rectangle(std::size_t width,std::size_t height,std::span<const std::uint8_t> walls,
    std::span<const double> masses,std::size_t anchor,std::uint32_t box_width,std::uint32_t box_height,double requested) {
    validate_sources(width,height,walls,masses,requested);
    if (anchor>=masses.size() || box_width>255 || !box_height || box_height>255 ||
        box_width>width-anchor%width || (box_width && box_height>height-anchor/width))
        throw std::invalid_argument("Rectangular consumption extends outside its reserved owner");
    std::vector<std::size_t> order;order.reserve(static_cast<std::size_t>(box_width)*box_height);
    for (std::size_t yy=0;yy<box_height;++yy) for (std::size_t xx=0;xx<box_width;++xx) {
        const auto cell=anchor+xx+yy*width;if (!walls[cell]) order.push_back(cell);
    }
    return allocate_order(masses,order,requested);
}
}
