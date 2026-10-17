#include "mac_compression_fixture.hpp"
#include <iomanip>
#include <array>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacTransportState source(std::size_t id) {
    const auto& set = ma::testing::setup(); auto s = id == 2 ? set.state(2, 2, true) : set.state(3, id == 0 ? 2 : 3, false, id == 1, id == 1);
    const auto m = mac_face_inertias(set.registry, s, *set.eos);
    if (id == 2) {
        std::vector<MacScaled> j(s.momentum_x.size());
        for (std::size_t f = 0; f < j.size(); ++f) j[f] = MacScaled::from_parts(f % 2 == 0 ? .75 : -.75, -1941);
        store_mac_field(s.momentum_x, s.full_momentum_x, j);
    } else {
        for (std::size_t f = 0; f < s.momentum_x.size(); ++f) if (!ma::testing::compression_fixed_face(s.grid, 0, f))
            s.momentum_x[f] = m.x[f] * (f % s.grid.x_width() == 1 ? 12 : -8);
    }
    return s;
}
void combined(std::size_t id) {
    const auto& set = ma::testing::setup(); const auto s = source(id); const auto held = ma::testing::fingerprint(s); constexpr double dt = 1e-4;
    MacClosedSettings settings; settings.maximum_timestep_s = dt; settings.shock_heat = MacShockHeatRegularization::none;
    settings.compression_viscosity = MacCompressionViscosity::quadratic;
    const auto first = advance_mac_compression(set.registry, *set.eos, s, .5 * dt);
    const auto primitive = advance_mac_closed_substep(set.registry, *set.eos, first.state, dt, settings);
    const auto compression = advance_mac_compression(set.registry, *set.eos, primitive.state, .5 * dt);
    const MacClosedWorld owner(set.registry, set.eos, s, settings); const auto result = owner.advanced(dt, 4); const auto& r = result.report;
    check(ma::testing::fingerprint(s) == held && ma::testing::fingerprint(owner.state()) == held && owner.time_s() == 0 && owner.revision() == 1 &&
        result.world.time_s() == dt && result.world.revision() == 2 && r.substeps == 1 && r.attempts == 1 && r.rejected_attempts == 0, "Compression owner atomic state/clock differs");
    check(ma::testing::fingerprint(result.world.state()) == ma::testing::fingerprint(compression.state), "Owner did not apply the declared compression stage");
    MacEnergySum heat; heat.add(first.report.heat); heat.add(compression.report.heat);
    check(r.compression_events == first.report.events.size() + compression.report.events.size() && r.compression_events > 0 &&
        r.unresolved_compression_heat_updates == first.report.unresolved_heat_updates + compression.report.unresolved_heat_updates &&
        r.unresolved_compression_momentum_updates == first.report.unresolved_momentum_updates + compression.report.unresolved_momentum_updates &&
        r.compression_heat_wide_j.mantissa() == heat.value().mantissa() && r.compression_heat_wide_j.exponent() == heat.value().exponent(), "Owner compression report differs");
    if (id == 2) check(r.compression_heat_wide_j.mantissa() > 0 && r.compression_heat_j == 0 && r.unresolved_compression_heat_updates > 0, "Owner lost below-range heat/update flag");
    ma::testing::export_compression(id, s, first, .5 * dt, "B");
    ma::testing::export_case(static_cast<int>(id), first.state, primitive, dt);
    ma::testing::export_compression(id, primitive.state, {result.world.state(), compression.report}, .5 * dt, "C");
    std::cout << "OWNER " << id << ' ' << owner.time_s() << ' ' << owner.revision() << ' ' << result.world.time_s() << ' ' << result.world.revision() << ' ' <<
        r.substeps << ' ' << r.attempts << ' ' << r.rejected_attempts << ' ' << r.compression_events << ' ' << r.unresolved_compression_heat_updates << ' ' << r.unresolved_compression_momentum_updates << ' ' << r.compression_heat_j;
    for (const auto& value : {r.compression_heat_wide_j, r.energy_residual_wide_j, r.energy_bound_wide_j, r.full_wall_impulse_x, r.full_wall_impulse_y}) ma::testing::export_compression_scaled(value);
    std::cout << ' ' << r.entropy_change_j_per_k << ' ' << r.entropy_roundoff_j_per_k << '\n';
}
void coupled_time_refinement() {
    constexpr double il = 2, ir = 4, force = 4, coefficient = .4, duration = .5;
    const double inverse = 1 / il + 1 / ir, equilibrium = std::sqrt(force / coefficient);
    const double z = (5 - equilibrium) / (5 + equilibrium) * std::exp(-2 * inverse * coefficient * equilibrium * duration);
    const double exact = equilibrium * (1 + z) / (1 - z); double previous = 0;
    for (int n : {8, 16, 32}) {
        const double dt = duration / n; std::array<double, 2> outputs{}; MacEnergySum heat, work;
        for (int mode = 0; mode < 2; ++mode) {
            auto jl = MacScaled::from_double(6), jr = MacScaled::from_double(-8);
            for (int step = 0; step < n; ++step) {
                if (mode == 0) {
                    const auto first = relax_mac_compression_pair(il, ir, jl, jr, false, false, MacScaled::from_double(.5 * dt * coefficient));
                    jl = first.after_left; jr = first.after_right; heat.add(first.heat);
                }
                const auto closing = mac_scaled_difference(jl.shared(1, il), jr.shared(1, ir));
                const auto impulse = MacScaled::from_double(force * dt); MacScaledSum midpoint; midpoint.add(closing); midpoint.add(impulse.multiplied(.5 * inverse));
                if (mode == 0) work.add(impulse.shared(midpoint.value(), MacScaled::from_double(1)));
                MacScaledSum left, right; left.add(jl); left.add(impulse); right.add(jr); right.add(impulse.negated());
                jl = mac_scaled_primary(left); jr = mac_scaled_primary(right);
                const auto last = relax_mac_compression_pair(il, ir, jl, jr, false, false, MacScaled::from_double((mode == 0 ? .5 : 1) * dt * coefficient));
                jl = last.after_left; jr = last.after_right; if (mode == 0) heat.add(last.heat);
            }
            outputs[static_cast<std::size_t>(mode)] = mac_scaled_difference(jl.shared(1, il), jr.shared(1, ir)).require_representable();
        }
        const double error = std::abs(outputs[0] - exact) / 5, post_error = std::abs(outputs[1] - exact) / 5;
        check(error > 0 && (previous == 0 || error < .3 * previous), "Symmetric forced compression fails temporal refinement"); previous = error;
        if (n == 32) check(error < 1e-4 && error < .1 * post_error, "Symmetric forced compression lacks accuracy improvement");
        const double reduced = 1 / inverse;
        MacEnergyAccount energy; energy.add(MacEnergy::variance(reduced, outputs[0])); energy.add(MacEnergy::variance(reduced, 5).negated());
        energy.add(heat.value()); energy.add(work.value().negated()); check(energy.within_bound(), "Forced compression K/heat/work failed");
        std::cout << "COUPLING " << n << ' ' << duration << ' ' << il << ' ' << ir << ' ' << force << ' ' << coefficient << ' ' << 5 << ' ' << outputs[0] << ' ' << outputs[1];
        ma::testing::export_compression_scaled(heat.value()); ma::testing::export_compression_scaled(work.value());
        std::cout << ' ' << error << ' ' << post_error << '\n';
    }
}
void controls() {
    const auto& set = ma::testing::setup(); const auto s = source(0); const auto held = ma::testing::fingerprint(s); constexpr double dt = 1e-4;
    MacClosedSettings settings; settings.maximum_timestep_s = dt; settings.compression_viscosity = MacCompressionViscosity::none;
    settings.shock_heat = MacShockHeatRegularization::none;
    const auto primitive = advance_mac_closed_substep(set.registry, *set.eos, s, dt, settings);
    const MacClosedWorld disabled(set.registry, set.eos, s, settings); const auto result = disabled.advanced(dt, 4);
    check(ma::testing::fingerprint(result.world.state()) == ma::testing::fingerprint(primitive.state) && result.report.compression_events == 0 &&
        result.report.compression_heat_wide_j.is_zero(), "Disabled reference mode applied compression");
    settings.compression_viscosity = MacCompressionViscosity::quadratic; const MacClosedWorld owner(set.registry, set.eos, s, settings);
    bool rollback = false; try { static_cast<void>(owner.advanced(2 * dt, 1)); } catch (const std::runtime_error&) { rollback = true; }
    check(rollback && owner.time_s() == 0 && owner.revision() == 1 && ma::testing::fingerprint(owner.state()) == held, "Later failure published a compressed candidate");
    settings.compression_viscosity = static_cast<MacCompressionViscosity>(-1); bool invalid = false;
    try { static_cast<void>(MacClosedWorld(set.registry, set.eos, s, settings)); } catch (const std::invalid_argument&) { invalid = true; }
    check(invalid && ma::testing::fingerprint(s) == held, "Invalid compression mode accepted or changed source");
    std::cout << "OCONTROL 1 1 1\n";
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& set = ma::testing::setup();
    for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    combined(0); std::cout << "PASS gas owner compression and wall accounts\n";
    combined(1); std::cout << "PASS mixed-liquid owner compression and actual U\n";
    combined(2); std::cout << "PASS full-range owner heat and unresolved increments\n";
    controls(); std::cout << "PASS disabled reference and atomic later failure\n";
    coupled_time_refinement(); std::cout << "PASS symmetric forced compression time refinement\n"; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
