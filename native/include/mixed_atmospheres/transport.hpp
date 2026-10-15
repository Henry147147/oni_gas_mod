#pragma once
#include "mixture.hpp"
#include <span>
#include <vector>

namespace ma {
struct TransportCell {
    MixtureInventory mixture;
    double volume_m3 = 1;
    double momentum_x = 0;
    double momentum_y = 0;
    [[nodiscard]] double kinetic_energy() const;
};
struct SweptFace {
    std::size_t left, right;
    double volume_m3; // signed integral of face velocity * area * dt
};

// First-order donor-cell remap, shared by gas and liquid constituents. The sum
// of OUTGOING swept volumes at a donor must not exceed its geometric volume.
// Every constituent, momentum and total energy uses the same transport weights.
// Numerical kinetic-energy loss becomes internal energy, rather than vanishing.
//
// Closed-domain, homogeneous-velocity transport only: this does not calculate
// face velocities, pressure work, gravity work, phase equilibrium or separation.
// Faces absent from the list are impermeable. Overfilled condensed cells and
// unsupported solids fail before any caller-owned state can be changed.
[[nodiscard]] std::vector<TransportCell> advect_mixtures(const SpeciesRegistry& registry,
                                                        std::span<const TransportCell> cells,
                                                        std::span<const SweptFace> faces);

// Remap moving material volumes onto an Eulerian mesh. Unlike advect_mixtures,
// the target volume must equal old volume minus outgoing plus incoming swept
// volume. This geometric conservation law preserves bounds on liquid volume
// fractions, including a completely filled incompressible donor/receiver.
// Optional cell potentials [J/kg] are included in transported total energy;
// conversion between potential, kinetic and thermal energy stays conservative.
[[nodiscard]] std::vector<TransportCell> remap_material_mixtures(
    const SpeciesRegistry& registry, std::span<const TransportCell> cells,
    std::span<const SweptFace> faces, std::span<const double> target_volumes_m3,
    std::span<const double> potentials_j_per_kg = {});
}
