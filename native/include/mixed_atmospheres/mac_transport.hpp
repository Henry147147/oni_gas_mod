#pragma once
#include "mixture.hpp"
#include "passive_quantities.hpp"
#include <cstdint>
#include <span>

namespace ma { class FluidThermodynamics; }

namespace ma::experimental {
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
    // Canonical normal momentum, not reconstructed from cell momentum.
    std::vector<double> momentum_x, momentum_y;
};
struct MacMassFluxes {
    // Integrated kg, oriented toward positive x/y; one value per primal face.
    std::vector<double> x, y;
};
struct MacDualFlux { std::size_t left = 0, right = 0; double mass_kg = 0; };
struct MacFaceTransportReport {
    std::vector<double> old_inertia, new_inertia, old_kinetic, new_kinetic;
    std::vector<double> transported_kinetic, mixing_heat, wall_heat, wall_impulse, dual_mass_residual;
    std::vector<MacDualFlux> fluxes;
};
struct MacTransportReport {
    MacFaceTransportReport x, y;
    std::vector<double> species_mass_residual;
    double energy_residual_j = 0, energy_bound_j = 0;
    double momentum_x_residual = 0, momentum_y_residual = 0;
    double mixing_heat_j = 0, wall_heat_j = 0;
    std::size_t unresolved_heat_updates = 0;
};
struct MacTransportAdvance { MacTransportState state; MacTransportReport report; };
struct MacFaceInertias { std::vector<double> x, y; };
[[nodiscard]] MacFaceInertias mac_face_inertias(const SpeciesRegistry&, const MacTransportState&);
// EOS-aware occupied transport: source volumes can be material volumes from a
// pressure substep; the target is the grid's nominal fixed cell volume. No
// legacy caloric-temperature or constant-density liquid validation is used.
[[nodiscard]] MacFaceInertias mac_face_inertias(const SpeciesRegistry&, const MacTransportState&,
    const FluidThermodynamics&);
// Shared positive primal transfers carry every species, U and passive quantity.
// Compatible dual fluxes transport normal J and K; upwind variance and fixed-wall
// kinetic removal become separately reported heat. CFL/precision/domain failures
// return no candidate and never change the source. This is not a world step,
// free-surface remap, entropy/process proof, gravity or game backend.
[[nodiscard]] MacTransportAdvance advance_mac_transport(const SpeciesRegistry&, const MacTransportState&, const MacMassFluxes&);
[[nodiscard]] MacTransportAdvance advance_mac_transport(const SpeciesRegistry&, const MacTransportState&, const MacMassFluxes&,
    const FluidThermodynamics&, std::span<const double> source_volumes_m3);
}
