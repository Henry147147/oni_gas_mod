#include "mixed_atmospheres/mac_transport.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

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
double kinetic(double inertia, double momentum) {
    if (momentum == 0) return 0;
    require(inertia > 0, "Momentum on an inactive MAC face");
    const double result = .5 * momentum * (momentum / inertia);
    if (!(result > 0) || !std::isfinite(result)) precision("MAC kinetic energy is outside representable range");
    return result;
}
double variance(double mass, double difference) {
    if (mass == 0 || difference == 0) return 0;
    int em = 0, eu = 0;
    const double m = std::frexp(mass, &em), u = std::frexp(difference, &eu);
    const double result = std::ldexp(.5 * m * u * u, em + 2 * eu);
    if (!(result > 0) || !std::isfinite(result)) precision("Positive MAC mixing heat is outside representable range");
    return result;
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
    const FluidThermodynamics* model = nullptr, std::span<const double> volumes = {}) {
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
            if (model) {
                const double volume = volumes.empty() ? g.volume_m3() : volumes[i];
                const auto state_eos = model->evaluate(cell.mixture, volume);
                if (state_eos.vacuum_volume_m3 > 32 * eps * volume)
                    throw FluidFatalError(FluidFatalCode::unsupported_topology, "MAC occupied EOS cell contains internal void");
            } else {
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
void validate_momentum(const std::vector<Neighbors>& faces, const std::vector<double>& inertia, const std::vector<double>& momentum) {
    require(momentum.size() == faces.size(), "MAC momentum shape differs");
    for (std::size_t i = 0; i < faces.size(); ++i) {
        require_finite(momentum[i], "MAC canonical momentum");
        if (faces[i].left == absent || faces[i].right == absent) require(momentum[i] == 0, "Fixed MAC normal face has momentum");
        static_cast<void>(kinetic(inertia[i], momentum[i]));
    }
}
void primal_edges(const std::vector<Neighbors>& faces, const std::vector<double>& flux, std::vector<MacDualFlux>& edges) {
    require(flux.size() == faces.size(), "MAC primal flux shape differs");
    for (std::size_t i = 0; i < faces.size(); ++i) {
        require_finite(flux[i], "MAC primal mass flux"); const auto& face = faces[i];
        if (face.left == absent || face.right == absent) require(flux[i] == 0, "MAC mass flux crosses a solid/exterior boundary");
        else if (flux[i] != 0) edges.push_back({face.left, face.right, flux[i]});
    }
}
std::vector<MacDualFlux> dual_edges(const MacGrid& g, const MacMassFluxes& f, bool x_component) {
    std::vector<MacDualFlux> result; result.reserve(2 * g.width * g.height + g.width + g.height);
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) if (!g.walls[x + y * g.width]) {
        if (x_component) {
            const auto l = g.x_face(x, y), r = g.x_face((x + 1) % g.x_width(), y);
            result.push_back({l, r, std::midpoint(f.x[l], f.x[r])});
        } else {
            const auto b = g.y_face(x, y), t = g.y_face(x, (y + 1) % g.y_height());
            result.push_back({b, t, std::midpoint(f.y[b], f.y[t])});
        }
    }
    if (x_component) {
        for (std::size_t y = g.periodic_y ? 0 : 1; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
            double left = 0, right = 0;
            if (x > 0 || g.periodic_x) left = f.y[g.y_face((x + g.width - 1) % g.width, y)];
            if (x < g.width) right = f.y[g.y_face(x, y)];
            result.push_back({g.x_face(x, (y + g.height - 1) % g.height), g.x_face(x, y), std::midpoint(left, right)});
        }
    } else {
        for (std::size_t x = g.periodic_x ? 0 : 1; x < g.width; ++x) for (std::size_t y = 0; y < g.y_height(); ++y) {
            double bottom = 0, top = 0;
            if (y > 0 || g.periodic_y) bottom = f.x[g.x_face(x, (y + g.height - 1) % g.height)];
            if (y < g.height) top = f.x[g.x_face(x, y)];
            result.push_back({g.y_face((x + g.width - 1) % g.width, y), g.y_face(x, y), std::midpoint(bottom, top)});
        }
    }
    return result;
}
std::vector<double> retained(const std::vector<double>& m, const std::vector<MacDualFlux>& edges) {
    std::vector<Sum> outgoing(m.size());
    for (const auto& edge : edges) if (edge.mass_kg != 0) outgoing[edge.mass_kg > 0 ? edge.left : edge.right].add(std::abs(edge.mass_kg));
    std::vector<double> result(m.size());
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (outgoing[i].value() > m[i]) throw std::domain_error("MAC outgoing mass CFL exceeded");
        result[i] = m[i] - outgoing[i].value();
    }
    return result;
}
MacFaceTransportReport transport_faces(const std::vector<Neighbors>& faces, const std::vector<double>& m0, const std::vector<double>& m1,
    const std::vector<double>& j0, std::vector<double>& j1, std::vector<MacDualFlux> edges) {
    const auto count = faces.size(); const auto keep = retained(m0, edges);
    MacFaceTransportReport report; report.old_inertia = m0; report.new_inertia = m1; report.fluxes = std::move(edges);
    for (auto* values : {&report.old_kinetic, &report.new_kinetic, &report.transported_kinetic, &report.mixing_heat,
        &report.wall_heat, &report.wall_impulse, &report.dual_mass_residual}) values->resize(count);
    std::vector<Sum> mass(count), momentum(count), transported(count), heat(count); std::vector<double> velocity(count), provisional(count);
    for (std::size_t i = 0; i < count; ++i) {
        velocity[i] = m0[i] == 0 ? 0 : j0[i] / m0[i]; require_finite(velocity[i], "MAC source velocity");
        report.old_kinetic[i] = kinetic(m0[i], j0[i]); mass[i].add(keep[i]);
        if (m0[i] > 0) { momentum[i].add(share(j0[i], keep[i], m0[i])); transported[i].add(share(report.old_kinetic[i], keep[i], m0[i])); }
    }
    for (const auto& edge : report.fluxes) if (edge.mass_kg != 0) {
        const auto donor = edge.mass_kg > 0 ? edge.left : edge.right, target = edge.mass_kg > 0 ? edge.right : edge.left;
        const auto amount = std::abs(edge.mass_kg); require(m0[donor] > 0, "MAC dual flux has no occupied donor");
        mass[target].add(amount); momentum[target].add(share(j0[donor], amount, m0[donor]));
        transported[target].add(share(report.old_kinetic[donor], amount, m0[donor]));
    }
    j1.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto balance = mass[i]; balance.add(-m1[i]); balance.check(); report.dual_mass_residual[i] = balance.value();
        const auto j = momentum[i].value(); provisional[i] = m1[i] == 0 ? 0 : j / m1[i];
        require_finite(provisional[i], "MAC transported velocity");
        j1[i] = faces[i].left == absent || faces[i].right == absent ? 0 : j;
        report.wall_impulse[i] = j1[i] - j; report.wall_heat[i] = j1[i] == j ? 0 : kinetic(m1[i], j);
        report.new_kinetic[i] = kinetic(m1[i], j1[i]); report.transported_kinetic[i] = transported[i].value();
        heat[i].add(variance(keep[i], velocity[i] - provisional[i]));
    }
    for (const auto& edge : report.fluxes) if (edge.mass_kg != 0) {
        const auto donor = edge.mass_kg > 0 ? edge.left : edge.right, target = edge.mass_kg > 0 ? edge.right : edge.left;
        heat[target].add(variance(std::abs(edge.mass_kg), velocity[donor] - provisional[target]));
    }
    for (std::size_t i = 0; i < count; ++i) report.mixing_heat[i] = heat[i].value();
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
    MacMassFluxes flux{std::vector<double>(geometry.x.size()), std::vector<double>(geometry.y.size())};
    std::vector<std::uint8_t> seen_x(geometry.x.size()), seen_y(geometry.y.size());
    std::vector<std::array<const MacPrimalParcel*, 4>> outgoing(count);
    for (auto& row : outgoing) row.fill(nullptr);
    std::vector<Sum> incoming_volume(count);
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
            std::isfinite(parcel.volume_m3) && parcel.volume_m3 > 0, "Invalid MAC parcel endpoint/volume");
        const auto& pairs = parcel.axis == 0 ? geometry.x : geometry.y;
        auto& seen = parcel.axis == 0 ? seen_x : seen_y; auto& values = parcel.axis == 0 ? flux.x : flux.y;
        require(parcel.face < pairs.size() && !seen[parcel.face], "MAC parcel face is duplicated/outside grid");
        const auto& pair = pairs[parcel.face];
        require(pair.left != absent && pair.right != absent && ((parcel.donor == pair.left && parcel.target == pair.right) ||
            (parcel.donor == pair.right && parcel.target == pair.left)), "MAC parcel does not cross its named open face");
        seen[parcel.face] = 1; physical(parcel.contents, parcel.volume_m3, plan.source_volumes_m3[parcel.donor]);
        values[parcel.face] = (parcel.donor == pair.left ? 1 : -1) * parcel.contents.mixture.mass();
        auto& slots = outgoing[parcel.donor]; auto slot = std::find(slots.begin(), slots.end(), nullptr);
        require(slot != slots.end(), "MAC donor has more than four face parcels"); *slot = &parcel;
        incoming_volume[parcel.target].add(parcel.volume_m3);
    }
    for (std::size_t i = 0; i < count; ++i) {
        require(std::isfinite(plan.limiter[i]) && plan.limiter[i] >= 0 && plan.limiter[i] <= 1, "Invalid MAC reconstruction limiter");
        physical(plan.retained[i], plan.retained_volumes_m3[i], plan.source_volumes_m3[i]);
        Sum volume, energy; volume.add(plan.retained_volumes_m3[i]); energy.add(plan.retained[i].mixture.energy);
        std::vector<Sum> mass(species); std::map<std::uint64_t, Sum> metadata;
        for (std::size_t s = 0; s < species; ++s) mass[s].add(plan.retained[i].mixture.masses[s]);
        for (const auto& p : plan.retained[i].passive_quantities) metadata[p.identity].add(p.quantity);
        for (const auto* parcel : outgoing[i]) if (parcel) {
            volume.add(parcel->volume_m3); energy.add(parcel->contents.mixture.energy);
            for (std::size_t s = 0; s < species; ++s) mass[s].add(parcel->contents.mixture.masses[s]);
            for (const auto& p : parcel->contents.passive_quantities) metadata[p.identity].add(p.quantity);
        }
        volume.add(-plan.source_volumes_m3[i]); volume.check(); energy.add(-source.cells[i].mixture.energy); energy.check();
        for (std::size_t s = 0; s < species; ++s) { mass[s].add(-source.cells[i].mixture.masses[s]); mass[s].check(); }
        for (const auto& p : source.cells[i].passive_quantities) metadata[p.identity].add(-p.quantity);
        for (const auto& [id, value] : metadata) { static_cast<void>(id); value.check(); }
        auto target_volume = incoming_volume[i]; target_volume.add(plan.retained_volumes_m3[i]);
        target_volume.add(source.grid.walls[i] ? 0 : -source.grid.volume_m3()); target_volume.check();
    }
    return flux;
}
MacTransportAdvance transport(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux,
    const FluidThermodynamics* model, std::span<const double> source_volumes, const MacParcelPlan* plan = nullptr) {
    const Geometry geometry(source.grid); const auto m0 = masses(registry, source, model, source_volumes); const auto& g = source.grid;
    MacMassFluxes parcel_flux;
    if (plan) parcel_flux = validate_parcels(registry, *model, source, geometry, *plan);
    const auto& actual_flux = plan ? parcel_flux : flux;
    const auto ix0 = inertias(geometry.x, m0), iy0 = inertias(geometry.y, m0);
    validate_momentum(geometry.x, ix0, source.momentum_x); validate_momentum(geometry.y, iy0, source.momentum_y);
    std::vector<MacDualFlux> primal; primal_edges(geometry.x, actual_flux.x, primal); primal_edges(geometry.y, actual_flux.y, primal);
    const auto keep = retained(m0, primal); const auto count = source.cells.size(), species = registry.size();
    MacTransportAdvance result{source, {}}; std::vector<Sum> mass(count * species), energy(count), cell_heat(count);
    std::vector<std::map<std::uint64_t, Sum>> metadata(count);
    const auto transfer = [&](std::size_t donor, std::size_t target, double amount) {
        if (amount == 0) return;
        for (std::size_t s = 0; s < species; ++s) mass[target * species + s].add(share(source.cells[donor].mixture.masses[s], amount, m0[donor]));
        energy[target].add(share(source.cells[donor].mixture.energy, amount, m0[donor]));
        for (const auto& entry : source.cells[donor].passive_quantities) metadata[target][entry.identity].add(share(entry.quantity, amount, m0[donor]));
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
        for (const auto& parcel : plan->parcels) add(parcel.target, parcel.contents);
        result.report.primal_mass_fluxes_kg = actual_flux;
    } else {
        for (std::size_t i = 0; i < count; ++i) if (!g.walls[i]) transfer(i, i, keep[i]);
        for (const auto& edge : primal) transfer(edge.mass_kg > 0 ? edge.left : edge.right,
            edge.mass_kg > 0 ? edge.right : edge.left, std::abs(edge.mass_kg));
    }
    for (std::size_t i = 0; i < count; ++i) {
        auto& next = result.state.cells[i];
        for (std::size_t s = 0; s < species; ++s) next.mixture.masses[s] = mass[i * species + s].value();
        next.mixture.energy = energy[i].value(); next.passive_quantities.clear();
        for (const auto& [identity, value] : metadata[i]) next.passive_quantities.push_back({identity, value.value()});
    }
    const auto m1 = masses(registry, result.state, model); auto& report = result.report;
    report.x = transport_faces(geometry.x, ix0, inertias(geometry.x, m1), source.momentum_x, result.state.momentum_x, dual_edges(g, actual_flux, true));
    report.y = transport_faces(geometry.y, iy0, inertias(geometry.y, m1), source.momentum_y, result.state.momentum_y, dual_edges(g, actual_flux, false));
    Sum total_energy, px, py, mixing, wall; std::vector<Sum> species_balance(species); std::map<std::uint64_t, Sum> passive_balance;
    const auto account_faces = [&](const std::vector<Neighbors>& faces, const MacFaceTransportReport& r,
        const std::vector<double>& j0, const std::vector<double>& j1, Sum& momentum) {
        for (std::size_t i = 0; i < faces.size(); ++i) {
            momentum.add(j1[i]); momentum.add(-j0[i]); momentum.add(-r.wall_impulse[i]);
            total_energy.add(r.new_kinetic[i]); total_energy.add(-r.old_kinetic[i]);
            mixing.add(r.mixing_heat[i]); wall.add(r.wall_heat[i]);
            const double heat = r.mixing_heat[i] + r.wall_heat[i]; const auto& pair = faces[i];
            const double total = (pair.left == absent ? 0 : m1[pair.left]) + (pair.right == absent ? 0 : m1[pair.right]);
            if (heat != 0) for (const auto cell : {pair.left, pair.right}) if (cell != absent) cell_heat[cell].add(share(heat, m1[cell], total));
        }
    };
    account_faces(geometry.x, report.x, source.momentum_x, result.state.momentum_x, px);
    account_faces(geometry.y, report.y, source.momentum_y, result.state.momentum_y, py);
    for (std::size_t i = 0; i < count; ++i) {
        const double old = result.state.cells[i].mixture.energy; energy[i].add(cell_heat[i].value()); result.state.cells[i].mixture.energy = energy[i].value();
        if (std::abs((result.state.cells[i].mixture.energy - old) - cell_heat[i].value()) > 32 * eps * cell_heat[i].value()) ++report.unresolved_heat_updates;
        total_energy.add(result.state.cells[i].mixture.energy); total_energy.add(-source.cells[i].mixture.energy);
        for (std::size_t s = 0; s < species; ++s) { species_balance[s].add(result.state.cells[i].mixture.masses[s]); species_balance[s].add(-source.cells[i].mixture.masses[s]); }
        for (const auto& entry : result.state.cells[i].passive_quantities) passive_balance[entry.identity].add(entry.quantity);
        for (const auto& entry : source.cells[i].passive_quantities) passive_balance[entry.identity].add(-entry.quantity);
    }
    static_cast<void>(masses(registry, result.state, model)); total_energy.check(); px.check(); py.check();
    report.energy_residual_j = total_energy.value(); report.energy_bound_j = total_energy.bound();
    report.momentum_x_residual = px.value(); report.momentum_y_residual = py.value(); report.mixing_heat_j = mixing.value(); report.wall_heat_j = wall.value();
    for (const auto& balance : species_balance) { balance.check(); report.species_mass_residual.push_back(balance.value()); }
    for (const auto& [identity, balance] : passive_balance) { static_cast<void>(identity); balance.check(); }
    return result;
}
}
MacTransportAdvance advance_mac_transport(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux) {
    return transport(registry, source, flux, nullptr, {});
}
MacTransportAdvance advance_mac_transport(const SpeciesRegistry& registry, const MacTransportState& source, const MacMassFluxes& flux,
    const FluidThermodynamics& model, std::span<const double> volumes) {
    model.validate_registry(registry);
    require(volumes.size() == source.cells.size(), "MAC EOS transport requires every source volume");
    return transport(registry, source, flux, &model, volumes);
}
MacTransportAdvance advance_mac_parcel_transport(const SpeciesRegistry& registry, const MacTransportState& source,
    const FluidThermodynamics& model, const MacParcelPlan& plan) {
    model.validate_registry(registry);
    return transport(registry, source, {}, &model, plan.source_volumes_m3, &plan);
}
}
