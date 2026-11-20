// Monotone (Christensen/LULESH-limited) compression and matched shock heat:
// limiter values, identity with the pressure-sensed law where phi = 0, no
// regularization in uniform compression, and production agreement and
// checkpoint continuation for the opt-in selectors.
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <iomanip>

namespace {
using namespace ma; using namespace ma::experimental;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool same(MacScaled a, MacScaled b) { return a.mantissa() == b.mantissa() && a.exponent() == b.exponent(); }
double limiter(double d, double l, double r) {
    return mac_compression_monotone_limiter(MacScaled::from_double(d), MacScaled::from_double(l), MacScaled::from_double(r));
}
void limiter_values() {
    check(limiter(0, -1, -1) == 0 && limiter(1, 1, 1) == 0, "Monotone limiter acted outside compression");
    check(limiter(-1, -1, -1) == 1, "Uniform compression was not fully limited");
    check(limiter(-1, 0, 0) == 0 && limiter(-1, -1, 0) == 0, "Isolated jump was limited");
    check(limiter(-1, -.5, -.5) == .5 && limiter(-1, -.25, -1) == .5, "Partial monotone limiter value");
    check(limiter(-1, 1, -1) == 0 && limiter(-1, 1, 1) == 0, "Alternating jumps were limited");
    check(limiter(-1, -10, -10) == 1 && limiter(-1, -1e300, -.5) == 1, "Large neighbour ratios were not clamped");
    const auto tiny = MacScaled::from_parts(-.75, -1500), half = MacScaled::from_parts(-.75, -1501);
    check(mac_compression_monotone_limiter(tiny, tiny, tiny) == 1, "Below-range uniform compression lost its ratio");
    check(mac_compression_monotone_limiter(tiny, MacScaled::from_double(-1), half) == 1, "Below-range ratio clamp failed");
    check(mac_compression_monotone_limiter(tiny, half, {}) == 0, "Below-range isolated side was limited");
    std::cout << "PASS monotone limiter values\n";
}
MacTransportState column(std::size_t width) {
    auto s = ma::testing::setup().state(width, 2, false); s.grid.periodic_y = true;
    s.momentum_y.assign(s.grid.width * s.grid.y_height(), 0);
    for (std::size_t i = 0; i < s.cells.size(); ++i) s.cells[i].mixture.energy *= 1 + .01 * static_cast<double>(i % width);
    return s;
}
void set_velocity(MacTransportState& s, const std::vector<double>& u) {
    const auto m = mac_face_inertias(ma::testing::setup().registry, s, *ma::testing::setup().eos);
    s.momentum_x.assign(s.grid.x_width() * s.grid.height, 0);
    for (std::size_t y = 0; y < s.grid.height; ++y) for (std::size_t x = 1; x < s.grid.width; ++x)
        s.momentum_x[s.grid.x_face(x, y)] = m.x[s.grid.x_face(x, y)] * u[x];
}
void isolated_identity() {
    const auto& set = ma::testing::setup(); auto s = column(8);
    std::vector<double> u(9, 0); u[3] = 40; u[4] = -40; set_velocity(s, u);
    const auto mono = advance_mac_compression(set.registry, *set.eos, s, 1e-4, MacCompressionLaw::monotone_pressure_sensed_acoustic);
    const auto base = advance_mac_compression(set.registry, *set.eos, s, 1e-4, MacCompressionLaw::pressure_sensed_acoustic);
    check(mono.report.monotone_limiter(0, 3) == 0 && !base.report.events.empty() && mono.report.events.size() == base.report.events.size(),
        "Isolated compression changed its events");
    check(mono.state.momentum_x == base.state.momentum_x && same(mono.report.heat, base.report.heat), "Isolated monotone compression differs");
    for (std::size_t i = 0; i < s.cells.size(); ++i) check(mono.state.cells[i].mixture.energy == base.state.cells[i].mixture.energy, "Isolated monotone heat differs");
    const auto heat_mono = advance_mac_shock_heat(set.registry, *set.eos, s, 1e-4, MacShockHeatLaw::monotone_pressure_sensed);
    const auto heat_base = advance_mac_shock_heat(set.registry, *set.eos, s, 1e-4, MacShockHeatLaw::pressure_sensed_viscosity);
    check(!heat_base.report.events.empty() && heat_mono.report.events.size() == heat_base.report.events.size() &&
        same(heat_mono.report.absolute_transfer, heat_base.report.absolute_transfer), "Isolated monotone shock heat differs");
    for (std::size_t i = 0; i < s.cells.size(); ++i)
        check(heat_mono.state.cells[i].mixture.energy == heat_base.state.cells[i].mixture.energy, "Isolated monotone shock-heat U differs");
    std::cout << "PASS isolated jump reproduces pressure-sensed compression and heat (" << base.report.events.size() << " events)\n";
}
void uniform_compression() {
    const auto& set = ma::testing::setup(); auto s = column(10);
    std::vector<double> u(11, 0); for (std::size_t f = 1; f < 10; ++f) u[f] = 10 * (5 - static_cast<double>(f)); set_velocity(s, u);
    const auto mono = advance_mac_compression(set.registry, *set.eos, s, 1e-4, MacCompressionLaw::monotone_pressure_sensed_acoustic);
    const auto base = advance_mac_compression(set.registry, *set.eos, s, 1e-4, MacCompressionLaw::pressure_sensed_acoustic);
    // u = J/I reproduces the requested velocities only to rounding, so interior
    // ratios (and phi) are one to within a few ulps rather than exactly.
    for (std::size_t y = 0; y < 2; ++y) for (std::size_t x = 0; x < 10; ++x) {
        const double phi = mono.report.monotone_limiter(0, x + 10 * y);
        check(x >= 2 && x <= 7 ? phi > 1 - 1e-12 && phi <= 1 : phi == 0, "Uniform compression limiter pattern");
    }
    MacEnergySum edge, interior; std::size_t edge_events = 0;
    for (const auto& event : mono.report.events) {
        const auto x = event.cell % 10; check(event.axis == 0, "Uniform x compression acted on y");
        if (x == 1 || x == 8) { edge.add(event.pair.heat); ++edge_events; } else interior.add(event.pair.heat);
    }
    check(edge_events == 8 && base.report.events.size() == 32 && edge.sign() > 0 &&
        mac_scaled_difference(edge.value().shifted(-40), interior.value()).mantissa() > 0 &&
        mac_scaled_difference(base.report.heat, mono.report.heat).mantissa() > 0, "Uniform compression event/heat counts");
    const auto heat = advance_mac_shock_heat(set.registry, *set.eos, s, 1e-4, MacShockHeatLaw::monotone_pressure_sensed);
    MacEnergySum edge_heat, interior_heat;
    for (const auto& event : heat.report.events) (event.face % 11 <= 2 || event.face % 11 >= 8 ? edge_heat : interior_heat).add(event.pair.transfer.magnitude());
    check(edge_heat.sign() > 0 && mac_scaled_difference(edge_heat.value().shifted(-40), interior_heat.value()).mantissa() > 0,
        "Monotone shock heat crossed a fully limited face");
    std::cout << "PASS uniform compression is unregularized away from its edges (" << edge_events << '/' << base.report.events.size()
              << " compression events)\n";
}
void expansion() {
    const auto& set = ma::testing::setup();
    MacClosedSettings settings;
    settings.maximum_timestep_s = 1e-5;
    auto boiling = settings;
    boiling.phase_change = MacPhaseChange::saturation;
    check(boiling.flow_momentum() == MacMomentumReconstruction::limited_linear &&
        boiling.flow_compression() == MacCompressionViscosity::acoustic_quadratic,
        "Saturation selected a dry-gas expansion closure");
    boiling.momentum_reconstruction = MacMomentumReconstruction::donor_cell;
    check(boiling.flow_momentum() == MacMomentumReconstruction::donor_cell &&
        boiling.flow_compression() == MacCompressionViscosity::monotone_acoustic_quadratic,
        "Saturation changed an explicitly selected historical combination");
    std::array<double, 2> work{};
    for (int axis = 0; axis < 2; ++axis) {
        auto source = set.state(8, 8, false);
        const auto inertia = mac_face_inertias(set.registry, source, *set.eos);
        const auto rest = advance_mac_closed_substep(set.registry, *set.eos, source, settings.maximum_timestep_s, settings);
        check(rest.expansion_work.is_zero(), "Resting gas generated expansion work");
        for (std::size_t y = 0; y < 8; ++y) for (std::size_t x = 0; x < 8; ++x) {
            const auto n = axis == 0 ? x : y;
            if (n == 0 || n == 4) continue;
            const double velocity = n < 4 ? -1200. : 1200.;
            if (axis == 0) {
                const auto f = source.grid.x_face(x,y);
                source.momentum_x[f] = velocity * inertia.x[f];
            } else {
                const auto f = source.grid.y_face(x,y);
                source.momentum_y[f] = velocity * inertia.y[f];
            }
        }
        const auto step = advance_mac_closed_substep(set.registry, *set.eos, source, settings.maximum_timestep_s, settings);
        work[axis] = step.expansion_work.require_representable();
        check(work[axis] > 0 && work[axis] <= step.transport.mixing_heat_j, "Expansion exceeded available remap heat");
        check(std::abs(step.energy_residual_j) <= step.energy_bound_j, "Expansion failed the complete mass/energy step");
        auto legacy = settings;
        legacy.momentum_reconstruction = MacMomentumReconstruction::limited_linear;
        const auto old = advance_mac_closed_substep(set.registry, *set.eos, source, settings.maximum_timestep_s, legacy);
        check(old.expansion_work.is_zero(), "Legacy momentum setting generated expansion work");
        const MacClosedWorld world(set.registry, set.eos, source, settings);
        const auto restored = MacClosedWorld::from_checkpoint(set.registry,set.eos,world.checkpoint());
        const auto advanced = world.advanced(settings.maximum_timestep_s);
        check(restored.settings().momentum_reconstruction == settings.momentum_reconstruction &&
            restored.advanced(settings.maximum_timestep_s).world.checkpoint() == advanced.world.checkpoint(),
            "Restored expanding gas changed its scheme or continuation");
        auto production = settings;
        production.numerical_kernel = MacNumericalKernel::production;
        const MacClosedWorld accelerated(set.registry, set.eos, source, production);
        const auto candidate = accelerated.advanced(settings.maximum_timestep_s);
        check(candidate.report.interval_kernel_intervals == 1 && candidate.report.interval_reference_fallbacks == 0,
            "Expansion did not run in the production kernel");
        const auto& reference_state = advanced.world.state();
        const auto& production_state = candidate.world.state();
        const auto& ref_j = axis == 0 ? reference_state.momentum_x : reference_state.momentum_y;
        const auto& fast_j = axis == 0 ? production_state.momentum_x : production_state.momentum_y;
        for (std::size_t i = 0; i < ref_j.size(); ++i)
            check(std::abs(ref_j[i] - fast_j[i]) <= 1e-9 * std::max(1., std::abs(ref_j[i])),
                "Expanding production face momentum differs from reference");
        for (std::size_t i = 0; i < source.cells.size(); ++i) {
            const double energy = reference_state.cells[i].mixture.energy;
            const double other = production_state.cells[i].mixture.energy;
            if (std::abs(energy - other) > 1e-9 * std::max(1., std::abs(energy))) {
                std::cerr << "axis " << axis << " cell " << i << " reference U " << energy << " production U " << other << '\n';
                throw std::runtime_error("Expanding production internal energy differs from reference");
            }
        }
    }
    check(std::abs(work[0] - work[1]) < 1e-10 * work[0], "Expansion work depends on axis");
    std::cout << "PASS gas expansion work in both axes and saved continuation\n";
}
void owner() {
    const auto& set = ma::testing::setup(); auto s = column(10);
    std::vector<double> u(11, 0); for (std::size_t f = 1; f < 10; ++f) u[f] = f < 5 ? 30 : f > 5 ? -30 : 0; set_velocity(s, u);
    MacClosedSettings settings; settings.maximum_timestep_s = 2e-5;
    settings.compression_viscosity = MacCompressionViscosity::monotone_acoustic_quadratic;
    settings.shock_heat = MacShockHeatRegularization::monotone_pressure_sensed;
    const MacClosedWorld reference(set.registry, set.eos, s, settings);
    const auto a = reference.advanced(1e-4);
    check(a.report.compression_events > 0 && a.report.shock_heat_events > 0 && a.report.rejected_attempts == 0, "Owner monotone stages inactive");
    auto production = settings; production.numerical_kernel = MacNumericalKernel::production;
    auto compression_only = MacClosedSettings{}, heat_only = MacClosedSettings{};
    compression_only.compression_viscosity = settings.compression_viscosity; heat_only.shock_heat = settings.shock_heat;
    check(mac_kernel_supports(compression_only) && mac_kernel_supports(heat_only), "Production rejected a monotone selector");
    const MacClosedWorld accelerated(set.registry, set.eos, s, production);
    const auto b = accelerated.advanced(1e-4);
    check(b.report.interval_reference_fallbacks == 0 && b.report.interval_kernel_intervals == 1 &&
        b.report.compression_events > 0 && b.report.shock_heat_events > 0, "Monotone production stages inactive");
    const auto close = [](double lhs, double rhs) {
        return std::abs(lhs - rhs) <= 1e-9 * std::max({1., std::abs(lhs), std::abs(rhs)});
    };
    for (std::size_t i = 0; i < s.cells.size(); ++i) {
        const auto& lhs = a.world.state().cells[i].mixture;
        const auto& rhs = b.world.state().cells[i].mixture;
        check(close(lhs.energy, rhs.energy), "Monotone production energy differs from reference");
        for (std::size_t k = 0; k < lhs.masses.size(); ++k)
            check(close(lhs.masses[k], rhs.masses[k]), "Monotone production mass differs from reference");
    }
    for (std::size_t i = 0; i < s.momentum_x.size(); ++i)
        check(close(a.world.state().momentum_x[i], b.world.state().momentum_x[i]), "Monotone production momentum differs from reference");
    const auto image = a.world.checkpoint();
    const auto restored = MacClosedWorld::from_checkpoint(set.registry, set.eos, image);
    check(restored.settings().compression_viscosity == settings.compression_viscosity && restored.settings().shock_heat == settings.shock_heat &&
        restored.checkpoint() == image, "Monotone selectors did not round-trip");
    check(ma::testing::fingerprint(a.world.advanced(1e-4).world.state()) == ma::testing::fingerprint(restored.advanced(1e-4).world.state()),
        "Restored monotone owner diverged");
    auto historical = production;
    historical.momentum_reconstruction = MacMomentumReconstruction::limited_linear;
    const MacClosedWorld old_owner(set.registry, set.eos, s, historical);
    const auto saved_old = MacClosedWorld::from_checkpoint(set.registry, set.eos, old_owner.checkpoint());
    const auto continued = saved_old.advanced(1e-4);
    check(continued.report.interval_reference_fallbacks == 1 && continued.report.interval_kernel_intervals == 0,
        "Saved historical monotone owner changed its numerical kernel");
    historical.numerical_kernel = MacNumericalKernel::reference;
    const MacClosedWorld old_reference(set.registry, set.eos, s, historical);
    check(ma::testing::fingerprint(continued.world.state()) == ma::testing::fingerprint(old_reference.advanced(1e-4).world.state()),
        "Historical monotone checkpoint changed its trajectory");
    auto invalid = settings; invalid.compression_viscosity = static_cast<MacCompressionViscosity>(5);
    bool rejected = false; try { invalid.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    invalid = settings; invalid.shock_heat = static_cast<MacShockHeatRegularization>(5);
    bool heat_rejected = false; try { invalid.validate(); } catch (const std::invalid_argument&) { heat_rejected = true; }
    check(rejected && heat_rejected, "Unknown regularization selector accepted");
    std::cout << "PASS owner production agreement and checkpoint continuation (" << a.report.substeps << " substeps, "
              << a.report.compression_events << " compression events)\n";
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        limiter_values(); isolated_identity(); uniform_compression(); owner(); expansion();
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
