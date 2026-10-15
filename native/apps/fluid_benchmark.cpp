#include "mixed_atmospheres/fluid_world.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Sample {
    double milliseconds = 0, mass_drift = 0, energy_drift = 0;
    ma::FluidStepResult step;
};
void print_sample(const Sample& sample) {
    const auto& step = sample.step;
    std::cout << "{\"wall_ms\":" << sample.milliseconds << ",\"seconds\":" << step.seconds
              << ",\"substeps\":" << step.substeps << ",\"rejected_substeps\":" << step.rejected_substeps
              << ",\"pressure_iterations\":" << step.pressure_iterations << ",\"nonlinear_iterations\":" << step.nonlinear_iterations
              << ",\"maximum_linear_true_residual\":" << step.maximum_linear_residual_norm
              << ",\"maximum_linear_residual_to_unchanged_tolerance\":" << step.maximum_linear_residual_ratio
              << ",\"maximum_volume_residual_m3\":" << step.maximum_volume_residual_m3
              << ",\"incomplete_cholesky_solves\":" << step.incomplete_cholesky_solves
              << ",\"jacobi_solves\":" << step.jacobi_solves << ",\"preconditioner_fallbacks\":" << step.preconditioner_fallbacks
              << ",\"maximum_relative_constituent_drift\":" << sample.mass_drift
              << ",\"relative_total_energy_drift\":" << sample.energy_drift << '}';
}
}

int main(int argc, char** argv) {
    try {
        const std::size_t width = argc > 1 ? std::stoul(argv[1]) : 400;
        const std::size_t height = argc > 2 ? std::stoul(argv[2]) : 250;
        const std::size_t repetitions = argc > 3 ? std::stoul(argv[3]) : 1;
        const std::string preconditioner = argc > 4 ? argv[4] : "auto";
        if (repetitions == 0 || repetitions > 100) throw std::invalid_argument("Benchmark repetitions must be 1..100");
        ma::FluidSolverSettings settings; settings.gravity = 0;
        settings.conduction = false; settings.diffusivity_m2_per_s = 0; settings.viscosity_m2_per_s = 0;
        if (preconditioner == "jacobi") settings.pressure_preconditioner = ma::PressurePreconditioner::jacobi;
        else if (preconditioner == "ic0") settings.pressure_preconditioner = ma::PressurePreconditioner::incomplete_cholesky;
        else if (preconditioner != "auto") throw std::invalid_argument("Pressure preconditioner must be auto, jacobi or ic0");
        std::vector<Sample> samples;
        samples.reserve(repetitions);
        // Identical initial states in one process: the first solve is cold;
        // later solves measure steady execution of precisely the same case.
        // World construction, initialization and totals stay outside step().
        for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
            ma::FluidWorld w(width, height, 1, 1, settings); const auto& r = w.registry();
            for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
                const double p = 100000 + 20 * std::sin(0.13 * static_cast<double>(x)) * std::cos(0.17 * static_cast<double>(y));
                auto m = ma::make_air(r, 0.9, 300, p); m.add(r, r.index("water"), 0.1 * r[r.index("water")].density, 300);
                m.add(r, r.index("helium"), 1e-28, 300); w.set_cell(x, y, m);
            }
            const auto before = w.totals();
            const auto start = std::chrono::steady_clock::now(); const auto step = w.step(0.1, 10000);
            const auto end = std::chrono::steady_clock::now(); const auto after = w.totals();
            double maximum_mass_drift = 0;
            for (std::size_t s = 0; s < r.size(); ++s) if (before.masses[s] != 0)
                maximum_mass_drift = std::max(maximum_mass_drift, std::abs(after.masses[s] - before.masses[s]) / before.masses[s]);
            samples.push_back({std::chrono::duration<double, std::milli>(end - start).count(), maximum_mass_drift,
                std::abs(after.energy() - before.energy()) / std::abs(before.energy()), step});
        }
        std::vector<double> steady;
        for (std::size_t i = samples.size() > 1 ? 1 : 0; i < samples.size(); ++i) steady.push_back(samples[i].milliseconds);
        std::sort(steady.begin(), steady.end());
        const double median = steady.size() % 2 ? steady[steady.size() / 2] :
            0.5 * (steady[steady.size() / 2 - 1] + steady[steady.size() / 2]);
        bool target = width * height == 100000;
        for (const auto& sample : samples) target = target && sample.milliseconds <= 25;
        const auto& first = samples.front();
        std::cout << std::setprecision(12) << "{\"cells\":" << width * height << ",\"seconds\":" << first.step.seconds
                  << ",\"wall_ms\":" << first.milliseconds << ",\"cold_wall_ms\":" << first.milliseconds
                  << ",\"steady_median_wall_ms\":" << median << ",\"repetitions\":" << repetitions
                  << ",\"preconditioner\":\"" << preconditioner << "\",\"case\":\"closed-wet-air-20Pa-0.1s-identical-initial-state\""
                  << ",\"substeps\":" << first.step.substeps << ",\"rejected_substeps\":" << first.step.rejected_substeps
                  << ",\"pressure_iterations\":" << first.step.pressure_iterations << ",\"nonlinear_iterations\":" << first.step.nonlinear_iterations
                  << ",\"maximum_relative_constituent_drift\":" << first.mass_drift
                  << ",\"relative_total_energy_drift\":" << first.energy_drift
                  << ",\"100k_25ms_target_met_on_this_host\":" << (target ? "true" : "false") << ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i) { if (i) std::cout << ','; print_sample(samples[i]); }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
