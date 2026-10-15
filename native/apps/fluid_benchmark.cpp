#include "mixed_atmospheres/fluid_world.hpp"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        const std::size_t width = argc > 1 ? std::stoul(argv[1]) : 400;
        const std::size_t height = argc > 2 ? std::stoul(argv[2]) : 250;
        ma::FluidSolverSettings settings; settings.gravity = 0;
        settings.conduction = false; settings.diffusivity_m2_per_s = 0; settings.viscosity_m2_per_s = 0;
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
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << std::setprecision(12) << "{\"cells\":" << width * height << ",\"seconds\":" << step.seconds
                  << ",\"wall_ms\":" << ms << ",\"substeps\":" << step.substeps << ",\"rejected_substeps\":" << step.rejected_substeps
                  << ",\"pressure_iterations\":" << step.pressure_iterations << ",\"nonlinear_iterations\":" << step.nonlinear_iterations
                  << ",\"maximum_relative_constituent_drift\":" << maximum_mass_drift
                  << ",\"relative_total_energy_drift\":" << std::abs(after.energy() - before.energy()) / std::abs(before.energy())
                  << ",\"100k_25ms_target_met_on_this_host\":" << (width * height == 100000 && ms <= 25 ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
