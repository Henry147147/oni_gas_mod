#pragma once
#include "mac_thermal.hpp"
#include "mac_shock_heat.hpp"
#include "mac_reconstruction.hpp"
#include "mac_compression.hpp"
#include "volume_work.hpp"
#include <memory>
#include <optional>

namespace ma::experimental {
enum class MacMaterialReconstruction { donor_cell, limited_linear };
enum class MacCompressionViscosity { none, quadratic, acoustic_quadratic };
// Persisted values: 0 disabled, 1 closing-speed, 2 matched Cp/max-speed,
// 3 matched Cp with two half-cell resistances. Older values keep their laws.
enum class MacShockHeatRegularization {
    none = 0, compression_activated = 1, pressure_sensed_acoustic = 2, pressure_sensed_series = 3
};
struct MacClosedSettings {
    // Physical setting additions require updating the checkpoint schema and
    // its complete-identity/continuation controls in mac_checkpoint.cpp.
    double maximum_timestep_s = .01;
    double maximum_outgoing_volume_fraction = .4;
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic;
    double gravity_m_per_s2 = 0; // downward, constant; vertical periodicity is unsupported
    MacMaterialReconstruction reconstruction = MacMaterialReconstruction::limited_linear;
    // The owning interval applies compression half steps around each
    // pressure/transport candidate. The primitive substep stays separate.
    MacCompressionViscosity compression_viscosity = MacCompressionViscosity::acoustic_quadratic;
    MacMomentumReconstruction momentum_reconstruction = MacMomentumReconstruction::limited_linear;
    bool thermal_conduction = false;
    MacShockHeatRegularization shock_heat = MacShockHeatRegularization::pressure_sensed_acoustic;
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
    std::size_t momentum_correction_events = 0, limited_momentum_corrections = 0;
    std::size_t unresolved_momentum_corrections = 0, unresolved_remainder_corrections = 0;
    std::size_t conduction_stages = 0, conduction_contacts = 0, conduction_iterations = 0, unresolved_conduction_updates = 0;
    MacEnergy conductive_redistribution_j;
    double conductive_entropy_j_per_k = 0;
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
    std::size_t shock_heat_stages = 0, shock_heat_events = 0, shock_heat_root_iterations = 0;
    std::size_t rejected_shock_heat_trials = 0, unresolved_shock_heat_updates = 0;
    MacEnergy absolute_shock_heat_transfer_wide_j;
    double absolute_shock_heat_transfer_j = 0;
};
struct MacClosedAdvance;
struct MacFluidTransferResult;
struct MacFluidTransfer {
    std::size_t cell_index, species_index;
    bool inject;
    double mass_kg, temperature_k;
};
struct MacFluidParcel {
    std::size_t cell_index, species_index;
    bool injected;
    double mass_kg, temperature_k, internal_energy_j;
    MacEnergy resolved_kinetic_energy, potential_energy;
    MacScaled momentum_x, momentum_y;
};
using MacGasTransfer = MacFluidTransfer;
using MacGasParcel = MacFluidParcel;
using MacGasTransferResult = MacFluidTransferResult;
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
    // Declared point sources, not an aperture/pump flow model. Injection is at
    // rest; extraction carries the selected species at the local face speeds.
    // Filled gas cells without passive metadata only; unsupported void/liquid
    // topology rejects atomically. Packets account for all carried mechanics.
    [[nodiscard]] MacGasTransferResult with_gas_transfers(std::span<const MacGasTransfer>) const;
    // Also admits a filled, single-liquid cell of the requested material.
    // The declared parcel is at the receiving cell's pre-edit pressure;
    // extraction carries local specific internal energy, including elastic U.
    // This is a point source contract, with no implicit pump/enthalpy work.
    // Gas/liquid mixing, void creation and passive metadata reject atomically.
    [[nodiscard]] MacFluidTransferResult with_fluid_transfers(std::span<const MacFluidTransfer>) const;
    // Physical owner checkpoint, not a scalar/display projection. Restoring
    // requires the exact registry/EOS identity and validates the complete image.
    [[nodiscard]] std::vector<std::uint8_t> checkpoint() const;
    [[nodiscard]] static MacClosedWorld from_checkpoint(SpeciesRegistry,
        std::shared_ptr<const FluidThermodynamics>,std::span<const std::uint8_t>);
private:
    SpeciesRegistry registry_;
    std::shared_ptr<const FluidThermodynamics> thermodynamics_;
    MacTransportState state_;
    MacClosedSettings settings_;
    double time_s_ = 0;
    std::uint64_t revision_ = 1;
};
struct MacClosedAdvance { MacClosedWorld world; MacClosedAdvanceReport report; };
struct MacFluidTransferResult {
    MacClosedWorld world;
    std::vector<MacFluidParcel> parcels;
    std::size_t unresolved_heat_updates = 0;
};
}
