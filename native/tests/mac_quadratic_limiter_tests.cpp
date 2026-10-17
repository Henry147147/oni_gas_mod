#include "mac_fixture.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>

namespace {
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacScaled value(double x) { return MacScaled::from_double(x); }
MacExponent anchor() { auto e = MacExponent(1); for (int i = 0; i < 255; ++i) e = e.doubled(); return e.negated(); }
void emit(MacScaled x) { std::cout << ' ' << x.mantissa() << ' ' << x.exponent(); }
void export_pair(std::size_t id, MacExponent scale, const MacMomentumLimitedPair& p,
    const MacMomentumWorkBudget* budget = nullptr) {
    std::cout << "QPAIR " << id << ' ' << scale << ' ' << p.inertia_left << ' ' << p.inertia_right << ' '
        << p.limiter_roundoff_backoffs << ' ' << p.unresolved_momentum_updates << ' ' << p.unresolved_heat_updates;
    for (const auto& x : {p.before_left, p.before_right, p.before_heat_left, p.before_heat_right, p.requested_impulse, p.limiter,
        p.impulse, p.after_left, p.after_right, p.after_heat_left, p.after_heat_right, p.energy_flux, p.heat_change_left, p.heat_change_right}) emit(x);
    std::cout << '\n';
    if (budget) for (int side = 0; side < 2; ++side) {
        const auto& terms = side == 0 ? budget->left : budget->right;
        std::cout << "QBUDGET " << id << ' ' << side << ' ' << terms.size();
        for (std::size_t i = 0; i < terms.size(); ++i) emit(terms.component(i));
        std::cout << '\n';
    }
}
MacMomentumLimitedPair pair(std::size_t id, double il, double ir, double jl, double jr, double ql, double qr, double request) {
    auto p = limit_mac_momentum_pair_quadratic(il, ir, value(jl), value(jr), value(ql), value(qr), value(request));
    export_pair(id, 0, p); return p;
}
void roots() {
    std::vector<MacExponent> exponents{0, 1, -1, 2, -2, -1073, 1025, -4000,
        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(),
        MacExponent::from_decimal("-9223372036854775809"), MacExponent::from_decimal("9223372036854775808"),
        anchor(), anchor() - 1, anchor().negated() + 1};
    for (std::size_t i = 0; i < exponents.size(); ++i) {
        const auto& e = exponents[i];
        check(e.halved_floor().doubled() + (e.is_odd() ? 1 : 0) == e, "Signed exponent half/parity identity");
        std::cout << "QHALF " << i << ' ' << e << ' ' << e.halved_floor() << ' ' << e.is_odd() << '\n';
        for (const double m : {.5, std::nextafter(.5, 1.), .75, std::nextafter(1., 0.)}) {
            const auto x = MacScaled::from_parts(m, e), root = x.square_root();
            check(root.mantissa() > 0 && x.mantissa() == m && x.exponent() == e, "Square root changed source or lost positivity");
            std::cout << "QSQRT " << i; emit(x); emit(root); std::cout << '\n';
        }
    }
    check(MacScaled{}.square_root().is_zero(), "Square root of exact zero");
}
void analytic() {
    const auto p = pair(0, 1, 1, 1, 0, 0, 0, 2);
    const auto old = limit_mac_momentum_pair(1, 1, value(1), {}, {}, {}, value(2));
    check(p.limiter.require_representable() == .5 && p.after_left.is_zero() && p.after_right.require_representable() == 1 &&
        p.after_heat_left.is_zero() && p.after_heat_right.is_zero() && old.limiter.is_zero(), "Quadratic zero-heat admissible correction was rejected");
    const auto negative = pair(1, 1, 2, 0, 4, .625, .625, 1);
    check(negative.limiter.require_representable() == .5 && negative.after_heat_left.is_zero(), "Negative linear coefficient root");
    const auto symmetric = pair(2, 1, 1, 1, 1, .125, .125, 1);
    check(symmetric.limiter.require_representable() == .5 && symmetric.after_heat_left.is_zero(), "Zero linear coefficient root");
    static_cast<void>(pair(3, 1, 2, 2, 0, .1, .1, 8));
    static_cast<void>(pair(4, 2, 1, 0, 2, .1, .1, -8));
    const auto endpoint = pair(5, 1, 1, 0, 2, 1.5, 1.5, 1);
    check(endpoint.limiter.require_representable() == 1 && endpoint.after_heat_left.is_zero(), "Exactly admissible full impulse");
    const auto blocked = pair(6, 1, 2, 0, 4, 0, 0, 1);
    check(blocked.limiter.is_zero(), "Negative linear coefficient creates heat from zero");
    const auto held = pair(7, 1, 2, 1, 4, .1, .2, 0);
    check(held.impulse.is_zero() && held.before_left.mantissa() == held.after_left.mantissa(), "Zero request changed state");
}
void budgets() {
    for (int sign : {1, -1}) {
        MacEnergySum left, right; left.add(1); left.add(MacEnergy::from_parts(-.5, -59)); right.add(1);
        const MacMomentumWorkBudget budget{sign > 0 ? left : right, sign > 0 ? right : left};
        const auto p = limit_mac_momentum_pair_quadratic(1, 1, value(sign), value(sign), value(1), value(1), value(1), &budget);
        export_pair(static_cast<std::size_t>(sign > 0 ? 8 : 9), 0, p, &budget);
        check(p.limiter.require_representable() < 1 && p.limiter_roundoff_backoffs > 0,
            "Rounded work budget overstatement was admitted");
    }
    MacEnergySum left, right; left.add(.5);
    const MacMomentumWorkBudget budget{left, right};
    const auto p = limit_mac_momentum_pair_quadratic(1, 1, value(1), {}, {}, {}, value(2), &budget);
    export_pair(10, 0, p, &budget);
    check(p.limiter.require_representable() == .5, "Transported work blocked admissible zero-heat correction");
}
void range() {
    const auto tiny_heat = MacEnergy::from_parts(.5, -4000);
    const auto p = limit_mac_momentum_pair_quadratic(1, 2, {}, value(2), tiny_heat, tiny_heat, value(1)); export_pair(11, 0, p);
    check(!p.limiter.is_zero() && p.limiter.project().rounded && p.after_left.mantissa() < 0, "Quadratic ratio projected below-range heat to zero");
    const auto e = anchor(); const auto tiny = MacScaled::from_parts(.75, e), heat = MacEnergy::from_parts(.5, e.doubled() - 2);
    const auto q = limit_mac_momentum_pair_quadratic(1, 2, {}, tiny, heat, heat, tiny); export_pair(12, e, q);
    check(!q.impulse.is_zero() && q.limiter.mantissa() > 0 && q.limiter.require_representable() < 1,
        "Out-of-int64 exponent quadratic limiter lost a correction");
    const auto r = limit_mac_momentum_pair_quadratic(1, 2, value(1e200), value(2e200), value(1), value(1), MacScaled::from_parts(.5, -1200));
    export_pair(13, 0, r); check(r.unresolved_momentum_updates == 2, "Unresolved corrected momentum was not reported");
}
void sampling() {
    std::uint64_t seed = UINT64_C(0x714ab6398d20ce51);
    const auto next = [&]() { seed = seed * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407); return static_cast<double>(seed >> 11) * 0x1p-53; };
    for (std::size_t i = 0; i < 256; ++i) {
        const double il = .125 + 4 * next(), ir = .125 + 4 * next(), jl = 4 * next() - 2, jr = 4 * next() - 2;
        const double ql = i % 7 == 0 ? 0 : .25 * next(), qr = i % 11 == 0 ? 0 : .25 * next(), request = 8 * next() - 4;
        static_cast<void>(pair(14 + i, il, ir, jl, jr, ql, qr, request));
    }
}
void batch() {
    const std::array<double, 4> inertia{1, 2, .5, 1}; const std::array<MacScaled, 4> j{value(1), {}, value(-.5), value(2)};
    const std::array<MacEnergy, 4> q{value(.1), value(.05), value(.2), value(.01)};
    const std::array<MacMomentumCorrection, 5> edges{{{0, 1, value(2)}, {1, 2, value(-1)}, {2, 3, value(.3)}, {3, 0, value(-.4)}, {0, 2, value(.2)}}};
    const auto result = limit_mac_momentum_batch_quadratic(inertia, j, q, edges);
    check(result.pairs.size() == edges.size() && result.limited_pairs > 0 && j[0].require_representable() == 1 && q[0].require_representable() == .1,
        "Quadratic batch did not limit or mutated a source");
    for (std::size_t i = 0; i < result.pairs.size(); ++i) export_pair(270 + i, 0, result.pairs[i]);
}
MacClosedWorld owner(bool wet, MacMaterialReconstruction material, MacShockHeatRegularization heat,
    MacCompressionViscosity compression) {
    const auto& set = ma::testing::setup(); auto state = set.state(4, 3, false, wet, true);
    const auto inertia = mac_face_inertias(set.registry, state, *set.eos);
    state.momentum_x[state.grid.x_face(2, 0)] = inertia.x[state.grid.x_face(2, 0)] * .1;
    state.momentum_y[state.grid.y_face(3, 1)] = inertia.y[state.grid.y_face(3, 1)] * -.025;
    MacClosedSettings settings; settings.gravity_m_per_s2 = 9.81; settings.thermal_conduction = !wet;
    settings.reconstruction = material; settings.momentum_reconstruction = MacMomentumReconstruction::quadratic_limited_linear;
    settings.shock_heat = heat; settings.compression_viscosity = compression;
    return MacClosedWorld(set.registry, set.eos, std::move(state), settings);
}
void continuation() {
    const auto& set = ma::testing::setup(); std::size_t count = 0;
    for (const bool wet : {false, true}) for (const auto material : {MacMaterialReconstruction::donor_cell, MacMaterialReconstruction::limited_linear, MacMaterialReconstruction::swept_first_moment})
        for (const auto heat : {MacShockHeatRegularization::none, MacShockHeatRegularization::compression_activated, MacShockHeatRegularization::pressure_sensed_acoustic, MacShockHeatRegularization::pressure_sensed_series})
            for (const auto compression : {MacCompressionViscosity::none, MacCompressionViscosity::quadratic, MacCompressionViscosity::acoustic_quadratic}) {
                const auto source = owner(wet, material, heat, compression); const auto held = source.checkpoint();
                const auto first = source.advanced(.02); const auto image = first.world.checkpoint();
                const auto restored = MacClosedWorld::from_checkpoint(set.registry, set.eos, image);
                check(restored.checkpoint() == image && restored.settings().momentum_reconstruction == MacMomentumReconstruction::quadratic_limited_linear &&
                    restored.settings().reconstruction == material && source.checkpoint() == held, "New momentum mode checkpoint changed source or settings");
                const auto expected = first.world.advanced(.02), resumed = restored.advanced(.02);
                check(expected.world.checkpoint() == resumed.world.checkpoint(), "Quadratic momentum continuation differs");
                for (std::size_t i = 0; i < resumed.world.state().cells.size(); ++i) if (!resumed.world.state().grid.walls[i])
                    check(set.eos->evaluate(resumed.world.state().cells[i].mixture, resumed.world.state().grid.volume_m3()).temperature_k > 0 &&
                        resumed.world.state().cells[i].mixture.masses[set.registry.index("helium")] > 0 &&
                        !resumed.world.state().cells[i].passive_quantities.empty(), "Owner lost EOS domain, trace constituent or passive inventory");
                ++count;
            }
    check(count == 72 && MacClosedSettings{}.momentum_reconstruction == MacMomentumReconstruction::limited_linear, "Coverage count or legacy default changed");
    std::cout << "QCONTINUATION " << count << '\n';
}
void invalid() {
    std::size_t rejected = 0;
    for (int kind = 0; kind < 7; ++kind) try {
        if (kind < 4) static_cast<void>(limit_mac_momentum_pair_quadratic(kind == 0 ? 0 : 1, 1, {}, {}, value(kind == 1 ? -1 : 0),
            value(kind == 2 ? -1 : 0), kind == 3 ? MacScaled::from_parts(.5, 1026) : value(1)));
        else if (kind == 4) static_cast<void>(value(-1).square_root());
        else if (kind == 5) {
            MacEnergySum left, right; left.add(-1); const MacMomentumWorkBudget budget{left, right};
            static_cast<void>(limit_mac_momentum_pair_quadratic(1, 1, {}, {}, {}, {}, value(1), &budget));
        } else { auto bad = MacClosedSettings{}; bad.momentum_reconstruction = static_cast<MacMomentumReconstruction>(3); bad.validate(); }
    } catch (const std::invalid_argument&) { ++rejected; }
    // A huge requested impulse is representable in range arithmetic; it is
    // admitted at a zero limiter here, so it must not be a false rejection.
    check(rejected == 6, "Invalid quadratic inputs accepted or full-range request rejected");
    std::cout << "QREJECTED " << rejected << '\n';
}
}
int main(int argc, char** argv) { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    roots(); std::cout << "PASS full-range signed-exponent square roots\n";
    analytic(); std::cout << "PASS sharp analytic heat roots and legacy defect control\n";
    budgets(); std::cout << "PASS full additive transported work budgets\n";
    range(); std::cout << "PASS below-range roots impulses and unresolved updates\n";
    sampling(); std::cout << "PASS deterministic signed unequal-inertia sampling\n";
    batch(); std::cout << "PASS conservative sequential quadratic batch\n";
    continuation(); std::cout << "PASS 72 material heat compression and phase continuations\n";
    invalid(); std::cout << "PASS immutable domain and selector rejection\n";
    if (argc == 3) {
        const auto& set = ma::testing::setup(); const std::string operation = argv[1];
        if (operation == "--write-checkpoint") {
            const auto image = owner(false, MacMaterialReconstruction::swept_first_moment, MacShockHeatRegularization::pressure_sensed_acoustic,
                MacCompressionViscosity::acoustic_quadratic).advanced(.02).world.checkpoint();
            std::ofstream output(argv[2], std::ios::binary); output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
            check(output.good(), "Could not write quadratic checkpoint fixture");
        } else if (operation == "--read-checkpoint") {
            std::ifstream input(argv[2], std::ios::binary); check(input.good(), "Could not read quadratic checkpoint fixture");
            const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(input), {}};
            check(MacClosedWorld::from_checkpoint(set.registry, set.eos, image).checkpoint() == image, "Cross-platform quadratic checkpoint changed bytes");
        } else throw std::invalid_argument("Unknown quadratic checkpoint fixture operation");
    } else check(argc == 1, "Invalid quadratic control arguments");
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
