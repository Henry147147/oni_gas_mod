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

namespace {
void report_check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void close_report(double actual, double expected, double allowance, const char* why) {
    report_check(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= .01 * (std::abs(actual) + std::abs(expected)) + allowance, why);
}
Scene diagnostic_shear(std::size_t w, std::size_t h) {
    auto scene = viscous_wave(w, h);
    const auto inertia = mac_face_inertias(setup().registry, scene.state, *setup().eos);
    for (std::size_t i = 0; i < scene.state.momentum_x.size(); ++i)
        scene.state.momentum_x[i] = inertia.x[i] * (i / scene.state.grid.x_width() % 2 == 0 ? .5 : -.5);
    return scene;
}
void same_report(const MacKernelPhysicalReport& a, const MacKernelPhysicalReport& b) {
    report_check(a.accepted_substeps == b.accepted_substeps && a.conduction_stages == b.conduction_stages && a.conduction_contacts == b.conduction_contacts &&
        a.conduction_iterations == b.conduction_iterations && a.unresolved_conduction_updates == b.unresolved_conduction_updates &&
        a.viscosity_stages == b.viscosity_stages && a.viscosity_modes == b.viscosity_modes &&
        a.unresolved_viscous_heat_updates == b.unresolved_viscous_heat_updates &&
        a.unresolved_viscous_momentum_updates == b.unresolved_viscous_momentum_updates &&
        a.conductive_entropy_j_per_k == b.conductive_entropy_j_per_k &&
        a.viscous_entropy_lower_bound_j_per_k == b.viscous_entropy_lower_bound_j_per_k,
        "Physical report counters or entropy depend on workers/cache/rejected work");
    const std::array left{a.conductive_redistribution_j, a.viscous_heat, a.viscous_stored_heat,
        a.viscous_wall_impulse_x, a.viscous_wall_impulse_y, a.gravity_internal_change_wide_j};
    const std::array right{b.conductive_redistribution_j, b.viscous_heat, b.viscous_stored_heat,
        b.viscous_wall_impulse_x, b.viscous_wall_impulse_y, b.gravity_internal_change_wide_j};
    for (std::size_t i = 0; i < left.size(); ++i)
        report_check(left[i].mantissa() == right[i].mantissa() && left[i].exponent() == right[i].exponent(),
            "Physical energy/impulse report depends on workers/cache/rejected work");
}
void physical_report_controls() {
    const auto& s = setup(); MacKernelOptions options;
    auto gravity = wave(2, 2); gravity.state = s.state(2, 2, false);
    gravity.settings.gravity_m_per_s2 = 9.81;
    for (std::size_t i = 0; i < 4; ++i) gravity.state.cells[i].mixture = s.mixture(1, 100000, i < 2 ? 300 : 600);
    reset_passives(s, gravity.state);
    for (const auto& scene : {conduction(16, 16), diagnostic_shear(16, 16), gravity}) {
        const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings);
        const auto reference = source.advanced(.002), production = advance_mac_production(source, .002, 64, options);
        const auto& a = production.report; const auto& b = reference.report;
        if (scene.settings.thermal_conduction) {
            report_check(a.conduction_stages == 2 * a.substeps && a.conduction_contacts > 0,
                "Production omitted accepted conduction stages/contacts");
            close_report(a.conductive_redistribution_j.require_representable(), b.conductive_redistribution_j.require_representable(),
                a.energy_bound_j + b.energy_bound_j, "Production conductive redistribution differs from the oracle");
            close_report(a.conductive_entropy_j_per_k, b.conductive_entropy_j_per_k,
                a.entropy_roundoff_j_per_k + b.entropy_roundoff_j_per_k, "Production conductive entropy differs from the oracle");
        }
        if (scene.settings.viscosity.shear_pa_s > 0) {
            report_check(a.viscosity_stages == 2 * a.substeps && a.viscosity_modes > 0 && b.viscous_heat.mantissa() > 0,
                "Production omitted accepted viscosity stages/modes or shear control has no heat");
            close_report(a.viscous_heat.require_representable(), b.viscous_heat.require_representable(),
                a.energy_bound_j + b.energy_bound_j, "Production viscous dissipation differs from the oracle");
            close_report(a.viscous_stored_heat.require_representable(), b.viscous_stored_heat.require_representable(),
                a.energy_bound_j + b.energy_bound_j, "Production stored viscous heat differs from the oracle");
            close_report(a.viscous_entropy_lower_bound_j_per_k, b.viscous_entropy_lower_bound_j_per_k,
                a.entropy_roundoff_j_per_k + b.entropy_roundoff_j_per_k, "Production viscous entropy differs from the oracle");
        }
        if (scene.settings.gravity_m_per_s2 != 0) {
            report_check(std::abs(b.gravity_internal_change_j) > b.energy_bound_j, "Gravity report control lost its signal");
            close_report(a.gravity_internal_change_j, b.gravity_internal_change_j, a.energy_bound_j + b.energy_bound_j,
                "Production omitted or changed gravity reconciliation work");
        }
    }
    for (const auto& scene : {conduction(193, 173), diagnostic_shear(193, 173)}) {
        const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings); const auto held = source.checkpoint();
        options.threads = 1; const auto one = advance_mac_production(source, .002, 64, options);
        const auto report = last_mac_production_report().physical;
        options.threads = 12; const auto twelve = advance_mac_production(source, .002, 64, options);
        report_check(last_mac_production_report().threads_used == 12 && one.world.checkpoint() == twelve.world.checkpoint(),
            "Physical report control did not use twelve workers or changed physical state");
        same_report(report, last_mac_production_report().physical);
        bool failed = false;
        try { static_cast<void>(advance_mac_production(source, .02, 1, options)); } catch (const std::runtime_error&) { failed = true; }
        report_check(failed && source.checkpoint() == held, "Failed diagnostic interval changed its source");
        const auto repeated = advance_mac_production(source, .002, 64, options);
        report_check(repeated.world.checkpoint() == one.world.checkpoint(), "Rejected work changed physical continuation");
        same_report(report, last_mac_production_report().physical);
    }
    auto scene = uniform_translation(16, 16, 10); scene.settings.maximum_timestep_s = .1;
    scene.settings.thermal_conduction = true; scene.settings.viscosity = {.5, .25};
    const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings);
    const auto refined = advance_mac_production(source, .1, 64, options);
    report_check(refined.report.rejected_attempts > 0 && refined.report.conduction_stages == 2 * refined.report.substeps &&
        refined.report.viscosity_stages == 2 * refined.report.substeps,
        "Rejected substeps leaked physical-stage diagnostics into the accepted interval");
}
}

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
        if (only.empty()) physical_report_controls();
        std::cout << (all ? "ALL PASS" : "SOME FAILED") << '\n';
        return all ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
