#include "mac_fixture.hpp"
#include <array>
#include <iomanip>
#include <numbers>

namespace {
using namespace ma;
using namespace ma::experimental;
auto mode = MacMomentumReconstruction::limited_linear;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
void scaled(MacScaled x) { std::cout << ' ' << x.mantissa() << ' ' << x.exponent(); }
bool same(MacScaled a, MacScaled b) { return a.mantissa() == b.mantissa() && a.exponent() == b.exponent(); }
bool fixed(const MacGrid& g, int component, std::size_t f) {
    const auto x = component == 0 ? f % g.x_width() : f % g.width;
    const auto y = component == 0 ? f / g.x_width() : f / g.width;
    if (component == 0) return ((!g.periodic_x && (x == 0 || x == g.width)) ||
        g.walls[(x + g.width - 1) % g.width + y * g.width] || g.walls[x % g.width + y * g.width]);
    return ((!g.periodic_y && (y == 0 || y == g.height)) ||
        g.walls[x + ((y + g.height - 1) % g.height) * g.width] || g.walls[x + (y % g.height) * g.width]);
}
void record(std::size_t id, const MacTransportState& before, const MacMassFluxes& flux, const MacTransportAdvance& after) {
    const auto& g = before.grid; const auto& r = after.report;
    std::cout << "GRID " << id << ' ' << g.width << ' ' << g.height << ' ' << g.spacing_m << ' ' << g.depth_m << ' ' << g.periodic_x << ' ' << g.periodic_y
        << ' ' << r.unresolved_heat_updates; scaled(r.energy_residual_wide_j); scaled(r.energy_bound_wide_j); std::cout << '\n';
    for (std::size_t i = 0; i < g.walls.size(); ++i) {
        std::cout << "CELL " << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << ' ' << before.cells[i].mixture.energy << ' '
            << r.advected_internal_energy_j[i] << ' ' << after.state.cells[i].mixture.energy << ' ' << before.cells[i].mixture.mass() << ' '
            << after.state.cells[i].mixture.mass();
        auto advected = after.state.cells[i].mixture; advected.energy = r.advected_internal_energy_j[i];
        for (const auto* mixture : std::array<const MixtureInventory*, 3>{&before.cells[i].mixture, &advected, &after.state.cells[i].mixture})
            std::cout << ' ' << (g.walls[i] ? 0 : ma::testing::setup().eos->evaluate(*mixture, g.volume_m3()).temperature_k);
        std::cout << '\n';
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            const auto& c = endpoint == 0 ? before.cells[i] : after.state.cells[i];
            for (std::size_t s = 0; s < c.mixture.masses.size(); ++s) if (c.mixture.masses[s] != 0)
                std::cout << "MASS " << id << ' ' << endpoint << ' ' << i << ' ' << s << ' ' << c.mixture.masses[s] << '\n';
            for (const auto& p : c.passive_quantities) std::cout << "PASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
        }
    }
    for (int component = 0; component < 2; ++component) {
        const auto& f = component == 0 ? r.x : r.y;
        const auto j0 = component == 0 ? mac_field_values(before.momentum_x, before.full_momentum_x) : mac_field_values(before.momentum_y, before.full_momentum_y);
        const auto j1 = component == 0 ? mac_field_values(after.state.momentum_x, after.state.full_momentum_x) : mac_field_values(after.state.momentum_y, after.state.full_momentum_y);
        const auto q = component == 0 ? mac_field_values(flux.x, flux.full_x) : mac_field_values(flux.y, flux.full_y);
        std::cout << "REPORT " << id << ' ' << component << ' ' << f.corrections.size() << ' ' << f.limited_corrections << ' '
            << f.unresolved_momentum_corrections << ' ' << f.unresolved_remainder_corrections << '\n';
        for (std::size_t i = 0; i < j0.size(); ++i) {
            std::cout << "FACE " << id << ' ' << component << ' ' << i << ' ' << f.old_inertia[i] << ' ' << f.new_inertia[i] << ' ' << fixed(g, component, i);
            const auto& e = f.energies[i];
            for (const auto& x : {q[i], j0[i], j1[i], f.upwind_momentum[i], f.velocity_slope_x[i], f.velocity_slope_y[i],
                f.centered_courant_x[i], f.centered_courant_y[i], e.old_kinetic, e.new_kinetic, f.upwind_mixing_heat[i], e.mixing_heat,
                f.upwind_transported_kinetic[i], e.transported_kinetic, e.wall_heat, f.full_wall_impulse[i], e.composed_energy_bound}) scaled(x);
            std::cout << '\n';
        }
        for (std::size_t k = 0; k < f.fluxes.size(); ++k) {
            const auto& edge = f.fluxes[k]; std::cout << "DUAL " << id << ' ' << component << ' ' << k << ' ' << edge.left << ' ' << edge.right;
            scaled(mac_dual_mass(edge)); std::cout << '\n';
        }
        for (std::size_t k = 0; k < f.corrections.size(); ++k) {
            const auto& e = f.corrections[k]; const auto& p = e.pair;
            std::cout << "CORRECTION " << id << ' ' << component << ' ' << k << ' ' << e.dual_edge << ' ' << e.transport_axis << ' '
                << p.limiter_roundoff_backoffs << ' ' << p.unresolved_momentum_updates << ' ' << p.unresolved_heat_updates;
            for (const auto& x : {e.predictor_delta_velocity, e.bounded_delta_velocity, p.before_left, p.before_right, p.before_heat_left, p.before_heat_right,
                p.requested_impulse, p.limiter, p.impulse, p.after_left, p.after_right, p.after_heat_left, p.after_heat_right, p.energy_flux, p.heat_change_left, p.heat_change_right}) scaled(x);
            std::cout << '\n';
        }
    }
}
void compare_material(const MacTransportAdvance& donor, const MacTransportAdvance& corrected) {
    for (std::size_t i = 0; i < donor.state.cells.size(); ++i) {
        check(donor.state.cells[i].mixture.masses == corrected.state.cells[i].mixture.masses, "Momentum correction changed material species transfer");
        const auto& a = donor.state.cells[i].passive_quantities; const auto& b = corrected.state.cells[i].passive_quantities;
        check(a.size() == b.size(), "Momentum correction changed passive identity count");
        for (std::size_t k = 0; k < a.size(); ++k) check(a[k].identity == b[k].identity && a[k].quantity == b[k].quantity, "Momentum correction changed passive transfer");
    }
    for (int component = 0; component < 2; ++component) {
        const auto& a = component == 0 ? donor.report.x : donor.report.y; const auto& b = component == 0 ? corrected.report.x : corrected.report.y;
        for (std::size_t i = 0; i < a.energies.size(); ++i) check(same(a.full_wall_impulse[i], b.full_wall_impulse[i]) && same(a.energies[i].wall_heat, b.energies[i].wall_heat),
            "Momentum correction changed fixed-wall reaction/heat");
    }
}
MacMassFluxes fluxes(const MacTransportState& state, double scale) {
    const auto& g = state.grid; MacMassFluxes f{std::vector<double>(state.momentum_x.size()), std::vector<double>(state.momentum_y.size())};
    for (std::size_t i = 0; i < f.x.size(); ++i) if (!fixed(g, 0, i)) f.x[i] = scale * (.5 + .25 * std::sin(static_cast<double>(i)));
    for (std::size_t i = 0; i < f.y.size(); ++i) if (!fixed(g, 1, i)) f.y[i] = -scale * (.5 + .25 * std::cos(static_cast<double>(i)));
    return f;
}
void velocities(MacTransportState& state) {
    const auto& set = ma::testing::setup(); const auto inertia = mac_face_inertias(set.registry, state, *set.eos);
    for (std::size_t i = 0; i < state.momentum_x.size(); ++i) if (!fixed(state.grid, 0, i)) state.momentum_x[i] = inertia.x[i] * (.25 + .125 * std::sin(static_cast<double>(i) + .2));
    for (std::size_t i = 0; i < state.momentum_y.size(); ++i) if (!fixed(state.grid, 1, i)) state.momentum_y[i] = inertia.y[i] * (-.25 + .125 * std::cos(static_cast<double>(i) + .3));
}
void identity() {
    const auto& set = ma::testing::setup(); auto s = set.state(4, 3, true);
    for (std::size_t i = 0; i < s.cells.size(); ++i) s.cells[i].mixture = set.mixture(1, 100000, 300, 0, .05 + .04 * static_cast<double>(i));
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t i = 0; i < s.momentum_x.size(); ++i) s.momentum_x[i] = .5 * inertia.x[i];
    for (std::size_t i = 0; i < s.momentum_y.size(); ++i) s.momentum_y[i] = -.25 * inertia.y[i];
    const auto q = fluxes(s, .01); const auto donor = advance_mac_transport(set.registry, s, q);
    const auto result = advance_mac_transport(set.registry, s, q, mode); compare_material(donor, result);
    check(result.report.x.corrections.empty() && result.report.y.corrections.empty() &&
        ma::testing::fingerprint(donor.state) == ma::testing::fingerprint(result.state), "Uniform velocity reconstruction is not an exact reference identity");
    record(0, s, q, result);
}
void mixed_and_walls() {
    const auto& set = ma::testing::setup(); auto wet = set.state(6, 4, true);
    for (auto& c : wet.cells) { c.mixture = set.mixture(1, 100000, 300, .2); c.passive_quantities = {{17, c.mixture.mass() * .125}, {UINT64_C(0x80000000), c.mixture.mass() * 1e-30}}; }
    velocities(wet);
    // Fixed source volumes require divergence-free sweeps to fill every target
    // volume. Momentum varies independently in both components.
    const MacMassFluxes volume_flux{std::vector<double>(wet.momentum_x.size(), .002),
        std::vector<double>(wet.momentum_y.size(), -.001)};
    const std::vector<double> volumes(wet.cells.size(), 1);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, wet, volumes, volume_flux);
    const auto donor = advance_mac_parcel_transport(set.registry, wet, *set.eos, plan);
    const auto result = advance_mac_parcel_transport(set.registry, wet, *set.eos, plan, mode); compare_material(donor, result);
    check(!result.report.x.corrections.empty() && !result.report.y.corrections.empty(), "Mixed parcel transport did not exercise both reconstruction components");
    record(1, wet, result.report.primal_mass_fluxes_kg, result);
    auto wall = set.state(6, 4, false, true, true); velocities(wall); const auto q = fluxes(wall, .01);
    const auto a = advance_mac_transport(set.registry, wall, q); const auto b = advance_mac_transport(set.registry, wall, q, mode); compare_material(a, b);
    check(!b.report.x.corrections.empty() && !b.report.y.corrections.empty(), "Wall fixture did not exercise interior reconstruction"); record(2, wall, q, b);
}
void tiny() {
    const auto& set = ma::testing::setup(); auto s = set.state(6, 3, true); const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    std::vector<MacScaled> j(s.momentum_x.size()); const auto unit = MacScaled::from_parts(.75, -1200);
    for (std::size_t i = 0; i < j.size(); ++i) j[i] = unit.multiplied(static_cast<double>(i % 6)).multiplied(inertia.x[i]);
    store_mac_field(s.momentum_x, s.full_momentum_x, j);
    MacMassFluxes q{std::vector<double>(j.size(), .125 * s.cells[0].mixture.mass()), std::vector<double>(s.momentum_y.size())};
    const auto result = advance_mac_transport(set.registry, s, q, mode);
    check(!result.report.x.corrections.empty() && !result.state.full_momentum_x.empty() && result.report.unresolved_heat_updates > 0,
        "Below-range reconstructed momentum/heat was discarded or unresolved heat unreported"); record(3, s, q, result);
}
double wave(std::size_t n, bool transverse, bool corrected) {
    const auto& set = ma::testing::setup(); const std::size_t height = transverse ? n : 2;
    auto s = set.state(n, height, true); s.grid.spacing_m = 1. / static_cast<double>(n);
    for (auto& c : s.cells) { c.mixture = set.mixture(s.grid.volume_m3(), 100000, 300, 0); c.passive_quantities = {{17, c.mixture.mass() * .125}, {UINT64_C(0x80000000), c.mixture.mass() * 1e-30}}; }
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos); const double mass = s.cells[0].mixture.mass();
    std::vector<double> initial(n);
    for (std::size_t i = 0; i < n; ++i) initial[i] = 1 + .2 * std::sin(2 * std::numbers::pi * (static_cast<double>(i) + .5) / static_cast<double>(n));
    for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < n; ++x) {
        const auto fx = s.grid.x_face(x, y), fy = s.grid.y_face(x, y);
        s.momentum_x[fx] = inertia.x[fx] * initial[transverse ? y : x];
        s.momentum_y[fy] = transverse ? inertia.y[fy] : 0;
    }
    MacEnergyAccount account;
    const auto energy = [&](const MacTransportState& a, bool negative) {
        for (const auto& cell : a.cells) account.add(negative ? -cell.mixture.energy : cell.mixture.energy);
        const auto m = mac_face_inertias(set.registry, a, *set.eos);
        const auto jx = mac_field_values(a.momentum_x, a.full_momentum_x), jy = mac_field_values(a.momentum_y, a.full_momentum_y);
        for (std::size_t i = 0; i < jx.size(); ++i) { const auto k = MacEnergy::kinetic(m.x[i], jx[i]); account.add(negative ? k.negated() : k); }
        for (std::size_t i = 0; i < jy.size(); ++i) { const auto k = MacEnergy::kinetic(m.y[i], jy[i]); account.add(negative ? k.negated() : k); }
    };
    energy(s, true); std::size_t events = 0; const std::size_t periods = transverse ? 10 : 5;
    for (std::size_t step = 0; step < periods * n; ++step) {
        MacMassFluxes q{std::vector<double>(s.momentum_x.size()), std::vector<double>(s.momentum_y.size())};
        for (std::size_t i = 0; i < q.x.size(); ++i) q.x[i] = mass * (transverse ? .1 * (s.momentum_x[i] / inertia.x[i]) : .2);
        if (transverse) std::fill(q.y.begin(), q.y.end(), .1 * mass);
        auto result = advance_mac_transport(set.registry, s, q, corrected ? mode : MacMomentumReconstruction::donor_cell);
        events += result.report.x.corrections.size() + result.report.y.corrections.size();
        for (const auto* report : {&result.report.x, &result.report.y}) for (const auto& e : report->energies)
            check(e.mixing_heat.mantissa() >= 0, "Wave reconstruction produced negative pending heat");
        for (const auto& c : result.state.cells) {
            check(set.eos->evaluate(c.mixture, s.grid.volume_m3()).temperature_k > 0 &&
                c.mixture.masses[set.registry.index("helium")] > 0 && c.passive_quantities.back().quantity > 0,
                "Wave transport left the EOS domain or lost positive trace inventory");
        }
        s = std::move(result.state);
    }
    energy(s, false); check(account.within_bound(), "Reconstructed wave endpoint total U+K exceeds original operand bound");
    const auto final_mass = mac_face_inertias(set.registry, s, *set.eos); double error = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto f = transverse ? s.grid.x_face(0, i) : s.grid.x_face(i, 0);
        const double u = s.momentum_x[f] / final_mass.x[f];
        error += std::abs(u - initial[i]) / static_cast<double>(n);
        for (std::size_t other = 1; other < (transverse ? n : height); ++other) {
            const auto at = transverse ? s.grid.x_face(other, i) : s.grid.x_face(i, other);
            check(s.momentum_x[at] / final_mass.x[at] == u, "Wave developed variation in its invariant coordinate");
        }
        if (corrected) std::cout << "PROFILE " << transverse << ' ' << n << ' ' << i << ' ' << initial[i] << ' ' << u << '\n';
    }
    std::cout << "WAVE " << transverse << ' ' << n << ' ' << corrected << ' ' << error << ' ' << events;
    scaled(account.residual()); scaled(account.bound()); std::cout << '\n'; return error;
}
void normal_wave() {
    const auto a = wave(32, false, true), b = wave(64, false, true), c = wave(128, false, true), donor = wave(128, false, false);
    check(b < .35 * a && c < .35 * b && c < .3 * donor, "Normal reconstructed momentum fails original scalar refinement limits");
}
void transverse_wave() {
    const auto a = wave(8, true, true), b = wave(16, true, true), c = wave(32, true, true), donor = wave(32, true, false);
    check(b < .45 * a && c < .45 * b && c < .4 * donor, "Transverse reconstructed momentum fails declared 2D refinement limits");
}
void invalid() {
    const auto& set = ma::testing::setup(); const auto s = set.state(4, 3, false); std::size_t rejected = 0;
    for (int kind = 0; kind < 4; ++kind) {
        auto bad = s; auto q = fluxes(bad, .01); auto setting = mode;
        if (kind == 0) setting = static_cast<MacMomentumReconstruction>(99);
        if (kind == 1) q.x[0] = .01;
        if (kind == 2) { bad.full_momentum_x.assign(bad.momentum_x.size(), {}); bad.full_momentum_x[1] = MacScaled::from_double(.125); }
        if (kind == 3) q.y.pop_back();
        const auto held = ma::testing::fingerprint(bad);
        try { static_cast<void>(advance_mac_transport(set.registry, bad, q, setting)); }
        catch (const std::invalid_argument&) { ++rejected; }
        check(ma::testing::fingerprint(bad) == held, "Rejected reconstruction modified source inventory or momentum");
    }
    check(rejected == 4, "Invalid reconstruction input accepted"); std::cout << "REJECTED " << rejected << '\n';
}
}
int main(int argc, char** argv) { try {
    if (argc == 2 && std::string(argv[1]) == "--quadratic") mode = MacMomentumReconstruction::quadratic_limited_linear;
    else check(argc == 1, "Invalid momentum reconstruction control arguments");
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    if (mode == MacMomentumReconstruction::quadratic_limited_linear) std::cout << "QMODE 2\n";
    std::cout << "TRACE " << ma::testing::setup().registry.index("helium") << ' ' << UINT64_C(0x80000000) << '\n';
    identity(); std::cout << "PASS uniform velocity material reference identity\n";
    mixed_and_walls(); std::cout << "PASS mixed parcels and impermeable fixed wall reactions\n";
    tiny(); std::cout << "PASS full-range reconstructed momentum and unresolved heat\n";
    invalid(); std::cout << "PASS immutable invalid reconstruction rejection\n";
    normal_wave(); std::cout << "PASS normal momentum full-period refinement\n";
    transverse_wave(); std::cout << "PASS transverse momentum 2D full-period refinement\n"; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
