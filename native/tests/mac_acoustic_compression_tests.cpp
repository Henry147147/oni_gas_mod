#include "mac_acoustic_compression_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma; using namespace ma::experimental;
constexpr auto acoustic = MacCompressionLaw::acoustic_quadratic;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
MacCompressionPair pair(std::size_t id, double il, double ir, double ul, double ur,
    double b, double speed, bool fl = false, bool fr = false) {
    auto p = relax_mac_compression_pair(il, ir, MacScaled::from_double(il * ul), MacScaled::from_double(ir * ur), fl, fr,
        MacScaled::from_double(b), MacScaled::from_double(speed));
    ma::testing::export_acoustic_pair(id, p); return p;
}
void analytic() {
    const auto full = pair(0, 2, 4, 3, -2, .125, 2);
    static_cast<void>(pair(1, 2, 4, 3, -2, 1e8, 2));
    static_cast<void>(pair(2, 2, 4, 0, -2, .125, 2, true));
    static_cast<void>(pair(3, 2, 4, 3, 0, .125, 2, false, true));
    const auto half = pair(4, 2, 4, 3, -2, .0625, 2);
    const auto twice = relax_mac_compression_pair(2, 4, half.after_left, half.after_right, false, false,
        MacScaled::from_double(.0625), MacScaled::from_double(2));
    ma::testing::export_acoustic_pair(5, twice);
    for (bool right : {false, true}) {
        MacScaledAccount difference; difference.add(right ? full.after_right : full.after_left);
        difference.add((right ? twice.after_right : twice.after_left).negated());
        check(difference.within_bound(), "Acoustic pair semigroup changed momentum");
    }
    MacEnergyAccount heat; heat.add(half.heat); heat.add(twice.heat); heat.add(full.heat.negated());
    check(heat.within_bound(), "Acoustic pair semigroup changed heat");
    const auto zero = pair(6, 2, 4, 3, -2, .125, 0);
    const auto old = relax_mac_compression_pair(2, 4, MacScaled::from_double(6), MacScaled::from_double(-8), false, false, MacScaled::from_double(.125));
    check(zero.impulse.mantissa() == old.impulse.mantissa() && zero.impulse.exponent() == old.impulse.exponent(), "Zero acoustic term changed the quadratic reference");
}
void ranges() {
    const auto tiny = MacScaled::from_parts(.75, -1941);
    auto p = relax_mac_compression_pair(1, 2, tiny, tiny.negated(), false, false, MacScaled::from_double(.125), MacScaled::from_double(2));
    ma::testing::export_acoustic_pair(10, p);
    check(p.impulse.mantissa() > 0 && p.heat.mantissa() > 0 && p.impulse.project().rounded && p.heat.project().rounded && p.after_right.mantissa() < 0,
        "Acoustic subrange impulse/heat disappeared");
    p = relax_mac_compression_pair(1, 2, MacScaled::from_double(1), {}, false, false, MacScaled::from_parts(.5, -2000), MacScaled::from_double(2));
    ma::testing::export_acoustic_pair(11, p);
    check(p.impulse.mantissa() > 0 && p.after_right.mantissa() > 0 && p.unresolved_momentum_updates == 1, "Tiny acoustic coefficient lost a new full face or unresolved flag");
    p = relax_mac_compression_pair(1, 2, MacScaled::from_double(1), {}, false, false, MacScaled::from_double(.125), MacScaled::from_parts(.5, 2000));
    ma::testing::export_acoustic_pair(12, p);
    p = relax_mac_compression_pair(1, 2, MacScaled::from_double(1), {}, false, false, MacScaled::from_parts(.5, 2000), MacScaled::from_double(2));
    ma::testing::export_acoustic_pair(13, p);
    p = relax_mac_compression_pair(1, 2, tiny, {}, false, false, MacScaled::from_double(.125), MacScaled::from_parts(.5, -2000));
    ma::testing::export_acoustic_pair(14, p);
    check(p.heat.mantissa() > 0 && p.impulse.mantissa() > 0, "Tiny linear and quadratic contributions disappeared");
}
void inactive() {
    check(pair(20, 2, 4, 3, 3, .125, 2).impulse.is_zero(), "Uniform velocity activated acoustic compression");
    check(pair(21, 2, 4, -2, 3, .125, 2).impulse.is_zero(), "Expansion activated acoustic compression");
    const auto base = pair(22, 2, 4, 3, -2, .125, 2), boost = pair(23, 2, 4, 1027, 1022, .125, 2);
    check(base.impulse.mantissa() == boost.impulse.mantissa() && base.impulse.exponent() == boost.impulse.exponent() &&
        base.heat.mantissa() == boost.heat.mantissa() && base.heat.exponent() == boost.heat.exponent(), "Galilean shift changed acoustic work");
    const auto& set = ma::testing::setup(); auto s = set.state(3, 3, true);
    const auto m = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 3; ++x)
        s.momentum_x[s.grid.x_face(x, y)] = m.x[s.grid.x_face(x, y)] * (static_cast<double>(y) - 1);
    const auto result = advance_mac_compression(set.registry, *set.eos, s, .001, acoustic);
    check(result.report.events.empty() && ma::testing::fingerprint(s) == ma::testing::fingerprint(result.state), "Acoustic compression damped tangential shear");
    s = set.state(3, 3, true, true);
    const auto rest = advance_mac_compression(set.registry, *set.eos, s, .001, acoustic);
    check(rest.report.events.empty() && ma::testing::fingerprint(s) == ma::testing::fingerprint(rest.state), "Stationary wet composition contact changed");
    ma::testing::export_acoustic_compression(6, s, rest, .001);
}
MacTransportState compressing(bool periodic, bool wet, bool corner = false, bool liquid = false) {
    const auto& set = ma::testing::setup(); auto s = set.state(3, 3, periodic, wet, corner);
    if (liquid) for (std::size_t i = 0; i < s.cells.size(); ++i) if (!s.grid.walls[i])
        s.cells[i].mixture = set.mixture(1, 2.2e7, 300, 1);
    const auto m = mac_face_inertias(set.registry, s, *set.eos);
    for (int axis = 0; axis < 2; ++axis) {
        auto& j = axis == 0 ? s.momentum_x : s.momentum_y; const auto& mass = axis == 0 ? m.x : m.y;
        for (std::size_t f = 0; f < j.size(); ++f) if (!ma::testing::compression_fixed_face(s.grid, axis, f))
            j[f] = mass[f] * (axis == 0 ? (f % s.grid.x_width() == 1 ? 12 : -8) : (f / s.grid.width == 1 ? 5 : -3));
    }
    return s;
}
void grids() {
    const auto& set = ma::testing::setup();
    for (std::size_t id = 0; id < 4; ++id) {
        const auto s = compressing(id == 1 || id == 2, id < 2, id == 0 || id == 3, id == 3);
        const auto held = ma::testing::fingerprint(s); const auto result = advance_mac_compression(set.registry, *set.eos, s, .001, acoustic);
        check(ma::testing::fingerprint(s) == held && result.report.heat.mantissa() > 0 && !result.report.events.empty(), "Acoustic compression changed source or did not heat a compressing grid");
        ma::testing::export_acoustic_compression(id, s, result, .001);
    }
}
MacTransportState transpose(const MacTransportState& s) {
    auto t = s; std::swap(t.grid.width, t.grid.height); std::swap(t.grid.periodic_x, t.grid.periodic_y);
    for (std::size_t y = 0; y < s.grid.height; ++y) for (std::size_t x = 0; x < s.grid.width; ++x) {
        t.cells[y + x * t.grid.width] = s.cells[x + y * s.grid.width]; t.grid.walls[y + x * t.grid.width] = s.grid.walls[x + y * s.grid.width];
    }
    t.momentum_x.assign(s.momentum_y.size(), 0); t.momentum_y.assign(s.momentum_x.size(), 0);
    for (std::size_t y = 0; y < s.grid.height; ++y) for (std::size_t x = 0; x < s.grid.x_width(); ++x) t.momentum_y[t.grid.y_face(y, x)] = s.momentum_x[s.grid.x_face(x, y)];
    for (std::size_t y = 0; y < s.grid.y_height(); ++y) for (std::size_t x = 0; x < s.grid.width; ++x) t.momentum_x[t.grid.x_face(y, x)] = s.momentum_y[s.grid.y_face(x, y)];
    return t;
}
void geometry_and_range() {
    const auto& set = ma::testing::setup(); const auto a = compressing(false, true, true), b = transpose(a);
    const auto ra = advance_mac_compression(set.registry, *set.eos, a, .001, acoustic), rb = advance_mac_compression(set.registry, *set.eos, b, .001, acoustic);
    const auto expected = transpose(ra.state);
    check(expected.momentum_x == rb.state.momentum_x && expected.momentum_y == rb.state.momentum_y, "Acoustic compression transpose changed momentum");
    for (std::size_t i = 0; i < expected.cells.size(); ++i) {
        MacEnergyAccount error; error.add(expected.cells[i].mixture.energy); error.add(-rb.state.cells[i].mixture.energy);
        check(error.within_bound(), "Acoustic compression transpose changed stored heat");
    }
    ma::testing::export_acoustic_compression(4, b, rb, .001);
    auto tiny = set.state(2, 2, true); std::vector<MacScaled> j(tiny.momentum_x.size());
    for (std::size_t i = 0; i < j.size(); ++i) j[i] = MacScaled::from_parts(i % 2 == 0 ? .75 : -.75, -1941);
    store_mac_field(tiny.momentum_x, tiny.full_momentum_x, j);
    const auto result = advance_mac_compression(set.registry, *set.eos, tiny, .001, acoustic);
    check(result.report.heat.mantissa() > 0 && result.report.heat.project().rounded && result.report.unresolved_heat_updates == 0 && ma::testing::has_primary_energy_remainder(result.state),
        "Acoustic grid lost positive below-range heat or its primary remainder");
    ma::testing::export_acoustic_compression(5, tiny, result, .001);
}
void tangents() {
    const auto& set = ma::testing::setup();
    for (const std::size_t id : {0U, 2U, 3U}) {
        const auto s = compressing(id == 2, id == 0, id != 2, id == 3);
        const auto result = advance_mac_compression(set.registry, *set.eos, s, .001, acoustic);
        const auto anchor = set.eos->reference().make_isentrope(set.eos->to_reference(s.cells[0].mixture, 1));
        const auto lo = set.eos->reference().isentropic_at_volume(anchor, 1 - 1e-5);
        const auto hi = set.eos->reference().isentropic_at_volume(anchor, 1 + 1e-5);
        const auto difference = lo.state.mechanical_pressure_pa - hi.state.mechanical_pressure_pa;
        const auto speed = result.report.source_linear_speeds[0].require_representable();
        const auto measured = std::sqrt(difference / (2e-5 * s.cells[0].mixture.mass())) / 2;
        check(difference > 0 && std::abs(speed - measured) < 1e-5 * speed, "Acoustic speed disagrees with independent isentropic finite difference");
        std::cout << "ATANGENT " << id << " 0 " << 1e-5 << ' ' << lo.state.mechanical_pressure_pa << ' ' << hi.state.mechanical_pressure_pa << '\n';
    }
}
void invalid() {
    const auto& set = ma::testing::setup(); const auto source = compressing(false, false); std::size_t rejected = 0;
    for (int kind = 0; kind < 10; ++kind) {
        auto bad = source; double dt = .001; auto law = acoustic;
        if (kind == 0) dt = 0;
        if (kind == 1) dt = -1;
        if (kind == 2) dt = std::numeric_limits<double>::infinity();
        if (kind == 3) bad.momentum_x.pop_back();
        if (kind == 4) bad.momentum_x[0] = 1;
        if (kind == 5) bad.full_momentum_x.assign(bad.momentum_x.size(), {});
        if (kind == 6) law = static_cast<MacCompressionLaw>(99);
        const auto held = ma::testing::fingerprint(bad);
        try {
            if (kind >= 7) static_cast<void>(relax_mac_compression_pair(kind == 7 ? 0 : 1, 1, {}, {}, false, false,
                MacScaled::from_double(kind == 8 ? -1 : 1), MacScaled::from_double(kind == 9 ? -1 : 1)));
            else static_cast<void>(advance_mac_compression(set.registry, *set.eos, bad, dt, law));
        } catch (const std::invalid_argument&) { ++rejected; }
        check(ma::testing::fingerprint(bad) == held, "Rejected acoustic compression changed its source");
    }
    check(rejected == 10, "Invalid acoustic compression input was accepted"); std::cout << "AREJECTED " << rejected << '\n';
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& set = ma::testing::setup();
    for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    analytic(); std::cout << "PASS acoustic exact pairs, semigroup and quadratic reference\n";
    ranges(); std::cout << "PASS full-range acoustic impulse, heat and stiff relaxation\n";
    inactive(); std::cout << "PASS inactive contacts, shear and Galilean work\n";
    grids(); std::cout << "PASS occupied gas, wet and liquid grids with wall and entropy accounts\n";
    geometry_and_range(); std::cout << "PASS transposed acoustic grid and unresolved heat\n";
    tangents(); std::cout << "PASS gas, wet and liquid acoustic tangents\n";
    invalid(); std::cout << "PASS atomic acoustic invalid-input rejection\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
