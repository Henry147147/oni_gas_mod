#include "mac_acoustic_compression_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma; using namespace ma::experimental;
constexpr auto sensed = MacCompressionLaw::pressure_sensed_acoustic;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void triple(std::size_t id, double left, double centre, double right) {
    const auto value = mac_compression_pressure_sensor(left, centre, right);
    std::cout << "PTRIPLE " << id << ' ' << left << ' ' << centre << ' ' << right;
    ma::testing::export_compression_scaled(value); std::cout << '\n';
    if (id == 5) check(value.mantissa() > 0 && value.project().rounded && value.project().value == 0,
        "Positive subrange pressure curvature disappeared");
}
void analytic() {
    triple(0, 1, 1, 1); triple(1, 1, 2, 3); triple(2, 1, 1, 4); triple(3, 4, 1, 4); triple(4, 1, 4, 1);
    triple(5, std::ldexp(1.0, 1000), std::ldexp(1.0, 999), std::numeric_limits<double>::denorm_min());
    const auto maximum = std::numeric_limits<double>::max(); triple(6, maximum, maximum, maximum); triple(7, 0, 0, 0);
}
MacTransportState source(std::size_t w, std::size_t h, bool periodic, bool wet, bool corner, bool uniform = false) {
    const auto& set = ma::testing::setup(); auto s = set.state(w, h, periodic, wet, corner);
    for (std::size_t i = 0; i < s.cells.size(); ++i) if (!s.grid.walls[i])
        s.cells[i].mixture = set.mixture(1, uniform || i % w < w / 2 ? 100000 : 400000, 300,
            wet ? .2 + .03 * static_cast<double>(i % 3) : 0);
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (int axis = 0; axis < 2; ++axis) {
        auto& j = axis == 0 ? s.momentum_x : s.momentum_y; const auto& m = axis == 0 ? inertia.x : inertia.y;
        for (std::size_t f = 0; f < j.size(); ++f) if (!ma::testing::compression_fixed_face(s.grid, axis, f))
            j[f] = m[f] * (axis == 0 ? (f % s.grid.x_width() == 1 ? 12 : -8) : (f / s.grid.width == 1 ? 5 : -3));
    }
    return s;
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
void grids() {
    const auto& set = ma::testing::setup(); const auto gas = source(5, 3, true, false, false), wet = source(4, 3, false, true, true);
    const auto rotated = transpose(wet), uniform = source(3, 3, true, false, false, true);
    const std::array states{gas, wet, rotated, uniform}; std::vector<MacCompressionAdvance> results;
    for (std::size_t id = 0; id < states.size(); ++id) {
        const auto& s = states[id]; const auto held = ma::testing::fingerprint(s);
        auto result = advance_mac_compression(set.registry, *set.eos, s, .001, sensed);
        check(ma::testing::fingerprint(s) == held && result.report.heat.mantissa() > 0, "Sensed compression mutated source or failed compatible heating");
        if (id < 2) {
            bool active = false; for (const auto& e : result.report.events) active = active || e.pair.linear_speed.mantissa() > 0;
            check(active, "Pressure jump did not activate acoustic compression");
        }
        ma::testing::export_acoustic_compression(id, s, result, .001); results.push_back(std::move(result));
    }
    const auto expected = transpose(results[1].state);
    check(expected.momentum_x == results[2].state.momentum_x && expected.momentum_y == results[2].state.momentum_y,
        "Pressure sensor transpose changed momentum");
    for (std::size_t i = 0; i < expected.cells.size(); ++i) {
        MacEnergyAccount error; error.add(expected.cells[i].mixture.energy); error.add(-results[2].state.cells[i].mixture.energy);
        check(error.within_bound(), "Pressure sensor transpose changed stored heat");
    }
    const auto quadratic = advance_mac_compression(set.registry, *set.eos, uniform, .001);
    check(ma::testing::fingerprint(quadratic.state) == ma::testing::fingerprint(results[3].state), "Uniform pressure changed the original quadratic solution");
    auto tiny = source(3, 2, true, false, false); const auto inertia = mac_face_inertias(set.registry, tiny, *set.eos);
    std::vector<MacScaled> j(tiny.momentum_x.size());
    for (std::size_t f = 0; f < j.size(); ++f) if (f % 3 != 1)
        j[f] = MacScaled::from_parts(f % 3 == 0 ? .75 : -.75, -1941).multiplied(inertia.x[f]);
    store_mac_field(tiny.momentum_x, tiny.full_momentum_x, j); tiny.momentum_y.assign(tiny.momentum_y.size(), 0);
    const auto small = advance_mac_compression(set.registry, *set.eos, tiny, .001, sensed);
    check(small.report.heat.mantissa() > 0 && small.report.heat.project().value == 0 && small.report.unresolved_heat_updates > 0 &&
        !small.state.full_momentum_x[1].is_zero() && tiny.full_momentum_x[1].is_zero(), "Sensed grid lost tiny heat or new canonical face momentum");
    ma::testing::export_acoustic_compression(4, tiny, small, .001);
}
void invalid() {
    std::size_t rejected = 0; const auto& set = ma::testing::setup(); const auto s = source(3, 3, false, true, false);
    const auto held = ma::testing::fingerprint(s);
    for (int kind = 0; kind < 8; ++kind) {
        try {
            if (kind < 6) {
                std::array<double, 3> p{1, 1, 1};
                p[static_cast<std::size_t>(kind % 3)] = kind < 3 ? -1 : kind == 3 ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
                static_cast<void>(mac_compression_pressure_sensor(p[0], p[1], p[2]));
            } else static_cast<void>(advance_mac_compression(set.registry, *set.eos, s, kind == 6 ? 0 : .001,
                kind == 7 ? static_cast<MacCompressionLaw>(99) : sensed));
        } catch (const std::invalid_argument&) { ++rejected; }
    }
    check(rejected == 8 && ma::testing::fingerprint(s) == held, "Invalid pressure sensor input changed state or was accepted");
    std::cout << "PREJECTED " << rejected << '\n';
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& set = ma::testing::setup();
    for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    analytic(); std::cout << "PASS exact pressure curvature, zero/affine triples and full exponent range\n";
    grids(); std::cout << "PASS pressure-sensed gas/wet walls, transpose, constant pressure and tiny target momentum\n";
    invalid(); std::cout << "PASS atomic invalid pressure sensor inputs\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
