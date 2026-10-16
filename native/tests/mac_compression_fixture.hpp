#pragma once
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_compression.hpp"
#include <string_view>
namespace ma::testing {
inline void export_compression_scaled(const MacScaled& x) { std::cout << ' ' << x.mantissa() << ' ' << x.exponent(); }
inline void export_compression_pair(std::size_t id, const MacCompressionPair& p, std::string_view prefix = {}) {
    std::cout << prefix << "PAIR " << id << ' ' << p.inertia_left << ' ' << p.inertia_right << ' ' << p.fixed_left << ' ' << p.fixed_right << ' ' << p.unresolved_momentum_updates;
    for (const auto& x : {p.before_left, p.before_right, p.integrated_density_area_s, p.closing_velocity,
        p.relaxation_number, p.impulse, p.heat, p.after_left, p.after_right, p.wall_impulse}) export_compression_scaled(x);
    std::cout << '\n';
}
inline bool compression_fixed_face(const MacGrid& g, int axis, std::size_t f) {
    const auto x = axis == 0 ? f % g.x_width() : f % g.width;
    const auto y = axis == 0 ? f / g.x_width() : f / g.width;
    if (axis == 0) return ((!g.periodic_x && (x == 0 || x == g.width)) ||
        g.walls[(x + g.width - 1) % g.width + y * g.width] || g.walls[x % g.width + y * g.width]);
    return ((!g.periodic_y && (y == 0 || y == g.height)) ||
        g.walls[x + ((y + g.height - 1) % g.height) * g.width] || g.walls[x + (y % g.height) * g.width]);
}
inline void export_compression(std::size_t id, const MacTransportState& source, const MacCompressionAdvance& result, double dt, std::string_view prefix = {}) {
    const auto& set = ma::testing::setup(); const auto& g = source.grid; const auto& report = result.report;
    std::cout << prefix << "GRID " << id << ' ' << g.width << ' ' << g.height << ' ' << g.spacing_m << ' ' << g.depth_m << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' << dt << ' ' << report.events.size() << ' ' << report.unresolved_heat_updates << ' ' << report.unresolved_momentum_updates;
    for (const auto& v : {report.heat, report.energy_residual, report.energy_bound, report.wall_impulse_x, report.wall_impulse_y}) export_compression_scaled(v);
    std::cout << ' ' << report.entropy_change_j_per_k << ' ' << report.entropy_roundoff_j_per_k << '\n';
    for (std::size_t i = 0; i < report.events.size(); ++i) {
        const auto key = 1000 + id * 100 + i; const auto& e = report.events[i]; export_compression_pair(key, e.pair, prefix);
        std::cout << prefix << "EVENT " << id << ' ' << i << ' ' << e.axis << ' ' << e.cell << ' ' << e.sweep << ' ' << key << '\n';
    }
    const auto mass = mac_face_inertias(set.registry, source, *set.eos);
    for (std::size_t i = 0; i < source.cells.size(); ++i) std::cout << prefix << "SOLID " << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << '\n';
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& s = endpoint == 0 ? source : result.state;
        for (std::size_t i = 0; i < s.cells.size(); ++i) {
            if (g.walls[i]) continue;
            const auto& c = s.cells[i]; const auto eos = set.eos->evaluate(c.mixture, g.volume_m3());
            std::cout << prefix << "SCELL " << id << ' ' << endpoint << ' ' << i << ' ' << c.mixture.energy << ' ' << eos.temperature_k << ' ' << eos.mechanical_pressure_pa << ' ' << eos.entropy_j_per_k << '\n';
            for (std::size_t k = 0; k < c.mixture.masses.size(); ++k) if (c.mixture.masses[k] != 0)
                std::cout << prefix << "SMASS " << id << ' ' << endpoint << ' ' << i << ' ' << k << ' ' << c.mixture.masses[k] << '\n';
            for (const auto& p : c.passive_quantities) std::cout << prefix << "SPASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
        }
        for (int axis = 0; axis < 2; ++axis) {
            const auto j = axis == 0 ? mac_field_values(s.momentum_x, s.full_momentum_x) : mac_field_values(s.momentum_y, s.full_momentum_y);
            const auto& m = axis == 0 ? mass.x : mass.y;
            for (std::size_t f = 0; f < j.size(); ++f) {
                std::cout << prefix << "SFACE " << id << ' ' << axis << ' ' << endpoint << ' ' << f << ' ' << m[f] << ' ' << compression_fixed_face(g, axis, f); export_compression_scaled(j[f]); std::cout << '\n';
            }
        }
    }
}
}
