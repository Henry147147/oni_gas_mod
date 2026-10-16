#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacTransportState gas(double mass, double volume = 1) {
    const auto& set = ma::testing::setup(); auto state = set.state(2, 2, true); state.grid.spacing_m = std::sqrt(volume);
    for (auto& c : state.cells) {
        std::vector<double> masses(set.registry.size()); masses[set.registry.index("nitrogen")] = mass;
        masses[set.registry.index("helium")] = mass * 1e-28;
        c.mixture = set.eos->initialize_at_temperature(masses, 300, volume).mixture;
        c.passive_quantities = {{17, mass}, {UINT64_C(0x80000000), mass * 1e-30}};
    }
    return state;
}
MacMassFluxes sweeps(const MacTransportState& source, double volume) {
    return {std::vector<double>(source.momentum_x.size(), volume), std::vector<double>(source.momentum_y.size())};
}
MacTransportAdvance run(int id, const MacTransportState& source, double volume) {
    const auto& set = ma::testing::setup(); const auto held = ma::testing::fingerprint(source);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source,
        std::vector<double>(source.cells.size(), source.grid.volume_m3()), sweeps(source, volume));
    auto result = advance_mac_parcel_transport(set.registry, source, *set.eos, plan);
    check(ma::testing::fingerprint(source) == held, "Momentum contribution changed source");
    ma::testing::export_transport(id, source, result, plan);
    return result;
}
void identified_share() {
    constexpr double mass = 3.0517578125e-5, volume = 0.000244140625;
    auto source = gas(mass, volume); std::fill(source.momentum_x.begin(), source.momentum_x.end(), 2.5697272365554806e-293);
    const auto result = run(0, source, volume * 4.2934097091770358e-297 / mass);
    check(ma::testing::fingerprint(result.state) == ma::testing::fingerprint(source), "Tiny incoming momentum changed uniform state");
    bool exercised = false;
    for (const auto& edge : result.report.x.fluxes) if (edge.mass_kg != 0) {
        const auto donor = edge.mass_kg > 0 ? edge.left : edge.right;
        const auto full = MacScaled::from_double(source.momentum_x[donor]).shared(std::abs(edge.mass_kg), result.report.x.old_inertia[donor]);
        check(full.mantissa() > 0 && full.project().rounded && full.project().value == 0, "Identified incoming momentum did not retain a subrange value");
        exercised = true;
    }
    check(exercised, "Identified incoming momentum bypassed transport");
}
void minimum_uniform() {
    auto source = gas(1); std::fill(source.momentum_x.begin(), source.momentum_x.end(), std::numeric_limits<double>::denorm_min());
    const auto result = run(1, source, .125);
    check(ma::testing::fingerprint(result.state) == ma::testing::fingerprint(source), "Subnormal retained/incoming J did not preserve a uniform state");
}
void signed_cancellation() {
    auto source = gas(1); const double unit = std::numeric_limits<double>::denorm_min();
    for (std::size_t i = 0; i < source.momentum_x.size(); ++i) source.momentum_x[i] = (i % 2 == 0 ? 1 : -3) * unit;
    const auto result = run(2, source, .25);
    for (std::size_t i = 0; i < source.momentum_x.size(); ++i)
        check(result.state.momentum_x[i] == (i % 2 == 0 ? 0 : -2) * unit, "Signed subrange contributions failed exact cancellation");
    check(result.report.mixing_heat_wide_j.mantissa() > 0 && result.report.mixing_heat_j == 0 && result.report.unresolved_heat_updates > 0,
        "Subrange cancellation did not retain positive mixing heat and unresolved U accounting");
}
void unresolved_destination() {
    const auto& set = ma::testing::setup(); auto source = gas(3.0517578125e-5, .000244140625); source.momentum_x[0] = 2.5697272365554806e-293;
    const auto held = ma::testing::fingerprint(source); bool rejected = false;
    try {
        const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, source, std::vector<double>(4, source.grid.volume_m3()),
            sweeps(source, source.grid.volume_m3() * 4.2934097091770358e-297 / 3.0517578125e-5));
        static_cast<void>(advance_mac_parcel_transport(set.registry, source, *set.eos, plan));
    } catch (const FluidFatalError& error) { rejected = error.code() == FluidFatalCode::numerical_precision; }
    check(rejected && ma::testing::fingerprint(source) == held, "Unresolved accumulated J was accepted or changed source");
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        identified_share(); std::cout << "PASS incoming momentum below binary64 reaches its accumulator\n";
        minimum_uniform(); std::cout << "PASS retained and incoming subnormal J recombine exactly\n";
        signed_cancellation(); std::cout << "PASS signed subrange J cancellation and positive mixing heat\n";
        unresolved_destination(); std::cout << "PASS unresolved accumulated primary J rejects atomically\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
