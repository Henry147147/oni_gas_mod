// Production kernel against the reference oracle at equal timestep schedule
// (native/MAC_KERNEL.md, "Validation"). Every scene advances the same owner
// with MacExecutionPolicy::reference and with the production kernel and
// reports every completion-plan metric; tolerances: 0.1 K, and 1% for
// pressure (of the disturbance amplitude for pressure waves), O2 partial
// pressure and every transported constituent including the helium trace.
#include "mac_kernel_fixture.hpp"
#include <cstdlib>
#include <iomanip>

using namespace ma::testing;
using namespace ma::testing::kernel;

int main(int argc, char** argv) {
    try {
        unsigned threads = 1; std::string only;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--threads" && i + 1 < argc) threads = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (a == "--scene" && i + 1 < argc) only = argv[++i];
            else throw std::invalid_argument("Usage: ma_mac_kernel_validation_tests [--threads N] [--scene name]");
        }
        std::vector<Scene> scenes;
        scenes.push_back(wave(16, 16));
        scenes.push_back(wave(32, 32));
        scenes.push_back(uniform_translation(16, 16, 1));
        scenes.push_back(hydrostatic(8, 16));
        scenes.push_back(conduction(16, 16));
        scenes.push_back(mixed(12, 12));
        scenes.push_back(liquid(8, 8));
        scenes.push_back(viscous_wave(16, 16));
        scenes.push_back(room(24, 16));
        scenes.push_back(vacuum(16, 8));
        const auto& s = setup();
        MacKernelOptions options; options.threads = threads;
        bool all = true;
        std::cout << std::setprecision(6);
        for (auto& sc : scenes) {
            if (!only.empty() && sc.name != only) continue;
            MacClosedWorld reference(s.registry, s.eos, sc.state, sc.settings), production = reference;
            double tr = 0, tp = 0; std::size_t sr = 0, sp = 0, newton = 0, linear = 0;
            bool failed = false; std::string error;
            for (std::size_t n = 0; n < sc.intervals; ++n) {
                tr += milliseconds([&] { auto r = reference.advanced(.1); sr += r.report.substeps; reference = std::move(r.world); });
                try {
                    tp += milliseconds([&] {
                        auto r = advance_mac_production(production, .1, 1024, options);
                        sp += r.report.substeps; newton += r.report.nonlinear_iterations; linear += r.report.pressure_iterations;
                        production = std::move(r.world);
                    });
                } catch (const std::exception& e) { failed = true; error = e.what(); break; }
            }
            if (failed) { all = false; std::cout << "SCENE " << sc.name << " PRODUCTION_FAILED " << error << '\n'; continue; }
            const auto m = compare(reference, production, sc.disturbance_pa);
            const double pressure_metric = sc.disturbance_pa > 0 ? m.pressure_over_disturbance : m.pressure_relative;
            const bool pass = m.temperature_k <= .1 && pressure_metric <= .01 && m.oxygen_relative <= .01 && m.constituent_relative <= .01;
            all = all && pass;
            const auto& k = last_mac_production_report();
            std::cout << "SCENE " << sc.name << " cells=" << sc.state.cells.size() << " intervals=" << sc.intervals
                << " ref_substeps=" << sr << " prod_substeps=" << sp
                << " dT_K=" << m.temperature_k << " dp_rel=" << m.pressure_relative << " dp_over_disturbance=" << m.pressure_over_disturbance
                << " dO2pp_rel=" << m.oxygen_relative << " dconstituent_rel=" << m.constituent_relative
                << " drho_rel=" << m.density_relative << " du_over_umax=" << m.velocity_over_maximum
                << " dE_total_rel=" << m.energy_relative << " dM_total_rel=" << m.mass_relative
                << " newton=" << newton << " linear=" << linear << " ref_ms=" << tr << " prod_ms=" << tp
                << " audit_energy=" << k.energy_residual_j << "/" << k.energy_bound_j
                << " closure=" << k.maximum_volume_closure << " entropy=" << k.entropy_change_j_per_k << "/" << -k.entropy_allowance_j_per_k
                << (pass ? " PASS" : " FAIL") << '\n';
        }
        std::cout << (all ? "ALL PASS" : "SOME FAILED") << '\n';
        return all ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
