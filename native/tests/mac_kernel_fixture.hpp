#pragma once
// Shared scenes and oracle-comparison metrics for the production kernel
// controls (native/MAC_KERNEL.md, "Validation").
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <chrono>
#include <functional>
#include <numbers>
#include <string>

namespace ma::testing::kernel {
struct Scene {
    std::string name;
    MacTransportState state;
    MacClosedSettings settings;
    double disturbance_pa = 0; // pressure-wave amplitude (0: use relative pressure)
    std::size_t intervals = 2;
};
inline void set_velocity(const Setup& s, MacTransportState& st, double ux, double uy) {
    const auto inertia = mac_face_inertias(s.registry, st, *s.eos);
    for (std::size_t f = 0; f < st.momentum_x.size(); ++f) {
        const auto& g = st.grid; const auto x = f % g.x_width(), y = f / g.x_width();
        const bool fixed = (!g.periodic_x && (x == 0 || x == g.width)) ||
            (x < g.width && g.walls[x + y * g.width]) || ((x > 0 || g.periodic_x) && g.walls[(x + g.width - 1) % g.width + y * g.width]);
        st.momentum_x[f] = fixed ? 0 : inertia.x[f] * ux;
    }
    for (std::size_t f = 0; f < st.momentum_y.size(); ++f) {
        const auto& g = st.grid; const auto x = f % g.width, y = f / g.width;
        const bool fixed = (!g.periodic_y && (y == 0 || y == g.height)) ||
            (y < g.height && g.walls[x + y * g.width]) || ((y > 0 || g.periodic_y) && g.walls[x + ((y + g.height - 1) % g.height) * g.width]);
        st.momentum_y[f] = fixed ? 0 : inertia.y[f] * uy;
    }
}
inline void reset_passives(const Setup& s, MacTransportState& st) {
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        auto& c = st.cells[i];
        if (st.grid.walls[i] || c.mixture.mass() == 0) { c.passive_quantities.clear(); continue; }
        c.passive_quantities = {{17, c.mixture.mass() * (1 + .125 * static_cast<double>(i % 7))}, {UINT64_C(0x80000000), c.mixture.mass() * 1e-30}};
    }
    (void)s;
}
// The benchmark wave: 20 Pa sinusoid along x, periodic.
inline Scene wave(std::size_t w, std::size_t h, double amplitude = 20) {
    const auto& s = setup(); Scene sc; sc.name = "wave" + std::to_string(w) + "x" + std::to_string(h);
    sc.state = s.state(w, h, true);
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) {
        const double x = (static_cast<double>(i % w) + .5) / static_cast<double>(w);
        sc.state.cells[i].mixture = s.mixture(1, 100000 + amplitude * std::sin(2 * std::numbers::pi * x), 300);
    }
    sc.disturbance_pa = amplitude;
    return sc;
}
inline Scene uniform_translation(std::size_t w, std::size_t h, double u) {
    const auto& s = setup(); Scene sc; sc.name = "translation";
    sc.state = s.state(w, h, true); set_velocity(s, sc.state, u, .5 * u);
    return sc;
}
// Nonperiodic closed box at 9.81 m/s^2, isothermal hydrostatic profile with
// a small horizontal pressure perturbation.
inline Scene hydrostatic(std::size_t w, std::size_t h) {
    const auto& s = setup(); Scene sc; sc.name = "hydrostatic_gravity";
    sc.state = s.state(w, h, false);
    const double R = gas_constant * (.78 / s.registry[s.registry.index("nitrogen")].molar_mass + .22 / s.registry[s.registry.index("oxygen")].molar_mass);
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) {
        const double y = static_cast<double>(i / w) + .5, x = (static_cast<double>(i % w) + .5) / static_cast<double>(w);
        const double p = 100000 * std::exp(-9.81 * y / (R * 300)) + 5 * std::cos(std::numbers::pi * x);
        sc.state.cells[i].mixture = s.mixture(1, p, 300);
    }
    reset_passives(s, sc.state);
    sc.settings.gravity_m_per_s2 = 9.81; sc.disturbance_pa = 5;
    return sc;
}
inline Scene conduction(std::size_t w, std::size_t h) {
    const auto& s = setup(); Scene sc; sc.name = "conduction";
    sc.state = s.state(w, h, false);
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) {
        const auto x = i % w, y = i / w;
        const bool hot = x >= w / 4 && x < w / 2 && y >= h / 4 && y < h / 2;
        sc.state.cells[i].mixture = s.mixture(1, 100000, hot ? 330 : 300);
    }
    reset_passives(s, sc.state);
    sc.settings.thermal_conduction = true;
    return sc;
}
// Wet periodic-free box with a solid corner: every cell holds gas and water.
inline Scene mixed(std::size_t w, std::size_t h) {
    const auto& s = setup(); Scene sc; sc.name = "mixed_gas_liquid";
    sc.state = s.state(w, h, false, true, true);
    sc.settings.thermal_conduction = true;
    return sc;
}
// Filled compressed water with gravity and a pressure perturbation.
inline Scene liquid(std::size_t w, std::size_t h) {
    const auto& s = setup(); Scene sc; sc.name = "filled_liquid_gravity";
    sc.state = s.state(w, h, false);
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) {
        const double y = static_cast<double>(i / w) + .5, x = (static_cast<double>(i % w) + .5) / static_cast<double>(w);
        const double p = 200000 + 1000 * 9.81 * (static_cast<double>(h) - y) + 200 * std::cos(std::numbers::pi * x);
        sc.state.cells[i].mixture = s.mixture(1, p, 300, 1);
    }
    reset_passives(s, sc.state);
    sc.settings.gravity_m_per_s2 = 9.81; sc.disturbance_pa = 200;
    return sc;
}
inline Scene viscous_wave(std::size_t w, std::size_t h) {
    auto sc = wave(w, h); sc.name = "viscous_wave"; sc.settings.viscosity = {.5, .25};
    set_velocity(setup(), sc.state, .3, -.2);
    return sc;
}
// ONI-like room: nonperiodic, interior wall segment, a 1 kPa step between
// halves, conduction on.
inline Scene room(std::size_t w, std::size_t h) {
    const auto& s = setup(); Scene sc; sc.name = "room_step";
    sc.state = s.state(w, h, false);
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) {
        const auto x = i % w, y = i / w;
        if (x == w / 2 && y >= h / 3 && y < 2 * h / 3) { sc.state.grid.walls[i] = 1; sc.state.cells[i] = {MixtureInventory(s.registry.size()), {}}; continue; }
        sc.state.cells[i].mixture = s.mixture(1, x < w / 2 ? 101000 : 100000, x < w / 2 ? 305 : 295, 0, x < w / 2 ? .22 : .18);
    }
    reset_passives(s, sc.state);
    sc.settings.thermal_conduction = true; sc.disturbance_pa = 1000;
    return sc;
}
// Gas expanding into exact empty cells.
inline Scene vacuum(std::size_t w, std::size_t h) {
    const auto& s = setup(); Scene sc; sc.name = "empty_cells";
    sc.state = s.state(w, h, false);
    for (std::size_t i = 0; i < sc.state.cells.size(); ++i) if (i % w >= 3 * w / 4)
        sc.state.cells[i] = {MixtureInventory(s.registry.size()), {}};
    reset_passives(s, sc.state);
    // A violent expansion: both solvers would otherwise reject and halve
    // near the same volume-CFL threshold, so a fixed step small enough for
    // neither to reject gives an equal-dt comparison.
    sc.settings.maximum_timestep_s = 1. / 4096; sc.intervals = 1;
    sc.disturbance_pa = 100000;
    return sc;
}

struct Metrics {
    double temperature_k = 0, pressure_relative = 0, pressure_over_disturbance = 0, oxygen_relative = 0,
        constituent_relative = 0, density_relative = 0, velocity_over_maximum = 0, energy_relative = 0, mass_relative = 0;
};
inline Metrics compare(const MacClosedWorld& reference, const MacClosedWorld& candidate, double disturbance) {
    const auto& s = setup(); Metrics m;
    const auto& a = reference.state(); const auto& b = candidate.state();
    const auto oxygen = s.registry.index("oxygen");
    double ea = 0, eb = 0, ma = 0, mb = 0;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        if (a.grid.walls[i]) continue;
        const auto& x = a.cells[i].mixture; const auto& y = b.cells[i].mixture;
        ea += x.energy; eb += y.energy; ma += x.mass(); mb += y.mass();
        if (x.mass() == 0 && y.mass() == 0) continue;
        if (x.mass() == 0 || y.mass() == 0) { m.constituent_relative = std::max(m.constituent_relative, 1.); continue; }
        const auto ra = s.eos->evaluate(x, 1), rb = s.eos->evaluate(y, 1);
        m.temperature_k = std::max(m.temperature_k, std::abs(rb.temperature_k - ra.temperature_k));
        m.pressure_relative = std::max(m.pressure_relative, std::abs(rb.mechanical_pressure_pa - ra.mechanical_pressure_pa) / ra.mechanical_pressure_pa);
        if (disturbance > 0) m.pressure_over_disturbance = std::max(m.pressure_over_disturbance, std::abs(rb.mechanical_pressure_pa - ra.mechanical_pressure_pa) / disturbance);
        m.density_relative = std::max(m.density_relative, std::abs(y.mass() - x.mass()) / x.mass());
        if (x.masses[oxygen] > 0 && ra.gas_pressure_pa > 0) {
            const double pa = ra.gas_pressure_pa * (x.masses[oxygen] / s.registry[oxygen].molar_mass) / x.gas_moles(s.registry);
            const double pb = rb.gas_pressure_pa * (y.masses[oxygen] / s.registry[oxygen].molar_mass) / y.gas_moles(s.registry);
            m.oxygen_relative = std::max(m.oxygen_relative, std::abs(pb - pa) / pa);
        }
        for (std::size_t k = 0; k < x.masses.size(); ++k) if (x.masses[k] > 0)
            m.constituent_relative = std::max(m.constituent_relative, std::abs(y.masses[k] - x.masses[k]) / x.masses[k]);
    }
    const auto ia = mac_face_inertias(s.registry, a, *s.eos);
    double vmax = 0, dv = 0;
    for (int axis = 0; axis < 2; ++axis) {
        const auto& ja = axis == 0 ? a.momentum_x : a.momentum_y; const auto& jb = axis == 0 ? b.momentum_x : b.momentum_y;
        const auto& in = axis == 0 ? ia.x : ia.y;
        for (std::size_t f = 0; f < ja.size(); ++f) if (in[f] > 0) {
            vmax = std::max(vmax, std::abs(ja[f] / in[f])); dv = std::max(dv, std::abs((jb[f] - ja[f]) / in[f]));
        }
    }
    m.velocity_over_maximum = vmax > 0 ? dv / vmax : dv;
    m.energy_relative = std::abs(eb - ea) / std::abs(ea);
    m.mass_relative = std::abs(mb - ma) / ma;
    return m;
}
inline double milliseconds(const std::function<void()>& f) {
    const auto t = std::chrono::steady_clock::now(); f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
}
