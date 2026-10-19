#pragma once
#include "mac_transport.hpp"
#include "fluid_thermodynamics.hpp"

namespace ma::experimental {
// Piecewise-linear material-to-fixed-grid parcels. One limiter scales all
// constituent/U/passive slopes in a donor, including retained and transverse
// predictor values. It uses no concentration cutoff or mass-fraction repair.
// Source/parcel/retained EOS admissibility is checked; an unsuccessful higher
// order slope is reduced together for every field, down to exact donor order.
[[nodiscard]] MacParcelPlan reconstruct_mac_parcels(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const double> material_volumes_m3,
    const MacMassFluxes& volume_fluxes_m3, double maximum_outgoing_fraction = .4, std::span<const double> void_phase_material_volumes_m3 = {});
// Opt-in first-moment geometry: the shared swept face locations and moved
// material centroids also define the gradient metric. Requires the material
// volumes to close against the fixed grid and the same volume sweeps.
[[nodiscard]] MacParcelPlan reconstruct_mac_swept_parcels(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const double> material_volumes_m3,
    const MacMassFluxes& volume_fluxes_m3, double maximum_outgoing_fraction = .4, std::span<const double> void_phase_material_volumes_m3 = {});
// Sharp interfaces: in a donor holding gas and liquid, a PLIC line (Youngs
// normal from neighbouring liquid fractions) places the liquid, and each swept
// strip carries liquid and gas in its geometric proportion at the donor's EOS
// state. A donor holding exactly two liquids and no gas places the denser one
// the same way. Single-phase donors keep limited-linear slopes away from
// interfaces and are flat beside them. A rejected split falls back to donor
// order. Conservation, EOS admission and momentum coupling are unchanged.
[[nodiscard]] MacParcelPlan reconstruct_mac_interface_parcels(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const double> material_volumes_m3,
    const MacMassFluxes& volume_fluxes_m3, double maximum_outgoing_fraction = .4, std::span<const double> void_phase_material_volumes_m3 = {});
// Void phase (any of the three): with material volumes, material_volumes_m3
// are swept cell volumes, each holding that much material and the rest void.
// A partly filled donor splits its strips between material and void by a PLIC
// line (Youngs normal from neighbouring filled fractions) and carries its
// material at one density; donors beside partly filled or empty cells keep
// donor order. The plan records each part's material and void volumes.
// Gravity weight of each y face with sharp interfaces: the mass of the two half
// cells beside it (upper half of the cell below, lower half of the cell above).
// A cell holding two phases (gas and liquid, or exactly two liquids) splits by
// its PLIC line at the grid volume, each phase at its cell-average density; other cells split evenly, so the weight
// equals the face inertia (m_below + m_above) / 2 away from interfaces.
[[nodiscard]] std::vector<double> mac_interface_gravity_weights(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&);
}
