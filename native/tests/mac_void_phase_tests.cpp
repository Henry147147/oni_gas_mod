// Void phase (MAC_VACUUM.md, "Void phase"): gas entering vacuum keeps its own
// volume, so each cell holds material and exact vacuum separated by a PLIC line.
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_reconstruction.hpp"
#include <cstring>
#include <functional>

namespace {
using namespace ma::testing;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
template<class F> void rejects(F&& f, const char* text, const char* what) {
    try { f(); } catch (const std::exception& e) { if (std::strstr(e.what(), text)) return; throw std::runtime_error(std::string(what) + ": " + e.what()); }
    throw std::runtime_error(what);
}
void full_range_parcel_geometry() {
    const auto& s = setup();
    for (const double filled : {1., .5}) {
        auto source = s.state(2, 1, false);
        for (auto& cell : source.cells) cell = {s.mixture(filled, 100000, 300), {}};
        source.occupied_volumes_m3.assign(2, filled);
        source.momentum_x.assign(3, 0); source.momentum_y.assign(4, 0);
        const std::vector<double> volumes(2, 1);
        MacMassFluxes sweep{std::vector<double>(3), std::vector<double>(4)};
        sweep.full_x.assign(3, {}); sweep.full_x[1] = MacScaled::from_double(1).shifted(-1200);
        const auto plan = reconstruct_mac_parcels(s.registry, *s.eos, source, volumes, sweep, .4, source.occupied_volumes_m3);
        require(plan.parcels.size() == 1 && !mac_parcel_mass_scaled(plan.parcels[0]).is_zero() &&
            mac_parcel_volume(plan.parcels[0]).project().value == 0 && plan.parcel_material_volumes_m3[0] == 0,
            "Void control did not retain a positive parcel below geometric readout range");
        const auto next = advance_mac_parcel_transport(s.registry, source, *s.eos, plan);
        for (std::size_t i = 0; i < source.cells.size(); ++i)
            require(next.state.cells[i].mixture.masses == source.cells[i].mixture.masses &&
                next.state.cells[i].mixture.energy == source.cells[i].mixture.energy,
                "Unresolved geometric atom changed a binary64 inventory projection");
        require(std::abs(next.report.energy_residual_j) <= next.report.energy_bound_j, "Wide void parcel energy account failed");
        auto bad = plan; bad.parcel_material_volumes_m3[0] = std::numeric_limits<double>::denorm_min();
        rejects([&] { static_cast<void>(advance_mac_parcel_transport(s.registry, source, *s.eos, bad)); },
            "material/void split", "Wide void parcel accepted a false geometric readout");
        if (filled == 1) {
            sweep.full_x.clear(); sweep.x[1] = .1;
            const std::vector<double> expanded{1.1, .9};
            auto ordinary = reconstruct_mac_parcels(s.registry, *s.eos, source, expanded, sweep, .4, expanded);
            require(!mac_parcel_mass_scaled(ordinary.parcels[0]).is_zero(), "Ordinary control did not carry mass");
            ordinary.parcel_material_volumes_m3[0] = 0; ordinary.parcel_void_volumes_m3[0] = .1;
            rejects([&] { static_cast<void>(advance_mac_parcel_transport(s.registry, source, *s.eos, ordinary)); },
                "material/void split", "Ordinary parcel accepted a false zero-material readout");
        }
    }
    std::cout << "PASS full-range void parcel geometry, unchanged projections and invalid readout refusal\n";
}
MacClosedSettings void_settings(double dt = 2e-4) {
    MacClosedSettings s; s.vacuum = MacVacuumRepresentation::void_phase; s.maximum_timestep_s = dt;
    s.reconstruction = MacMaterialReconstruction::swept_first_moment; s.momentum_reconstruction = MacMomentumReconstruction::donor_cell;
    s.compression_viscosity = MacCompressionViscosity::coupled_pressure; return s;
}
// Air at 100 kPa and 300 K where gas(x, y) holds, vacuum elsewhere.
template<class F> MacTransportState expansion(std::size_t w, std::size_t h, F gas) {
    const auto& s = setup(); MacTransportState st; st.grid = {w, h, 1, 1, false, false, std::vector<std::uint8_t>(w * h)};
    for (std::size_t y = 0; y < h; ++y) for (std::size_t x = 0; x < w; ++x)
        st.cells.push_back({gas(x, y) ? s.mixture(1, 100000, 300) : MixtureInventory(s.registry.size()), {}});
    const auto inertia = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(inertia.x.size(), 0); st.momentum_y.assign(inertia.y.size(), 0); return st;
}
std::vector<long double> species(const MacTransportState& st) {
    std::vector<long double> total(st.cells.front().mixture.masses.size());
    for (const auto& c : st.cells) for (std::size_t k = 0; k < total.size(); ++k) total[k] += c.mixture.masses[k];
    return total;
}
// Occupied volumes are valid and every partly filled gas cell borders vacuum, a
// partly filled cell, a wall or the domain edge (gas never encloses a void). A
// liquid's void may be enclosed while it is squeezed out.
bool s_liquid(const MixtureInventory& m) { const auto& s = setup(); return m.masses[s.registry.index("water")] > 0; }
std::size_t check_state(const MacTransportState& st) {
    const auto& g = st.grid; const double v = g.volume_m3(); std::size_t partial = 0;
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        const double mass = st.cells[i].mixture.mass(), occupied = mac_material_volume(st, i);
        if (mass == 0) { require(st.occupied_volumes_m3.empty() || occupied == 0, "Empty void-phase cell has an occupied volume"); continue; }
        require(occupied > 0 && occupied <= v, "Occupied void-phase cell has an invalid volume");
        if (occupied == v) continue;
        ++partial;
        if (s_liquid(st.cells[i].mixture)) continue;
        const auto x = i % g.width, y = i / g.width; bool open = false;
        for (const auto& [dx, dy] : {std::pair<int, int>{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const long nx = static_cast<long>(x) + dx, ny = static_cast<long>(y) + dy;
            if (nx < 0 || ny < 0 || nx >= static_cast<long>(g.width) || ny >= static_cast<long>(g.height)) { open = true; continue; }
            const auto n = static_cast<std::size_t>(nx) + static_cast<std::size_t>(ny) * g.width;
            if (g.walls[n] || mac_material_volume(st, n) < v) open = true;
        }
        require(open, "A partly filled cell is enclosed by filled cells");
    }
    return partial;
}
// 1-D expansion: the gas/void line advances inside one partly filled cell,
// nothing reaches beyond it, and species, energy and momentum close.
void one_dimensional() {
    const auto& s = setup(); constexpr std::size_t n = 48;
    const auto start = expansion(n, 1, [](std::size_t x, std::size_t) { return x < 12; });
    MacClosedWorld world(s.registry, s.eos, start, void_settings());
    const auto initial = species(start); const double c0 = 347, gamma = 1.4;
    double t = 0; std::size_t partial_seen = 0;
    for (int k = 0; k < 8; ++k) {
        auto r = world.advanced(2e-3, 65536); world = std::move(r.world); t += 2e-3;
        const auto& st = world.state(); const auto partial = check_state(st); partial_seen += partial;
        require(partial <= 1, "1-D expansion has more than one partly filled cell");
        std::size_t last = 0; for (std::size_t i = 0; i < n; ++i) if (st.cells[i].mixture.mass() > 0) last = i;
        for (std::size_t i = 0; i <= last; ++i) require(st.cells[i].mixture.mass() > 0, "1-D expansion left a gap");
        if (partial == 1) require(mac_material_volume(st, last) < st.grid.volume_m3(), "Partly filled cell is not the front cell");
        // No gas ahead of the exact front (one cell of slack).
        require(static_cast<double>(last) + mac_material_volume(st, last) <= 12 + 2 / (gamma - 1) * c0 * t + 1, "Gas ran ahead of the vacuum front");
        require(std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "1-D void-phase energy account failed");
        const auto now = species(st);
        for (std::size_t k2 = 0; k2 < now.size(); ++k2) require(std::abs(now[k2] - initial[k2]) <= 1e-15L * (initial[k2] + 1e-300L), "1-D species drift");
    }
    require(partial_seen > 0, "1-D expansion never formed a partly filled cell");
    std::cout << "void 1-D: front cell " << [&] { std::size_t last = 0; for (std::size_t i = 0; i < n; ++i) if (world.state().cells[i].mixture.mass() > 0) last = i; return last; }()
              << " partial-cell observations " << partial_seen << '\n';
}
// 2-D corner expansion: the line crosses cells obliquely; the same invariants
// hold, and the gas stays within the physical reach of the escape speed.
void two_dimensional() {
    const auto& s = setup(); constexpr std::size_t w = 20, h = 20;
    const auto start = expansion(w, h, [](std::size_t x, std::size_t y) { return x < 6 && y < 6; });
    MacClosedWorld world(s.registry, s.eos, start, void_settings());
    const auto initial = species(start); double t = 0; std::size_t partial = 0;
    for (int k = 0; k < 6; ++k) {
        auto r = world.advanced(2e-3, 65536); world = std::move(r.world); t += 2e-3;
        const auto& st = world.state(); partial = std::max(partial, check_state(st));
        require(std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "2-D void-phase energy account failed");
        for (std::size_t i = 0; i < st.cells.size(); ++i) if (st.cells[i].mixture.mass() > 0) {
            const double x = static_cast<double>(i % w), y = static_cast<double>(i / w);
            require(std::hypot(std::max(0., x - 6), std::max(0., y - 6)) <= 5 * 347 * t + 2, "2-D gas ran ahead of its escape-speed reach");
        }
        const auto now = species(st);
        for (std::size_t k2 = 0; k2 < now.size(); ++k2) require(std::abs(now[k2] - initial[k2]) <= 1e-14L * (initial[k2] + 1e-300L), "2-D species drift");
    }
    require(partial > 1, "2-D expansion never formed several partly filled cells");
    std::cout << "void 2-D: up to " << partial << " partly filled cells\n";
}
// Gas over vacuum under gravity: it falls into an empty lower region.
void gravity() {
    const auto& s = setup(); constexpr std::size_t w = 3, h = 24;
    const auto start = expansion(w, h, [](std::size_t, std::size_t y) { return y >= 6; });
    auto settings = void_settings(); settings.gravity_m_per_s2 = 9.81;
    MacClosedWorld world(s.registry, s.eos, start, settings);
    const auto initial = species(start);
    for (int k = 0; k < 4; ++k) {
        auto r = world.advanced(2e-3, 65536); world = std::move(r.world); static_cast<void>(check_state(world.state()));
        require(std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "Gravity void-phase energy account failed");
    }
    const auto now = species(world.state());
    for (std::size_t k = 0; k < now.size(); ++k) require(std::abs(now[k] - initial[k]) <= 1e-14L * (initial[k] + 1e-300L), "Gravity void-phase species drift");
    require(world.state().cells.front().mixture.mass() > 0, "Gas did not reach the floor");
}
// Version-9 checkpoints restore partly filled cells and continue exactly; the
// production policy falls back to the reference for selector 2 and coupled
// compression, which the production kernel does not implement.
void checkpoint_and_policy() {
    const auto& s = setup();
    MacClosedWorld world(s.registry, s.eos, expansion(24, 1, [](std::size_t x, std::size_t) { return x < 8; }), void_settings());
    world = world.advanced(3e-3, 65536).world;
    require(!world.state().occupied_volumes_m3.empty(), "Checkpoint control has no partly filled cell");
    const auto bytes = world.checkpoint();
    require(bytes.size() > 12 && bytes[8] == checkpoint_version(world,9) && bytes[9] == 0, "Void-phase checkpoint version does not represent all carried state");
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, bytes);
    require(fingerprint(restored.state()) == fingerprint(world.state()) && restored.state().occupied_volumes_m3 == world.state().occupied_volumes_m3,
        "Void-phase checkpoint changed the state");
    const auto a = world.advanced(2e-3, 65536).world, b = restored.advanced(2e-3, 65536).world;
    require(fingerprint(a.state()) == fingerprint(b.state()) && a.state().occupied_volumes_m3 == b.state().occupied_volumes_m3,
        "Restored void-phase continuation differs");
    require(MacClosedWorld::from_checkpoint(s.registry, s.eos, a.checkpoint()).checkpoint() == a.checkpoint(), "Void-phase checkpoint is not canonical");
    auto production = world.advanced(2e-3, 65536, MacExecutionPolicy::production);
    require(production.report.interval_reference_fallbacks > 0, "Unsupported void-phase settings did not fall back to the reference path");
    require(fingerprint(production.world.state()) == fingerprint(a.state()), "Void-phase production fallback differs from the reference");
    // Legacy owners keep their checkpoint version.
    MacClosedSettings legacy; legacy.maximum_timestep_s = 2e-4;
    const auto old = MacClosedWorld(s.registry, s.eos, expansion(8, 1, [](std::size_t x, std::size_t) { return x < 4; }), legacy).checkpoint();
    require(old[8] == 2, "Legacy owner changed checkpoint version");
}
// The production kernel runs the void phase with material selectors 1 and 3
// and default compression: the same invariants hold, with no reference
// fallback, and its front and gas volume follow the reference path's.
void production() {
    const auto& s = setup();
    for (const auto reconstruction : {MacMaterialReconstruction::limited_linear, MacMaterialReconstruction::interface_plic}) {
        auto settings = void_settings(); settings.reconstruction = reconstruction;
        settings.compression_viscosity = MacCompressionViscosity{};
        for (const bool planar : {true, false}) {
            const auto start = planar ? expansion(48, 1, [](std::size_t x, std::size_t) { return x < 12; }) :
                expansion(20, 20, [](std::size_t x, std::size_t y) { return x < 6 && y < 6; });
            MacClosedWorld fast(s.registry, s.eos, start, settings), slow = fast;
            const auto initial = species(start); std::size_t partial = 0;
            for (int k = 0; k < 6; ++k) {
                auto a = fast.advanced(2e-3, 65536, MacExecutionPolicy::production); fast = std::move(a.world);
                auto b = slow.advanced(2e-3, 65536); slow = std::move(b.world);
                require(a.report.interval_reference_fallbacks == 0, "Production void phase fell back to the reference");
                require(std::abs(a.report.energy_residual_j) <= a.report.energy_bound_j, "Production void-phase energy account failed");
                partial = std::max(partial, check_state(fast.state()));
                const auto now = species(fast.state());
                for (std::size_t k2 = 0; k2 < now.size(); ++k2)
                    require(std::abs(now[k2] - initial[k2]) <= 1e-14L * (initial[k2] + 1e-300L), "Production void-phase species drift");
            }
            // Gas volume and reach against the reference path.
            const auto gas = [](const MacTransportState& st) {
                double volume = 0, reach = 0;
                for (std::size_t i = 0; i < st.cells.size(); ++i) if (st.cells[i].mixture.mass() > 0) {
                    volume += mac_material_volume(st, i);
                    reach = std::max(reach, std::hypot(static_cast<double>(i % st.grid.width), static_cast<double>(i / st.grid.width)));
                }
                return std::pair{volume, reach};
            };
            const auto [fast_volume, fast_reach] = gas(fast.state()); const auto [slow_volume, slow_reach] = gas(slow.state());
            std::cout << "void production " << (planar ? "1-D" : "2-D") << " selector " << static_cast<int>(reconstruction)
                      << ": gas volume " << fast_volume << " (reference " << slow_volume << "), reach " << fast_reach
                      << " (reference " << slow_reach << "), partly filled " << partial << '\n';
            require(partial > 0, "Production void phase never formed a partly filled cell");
            require(std::abs(fast_volume - slow_volume) <= .05 * slow_volume && std::abs(fast_reach - slow_reach) <= 1.5,
                "Production void phase departs from the reference path");
        }
    }
}
// Liquid surfaces (production kernel): water under vacuum keeps its
// hydrostatic rest, and a water block falls freely through vacuum, its liquid
// at zero pressure in every partly filled cell.
struct Column { MacTransportState state; double mass = 0; };
Column water_in_vacuum(std::size_t w, std::size_t h, double spacing, std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1,
    bool hydrostatic) {
    const auto& s = setup(); const double g = 9.81, volume = spacing * spacing * spacing, area = spacing * spacing;
    Column c; c.state.grid = {w, h, spacing, spacing, false, false, std::vector<std::uint8_t>(w * h)};
    c.state.cells.assign(w * h, MacMaterialCell{MixtureInventory(s.registry.size()), {}});
    const auto water = s.registry.index("water");
    const auto liquid = [&](double p) { return volume * 1000 * (1 + p / 2.2e9); };
    for (std::size_t x = x0; x < x1; ++x) {
        // Discrete hydrostatics: A (p_below - p_above) = g (m_below + m_above) / 2,
        // with vacuum (no pressure, no mass) above the top cell.
        double above_p = 0, above_m = 0;
        for (std::size_t y = y1; y-- > y0;) {
            double p = 0;
            if (hydrostatic) for (int it = 0; it < 8; ++it) p = above_p + g * (liquid(p) + above_m) / (2 * area);
            std::vector<double> m(s.registry.size()); m[water] = liquid(p);
            c.state.cells[x + y * w].mixture = s.eos->initialize_at_temperature(m, 300, volume).mixture;
            c.mass += m[water]; above_p = p; above_m = m[water];
        }
    }
    const auto inertia = mac_face_inertias(s.registry, c.state, *s.eos);
    c.state.momentum_x.assign(inertia.x.size(), 0); c.state.momentum_y.assign(inertia.y.size(), 0);
    return c;
}
void liquid_surfaces(MacMaterialReconstruction reconstruction) {
    const auto& s = setup();
    auto settings = void_settings(1e-3); settings.reconstruction = reconstruction;
    settings.compression_viscosity = MacCompressionViscosity{}; settings.gravity_m_per_s2 = 9.81;
    const auto water = s.registry.index("water");
    const auto total = [&](const MacTransportState& st) { long double m = 0; for (const auto& c : st.cells) m += c.mixture.masses[water]; return m; };
    {
        const auto start = water_in_vacuum(4, 12, .1, 0, 4, 0, 6, true);
        MacClosedWorld world(s.registry, s.eos, start.state, settings);
        double speed = 0;
        for (int k = 0; k < 20; ++k) {
            auto r = world.advanced(1e-2, 65536, MacExecutionPolicy::production); world = std::move(r.world);
            require(r.report.interval_reference_fallbacks == 0, "Still water in vacuum fell back to the reference");
            require(std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "Still water in vacuum energy account failed");
        }
        // The water's faces (traces lifted off the surface by rounding fall
        // freely in vacuum, so they are bounded by mass instead).
        const auto& st = world.state(); const auto inertia = mac_face_inertias(s.registry, st, *s.eos);
        for (std::size_t f = 0; f < st.momentum_y.size(); ++f) if (inertia.y[f] > 1e-3) speed = std::max(speed, std::abs(st.momentum_y[f] / inertia.y[f]));
        for (std::size_t f = 0; f < st.momentum_x.size(); ++f) if (inertia.x[f] > 1e-3) speed = std::max(speed, std::abs(st.momentum_x[f] / inertia.x[f]));
        long double lifted = 0; for (std::size_t i = 6 * 4; i < st.cells.size(); ++i) lifted += st.cells[i].mixture.masses[water];
        std::cout << "void still water (selector " << static_cast<int>(reconstruction) << "): max speed " << speed << " m/s, water above the surface " << static_cast<double>(lifted)
                  << " kg, partly filled " << check_state(st) << '\n';
        require(speed <= 1e-8 && lifted <= 1e-15L * start.mass, "Still water under vacuum moved");
        require(std::abs(total(st) - start.mass) <= 1e-14L * start.mass, "Still water under vacuum lost mass");
    }
    {
        const auto start = water_in_vacuum(4, 12, .1, 1, 3, 8, 10, false);
        MacClosedWorld world(s.registry, s.eos, start.state, settings);
        const auto height = [&](const MacTransportState& st) {
            long double moment = 0, mass = 0;
            for (std::size_t i = 0; i < st.cells.size(); ++i) {
                const double m = st.cells[i].mixture.masses[water]; if (m == 0) continue;
                moment += m * (static_cast<double>(i / st.grid.width) + .5) * .1; mass += m;
            }
            return static_cast<double>(moment / mass);
        };
        const double z0 = height(start.state); std::size_t partial = 0;
        for (int k = 0; k < 10; ++k) {
            auto r = world.advanced(1e-2, 65536, MacExecutionPolicy::production); world = std::move(r.world);
            require(r.report.interval_reference_fallbacks == 0, "Falling water fell back to the reference");
            require(std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "Falling water energy account failed");
            partial = std::max(partial, check_state(world.state()));
        }
        const double fall = z0 - height(world.state()), exact = .5 * 9.81 * .1 * .1;
        // Free fall: the block's momentum is M g t whatever its geometry.
        long double momentum = 0; for (const double j : world.state().momentum_y) momentum -= j;
        const double expected = static_cast<double>(start.mass) * 9.81 * .1;
        std::cout << "void falling water (selector " << static_cast<int>(reconstruction) << "): fell " << fall << " m (free fall " << exact
                  << "), momentum " << static_cast<double>(momentum) << " (M g t " << expected << "), partly filled up to " << partial << '\n';
        require(std::abs(static_cast<double>(momentum) - expected) <= 1e-9 * expected, "Falling water's momentum is not M g t");
        require(partial > 0, "Falling water never formed a liquid surface cell");
        require(std::abs(fall - exact) <= .1 * exact, "Water did not fall freely through vacuum");
        require(std::abs(total(world.state()) - start.mass) <= 1e-14L * start.mass, "Falling water lost mass");
    }
}
// Conduction through partly filled cells (production kernel): a face conducts
// over the part both cells' material touches. Water blocks separated by vacuum
// exchange no heat; a half-filled hot surface cell conducts into the water
// below it across its whole bottom face.
void void_conduction() {
    const auto& s = setup(); const auto water = s.registry.index("water");
    auto settings = void_settings(1e-3); settings.reconstruction = MacMaterialReconstruction::limited_linear;
    settings.compression_viscosity = MacCompressionViscosity{}; settings.numerical_kernel = MacNumericalKernel::production;
    settings.thermal_conduction = true; settings.viscosity.shear_pa_s = 1e-3;
    const double h = .1, volume = h * h * h;
    const auto cell = [&](double mass, double t) {
        std::vector<double> m(s.registry.size()); m[water] = mass; return s.eos->initialize_at_temperature(m, t, volume).mixture;
    };
    const auto finish = [&](MacTransportState& st) {
        const auto inertia = mac_face_inertias(s.registry, st, *s.eos);
        st.momentum_x.assign(inertia.x.size(), 0); st.momentum_y.assign(inertia.y.size(), 0);
    };
    {
        // Zero gravity: water at zero pressure in two blocks, an empty column between.
        MacTransportState st; st.grid = {5, 2, h, h, false, false, std::vector<std::uint8_t>(10)};
        st.cells.assign(10, MacMaterialCell{MixtureInventory(s.registry.size()), {}});
        for (std::size_t y = 0; y < 2; ++y) for (std::size_t x : {0, 1, 3, 4}) st.cells[x + y * 5].mixture = cell(1000 * volume, x < 2 ? 350 : 300);
        finish(st);
        MacClosedWorld world(s.registry, s.eos, st, settings);
        for (int k = 0; k < 5; ++k) world = world.advanced(1e-2, 65536).world;
        for (std::size_t y = 0; y < 2; ++y) for (std::size_t x : {0, 1, 3, 4}) {
            const auto t = s.eos->evaluate(world.state().cells[x + y * 5].mixture, mac_material_volume(world.state(), x + y * 5)).temperature_k;
            require(std::abs(t - (x < 2 ? 350 : 300)) <= 1e-9, "Heat crossed vacuum");
        }
    }
    {
        // Gravity: a hot half-filled surface cell over cold water, in discrete
        // hydrostatic balance. The bottom face is wholly wet, so it conducts
        // k A / h of the temperature difference.
        auto gravity = settings; gravity.gravity_m_per_s2 = 9.81;
        MacTransportState st; st.grid = {1, 6, h, h, false, false, std::vector<std::uint8_t>(6)};
        st.cells.assign(6, MacMaterialCell{MixtureInventory(s.registry.size()), {}});
        // The surface cell rests on the water below: its whole weight bears on
        // its bottom face (MAC_VACUUM.md, "Liquid surfaces").
        const double area = h * h, top = 500 * volume;
        double above_p = 0, above_m = 2 * top;
        st.cells[4].mixture = cell(top, 330); st.occupied_volumes_m3.assign(6, volume); st.occupied_volumes_m3[5] = 0;
        st.occupied_volumes_m3[4] = top / 1000;
        for (std::size_t y = 4; y-- > 0;) {
            double p = 0; for (int it = 0; it < 8; ++it) p = above_p + 9.81 * (volume * 1000 * (1 + p / 2.2e9) + above_m) / (2 * area);
            const double m = volume * 1000 * (1 + p / 2.2e9); st.cells[y].mixture = cell(m, 300); above_p = p; above_m = m;
        }
        finish(st);
        MacClosedWorld world(s.registry, s.eos, st, gravity);
        const double u0 = st.cells[4].mixture.energy, seconds = .05;
        auto r = world.advanced(seconds, 65536, MacExecutionPolicy::production); world = std::move(r.world);
        require(r.report.interval_reference_fallbacks == 0, "Void conduction fell back to the reference");
        require(std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "Void conduction energy account failed");
        const double moved = u0 - world.state().cells[4].mixture.energy, expected = .6 * area / h * 30 * seconds;
        std::cout << "void conduction: surface cell gave " << moved << " J (k A dT t / h " << expected << ")\n";
        require(std::abs(moved - expected) <= .03 * expected, "A wet bottom face did not conduct across its whole area");
    }
}
// Point transfers in the void phase (MAC_VACUUM.md, "Point transfers"): gas
// injected into an empty cell fills it; removing a cell's last gas empties it;
// removing water from a full cell opens a zero-pressure surface; water poured
// into an empty cell rests at zero pressure; the owner then advances.
void transfers() {
    const auto& s = setup(); const auto nitrogen = s.registry.index("nitrogen"), water = s.registry.index("water");
    auto settings = void_settings(1e-3); settings.reconstruction = MacMaterialReconstruction::limited_linear;
    settings.compression_viscosity = MacCompressionViscosity{};
    auto start = expansion(6, 1, [](std::size_t x, std::size_t) { return x < 2; });
    std::vector<double> m(s.registry.size()); m[water] = 1000;
    start.cells[3].mixture = s.eos->initialize_at_temperature(m, 300, 1).mixture;
    std::vector<double> pure(s.registry.size()); pure[nitrogen] = 1.1;
    start.cells[0].mixture = s.eos->initialize_at_temperature(pure, 300, 1).mixture;
    for (auto& c : start.cells) c.passive_quantities.clear();
    const auto inertia = mac_face_inertias(s.registry, start, *s.eos); start.momentum_x.assign(inertia.x.size(), 0);
    const MacClosedWorld owner(s.registry, s.eos, start, settings);
    const std::vector<MacFluidTransfer> edits{{5, nitrogen, true, .5, 280}, {0, nitrogen, false, 1e9, 0}, {3, water, false, 400, 0}, {4, water, true, 250, 290}};
    // Cell 0 holds pure nitrogen, so one removal empties it. (Removing a mixture
    // species by species would leave a trace holding only rounding residue.)
    const auto result = owner.with_fluid_transfers(edits, MacFluidTransferTopology::occupied_mixture);
    const auto& st = result.world.state();
    require(!st.occupied_volumes_m3.empty() && st.cells[0].mixture.mass() == 0 && mac_material_volume(st, 0) == 0, "Removal did not empty a void-phase cell");
    require(mac_material_volume(st, 5) == st.grid.volume_m3(), "Gas injected into an empty cell did not fill it");
    require(std::abs(mac_material_volume(st, 3) - .6) <= 1e-15 && std::abs(mac_material_volume(st, 4) - .25) <= 1e-15,
        "Water transfers did not leave zero-pressure surfaces");
    auto world = result.world;
    for (int k = 0; k < 4; ++k) {
        auto r = world.advanced(1e-2, 65536, MacExecutionPolicy::production); world = std::move(r.world);
        require(r.report.interval_reference_fallbacks == 0 && std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "Void transfers did not advance");
        static_cast<void>(check_state(world.state()));
    }
    std::cout << "void transfers: emptied, filled and two water surfaces; advanced 0.04 s\n";
}
// Removing a wet cell's last gas opens a zero-pressure liquid surface: the
// water keeps its nominal volume and the owner advances.
void gas_removal_surface() {
    const auto& s = setup(); const auto nitrogen = s.registry.index("nitrogen"), water = s.registry.index("water");
    auto settings = void_settings(1e-3); settings.reconstruction = MacMaterialReconstruction::limited_linear;
    settings.compression_viscosity = MacCompressionViscosity{};
    auto start = expansion(4, 1, [](std::size_t x, std::size_t) { return x < 2; });
    std::vector<double> m(s.registry.size()); m[water] = .685; m[nitrogen] = .01;
    start.cells[1].mixture = s.eos->initialize_at_temperature(m, 300, 1).mixture;
    for (auto& c : start.cells) c.passive_quantities.clear();
    const auto inertia = mac_face_inertias(s.registry, start, *s.eos); start.momentum_x.assign(inertia.x.size(), 0);
    const MacClosedWorld owner(s.registry, s.eos, start, settings);
    const MacFluidTransfer take{1, nitrogen, false, 1e9, 0};
    const auto result = owner.with_fluid_transfers(std::span(&take, 1), MacFluidTransferTopology::occupied_mixture);
    const auto& st = result.world.state();
    require(st.cells[1].mixture.masses[nitrogen] == 0 && std::abs(mac_material_volume(st, 1) - .685e-3) <= 1e-15,
        "Removing a wet cell's last gas did not open a liquid surface");
    // The opened surface takes later edits: some of its water leaves.
    const MacFluidTransfer drain{1, water, false, .2, 0};
    auto world = result.world.with_fluid_transfers(std::span(&drain, 1), MacFluidTransferTopology::occupied_mixture).world;
    require(std::abs(world.state().cells[1].mixture.masses[water] - .485) <= 1e-12, "The opened surface did not take a later removal");
    for (int k = 0; k < 4; ++k) {
        auto r = world.advanced(1e-2, 65536, MacExecutionPolicy::production); world = std::move(r.world);
        require(r.report.interval_reference_fallbacks == 0 && std::abs(r.report.energy_residual_j) <= r.report.energy_bound_j, "The opened surface did not advance");
        static_cast<void>(check_state(world.state()));
    }
    std::cout << "void transfers: a wet cell's last gas removed, then water; advanced 0.04 s\n";
}
void rejections() {
    const auto& s = setup(); const auto start = expansion(8, 1, [](std::size_t x, std::size_t) { return x < 4; });
    for (const auto& change : std::vector<std::function<void(MacClosedSettings&)>>{
             [](MacClosedSettings& c) { c.reconstruction = MacMaterialReconstruction::donor_cell; },
             [](MacClosedSettings& c) { c.thermal_conduction = true; },
             [](MacClosedSettings& c) { c.viscosity.shear_pa_s = 1e-5; },
             [](MacClosedSettings& c) { c.diffusion = {MacDiffusionMode::physical, 1}; }}) {
        auto c = void_settings(); change(c);
        rejects([&] { c.validate(); }, "Invalid MAC vacuum representation", "Unsupported void-phase combination was accepted");
    }
    auto partial = start; partial.occupied_volumes_m3.assign(8, 0); for (std::size_t i = 0; i < 4; ++i) partial.occupied_volumes_m3[i] = .5;
    rejects([&] { MacClosedWorld(s.registry, s.eos, partial, MacClosedSettings{}); }, "require the void phase", "Occupied volumes accepted without the void phase");
    auto oversized = partial; oversized.occupied_volumes_m3[0] = 1.5;
    rejects([&] { MacClosedWorld(s.registry, s.eos, oversized, void_settings()); }, "Invalid MAC occupied volume", "Oversized occupied volume accepted");
    auto missing = partial; missing.occupied_volumes_m3[1] = 0;
    rejects([&] { MacClosedWorld(s.registry, s.eos, missing, void_settings()); }, "Invalid MAC occupied volume", "Occupied cell without volume accepted");
    // The reference path still rejects liquid in a partly filled cell.
    auto wet = start; wet.cells[3].mixture = s.mixture(1, 100000, 300, .5);
    const auto inertia = mac_face_inertias(s.registry, wet, *s.eos); wet.momentum_x.assign(inertia.x.size(), 0);
    rejects([&] { static_cast<void>(MacClosedWorld(s.registry, s.eos, wet, void_settings()).advanced(5e-3, 65536)); },
        "does not yet hold liquid", "Liquid in a partly filled cell was accepted");
}
}

int main() {
    try {
        one_dimensional(); two_dimensional(); gravity(); checkpoint_and_policy(); production();
        liquid_surfaces(MacMaterialReconstruction::limited_linear); liquid_surfaces(MacMaterialReconstruction::interface_plic); void_conduction(); transfers(); gas_removal_surface(); rejections();
        full_range_parcel_geometry();
        std::cout << "MAC void-phase tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
