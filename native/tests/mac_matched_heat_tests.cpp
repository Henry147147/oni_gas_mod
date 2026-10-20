#include "mac_shock_heat_fixture.hpp"
#include <iomanip>
#include <numbers>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr auto law = MacShockHeatLaw::pressure_sensed_viscosity;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MixtureInventory gas(double mass, double t) {
    const auto& s = ma::testing::setup(); std::vector<double> m(s.registry.size());
    m[s.registry.index("nitrogen")] = mass; m[s.registry.index("helium")] = mass * 1e-28;
    return s.eos->initialize_at_temperature(m, t, 1).mixture;
}
void velocities(MacTransportState& state) {
    const auto& s = ma::testing::setup(); const auto m = mac_face_inertias(s.registry, state, *s.eos);
    for (int axis = 0; axis < 2; ++axis) {
        auto& j = axis == 0 ? state.momentum_x : state.momentum_y; const auto& mass = axis == 0 ? m.x : m.y;
        for (std::size_t f = 0; f < j.size(); ++f) if (!ma::testing::compression_fixed_face(state.grid, axis, f))
            j[f] = mass[f] * (axis == 0 ? 3 * std::sin(static_cast<double>(f) + .2) : -2 * std::cos(static_cast<double>(f) + .1));
    }
}
MacTransportState source(bool wet, bool corner = false) {
    const auto& s = ma::testing::setup(); auto state = s.state(4, 3, !corner, wet, corner);
    for (std::size_t i = 0; i < state.cells.size(); ++i) if (!state.grid.walls[i])
        state.cells[i].mixture = s.mixture(1, 100000 * (1 + .2 * std::cos(static_cast<double>(i))),
            300 + 40 * std::sin(static_cast<double>(i) + .4), wet ? .25 + .04 * std::cos(static_cast<double>(i)) : 0);
    velocities(state); return state;
}
MacShockHeatAdvance grid(std::size_t id, const MacTransportState& state, double dt, bool active, bool changes) {
    const auto& s = ma::testing::setup(); const auto held = ma::testing::fingerprint(state);
    auto result = advance_mac_shock_heat(s.registry, *s.eos, state, dt, law);
    check(ma::testing::fingerprint(state) == held && (!result.report.events.empty()) == active, "Matched heat changed source or activation differs");
    check((ma::testing::fingerprint(result.state) != held) == changes, "Matched heat state change differs");
    auto unchanged = result.state;
    for (std::size_t i = 0; i < state.cells.size(); ++i) { unchanged.cells[i].mixture.energy = state.cells[i].mixture.energy; unchanged.cells[i].energy_remainder_j=state.cells[i].energy_remainder_j; }
    check(ma::testing::fingerprint(unchanged) == held, "Matched heat changes constituents, passives, momentum or geometry");
    ma::testing::export_shock_heat(id, state, result, dt); return result;
}
void pure_liquid() {
    const auto& s = ma::testing::setup(); auto state = s.state(3, 2, true);
    state.grid.spacing_m = .1; state.grid.depth_m = .1;
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        std::vector<double> m(s.registry.size()); m[s.registry.index("water")] = 1.02 + .001 * static_cast<double>(i);
        state.cells[i].mixture = s.eos->initialize_at_temperature(m, 300 + 5 * static_cast<double>(i), state.grid.volume_m3()).mixture;
    }
    // Liquid cells exchange no shock heat; their frozen Cp and sound speed are still exported.
    velocities(state); static_cast<void>(grid(2, state, 1e-5, false, false));
}
void inactive() {
    auto rest = source(false); rest.momentum_x.assign(rest.momentum_x.size(), 0); rest.momentum_y.assign(rest.momentum_y.size(), 0);
    static_cast<void>(grid(3, rest, .001, false, false));
    const auto& s = ma::testing::setup(); auto shear = rest; const auto inertia = mac_face_inertias(s.registry, shear, *s.eos);
    for (std::size_t y = 0; y < shear.grid.height; ++y) for (std::size_t x = 0; x < shear.grid.width; ++x) {
        const auto f = shear.grid.x_face(x, y); shear.momentum_x[f] = inertia.x[f] * static_cast<double>(y);
    }
    static_cast<void>(grid(4, shear, .001, false, false));
    auto vertical_shear = rest;
    for (std::size_t y = 0; y < vertical_shear.grid.height; ++y) for (std::size_t x = 0; x < vertical_shear.grid.width; ++x) {
        const auto f = vertical_shear.grid.y_face(x, y);
        vertical_shear.momentum_y[f] = inertia.y[f] * (x == 0 ? 0 : x == 1 ? 1 : x == 2 ? 2 : 4);
    }
    static_cast<void>(grid(5, vertical_shear, .001, false, false));
    auto uniform = source(false);
    for (auto& c : uniform.cells) c.mixture = gas(1, 300);
    static_cast<void>(grid(6, uniform, .001, true, false));
    auto contact = rest;
    for (std::size_t i = 0; i < contact.cells.size(); ++i) contact.cells[i].mixture = gas(i % 2 ? .5 : 1, i % 2 ? 600 : 300);
    static_cast<void>(grid(7, contact, .001, false, false));
}
void subrange() {
    const auto& s = ma::testing::setup(); auto state = s.state(3, 1, false);
    for (std::size_t i = 0; i < state.cells.size(); ++i) state.cells[i].mixture = gas(i == 1 ? .5 : 1, i == 1 ? 600 : 300);
    std::vector<MacScaled> j(state.momentum_x.size()); j[1] = MacScaled::from_parts(.75, -1200); j[2] = j[1].negated();
    store_mac_field(state.momentum_x, state.full_momentum_x, j);
    const auto result = grid(8, state, .001, true, true);
    check(result.report.absolute_transfer.mantissa() > 0 && result.report.absolute_transfer.project().rounded && result.report.unresolved_updates == 0 && !result.state.cells[0].energy_remainder_j.is_zero(),
        "Matched heat lost a below-range heat transfer or its carried correction");
}
void two_body() {
    const auto& s = ma::testing::setup(); auto state = s.state(2, 1, false);
    state.cells[0].mixture = gas(1, 600); state.cells[1].mixture = gas(1, 300);
    const auto m = mac_face_inertias(s.registry, state, *s.eos); state.momentum_x[1] = m.x[1];
    const auto result = grid(9, state, .001, true, true); check(result.report.events.size() == 2, "Two-body fixture has extra links");
    const auto direct = relax_mac_shock_heat_pair(*s.eos, state.cells[0].mixture, state.cells[1].mixture, 1, 1,
        result.report.events[0].pair.integrated_conductance.multiplied(2));
    for (auto delta : {result.state.cells[0].mixture.energy - direct.after_left_j, result.state.cells[1].mixture.energy - direct.after_right_j})
        check(std::abs(delta) < 128 * std::numeric_limits<double>::epsilon() * (std::abs(state.cells[0].mixture.energy) + std::abs(state.cells[1].mixture.energy)), "Frozen matched two-body semigroup differs");
}
void weak() {
    const auto& s = ma::testing::setup(); double previous = 0;
    for (std::size_t id = 10; id < 13; ++id) {
        const auto amplitude = id == 10 ? 1e-3 : id == 11 ? 1e-5 : 1e-7;
        auto state = s.state(16, 1, true); const auto wave = 2 * std::numbers::pi / 16;
        for (std::size_t i = 0; i < 16; ++i) state.cells[i].mixture = gas(1, 300 + amplitude * std::cos((static_cast<double>(i) + .5) * wave));
        const auto m = mac_face_inertias(s.registry, state, *s.eos);
        for (std::size_t i = 0; i < 16; ++i) state.momentum_x[i] = m.x[i] * amplitude * std::sin(static_cast<double>(i) * wave);
        const auto result = grid(id, state, .001, true, true);
        const auto scaled = result.report.absolute_transfer.require_representable() / (amplitude * amplitude);
        check(scaled > 0 && (previous == 0 || std::abs(scaled - previous) < 1e-5 * previous), "Matched weak-wave heat has a first-order amplitude term");
        std::cout << "HMWEAK " << id << ' ' << amplitude << ' ' << scaled << '\n'; previous = scaled;
    }
}
void invalid() {
    const auto& s = ma::testing::setup(); const auto state = source(false); const auto held = ma::testing::fingerprint(state); std::size_t rejected = 0;
    for (int kind = 0; kind < 6; ++kind) {
        auto bad = state; double dt = .001; auto selected = law;
        if (kind == 0) selected = static_cast<MacShockHeatLaw>(99);
        if (kind == 1) dt = 0;
        if (kind == 2) dt = std::numeric_limits<double>::denorm_min();
        if (kind == 3) bad.momentum_x.pop_back();
        if (kind == 4) bad.cells[0].mixture.energy = std::numeric_limits<double>::quiet_NaN();
        if (kind == 5) bad.grid.spacing_m = 0;
        try { static_cast<void>(advance_mac_shock_heat(s.registry, *s.eos, bad, dt, selected)); }
        catch (const std::exception&) { ++rejected; }
    }
    try { static_cast<void>(freeze_mac_compression_coefficients(s.registry, *s.eos, state, static_cast<MacCompressionLaw>(99))); }
    catch (const std::invalid_argument&) { ++rejected; }
    check(rejected == 7 && ma::testing::fingerprint(state) == held, "Invalid matching coefficient request changed source or was accepted");
    std::cout << "HMREJECTED " << rejected << '\n';
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& s = ma::testing::setup();
    for (const auto& m : s.eos->reference().materials()) std::cout << "MATERIAL " << s.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    static_cast<void>(grid(0, source(false), .001, true, true)); std::cout << "PASS matched gas heat and frozen pressure coefficients\n";
    // Shock heat joins gas cells only (MAC_HEAT.md, "Phase interfaces"): wet cells stay inactive.
    static_cast<void>(grid(1, source(true, true), .001, false, false)); std::cout << "PASS matched wet cells, walls and traces stay inactive\n";
    pure_liquid(); std::cout << "PASS matched compressed-liquid heat capacity and sound speed\n";
    inactive(); std::cout << "PASS inactive rest, shear, contacts and equal temperatures\n";
    subrange(); std::cout << "PASS matched subrange heat and unresolved storage\n";
    two_body(); std::cout << "PASS matched frozen two-body semigroup\n";
    weak(); std::cout << "PASS quadratic weak-wave heat amplitude\n";
    invalid(); std::cout << "PASS immutable invalid matching requests\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
