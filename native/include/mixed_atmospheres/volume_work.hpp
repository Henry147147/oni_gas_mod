#pragma once
#include "acoustics.hpp"
#include "mixture.hpp"

namespace ma {
struct MaterialCell {
    MixtureInventory mixture;
    double volume_m3 = 1;
    // Used only for a completely filled incompressible liquid cell. Gas and
    // gas/liquid cells derive their pressure from the thermodynamic inventory.
    double incompressible_pressure_pa = 101325;
};

struct MaterialPressureStep {
    std::vector<MaterialCell> cells;
    std::vector<double> velocities_m_per_s;
    std::vector<double> work_pressures_pa; // time-discrete pressure in p dV
    std::vector<double> endpoint_pressures_pa;
    double body_force_work_j = 0;
    double energy_balance_error_j = 0;
    double maximum_volume_residual_m3 = 0;
    std::size_t nonlinear_iterations = 0;
    std::size_t pressure_iterations = 0;
};

// Reversible pressure-work substep on material (moving-volume) cells, with
// constant-capacity ideal gases and incompressible liquids at a common T.
// Species stay in their material cells. A discrete pressure gradient and
// midpoint face velocity conserve internal + staggered kinetic energy, with
// body-force work accounted separately. Cell entropy remains constant.
//
// This is NOT an Eulerian world step: geometry, conservative remapping of the
// staggered momentum, shocks/dissipation, vacuum, free surfaces, phase change,
// and immiscible separation remain the caller's responsibility. Faces must
// have compatible areas and fixed material inertias. Failure changes no input.
[[nodiscard]] MaterialPressureStep advance_material_pressure(
    const SpeciesRegistry& registry, std::span<const MaterialCell> cells,
    std::span<const AcousticFace> faces, double seconds);
}
