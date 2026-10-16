#include "mac_shock_heat_fixture.hpp"
#include "mixed_atmospheres/mac_shock_heat.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MixtureInventory gas(double mass, double t, double volume = 1) {
    const auto& s = ma::testing::setup(); std::vector<double> m(s.registry.size());
    m[s.registry.index("nitrogen")] = mass; m[s.registry.index("helium")] = mass * 1e-28;
    return s.eos->initialize_at_temperature(m, t, volume).mixture;
}
MacShockHeatPair pair(std::size_t id, const MixtureInventory& l, const MixtureInventory& r, MacEnergy g, double vl = 1, double vr = 1) {
    const auto& s = ma::testing::setup(); const auto p = relax_mac_shock_heat_pair(*s.eos, l, r, vl, vr, g); ma::testing::export_shock_heat_pair(id, l, r, vl, vr, p); return p;
}
void analytic() {
    auto l = gas(.2, 600), r = gas(.3, 250); const auto held_l = l.masses, held_r = r.masses;
    const auto a = pair(0, l, r, MacEnergy::from_double(.1));
    static_cast<void>(pair(1, l, r, MacEnergy::from_double(1e8)));
    const auto reflected = pair(2, r, l, MacEnergy::from_double(.1));
    check(reflected.transfer.mantissa() == -a.transfer.mantissa() && reflected.transfer.exponent() == a.transfer.exponent(), "Reflected thermal transfer differs");
    static_cast<void>(pair(3, l, r, {})); static_cast<void>(pair(4, l, gas(.3, 600), MacEnergy::from_double(.1)));
    const auto half = pair(5, l, r, MacEnergy::from_double(.05));
    auto lh = l, rh = r; lh.energy = half.after_left_j; rh.energy = half.after_right_j;
    const auto twice = pair(6, lh, rh, MacEnergy::from_double(.05));
    for (auto difference : {a.after_left_j - twice.after_left_j, a.after_right_j - twice.after_right_j})
        check(std::abs(difference) < 128 * std::numeric_limits<double>::epsilon() * (std::abs(l.energy) + std::abs(r.energy)), "Constant-capacity heat semigroup differs");
    check(l.masses == held_l && r.masses == held_r, "Pair changed input masses");
    const auto& s = ma::testing::setup(); std::vector<double> ml(s.registry.size()), mr(s.registry.size());
    ml[s.registry.index("water")] = 1.02; mr[s.registry.index("water")] = 2.04;
    const auto water_l = s.eos->initialize_at_temperature(ml, 500, .001).mixture;
    const auto water_r = s.eos->initialize_at_temperature(mr, 280, .002).mixture;
    static_cast<void>(pair(7, water_l, water_r, MacEnergy::from_double(100), .001, .002));
}
void nonlinear_and_range() {
    const auto& s = ma::testing::setup(); const auto l = s.mixture(1, 2e5, 550, .3), r = s.mixture(.75, 1e5, 250, .65);
    const auto mixed = pair(8, l, r, MacEnergy::from_double(1e8), 1, .75);
    check(mixed.equilibrium_iterations > 0 && mixed.transfer.mantissa() > 0, "Mixed EOS equilibrium was not solved");
    const auto a = gas(.2, 600), b = gas(.3, 250);
    const auto tiny = pair(9, a, b, MacEnergy::from_parts(.75, -1200));
    check(tiny.transfer.mantissa() > 0 && tiny.transfer.project().rounded && tiny.unresolved_updates == 2 &&
        tiny.after_left_j == a.energy && tiny.after_right_j == b.energy, "Below-range heat or update flags disappeared");
    const auto stiff = pair(10, a, b, MacEnergy::from_parts(.5, 1200));
    check(stiff.fraction.require_representable() == 1, "Stiff full-range thermal relaxation overflowed");
}
MacTransportState compressing(bool periodic, bool wet, bool corner) {
    const auto& s = ma::testing::setup(); auto state = s.state(3, 3, periodic, wet, corner);
    for (std::size_t i = 0; i < state.cells.size(); ++i) if (!state.grid.walls[i])
        state.cells[i].mixture = s.eos->initialize_at_temperature(state.cells[i].mixture.masses, 250 + 60 * static_cast<double>(i), 1).mixture;
    const auto m = mac_face_inertias(s.registry, state, *s.eos);
    for (int axis = 0; axis < 2; ++axis) {
        auto& j = axis == 0 ? state.momentum_x : state.momentum_y; const auto& mass = axis == 0 ? m.x : m.y;
        for (std::size_t f = 0; f < j.size(); ++f) if (!ma::testing::compression_fixed_face(state.grid, axis, f))
            j[f] = mass[f] * (axis == 0 ? (f % state.grid.x_width() == 1 ? 12 : -8) : (f / state.grid.width == 1 ? 5 : -3));
    }
    return state;
}
void grid_case(std::size_t id, MacTransportState state, double dt, bool active, bool changes) {
    const auto& s = ma::testing::setup(); const auto held = ma::testing::fingerprint(state);
    const auto result = advance_mac_shock_heat(s.registry, *s.eos, state, dt);
    check(ma::testing::fingerprint(state) == held && (!result.report.events.empty()) == active, "Heat grid changed source or activation differs");
    check((ma::testing::fingerprint(result.state) != held) == changes, "Heat grid state change differs");
    auto unchanged_fields = result.state;
    for (std::size_t i = 0; i < state.cells.size(); ++i) unchanged_fields.cells[i].mixture.energy = state.cells[i].mixture.energy;
    check(ma::testing::fingerprint(unchanged_fields) == held, "Heat grid changes mass, passives, J or geometry");
    ma::testing::export_shock_heat(id, state, result, dt);
}
void occupied() { grid_case(0, compressing(false, true, true), .01, true, true); grid_case(1, compressing(true, true, false), .01, true, true); }
MacTransportState chain() {
    const auto& s = ma::testing::setup(); auto state = s.state(3, 1, false);
    for (std::size_t i = 0; i < 3; ++i) state.cells[i].mixture = gas(1, i == 1 ? 600 : 300);
    const auto m = mac_face_inertias(s.registry, state, *s.eos);
    state.momentum_x[1] = m.x[1]; state.momentum_x[2] = -m.x[2]; return state;
}
void inactive() {
    const auto& s = ma::testing::setup(); auto rest = compressing(false, true, true);
    rest.momentum_x.assign(rest.momentum_x.size(), 0); rest.momentum_y.assign(rest.momentum_y.size(), 0); grid_case(2, rest, .01, false, false);
    auto shear = s.state(3, 3, true); const auto m = mac_face_inertias(s.registry, shear, *s.eos);
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 3; ++x) shear.momentum_x[shear.grid.x_face(x,y)] = m.x[shear.grid.x_face(x,y)] * static_cast<double>(y);
    grid_case(3, shear, .01, false, false);
    auto uniform = chain(); for (auto& c : uniform.cells) c.mixture = gas(1, 300); grid_case(4, uniform, .01, true, false);
}
void temporal() {
    const auto& s = ma::testing::setup(); double previous = 0;
    for (const auto n : {1,2,4,8,16}) {
        auto state = chain(); const double dt = .25 / n;
        for (int i = 0; i < n; ++i) state = advance_mac_shock_heat(s.registry, *s.eos, state, dt).state;
        double error = 0; std::cout << "HNETWORK " << n << " 0.25";
        for (std::size_t i = 0; i < 3; ++i) {
            const auto t = s.eos->evaluate(state.cells[i].mixture, 1).temperature_k;
            const auto expected = 400 + (i == 1 ? 200 : -100) * std::exp(-1.5);
            error = std::max(error, std::abs(t - expected)); std::cout << ' ' << t;
        }
        std::cout << ' ' << error << '\n';
        check(error > 0 && (previous == 0 || error < .3 * previous), "Symmetric heat network lacks second-order time refinement");
        if (n == 16) check(error < .05, "Fine heat network absolute error exceeds .05 K");
        previous = error;
    }
}
void range_and_invalid() {
    const auto& s = ma::testing::setup(); auto tiny = chain(); std::vector<MacScaled> j(tiny.momentum_x.size());
    j[1] = MacScaled::from_parts(.75, -1200); j[2] = j[1].negated(); store_mac_field(tiny.momentum_x, tiny.full_momentum_x, j);
    grid_case(5, tiny, .01, true, false);
    const auto r = advance_mac_shock_heat(s.registry, *s.eos, tiny, .01);
    check(r.report.absolute_transfer.mantissa() > 0 && r.report.unresolved_updates > 0, "Range heat disappeared without unresolved flags");
    std::size_t rejected = 0; const auto source = chain();
    for (int kind = 0; kind < 8; ++kind) {
        auto bad = source; double dt = .01;
        if (kind == 0) dt = 0;
        if (kind == 1) dt = -1;
        if (kind == 2) dt = std::numeric_limits<double>::infinity();
        if (kind == 3) bad.momentum_x.pop_back();
        if (kind == 4) bad.momentum_x[0] = 1;
        if (kind == 5) bad.full_momentum_x.assign(1, {});
        const auto held = ma::testing::fingerprint(bad);
        try {
            if (kind == 6) static_cast<void>(relax_mac_shock_heat_pair(*s.eos, gas(1,300), gas(1,400), 1, 1, MacEnergy::from_double(-1)));
            else if (kind == 7) static_cast<void>(relax_mac_shock_heat_pair(*s.eos, gas(1,300), gas(1,400), 0, 1, MacEnergy::from_double(1)));
            else static_cast<void>(advance_mac_shock_heat(s.registry, *s.eos, bad, dt));
        } catch (const std::exception&) { ++rejected; }
        check(ma::testing::fingerprint(bad) == held, "Rejected heat input was mutated");
    }
    check(rejected == 8, "Invalid heat input was accepted"); std::cout << "HREJECTED " << rejected << '\n';
}
void admissible_brackets() {
    const auto& s = ma::testing::setup();
    const auto cold = s.mixture(1, 2e9, 250, .9);
    const auto hot = gas(.001, 10000);
    std::cout << "HBRACKET_START 11\n";
    const auto first = pair(11, hot, cold, MacEnergy::from_double(1e8));
    check(first.equilibrium_iterations > 0 && first.transfer.mantissa() > 0, "Admissible partial thermal bracket was rejected");
    std::cout << "HBRACKET_START 12\n";
    const auto reflected = pair(12, cold, gas(1e-8, 100000), MacEnergy::from_double(1e8));
    check(reflected.transfer.mantissa() < 0, "Conditioned reflected thermal bracket was rejected");
    std::cout << "HBRACKET_START 13\n";
    const auto cooling = pair(13, s.mixture(1, 2e9, 1000, .9), gas(.001, 200), MacEnergy::from_double(1e8));
    check(cooling.transfer.mantissa() > 0, "Asymmetric mixed thermal bracket was rejected");
}
void pressure_contacts() {
    const auto& s = ma::testing::setup(); auto state = s.state(3, 3, false, true, true);
    for (std::size_t i = 0; i < state.cells.size(); ++i) if (!state.grid.walls[i]) {
        state.cells[i].mixture = s.mixture(1, 100000, 250 + 30 * static_cast<double>(i), .4 + .01 * static_cast<double>(i), .1 + .05 * static_cast<double>(i));
        check(std::abs(s.eos->evaluate(state.cells[i].mixture, 1).mechanical_pressure_pa - 100000) < 5e-10 * 100000,
            "Contact fixture is not at its declared pressure");
    }
    grid_case(6, state, .01, false, false);
}
}
int main(int argc, char** argv) { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& s = ma::testing::setup();
    for (const auto& m : s.eos->reference().materials()) std::cout << "MATERIAL " << s.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    if (argc == 2 && std::string_view(argv[1]) == "--source-regression") {
        std::cout << "HSOURCE_BEGIN 1\n";
        const auto initial = gas(.001, 2);
        std::cout << "HSOURCE_INITIALIZED " << initial.energy << '\n';
        const auto reading = s.eos->evaluate(initial, 1);
        std::cout << "HSOURCE_READING " << reading.temperature_k << '\n';
        std::cout << "PASS independent starting-inventory evaluation\n"; return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--bracket-regression") {
        admissible_brackets(); std::cout << "PASS admissible mixed-EOS bracket regression\n"; return 0;
    }
    analytic(); std::cout << "PASS exact constant-capacity pairs and semigroup\n";
    nonlinear_and_range(); std::cout << "PASS mixed EOS equilibrium and full-range heat\n";
    occupied(); std::cout << "PASS occupied wet grids, walls and conserved fields\n";
    inactive(); std::cout << "PASS stationary contacts, shear and uniform temperature\n";
    temporal(); std::cout << "PASS symmetric heat network time refinement\n";
    range_and_invalid(); std::cout << "PASS unresolved heat and immutable rejection\n";
    admissible_brackets(); std::cout << "PASS admissible mixed-EOS bracket regression\n";
    pressure_contacts(); std::cout << "PASS stationary uniform-pressure composition contacts\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
