#pragma once
#include "grid.hpp"
#include "pressure.hpp"
#include "fluid_thermodynamics.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ma {
struct FluidCell : GasCell {
    double liquid_pressure_pa = 101325;
    // Local axial coordinates, used only by the explicit 1-D geometry mode.
    // Vacuum has begin=end=0; occupied cells have 0<=begin<end<=cell_size.
    double occupied_begin_m = 0, occupied_end_m = 0;
    explicit FluidCell(std::size_t count = 0) : GasCell(count) {}
};

enum class FluidAxis { x, y };
struct FluidGeometry1D {
    FluidAxis axis = FluidAxis::y;
    std::shared_ptr<const FluidThermodynamics> thermodynamics;
    LiquidFillRoundoffPolicy liquid_fill_roundoff = LiquidFillRoundoffPolicy::strict_eos;
};

struct FluidSolverSettings {
    double gravity = 9.81;
    double diffusivity_m2_per_s = 2e-5;
    double viscosity_m2_per_s = 1.5e-5;
    double maximum_timestep_s = 0.1;
    double maximum_outgoing_fraction = 0.45;
    bool conduction = true;
    PressurePreconditioner pressure_preconditioner = PressurePreconditioner::automatic;
    void validate() const;
};

struct FluidStepResult {
    double seconds = 0;
    std::uint64_t substeps = 0;
    std::uint64_t rejected_substeps = 0;
    std::size_t pressure_iterations = 0;
    std::size_t nonlinear_iterations = 0;
    std::size_t thermal_iterations = 0;
    double maximum_linear_residual_norm = 0, maximum_linear_residual_ratio = 0, maximum_volume_residual_m3 = 0;
    std::size_t incomplete_cholesky_solves = 0, jacobi_solves = 0, preconditioner_fallbacks = 0;
    // Occupied mode retains canonical cell momentum: initial heat is zero;
    // final heat records only unresolved pressure/body increment variance.
    double initial_projection_heat_j = 0, final_projection_heat_j = 0;
    double projection_heat_roundoff_j = 0;
    double wall_reaction_impulse_kg_m_per_s = 0, body_impulse_kg_m_per_s = 0;
    double maximum_conservation_error_j = 0, pressure_work_roundoff_j = 0;
    std::size_t unresolved_energy_updates = 0, unresolved_volume_updates = 0, unresolved_position_updates = 0;
    double maximum_volume_roundoff_m3 = 0, maximum_position_roundoff_m = 0;
    double maximum_volume_addition_roundoff_m3 = 0, maximum_root_arithmetic_roundoff_m3 = 0;
    double maximum_endpoint_relative_volume_residual = 0;
    double signed_geometry_volume_residual_m3 = 0, absolute_geometry_volume_residual_m3 = 0;
    std::size_t zero_work_substitutions = 0;
    double maximum_omitted_pressure_pa = 0, omitted_elastic_work_bound_j = 0;
    double cumulative_omitted_elastic_work_bound_j = 0;
};

// Experimental, CLOSED-domain Lagrange/remap gas/liquid world. Nonlinear
// pressure work advances moving material volumes and staggered face velocities;
// a geometrically conservative remap returns every constituent, momentum and
// total energy to the fixed mesh. Pressure/gravity cancellation is evaluated
// before CG. Liquid volumes remain bounded without cutting or deleting mass.
//
// Default closed mode: ideal-gas mixtures, common-temperature miscible liquid inventory,
// completely filled incompressible cells, gravity, gas diffusion, viscosity
// and implicit conduction. Every non-wall cell must contain gas or be filled
// with liquid. This integration benchmark is NOT the production runtime:
// vacuum/free surfaces, immiscible interface reconstruction, phase transitions,
// dissolved gases, boundary fluxes, persistence and game adapters remain work.
// GasWorld and its existing ABI remain the separate explicit reference path.
// The explicit FluidGeometry1D overload instead uses occupied intervals and a
// versioned elastic EOS. Its bounded liquid/free-surface validation and opt-in
// representability approximation are documented in native/FREE_SURFACES.md.
// Gas/vacuum rarefaction convergence and 2-D interfaces are not validated here.
class FluidWorld {
public:
    FluidWorld(std::size_t width, std::size_t height, double cell_size = 1,
               double depth = 1, FluidSolverSettings settings = {},
               SpeciesRegistry registry = SpeciesRegistry::standard());
    FluidWorld(std::size_t width, std::size_t height, double cell_size, double depth,
               FluidSolverSettings settings, SpeciesRegistry registry, FluidGeometry1D geometry);
    [[nodiscard]] const SpeciesRegistry& registry() const { return registry_; }
    [[nodiscard]] const FluidCell& cell(std::size_t x, std::size_t y) const;
    [[nodiscard]] double pressure(std::size_t x, std::size_t y) const;
    [[nodiscard]] double liquid_fraction(std::size_t x, std::size_t y) const;
    [[nodiscard]] double occupied_volume(std::size_t x, std::size_t y) const;
    [[nodiscard]] experimental::ReferenceEquilibriumState thermodynamic_state(std::size_t x, std::size_t y) const;
    void set_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
                  double velocity_x = 0, double velocity_y = 0,
                  double incompressible_pressure_pa = 101325);
    // Conserved initialization/import: do not round momentum through P/M*M.
    void set_conserved_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
                            double momentum_x, double momentum_y,
                            double incompressible_pressure_pa = 101325);
    void set_occupied_cell(std::size_t x, std::size_t y, const MixtureInventory& mixture,
                           double begin_m, double end_m, double axial_velocity_m_per_s = 0);
    void set_wall(std::size_t x, std::size_t y, bool wall);
    [[nodiscard]] FluidStepResult step(double seconds, std::uint64_t attempt_budget = 100000);
    [[nodiscard]] ConservationTotals totals() const;
    [[nodiscard]] double time() const { return time_; }
    [[nodiscard]] double cumulative_omitted_elastic_work_bound() const { return cumulative_omitted_elastic_work_bound_j_; }
    [[nodiscard]] double volume() const { return dx_ * dx_ * depth_; }
    [[nodiscard]] std::size_t width() const { return width_; }
    [[nodiscard]] std::size_t height() const { return height_; }
private:
    struct Face { std::size_t left, right; int axis; };
    struct Candidate {
        std::vector<FluidCell> cells;
        std::size_t pressure_iterations = 0, nonlinear_iterations = 0, thermal_iterations = 0;
        double maximum_linear_residual_norm = 0, maximum_linear_residual_ratio = 0, maximum_volume_residual_m3 = 0;
        std::size_t incomplete_cholesky_solves = 0, jacobi_solves = 0, preconditioner_fallbacks = 0;
        double initial_projection_heat_j = 0, final_projection_heat_j = 0;
        double projection_heat_roundoff_j = 0;
        double wall_reaction_impulse_kg_m_per_s = 0, body_impulse_kg_m_per_s = 0;
        double maximum_conservation_error_j = 0, pressure_work_roundoff_j = 0;
        std::size_t unresolved_energy_updates = 0, unresolved_volume_updates = 0, unresolved_position_updates = 0;
        double maximum_volume_roundoff_m3 = 0, maximum_position_roundoff_m = 0;
        double maximum_volume_addition_roundoff_m3 = 0, maximum_root_arithmetic_roundoff_m3 = 0;
        double maximum_endpoint_relative_volume_residual = 0;
        double signed_geometry_volume_residual_m3 = 0, absolute_geometry_volume_residual_m3 = 0;
        std::size_t zero_work_substitutions = 0;
        double maximum_omitted_pressure_pa = 0, omitted_elastic_work_bound_j = 0;
    };
    SpeciesRegistry registry_;
    std::size_t width_, height_;
    double dx_, depth_, time_ = 0;
    double cumulative_omitted_elastic_work_bound_j_ = 0;
    FluidSolverSettings settings_;
    std::optional<FluidGeometry1D> geometry_;
    std::vector<FluidCell> cells_;
    [[nodiscard]] std::size_t offset(std::size_t x, std::size_t y) const;
    void validate_cell(const FluidCell& cell, bool require_filled) const;
    void validate_occupied_geometry() const;
    [[nodiscard]] Candidate advance(double dt) const;
    [[nodiscard]] Candidate advance_occupied(double dt) const;
};
}
