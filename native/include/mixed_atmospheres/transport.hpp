#pragma once
#include "mixture.hpp"
#include "fluid_thermodynamics.hpp"
#include <cstdint>
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

struct OccupiedMeshPosition {
    std::size_t grid_line = 0;
    double offset_m = 0; // local offset; not added to a large absolute position
};
[[nodiscard]] inline double occupied_position_relative_to(OccupiedMeshPosition position, std::size_t grid_line, double dx) {
    return (static_cast<double>(position.grid_line) - static_cast<double>(grid_line)) * dx + position.offset_m;
}
[[nodiscard]] inline double occupied_interval_length(OccupiedMeshPosition begin, OccupiedMeshPosition end, double dx) {
    return (static_cast<double>(end.grid_line) - static_cast<double>(begin.grid_line)) * dx + (end.offset_m - begin.offset_m);
}
struct OccupiedTransportCell {
    MixtureInventory mixture;
    OccupiedMeshPosition begin, end; // endpoints share exact node coordinates
    double momentum_kg_m_per_s = 0;
};
// Geometric interval-overlap remap. Entire donors may leave a fixed cell;
// there is no cut-volume outgoing-fraction cutoff. One uniform connected slab
// is represented per cell. Potential first moments, species, momentum and
// internal/kinetic energy all follow the same occupied overlaps.
[[nodiscard]] std::vector<OccupiedTransportCell> remap_occupied_mixtures(
    const FluidThermodynamics& thermodynamics, std::span<const OccupiedTransportCell> moving,
    std::span<const std::uint8_t> walls, double cell_size_m, double cross_section_m2,
    double gravitational_potential_gradient_j_per_kg_m = 0);
}
