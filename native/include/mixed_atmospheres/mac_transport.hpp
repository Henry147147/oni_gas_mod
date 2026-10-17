#pragma once
#include "mixture.hpp"
#include "passive_quantities.hpp"
#include "mac_energy.hpp"
#include "mac_fields.hpp"
#include "mac_momentum_limiter.hpp"
#include <cstdint>
#include <span>

namespace ma { class FluidThermodynamics; }

namespace ma::experimental {
enum class MacMomentumReconstruction { donor_cell = 0, limited_linear = 1, quadratic_limited_linear = 2 };
// Closed rectangular MAC transport component. Periodicity is explicit; solids
// and exterior boundaries are impermeable. No pressure, body force or timestep
// selection is performed here. Every fluid cell must remain occupied.
struct MacGrid {
    std::size_t width = 1, height = 1;
    double spacing_m = 1, depth_m = 1;
    bool periodic_x = false, periodic_y = false;
    std::vector<std::uint8_t> walls;
    [[nodiscard]] std::size_t x_width() const { return width + (periodic_x ? 0 : 1); }
    [[nodiscard]] std::size_t y_height() const { return height + (periodic_y ? 0 : 1); }
    [[nodiscard]] std::size_t x_face(std::size_t x, std::size_t y) const { return x + y * x_width(); }
    [[nodiscard]] std::size_t y_face(std::size_t x, std::size_t y) const { return x + y * width; }
    [[nodiscard]] double volume_m3() const { return spacing_m * spacing_m * depth_m; }
};
struct MacMaterialCell { MixtureInventory mixture; PassiveQuantities passive_quantities; };
struct MacTransportState {
    MacGrid grid;
    std::vector<MacMaterialCell> cells;
    // Normal face momentum, never reconstructed from cell momentum. These
    // doubles are explicit projections when the corresponding full field exists.
    std::vector<double> momentum_x, momentum_y;
    // Authoritative when nonempty; momentum_x/y are explicit projections.
    std::vector<MacScaled> full_momentum_x{}, full_momentum_y{};
};
struct MacMassFluxes {
    // Integrated kg, oriented toward positive x/y; one value per primal face.
    std::vector<double> x, y;
    std::vector<MacScaled> full_x{}, full_y{};
};
struct MacPrimalParcel {
    std::size_t axis = 0, face = 0, donor = 0, target = 0;
    double volume_m3 = 0;
    MacMaterialCell contents;
    // Zero: contents stores actual amounts (legacy form). Otherwise contents
    // is a prototype at this volume; actual amounts retain the common scale.
    double reference_volume_m3 = 0;
    MacScaled full_volume_m3{};
};
// Complete conservative donor decomposition. The transport API validates
// inventories, geometry, each donor's species/U/passive accounts and EOS. It
// does not certify a reconstruction method chosen by an arbitrary caller.
struct MacParcelPlan {
    std::vector<MacPrimalParcel> parcels;
    std::vector<MacMaterialCell> retained;
    std::vector<double> source_volumes_m3, retained_volumes_m3, limiter;
    std::size_t eos_backtracks = 0, unresolved_stencil_cells = 0;
};
struct MacDualFlux { std::size_t left = 0, right = 0; double mass_kg = 0; MacScaled full_mass_kg{}; };
[[nodiscard]] inline MacScaled mac_projected_value(double projected, MacScaled full) {
    if (full.is_zero()) return MacScaled::from_double(projected);
    if (std::bit_cast<std::uint64_t>(full.project().value) != std::bit_cast<std::uint64_t>(projected))
        throw std::invalid_argument("MAC scalar differs from its explicit projection");
    return full;
}
[[nodiscard]] inline MacScaled mac_dual_mass(const MacDualFlux& flux) { return mac_projected_value(flux.mass_kg, flux.full_mass_kg); }
[[nodiscard]] inline MacDualFlux mac_dual_flux(std::size_t left, std::size_t right, MacScaled value) {
    const auto p = value.project(); return {left, right, p.value, p.rounded ? value : MacScaled{}};
}
[[nodiscard]] inline MacMassFluxes mac_fluxes(std::span<const MacScaled> x, std::span<const MacScaled> y) {
    MacMassFluxes result; store_mac_field(result.x, result.full_x, x); store_mac_field(result.y, result.full_y, y); return result;
}
struct MacFaceEnergy {
    MacEnergy old_kinetic, new_kinetic, transported_kinetic, mixing_heat, wall_heat;
    // Corresponding legacy binary64 fields are projections, never account
    // operands. Bits 0..4 flag rounding in the order declared above.
    unsigned int rounded_projection_mask = 0;
    // Original 128-epsilon donor account plus actual correction/work-storage
    // stage accounts. Cancellation cannot reset inherited roundoff to the
    // magnitude of only the much smaller final transported energy.
    MacEnergy composed_energy_bound{};
};
struct MacMomentumReconstructionEvent {
    std::size_t dual_edge = 0, transport_axis = 0;
    MacScaled predictor_delta_velocity, bounded_delta_velocity;
    MacMomentumLimitedPair pair;
};
struct MacFaceTransportReport {
    std::vector<double> old_inertia, new_inertia, old_kinetic, new_kinetic;
    std::vector<double> transported_kinetic, mixing_heat, wall_heat, wall_impulse, dual_mass_residual;
    std::vector<MacDualFlux> fluxes;
    std::vector<MacFaceEnergy> energies;
    std::size_t rounded_energy_projections = 0;
    std::vector<MacScaled> full_wall_impulse;
    std::vector<MacScaled> full_dual_mass_residual{};
    MacMomentumReconstruction reconstruction = MacMomentumReconstruction::donor_cell;
    std::vector<MacScaled> upwind_momentum, velocity_slope_x, velocity_slope_y, centered_courant_x, centered_courant_y;
    std::vector<MacEnergy> upwind_mixing_heat, upwind_transported_kinetic;
    std::vector<MacMomentumReconstructionEvent> corrections;
    std::size_t limited_corrections = 0, unresolved_momentum_corrections = 0, unresolved_remainder_corrections = 0;
};
struct MacTransportReport {
    MacFaceTransportReport x, y;
    std::vector<double> species_mass_residual;
    double energy_residual_j = 0, energy_bound_j = 0;
    double momentum_x_residual = 0, momentum_y_residual = 0;
    MacScaled full_momentum_x_residual{}, full_momentum_y_residual{};
    double mixing_heat_j = 0, wall_heat_j = 0;
    std::size_t unresolved_heat_updates = 0;
    MacEnergy energy_residual_wide_j, energy_bound_wide_j, mixing_heat_wide_j, wall_heat_wide_j;
    std::size_t rounded_energy_projections = 0;
    MacMassFluxes primal_mass_fluxes_kg; // populated by the parcel overload
    std::size_t scaled_primary_parcels = 0;
    std::vector<double> advected_internal_energy_j; // populated before heat deposition in reconstructed momentum mode
};
struct MacTransportAdvance { MacTransportState state; MacTransportReport report; };
struct MacFaceInertias { std::vector<double> x, y; };
[[nodiscard]] MacFaceInertias mac_face_inertias(const SpeciesRegistry&, const MacTransportState&);
// EOS-aware occupied transport: source volumes can be material volumes from a
// pressure substep; the target is the grid's nominal fixed cell volume. No
// legacy caloric-temperature or constant-density liquid validation is used.
[[nodiscard]] MacFaceInertias mac_face_inertias(const SpeciesRegistry&, const MacTransportState&,
    const FluidThermodynamics&, std::span<const double> source_volumes_m3 = {});
// Shared positive primal transfers carry every species, U and passive quantity.
// Compatible dual fluxes transport normal J and K; upwind variance and fixed-wall
// kinetic removal become separately reported heat. CFL/precision/domain failures
// return no candidate and never change the source. This is not a world step,
// free-surface remap, entropy/process proof, gravity or game backend.
[[nodiscard]] MacTransportAdvance advance_mac_transport(const SpeciesRegistry&, const MacTransportState&, const MacMassFluxes&,
    MacMomentumReconstruction = MacMomentumReconstruction::donor_cell);
[[nodiscard]] MacTransportAdvance advance_mac_transport(const SpeciesRegistry&, const MacTransportState&, const MacMassFluxes&,
    const FluidThermodynamics&, std::span<const double> source_volumes_m3,
    MacMomentumReconstruction = MacMomentumReconstruction::donor_cell);
[[nodiscard]] MacTransportAdvance advance_mac_parcel_transport(const SpeciesRegistry&, const MacTransportState&,
    const FluidThermodynamics&, const MacParcelPlan&, MacMomentumReconstruction = MacMomentumReconstruction::donor_cell);
}
