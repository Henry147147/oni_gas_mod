#include "mac_compression_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacCompressionPair pair(std::size_t id, double il, double ir, double ul, double ur, double b, bool fl = false, bool fr = false) {
    auto p = relax_mac_compression_pair(il, ir, MacScaled::from_double(il * ul), MacScaled::from_double(ir * ur), fl, fr, MacScaled::from_double(b));
    ma::testing::export_compression_pair(id, p); return p;
}
void analytic_pairs() {
    static_cast<void>(pair(0, 2, 4, 3, -2, .125));
    static_cast<void>(pair(1, 2, 4, 3, -2, 1e8));
    static_cast<void>(pair(2, 2, 4, 0, -2, .125, true));
    static_cast<void>(pair(3, 2, 4, 3, 0, .125, false, true));
    const auto half = pair(4, 2, 4, 3, -2, .0625);
    const auto twice = relax_mac_compression_pair(2, 4, half.after_left, half.after_right, false, false, MacScaled::from_double(.0625));
    ma::testing::export_compression_pair(5, twice);
    const auto full = relax_mac_compression_pair(2, 4, MacScaled::from_double(6), MacScaled::from_double(-8), false, false, MacScaled::from_double(.125));
    MacScaledAccount same; same.add(twice.after_left); same.add(full.after_left.negated());
    check(same.within_bound(), "Compression pair semigroup differs");
}
void range_pairs() {
    const auto tiny = MacScaled::from_parts(.75, -1941);
    const auto p = relax_mac_compression_pair(1, 2, tiny, tiny.negated(), false, false, MacScaled::from_double(.125));
    ma::testing::export_compression_pair(6, p);
    check(p.impulse.mantissa() > 0 && p.heat.mantissa() > 0 && p.impulse.project().rounded && p.heat.project().rounded &&
        p.unresolved_momentum_updates == 2, "Compression below-range impulse/heat or unresolved update was lost");
    const auto q = relax_mac_compression_pair(1, 2, MacScaled::from_parts(.75, -1200), {}, false, false, MacScaled::from_parts(.5, -1200));
    ma::testing::export_compression_pair(7, q); check(q.impulse.mantissa() > 0 && q.after_right.mantissa() > 0, "Compression failed to create full target J");
}
void inactive_and_boost() {
    check(pair(8, 2, 4, 3, 3, .125).impulse.is_zero(), "Uniform velocity activates compression");
    check(pair(9, 2, 4, -2, 3, .125).impulse.is_zero(), "Expansion activates compression");
    const auto base = pair(10, 2, 4, 3, -2, .125), boosted = pair(11, 2, 4, 1027, 1022, .125);
    check(base.impulse.mantissa() == boosted.impulse.mantissa() && base.impulse.exponent() == boosted.impulse.exponent() &&
        base.heat.mantissa() == boosted.heat.mantissa() && base.heat.exponent() == boosted.heat.exponent(), "Pair work changes under exact Galilean shift");
    const auto& set = ma::testing::setup(); auto state = set.state(3, 3, true); const auto mass = mac_face_inertias(set.registry, state, *set.eos);
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 3; ++x)
        state.momentum_x[state.grid.x_face(x, y)] = mass.x[state.grid.x_face(x, y)] * (static_cast<double>(y) - 1);
    const auto held = ma::testing::fingerprint(state); const auto result = advance_mac_compression(set.registry, *set.eos, state, .01);
    check(result.report.events.empty() && ma::testing::fingerprint(result.state) == held, "Tangential shear was damped");
    std::cout << "INACTIVE 1\n";
}
MacTransportState compressing(bool periodic, bool wet, bool corner = false) {
    const auto& set = ma::testing::setup(); auto s = set.state(3, 3, periodic, wet, corner);
    const auto m = mac_face_inertias(set.registry, s, *set.eos);
    for (int axis = 0; axis < 2; ++axis) {
        auto& j = axis == 0 ? s.momentum_x : s.momentum_y; const auto& mass = axis == 0 ? m.x : m.y;
        for (std::size_t f = 0; f < j.size(); ++f) if (!ma::testing::compression_fixed_face(s.grid, axis, f))
            j[f] = mass[f] * (axis == 0 ? (f % s.grid.x_width() == 1 ? 12 : -8) : (f / s.grid.width == 1 ? 5 : -3));
    }
    return s;
}
void occupied_grids() {
    const auto& set = ma::testing::setup();
    for (std::size_t id = 0; id < 2; ++id) {
        auto s = compressing(id == 1, true, id == 0); const auto held = ma::testing::fingerprint(s);
        const auto result = advance_mac_compression(set.registry, *set.eos, s, .01);
        check(ma::testing::fingerprint(s) == held && result.report.heat.mantissa() > 0 && !result.report.events.empty(), "Compression changed source or failed to heat occupied grid");
        ma::testing::export_compression(id, s, result, .01);
    }
}
MacTransportState transpose(const MacTransportState& s) {
    auto t = s; std::swap(t.grid.width, t.grid.height); std::swap(t.grid.periodic_x, t.grid.periodic_y);
    t.momentum_x.assign(s.momentum_y.size(), 0); t.momentum_y.assign(s.momentum_x.size(), 0);
    for (std::size_t y = 0; y < s.grid.height; ++y) for (std::size_t x = 0; x < s.grid.width; ++x) {
        t.cells[y + x * t.grid.width] = s.cells[x + y * s.grid.width]; t.grid.walls[y + x * t.grid.width] = s.grid.walls[x + y * s.grid.width];
    }
    for (std::size_t y = 0; y < s.grid.height; ++y) for (std::size_t x = 0; x < s.grid.x_width(); ++x) t.momentum_y[t.grid.y_face(y, x)] = s.momentum_x[s.grid.x_face(x, y)];
    for (std::size_t y = 0; y < s.grid.y_height(); ++y) for (std::size_t x = 0; x < s.grid.width; ++x) t.momentum_x[t.grid.x_face(y, x)] = s.momentum_y[s.grid.y_face(x, y)];
    return t;
}
void transpose_and_range() {
    const auto& set = ma::testing::setup(); const auto a = compressing(false, true, true), b = transpose(a);
    const auto ra = advance_mac_compression(set.registry, *set.eos, a, .01), rb = advance_mac_compression(set.registry, *set.eos, b, .01);
    const auto expected = transpose(ra.state);
    for (std::size_t i = 0; i < expected.cells.size(); ++i) {
        MacEnergyAccount error; error.add(expected.cells[i].mixture.energy); error.add(-rb.state.cells[i].mixture.energy);
        check(error.within_bound(), "Transposed compression energy differs");
    }
    check(expected.momentum_x == rb.state.momentum_x && expected.momentum_y == rb.state.momentum_y, "Transposed compression J differs");
    ma::testing::export_compression(2, b, rb, .01);
    auto tiny = set.state(2, 2, true); std::vector<MacScaled> j(tiny.momentum_x.size());
    for (std::size_t i = 0; i < j.size(); ++i) j[i] = MacScaled::from_parts(i % 2 == 0 ? .75 : -.75, -1941);
    store_mac_field(tiny.momentum_x, tiny.full_momentum_x, j);
    const auto result = advance_mac_compression(set.registry, *set.eos, tiny, .01);
    check(result.report.heat.mantissa() > 0 && result.report.unresolved_heat_updates == 0 && !result.state.cells[0].energy_remainder_j.is_zero() && result.report.unresolved_momentum_updates > 0,
        "Tiny compression did not retain heat while reporting unresolved J"); ma::testing::export_compression(3, tiny, result, .01);
}
void invalid_inputs() {
    const auto& set = ma::testing::setup(); const auto source = compressing(false, false); std::size_t rejected = 0;
    for (int kind = 0; kind < 8; ++kind) {
        auto bad = source; double dt = .01;
        if (kind == 0) dt = 0;
        if (kind == 1) dt = -1;
        if (kind == 2) dt = std::numeric_limits<double>::infinity();
        if (kind == 3) bad.momentum_x.pop_back();
        if (kind == 4) bad.momentum_x[0] = 1;
        if (kind == 5) { bad.full_momentum_x.assign(bad.momentum_x.size(), {}); }
        const auto held = ma::testing::fingerprint(bad);
        try {
            if (kind == 6) static_cast<void>(relax_mac_compression_pair(0, 1, {}, {}, false, false, MacScaled::from_double(1)));
            else if (kind == 7) static_cast<void>(relax_mac_compression_pair(1, 1, {}, {}, false, false, MacScaled::from_double(-1)));
            else static_cast<void>(advance_mac_compression(set.registry, *set.eos, bad, dt));
        } catch (const std::invalid_argument&) { ++rejected; }
        check(ma::testing::fingerprint(bad) == held, "Invalid compression input was mutated");
    }
    check(rejected == 8, "Invalid compression input was accepted"); std::cout << "REJECTED " << rejected << '\n';
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& set = ma::testing::setup();
    for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    analytic_pairs(); std::cout << "PASS analytic compression pairs and semigroup\n";
    range_pairs(); std::cout << "PASS subrange impulses and heat\n";
    inactive_and_boost(); std::cout << "PASS inactive flow and Galilean work\n";
    occupied_grids(); std::cout << "PASS occupied mixed grids with wall and entropy accounts\n";
    transpose_and_range(); std::cout << "PASS transpose and unresolved full-range updates\n";
    invalid_inputs(); std::cout << "PASS immutable invalid-input rejection\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
