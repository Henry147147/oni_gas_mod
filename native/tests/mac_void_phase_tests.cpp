// Void phase (MAC_VACUUM.md, "Void phase"): gas entering vacuum keeps its own
// volume, so each cell holds material and exact vacuum separated by a PLIC line.
#include "mac_fixture.hpp"
#include <cstring>
#include <functional>

namespace {
using namespace ma::testing;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
template<class F> void rejects(F&& f, const char* text, const char* what) {
    try { f(); } catch (const std::exception& e) { if (std::strstr(e.what(), text)) return; throw std::runtime_error(std::string(what) + ": " + e.what()); }
    throw std::runtime_error(what);
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
// Occupied volumes are valid and every partly filled cell borders vacuum or a
// partly filled cell (material never encloses a void).
std::size_t check_state(const MacTransportState& st) {
    const auto& g = st.grid; const double v = g.volume_m3(); std::size_t partial = 0;
    for (std::size_t i = 0; i < st.cells.size(); ++i) {
        const double mass = st.cells[i].mixture.mass(), occupied = mac_material_volume(st, i);
        if (mass == 0) { require(st.occupied_volumes_m3.empty() || occupied == 0, "Empty void-phase cell has an occupied volume"); continue; }
        require(occupied > 0 && occupied <= v, "Occupied void-phase cell has an invalid volume");
        if (occupied == v) continue;
        ++partial; const auto x = i % g.width, y = i / g.width; bool open = false;
        for (const auto& [dx, dy] : {std::pair<int, int>{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            const long nx = static_cast<long>(x) + dx, ny = static_cast<long>(y) + dy;
            if (nx < 0 || ny < 0 || nx >= static_cast<long>(g.width) || ny >= static_cast<long>(g.height)) continue;
            const auto n = static_cast<std::size_t>(nx) + static_cast<std::size_t>(ny) * g.width;
            if (!g.walls[n] && mac_material_volume(st, n) < v) open = true;
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
// production policy falls back to the reference for the void phase.
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
    require(production.report.interval_reference_fallbacks > 0, "Void phase did not fall back to the reference path");
    require(fingerprint(production.world.state()) == fingerprint(a.state()), "Void-phase production fallback differs from the reference");
    // Legacy owners keep their checkpoint version.
    MacClosedSettings legacy; legacy.maximum_timestep_s = 2e-4;
    const auto old = MacClosedWorld(s.registry, s.eos, expansion(8, 1, [](std::size_t x, std::size_t) { return x < 4; }), legacy).checkpoint();
    require(old[8] == 2, "Legacy owner changed checkpoint version");
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
    const MacClosedWorld owner(s.registry, s.eos, partial, void_settings());
    const MacGasTransfer edit{0, s.registry.index("nitrogen"), true, 1e-6, 300};
    rejects([&] { static_cast<void>(owner.with_gas_transfers(std::span(&edit, 1))); }, "partly filled void-phase", "Point transfer accepted a partly filled state");
    // Liquid entering vacuum would need a liquid/void interface.
    auto wet = start; wet.cells[3].mixture = s.mixture(1, 100000, 300, .5);
    const auto inertia = mac_face_inertias(s.registry, wet, *s.eos); wet.momentum_x.assign(inertia.x.size(), 0);
    rejects([&] { static_cast<void>(MacClosedWorld(s.registry, s.eos, wet, void_settings()).advanced(5e-3, 65536)); },
        "does not yet hold liquid", "Liquid in a partly filled cell was accepted");
}
}

int main() {
    try {
        one_dimensional(); two_dimensional(); gravity(); checkpoint_and_policy(); rejections();
        std::cout << "MAC void-phase tests passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
