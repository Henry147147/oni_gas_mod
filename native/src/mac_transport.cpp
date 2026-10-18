#include "mixed_atmospheres/mac_transport.hpp"
#include "mixed_atmospheres/mac_parcel.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "mac_swept_momentum.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>

namespace ma::experimental {
namespace {
constexpr auto absent = std::numeric_limits<std::size_t>::max();
constexpr double eps = std::numeric_limits<double>::epsilon();
[[noreturn]] void precision(const char* reason) { throw FluidFatalError(FluidFatalCode::numerical_precision, reason); }
void require(bool condition, const char* reason) { if (!condition) throw std::invalid_argument(reason); }
struct Sum {
    double hi = 0, lo = 0, scale = 0;
    void add(double value) {
        require_finite(value, "MAC account operand");
        const double s = hi + value, z = s - hi, e = (hi - (s - z)) + (value - z);
        const double t = lo + e, h = s + t, q = h - s;
        lo = (s - (h - q)) + (t - q); hi = h; scale += std::abs(value);
        require_finite(hi, "MAC account sum"); require_finite(lo, "MAC account tail"); require_finite(scale, "MAC account scale");
    }
    [[nodiscard]] double value() const { return hi + lo; }
    [[nodiscard]] double bound() const { return 128 * eps * scale; }
    void check() const { if (std::abs(value()) > bound()) precision("MAC finite conservation account exceeds its operand bound"); }
};
double share(double value, double part, double total) {
    if (value == 0 || part == 0) return 0;
    if (part == total) return value;
    int ev = 0, ep = 0, et = 0;
    const double v = std::frexp(value, &ev), p = std::frexp(part, &ep), t = std::frexp(total, &et);
    const double answer = std::ldexp((v * p) / t, ev + ep - et);
    if (!std::isfinite(answer) || answer == 0 || std::signbit(answer) != std::signbit(value))
        precision("Positive MAC transfer cannot represent its constituent or signed scalar");
    return answer;
}
double projected_energy(MacEnergy value, std::size_t& rounded) {
    const auto projection = value.project();
    if (projection.rounded) ++rounded;
    return projection.value;
}
struct Neighbors { std::size_t left = absent, right = absent; };
struct Geometry {
    const MacGrid& g;
    std::vector<Neighbors> x, y;
    explicit Geometry(const MacGrid& grid) : g(grid) {
        require(g.width > 0 && g.height > 0 && g.width <= 1000000 && g.height <= 1000000 / g.width &&
            (!g.periodic_x || g.width > 1) && (!g.periodic_y || g.height > 1), "Invalid MAC dimensions/periodic topology");
        require_finite(g.spacing_m, "MAC spacing"); require_finite(g.depth_m, "MAC depth");
        require(g.spacing_m > 0 && g.depth_m > 0 && std::isfinite(g.volume_m3()) && g.volume_m3() > 0 &&
            g.walls.size() == g.width * g.height, "Invalid MAC volume or wall shape");
        for (auto wall : g.walls) require(wall <= 1, "Invalid MAC wall flag");
        const auto fluid = [&](std::size_t i) { return g.walls[i] ? absent : i; };
        x.resize(g.x_width() * g.height); y.resize(g.width * g.y_height());
        for (std::size_t row = 0; row < g.height; ++row) for (std::size_t col = 0; col < g.x_width(); ++col) {
            auto& pair = x[g.x_face(col, row)];
            if (col > 0 || g.periodic_x) pair.left = fluid((col + g.width - 1) % g.width + row * g.width);
            if (col < g.width) pair.right = fluid(col + row * g.width);
        }
        for (std::size_t row = 0; row < g.y_height(); ++row) for (std::size_t col = 0; col < g.width; ++col) {
            auto& pair = y[g.y_face(col, row)];
            if (row > 0 || g.periodic_y) pair.left = fluid(col + ((row + g.height - 1) % g.height) * g.width);
            if (row < g.height) pair.right = fluid(col + row * g.width);
        }
    }
};
std::vector<double> masses(const SpeciesRegistry& registry, const MacTransportState& state,
    const FluidThermodynamics* model = nullptr, std::span<const double> volumes = {}, bool evaluate_eos = true) {
    const auto& g = state.grid; require(state.cells.size() == g.walls.size(), "MAC material shape differs");
    require(volumes.empty() || volumes.size() == state.cells.size(), "MAC source-volume shape differs");
    require(registry.size() > 0 && registry.size() <= 16000000 && state.cells.size() <= 16000000 / registry.size(),
        "MAC constituent storage capacity exceeded");
    std::vector<double> result; result.reserve(state.cells.size());
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto& cell = state.cells[i];
        if (!volumes.empty()) require(std::isfinite(volumes[i]) && (g.walls[i] ? volumes[i] == 0 : volumes[i] > 0),
            "Invalid MAC material source volume");
        if (model) model->validate_inventory(cell.mixture); else cell.mixture.validate(registry);
        validate_passive_quantities(cell.passive_quantities);
        const auto mass = cell.mixture.mass();
        if (g.walls[i]) require(mass == 0 && cell.mixture.energy == 0 && cell.passive_quantities.empty(), "MAC solid contains fluid");
        else {
            if (!(mass > 0)) throw FluidFatalError(FluidFatalCode::unsupported_topology, "MAC transport requires occupied fluid cells");
            for (std::size_t s = 0; s < registry.size(); ++s)
                require(cell.mixture.masses[s] == 0 || registry[s].phase != Phase::solid, "MAC transport cannot move solids");
            if (model && evaluate_eos) {
                const double volume = volumes.empty() ? g.volume_m3() : volumes[i];
                const auto state_eos = model->evaluate(cell.mixture, volume);
                if (state_eos.vacuum_volume_m3 > 32 * eps * volume)
                    throw FluidFatalError(FluidFatalCode::unsupported_topology, "MAC occupied EOS cell contains internal void");
            } else if (!model) {
                const auto liquid = cell.mixture.condensed_volume(registry);
                require(liquid <= g.volume_m3() * (1 + 1e-12) && (cell.mixture.gas_moles(registry) > 0 ? liquid < g.volume_m3() :
                    std::abs(liquid - g.volume_m3()) <= 1e-12 * g.volume_m3()), "MAC closed fluid needs gas headspace or filled liquid");
            }
        }
        result.push_back(mass);
    }
    return result;
}
std::vector<double> inertias(const std::vector<Neighbors>& faces, const std::vector<double>& m) {
    std::vector<double> result; result.reserve(faces.size());
    for (const auto& face : faces) {
        const double left = face.left == absent ? 0 : .5 * m[face.left], right = face.right == absent ? 0 : .5 * m[face.right];
        if ((face.left != absent && left == 0) || (face.right != absent && right == 0)) precision("Positive MAC half mass is unrepresentable");
        const double value = left + right; require_finite(value, "MAC face inertia"); result.push_back(value);
    }
    return result;
}
void validate_momentum(const std::vector<Neighbors>& faces, const std::vector<double>& inertia, const std::vector<MacScaled>& momentum) {
    require(momentum.size() == faces.size(), "MAC momentum shape differs");
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (faces[i].left == absent || faces[i].right == absent) require(momentum[i].is_zero(), "Fixed MAC normal face has momentum");
        static_cast<void>(MacEnergy::kinetic(inertia[i], momentum[i]).project());
    }
}
void primal_edges(const std::vector<Neighbors>& faces, const std::vector<MacScaled>& flux, std::vector<MacDualFlux>& edges) {
    require(flux.size() == faces.size(), "MAC primal flux shape differs");
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto& face = faces[i];
        if (face.left == absent || face.right == absent) require(flux[i].is_zero(), "MAC mass flux crosses a solid/exterior boundary");
        else if (!flux[i].is_zero()) edges.push_back(mac_dual_flux(face.left, face.right, flux[i]));
    }
}
std::vector<MacDualFlux> dual_edges(const MacGrid& g, const MacMassFluxes& f, bool x_component) {
    const auto fx = mac_field_values(f.x, f.full_x), fy = mac_field_values(f.y, f.full_y);
    const auto midpoint = [](MacScaled a, MacScaled b) { MacScaledSum sum; sum.add(a); sum.add(b); return sum.value().shifted(-1); };
    std::vector<MacDualFlux> result; result.reserve(2 * g.width * g.height + g.width + g.height);
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) if (!g.walls[x + y * g.width]) {
        if (x_component) {
            const auto l = g.x_face(x, y), r = g.x_face((x + 1) % g.x_width(), y);
            result.push_back(mac_dual_flux(l, r, midpoint(fx[l], fx[r])));
        } else {
            const auto b = g.y_face(x, y), t = g.y_face(x, (y + 1) % g.y_height());
            result.push_back(mac_dual_flux(b, t, midpoint(fy[b], fy[t])));
        }
    }
    if (x_component) {
        for (std::size_t y = g.periodic_y ? 0 : 1; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
            MacScaled left, right;
            if (x > 0 || g.periodic_x) left = fy[g.y_face((x + g.width - 1) % g.width, y)];
            if (x < g.width) right = fy[g.y_face(x, y)];
            result.push_back(mac_dual_flux(g.x_face(x, (y + g.height - 1) % g.height), g.x_face(x, y), midpoint(left, right)));
        }
    } else {
        for (std::size_t x = g.periodic_x ? 0 : 1; x < g.width; ++x) for (std::size_t y = 0; y < g.y_height(); ++y) {
            MacScaled bottom, top;
            if (y > 0 || g.periodic_y) bottom = fx[g.x_face(x, (y + g.height - 1) % g.height)];
            if (y < g.height) top = fx[g.x_face(x, y)];
            result.push_back(mac_dual_flux(g.y_face((x + g.width - 1) % g.width, y), g.y_face(x, y), midpoint(bottom, top)));
        }
    }
    return result;
}
std::vector<MacScaled> retained(const std::vector<double>& m, const std::vector<MacDualFlux>& edges) {
    std::vector<MacScaledSum> outgoing(m.size());
    for (const auto& edge : edges) { const auto mass = mac_dual_mass(edge); if (!mass.is_zero()) outgoing[mass.mantissa() > 0 ? edge.left : edge.right].add(mass.magnitude()); }
    std::vector<MacScaled> result(m.size());
    for (std::size_t i = 0; i < m.size(); ++i) {
        MacScaledSum remaining; remaining.add(m[i]);
        for (std::size_t k = 0; k < outgoing[i].size(); ++k) remaining.add(outgoing[i].component(k).negated());
        if (remaining.sign() < 0) throw std::domain_error("MAC outgoing mass CFL exceeded");
        result[i] = mac_scaled_primary(remaining);
    }
    return result;
}
MacScaled signed_product(MacScaled a, MacScaled b) {
    return (b.mantissa() < 0 ? a.negated() : a).shared(b.magnitude(), MacScaled::from_double(1));
}
bool exceeds(MacScaled a, MacScaled b) { MacScaledSum sum; sum.add(a); sum.add(b.negated()); return sum.sign() > 0; }
MacScaled limited_slope(MacScaled left, MacScaled right) {
    if (left.is_zero() || right.is_zero() || (left.mantissa() < 0) != (right.mantissa() < 0)) return {};
    return exceeds(left.magnitude(), right.magnitude()) ? right : left;
}
MacEnergyAccount face_energy_account(const MacFaceEnergy& values) {
    MacEnergyAccount balance; balance.add(values.transported_kinetic); balance.add(values.new_kinetic.negated());
    balance.add(values.mixing_heat.negated()); balance.add(values.wall_heat.negated());
    if (!values.remap_work_energy.is_zero()) balance.add(values.remap_work_energy.negated());
    return balance;
}
MacEnergyAccount correction_energy_account(const MacMomentumLimitedPair& p, bool left) {
    const double inertia = left ? p.inertia_left : p.inertia_right;
    MacEnergyAccount balance;
    balance.add(MacEnergy::kinetic(inertia, left ? p.after_left : p.after_right));
    balance.add(MacEnergy::kinetic(inertia, left ? p.before_left : p.before_right).negated());
    balance.add(left ? p.after_heat_left : p.after_heat_right);
    balance.add((left ? p.before_heat_left : p.before_heat_right).negated());
    balance.add(left ? p.energy_flux : p.energy_flux.negated());
    return balance;
}
void validate_face_energy(const MacFaceEnergy& values, bool composed = false) {
    require(values.mixing_heat.mantissa() >= 0 && values.wall_heat.mantissa() >= 0 && values.transported_kinetic.mantissa() >= 0,
        "Negative MAC transported kinetic energy or pending heat");
    const auto balance = face_energy_account(values);
    if (!composed) {
        if (!balance.within_bound()) precision("MAC local derived kinetic/heat account exceeds its operand bound");
        return;
    }
    MacEnergySum remaining; remaining.add(values.composed_energy_bound);
    for (std::size_t k = 0; k < balance.terms().size(); ++k)
        remaining.add(balance.terms().sign() < 0 ? balance.terms().component(k) : balance.terms().component(k).negated());
    if (remaining.sign() < 0) precision("MAC composed local kinetic/heat account exceeds its stage operand bounds");
}
void reconstruct_momentum(const MacGrid& grid, bool x_component, const std::vector<Neighbors>& faces,
    const std::vector<MacScaled>& old_velocity, std::vector<MacScaled>& momentum, MacFaceTransportReport& report,
    MacMomentumReconstruction reconstruction) {
    const auto count = faces.size(), width = x_component ? grid.x_width() : grid.width,
        height = x_component ? grid.height : grid.y_height();
    const auto is_free = [&](std::size_t i) { return i != absent && faces[i].left != absent && faces[i].right != absent; };
    const auto neighbor = [&](std::size_t i, std::size_t axis, bool positive) {
        const auto x = i % width, y = i / width;
        const auto coordinate = axis == 0 ? x : y, length = axis == 0 ? width : height;
        const auto periodic = axis == 0 ? grid.periodic_x : grid.periodic_y;
        if (!periodic && (positive ? coordinate + 1 == length : coordinate == 0)) return absent;
        const auto next = positive ? (coordinate + 1) % length : (coordinate + length - 1) % length;
        const auto target = axis == 0 ? next + y * width : x + next * width;
        return is_free(target) ? target : absent;
    };
    report.reconstruction = reconstruction; report.upwind_momentum = momentum;
    report.velocity_slope_x.resize(count); report.velocity_slope_y.resize(count);
    report.centered_courant_x.resize(count); report.centered_courant_y.resize(count);
    report.upwind_mixing_heat.resize(count); report.upwind_transported_kinetic.resize(count);
    std::vector<std::array<MacScaledSum, 2>> centered(count);
    std::vector<MacEnergySum> transported(count);
    std::vector<MacEnergySum> bounds(count);
    std::vector<MacEnergyAccount> storage(count);
    std::vector<std::uint8_t> changed(count);
    const auto axis_of = [&](const MacDualFlux& edge) -> std::size_t { return edge.left % width == edge.right % width ? 1U : 0U; };
    for (const auto& edge : report.fluxes) {
        const auto half = mac_dual_mass(edge).shifted(-1); const auto axis = axis_of(edge);
        centered[edge.left][axis].add(half); centered[edge.right][axis].add(half);
    }
    for (std::size_t i = 0; i < count; ++i) {
        report.upwind_mixing_heat[i] = report.energies[i].mixing_heat;
        report.upwind_transported_kinetic[i] = report.energies[i].transported_kinetic;
        transported[i].add(report.energies[i].transported_kinetic);
        bounds[i].add(report.energies[i].composed_energy_bound);
        storage[i].add(report.energies[i].transported_kinetic);
        if (!is_free(i)) continue;
        for (std::size_t axis = 0; axis < 2; ++axis) {
            auto& slope = axis == 0 ? report.velocity_slope_x[i] : report.velocity_slope_y[i];
            auto& courant = axis == 0 ? report.centered_courant_x[i] : report.centered_courant_y[i];
            const auto before = neighbor(i, axis, false), after = neighbor(i, axis, true);
            if (before != absent && after != absent)
                slope = limited_slope(mac_scaled_difference(old_velocity[i], old_velocity[before]), mac_scaled_difference(old_velocity[after], old_velocity[i]));
            courant = centered[i][axis].value().shared(1, report.old_inertia[i]);
        }
    }
    const auto one = MacScaled::from_double(1);
    for (std::size_t k = 0; k < report.fluxes.size(); ++k) {
        const auto& edge = report.fluxes[k]; const auto flux = mac_dual_mass(edge);
        if (flux.is_zero() || !is_free(edge.left) || !is_free(edge.right)) continue;
        const auto axis = axis_of(edge), donor = flux.mantissa() > 0 ? edge.left : edge.right,
            target = flux.mantissa() > 0 ? edge.right : edge.left;
        const auto slope = axis == 0 ? report.velocity_slope_x[donor] : report.velocity_slope_y[donor];
        MacScaledSum predicted; predicted.add(slope.multiplied(flux.mantissa() > 0 ? .5 : -.5));
        predicted.add(signed_product(report.centered_courant_x[donor], report.velocity_slope_x[donor]).multiplied(-.5));
        predicted.add(signed_product(report.centered_courant_y[donor], report.velocity_slope_y[donor]).multiplied(-.5));
        const auto delta = predicted.value(), difference = mac_scaled_difference(old_velocity[target], old_velocity[donor]);
        MacScaled bounded;
        if (!delta.is_zero() && !difference.is_zero() && (delta.mantissa() < 0) == (difference.mantissa() < 0))
            bounded = exceeds(delta.magnitude(), difference.magnitude()) ? difference : delta;
        const auto request = signed_product(flux, bounded);
        if (request.is_zero()) continue;
        const MacMomentumWorkBudget work_budget{transported[edge.left], transported[edge.right]};
        const auto limiter = reconstruction == MacMomentumReconstruction::quadratic_limited_linear ?
            limit_mac_momentum_pair_quadratic : limit_mac_momentum_pair;
        auto pair = limiter(report.new_inertia[edge.left], report.new_inertia[edge.right], momentum[edge.left], momentum[edge.right],
            report.energies[edge.left].mixing_heat, report.energies[edge.right].mixing_heat, request, &work_budget);
        momentum[edge.left] = pair.after_left; momentum[edge.right] = pair.after_right;
        report.energies[edge.left].mixing_heat = pair.after_heat_left; report.energies[edge.right].mixing_heat = pair.after_heat_right;
        transported[edge.left].add(pair.energy_flux.negated()); transported[edge.right].add(pair.energy_flux);
        if (!pair.impulse.is_zero()) {
            bounds[edge.left].add(correction_energy_account(pair, true).bound());
            bounds[edge.right].add(correction_energy_account(pair, false).bound());
            storage[edge.left].add(pair.energy_flux.negated()); storage[edge.right].add(pair.energy_flux);
            changed[edge.left] = changed[edge.right] = 1;
        }
        if (exceeds(one, pair.limiter)) ++report.limited_corrections;
        report.unresolved_momentum_corrections += pair.unresolved_momentum_updates;
        report.unresolved_remainder_corrections += pair.unresolved_heat_updates;
        report.corrections.push_back({k, axis, delta, bounded, std::move(pair)});
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (transported[i].sign() < 0) precision("MAC corrected transported kinetic energy is negative");
        report.energies[i].transported_kinetic = transported[i].value();
        if (transported[i].sign() != 0 && report.energies[i].transported_kinetic.is_zero())
            precision("MAC corrected transported energy lost its nonzero sign");
        report.energies[i].new_kinetic = MacEnergy::kinetic(report.new_inertia[i], momentum[i]);
        if (changed[i]) {
            storage[i].add(report.energies[i].transported_kinetic.negated());
            if (!storage[i].within_bound()) precision("MAC transported work storage exceeds its actual operand bound");
            bounds[i].add(storage[i].bound());
        }
        report.energies[i].composed_energy_bound = bounds[i].value();
    }
}
MacFaceTransportReport transport_faces(const MacGrid& grid, bool x_component, const std::vector<Neighbors>& faces,
    const std::vector<double>& m0, const std::vector<double>& m1, const std::vector<MacScaled>& j0,
    std::vector<MacScaled>& j1, std::vector<MacDualFlux> edges, MacMomentumReconstruction reconstruction,
    std::span<const double> dual_source_volumes, const std::vector<MacDualFlux>& volume_edges, double theta,
    const MacNodalConversion* conversion = nullptr) {
    const auto count = faces.size(); const auto keep = retained(m0, edges);
    MacFaceTransportReport report; report.old_inertia = m0; report.new_inertia = m1; report.fluxes = std::move(edges);
    report.energies.resize(count);
    for (auto* values : {&report.old_kinetic, &report.new_kinetic, &report.transported_kinetic, &report.mixing_heat,
        &report.wall_heat, &report.wall_impulse, &report.dual_mass_residual}) values->resize(count);
    std::vector<MacScaledSum> mass(count), momentum(count);
    std::vector<MacEnergySum> transported(count), heat(count);
    std::vector<MacScaled> velocity(count), provisional(count); report.full_wall_impulse.resize(count); report.full_dual_mass_residual.resize(count);
    const auto& carried_j=conversion ? conversion->momentum : j0;
    if (conversion) {
        report.nodal_conversions=conversion->events;report.nodal_source_momentum=conversion->momentum;report.nodal_source_work=conversion->work;
        report.unresolved_momentum_corrections+=conversion->unresolved_updates;
    }
    for (std::size_t i = 0; i < count; ++i) {
        velocity[i] = m0[i] == 0 ? MacScaled{} : carried_j[i].shared(1, m0[i]);
        report.energies[i].old_kinetic = MacEnergy::kinetic(m0[i], j0[i]); mass[i].add(keep[i]);
        if (m0[i] > 0) {
            momentum[i].add(carried_j[i].shared(keep[i], MacScaled::from_double(m0[i])));
            const auto kinetic=conversion ? MacEnergy::kinetic(m0[i],carried_j[i]) : report.energies[i].old_kinetic;
            transported[i].add(kinetic.shared(keep[i], MacScaled::from_double(m0[i])));
        }
    }
    for (const auto& edge : report.fluxes) if (!mac_dual_mass(edge).is_zero()) {
        const auto flux = mac_dual_mass(edge); const auto donor = flux.mantissa() > 0 ? edge.left : edge.right, target = flux.mantissa() > 0 ? edge.right : edge.left;
        const auto amount = flux.magnitude(); require(m0[donor] > 0, "MAC dual flux has no occupied donor");
        mass[target].add(amount); momentum[target].add(carried_j[donor].shared(amount, MacScaled::from_double(m0[donor])));
        const auto kinetic=conversion ? MacEnergy::kinetic(m0[donor],carried_j[donor]) : report.energies[donor].old_kinetic;
        transported[target].add(kinetic.shared(amount, MacScaled::from_double(m0[donor])));
    }
    j1.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        MacScaledAccount balance; for (std::size_t k = 0; k < mass[i].size(); ++k) balance.add(mass[i].component(k)); balance.add(-m1[i]);
        if (!balance.within_bound()) precision("MAC full dual mass account exceeds its operand bound");
        report.full_dual_mass_residual[i] = balance.residual(); report.dual_mass_residual[i] = balance.residual().project().value;
        const auto j = mac_scaled_primary(momentum[i]); provisional[i] = m1[i] == 0 ? MacScaled{} : j.shared(1, m1[i]);
        const bool fixed = faces[i].left == absent || faces[i].right == absent;
        j1[i] = fixed ? MacScaled{} : j;
        report.full_wall_impulse[i] = mac_scaled_difference(j1[i], j);
        report.wall_impulse[i] = report.full_wall_impulse[i].project().value;
        report.energies[i].wall_heat = fixed ? MacEnergy::kinetic(m1[i], j) : MacEnergy{};
        report.energies[i].new_kinetic = MacEnergy::kinetic(m1[i], j1[i]); report.energies[i].transported_kinetic = transported[i].value();
        heat[i].add(MacEnergy::variance(keep[i], mac_scaled_difference(velocity[i], provisional[i])));
    }
    for (const auto& edge : report.fluxes) if (!mac_dual_mass(edge).is_zero()) {
        const auto flux = mac_dual_mass(edge); const auto donor = flux.mantissa() > 0 ? edge.left : edge.right, target = flux.mantissa() > 0 ? edge.right : edge.left;
        heat[target].add(MacEnergy::variance(flux.magnitude(), mac_scaled_difference(velocity[donor], provisional[target])));
    }
    for (std::size_t i = 0; i < count; ++i) {
        report.energies[i].mixing_heat = heat[i].value();
        validate_face_energy(report.energies[i]);
        report.energies[i].composed_energy_bound = face_energy_account(report.energies[i]).bound();
    }
    if (reconstruction == MacMomentumReconstruction::swept_physical ||
        reconstruction == MacMomentumReconstruction::swept_energy_flux || reconstruction == MacMomentumReconstruction::swept_nodal_1d)
        reconstruct_mac_swept_momentum(grid, x_component, dual_source_volumes, volume_edges, velocity, j1, report, theta, reconstruction);
    else if (reconstruction != MacMomentumReconstruction::donor_cell) reconstruct_momentum(grid, x_component, faces, velocity, j1, report, reconstruction);
    for (std::size_t i = 0; i < count; ++i) {
        auto& values = report.energies[i]; validate_face_energy(values, reconstruction != MacMomentumReconstruction::donor_cell);
        const std::array<MacEnergy, 5> encoded{values.old_kinetic, values.new_kinetic, values.transported_kinetic, values.mixing_heat, values.wall_heat};
        const std::array<double*, 5> legacy{&report.old_kinetic[i], &report.new_kinetic[i], &report.transported_kinetic[i], &report.mixing_heat[i], &report.wall_heat[i]};
        for (std::size_t k = 0; k < encoded.size(); ++k) {
            const auto projection = encoded[k].project(); *legacy[k] = projection.value;
            if (projection.rounded) { values.rounded_projection_mask |= 1U << k; ++report.rounded_energy_projections; }
        }
    }
    return report;
}
}
MacFaceInertias mac_face_inertias(const SpeciesRegistry& registry, const MacTransportState& state) {
    const Geometry geometry(state.grid); const auto m = masses(registry, state);
    return {inertias(geometry.x, m), inertias(geometry.y, m)};
}
MacFaceInertias mac_face_inertias(const SpeciesRegistry& registry, const MacTransportState& state, const FluidThermodynamics& model,
    std::span<const double> volumes) {
    model.validate_registry(registry);
    const Geometry geometry(state.grid); const auto m = masses(registry, state, &model, volumes);
    return {inertias(geometry.x, m), inertias(geometry.y, m)};
}
namespace {
MacMassFluxes validate_parcels(const SpeciesRegistry& registry, const FluidThermodynamics& model, const MacTransportState& source,
    const Geometry& geometry, const MacParcelPlan& plan) {
    const auto count = source.cells.size(), species = registry.size();
    require(plan.retained.size() == count && plan.retained_volumes_m3.size() == count && plan.limiter.size() == count &&
        plan.source_volumes_m3.size() == count && plan.parcels.size() <= 16000000 / species, "MAC parcel storage shape/capacity differs");
    std::vector<MacScaled> flux_x(geometry.x.size()), flux_y(geometry.y.size());
    std::vector<std::uint8_t> seen_x(geometry.x.size()), seen_y(geometry.y.size());
    std::vector<std::array<const MacPrimalParcel*, 4>> outgoing(count);
    for (auto& row : outgoing) row.fill(nullptr);
    std::vector<MacScaledSum> incoming_volume(count);
    const auto physical = [&](const MacMaterialCell& cell, double volume, double scale_volume) {
        model.validate_inventory(cell.mixture); validate_passive_quantities(cell.passive_quantities);
        require(std::isfinite(volume) && volume >= 0, "Invalid MAC parcel volume");
        if (volume == 0) { require(cell.mixture.mass() == 0 && cell.mixture.energy == 0 && cell.passive_quantities.empty(), "Zero-volume MAC parcel is not empty"); return; }
        require(cell.mixture.mass() > 0 && scale_volume > 0, "MAC parcel has no occupied inventory");
        auto normalized = cell.mixture;
        for (auto& value : normalized.masses) value = share(value, scale_volume, volume);
        normalized.energy = share(normalized.energy, scale_volume, volume);
        const auto eos = model.evaluate(normalized, scale_volume);
        if (eos.vacuum_volume_m3 > 32 * eps * scale_volume) throw std::domain_error("MAC parcel contains unsupported internal void");
    };
    for (const auto& parcel : plan.parcels) {
        require(parcel.axis < 2 && parcel.donor < count && parcel.target < count && parcel.donor != parcel.target &&
            mac_parcel_volume(parcel).mantissa() > 0, "Invalid MAC parcel endpoint/volume");
        const auto& pairs = parcel.axis == 0 ? geometry.x : geometry.y;
        auto& seen = parcel.axis == 0 ? seen_x : seen_y; auto& values = parcel.axis == 0 ? flux_x : flux_y;
        require(parcel.face < pairs.size() && !seen[parcel.face], "MAC parcel face is duplicated/outside grid");
        const auto& pair = pairs[parcel.face];
        require(pair.left != absent && pair.right != absent && ((parcel.donor == pair.left && parcel.target == pair.right) ||
            (parcel.donor == pair.right && parcel.target == pair.left)), "MAC parcel does not cross its named open face");
        seen[parcel.face] = 1;
        const double reference = plan.source_volumes_m3[parcel.donor];
        model.validate_inventory(parcel.contents.mixture); validate_passive_quantities(parcel.contents.passive_quantities);
        if (parcel.reference_volume_m3 > 0) physical(parcel.contents, parcel.reference_volume_m3, parcel.reference_volume_m3);
        physical(mac_parcel_contents_at(parcel, reference), reference, reference);
        values[parcel.face] = mac_parcel_mass_scaled(parcel).multiplied(parcel.donor == pair.left ? 1 : -1);
        auto& slots = outgoing[parcel.donor]; auto slot = std::find(slots.begin(), slots.end(), nullptr);
        require(slot != slots.end(), "MAC donor has more than four face parcels"); *slot = &parcel;
        incoming_volume[parcel.target].add(mac_parcel_volume(parcel));
    }
    for (std::size_t i = 0; i < count; ++i) {
        require(std::isfinite(plan.limiter[i]) && plan.limiter[i] >= 0 && plan.limiter[i] <= 1, "Invalid MAC reconstruction limiter");
        physical(plan.retained[i], plan.retained_volumes_m3[i], plan.source_volumes_m3[i]);
        MacScaledAccount volume; volume.add(plan.retained_volumes_m3[i]);
        MacScaledAccount energy; energy.add(plan.retained[i].mixture.energy);
        std::vector<MacScaledAccount> mass(species); std::map<std::uint64_t, MacScaledAccount> metadata;
        for (std::size_t s = 0; s < species; ++s) mass[s].add(plan.retained[i].mixture.masses[s]);
        for (const auto& p : plan.retained[i].passive_quantities) metadata[p.identity].add(p.quantity);
        for (const auto* parcel : outgoing[i]) if (parcel) {
            volume.add(mac_parcel_volume(*parcel)); energy.add(mac_parcel_quantity(*parcel, parcel->contents.mixture.energy));
            for (std::size_t s = 0; s < species; ++s) mass[s].add(mac_parcel_quantity(*parcel, parcel->contents.mixture.masses[s]));
            for (const auto& p : parcel->contents.passive_quantities) metadata[p.identity].add(mac_parcel_quantity(*parcel, p.quantity));
        }
        volume.add(-plan.source_volumes_m3[i]); if (!volume.within_bound()) precision("MAC full donor volume account exceeds its operand bound");
        energy.add(-source.cells[i].mixture.energy);
        if (!energy.within_bound()) precision("MAC parcel donor energy account exceeds its operand bound");
        for (std::size_t s = 0; s < species; ++s) {
            mass[s].add(-source.cells[i].mixture.masses[s]);
            if (!mass[s].within_bound()) precision("MAC parcel donor constituent account exceeds its operand bound");
        }
        for (const auto& p : source.cells[i].passive_quantities) metadata[p.identity].add(-p.quantity);
        for (const auto& [id, value] : metadata) {
            static_cast<void>(id); if (!value.within_bound()) precision("MAC parcel donor passive account exceeds its operand bound");
        }
        MacScaledAccount target_volume;
        for (std::size_t k = 0; k < incoming_volume[i].size(); ++k) target_volume.add(incoming_volume[i].component(k));
        target_volume.add(plan.retained_volumes_m3[i]); target_volume.add(source.grid.walls[i] ? 0 : -source.grid.volume_m3());
        if (!target_volume.within_bound()) precision("MAC full target volume account exceeds its operand bound");
    }
    return mac_fluxes(flux_x, flux_y);
}
MacTransportAdvance transport_candidate(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux,
    const FluidThermodynamics* model, std::span<const double> source_volumes, const MacParcelPlan* plan,
    MacMomentumReconstruction reconstruction, double theta) {
    require_mac_energy_environment();
    require(reconstruction == MacMomentumReconstruction::donor_cell || reconstruction == MacMomentumReconstruction::limited_linear ||
        reconstruction == MacMomentumReconstruction::quadratic_limited_linear || reconstruction == MacMomentumReconstruction::swept_physical ||
        reconstruction == MacMomentumReconstruction::swept_energy_flux || reconstruction == MacMomentumReconstruction::swept_nodal_1d,
        "Invalid MAC momentum reconstruction mode");
    const bool physical = reconstruction == MacMomentumReconstruction::swept_physical ||
        reconstruction == MacMomentumReconstruction::swept_energy_flux || reconstruction == MacMomentumReconstruction::swept_nodal_1d;
    require(!physical || (model && source_volumes.size() == source.cells.size()), "Swept physical momentum requires EOS and every source volume");
    const Geometry geometry(source.grid); const auto m0 = masses(registry, source, model, source_volumes); const auto& g = source.grid;
    MacMassFluxes parcel_flux;
    if (plan) parcel_flux = validate_parcels(registry, *model, source, geometry, *plan);
    const auto& actual_flux = plan ? parcel_flux : flux;
    const auto flux_x = mac_field_values(actual_flux.x, actual_flux.full_x), flux_y = mac_field_values(actual_flux.y, actual_flux.full_y);
    const auto ix0 = inertias(geometry.x, m0), iy0 = inertias(geometry.y, m0);
    const auto jx0 = mac_field_values(source.momentum_x, source.full_momentum_x), jy0 = mac_field_values(source.momentum_y, source.full_momentum_y);
    validate_momentum(geometry.x, ix0, jx0); validate_momentum(geometry.y, iy0, jy0);
    std::vector<MacDualFlux> primal; primal_edges(geometry.x, flux_x, primal); primal_edges(geometry.y, flux_y, primal);
    MacMassFluxes volume_sweeps;
    if (physical) {
        std::vector<MacScaledSum> qx(flux_x.size()), qy(flux_y.size());
        if (plan) for (const auto& parcel : plan->parcels) {
            const auto& face = (parcel.axis == 0 ? geometry.x : geometry.y)[parcel.face];
            auto volume = mac_parcel_volume(parcel);
            if (parcel.donor != face.left) volume = volume.negated();
            (parcel.axis == 0 ? qx : qy)[parcel.face].add(volume);
        } else {
            const auto fill = [&](const auto& faces, const auto& values, auto& target) {
                for (std::size_t i = 0; i < faces.size(); ++i) if (!values[i].is_zero()) {
                    const auto donor = values[i].mantissa() > 0 ? faces[i].left : faces[i].right;
                    require(donor != absent, "Swept physical volume has no donor");
                    target[i].add(values[i].shared(source_volumes[donor], m0[donor]));
                }
            };
            fill(geometry.x, flux_x, qx); fill(geometry.y, flux_y, qy);
        }
        std::vector<MacScaled> x(qx.size()), y(qy.size());
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = mac_scaled_primary(qx[i]);
        for (std::size_t i = 0; i < y.size(); ++i) y[i] = mac_scaled_primary(qy[i]);
        volume_sweeps = mac_fluxes(x, y);
    }
    const auto keep = retained(m0, primal); const auto count = source.cells.size(), species = registry.size();
    MacTransportAdvance result{source, {}}; std::vector<MacScaledSum> mass(count * species), energy(count);
    std::vector<MacEnergySum> cell_heat(count);
    std::vector<std::map<std::uint64_t, MacScaledSum>> metadata(count);
    const auto transfer = [&](std::size_t donor, std::size_t target, MacScaled amount) {
        if (amount.is_zero()) return;
        const auto total = MacScaled::from_double(m0[donor]);
        for (std::size_t s = 0; s < species; ++s) mass[target * species + s].add(MacScaled::from_double(source.cells[donor].mixture.masses[s]).shared(amount, total));
        energy[target].add(MacScaled::from_double(source.cells[donor].mixture.energy).shared(amount, total));
        for (const auto& entry : source.cells[donor].passive_quantities) metadata[target][entry.identity].add(MacScaled::from_double(entry.quantity).shared(amount, total));
        require(metadata[target].size() <= 65537, "MAC target passive identity capacity exceeded");
    };
    if (plan) {
        const auto add = [&](std::size_t target, const MacMaterialCell& cell) {
            for (std::size_t s = 0; s < species; ++s) mass[target * species + s].add(cell.mixture.masses[s]);
            energy[target].add(cell.mixture.energy);
            for (const auto& p : cell.passive_quantities) metadata[target][p.identity].add(p.quantity);
            require(metadata[target].size() <= 65537, "MAC parcel target passive capacity exceeded");
        };
        for (std::size_t i = 0; i < count; ++i) add(i, plan->retained[i]);
        for (const auto& parcel : plan->parcels) {
            for (std::size_t s = 0; s < species; ++s) mass[parcel.target * species + s].add(mac_parcel_quantity(parcel, parcel.contents.mixture.masses[s]));
            energy[parcel.target].add(mac_parcel_quantity(parcel, parcel.contents.mixture.energy));
            for (const auto& p : parcel.contents.passive_quantities) metadata[parcel.target][p.identity].add(mac_parcel_quantity(parcel, p.quantity));
            require(metadata[parcel.target].size() <= 65537, "MAC scaled parcel target passive capacity exceeded");
            if (parcel.reference_volume_m3 != 0) ++result.report.scaled_primary_parcels;
        }
        result.report.primal_mass_fluxes_kg = actual_flux;
    } else {
        for (std::size_t i = 0; i < count; ++i) if (!g.walls[i]) transfer(i, i, keep[i]);
        for (const auto& edge : primal) {
            const auto amount = mac_dual_mass(edge); transfer(amount.mantissa() > 0 ? edge.left : edge.right,
                amount.mantissa() > 0 ? edge.right : edge.left, amount.magnitude());
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto& next = result.state.cells[i];
        for (std::size_t s = 0; s < species; ++s) next.mixture.masses[s] = store_mac_primary(mass[i * species + s], true);
        next.mixture.energy = store_mac_primary(energy[i], false); next.passive_quantities.clear();
        for (const auto& [identity, value] : metadata[i]) {
            const double stored = store_mac_primary(value, true);
            if (stored != 0) next.passive_quantities.push_back({identity, stored});
        }
    }
    if (reconstruction != MacMomentumReconstruction::donor_cell) {
        result.report.advected_internal_energy_j.reserve(count);
        for (const auto& cell : result.state.cells) result.report.advected_internal_energy_j.push_back(cell.mixture.energy);
    }
    const auto m1 = masses(registry, result.state, model, {}, !physical); auto& report = result.report;
    std::vector<MacScaled> jx1, jy1;
    const auto vx = physical ? inertias(geometry.x, std::vector<double>(source_volumes.begin(), source_volumes.end())) : std::vector<double>{};
    const auto vy = physical ? inertias(geometry.y, std::vector<double>(source_volumes.begin(), source_volumes.end())) : std::vector<double>{};
    std::optional<MacNodalConversion> nodal_x,nodal_y;
    if (reconstruction==MacMomentumReconstruction::swept_nodal_1d) {
        const auto qx=mac_field_values(volume_sweeps.x,volume_sweeps.full_x),qy=mac_field_values(volume_sweeps.y,volume_sweeps.full_y);
        const auto moves=[](const auto& values) {return std::any_of(values.begin(),values.end(),[](const auto& value){return !value.is_zero();});};
        const bool x=moves(qx),y=moves(qy);
        if ((x && y) || (x && moves(jy0)) || (y && moves(jx0)))
            throw FluidFatalError(FluidFatalCode::unsupported_topology,"Nodal conversion selector requires single-component one-dimensional motion");
        if (x) nodal_x=convert_mac_nodal_momentum_1d(g,true,m0,source_volumes,volume_sweeps,ix0,jx0,theta);
        if (y) nodal_y=convert_mac_nodal_momentum_1d(g,false,m0,source_volumes,volume_sweeps,iy0,jy0,theta);
    }
    if (nodal_x || nodal_y) {
        std::vector<MacEnergySum> source_work(count),advected_work(count);
        const auto gather=[&](const auto& pairs,const auto& inertia,const auto& conversion) {
            if (!conversion) return;
            for (std::size_t f=0;f<pairs.size();++f) if (!conversion->work[f].is_zero())
                for (const auto cell:{pairs[f].left,pairs[f].right}) if (cell!=absent)
                    source_work[cell].add(conversion->work[f].shared(.5*m0[cell],inertia[f]));
        };
        gather(geometry.x,ix0,nodal_x);gather(geometry.y,iy0,nodal_y);
        const auto carry=[&](std::size_t donor,std::size_t target,MacScaled amount) {
            if (amount.is_zero()) return;
            for (std::size_t k=0;k<source_work[donor].size();++k)
                advected_work[target].add(source_work[donor].component(k).shared(amount,MacScaled::from_double(m0[donor])));
        };
        for (std::size_t i=0;i<count;++i) carry(i,i,keep[i]);
        for (const auto& edge:primal) {
            const auto amount=mac_dual_mass(edge);const bool positive=amount.mantissa()>0;
            carry(positive?edge.left:edge.right,positive?edge.right:edge.left,amount.magnitude());
        }
        report.nodal_source_cell_work.resize(count);report.nodal_advected_work.resize(count);
        MacEnergyAccount balance;
        for (std::size_t i=0;i<count;++i) {
            report.nodal_source_cell_work[i]=source_work[i].value();report.nodal_advected_work[i]=advected_work[i].value();
            for (std::size_t k=0;k<source_work[i].size();++k) balance.add(source_work[i].component(k).negated());
            for (std::size_t k=0;k<advected_work[i].size();++k) {
                balance.add(advected_work[i].component(k));energy[i].add(advected_work[i].component(k));
            }
        }
        if (!balance.within_bound()) precision("Nodal source work changed during material transport");
    }
    report.x = transport_faces(g, true, geometry.x, ix0, inertias(geometry.x, m1), jx0, jx1, dual_edges(g, actual_flux, true), reconstruction,
        vx, physical ? dual_edges(g, volume_sweeps, true) : std::vector<MacDualFlux>{}, theta,nodal_x ? &*nodal_x : nullptr);
    report.y = transport_faces(g, false, geometry.y, iy0, inertias(geometry.y, m1), jy0, jy1, dual_edges(g, actual_flux, false), reconstruction,
        vy, physical ? dual_edges(g, volume_sweeps, false) : std::vector<MacDualFlux>{}, theta,nodal_y ? &*nodal_y : nullptr);
    report.physical_correction_fraction = theta;
    store_mac_field(result.state.momentum_x, result.state.full_momentum_x, jx1); store_mac_field(result.state.momentum_y, result.state.full_momentum_y, jy1);
    MacEnergyAccount total_energy; MacEnergySum mixing, wall, remap_work; MacScaledAccount px, py;
    std::vector<Sum> species_balance(species); std::map<std::uint64_t, Sum> passive_balance;
    const auto account_faces = [&](const std::vector<Neighbors>& faces, const MacFaceTransportReport& r,
        const std::vector<MacScaled>& j0, const std::vector<MacScaled>& j1, MacScaledAccount& momentum) {
        for (std::size_t i = 0; i < faces.size(); ++i) {
            momentum.add(j1[i]); momentum.add(j0[i].negated()); momentum.add(r.full_wall_impulse[i].negated());
            const auto& derived = r.energies[i];
            total_energy.add(derived.new_kinetic); total_energy.add(derived.old_kinetic.negated());
            mixing.add(derived.mixing_heat); wall.add(derived.wall_heat);
            MacEnergySum packet; packet.add(derived.mixing_heat); packet.add(derived.wall_heat);
            if (physical) { packet.add(derived.remap_work_energy); remap_work.add(derived.remap_work_energy); }
            const auto heat = packet.value(); const auto& pair = faces[i];
            const double total = (pair.left == absent ? 0 : m1[pair.left]) + (pair.right == absent ? 0 : m1[pair.right]);
            if (!heat.is_zero()) for (const auto cell : {pair.left, pair.right}) if (cell != absent) cell_heat[cell].add(heat.shared(m1[cell], total));
        }
    };
    account_faces(geometry.x, report.x, jx0, jx1, px);
    account_faces(geometry.y, report.y, jy0, jy1, py);
    for (std::size_t i = 0; i < count; ++i) {
        const double old = result.state.cells[i].mixture.energy; const auto heat = cell_heat[i].value();
        const auto conversion_work=report.nodal_advected_work.empty()?MacEnergy{}:report.nodal_advected_work[i];
        if (!heat.is_zero() || !conversion_work.is_zero()) {
            MacEnergySum updated;
            for (std::size_t k = 0; k < energy[i].size(); ++k) updated.add(energy[i].component(k));
            updated.add(heat);
            result.state.cells[i].mixture.energy = updated.value().project().value;
            MacEnergySum requested;requested.add(heat);requested.add(conversion_work);
            MacEnergySum difference; difference.add(result.state.cells[i].mixture.energy); difference.add(-old); difference.add(requested.value().negated());
            MacEnergySum allowance; allowance.add((physical ? requested.value().magnitude() : requested.value()).shifted(-47)); const int polarity = difference.sign();
            for (std::size_t k = 0; k < difference.size(); ++k) allowance.add(polarity < 0 ? difference.component(k) : difference.component(k).negated());
            if (allowance.sign() < 0) ++report.unresolved_heat_updates;
        }
        total_energy.add(result.state.cells[i].mixture.energy); total_energy.add(-source.cells[i].mixture.energy);
        for (std::size_t s = 0; s < species; ++s) { species_balance[s].add(result.state.cells[i].mixture.masses[s]); species_balance[s].add(-source.cells[i].mixture.masses[s]); }
        for (const auto& entry : result.state.cells[i].passive_quantities) passive_balance[entry.identity].add(entry.quantity);
        for (const auto& entry : source.cells[i].passive_quantities) passive_balance[entry.identity].add(-entry.quantity);
    }
    if (physical) {
        try { static_cast<void>(masses(registry, result.state, model)); }
        catch (const FluidFatalError&) { throw MacRemapCandidateRejected("Swept MAC completed candidate violates EOS domain/topology"); }
        catch (const ReferenceError&) { throw MacRemapCandidateRejected("Swept MAC completed candidate violates reference EOS domain"); }
        catch (const std::invalid_argument&) { throw MacRemapCandidateRejected("Swept MAC completed candidate violates EOS domain"); }
        catch (const std::domain_error&) { throw MacRemapCandidateRejected("Swept MAC completed candidate violates EOS domain"); }
        MacEnergySum entropy, allowance;
        for (std::size_t i = 0; i < count; ++i) if (!g.walls[i]) {
            const auto a = model->evaluate(source.cells[i].mixture, source_volumes[i]);
            const auto b = model->evaluate(result.state.cells[i].mixture, g.volume_m3());
            entropy.add(b.entropy_j_per_k); entropy.add(-a.entropy_j_per_k);
            allowance.add(MacEnergy::from_double(std::abs(source.cells[i].mixture.energy)).shared(1, a.temperature_k).shifted(-45));
            allowance.add(MacEnergy::from_double(std::abs(result.state.cells[i].mixture.energy)).shared(1, b.temperature_k).shifted(-45));
        }
        report.entropy_change_j_per_k = entropy.value(); report.entropy_allowance_j_per_k = allowance.value();
        for (std::size_t k = 0; k < allowance.size(); ++k) entropy.add(allowance.component(k));
        if (entropy.sign() < 0) throw MacRemapCandidateRejected("Swept MAC completed candidate decreases entropy beyond original stored-U bound");
    } else static_cast<void>(masses(registry, result.state, model));
    if (!total_energy.within_bound()) precision("MAC total wide energy account exceeds its operand bound");
    if (!px.within_bound() || !py.within_bound()) precision("MAC full momentum account exceeds its operand bound");
    report.rounded_energy_projections = report.x.rounded_energy_projections + report.y.rounded_energy_projections;
    report.energy_residual_wide_j = total_energy.residual(); report.energy_bound_wide_j = total_energy.bound();
    report.mixing_heat_wide_j = mixing.value(); report.wall_heat_wide_j = wall.value();
    report.remap_work_wide_j = remap_work.value();
    report.energy_residual_j = projected_energy(report.energy_residual_wide_j, report.rounded_energy_projections);
    report.energy_bound_j = projected_energy(report.energy_bound_wide_j, report.rounded_energy_projections);
    report.mixing_heat_j = projected_energy(report.mixing_heat_wide_j, report.rounded_energy_projections);
    report.wall_heat_j = projected_energy(report.wall_heat_wide_j, report.rounded_energy_projections);
    report.full_momentum_x_residual = px.residual(); report.full_momentum_y_residual = py.residual();
    report.momentum_x_residual = px.residual().project().value; report.momentum_y_residual = py.residual().project().value;
    for (const auto& balance : species_balance) { balance.check(); report.species_mass_residual.push_back(balance.value()); }
    for (const auto& [identity, balance] : passive_balance) { static_cast<void>(identity); balance.check(); }
    return result;
}
MacTransportAdvance transport(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux,
    const FluidThermodynamics* model, std::span<const double> source_volumes, const MacParcelPlan* plan,
    MacMomentumReconstruction reconstruction) {
    if (reconstruction != MacMomentumReconstruction::swept_physical &&
        reconstruction != MacMomentumReconstruction::swept_energy_flux && reconstruction != MacMomentumReconstruction::swept_nodal_1d)
        return transport_candidate(registry, source, flux, model, source_volumes, plan, reconstruction, 1);
    try { return transport_candidate(registry, source, flux, model, source_volumes, plan, reconstruction, 1); }
    catch (const MacRemapCandidateRejected&) { }
    // Both components, signed work deposition and canonical U/J are checked
    // together. A failed candidate is never published. Even theta=0 must pass
    // the same EOS/entropy/conservation tests; no assumed-safe fallback.
    auto accepted = transport_candidate(registry, source, flux, model, source_volumes, plan, reconstruction, 0);
    double lower = 0, upper = 1; std::size_t rejected = 1;
    for (std::size_t attempt = 0; attempt < 53; ++attempt) {
        const double theta = lower + .5 * (upper-lower);
        if (theta == lower || theta == upper) break;
        try {
            auto candidate = transport_candidate(registry, source, flux, model, source_volumes, plan, reconstruction, theta);
            accepted = std::move(candidate); lower = theta;
        } catch (const MacRemapCandidateRejected&) { upper = theta; ++rejected; }
    }
    accepted.report.physical_admission_backtracks = rejected;
    return accepted;
}
}
MacTransportAdvance advance_mac_transport(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux,
    MacMomentumReconstruction reconstruction) {
    return transport(registry, source, flux, nullptr, {}, nullptr, reconstruction);
}
MacTransportAdvance advance_mac_transport(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux,
    const FluidThermodynamics& model, std::span<const double> volumes, MacMomentumReconstruction reconstruction) {
    model.validate_registry(registry);
    require(volumes.size() == source.cells.size(), "MAC EOS transport requires every source volume");
    return transport(registry, source, flux, &model, volumes, nullptr, reconstruction);
}
MacTransportAdvance advance_mac_parcel_transport(const SpeciesRegistry& registry, const MacTransportState& source,
    const FluidThermodynamics& model, const MacParcelPlan& plan, MacMomentumReconstruction reconstruction) {
    model.validate_registry(registry);
    return transport(registry, source, {}, &model, plan.source_volumes_m3, &plan, reconstruction);
}
}
