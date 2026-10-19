// Count pressure-momentum storage additions inside the unchanged interval bound.
#include "mac_kernel_fixture.hpp"
#include "../src/mac_kernel_detail.hpp"
#include <bit>
#include <iomanip>
using namespace ma::testing;
namespace {
void check(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
void additions() {
    mac_kernel::PrimaryRounding total;
    std::size_t id = 0;
    const auto sample = [&](double before, double increment) {
        const double stored = before + increment;
        mac_kernel::PrimaryRounding value; value.addition(before, increment, stored); total.merge(value);
        check(value.within_bound(std::numeric_limits<double>::max()), "Correct addition failed its rounding account");
        std::cout << "WADD " << id++ << ' ' << before << ' ' << increment << ' ' << stored << ' ' << value.count
            << ' ' << value.error.hi << ' ' << value.error.lo << ' ' << value.half_ulps.hi << ' ' << value.half_ulps.lo
            << ' ' << value.allowance() << '\n';
    };
    const double tiny = std::numeric_limits<double>::denorm_min(), normal = std::numeric_limits<double>::min();
    for (double a : {0., tiny, 3 * tiny, normal, 2 * normal, 1., std::nextafter(1., 2.), 2., 0x1p450})
        for (double b : {0., tiny, -tiny, 0x1p-107, 0x1p-54, 0x1p-53, 0x1p396}) {
            sample(a, b); sample(-a, -b); sample(a, -a);
        }
    std::uint64_t state = UINT64_C(0x3ab9a538814f6d19);
    const auto next = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    for (std::size_t i = 0; i < 1024; ++i) {
        const auto bits = next(); const auto exponent = next(); const auto shift = next();
        const double a = std::ldexp(std::bit_cast<double>((bits & UINT64_C(0xfffffffffffff)) | UINT64_C(0x3fe0000000000000)),
            static_cast<int>(exponent % 1451) - 1000);
        const double b = std::ldexp(a, -static_cast<int>(shift % 100));
        sample(i % 2 ? -a : a, i % 3 ? b : -b);
    }
    check(total.count > 500 && total.within_bound(std::numeric_limits<double>::max()), "Merged primary ledger lost rounding");
    std::cout << "WMERGE " << id << ' ' << total.count << ' ' << total.error.hi << ' ' << total.error.lo
        << ' ' << total.half_ulps.hi << ' ' << total.half_ulps.lo << ' ' << total.allowance() << '\n';
    auto malformed = total; malformed.count = 0;
    check(!malformed.within_bound(std::numeric_limits<double>::max()), "Uncounted primary errors passed");
    check(!total.within_bound(total.allowance() / 2), "Primary charges exceeded the interval budget unnoticed");
    for (const auto stored : {std::nextafter(1., 2.), 2., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        mac_kernel::PrimaryRounding bad; bad.addition(1., 0x1p-53, stored);
        check(!bad.within_bound(std::numeric_limits<double>::max()), "Invalid or odd-tie primary write passed");
    }
    mac_kernel::PrimaryRounding overflow; overflow.count = std::numeric_limits<std::size_t>::max();
    bool rejected = false; try { overflow.addition(1., 0x1p-54, 1.); } catch (const FluidFatalError&) { rejected = true; }
    check(rejected, "Primary counter wrapped");
    std::cout << "WCONTROLS 1 1 1 1\n";
}
void same_audit(const MacKernelReport& a, const MacKernelReport& b) {
    check(a.rounded_primary_writes == b.rounded_primary_writes && a.rounded_inventory_writes == b.rounded_inventory_writes && a.audits.size() == b.audits.size() &&
        a.rounded_energy_writes == b.rounded_energy_writes && a.discarded_energy_remainders == b.discarded_energy_remainders, "Primary count depends on execution history");
    for (std::size_t i = 0; i < a.audits.size(); ++i) {
        const auto& x = a.audits[i]; const auto& y = b.audits[i];
        check(x.quantity == y.quantity && x.rounded_primary_writes == y.rounded_primary_writes &&
            x.rounded_inventory_writes == y.rounded_inventory_writes &&
            x.rounding_allowance == y.rounding_allowance && x.rounded_energy_writes == y.rounded_energy_writes &&
            x.discarded_energy_remainders == y.discarded_energy_remainders, "Primary audit depends on workers/cache/rejected work");
    }
}
void evolution(MacPressureIntegration method) {
    const auto& s = setup(); auto scene = kernel::wave(193, 173);
    for (std::size_t i = 0; i < scene.state.cells.size(); ++i) {
        const double x = (static_cast<double>(i % 193) + .5) / 193, y = (static_cast<double>(i / 193) + .5) / 173;
        scene.state.cells[i].mixture = s.mixture(1, 100000 + 20 * std::cos(2 * std::numbers::pi * x) +
            10 * std::cos(2 * std::numbers::pi * y), 300);
    }
    kernel::reset_passives(s, scene.state); kernel::set_velocity(s, scene.state, 1, .5);
    scene.settings.numerical_kernel = MacNumericalKernel::production; scene.settings.pressure_integration = method;
    const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings); const auto held = source.checkpoint();
    MacKernelOptions options; options.threads = 1;
    const auto one = advance_mac_production(source, .002, 1024, options); const auto first = last_mac_production_report();
    options.threads = 12;
    const auto many = advance_mac_production(source, .002, 1024, options); const auto second = last_mac_production_report();
    check(!second.reference_fallback && second.threads_used == 12 && second.rounded_primary_writes > 0 && second.rounded_inventory_writes > 0 &&
        one.world.checkpoint() == many.world.checkpoint(), "Pressure rounding left production or changed threaded state");
    same_audit(first, second);
    std::size_t total = 0, axes = 0;
    for (const auto& a : second.audits) {
        check(a.passed && a.rounding_allowance <= a.bound, "Primary charge does not fit the original interval bound");
        total += a.rounded_primary_writes;
        if (a.rounded_primary_writes) {
            ++axes;
            check(a.quantity == "momentum_x" || a.quantity == "momentum_y", "Pressure ledger counted another physical stage");
            std::cout << "WAXIS " << static_cast<int>(method) << ' ' << a.quantity << ' ' << a.rounded_primary_writes
                << ' ' << a.rounding_allowance << ' ' << a.bound << '\n';
        }
    }
    check(axes == 2 && total == second.rounded_primary_writes && many.report.interval_rounded_updates ==
        total + second.rounded_transfers + second.rounded_range_inputs + second.rounded_inventory_writes + second.rounded_energy_writes + second.discarded_energy_remainders, "Primary count did not reach the owner");
    bool rejected = false; try { (void)advance_mac_production(source, .02, 1, options); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected && source.checkpoint() == held && last_mac_production_report().rounded_primary_writes == 0,
        "Failed interval leaked primary rounding or changed its source");
    const auto repeated = advance_mac_production(source, .002, 1024, options); same_audit(second, last_mac_production_report());
    check(repeated.world.checkpoint() == many.world.checkpoint(), "Failed interval changed primary continuation");
    set_mac_kernel_default_options(options);
    const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, many.world.checkpoint()).advanced(.002);
    const auto resumed_report = last_mac_production_report(); options.threads = 1; set_mac_kernel_default_options(options);
    const auto continued = one.world.advanced(.002); same_audit(resumed_report, last_mac_production_report());
    check(restored.world.checkpoint() == continued.world.checkpoint(), "Primary ledger changed exact checkpoint continuation");
    std::cout << "WCASE " << static_cast<int>(method) << ' ' << scene.state.cells.size() << ' ' << second.threads_used
        << ' ' << total << " 1 1 1\n";
}
}
int main() {
    try {
        std::cout << std::setprecision(17) << "WCONTRACT 128\nWSETUP 193 173 " << .002 << ' ' << .01
            << " 1 0.5 100000 20 10 300\n"; additions();
        for (const auto method : {MacPressureIntegration::discrete_gradient_midpoint, MacPressureIntegration::two_moment,
            MacPressureIntegration::two_moment_coupled}) evolution(method);
        std::cout << "WPASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
