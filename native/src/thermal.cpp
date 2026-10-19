#include "mixed_atmospheres/thermal.hpp"
#include "mixed_atmospheres/pressure.hpp"
#include "mixed_atmospheres/species.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace ma {
double thermal_conductance(double first, double second, double da, double db, double area) {
    for (const auto value : {first, second, da, db, area}) require_finite(value, "thermal geometry");
    if (first < 0 || second < 0 || da <= 0 || db <= 0 || area < 0)
        throw std::invalid_argument("Invalid thermal geometry");
    if (first == 0 || second == 0 || area == 0) return 0;
    const double result = static_cast<double>(static_cast<long double>(area) /
        (static_cast<long double>(da) / first + static_cast<long double>(db) / second));
    require_finite(result, "thermal conductance");
    return result;
}

ThermalResult conduct_heat(std::span<const ThermalBody> bodies,
                           std::span<const ThermalContact> contacts, double seconds) {
    require_finite(seconds, "heat exchange timestep");
    if (seconds < 0 || bodies.empty()) throw std::invalid_argument("Invalid heat exchange domain/timestep");
    std::vector<double> capacity, initial;
    for (const auto& body : bodies) {
        require_finite(body.heat_capacity_j_per_k, "heat capacity");
        require_finite(body.temperature_k, "thermal temperature");
        if (body.heat_capacity_j_per_k <= 0 || body.temperature_k <= 0)
            throw std::invalid_argument("Thermal bodies need positive capacity and temperature");
        capacity.push_back(body.heat_capacity_j_per_k);
        initial.push_back(body.temperature_k);
    }
    std::vector<PressureFace> faces;
    for (const auto& contact : contacts) {
        require_finite(contact.conductance_w_per_k, "thermal contact");
        if (contact.conductance_w_per_k < 0) throw std::invalid_argument("Negative thermal contact");
        faces.push_back({contact.first, contact.second, seconds * contact.conductance_w_per_k});
    }
    const PressureSystem system(capacity, faces);
    // Solve temperatures relative to each connected component's equilibrium.
    // Otherwise a large dt*G times two nearly equal absolute temperatures can
    // make the true residual smaller than floating point can represent.
    std::vector<std::size_t> parent(bodies.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](std::size_t node) {
        while (parent[node] != node) { parent[node] = parent[parent[node]]; node = parent[node]; }
        return node;
    };
    for (const auto& face : faces) if (face.conductance > 0) parent[root(face.right)] = root(face.left);
    std::vector<long double> capacities(bodies.size()), sensible(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        capacities[root(i)] += capacity[i];
        sensible[root(i)] += static_cast<long double>(capacity[i]) * initial[i];
    }
    std::vector<double> relative_temperature(bodies.size()), rhs(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        relative_temperature[i] = static_cast<double>(initial[i] - sensible[root(i)] / capacities[root(i)]);
        rhs[i] = capacity[i] * relative_temperature[i];
    }
    const auto solve = system.solve(rhs, relative_temperature, 1e-13, 1e-12);
    ThermalResult result{initial, std::vector<double>(bodies.size()), 0, solve.iterations};
    // Apply signed face energy once to each endpoint. Reconstruct from these
    // transfers instead of accepting a small linear-solve energy residual.
    std::vector<long double> energy(bodies.size());
    for (const auto& face : faces) {
        const long double q = static_cast<long double>(face.conductance) *
            (static_cast<long double>(solve.pressure[face.right]) - solve.pressure[face.left]);
        result.shared_face_energy_j.push_back(q);
        energy[face.left] += q;
        energy[face.right] -= q;
    }
    const auto [minimum, maximum] = std::minmax_element(initial.begin(), initial.end());
    long double entropy = 0, entropy_scale = 0;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        result.energy_changes_j[i] = static_cast<double>(energy[i]);
        require_finite(result.energy_changes_j[i], "thermal energy transfer");
        const long double change = energy[i] / capacity[i];
        const double t = static_cast<double>(initial[i] + change);
        require_finite(t, "thermal result temperature");
        const double tolerance = 1e-10 * std::max(1.0, *maximum);
        if (t <= 0 || t < *minimum - tolerance || t > *maximum + tolerance)
            throw std::runtime_error("Heat solve violated maximum principle; no state published");
        result.temperatures_k[i] = t;
        entropy += static_cast<long double>(capacity[i]) * std::log1p(change / initial[i]);
        entropy_scale += capacity[i];
    }
    const long double entropy_tolerance = 256 * std::numeric_limits<double>::epsilon() * entropy_scale;
    if (entropy < -entropy_tolerance) throw std::runtime_error("Heat exchange reduced closed-system entropy");
    result.entropy_produced_j_per_k = static_cast<double>(std::max(0.0L, entropy));
    require_finite(result.entropy_produced_j_per_k, "thermal entropy");
    return result;
}
}
