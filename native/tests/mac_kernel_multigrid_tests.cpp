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
        MacKernelOptions{})); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && source.checkpoint() == held && advance(source, 1, seconds).world.checkpoint() == bytes,
        "Rejected multigrid advance changed continuation");
    std::cout << "PASS " << scene.name << " solves=" << report.multigrid_solves << " cycles=" << report.multigrid_cycles
        << " levels=" << report.multigrid_levels << " dT_K=" << error.temperature_k
        << " dp_over_disturbance=" << error.pressure_over_disturbance
        << " dO2pp_rel=" << error.oxygen_relative << " dconstituent_rel=" << error.constituent_relative << '\n';
}
void component_control() {
    const auto& s = setup(); auto scene = room(48, 32);
    for (std::size_t i = 0; i < scene.state.cells.size(); ++i) {
        const auto x = i % 48;
        scene.state.grid.walls[i] = x == 16 || x == 32 ? 1 : 0;
        if (scene.state.grid.walls[i]) scene.state.cells[i] = {MixtureInventory(s.registry.size()), {}};
        else scene.state.cells[i].mixture = s.mixture(1, 100000 + 100 * std::cos(static_cast<double>(i)),
            300 + static_cast<double>(x % 3), .2);
    }
    reset_passives(s, scene.state);
    const auto geometric = advance(owner(scene), 1, .02);
    scene.settings.preconditioner = PressurePreconditioner::incomplete_cholesky;
    const auto source = owner(scene); const auto held = source.checkpoint();
    const auto first = advance(source, 1, .02); const auto report = last_mac_production_report();
    const auto error = compare(geometric.world, first.world, 100);
    check(report.maximum_linear_components == 3 && report.incomplete_cholesky_solves > 0 &&
        report.incomplete_cholesky_fallbacks == 0 && !report.reference_fallback && report.multigrid_solves == 0,
        "Independent components did not use IC without fallback");
    check(error.temperature_k <= .1 && error.pressure_relative <= .01 && error.constituent_relative <= .01 &&
        std::abs(first.report.energy_residual_j) <= first.report.energy_bound_j,
        "Independent component solve changed resolved physics or energy admission");
    const auto bytes = first.world.checkpoint();
    check(advance(source, 12, .02).world.checkpoint() == bytes, "Component solve depends on worker count");
    const auto continued = advance(first.world, 12, .02).world.checkpoint();
    const auto cold = std::async(std::launch::async, [bytes] {
        const auto& model = setup();
        return advance(MacClosedWorld::from_checkpoint(model.registry, model.eos, bytes), 1, .02).world.checkpoint();
    }).get();
    check(continued == cold, "Component continuation depends on cached state");
    bool rejected = false;
    try { static_cast<void>(advance_mac_production(source, .02, 1, {})); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && source.checkpoint() == held, "Component budget failure mutated its source");
    std::cout << "PASS independent IC components: physics, ledgers, workers, continuation and rollback\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--components-only") { component_control(); return 0; }
        check(argc == 1, "Usage: ma_mac_kernel_multigrid_tests [--components-only]");
        component_control();
        // Weak conduction needs no hierarchy. Pure liquid's pressure is
        // independent of temperature, so this isolates the heat solve.
        {
            const auto& s = setup(); auto scene = liquid(32, 16);
            scene.settings.gravity_m_per_s2 = 0; scene.settings.thermal_conduction = true;
            for (std::size_t i = 0; i < scene.state.cells.size(); ++i)
                scene.state.cells[i].mixture = s.mixture(1, 200000, i % 32 < 16 ? 330 : 300, 1);
            const auto automatic = advance(owner(scene), 1, .02);
            const auto report = last_mac_production_report();
            check(report.physical.conduction_iterations > 0 && report.multigrid_solves == 0 &&
                report.topology_builds == 0 && !report.reference_fallback,
                "Weak conduction unnecessarily prepared multigrid");
            scene.settings.preconditioner = PressurePreconditioner::jacobi;
            const auto jacobi = advance(owner(scene), 1, .02);
            const auto error = compare(automatic.world, jacobi.world, 0);
            check(error.temperature_k == 0 && error.energy_relative == 0,
                "Automatic weak conduction differs from explicit Jacobi");
            check(std::abs(automatic.report.energy_residual_j) <= automatic.report.energy_bound_j,
                "Weak conduction energy ledger did not close");
            std::cout << "PASS weak conduction uses Jacobi without a hierarchy\n";
        }
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
