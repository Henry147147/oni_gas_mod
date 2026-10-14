#include "mixed_atmospheres/grid.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        const bool benchmark = argc > 1 && std::string(argv[1]) == "--benchmark";
        const std::size_t width = benchmark ? 400 : 32, height = benchmark ? 250 : 16;
        ma::GasWorld world(width, height, 1, 1, {0, 2e-5, true, false});
        const auto& r = world.registry();
        for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x)
            world.set_cell(x, y, ma::make_air(r, 1, 293.15, x < width / 2 ? 110000 : 90000, x < width / 2 ? 0.30 : 0.15));
        const auto before = world.totals();
        const auto start = std::chrono::steady_clock::now();
        const auto result = world.step(benchmark ? 0.001 : 0.1);
        const double wall_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto after = world.totals();
        double mass_error = 0;
        for (std::size_t i = 0; i < before.masses.size(); ++i) mass_error += std::abs(after.masses[i] - before.masses[i]);
        std::cout << std::setprecision(12)
                  << "{\"solver\":\"explicit_gas_reference\",\"cells\":" << width * height
                  << ",\"simulated_seconds\":" << result.seconds << ",\"substeps\":" << result.substeps
                  << ",\"wall_ms\":" << wall_ms << ",\"mass_error_kg\":" << mass_error
                  << ",\"energy_error_j\":" << after.energy() - before.energy() << "}\n";
        if (!benchmark && argc > 1) {
            std::ofstream file(argv[1]);
            if (!file) throw std::runtime_error("Cannot open CSV output");
            file << "x,y,temperature_k,pressure_pa,oxygen_mole_fraction,total_grams,velocity_x,velocity_y\n";
            for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
                const auto& c = world.cell(x, y); const auto atmosphere = ma::assess_atmosphere(r, c.mixture, 1);
                file << x << ',' << y << ',' << c.mixture.temperature(r) << ',' << atmosphere.pressure << ','
                     << atmosphere.oxygen_fraction << ',' << c.mixture.mass() * 1000 << ','
                     << c.momentum_x / c.mixture.mass() << ',' << c.momentum_y / c.mixture.mass() << '\n';
            }
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
