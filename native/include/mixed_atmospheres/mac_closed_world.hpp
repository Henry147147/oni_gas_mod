#pragma once
#include "mac_transport.hpp"
#include "volume_work.hpp"
#include <memory>

namespace ma::experimental {
struct MacClosedSettings {
    double maximum_timestep_s = .01;
    double maximum_outgoing_volume_fraction = .4;
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic;
    void validate() const;
};
struct MacClosedSubstep {
    MacTransportState state;
    MacTransportState pressure_state;
    OccupiedMaterialPressureStep pressure;
    MacMassFluxes volume_fluxes_m3, mass_fluxes_kg;
    MacTransportReport transport;
    // Pressure cells are compacted in ascending grid-cell order. The two face
    // arrays contain the corresponding compact pressure-node index, or max().
    std::vector<std::size_t> active_cells, pressure_nodes_x, pressure_nodes_y;
    std::vector<double> material_volumes_m3, geometric_residuals_m3;
    double pressure_energy_residual_j = 0, pressure_energy_bound_j = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    double pressure_wall_impulse_x = 0, pressure_wall_impulse_y = 0;
    std::size_t unresolved_momentum_updates = 0;
};
// Closed, occupied regular-grid Lagrange/projection split. Work pressure acts
// directly on canonical face J; the same midpoint sweeps define material
// volume and donor density for the compatible transport operator. No gravity,
// diffusion, free surface, phase transition or game dispatch is supplied here.
[[nodiscard]] MacClosedSubstep advance_mac_closed_substep(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, double seconds, const MacClosedSettings& = {});

struct MacClosedAdvanceReport {
    std::size_t substeps = 0, attempts = 0, rejected_attempts = 0;
    std::size_t pressure_iterations = 0, nonlinear_iterations = 0;
    std::size_t unresolved_heat_updates = 0, unresolved_momentum_updates = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    double mixing_heat_j = 0, wall_heat_j = 0;
    double wall_impulse_x = 0, wall_impulse_y = 0;
    double maximum_geometric_residual_m3 = 0;
};
struct MacClosedAdvance;
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
