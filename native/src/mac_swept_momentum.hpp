#pragma once
#include "mixed_atmospheres/mac_transport.hpp"

namespace ma::experimental {
class MacRemapCandidateRejected final : public std::domain_error {
public:
    explicit MacRemapCandidateRejected(const char* why) : std::domain_error(why) {}
};
struct MacNodalConversion {
    std::vector<MacScaled> momentum;
    std::vector<MacEnergy> work;
    std::vector<MacNodalConversionEvent> events;
    std::size_t unresolved_updates = 0;
};
[[nodiscard]] MacNodalConversion convert_mac_nodal_momentum_1d(const MacGrid&, bool x_component,
    std::span<const double> cell_mass, std::span<const double> source_volumes,
    const MacMassFluxes& sweeps, std::span<const double> inertia,
    std::span<const MacScaled> momentum, double correction_fraction);
// Discrete swept first moments of free, full-volume dual cells. Fixed half
// cells retain the donor wall treatment and receive no reconstructed impulse.
// Volume-edge entries have the same order/topology as report.fluxes; their
// unit-neutral full values represent m3, never inferred from reconstructed kg.
void reconstruct_mac_swept_momentum(const MacGrid&, bool x_component,
    std::span<const double> dual_source_volumes,
    const std::vector<MacDualFlux>& volume_edges,
    const std::vector<MacScaled>& old_velocity, std::vector<MacScaled>& momentum,
    MacFaceTransportReport&, double correction_fraction, MacMomentumReconstruction);
}
