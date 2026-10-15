#include "mixed_atmospheres/transport.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ma {
double TransportCell::kinetic_energy() const {
    const double mass = mixture.mass();
    return mass == 0 ? 0 : (momentum_x * momentum_x + momentum_y * momentum_y) / (2 * mass);
}

namespace {
void validate(const SpeciesRegistry& registry, const TransportCell& cell) {
    cell.mixture.validate(registry);
    require_finite(cell.volume_m3, "transport cell volume");
    require_finite(cell.momentum_x, "transport x momentum");
    require_finite(cell.momentum_y, "transport y momentum");
    if (cell.volume_m3 <= 0) throw std::invalid_argument("Transport cell volume must be positive");
    if (cell.mixture.mass() == 0 && (cell.momentum_x != 0 || cell.momentum_y != 0))
        throw std::invalid_argument("Vacuum cannot carry momentum");
    require_finite(cell.kinetic_energy(), "transport kinetic energy");
    for (std::size_t s = 0; s < registry.size(); ++s)
        if (cell.mixture.masses[s] != 0 && registry[s].phase == Phase::solid)
            throw std::domain_error("Mixture remap cannot transport fixed solids");
    const double occupied = cell.mixture.condensed_volume(registry);
    require_finite(occupied, "transport condensed volume");
    // Only a floating-point tolerance; no excess mass or constituent is cut.
    if (occupied > cell.volume_m3 * (1 + 1e-12))
        throw std::domain_error("Transport would overfill condensed volume; pressure/displacement is required");
}
}

namespace {
std::vector<TransportCell> remap(const SpeciesRegistry& registry,
                               std::span<const TransportCell> cells,
                               std::span<const SweptFace> faces,
                               std::span<const double> target_volumes,
                               std::span<const double> potentials) {
    if (cells.empty() || registry.size() == 0) throw std::invalid_argument("Empty mixture transport domain");
    const auto count = cells.size();
    if ((!target_volumes.empty() && target_volumes.size() != count) ||
        (!potentials.empty() && potentials.size() != count))
        throw std::invalid_argument("Remap geometry size mismatch");
    std::vector<long double> outgoing(count, 0), incoming(count, 0);
    std::vector<double> total_energy(count), updated_energy(count);
    for (std::size_t i = 0; i < count; ++i) {
        validate(registry, cells[i]);
        total_energy[i] = cells[i].mixture.energy + cells[i].kinetic_energy();
        if (!potentials.empty()) {
            require_finite(potentials[i], "remap cell potential");
            total_energy[i] += cells[i].mixture.mass() * potentials[i];
        }
        require_finite(total_energy[i], "transport total energy");
    }
    for (const auto& face : faces) {
        require_finite(face.volume_m3, "swept face volume");
        if (face.left >= count || face.right >= count || face.left == face.right)
            throw std::invalid_argument("Invalid transport face");
        const auto donor = face.volume_m3 >= 0 ? face.left : face.right;
        const auto receiver = face.volume_m3 >= 0 ? face.right : face.left;
        outgoing[donor] += std::abs(face.volume_m3);
        incoming[receiver] += std::abs(face.volume_m3);
    }
    std::vector<TransportCell> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (outgoing[i] > cells[i].volume_m3)
            throw std::domain_error("Transport CFL exceeded; retry with a smaller timestep");
        double target = cells[i].volume_m3;
        if (!target_volumes.empty()) {
            target = target_volumes[i];
            require_finite(target, "remap target volume");
            const long double expected = cells[i].volume_m3 - outgoing[i] + incoming[i];
            if (target <= 0 || std::abs(expected - target) > 5e-12L * std::max(target, cells[i].volume_m3))
                throw std::domain_error("Remap violates geometric volume conservation");
        }
        const double retained = static_cast<double>(1 - outgoing[i] / cells[i].volume_m3);
        result.push_back({MixtureInventory(registry.size()), target,
                          retained * cells[i].momentum_x, retained * cells[i].momentum_y});
        auto& next = result.back();
        for (std::size_t s = 0; s < registry.size(); ++s)
            next.mixture.masses[s] = retained * cells[i].mixture.masses[s];
        updated_energy[i] = retained * total_energy[i];
    }
    // Retain the donor remainder first, then only ADD positive incoming mass.
    // This avoids order-dependent negative trace masses from sequential
    // subtraction, while every shared face has one common transfer weight.
    for (const auto& face : faces) {
        const auto donor = face.volume_m3 >= 0 ? face.left : face.right;
        const auto receiver = face.volume_m3 >= 0 ? face.right : face.left;
        const double fraction = std::abs(face.volume_m3) / cells[donor].volume_m3;
        auto& next = result[receiver];
        for (std::size_t s = 0; s < registry.size(); ++s)
            next.mixture.masses[s] += fraction * cells[donor].mixture.masses[s];
        next.momentum_x += fraction * cells[donor].momentum_x;
        next.momentum_y += fraction * cells[donor].momentum_y;
        updated_energy[receiver] += fraction * total_energy[donor];
    }
    for (std::size_t i = 0; i < count; ++i) {
        result[i].mixture.energy = updated_energy[i] - result[i].kinetic_energy();
        if (!potentials.empty()) result[i].mixture.energy -= result[i].mixture.mass() * potentials[i];
        validate(registry, result[i]);
    }
    return result;
}
}

std::vector<TransportCell> advect_mixtures(const SpeciesRegistry& registry,
                                          std::span<const TransportCell> cells,
                                          std::span<const SweptFace> faces) {
    return remap(registry, cells, faces, {}, {});
}

std::vector<TransportCell> remap_material_mixtures(const SpeciesRegistry& registry,
    std::span<const TransportCell> cells, std::span<const SweptFace> faces,
    std::span<const double> target_volumes, std::span<const double> potentials) {
    if (target_volumes.empty()) throw std::invalid_argument("Missing remap target volumes");
    return remap(registry, cells, faces, target_volumes, potentials);
}
}
