#include "mixed_atmospheres/acoustics.hpp"
#include "mixed_atmospheres/pressure.hpp"
#include "mixed_atmospheres/species.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace ma {
AcousticStep advance_acoustics(std::span<const AcousticCell> cells,
                               std::span<const AcousticFace> faces, double dt) {
    require_finite(dt, "acoustic timestep");
    if (dt < 0 || cells.empty()) throw std::invalid_argument("Invalid acoustic domain or timestep");
    const auto count = cells.size();
    std::vector<double> compliance(count), rhs(count, 0), face_drives(faces.size()), weights(faces.size());
    std::vector<std::size_t> parents(count);
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](std::size_t i) {
        while (parents[i] != i) { parents[i] = parents[parents[i]]; i = parents[i]; }
        return i;
    };
    AcousticStep result;
    result.pressures_pa.reserve(count);
    result.velocities_m_per_s.reserve(faces.size());
    result.cell_volume_changes_m3.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& cell = cells[i];
        require_finite(cell.pressure_pa, "acoustic pressure");
        require_finite(cell.compliance_m3_per_pa, "acoustic compliance");
        if (cell.compliance_m3_per_pa < 0) throw std::invalid_argument("Negative acoustic compliance");
        compliance[i] = cell.compliance_m3_per_pa;
        result.pressures_pa.push_back(cell.pressure_pa);
    }
    std::vector<PressureFace> matrix_faces;
    matrix_faces.reserve(faces.size());
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const auto& face = faces[f];
        require_finite(face.area_m2, "acoustic face area");
        require_finite(face.inertia_kg, "acoustic face inertia");
        require_finite(face.velocity_m_per_s, "acoustic face velocity");
        require_finite(face.acceleration_m_per_s2, "acoustic acceleration");
        if (face.left >= count || face.right >= count || face.left == face.right ||
            face.area_m2 <= 0 || face.inertia_kg <= 0)
            throw std::invalid_argument("Invalid acoustic face");
        parents[root(face.right)] = root(face.left);
        result.velocities_m_per_s.push_back(face.velocity_m_per_s);
        // Solve for pressure INCREMENTS. A hydrostatic pressure gradient and
        // gravity cancel here, before a large background pressure enters CG.
        const double force = face.area_m2 * (cells[face.left].pressure_pa - cells[face.right].pressure_pa) +
                             face.inertia_kg * face.acceleration_m_per_s2;
        const double predicted = face.velocity_m_per_s + dt * force / face.inertia_kg;
        face_drives[f] = predicted;
        weights[f] = dt * face.area_m2 * (dt * face.area_m2 / face.inertia_kg);
        require_finite(predicted, "acoustic predicted velocity");
        require_finite(weights[f], "acoustic pressure coefficient");
        const double swept = dt * face.area_m2 * predicted;
        require_finite(swept, "acoustic predicted swept volume");
        rhs[face.left] -= swept;
        rhs[face.right] += swept;
        matrix_faces.push_back({face.left, face.right, weights[f]});
    }
    if (dt == 0) return result;
    std::vector<bool> compressible(count, false);
    std::vector<double> component_scale(count, 0);
    for (std::size_t i = 0; i < count; ++i)
        if (compliance[i] > 0) compressible[root(i)] = true;
    for (std::size_t f = 0; f < faces.size(); ++f)
        component_scale[root(faces[f].left)] = std::max(component_scale[root(faces[f].left)], weights[f]);
    // In a closed incompressible component sum(rhs)=0. One diagonal anchor
    // fixes dp_anchor=0 without changing the divergence equations. This is a
    // numerical gauge, NOT physical compliance; omit it from all energy and
    // volume diagnostics and verify the original equations after the solve.
    for (std::size_t i = 0; i < count; ++i) if (root(i) == i && !compressible[i])
        compliance[i] = component_scale[i] > 0 ? component_scale[i] : 1;
    const PressureSystem matrix(std::move(compliance), std::move(matrix_faces));
    const auto solved = matrix.solve(rhs, std::vector<double>(count), 1e-11, 1e-14);
    result.pressure_iterations = solved.iterations;
    for (std::size_t i = 0; i < count; ++i) {
        result.pressures_pa[i] += solved.pressure[i];
        require_finite(result.pressures_pa[i], "updated acoustic pressure");
    }
    long double energy = 0, dissipation = 0, work = 0;
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const auto& face = faces[f];
        const double next = face_drives[f] + dt * face.area_m2 / face.inertia_kg *
                            (solved.pressure[face.left] - solved.pressure[face.right]);
        require_finite(next, "updated acoustic velocity");
        result.velocities_m_per_s[f] = next;
        const double swept = dt * face.area_m2 * next;
        result.cell_volume_changes_m3[face.left] += swept;
        result.cell_volume_changes_m3[face.right] -= swept;
        const long double du = static_cast<long double>(next) - face.velocity_m_per_s;
        energy += face.inertia_kg * du * (face.velocity_m_per_s + 0.5L * du);
        dissipation += 0.5L * face.inertia_kg * du * du;
        work += static_cast<long double>(dt) * face.inertia_kg * face.acceleration_m_per_s2 * next;
    }
    double volume_scale = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const long double dp = solved.pressure[i];
        const double c = cells[i].compliance_m3_per_pa;
        energy += c * dp * (cells[i].pressure_pa + 0.5L * dp);
        dissipation += 0.5L * c * dp * dp;
        const double volume = result.cell_volume_changes_m3[i];
        result.maximum_continuity_error_m3 = std::max(result.maximum_continuity_error_m3,
                                                     std::abs(volume + c * solved.pressure[i]));
        volume_scale = std::max(volume_scale, std::max(std::abs(volume), std::abs(rhs[i])));
    }
    result.acoustic_energy_change_j = static_cast<double>(energy);
    result.numerical_dissipation_j = static_cast<double>(dissipation);
    result.body_force_work_j = static_cast<double>(work);
    require_finite(result.acoustic_energy_change_j, "acoustic energy change");
    require_finite(result.numerical_dissipation_j, "acoustic dissipation");
    require_finite(result.body_force_work_j, "acoustic body work");
    // Reject rather than publish a pressure solution that violates physical
    // volume continuity (especially the removed incompressible gauge equation).
    if (result.maximum_continuity_error_m3 > 2e-12 + 1e-9 * volume_scale)
        throw std::runtime_error("Acoustic pressure step failed volume continuity");
    return result;
}
}
