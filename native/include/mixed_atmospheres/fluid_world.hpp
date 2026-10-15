#pragma once
#include "grid.hpp"
#include <cstdint>
#include <vector>

namespace ma {
struct FluidCell : GasCell {
    double liquid_pressure_pa = 101325;
    explicit FluidCell(std::size_t count = 0) : GasCell(count) {}
};

struct FluidSolverSettings {
    double gravity = 9.81;
    double diffusivity_m2_per_s = 2e-5;
    double viscosity_m2_per_s = 1.5e-5;
    double maximum_timestep_s = 0.1;
    double maximum_outgoing_fraction = 0.45;
    bool conduction = true;
    void validate() const;
};

struct FluidStepResult {
    double seconds = 0;
    std::uint64_t substeps = 0;
    std::uint64_t rejected_substeps = 0;
    std::size_t pressure_iterations = 0;
    std::size_t nonlinear_iterations = 0;
    std::size_t thermal_iterations = 0;
};

// Experimental, CLOSED-domain Lagrange/remap gas/liquid world. Nonlinear
// pressure work advances moving material volumes and staggered face velocities;
// a geometrically conservative remap returns every constituent, momentum and
// total energy to the fixed mesh. Pressure/gravity cancellation is evaluated
// before CG. Liquid volumes remain bounded without cutting or deleting mass.
//
// Supported: ideal-gas mixtures, common-temperature miscible liquid inventory,
// completely filled incompressible cells, gravity, gas diffusion, viscosity
// and implicit conduction. Every non-wall cell must contain gas or be filled
// with liquid. This integration benchmark is NOT the production runtime:
// vacuum/free surfaces, immiscible interface reconstruction, phase transitions,
// dissolved gases, boundary fluxes, persistence and game adapters remain work.
// GasWorld and its existing ABI remain the separate explicit reference path.
class FluidWorld {
public:
    FluidWorld(std::size_t width, std::size_t height, double cell_size = 1,
               double depth = 1, FluidSolverSettings settings = {},
               SpeciesRegistry registry = SpeciesRegistry::standard());
    [[nodiscard]] const SpeciesRegistry& registry() const { return registry_; }
    [[nodiscard]] const FluidCell& cell(std::size_t x, std::size_t y) const;
    [[nodiscard]] double pressure(std::size_t x, std::size_t y) const;
    [[nodiscard]] double liquid_fraction(std::size_t x, std::size_t y) const;
    void set_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
                  double velocity_x = 0, double velocity_y = 0,
                  double incompressible_pressure_pa = 101325);
    void set_wall(std::size_t x, std::size_t y, bool wall);
    [[nodiscard]] FluidStepResult step(double seconds, std::uint64_t attempt_budget = 100000);
    [[nodiscard]] ConservationTotals totals() const;
    [[nodiscard]] double time() const { return time_; }
    [[nodiscard]] double volume() const { return dx_ * dx_ * depth_; }
    [[nodiscard]] std::size_t width() const { return width_; }
    [[nodiscard]] std::size_t height() const { return height_; }
private:
    struct Face { std::size_t left, right; int axis; };
    struct Candidate {
        std::vector<FluidCell> cells;
        std::size_t pressure_iterations = 0, nonlinear_iterations = 0, thermal_iterations = 0;
    };
    SpeciesRegistry registry_;
    std::size_t width_, height_;
    double dx_, depth_, time_ = 0;
    FluidSolverSettings settings_;
    std::vector<FluidCell> cells_;
    [[nodiscard]] std::size_t offset(std::size_t x, std::size_t y) const;
    void validate_cell(const FluidCell& cell, bool require_filled) const;
    [[nodiscard]] Candidate advance(double dt) const;
};
}
