// Monotone (Christensen/LULESH-limited) compression and matched shock heat:
// limiter values, identity with the pressure-sensed law where phi = 0, no
// regularization in uniform compression, and owner reference fallback and
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
    check(mac_kernel_supports(MacClosedSettings{}) && !mac_kernel_supports(compression_only) && !mac_kernel_supports(heat_only),
        "Production kernel claimed a monotone selector");
    const MacClosedWorld fallback(set.registry, set.eos, s, production);
    const auto b = fallback.advanced(1e-4);
    check(b.report.interval_reference_fallbacks == 1 && ma::testing::fingerprint(a.world.state()) == ma::testing::fingerprint(b.world.state()),
        "Production setting did not take the identical reference interval");
    const auto image = a.world.checkpoint();
    const auto restored = MacClosedWorld::from_checkpoint(set.registry, set.eos, image);
    check(restored.settings().compression_viscosity == settings.compression_viscosity && restored.settings().shock_heat == settings.shock_heat &&
        restored.checkpoint() == image, "Monotone selectors did not round-trip");
    check(ma::testing::fingerprint(a.world.advanced(1e-4).world.state()) == ma::testing::fingerprint(restored.advanced(1e-4).world.state()),
        "Restored monotone owner diverged");
    auto invalid = settings; invalid.compression_viscosity = static_cast<MacCompressionViscosity>(5);
    bool rejected = false; try { invalid.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    invalid = settings; invalid.shock_heat = static_cast<MacShockHeatRegularization>(5);
    bool heat_rejected = false; try { invalid.validate(); } catch (const std::invalid_argument&) { heat_rejected = true; }
    check(rejected && heat_rejected, "Unknown regularization selector accepted");
    std::cout << "PASS owner reference fallback and checkpoint continuation (" << a.report.substeps << " substeps, "
              << a.report.compression_events << " compression events)\n";
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        limiter_values(); isolated_identity(); uniform_compression(); owner();
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
