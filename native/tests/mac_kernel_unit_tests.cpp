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
        // reductions, line-ordered sweeps). The two larger grids have nine row
        // blocks each, so up to nine threads run blocks concurrently.
        for (auto sc : {wave(40, 37), room(33, 21), hydrostatic(9, 31), conduction(27, 18), viscous_wave(19, 23), wave(96, 90), room(128, 72)}) {
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
        // Gas diffusion: the flow-only entry rejects a diffusion-enabled owner;
        // the production policy wraps the kernel in the owner's diffusion half
        // steps and agrees with the reference composition.
        {
            auto sc = room(24, 16);
            const auto source = owner(sc).with_diffusion(MacDiffusionSettings{});
            bool rejected = false;
            try { static_cast<void>(advance_mac_production(source, .1, 1024, {})); } catch (const std::invalid_argument&) { rejected = true; }
            const auto ref = source.advanced(.1, 1024, MacExecutionPolicy::reference);
            const auto prod = source.advanced(.1, 1024, MacExecutionPolicy::production);
            const auto m = compare(ref.world, prod.world, sc.disturbance_pa);
            std::cout << "DIFFUSION substeps=" << prod.report.diffusion.substeps << " dT_K=" << m.temperature_k
                << " dp_over_disturbance=" << m.pressure_over_disturbance << " dO2pp_rel=" << m.oxygen_relative
                << " dconstituent_rel=" << m.constituent_relative << '\n';
            check(rejected && prod.report.diffusion.substeps > 0 && ref.report.diffusion.substeps == prod.report.diffusion.substeps &&
                m.temperature_k <= .1 && m.pressure_over_disturbance <= .01 && m.oxygen_relative <= .01 && m.constituent_relative <= .01,
                "production policy composes gas diffusion");
        }
        // Persisted production selection admits the production physical
        // defaults and below-range traces; flagged large values refine.
        {
            auto sc = wave(8, 8);
            const auto off = owner(sc).advanced(.1);
            sc.settings.numerical_kernel = MacNumericalKernel::production;
            const auto on = owner(sc).advanced(.1);
            auto trace = sc; for (auto& cell : trace.state.cells) cell.mixture.masses[s.registry.index("helium")] = 1e-310;
            auto huge = sc; huge.state.cells[0].passive_quantities.back().quantity = 1e200;
            const auto subnormal = owner(trace).advanced(.1);
            const auto flagged = owner(huge).advanced(.1);
            const auto m = compare(off.world, on.world, sc.disturbance_pa);
            check(on.report.interval_kernel_intervals == 1 && m.pressure_over_disturbance < 1e-6 && m.temperature_k < 1e-6 &&
                on.world.settings().numerical_kernel == MacNumericalKernel::production, "persisted production selects production physics");
            check(subnormal.report.interval_kernel_intervals == 1 && flagged.report.interval_reference_fallbacks == 1 &&
                flagged.report.interval_kernel_intervals == 0, "below-range admitted, overflow-range keeps the reference fallback");
        }
        // Roundoff-level transverse momentum decays far below binary64's
        // normal range on a tall grid; it rounds (counted) without a
        // reference fallback, a limiter fault or an audit failure.
        {
            auto w = owner(wave(32, 512));
            bool fallback = false;
            for (int n = 0; n < 3; ++n) { w = advance_mac_production(w, .1, 1024, {}).world; fallback = fallback || last_mac_production_report().reference_fallback; }
            check(!fallback, "below-range transverse momentum stays on the production path");
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
