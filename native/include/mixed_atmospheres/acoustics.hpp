#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace ma {
// Frozen-coefficient acoustic subproblem, on cells and oriented interior faces.
// Compliance is V/(rho*c*c) [m^3/Pa]; zero denotes an incompressible cell.
// The caller supplies the bulk mixture density/sound speed, never species drift.
struct AcousticCell {
    double pressure_pa;
    double compliance_m3_per_pa;
};
struct AcousticFace {
    std::size_t left, right;
    double area_m2;
    double inertia_kg; // rho_face * face_area * distance_between_centres
    double velocity_m_per_s; // positive from left to right
    double acceleration_m_per_s2 = 0; // gravity projected onto the face normal
};
struct AcousticStep {
    std::vector<double> pressures_pa;
    std::vector<double> velocities_m_per_s;
    std::vector<double> cell_volume_changes_m3; // outward swept volume
    double body_force_work_j = 0;
    double acoustic_energy_change_j = 0;
    double numerical_dissipation_j = 0;
    double maximum_continuity_error_m3 = 0;
    std::size_t pressure_iterations = 0;
};

// Backward-Euler pressure/velocity coupling. Boundaries omitted from faces are
// impermeable. A fully incompressible connected component uses a pressure gauge
// and retains zero net volume change. Inputs are unchanged on failure.
//
// This advances the LINEARIZED acoustic subproblem only. Its quadratic acoustic
// energy is not a replacement for thermodynamic total energy. Conservative
// advection, nonlinear EOS/pressure-work coupling, free surfaces, and remapping
// are still required before this can advance MixtureInventory or a game world.
[[nodiscard]] AcousticStep advance_acoustics(std::span<const AcousticCell> cells,
                                            std::span<const AcousticFace> faces,
                                            double seconds);
}
