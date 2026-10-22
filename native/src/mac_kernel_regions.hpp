#pragma once
// Private connected-region geometry. Closed faces remain connected here so
// every possible thermal contact stays inside one region. Crops retain the
// original cell spacing, inventory, primary corrections and canonical J.
#include "mac_kernel_detail.hpp"

namespace ma::experimental::mac_kernel {
struct ClosedRegion {
    std::vector<std::size_t> cells;
    std::size_t id = 0, x = 0, y = 0, width = 0, height = 0;
    bool material = false;
};
inline std::vector<ClosedRegion> closed_regions(const MacGrid& g, std::vector<std::size_t>& labels) { const auto absent = std::numeric_limits<std::size_t>::max();
    if (g.periodic_x || g.periodic_y) throw std::runtime_error("Regional stepping requires a nonperiodic grid");
    labels.assign(g.width * g.height, absent); std::vector<ClosedRegion> parts;
    for (std::size_t seed = 0; seed < g.width * g.height; ++seed) {
        if (g.walls[seed] || labels[seed] != absent) continue;
        ClosedRegion p; p.id = parts.size(); p.cells = {seed}; labels[seed] = p.id;
        std::size_t xmin = seed % g.width, xmax = xmin, ymin = seed / g.width, ymax = ymin;
        for (std::size_t q = 0; q < p.cells.size(); ++q) {
            const auto i = p.cells[q], x = i % g.width, y = i / g.width;
            xmin = std::min(xmin, x); xmax = std::max(xmax, x); ymin = std::min(ymin, y); ymax = std::max(ymax, y);
            const auto visit = [&](std::size_t j) { if (!g.walls[j] && labels[j] == absent) { labels[j] = p.id; p.cells.push_back(j); } };
            if (x > 0) visit(i - 1);
            if (x + 1 < g.width) visit(i + 1);
            if (y > 0) visit(i - g.width);
            if (y + 1 < g.height) visit(i + g.width);
        }
        p.x = xmin; p.y = ymin; p.width = xmax - xmin + 1; p.height = ymax - ymin + 1;
        parts.push_back(std::move(p));
    }
    return parts;
}
inline std::vector<ClosedRegion> closed_regions(const MacTransportState& st, std::vector<std::size_t>& labels) {
    auto parts = closed_regions(st.grid, labels);
    for (auto& p : parts) for (const auto i : p.cells) p.material = p.material || st.cells[i].mixture.mass() != 0;
    return parts;
}
inline bool resting_singleton(const SpeciesRegistry& registry, const MacTransportState& st,
    const MacClosedSettings& settings, const ClosedRegion& p) {
    if (p.cells.size() != 1) return false;
    const auto i = p.cells.front(); const auto& g = st.grid;
    const auto x = i % g.width, y = i / g.width;
    for (const auto f : {g.x_face(x,y),g.x_face(x+1,y)})
        if (st.momentum_x[f] != 0 || (!st.full_momentum_x.empty() && !st.full_momentum_x[f].is_zero())) return false;
    for (const auto f : {g.y_face(x,y),g.y_face(x,y+1)})
        if (st.momentum_y[f] != 0 || (!st.full_momentum_y.empty() && !st.full_momentum_y[f].is_zero())) return false;
    if (settings.subcell_void == MacSubcellVoidPolicy::expand_unresolved_gas && !st.occupied_volumes_m3.empty() &&
        st.occupied_volumes_m3[i] < g.volume_m3() && st.occupied_volumes_m3[i] > 0 &&
        st.occupied_volumes_m3[i] <= settings.gas_expansion_volume_fraction * g.volume_m3())
        for (std::size_t k = 0; k < registry.size(); ++k)
            if (registry[k].phase == Phase::gas && st.cells[i].mixture.masses[k] != 0) return false;
    return true;
}
inline MacTransportState crop_region(const SpeciesRegistry& registry, const MacTransportState& st, const ClosedRegion& p) {
    MacTransportState out; auto& g = out.grid; const auto& original = st.grid;
    g.width = p.width; g.height = p.height; g.spacing_m = original.spacing_m; g.depth_m = original.depth_m;
    g.walls.assign(p.width * p.height, 1);
    out.cells.assign(g.walls.size(), {MixtureInventory(registry.size()), {}});
    if (!st.occupied_volumes_m3.empty()) out.occupied_volumes_m3.assign(g.walls.size(), 0);
    for (const auto i : p.cells) {
        const auto local = i % original.width - p.x + (i / original.width - p.y) * p.width;
        g.walls[local] = 0; out.cells[local] = st.cells[i];
        if (!out.occupied_volumes_m3.empty()) out.occupied_volumes_m3[local] = st.occupied_volumes_m3[i];
    }
    out.momentum_x.assign(g.x_width() * g.height, 0); out.momentum_y.assign(g.width * g.y_height(), 0);
    if (!st.full_momentum_x.empty()) out.full_momentum_x.resize(out.momentum_x.size());
    if (!st.full_momentum_y.empty()) out.full_momentum_y.resize(out.momentum_y.size());
    if (!original.closed_x.empty()) g.closed_x.resize(out.momentum_x.size());
    if (!original.closed_y.empty()) g.closed_y.resize(out.momentum_y.size());
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
        const auto f = g.x_face(x,y), global = original.x_face(p.x + x,p.y + y);
        if (!g.closed_x.empty()) g.closed_x[f] = original.closed_x[global];
        if ((x > 0 && !g.walls[x-1+y*g.width]) || (x < g.width && !g.walls[x+y*g.width])) {
            out.momentum_x[f] = st.momentum_x[global];
            if (!out.full_momentum_x.empty()) out.full_momentum_x[f] = st.full_momentum_x[global];
        }
    }
    for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto f = g.y_face(x,y), global = original.y_face(p.x + x,p.y + y);
        if (!g.closed_y.empty()) g.closed_y[f] = original.closed_y[global];
        if ((y > 0 && !g.walls[x+(y-1)*g.width]) || (y < g.height && !g.walls[x+y*g.width])) {
            out.momentum_y[f] = st.momentum_y[global];
            if (!out.full_momentum_y.empty()) out.full_momentum_y[f] = st.full_momentum_y[global];
        }
    }
    return out;
}
inline void stitch_region(MacTransportState& target, const MacTransportState& part, const ClosedRegion& p) {
    const auto& g = part.grid; const auto& original = target.grid;
    for (const auto i : p.cells) {
        const auto local = i % original.width - p.x + (i / original.width - p.y) * p.width;
        target.cells[i] = part.cells[local];
        target.occupied_volumes_m3[i] = part.occupied_volumes_m3.empty() ? (part.cells[local].mixture.mass() != 0 ? g.volume_m3() : 0) : part.occupied_volumes_m3[local];
    }
    if (!part.full_momentum_x.empty() && target.full_momentum_x.empty()) target.full_momentum_x.resize(target.momentum_x.size());
    if (!part.full_momentum_y.empty() && target.full_momentum_y.empty()) target.full_momentum_y.resize(target.momentum_y.size());
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
        if (!((x > 0 && !g.walls[x-1+y*g.width]) || (x < g.width && !g.walls[x+y*g.width]))) continue;
        const auto f = g.x_face(x,y), global = original.x_face(p.x+x,p.y+y);
        target.momentum_x[global] = part.momentum_x[f];
        if (!target.full_momentum_x.empty()) target.full_momentum_x[global] = part.full_momentum_x.empty() ? MacScaled{} : part.full_momentum_x[f];
    }
    for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x) {
        if (!((y > 0 && !g.walls[x+(y-1)*g.width]) || (y < g.height && !g.walls[x+y*g.width]))) continue;
        const auto f = g.y_face(x,y), global = original.y_face(p.x+x,p.y+y);
        target.momentum_y[global] = part.momentum_y[f];
        if (!target.full_momentum_y.empty()) target.full_momentum_y[global] = part.full_momentum_y.empty() ? MacScaled{} : part.full_momentum_y[f];
    }
}
}
