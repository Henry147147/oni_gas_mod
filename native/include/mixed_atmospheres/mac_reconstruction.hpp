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
    const MacMassFluxes& volume_fluxes_m3, double maximum_outgoing_fraction = .4);
// Opt-in first-moment geometry: the shared swept face locations and moved
// material centroids also define the gradient metric. Requires the material
// volumes to close against the fixed grid and the same volume sweeps.
[[nodiscard]] MacParcelPlan reconstruct_mac_swept_parcels(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const double> material_volumes_m3,
    const MacMassFluxes& volume_fluxes_m3, double maximum_outgoing_fraction = .4);
}
