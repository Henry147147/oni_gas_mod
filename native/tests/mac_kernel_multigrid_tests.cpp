// Geometric multigrid inside the production kernel: unchanged physical laws,
// cache-independent continuation, active geometry changes and one worker pool.
#include "mac_kernel_fixture.hpp"
#include <future>

using namespace ma::testing;
using namespace ma::testing::kernel;

namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacClosedWorld owner(const Scene& scene) {
    const auto& s = setup(); return {s.registry, s.eos, scene.state, scene.settings};
}
MacClosedAdvance advance(const MacClosedWorld& w, unsigned threads, double seconds) {
    MacKernelOptions options; options.threads = threads;
    options.preconditioner = MacKernelPreconditioner::multigrid;
    return advance_mac_production(w, seconds, 4096, options);
}
void run(Scene scene) {
    if (scene.name == "empty_cells") scene.settings.maximum_timestep_s = 1. / 4096;
    // Give every material scene a mechanical disturbance, including the
    // isobaric mixture fixture whose zero-expansion conduction alone needs
    // no pressure solve. This is a new integration control, not a gate edit.
    if (scene.name == "mixed_gas_liquid") set_velocity(setup(), scene.state, .01, 0);
    const double seconds = scene.name == "empty_cells" ? .002 : .02;
    const auto source = owner(scene); const auto held = source.checkpoint();
    const auto first = advance(source, 1, seconds);
    const auto report = last_mac_production_report();
    check(report.multigrid_solves > 0 && report.multigrid_cycles >= report.multigrid_solves &&
        report.multigrid_levels >= 2 && report.multigrid_fallbacks == 0 && !report.reference_fallback,
        "Production did not run geometric multigrid");
    const auto ref = source.advanced(seconds);
    const auto error = compare(ref.world, first.world, scene.disturbance_pa);
    check(error.temperature_k <= .1 && error.pressure_relative <= .01 && error.pressure_over_disturbance <= .01 &&
        error.oxygen_relative <= .01 && error.constituent_relative <= .01,
        "Production multigrid changed the resolved physics");
    const auto bytes = first.world.checkpoint();
    const auto threaded = advance(source, 12, seconds);
    check(threaded.world.checkpoint() == bytes, "Multigrid result depends on worker count");
    const auto continued = advance(first.world, 12, seconds).world.checkpoint();
    const auto cold = std::async(std::launch::async, [bytes, seconds] {
        const auto& s = setup();
        return advance(MacClosedWorld::from_checkpoint(s.registry, s.eos, bytes), 4, seconds).world.checkpoint();
    }).get();
    check(continued == cold, "Multigrid continuation depends on cached coefficients or topology");
    check(source.checkpoint() == held, "Multigrid mutated its source");
    bool rejected = false;
    try { static_cast<void>(advance_mac_production(source, seconds, 1,
        MacKernelOptions{1, 1, MacKernelPreconditioner::multigrid})); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && source.checkpoint() == held && advance(source, 1, seconds).world.checkpoint() == bytes,
        "Rejected multigrid advance changed continuation");
    std::cout << "PASS " << scene.name << " solves=" << report.multigrid_solves << " cycles=" << report.multigrid_cycles
        << " levels=" << report.multigrid_levels << " dT_K=" << error.temperature_k
        << " dp_over_disturbance=" << error.pressure_over_disturbance
        << " dO2pp_rel=" << error.oxygen_relative << " dconstituent_rel=" << error.constituent_relative << '\n';
}
}
int main() {
    try {
        for (auto scene : {wave(19, 17), room(24, 16), liquid(20, 16), mixed(20, 16),
            hydrostatic(17, 19), conduction(19, 17), viscous_wave(19, 17), vacuum(32, 8), wave(193, 173)}) run(std::move(scene));
        // Same geometry and different coefficients on concurrent callers. Each
        // caller owns its workspace; the fixed-block pool serializes its jobs.
        const auto source = owner(wave(193, 173));
        const std::array<MacCellHeat, 1> heat{{{0, .01, 1e6}}};
        const auto hot = source.with_cell_heat(heat);
        const auto expected = advance(hot, 12, .02).world.checkpoint();
        auto other = std::async(std::launch::async, [&] { return advance(hot, 12, .02).world.checkpoint(); });
        static_cast<void>(advance(source, 12, .02));
        check(other.get() == expected, "Concurrent multigrid branches share coefficients");
        std::cout << "PASS concurrent production multigrid branches\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
