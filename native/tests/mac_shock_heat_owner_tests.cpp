#include "mac_shock_heat_fixture.hpp"
#include "mac_acoustic_compression_fixture.hpp"
#include <array>
#include <iomanip>
#include <string_view>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double dt = 1e-5;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void full(MacEnergy v) { ma::testing::export_shock_heat_scaled(v); }
bool equal(MacEnergy a, MacEnergy b) { return a.mantissa() == b.mantissa() && a.exponent() == b.exponent(); }
MacTransportState source(bool wet, bool corner = false) {
    const auto& s = ma::testing::setup(); auto state = s.state(corner ? 4 : 3, corner ? 3 : 2, !corner, wet, corner);
    for (std::size_t i = 0; i < state.cells.size(); ++i) if (!state.grid.walls[i])
        state.cells[i].mixture = s.mixture(1, 100000 * (1 + .01 * std::cos(static_cast<double>(i))),
            300 + 12 * std::sin(static_cast<double>(i) + .4), wet ? .25 + .04 * std::cos(static_cast<double>(i)) : 0);
    const auto m = mac_face_inertias(s.registry, state, *s.eos);
    for (int axis = 0; axis < 2; ++axis) {
        auto& j = axis == 0 ? state.momentum_x : state.momentum_y; const auto& mass = axis == 0 ? m.x : m.y;
        for (std::size_t f = 0; f < j.size(); ++f) if (!ma::testing::compression_fixed_face(state.grid, axis, f))
            j[f] = mass[f] * (axis == 0 ? .3 * std::sin(static_cast<double>(f) + .2) : -.2 * std::cos(static_cast<double>(f) + .1));
    }
    return state;
}
void frame(std::size_t gid, std::size_t id, const char* tag, const MacTransportState& state) {
    const auto& s = ma::testing::setup(); const auto& g = state.grid; const auto m = mac_face_inertias(s.registry, state, *s.eos);
    std::cout << "SHFRAME " << gid << ' ' << id << ' ' << g.width << ' ' << g.height << ' ' << g.spacing_m << ' ' << g.depth_m << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' << tag << '\n';
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        std::cout << "SHWALL " << gid << ' ' << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << '\n';
        if (g.walls[i]) continue;
        const auto& c = state.cells[i]; const auto e = s.eos->evaluate(c.mixture, g.volume_m3());
        std::cout << "SHCELL " << gid << ' ' << id << ' ' << i << ' ' << c.mixture.energy << ' ' << e.mechanical_pressure_pa << ' ' << e.temperature_k << ' ' << e.entropy_j_per_k << '\n';
        for (std::size_t k = 0; k < c.mixture.masses.size(); ++k) if (c.mixture.masses[k] != 0)
            std::cout << "SHMASS " << gid << ' ' << id << ' ' << i << ' ' << k << ' ' << c.mixture.masses[k] << '\n';
        for (const auto& v : c.passive_quantities) std::cout << "SHPASSIVE " << gid << ' ' << id << ' ' << i << ' ' << v.identity << ' ' << v.quantity << '\n';
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto j = axis == 0 ? mac_field_values(state.momentum_x, state.full_momentum_x) : mac_field_values(state.momentum_y, state.full_momentum_y);
        const auto& inertia = axis == 0 ? m.x : m.y;
        for (std::size_t f = 0; f < j.size(); ++f) {
            std::cout << "SHFACE " << gid << ' ' << id << ' ' << axis << ' ' << f << ' ' << inertia[f]; full(j[f]); std::cout << '\n';
        }
    }
}
struct Expected {
    std::size_t stages = 0, events = 0, roots = 0, rejected = 0, unresolved = 0, momentum = 0, compression = 0;
    std::size_t conduction = 0, contacts = 0, iterations = 0, conduction_unresolved = 0;
    MacEnergySum heat, bounds, physical;
};
void run_case(std::size_t gid, MacTransportState state, MacClosedSettings options, std::size_t steps, bool tiny = false) {
    const auto& s = ma::testing::setup(); options.maximum_timestep_s = dt;
    std::cout << "SHLAW " << gid << ' ' << static_cast<int>(options.compression_viscosity) << '\n';
    const MacClosedWorld owner(s.registry, s.eos, state, options); const auto held = ma::testing::fingerprint(state);
    auto manual = state; Expected expected; std::size_t current = 0; frame(gid, current, "source", manual);
    const bool enabled = options.shock_heat == MacShockHeatRegularization::compression_activated;
    for (std::size_t step = 0; step < steps; ++step) {
        std::size_t order = 0;
        const auto path = [&](char kind, std::size_t id) {
            const auto before = current; ++current; frame(gid, current, "stage", manual);
            std::cout << "SHPATH " << gid << ' ' << step << ' ' << order++ << ' ' << kind << ' ' << id << ' ' << before << ' ' << current << '\n';
        };
        const auto physical = [&]() {
            const auto id = gid * 100 + step * 10 + order;
            auto a = advance_mac_conduction(s.registry, *s.eos, manual, .5 * dt); const auto& r = a.report;
            std::cout << "SHL " << id << ' ' << .5 * dt << ' ' << r.contacts << ' ' << r.iterations << ' ' << r.unresolved_heat_updates;
            for (auto v : {r.redistributed_energy, r.energy_residual, r.energy_bound}) full(v);
            std::cout << ' ' << r.entropy_change_j_per_k << '\n';
            ++expected.conduction; expected.contacts += r.contacts; expected.iterations += r.iterations;
            expected.conduction_unresolved += r.unresolved_heat_updates; expected.physical.add(r.redistributed_energy); expected.bounds.add(r.energy_bound);
            manual = std::move(a.state); path('L', id);
        };
        const auto heat = [&]() {
            const auto id = gid * 100 + step * 10 + order;
            auto a = advance_mac_shock_heat(s.registry, *s.eos, manual, .5 * dt); const auto& r = a.report;
            if (tiny && step == 0 && expected.stages == 0)
                check(r.absolute_transfer.mantissa() > 0 && r.absolute_transfer.project().rounded && r.unresolved_updates > 0,
                    "Default owner source heat did not exercise a full-range unresolved transfer");
            ma::testing::export_shock_heat(id, manual, a, .5 * dt);
            ++expected.stages; expected.events += r.events.size(); expected.unresolved += r.unresolved_updates;
            for (const auto& e : r.events) { expected.roots += e.pair.equilibrium_iterations; expected.rejected += e.pair.rejected_equilibrium_trials; }
            expected.heat.add(r.absolute_transfer); expected.bounds.add(r.energy_bound);
            manual = std::move(a.state); path('H', id);
        };
        const auto compression = [&]() {
            const auto id = gid * 100 + step * 10 + order;
            const bool acoustic = options.compression_viscosity == MacCompressionViscosity::acoustic_quadratic;
            auto a = advance_mac_compression(s.registry, *s.eos, manual, .5 * dt,
                acoustic ? MacCompressionLaw::pressure_sensed_acoustic : MacCompressionLaw::quadratic);
            if (acoustic) ma::testing::export_acoustic_compression(id, manual, a, .5 * dt, "K");
            else ma::testing::export_compression(id, manual, a, .5 * dt, "K");
            expected.compression += a.report.events.size(); expected.bounds.add(a.report.energy_bound);
            manual = std::move(a.state); path('C', id);
        };
        if (options.thermal_conduction) physical();
        if (enabled) heat();
        compression();
        {
            const auto id = gid * 100 + step * 10 + order;
            auto a = advance_mac_closed_substep(s.registry, *s.eos, manual, dt, options);
            ma::testing::export_case(static_cast<int>(id), manual, a, dt); expected.bounds.add(a.energy_bound_wide_j);
            expected.momentum += a.transport.x.corrections.size() + a.transport.y.corrections.size();
            manual = std::move(a.state); path('P', id);
        }
        compression();
        if (enabled) heat();
        if (options.thermal_conduction) physical();
    }
    const auto manual_frame = current;
    const auto result = owner.advanced(dt * static_cast<double>(steps), steps + 2); const auto& r = result.report;
    check(ma::testing::fingerprint(owner.state()) == held && ma::testing::fingerprint(state) == held && owner.time_s() == 0 && owner.revision() == 1,
        "Default heat owner changed source state or clock");
    check(ma::testing::fingerprint(result.world.state()) == ma::testing::fingerprint(manual) && result.world.time_s() == dt * static_cast<double>(steps) &&
        result.world.revision() == 2 && r.attempts == steps && r.substeps == steps && r.rejected_attempts == 0, "Default heat owner differs from completed component stages");
    check(r.shock_heat_stages == expected.stages && r.shock_heat_events == expected.events && r.shock_heat_root_iterations == expected.roots &&
        r.rejected_shock_heat_trials == expected.rejected && r.unresolved_shock_heat_updates == expected.unresolved &&
        r.momentum_correction_events == expected.momentum && r.compression_events == expected.compression &&
        r.conduction_stages == expected.conduction && r.conduction_contacts == expected.contacts && r.conduction_iterations == expected.iterations &&
        r.unresolved_conduction_updates == expected.conduction_unresolved, "Owner accepted-stage counters differ");
    check(equal(r.absolute_shock_heat_transfer_wide_j, expected.heat.value()) && equal(r.conductive_redistribution_j, expected.physical.value()), "Owner heat totals differ from actual stages");
    MacEnergyAccount budget; budget.add(r.energy_bound_wide_j); budget.add(expected.bounds.value().negated()); check(budget.within_bound(), "Owner omitted a stage operand bound");
    if (enabled) check(r.shock_heat_stages == 2 * steps && r.shock_heat_events > 0 && r.absolute_shock_heat_transfer_wide_j.mantissa() > 0, "Default heat path was inactive");
    else check(r.shock_heat_stages == 0 && r.shock_heat_events == 0 && r.absolute_shock_heat_transfer_wide_j.is_zero(), "Disabled heat reference was active");
    if (options.thermal_conduction) check(r.conductive_redistribution_j.mantissa() > 0, "Physical and shock heat were not both exercised");
    const auto owner_frame = ++current; frame(gid, owner_frame, "owner", result.world.state());
    const auto held_frame = ++current; frame(gid, held_frame, "held", owner.state());
    std::cout << "SHCASE " << gid << ' ' << dt << ' ' << steps << ' ' << enabled << ' ' << options.thermal_conduction << ' ' << manual_frame << ' ' << owner_frame << ' ' << held_frame << '\n';
    std::cout << "SHREPORT " << gid << ' ' << result.world.time_s() << ' ' << result.world.revision() << ' ' << r.attempts << ' ' << r.substeps << ' ' << r.rejected_attempts << ' '
        << r.shock_heat_stages << ' ' << r.shock_heat_events << ' ' << r.shock_heat_root_iterations << ' ' << r.rejected_shock_heat_trials << ' ' << r.unresolved_shock_heat_updates << ' '
        << r.momentum_correction_events << ' ' << r.compression_events << ' ' << r.conduction_stages << ' ' << r.conduction_contacts << ' ' << r.conduction_iterations << ' ' << r.unresolved_conduction_updates;
    for (auto v : {r.absolute_shock_heat_transfer_wide_j, r.energy_residual_wide_j, r.energy_bound_wide_j, r.conductive_redistribution_j, r.full_wall_impulse_x, r.full_wall_impulse_y}) full(v);
    std::cout << ' ' << r.entropy_change_j_per_k << ' ' << r.entropy_roundoff_j_per_k << '\n';
}
void controls(MacClosedSettings options) {
    const auto& s = ma::testing::setup(); const auto state = source(true); const auto held = ma::testing::fingerprint(state);
    std::cout << "SHLAW 6 " << static_cast<int>(options.compression_viscosity) << '\n';
    options.maximum_timestep_s = dt; auto bad = options; bad.shock_heat = static_cast<MacShockHeatRegularization>(99);
    std::size_t rejected = 0;
    try { const MacClosedWorld invalid(s.registry, s.eos, state, bad); } catch (const std::invalid_argument&) { ++rejected; }
    try { static_cast<void>(advance_mac_closed_substep(s.registry, *s.eos, state, dt, bad)); } catch (const std::invalid_argument&) { ++rejected; }
    const MacClosedWorld owner(s.registry, s.eos, state, options); frame(6, 0, "before", owner.state()); bool failed = false;
    try { static_cast<void>(owner.advanced(2 * dt, 1)); }
    catch (const std::runtime_error& e) { if (std::string_view(e.what()) != "MAC closed interval exhausted its shared attempt budget") throw; failed = true; }
    frame(6, 1, "failed", owner.state());
    check(rejected == 2 && failed && owner.time_s() == 0 && owner.revision() == 1 && ma::testing::fingerprint(owner.state()) == held,
        "Invalid heat mode or later failure published state/time");
    const auto a = owner.advanced(2 * dt, 4), b = owner.advanced(2 * dt, 4);
    check(a.report.substeps == 2 && a.report.shock_heat_stages == 4 && a.report.shock_heat_events > 0 &&
        ma::testing::fingerprint(a.world.state()) == ma::testing::fingerprint(b.world.state()), "Failed heat interval retained cached partial state");
    frame(6, 2, "recovered", a.world.state()); frame(6, 3, "repeated", b.world.state());
    std::cout << "SHCONTROL " << rejected << ' ' << failed << ' ' << owner.time_s() << ' ' << owner.revision() << ' ' << a.report.substeps << ' ' << a.report.shock_heat_stages << '\n';
}
}
int main(int argc, char** argv) { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& s = ma::testing::setup();
    for (const auto& m : s.eos->reference().materials()) {
        const auto index = s.registry.index(m.id);
        std::cout << "MATERIAL " << index << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
        std::cout << "SHKAPPA " << index << ' ' << s.registry[index].conductivity << '\n';
    }
    MacClosedSettings baseline;
    check(baseline.shock_heat == MacShockHeatRegularization::compression_activated &&
        baseline.compression_viscosity == MacCompressionViscosity::acoustic_quadratic, "Default regularization setting differs");
    if (argc == 2 && std::string_view(argv[1]) == "--quadratic-compression") baseline.compression_viscosity = MacCompressionViscosity::quadratic;
    else check(argc == 1, "Unknown heat-owner test arguments");
    auto options = baseline;
    run_case(0, source(true), options, 1); std::cout << "PASS default wet heat, compression and pressure stages\n";
    run_case(1, source(true, true), options, 1); std::cout << "PASS default wet corner and closed wall accounts\n";
    options.thermal_conduction = true; run_case(2, source(false), options, 1); std::cout << "PASS physical conduction with default shock heat\n";
    options.thermal_conduction = false; run_case(3, source(true), options, 2); std::cout << "PASS multiple accepted heat substeps and one owner clock\n";
    auto tiny = source(false); const auto inertia = mac_face_inertias(s.registry, tiny, *s.eos); std::vector<MacScaled> j(tiny.momentum_x.size());
    for (std::size_t f = 0; f < j.size(); ++f) j[f] = MacScaled::from_parts(f % 3 == 1 ? -.75 : .75, -1200).multiplied(inertia.x[f]);
    store_mac_field(tiny.momentum_x, tiny.full_momentum_x, j); tiny.momentum_y.assign(tiny.momentum_y.size(), 0);
    run_case(4, tiny, options, 1, true); std::cout << "PASS retained below-range source heat and unresolved owner updates\n";
    options.shock_heat = MacShockHeatRegularization::none; run_case(5, source(true), options, 1); controls(baseline);
    std::cout << "PASS disabled reference, invalid modes and atomic later failure\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
