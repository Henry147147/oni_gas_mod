#pragma once
#include "mixed_atmospheres/mac_transport.hpp"

namespace ma::experimental {
class MacRemapCandidateRejected final : public std::domain_error {
public:
    explicit MacRemapCandidateRejected(const char* why) : std::domain_error(why) {}
};
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
