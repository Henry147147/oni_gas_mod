// Production kernel contract controls (native/MAC_KERNEL.md): checkpoint
// continuation, atomic failure, results independent of the thread count,
// fail-closed admission of unported selectors, the production policy's
// equilibrium certificate and the interval audit report.
#include "mac_kernel_fixture.hpp"
#include <cstdlib>

using namespace ma::testing;
using namespace ma::testing::kernel;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << '\n';
    if (!ok) ++failures;
}
MacClosedWorld owner(const Scene& sc) { const auto& s = setup(); return MacClosedWorld(s.registry, s.eos, sc.state, sc.settings); }
MacClosedWorld production_run(MacClosedWorld w, std::size_t intervals, unsigned threads, double seconds = .1) {
    MacKernelOptions o; o.threads = threads;
    for (std::size_t n = 0; n < intervals; ++n) w = advance_mac_production(w, seconds, 1024, o).world;
    return w;
}
}

int main(int argc, char** argv) {
    try {
        std::vector<unsigned> thread_counts{1, 2, 4, 8, 12};
        if (argc > 1 && std::string(argv[1]) == "--quick") thread_counts = {1, 3};
        const auto& s = setup();
        // Checkpoint continuation: an interrupted run continues byte for byte.
        for (auto sc : {wave(16, 16), room(24, 16), liquid(8, 8), mixed(12, 12), vacuum(16, 8)}) {
            if (sc.name == "empty_cells") sc.settings.maximum_timestep_s = 1. / 4096;
            const double seconds = sc.name == "empty_cells" ? .02 : .1;
            const auto uninterrupted = production_run(owner(sc), 2, 1, seconds);
            const auto first = production_run(owner(sc), 1, 1, seconds);
            const auto restored = MacClosedWorld::from_checkpoint(s.registry, s.eos, first.checkpoint());
            const auto resumed = production_run(restored, 1, 1, seconds);
            check(uninterrupted.checkpoint() == resumed.checkpoint() && resumed.revision() == 3,
                "checkpoint continuation " + sc.name);
        }
        // Results never depend on the thread count (fixed blocks, block-ordered
        // reductions, line-ordered sweeps).
        for (auto sc : {wave(40, 37), room(33, 21), hydrostatic(9, 31), conduction(27, 18), viscous_wave(19, 23)}) {
            std::vector<std::uint8_t> first;
            bool same = true;
            for (const auto t : thread_counts) {
                const auto bytes = production_run(owner(sc), 2, t).checkpoint();
                if (first.empty()) first = bytes; else same = same && bytes == first;
            }
            check(same, "thread-count independence " + sc.name);
        }
        // Atomic failure: an exhausted attempt budget publishes nothing.
        {
            auto sc = vacuum(16, 8); // the first 0.01 s attempts reject (volume CFL)
            const auto source = owner(sc); const auto before = source.checkpoint();
            bool threw = false;
            try { static_cast<void>(advance_mac_production(source, .1, 2, {})); } catch (const std::runtime_error&) { threw = true; }
            check(threw && source.checkpoint() == before, "budget exhaustion is atomic");
        }
        // Fail-closed admission of unported selectors.
        {
            auto sc = wave(8, 8); sc.settings.reconstruction = MacMaterialReconstruction::swept_first_moment;
            bool rejected = false;
            try { static_cast<void>(advance_mac_production(owner(sc), .1, 64, {})); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "swept reconstruction rejects");
            sc = wave(8, 8); sc.settings.compression_viscosity = MacCompressionViscosity::coupled_pressure; rejected = false;
            try { static_cast<void>(advance_mac_production(owner(sc), .1, 64, {})); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "coupled compressive pressure rejects");
        }
        // Production policy: exact equilibrium certificate first.
        {
            const MacClosedWorld rest(s.registry, s.eos, s.state(8, 8, true));
            const auto r = rest.advanced(.1, 64, MacExecutionPolicy::production);
            check(r.report.exact_equilibrium_intervals == 1 && r.world.revision() == rest.revision() + 1, "production policy certifies exact equilibrium");
            const auto moving = owner(wave(8, 8)).advanced(.1, 64, MacExecutionPolicy::production);
            check(moving.report.exact_equilibrium_intervals == 0 && moving.report.substeps > 0, "production policy advances a wave");
        }
        // Opt-in extension of the persisted interval kernel's admission: an
        // unported selector set goes to the production kernel (not the
        // reference), agreeing with the reference within the tolerances; a
        // subnormal trace still takes the reference.
        {
            auto sc = wave(8, 8); sc.settings.numerical_kernel = MacNumericalKernel::interval_audited;
            const auto source = owner(sc);
            const auto off = source.advanced(.1);
            auto options = mac_kernel_default_options(); options.extend_interval_admission = true;
            set_mac_kernel_default_options(options);
            const auto on = source.advanced(.1);
            auto trace = sc; for (auto& cell : trace.state.cells) cell.mixture.masses[s.registry.index("helium")] = 1e-310;
            const auto flagged = owner(trace).advanced(.1);
            options.extend_interval_admission = false; set_mac_kernel_default_options(options);
            const auto m = compare(off.world, on.world, sc.disturbance_pa);
            check(off.report.interval_reference_fallbacks == 1 && on.report.interval_kernel_intervals == 1 &&
                m.pressure_over_disturbance < 1e-6 && m.temperature_k < 1e-6 && on.world.settings().numerical_kernel == MacNumericalKernel::interval_audited,
                "interval admission extension takes production selectors");
            check(flagged.report.interval_reference_fallbacks == 1 && flagged.report.interval_kernel_intervals == 0,
                "flagged range keeps the reference fallback");
        }
        // Interval audit report: every quantity audited and passed.
        {
            MacKernelOptions o; static_cast<void>(advance_mac_production(owner(room(16, 12)), .1, 1024, o));
            const auto& k = last_mac_production_report();
            bool all = !k.audits.empty();
            for (const auto& a : k.audits) {
                all = all && a.passed && std::abs(a.residual) <= a.bound;
                std::cout << "AUDIT " << a.quantity << " residual=" << a.residual << " bound=" << a.bound << (a.escalated ? " exact" : "") << '\n';
            }
            check(all && k.entropy_change_j_per_k >= -k.entropy_allowance_j_per_k, "interval audit covers every quantity");
        }
        std::cout << (failures == 0 ? "ALL PASS" : "SOME FAILED") << '\n';
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
