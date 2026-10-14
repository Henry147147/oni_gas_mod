#pragma once
#include "atmosphere.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace ma {
struct GasCell {
    MixtureInventory mixture;
    double momentum_x = 0;
    double momentum_y = 0;
    bool wall = false;
    explicit GasCell(std::size_t count = 0) : mixture(count) {}
    [[nodiscard]] double kinetic_energy() const;
};

struct SolverSettings {
    double gravity = 9.81;
    double diffusivity = 2e-5; // m^2/s, mixture-averaged reference approximation
    bool conduction = true;
    bool open_top = false;
    void validate() const;
};
struct StepResult { double seconds = 0; std::uint64_t substeps = 0; };
struct ConservationTotals {
    std::vector<double> masses;
    double internal_energy = 0;
    double kinetic_energy = 0;
    double potential_energy = 0;
    [[nodiscard]] double energy() const { return internal_energy + kinetic_energy + potential_energy; }
};

// Conservative gas-only reference solver. Explicit acoustic CFL + HLLC fluxes
// are deliberately exposed as a validation baseline, not the release all-Mach
// multiphase implementation. Liquid/solid mass in fluid cells is rejected.
class GasWorld {
public:
    GasWorld(std::size_t width, std::size_t height, double cell_size = 1,
             double depth = 1, SolverSettings settings = {},
             SpeciesRegistry registry = SpeciesRegistry::standard());
    [[nodiscard]] const SpeciesRegistry& registry() const { return registry_; }
    [[nodiscard]] const GasCell& cell(std::size_t x, std::size_t y) const;
    void set_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
                  double velocity_x = 0, double velocity_y = 0);
    void set_wall(std::size_t x, std::size_t y, bool wall);
    [[nodiscard]] StepResult step(double seconds, std::uint64_t substep_budget = 100000);
    [[nodiscard]] ConservationTotals totals() const;
    [[nodiscard]] const std::vector<double>& escaped_masses() const { return escaped_masses_; }
    [[nodiscard]] double escaped_energy() const { return escaped_energy_; }
    [[nodiscard]] double time() const { return time_; }
    [[nodiscard]] std::size_t width() const { return width_; }
    [[nodiscard]] std::size_t height() const { return height_; }
    [[nodiscard]] double cell_size() const { return dx_; }
    [[nodiscard]] double depth() const { return depth_; }
    [[nodiscard]] double volume() const { return dx_ * dx_ * depth_; }
    [[nodiscard]] const SolverSettings& settings() const { return settings_; }
    [[nodiscard]] std::vector<std::uint8_t> save() const;
    [[nodiscard]] static GasWorld load(std::span<const std::uint8_t> bytes,
                                     SpeciesRegistry registry = SpeciesRegistry::standard());
private:
    SpeciesRegistry registry_;
    std::size_t width_, height_;
    double dx_, depth_;
    SolverSettings settings_;
    std::vector<GasCell> cells_;
    std::vector<double> escaped_masses_;
    double escaped_energy_ = 0;
    double time_ = 0;
    [[nodiscard]] std::size_t offset(std::size_t x, std::size_t y) const;
    [[nodiscard]] double stable_timestep() const;
    void advance(double dt);
};
}
