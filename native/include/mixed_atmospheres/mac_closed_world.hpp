#pragma once
#include "mac_reconstruction.hpp"
#include "mac_compression.hpp"
#include "volume_work.hpp"
#include <memory>
#include <optional>

namespace ma::experimental {
enum class MacMaterialReconstruction { donor_cell, limited_linear };
enum class MacCompressionViscosity { none, quadratic };
struct MacClosedSettings {
    double maximum_timestep_s = .01;
    double maximum_outgoing_volume_fraction = .4;
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic;
    double gravity_m_per_s2 = 0; // downward, constant; vertical periodicity is unsupported
    MacMaterialReconstruction reconstruction = MacMaterialReconstruction::limited_linear;
    // The owning interval applies compression half steps around each
    // pressure/transport candidate. The primitive substep stays separate.
    MacCompressionViscosity compression_viscosity = MacCompressionViscosity::quadratic;
    void validate() const;
};
struct MacClosedSubstep {
    MacTransportState state;
    MacTransportState pressure_state;
    OccupiedMaterialPressureStep pressure;
    std::vector<double> root_input_velocities{}, root_output_velocities{};
    std::vector<MacScaled> full_pressure_velocities{};
    MacMassFluxes volume_fluxes_m3, mass_fluxes_kg;
    MacTransportReport transport;
    std::optional<MacParcelPlan> reconstruction;
    // Pressure cells are compacted in ascending grid-cell order. The two face
    // arrays contain the corresponding compact pressure-node index, or max().
    std::vector<std::size_t> active_cells, pressure_nodes_x, pressure_nodes_y;
    std::vector<double> material_volumes_m3, geometric_residuals_m3;
    double pressure_energy_residual_j = 0, pressure_energy_bound_j = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    MacEnergy pressure_energy_residual_wide_j, pressure_energy_bound_wide_j;
    MacEnergy energy_residual_wide_j, energy_bound_wide_j;
    std::size_t rounded_energy_projections = 0;
    double pressure_wall_impulse_x = 0, pressure_wall_impulse_y = 0;
    MacScaled full_pressure_wall_impulse_x{}, full_pressure_wall_impulse_y{};
    double body_impulse_y = 0;
    std::vector<double> gravity_body_work_y, gravity_potential_flux_y, gravity_internal_work_y;
    std::vector<double> pre_gravity_internal_energy_j, gravity_internal_changes_j;
    double gravity_internal_residual_j = 0, gravity_internal_bound_j = 0;
    double potential_transport_residual_j = 0, potential_transport_bound_j = 0;
    double gravity_internal_change_j = 0, entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    std::size_t unresolved_momentum_updates = 0;
    std::size_t rounded_pressure_velocity_inputs = 0;
    std::size_t unresolved_gravity_internal_updates = 0;
};
// Closed, occupied regular-grid Lagrange/projection split. Work pressure acts
// directly on canonical face J; the same midpoint sweeps define material
// volume and reconstructed material parcels for compatible transport. Gravity work
// is reconciled with actual potential-energy mass flux, with a signed internal
// energy change kept separate from transport mixing/projection heat. No
// diffusion, free surface, phase transition or game dispatch is supplied here.
[[nodiscard]] MacClosedSubstep advance_mac_closed_substep(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, double seconds, const MacClosedSettings& = {});

struct MacClosedAdvanceReport {
    std::size_t substeps = 0, attempts = 0, rejected_attempts = 0;
    std::size_t pressure_iterations = 0, nonlinear_iterations = 0;
    std::size_t unresolved_heat_updates = 0, unresolved_momentum_updates = 0;
    std::size_t unresolved_gravity_internal_updates = 0;
    std::size_t compression_events = 0, unresolved_compression_heat_updates = 0, unresolved_compression_momentum_updates = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    double mixing_heat_j = 0, wall_heat_j = 0;
    double wall_impulse_x = 0, wall_impulse_y = 0;
    MacScaled full_wall_impulse_x{}, full_wall_impulse_y{};
    double maximum_geometric_residual_m3 = 0;
    double gravity_internal_change_j = 0, body_impulse_y = 0;
    double entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    MacEnergy energy_residual_wide_j, energy_bound_wide_j, mixing_heat_wide_j, wall_heat_wide_j;
    MacEnergy compression_heat_wide_j;
    double compression_heat_j = 0;
    std::size_t rounded_energy_projections = 0;
};
struct MacClosedAdvance;
struct MacCellHeat {
    std::size_t cell_index;
    double joules, maximum_temperature_k;
};
// Owns its registry, immutable EOS identity, canonical state and clock. Advance
// creates a new owner; failed later attempts publish neither time nor state.
class MacClosedWorld {
public:
    MacClosedWorld(SpeciesRegistry, std::shared_ptr<const FluidThermodynamics>, MacTransportState, MacClosedSettings = {});
    [[nodiscard]] const MacTransportState& state() const noexcept { return state_; }
    [[nodiscard]] const SpeciesRegistry& registry() const noexcept { return registry_; }
    [[nodiscard]] const FluidThermodynamics& thermodynamics() const noexcept { return *thermodynamics_; }
    [[nodiscard]] const MacClosedSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] double time_s() const noexcept { return time_s_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] MacClosedAdvance advanced(double seconds, std::size_t attempt_budget = 1024) const;
    // Immutable external heat exchange. Retains time, geometry, every mass and
    // canonical face momentum; checks the complete batch before returning it.
    [[nodiscard]] MacClosedWorld with_cell_heat(std::span<const MacCellHeat>) const;
private:
    SpeciesRegistry registry_;
    std::shared_ptr<const FluidThermodynamics> thermodynamics_;
    MacTransportState state_;
    MacClosedSettings settings_;
    double time_s_ = 0;
    std::uint64_t revision_ = 1;
};
struct MacClosedAdvance { MacClosedWorld world; MacClosedAdvanceReport report; };
}
