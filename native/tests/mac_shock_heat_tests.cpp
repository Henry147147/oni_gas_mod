#include "mac_shock_heat_fixture.hpp"
#include "mixed_atmospheres/mac_shock_heat.hpp"
#include "../src/mac_material_entropy_detail.hpp"
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
void entropy_reference_precision() {
    // First failed heat pair retained from the original N256 separation run.
    // Caloric offset and thermal energy nearly cancel in the stored U.
    const auto& s = ma::testing::setup(); MixtureInventory left(s.registry.size()), right(s.registry.size());
    const auto nitrogen = s.registry.index("nitrogen"), helium = s.registry.index("helium");
    left.masses[nitrogen] = 1.5955820824192574e-05; left.masses[helium] = 1.5955820824192582e-33;
    right.masses[nitrogen] = 1.5955820813853493e-05; right.masses[helium] = 1.5955820813853496e-33;
    left.energy = -0.0033121700131082035; right.energy = -0.0033124391079650661;
    constexpr double volume = 1.52587890625e-05;
    const auto conductance = MacEnergy::from_parts(.64028600022444926, -37);
    const auto forward = pair(0, left, right, conductance, volume, volume);
    const auto reverse = pair(1, right, left, conductance, volume, volume);
    check(forward.after_left_j == -0.0033121700131083093 && forward.after_right_j == -0.0033124391079649602,
        "Captured heat pair changed actual stored endpoints");
    check(reverse.after_left_j == forward.after_right_j && reverse.after_right_j == forward.after_left_j &&
        forward.entropy_change_j_per_k > 0 && reverse.entropy_change_j_per_k > 0,
        "Relative heat entropy loses a positive or reflected transfer");
    std::cout << "PASS captured reference-cancelling heat pair and reflection\n";
    for (std::size_t id = 2; id <= 4; ++id) {
        const auto result = id == 4 ? pair(id, left, left, conductance, volume, volume) :
            pair(id, left, right, id == 2 ? MacEnergy{} : MacEnergy::from_parts(.75, -1200), volume, volume);
        check(result.after_left_j == left.energy && result.after_right_j == (id == 4 ? left.energy : right.energy) &&
            result.entropy_change_j_per_k == 0, "Unchanged stored heat state has nonzero entropy change");
        if (id == 3) check(!result.transfer.is_zero() && result.transfer.project().rounded && result.unresolved_updates == 2,
            "Unresolved reference-cancelling transfer lost its full-range value");
    }
    std::cout << "PASS unchanged and subrange stored heat entropy\n";
    auto endpoint = left; endpoint.energy -= .001;
    const auto initial = s.eos->evaluate(left, volume), after = s.eos->evaluate(endpoint, volume);
    const auto ds = entropy_detail::change(*s.eos, left, volume, initial, endpoint, volume, after);
    MacEnergySum bound; bound.add(MacEnergy::from_double(std::abs(left.energy)).shared(1, initial.temperature_k).shifted(-45));
    bound.add(MacEnergy::from_double(std::abs(endpoint.energy)).shared(1, after.temperature_k).shifted(-45));
    MacEnergySum lower = bound; lower.add(ds);
    check(lower.sign() < 0, "Relative heat entropy hides genuine stored-state cooling");
    std::cout << "HTCOOL 0 0 0 " << volume << ' ' << left.energy << ' ' << endpoint.energy << ' ' << initial.temperature_k << ' '
        << after.temperature_k << ' ' << initial.entropy_j_per_k << ' ' << after.entropy_j_per_k;
    ma::testing::export_shock_heat_scaled(ds); ma::testing::export_shock_heat_scaled(bound.value()); std::cout << '\n';
    std::cout << "PASS genuine cooling still violates original stored-U entropy bound\n";
}
}
// A captured dam-break pair: two gas cells carrying trace liquid exchange a
// heat far below the EOS pressure root's volume tolerance (2e-12 V). Stage
// readings of the same state can differ within that tolerance, so each
// reported headspace is shifted by +/-1e-12 V. Each actual entropy change
// must still match dU/T, and their sum may not fall below the original
// stored-U allowance. A liquid-dominated cell checks the reported headspace
// branch without the shift.
void trace_liquid_regression() {
    const auto& s = ma::testing::setup();
    const auto cell = [&](double oxygen, double nitrogen, double helium, double water, double energy) {
        MixtureInventory m(s.registry.size());
        m.masses[s.registry.index("oxygen")] = oxygen; m.masses[s.registry.index("nitrogen")] = nitrogen;
        m.masses[s.registry.index("helium")] = helium; m.masses[s.registry.index("water")] = water; m.energy = energy; return m;
    };
    const auto exchange = [&](const MixtureInventory& source, double endpoint_energy, double volume, double root_shift) {
        auto endpoint = source; endpoint.energy = endpoint_energy;
        auto before = s.eos->evaluate(source, volume), after = s.eos->evaluate(endpoint, volume);
        before.gas_volume_m3 += root_shift * volume; after.gas_volume_m3 -= root_shift * volume;
        const double heat = endpoint_energy - source.energy;
        const double ds = entropy_detail::change(*s.eos, source, volume, before, endpoint, volume, after).project().value;
        const double expected = heat / (.5 * (before.temperature_k + after.temperature_k));
        std::cout << "HTRACE " << before.liquid_volume_m3 << ' ' << before.gas_volume_m3 << ' ' << heat << ' ' << ds << ' ' << expected << '\n';
        check(std::abs(ds - expected) <= 1e-6 * std::abs(expected), "Trace-liquid entropy change departs from dU/T");
        MacEnergySum bound; bound.add(MacEnergy::from_double(std::abs(source.energy)).shared(1, before.temperature_k).shifted(-45));
        bound.add(MacEnergy::from_double(std::abs(endpoint.energy)).shared(1, after.temperature_k).shifted(-45));
        return std::pair{ds, bound.value().project().value};
    };
    const double volume = 0x1.47ae147ae147cp-13;
    const auto hot = cell(0x1.516330ee972b2p-15, 0x1.2b0c48766314dp-13, 0x1.7bb2243f8608fp-106, 0x1.fdc50f26342ecp-24, -0x1.15f1f70f854cep+4);
    const auto cold = cell(0x1.51632475587a1p-15, 0x1.2b0c3d6802c94p-13, 0x1.7bb21635d87d4p-106, 0x1.7ad19d09c1377p-25, -0x1.0304fe4c267e8p+4);
    const auto [hot_ds, hot_bound] = exchange(hot, -0x1.15f1f710cdd63p+4, volume, 1e-12);
    const auto [cold_ds, cold_bound] = exchange(cold, -0x1.0304fe4addf53p+4, volume, -1e-12);
    std::cout << "HTRACESUM " << hot_ds + cold_ds << ' ' << hot_bound + cold_bound << '\n';
    check(hot_ds + cold_ds >= -(hot_bound + cold_bound), "Trace-liquid heat exchange reduces actual entropy");
    const auto wet = s.mixture(volume, 101325, 300, .9);
    static_cast<void>(exchange(wet, wet.energy + 1e-3, volume, 0));
    std::cout << "PASS trace-liquid heat exchange keeps actual stored-state entropy\n";
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
    if (argc == 2 && std::string_view(argv[1]) == "--trace-liquid-regression") {
        trace_liquid_regression(); return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--entropy-regression") {
        entropy_reference_precision(); return 0;
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
