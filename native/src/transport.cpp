#include "mixed_atmospheres/transport.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <optional>

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

std::vector<OccupiedTransportCell> remap_occupied_mixtures(const FluidThermodynamics& model,
    std::span<const OccupiedTransportCell> moving, std::span<const std::uint8_t> walls,
    double dx, double area, double gravity) {
    for (double value : {dx, area, gravity}) require_finite(value, "occupied remap geometry");
    if (walls.empty() || dx <= 0 || area <= 0 || gravity < 0)
        throw std::invalid_argument("Invalid occupied remap geometry");
    struct Sum {
        double value = 0, correction = 0;
        void add(double amount) {
            const double next = value + amount;
            correction += std::abs(value) >= std::abs(amount) ? (value - next) + amount : (amount - next) + value;
            value = next;
        }
        [[nodiscard]] double total() const { return value + correction; }
    };
    struct Sums { std::vector<Sum> masses; Sum internal, kinetic, potential, momentum; };
    std::vector<Sums> sums(walls.size());
    std::vector<OccupiedTransportCell> result;
    result.reserve(walls.size());
    for (std::size_t i = 0; i < walls.size(); ++i) {
        sums[i].masses.resize(model.registry_size());
        result.push_back({MixtureInventory(model.registry_size()), {}, {}, 0});
    }
    struct Overlap { std::size_t cell; double begin, end; };
    std::optional<OccupiedMeshPosition> previous_end;
    for (const auto& donor : moving) {
        model.validate_inventory(donor.mixture);
        for (double value : {donor.begin.offset_m, donor.end.offset_m, donor.momentum_kg_m_per_s}) require_finite(value, "occupied remap donor");
        const double mass = donor.mixture.mass();
        const double length = occupied_interval_length(donor.begin, donor.end, dx);
        if (mass <= 0 || length <= 0 || donor.begin.grid_line > walls.size() || donor.end.grid_line > walls.size())
            throw std::invalid_argument("Occupied remap donors must contain positive material in an interval");
        if (occupied_position_relative_to(donor.begin, 0, dx) < 0 || occupied_position_relative_to(donor.end, walls.size(), dx) > 0)
            throw FluidFatalError(FluidFatalCode::unsupported_topology, "Free surface reached a rigid domain boundary; collision topology is unsupported");
        if (previous_end && occupied_interval_length(*previous_end, donor.begin, dx) != 0)
            throw FluidFatalError(FluidFatalCode::unsupported_topology, "Occupied remap cannot create or merge interior gaps/overlaps");
        previous_end = donor.end;
        const auto state = model.evaluate(donor.mixture, area * length);
        if (state.vacuum_volume_m3 > 32 * std::numeric_limits<double>::epsilon() * area * length)
            throw FluidFatalError(FluidFatalCode::unsupported_topology, "Moving liquid interval contains unsupported internal void");
        const double kinetic = 0.5 * donor.momentum_kg_m_per_s * (donor.momentum_kg_m_per_s / mass);
        const double potential = mass * gravity * (static_cast<double>(donor.begin.grid_line) * dx + donor.begin.offset_m + 0.5 * length);
        require_finite(kinetic, "occupied remap kinetic energy"); require_finite(potential, "occupied remap potential energy");
        const double first_index = static_cast<double>(donor.begin.grid_line) + std::floor(donor.begin.offset_m / dx);
        const double last_index = static_cast<double>(donor.end.grid_line) + std::floor(donor.end.offset_m / dx);
        if (!std::isfinite(first_index) || !std::isfinite(last_index) || first_index < 0 || first_index >= static_cast<double>(walls.size()) || last_index < first_index)
            throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied mesh overlap indices are unrepresentable");
        const auto first = static_cast<std::size_t>(first_index);
        const auto last = static_cast<std::size_t>(std::min(static_cast<double>(walls.size() - 1), last_index));
        std::vector<Overlap> overlaps;
        for (std::size_t i = first; i <= last; ++i) {
            const double begin = std::max(0.0, occupied_position_relative_to(donor.begin, i, dx));
            const double end = std::min(dx, occupied_position_relative_to(donor.end, i, dx));
            if (end <= begin) continue;
            if (walls[i]) throw FluidFatalError(FluidFatalCode::unsupported_topology, "Moving free surface reached a rigid wall; collision topology is unsupported");
            overlaps.push_back({i, begin, end});
        }
        if (overlaps.empty()) throw FluidFatalError(FluidFatalCode::numerical_precision, "Positive material has no representable mesh overlap");
        // Give tiny cut receivers their directly computed positive share.
        // Assign the conservative subtraction remainder to the largest
        // overlap, where cancellation cannot erase an arbitrarily small wet
        // interval or its traces as a front crosses a fixed grid face.
        const auto largest = std::max_element(overlaps.begin(), overlaps.end(), [](const auto& a, const auto& b) {
            return a.end - a.begin < b.end - b.begin;
        });
        std::iter_swap(largest, overlaps.end() - 1);
        auto remaining_masses = donor.mixture.masses;
        double remaining_internal = donor.mixture.energy, remaining_kinetic = kinetic;
        double remaining_potential = potential, remaining_momentum = donor.momentum_kg_m_per_s;
        for (std::size_t o = 0; o < overlaps.size(); ++o) {
            const auto& overlap = overlaps[o]; const bool final = o + 1 == overlaps.size();
            const double overlap_length = overlap.end - overlap.begin;
            const double fraction = overlap_length / length;
            const auto share = [&](double value) {
                const double density = value / length;
                return std::isfinite(density) && (density != 0 || value == 0) ? density * overlap_length : fraction * value;
            };
            auto& target = result[overlap.cell]; auto& accumulated = sums[overlap.cell];
            if (target.begin.offset_m == target.end.offset_m) { target.begin = {overlap.cell, overlap.begin}; target.end = {overlap.cell, overlap.end}; }
            else {
                if (overlap.begin > target.end.offset_m || overlap.end < target.begin.offset_m)
                    throw FluidFatalError(FluidFatalCode::unsupported_topology, "Target cell needs more than one occupied interval");
                target.begin.offset_m = std::min(target.begin.offset_m, overlap.begin); target.end.offset_m = std::max(target.end.offset_m, overlap.end);
            }
            double overlap_mass = 0;
            for (std::size_t s = 0; s < model.registry_size(); ++s) {
                const double amount = final ? remaining_masses[s] : share(donor.mixture.masses[s]);
                if (amount < 0 || (!final && donor.mixture.masses[s] > 0 && amount == 0))
                    throw FluidFatalError(FluidFatalCode::numerical_precision, "Occupied constituent overlap is unrepresentable");
                accumulated.masses[s].add(amount); overlap_mass += amount;
                if (!final) remaining_masses[s] -= amount;
            }
            const double internal = final ? remaining_internal : share(donor.mixture.energy);
            const double ke = final ? remaining_kinetic : share(kinetic);
            const double pe = final ? remaining_potential : overlap_mass * gravity * (static_cast<double>(overlap.cell) * dx + overlap.begin + 0.5 * overlap_length);
            const double momentum = final ? remaining_momentum : share(donor.momentum_kg_m_per_s);
            accumulated.internal.add(internal); accumulated.kinetic.add(ke);
            accumulated.potential.add(pe); accumulated.momentum.add(momentum);
            if (!final) { remaining_internal -= internal; remaining_kinetic -= ke; remaining_potential -= pe; remaining_momentum -= momentum; }
        }
    }
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto& target = result[i]; const auto& accumulated = sums[i];
        for (std::size_t s = 0; s < model.registry_size(); ++s) target.mixture.masses[s] = accumulated.masses[s].total();
        target.momentum_kg_m_per_s = accumulated.momentum.total();
        const double mass = target.mixture.mass();
        if (mass == 0) continue;
        const double length = target.end.offset_m - target.begin.offset_m;
        const double ke = 0.5 * target.momentum_kg_m_per_s * (target.momentum_kg_m_per_s / mass);
        const double pe = mass * gravity * (static_cast<double>(i) * dx + target.begin.offset_m + 0.5 * length);
        target.mixture.energy = accumulated.internal.total() +
            (accumulated.kinetic.total() - ke) + (accumulated.potential.total() - pe);
        const auto state = model.evaluate(target.mixture, area * length);
        if (state.vacuum_volume_m3 > 32 * std::numeric_limits<double>::epsilon() * area * length)
            throw FluidFatalError(FluidFatalCode::unsupported_topology, "Remapped liquid needs unsupported internal void");
    }
    return result;
}
}
