#include "mac_interval_kernel.hpp"
#include "mac_interval_audit.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

namespace ma::experimental {
namespace {
using interval_detail::Sum;
using interval_detail::StoredSum;
constexpr auto none = std::numeric_limits<std::size_t>::max();
constexpr double eps = std::numeric_limits<double>::epsilon();
struct Decline {};
// This first port admits normal-range occupied ideal gases. A flagged value
// restarts the entire interval through the retained range implementation.
double normal(double v) {
    if (!std::isfinite(v) || (v != 0 && (std::abs(v) < 0x1p-450 || std::abs(v) > 0x1p450))) throw Decline{};
    return v;
}
double product(double a, double b) {
    const double v = normal(a * b);
    if (v == 0 && a != 0 && b != 0) throw Decline{};
    return v;
}
struct Rounding { std::size_t rounded = 0, unresolved = 0; };
double primary(const StoredSum& s, double before, Rounding& rounding) {
    const double after = normal(s.value());
    if (s.lo != 0 || s.discarded) ++rounding.rounded;
    if (after == before) {
        Sum change = s; change.add(-before);
        if (change.hi != 0 || change.lo != 0 || s.discarded) ++rounding.unresolved;
    }
    return after;
}
struct Face { std::size_t left = none, right = none; bool free() const { return left != none && right != none; } };
struct DualEdge { std::size_t left, right, axis, a, b; }; // absent primal operand is exact zero
struct Geometry {
    const MacGrid& g;
    std::array<std::vector<Face>, 2> faces;
    std::array<std::vector<DualEdge>, 2> dual;
    std::vector<std::array<std::size_t, 4>> cell_faces;
    explicit Geometry(const MacGrid& grid) : g(grid), cell_faces(g.walls.size()) {
        const auto fluid = [&](std::size_t i) { return g.walls[i] ? none : i; };
        faces[0].resize(g.x_width() * g.height); faces[1].resize(g.width * g.y_height());
        for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
            auto& f = faces[0][g.x_face(x, y)];
            if (x > 0 || g.periodic_x) f.left = fluid((x + g.width - 1) % g.width + y * g.width);
            if (x < g.width) f.right = fluid(x + y * g.width);
        }
        for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x) {
            auto& f = faces[1][g.y_face(x, y)];
            if (y > 0 || g.periodic_y) f.left = fluid(x + ((y + g.height - 1) % g.height) * g.width);
            if (y < g.height) f.right = fluid(x + y * g.width);
        }
        for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
            const auto i = x + y * g.width;
            const auto l = g.x_face(x, y), r = g.x_face((x + 1) % g.x_width(), y);
            const auto b = g.y_face(x, y), t = g.y_face(x, (y + 1) % g.y_height());
            cell_faces[i] = {l, r, b, t};
            if (!g.walls[i]) {
                dual[0].push_back({l, r, 0, l, r});
                dual[1].push_back({b, t, 1, b, t});
            }
        }
        for (std::size_t y = g.periodic_y ? 0 : 1; y < g.height; ++y)
            for (std::size_t x = 0; x < g.x_width(); ++x)
                dual[0].push_back({g.x_face(x, (y + g.height - 1) % g.height), g.x_face(x, y), 1,
                    x > 0 || g.periodic_x ? g.y_face((x + g.width - 1) % g.width, y) : none,
                    x < g.width ? g.y_face(x, y) : none});
        for (std::size_t x = g.periodic_x ? 0 : 1; x < g.width; ++x)
            for (std::size_t y = 0; y < g.y_height(); ++y)
                dual[1].push_back({g.y_face((x + g.width - 1) % g.width, y), g.y_face(x, y), 0,
                    y > 0 || g.periodic_y ? g.x_face(x, (y + g.height - 1) % g.height) : none,
                    y < g.height ? g.x_face(x, y) : none});
    }
};
struct Gas { std::size_t species; double cv, offset, molar, low, high; };
struct Fields {
    std::vector<std::vector<double>> q; // present species, present passive IDs, U
    std::array<std::vector<double>, 2> j;
    Fields(std::size_t columns, std::size_t cells, const Geometry& topology) : q(columns, std::vector<double>(cells)) {
        for (std::size_t a = 0; a < 2; ++a) j[a].resize(topology.faces[a].size());
    }
    std::vector<double>& u() { return q.back(); }
    const std::vector<double>& u() const { return q.back(); }
};
struct Reading {
    double mass = 0, capacity = 0, gas_heat = 0, offset = 0, thermal = 0, temperature = 0, pressure = 0, gamma = 0;
    double minimum_temperature = 0, maximum_temperature = 0;
};
struct Kernel {
    const MacTransportState& source;
    const MacClosedSettings& settings;
    MacIntervalCache& cache;
    Geometry topology;
    std::vector<Gas> gases;
    std::vector<std::uint64_t> passive_ids;
    std::array<Fields, 2> banks;
    std::vector<Reading> initial, current;
    std::vector<double> dv, trial, pressure, increment, compliance, residual, x, r, z, direction, applied, diagonal;
    std::vector<double> material_volume, pressure_u, next_mass;
    std::array<std::vector<double>, 2> inertia, conductance, sweep, pressure_j, mass_flux, scalar_flux;
    std::vector<double> keep, velocity, provisional, dual_flux;
    std::vector<StoredSum> gathered, heat;
    mutable std::vector<Sum> dot_blocks;
    std::array<MacEnergySum, 2> wall;
    Sum mixing_heat, wall_heat;
    Rounding rounding;
    MacClosedAdvanceReport report;
    std::size_t selected = 0;
    std::size_t active_cells = 0, topology_builds = 0;
    std::unique_ptr<interval_detail::PressureMultigrid> multigrid;

    static std::vector<Gas> gas_models(const SpeciesRegistry& registry, const FluidThermodynamics& model,
        const MacTransportState& input) {
        std::vector<Gas> result;
        for (std::size_t s = 0; s < registry.size(); ++s) {
            bool present = false;
            for (const auto& c : input.cells) if (c.mixture.masses[s] > 0) { present = true; break; }
            if (!present) continue;
            const auto& materials = model.reference().materials();
            const auto found = std::find_if(materials.begin(), materials.end(), [&](const auto& m) { return m.id == registry[s].id; });
            if (found == materials.end() || found->phase != ReferencePhase::ideal_gas) throw Decline{};
            normal(found->cv_j_per_kg_k); normal(found->energy_offset_j_per_kg); normal(found->molar_mass_kg_per_mol);
            result.push_back({s, found->cv_j_per_kg_k, found->energy_offset_j_per_kg,
                found->molar_mass_kg_per_mol, found->minimum_temperature_k, found->maximum_temperature_k});
        }
        if (result.empty()) throw Decline{};
        return result;
    }
    static std::vector<std::uint64_t> ids(const MacTransportState& input) {
        std::vector<std::uint64_t> result;
        for (const auto& c : input.cells) for (const auto& p : c.passive_quantities) result.push_back(p.identity);
        std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
    Kernel(const SpeciesRegistry& registry, const FluidThermodynamics& model, const MacTransportState& input,
        const MacClosedSettings& config, MacIntervalCache& cached) : source(input), settings(config), cache(cached), topology(input.grid),
        gases(gas_models(registry, model, input)), passive_ids(ids(input)),
        banks{Fields(gases.size() + passive_ids.size() + 1, input.cells.size(), topology),
              Fields(gases.size() + passive_ids.size() + 1, input.cells.size(), topology)} {
        if (!source.full_momentum_x.empty() || !source.full_momentum_y.empty()) throw Decline{};
        normal(source.grid.volume_m3()); normal(source.grid.spacing_m * source.grid.depth_m);
        active_cells = source.grid.walls.size() - static_cast<std::size_t>(std::count(source.grid.walls.begin(), source.grid.walls.end(), std::uint8_t{1}));
        const auto n = input.cells.size(); initial.resize(n); current.resize(n); heat.resize(n);
        dot_blocks.resize((n + 255) / 256);
        for (auto* v : {&dv, &trial, &pressure, &increment, &compliance, &residual, &x, &r, &z, &direction, &applied,
            &diagonal, &material_volume, &pressure_u, &next_mass}) v->resize(n);
        for (std::size_t a = 0; a < 2; ++a) {
            const auto count = topology.faces[a].size();
            for (auto* v : {&inertia[a], &conductance[a], &sweep[a], &pressure_j[a], &mass_flux[a], &scalar_flux[a]}) v->resize(count);
        }
        const auto count = std::max(topology.faces[0].size(), topology.faces[1].size());
        for (auto* v : {&keep, &velocity, &provisional}) v->resize(count);
        gathered.resize(count); dual_flux.resize(std::max(topology.dual[0].size(), topology.dual[1].size()));
        pack(); initial = current;
    }
    double original(std::size_t column, std::size_t cell) const {
        const auto& c = source.cells[cell];
        if (column < gases.size()) return c.mixture.masses[gases[column].species];
        if (column == gases.size() + passive_ids.size()) return c.mixture.energy;
        const auto id = passive_ids[column - gases.size()];
        const auto p = std::lower_bound(c.passive_quantities.begin(), c.passive_quantities.end(), id,
            [](const auto& entry, auto value) { return entry.identity < value; });
        return p != c.passive_quantities.end() && p->identity == id ? p->quantity : 0;
    }
    void pack() {
        selected = 0;
        auto& f = banks[0];
        for (std::size_t k = 0; k < f.q.size(); ++k)
            for (std::size_t i = 0; i < source.cells.size(); ++i) f.q[k][i] = normal(original(k, i));
        f.j[0] = source.momentum_x; f.j[1] = source.momentum_y;
        for (const auto& j : f.j) for (const auto value : j) normal(value);
        readings(f, current);
    }
    Reading reading(const Fields& f, std::size_t i) const {
        Reading out; if (source.grid.walls[i]) return out;
        Sum c, gas, offset;
        double low = 0, high = std::numeric_limits<double>::max();
        for (std::size_t k = 0; k < gases.size(); ++k) {
            const double m = normal(f.q[k][i]); if (m < 0) throw std::domain_error("Interval kernel negative constituent");
            if (m == 0) continue;
            const auto& material = gases[k]; out.mass += m;
            c.add(product(m, material.cv)); offset.add(product(m, material.offset));
            gas.add(product(normal(m / material.molar), reference_gas_constant));
            low = std::max(low, material.low); high = std::min(high, material.high);
        }
        if (!(out.mass > 0)) throw Decline{};
        out.capacity = normal(c.value()); out.gas_heat = normal(gas.value()); out.offset = normal(offset.value());
        Sum thermal; thermal.add(f.u()[i]); thermal.add(-offset.hi); thermal.add(-offset.lo);
        out.thermal = normal(thermal.value());
        if (!(out.thermal > 0)) throw std::domain_error("Interval kernel nonpositive thermal energy");
        if (out.thermal < 0x1p-30 * (std::abs(f.u()[i]) + std::abs(out.offset))) throw Decline{};
        out.temperature = normal(out.thermal / out.capacity);
        out.minimum_temperature = low; out.maximum_temperature = high;
        if (out.temperature - low <= 64 * eps * std::max(low, out.temperature) ||
            high - out.temperature <= 64 * eps * std::max(high, out.temperature)) throw Decline{};
        out.pressure = normal(product(out.gas_heat, out.temperature) / source.grid.volume_m3());
        out.gamma = normal(1 + out.gas_heat / out.capacity);
        if (!(out.pressure > 0 && out.gamma > 1 && out.gamma <= 2)) throw Decline{};
        return out;
    }
    void readings(const Fields& f, std::vector<Reading>& output) const {
        for (std::size_t i = 0; i < output.size(); ++i) output[i] = reading(f, i);
    }
    static double inertia_at(const Face& f, const std::vector<Reading>& values) {
        return (f.left == none ? 0 : .5 * values[f.left].mass) + (f.right == none ? 0 : .5 * values[f.right].mass);
    }
    void work(std::span<const double> volume_change) {
        const double volume = source.grid.volume_m3();
        for (std::size_t i = 0; i < current.size(); ++i) if (!source.grid.walls[i]) {
            const double q = volume_change[i] / volume, gamma = current[i].gamma, p0 = current[i].pressure;
            if (!(q > -1)) throw std::domain_error("Interval pressure exhausted material volume");
            const auto& anchor = current[i];
            // For |q|<2^-10 and 1<gamma<=2, this outward thermal envelope
            // bounds the whole isentrope without another transcendental call.
            const double thermal_envelope = 2 * anchor.temperature * (gamma - 1) * std::abs(q);
            if (std::abs(q) >= 0x1p-10 || anchor.temperature - thermal_envelope <= anchor.minimum_temperature ||
                anchor.temperature + thermal_envelope >= anchor.maximum_temperature) {
                const double endpoint_t = anchor.temperature * std::exp(-(gamma - 1) * std::log1p(q));
                if (endpoint_t < anchor.minimum_temperature || endpoint_t > anchor.maximum_temperature)
                    throw std::domain_error("Interval pressure isentrope left its material temperature range");
                if (endpoint_t - anchor.minimum_temperature <= 64 * eps * std::max(endpoint_t, anchor.minimum_temperature) ||
                    anchor.maximum_temperature - endpoint_t <= 64 * eps * std::max(endpoint_t, anchor.maximum_temperature)) throw Decline{};
            }
            double inc = 0, tangent = 0;
            if (std::abs(q) < 0x1p-10) {
                // Continuous secant and tangent, without subtracting p0.
                const double c1 = -.5 * gamma, c2 = -c1 * (gamma + 1) / 3,
                    c3 = -c2 * (gamma + 2) / 4, c4 = -c3 * (gamma + 3) / 5,
                    c5 = -c4 * (gamma + 4) / 6, c6 = -c5 * (gamma + 5) / 7;
                inc = p0 * q * (c1 + q * (c2 + q * (c3 + q * (c4 + q * (c5 + q * c6)))));
                tangent = p0 / volume * (c1 + q * (2 * c2 + q * (3 * c3 + q * (4 * c4 + q * (5 * c5 + q * 6 * c6)))));
            } else {
                const double t = std::log1p(q), alpha = gamma - 1;
                const double w = p0 * (-std::expm1(-alpha * t) / (alpha * q));
                inc = w - p0; tangent = (p0 * std::exp(-gamma * t) - w) / (volume * q);
            }
            increment[i] = normal(inc); pressure[i] = normal(p0 + inc);
            if (!(tangent < 0)) throw Decline{};
            compliance[i] = normal(-1 / tangent);
        }
    }
    double pressure_gradient(const Face& f) const {
        Sum d;
        if (f.left != none) { d.add(current[f.left].pressure); d.add(increment[f.left]); }
        if (f.right != none) { d.add(-current[f.right].pressure); d.add(-increment[f.right]); }
        return normal(d.value());
    }
    double evaluate(const Fields& fields, std::span<const double> volumes, double dt) {
        work(volumes); const double factor = dt * source.grid.spacing_m * source.grid.depth_m;
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
            if (!topology.faces[a][f].free()) { pressure_j[a][f] = 0; sweep[a][f] = 0; continue; }
            const double impulse = product(factor, pressure_gradient(topology.faces[a][f]));
            pressure_j[a][f] = normal(fields.j[a][f] + impulse);
            sweep[a][f] = normal(product(factor, .5 * fields.j[a][f] + .5 * pressure_j[a][f]) / inertia[a][f]);
        }
        double norm = 0;
        for (std::size_t i = 0; i < current.size(); ++i) if (!source.grid.walls[i]) {
            const auto& f = topology.cell_faces[i]; Sum value;
            value.add(volumes[i]); value.add(sweep[0][f[0]]); value.add(-sweep[0][f[1]]);
            value.add(sweep[1][f[2]]); value.add(-sweep[1][f[3]]);
            residual[i] = normal(value.value()); norm = std::max(norm, std::abs(residual[i]) / source.grid.volume_m3());
        }
        return norm;
    }
    double dot(const std::vector<double>& a, const std::vector<double>& b) const {
        // Fixed blocks and lanes keep the reduction independent of future task
        // partitioning while breaking the long double-double dependency chain.
        for (std::size_t block = 0; block < dot_blocks.size(); ++block) {
            std::array<Sum, 4> lanes{}; const auto begin = block * 256, end = std::min(begin + 256, a.size());
            bool flagged = false;
            const auto accumulate = [&](std::size_t i, Sum& lane) {
                if (source.grid.walls[i]) return;
                const double value = a[i] * b[i]; const auto bits = std::bit_cast<std::uint64_t>(value) & UINT64_C(0x7fffffffffffffff);
                // Delay only the exception, not a physical state update. This
                // keeps ordinary multiplies/adds free of throwing branches.
                flagged |= ((bits != 0) & (bits < std::bit_cast<std::uint64_t>(0x1p-450))) |
                    (bits > std::bit_cast<std::uint64_t>(0x1p450)) | ((bits == 0) & (a[i] != 0) & (b[i] != 0));
                lane.add(value);
            };
            auto i = begin;
            for (; i + 4 <= end; i += 4) { accumulate(i, lanes[0]); accumulate(i + 1, lanes[1]); accumulate(i + 2, lanes[2]); accumulate(i + 3, lanes[3]); }
            for (; i < end; ++i) accumulate(i, lanes[i % 4]);
            if (flagged) throw Decline{};
            Sum value; for (const auto& lane : lanes) { value.add(lane.hi); value.add(lane.lo); } dot_blocks[block] = value;
        }
        Sum sum; for (const auto& block : dot_blocks) { sum.add(block.hi); sum.add(block.lo); }
        return normal(sum.value());
    }
    void apply(const std::vector<double>& in, std::vector<double>& out) const {
        for (std::size_t i = 0; i < in.size(); ++i) out[i] = source.grid.walls[i] ? 0 : compliance[i] * in[i];
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
            const auto& pair = topology.faces[a][f]; if (!pair.free()) continue;
            const double v = conductance[a][f] * (in[pair.left] - in[pair.right]);
            out[pair.left] += v; out[pair.right] -= v;
        }
    }
    std::size_t solve(double dt, MacClosedAdvanceReport& step) {
        const double da = dt * source.grid.spacing_m * source.grid.depth_m, factor = .5 * da * da;
        for (std::size_t i = 0; i < current.size(); ++i) {
            x[i] = 0; r[i] = source.grid.walls[i] ? 0 : residual[i]; diagonal[i] = compliance[i];
        }
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
            const auto& pair = topology.faces[a][f]; conductance[a][f] = 0; if (!pair.free()) continue;
            const double g = conductance[a][f] = normal(factor / inertia[a][f]); diagonal[pair.left] += g; diagonal[pair.right] += g;
        }
        double coupling = 0;
        for (std::size_t i = 0; i < r.size(); ++i) if (!source.grid.walls[i]) coupling = std::max(coupling, (diagonal[i] - compliance[i]) / compliance[i]);
        bool use_multigrid = settings.preconditioner == PressurePreconditioner::automatic && active_cells >= 256 && coupling >= 2;
        if (use_multigrid) {
            if (!multigrid) {
                std::call_once(cache.multigrid_once, [&] {
                    cache.multigrid = std::make_shared<const interval_detail::PressureMultigridTopology>(source.grid); ++topology_builds;
                });
                multigrid = std::make_unique<interval_detail::PressureMultigrid>(cache.multigrid);
            }
            use_multigrid = multigrid->prepare(compliance, conductance[0], conductance[1]);
            if (use_multigrid) {
                ++step.interval_multigrid_solves;
                step.interval_multigrid_levels = std::max(step.interval_multigrid_levels, cache.multigrid->levels().size());
            } else ++step.interval_multigrid_fallbacks;
        }
        const auto precondition = [&] {
            if (use_multigrid) { if (!multigrid->apply(r, z)) throw Decline{}; }
            else for (std::size_t i = 0; i < r.size(); ++i) z[i] = source.grid.walls[i] ? 0 : r[i] / diagonal[i];
        };
        const auto completed = [&](std::size_t iterations) {
            if (use_multigrid) step.interval_multigrid_cycles += multigrid->cycles();
            return iterations;
        };
        precondition(); direction = z;
        double rz = dot(r, z); const double rr0 = dot(r, r);
        if (rr0 == 0) return completed(0);
        for (std::size_t iteration = 0; iteration < 512; ++iteration) {
            apply(direction, applied); const double denominator = dot(direction, applied);
            if (!(denominator > 0 && rz > 0)) throw std::domain_error("Interval pressure PCG lost positive definiteness");
            const double alpha = rz / denominator;
            for (std::size_t i = 0; i < r.size(); ++i) { x[i] += alpha * direction[i]; r[i] -= alpha * applied[i]; }
            if (dot(r, r) <= 1e-24 * rr0) return completed(iteration + 1);
            precondition();
            const double next = dot(r, z), beta = next / rz; rz = next;
            for (std::size_t i = 0; i < r.size(); ++i) direction[i] = z[i] + beta * direction[i];
        }
        throw std::domain_error("Interval pressure PCG exhausted iterations");
    }
    void pressure_step(const Fields& from, double dt, MacClosedAdvanceReport& step) {
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f)
            inertia[a][f] = normal(inertia_at(topology.faces[a][f], current));
        std::fill(dv.begin(), dv.end(), 0);
        double norm = evaluate(from, dv, dt);
        for (std::size_t iteration = 0; norm > 16 * eps; ++iteration) {
            if (iteration == 64) throw std::domain_error("Interval pressure nonlinear root exhausted iterations");
            step.pressure_iterations += solve(dt, step); ++step.nonlinear_iterations;
            // Keep the tangent from the accepted iterate across backtracking.
            for (std::size_t i = 0; i < dv.size(); ++i) applied[i] = -compliance[i] * x[i];
            double fraction = 1; bool accepted = false;
            for (std::size_t line = 0; line < 24; ++line) {
                for (std::size_t i = 0; i < dv.size(); ++i) trial[i] = dv[i] + fraction * applied[i];
                try {
                    const double next = evaluate(from, trial, dt);
                    if (next < norm || next <= 16 * eps) { dv.swap(trial); norm = next; accepted = true; break; }
                } catch (const std::domain_error&) {}
                fraction *= .5;
            }
            if (!accepted) throw std::domain_error("Interval pressure line search stalled");
        }
        const double volume = source.grid.volume_m3();
        const double factor = dt * source.grid.spacing_m * source.grid.depth_m;
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f)
            if (topology.faces[a][f].free()) {
                StoredSum j; j.add(from.j[a][f]); j.add(product(factor, pressure_gradient(topology.faces[a][f])));
                pressure_j[a][f] = primary(j, from.j[a][f], rounding);
            }
        for (std::size_t i = 0; i < current.size(); ++i) if (!source.grid.walls[i]) {
            const auto& f = topology.cell_faces[i]; Sum change;
            change.add(sweep[0][f[1]]); change.add(-sweep[0][f[0]]);
            change.add(sweep[1][f[3]]); change.add(-sweep[1][f[2]]);
            const double swept = normal(change.value()); material_volume[i] = normal(volume + swept);
            const double outgoing = std::max(0., -sweep[0][f[0]]) + std::max(0., sweep[0][f[1]]) +
                std::max(0., -sweep[1][f[2]]) + std::max(0., sweep[1][f[3]]);
            if (!(material_volume[i] > 0) || outgoing > settings.maximum_outgoing_volume_fraction * material_volume[i])
                throw std::domain_error("Interval pressure material-volume CFL exceeded");
            StoredSum energy; energy.add(from.u()[i]); energy.add(-product(pressure[i], swept));
            pressure_u[i] = primary(energy, from.u()[i], rounding);
            step.maximum_geometric_residual_m3 = std::max(step.maximum_geometric_residual_m3, std::abs(dv[i] - swept));
        }
    }
    void advect(const Fields& from, Fields& to) {
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
            const auto& pair = topology.faces[a][f]; const double q = sweep[a][f];
            mass_flux[a][f] = q == 0 ? 0 : product(q / material_volume[q > 0 ? pair.left : pair.right],
                current[q > 0 ? pair.left : pair.right].mass);
        }
        std::fill(next_mass.begin(), next_mass.end(), 0);
        for (std::size_t k = 0; k < from.q.size(); ++k) {
            const auto& values = k + 1 == from.q.size() ? pressure_u : from.q[k];
            for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
                const auto& pair = topology.faces[a][f]; const double q = sweep[a][f];
                scalar_flux[a][f] = q == 0 ? 0 : product(q / material_volume[q > 0 ? pair.left : pair.right],
                    values[q > 0 ? pair.left : pair.right]);
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (source.grid.walls[i]) { to.q[k][i] = 0; continue; }
                const auto& f = topology.cell_faces[i]; StoredSum value; value.add(values[i]);
                value.add(scalar_flux[0][f[0]]); value.add(-scalar_flux[0][f[1]]);
                value.add(scalar_flux[1][f[2]]); value.add(-scalar_flux[1][f[3]]);
                const double v = primary(value, values[i], rounding);
                if (k + 1 != from.q.size()) {
                    if (v < 0) throw std::domain_error("Interval donor transport produced negative inventory");
                    if (v == 0 && values[i] > 0) throw Decline{};
                }
                to.q[k][i] = v; if (k < gases.size()) next_mass[i] += v;
            }
        }
    }
    void transport_momentum(Fields& to, std::size_t axis, MacEnergySum& reaction) {
        const auto& faces = topology.faces[axis]; const auto& edges = topology.dual[axis];
        for (std::size_t f = 0; f < faces.size(); ++f) {
            keep[f] = inertia[axis][f]; gathered[f] = {}; velocity[f] = inertia[axis][f] == 0 ? 0 :
                normal(pressure_j[axis][f] / inertia[axis][f]);
        }
        // Each edge has one shared mass transfer, including transverse edges.
        for (std::size_t e = 0; e < edges.size(); ++e) {
            const auto& edge = edges[e]; Sum flux;
            if (edge.a != none) flux.add(.5 * mass_flux[edge.axis][edge.a]);
            if (edge.b != none) flux.add(.5 * mass_flux[edge.axis][edge.b]);
            const double f = dual_flux[e] = normal(flux.value()); if (f == 0) continue;
            gathered[f > 0 ? edge.left : edge.right].add(-std::abs(f));
        }
        for (std::size_t f = 0; f < faces.size(); ++f) {
            gathered[f].add(keep[f]); keep[f] = primary(gathered[f], keep[f], rounding);
            if (keep[f] < 0) throw std::domain_error("Interval dual mass CFL exceeded");
            gathered[f] = {}; gathered[f].add(product(keep[f], velocity[f]));
        }
        for (std::size_t e = 0; e < edges.size(); ++e) {
            const double f = dual_flux[e]; if (f == 0) continue;
            const auto& edge = edges[e]; const auto donor = f > 0 ? edge.left : edge.right, target = f > 0 ? edge.right : edge.left;
            gathered[target].add(product(std::abs(f), velocity[donor]));
        }
        for (std::size_t f = 0; f < faces.size(); ++f) {
            const auto& pair = faces[f];
            const double mass = (pair.left == none ? 0 : .5 * next_mass[pair.left]) +
                (pair.right == none ? 0 : .5 * next_mass[pair.right]);
            const double j = primary(gathered[f], pressure_j[axis][f], rounding);
            if (mass == 0 && j != 0) throw Decline{};
            provisional[f] = mass == 0 ? 0 : normal(j / mass); to.j[axis][f] = pair.free() ? j : 0;
            if (!pair.free()) reaction.add(-j);
            const double wall_q = pair.free() ? 0 : .5 * product(j, provisional[f]); wall_heat.add(wall_q);
            const double difference = velocity[f] - provisional[f];
            gathered[f] = {}; gathered[f].add(.5 * product(product(keep[f], difference), difference));
            // Keep wall heat separate from the positive mixing variance.
            const double total = (pair.left == none ? 0 : next_mass[pair.left]) + (pair.right == none ? 0 : next_mass[pair.right]);
            for (const auto cell : {pair.left, pair.right}) if (cell != none && wall_q != 0)
                heat[cell].add(product(wall_q, next_mass[cell] / total));
        }
        for (std::size_t e = 0; e < edges.size(); ++e) {
            const double f = dual_flux[e]; if (f == 0) continue;
            const auto& edge = edges[e]; const auto donor = f > 0 ? edge.left : edge.right, target = f > 0 ? edge.right : edge.left;
            const double difference = velocity[donor] - provisional[target];
            gathered[target].add(.5 * product(product(std::abs(f), difference), difference));
        }
        for (std::size_t f = 0; f < faces.size(); ++f) {
            const double q = normal(gathered[f].value()); mixing_heat.add(q); if (q == 0) continue;
            const auto& pair = faces[f];
            const double total = (pair.left == none ? 0 : next_mass[pair.left]) + (pair.right == none ? 0 : next_mass[pair.right]);
            if (!(total > 0)) throw Decline{};
            for (const auto cell : {pair.left, pair.right}) if (cell != none) heat[cell].add(product(q, next_mass[cell] / total));
        }
    }
    void step(double dt, MacClosedAdvanceReport& result, std::array<MacEnergySum, 2>& reaction) {
        const auto& from = banks[selected]; auto& to = banks[1 - selected];
        pressure_step(from, dt, result); advect(from, to); std::fill(heat.begin(), heat.end(), StoredSum{});
        const double factor = dt * source.grid.spacing_m * source.grid.depth_m;
        for (std::size_t a = 0; a < 2; ++a) {
            Sum fixed_pressure;
            for (const auto& face : topology.faces[a]) if (!face.free()) {
                if (face.left != none) { fixed_pressure.add(-current[face.left].pressure); fixed_pressure.add(-increment[face.left]); }
                if (face.right != none) { fixed_pressure.add(current[face.right].pressure); fixed_pressure.add(increment[face.right]); }
            }
            reaction[a].add(product(factor, fixed_pressure.hi)); reaction[a].add(product(factor, fixed_pressure.lo));
            transport_momentum(to, a, reaction[a]);
        }
        for (std::size_t i = 0; i < current.size(); ++i) if (!source.grid.walls[i]) {
            StoredSum value = heat[i]; value.add(to.u()[i]); to.u()[i] = primary(value, to.u()[i], rounding);
        }
        readings(to, current); selected = 1 - selected;
    }
    template<class Operands> bool audit(Operands operands) {
        interval_detail::Account account; operands(account);
        const auto result = account.decide(operands); report.interval_audit_exact_fallbacks += result.exact ? 1U : 0U;
        return result.passed;
    }
    double kinetic(double mass, double j) const {
        if (mass == 0) { if (j != 0) throw Decline{}; return 0; }
        return .5 * product(j, normal(j / mass));
    }
    bool audit_interval() {
        const auto& final = banks[selected];
        for (std::size_t k = 0; k + 1 < final.q.size(); ++k) if (!audit([&](auto& account) {
            for (std::size_t i = 0; i < source.cells.size(); ++i) { account.add(final.q[k][i]); account.add(-original(k, i)); }
        })) return false;
        for (std::size_t a = 0; a < 2; ++a) if (!audit([&](auto& account) {
            for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
                account.add(final.j[a][f]); account.add(-(a == 0 ? source.momentum_x : source.momentum_y)[f]);
            }
            for (std::size_t k = 0; k < wall[a].size(); ++k) account.add(wall[a].component(k).negated());
        })) return false;
        const auto energy_operands = [&](auto& account) {
            for (std::size_t i = 0; i < source.cells.size(); ++i) { account.add(final.u()[i]); account.add(-source.cells[i].mixture.energy); }
            for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < topology.faces[a].size(); ++f) {
                account.add(kinetic(inertia_at(topology.faces[a][f], current), final.j[a][f]));
                account.add(-kinetic(inertia_at(topology.faces[a][f], initial), (a == 0 ? source.momentum_x : source.momentum_y)[f]));
            }
        };
        interval_detail::Account energy; energy_operands(energy); const auto result = energy.decide(energy_operands);
        report.interval_audit_exact_fallbacks += result.exact ? 1U : 0U;
        report.energy_residual_wide_j = result.residual; report.energy_bound_wide_j = result.bound;
        report.energy_residual_j = result.residual.project().value; report.energy_bound_j = result.bound.project().value;
        if (!result.passed) return false;
        // Relative ideal-mixture entropy, using actual endpoint inventories.
        // This avoids subtracting two large absolute entropy readings.
        Sum entropy, allowance, entropy_error_scale;
        for (std::size_t i = 0; i < source.cells.size(); ++i) if (!source.grid.walls[i]) {
            const auto& before = initial[i]; const auto& after = current[i]; Sum dc, doffset, du, absolute_dc, absolute_offset;
            du.add(final.u()[i]); du.add(-source.cells[i].mixture.energy);
            for (std::size_t k = 0; k < gases.size(); ++k) {
                const double change = final.q[k][i] - original(k, i);
                dc.add(product(change, gases[k].cv)); doffset.add(product(change, gases[k].offset));
                absolute_dc.add(std::abs(change * gases[k].cv)); absolute_offset.add(std::abs(change * gases[k].offset));
            }
            du.add(-doffset.hi); du.add(-doffset.lo);
            const double dt = du.value() / before.thermal, cv = dc.value() / before.capacity;
            const bool relative = std::abs(dt) < .5 && std::abs(cv) < .5;
            const double log_u = relative ? std::log1p(dt) : std::log(after.temperature);
            const double log_c = relative ? std::log1p(cv) : std::log(before.temperature);
            const double log_t = log_u - log_c;
            const double log_reference_t = std::log(before.temperature / entropy_reference_temperature_k);
            entropy.add(product(after.capacity, log_t)); entropy.add(product(dc.value(), log_reference_t));
            entropy_error_scale.add(after.capacity * ((std::abs(final.u()[i] - source.cells[i].mixture.energy) + absolute_offset.value()) /
                before.thermal + absolute_dc.value() / before.capacity + std::abs(log_u) + std::abs(log_c)));
            entropy_error_scale.add(absolute_dc.value() * (1 + std::abs(log_reference_t)));
            for (std::size_t k = 0; k < gases.size(); ++k) {
                const double n0 = original(k, i) / gases[k].molar, n1 = final.q[k][i] / gases[k].molar;
                // Distinct stored masses can divide to the same rounded mole
                // count. Their logarithmic increment must still be retained.
                if (final.q[k][i] == original(k, i)) continue;
                if (n0 == 0 || n1 == 0) {
                    const double n = n0 == 0 ? n1 : n0, log_volume = std::log(source.grid.volume_m3() / n);
                    entropy.add((n0 == 0 ? 1 : -1) * reference_gas_constant * n * log_volume);
                    entropy_error_scale.add(reference_gas_constant * n * (1 + std::abs(log_volume)));
                }
                else {
                    const double dn = (final.q[k][i] - original(k, i)) / gases[k].molar;
                    const double ratio = dn / n0;
                    const double log_volume = std::log(source.grid.volume_m3() / n0);
                    const double log_n = std::abs(ratio) < .5 ? std::log1p(ratio) : std::log(n1) - std::log(n0);
                    entropy.add(reference_gas_constant * dn * log_volume); entropy.add(-reference_gas_constant * n1 * log_n);
                    entropy_error_scale.add(reference_gas_constant * (std::abs(dn) * (1 + std::abs(log_volume)) + n1 * std::abs(log_n)));
                }
            }
            allowance.add(128 * eps * (std::abs(source.cells[i].mixture.energy) / before.temperature + std::abs(final.u()[i]) / after.temperature));
        }
        report.entropy_change_j_per_k = normal(entropy.value()); report.entropy_roundoff_j_per_k = normal(allowance.value());
        // This bounds ordinary log/product/input rounding; it does not enlarge
        // the physical entropy allowance. An uncertain decision is recomputed
        // by the retained extended implementation from the original interval.
        const double uncertainty = normal(64 * eps * entropy_error_scale.value());
        Sum margin = entropy; margin.add(allowance.hi); margin.add(allowance.lo);
        if (std::abs(margin.value()) < uncertainty) throw Decline{};
        return margin.value() >= 0;
    }
    MacIntervalCandidate run(double seconds, std::size_t budget, std::size_t& attempts) {
        double maximum_dt = settings.maximum_timestep_s; std::size_t audit_retries = 0, rejected = 0;
        for (;;) {
            pack(); report = {}; wall = {}; mixing_heat = {}; wall_heat = {}; rounding = {};
            double elapsed = 0, next_dt = maximum_dt;
            while (elapsed < seconds) {
                double dt = std::min(next_dt, seconds - elapsed);
                for (;;) {
                    if (attempts == budget) throw std::runtime_error("MAC interval kernel exhausted its shared attempt budget");
                    ++attempts;
                    if (!(dt > 0) || elapsed + dt <= elapsed) throw FluidFatalError(FluidFatalCode::numerical_precision, "MAC interval retry time cannot be represented");
                    const auto saved_rounding = rounding; const auto saved_mix = mixing_heat, saved_wall = wall_heat;
                    try {
                        MacClosedAdvanceReport accepted; std::array<MacEnergySum, 2> reaction;
                        step(dt, accepted, reaction);
                        for (std::size_t a = 0; a < 2; ++a) for (std::size_t k = 0; k < reaction[a].size(); ++k) wall[a].add(reaction[a].component(k));
                        report.pressure_iterations += accepted.pressure_iterations; report.nonlinear_iterations += accepted.nonlinear_iterations;
                        report.interval_multigrid_solves += accepted.interval_multigrid_solves; report.interval_multigrid_cycles += accepted.interval_multigrid_cycles;
                        report.interval_multigrid_fallbacks += accepted.interval_multigrid_fallbacks;
                        report.interval_multigrid_levels = std::max(report.interval_multigrid_levels, accepted.interval_multigrid_levels);
                        report.maximum_geometric_residual_m3 = std::max(report.maximum_geometric_residual_m3, accepted.maximum_geometric_residual_m3);
                        ++report.substeps; elapsed += dt; next_dt = std::min(maximum_dt, 2 * dt); break;
                    } catch (const std::domain_error&) {
                        ++rejected; rounding = saved_rounding; mixing_heat = saved_mix; wall_heat = saved_wall;
                        readings(banks[selected], current); dt *= .5;
                    }
                }
            }
            if (audit_interval()) break;
            ++audit_retries; rejected += report.substeps; maximum_dt *= .5;
        }
        report.attempts = attempts; report.rejected_attempts = rejected; report.interval_audit_retries = audit_retries;
        report.interval_kernel_intervals = 1; report.interval_rounded_updates = rounding.rounded; report.interval_unresolved_updates = rounding.unresolved;
        report.interval_topology_builds = topology_builds;
        report.mixing_heat_j = normal(mixing_heat.value()); report.mixing_heat_wide_j = MacEnergy::from_double(report.mixing_heat_j);
        report.wall_heat_j = normal(wall_heat.value()); report.wall_heat_wide_j = MacEnergy::from_double(report.wall_heat_j);
        report.full_wall_impulse_x = wall[0].value(); report.full_wall_impulse_y = wall[1].value();
        report.wall_impulse_x = report.full_wall_impulse_x.project().value; report.wall_impulse_y = report.full_wall_impulse_y.project().value;
        MacIntervalCandidate result{source, report}; const auto& final = banks[selected];
        for (std::size_t i = 0; i < source.cells.size(); ++i) {
            auto& cell = result.state.cells[i];
            for (std::size_t k = 0; k < gases.size(); ++k) cell.mixture.masses[gases[k].species] = final.q[k][i];
            cell.mixture.energy = final.u()[i]; cell.passive_quantities.clear();
            for (std::size_t k = 0; k < passive_ids.size(); ++k) if (final.q[gases.size() + k][i] != 0)
                cell.passive_quantities.push_back({passive_ids[k], final.q[gases.size() + k][i]});
        }
        result.state.momentum_x = final.j[0]; result.state.momentum_y = final.j[1]; return result;
    }
};
}
std::optional<MacIntervalCandidate> advance_mac_interval_kernel(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, const MacClosedSettings& settings, double seconds, std::size_t budget, std::size_t& attempts, MacIntervalCache* cached) {
    attempts = 0;
    // Never substitute a cheaper physical selector. Each remaining selector
    // will be ported and validated before this admission expands.
    if (settings.reconstruction != MacMaterialReconstruction::donor_cell ||
        settings.momentum_reconstruction != MacMomentumReconstruction::donor_cell ||
        settings.compression_viscosity != MacCompressionViscosity::none || settings.shock_heat != MacShockHeatRegularization::none ||
        settings.thermal_conduction || settings.viscosity.shear_pa_s != 0 || settings.viscosity.bulk_pa_s != 0 ||
        settings.gravity_m_per_s2 != 0 || settings.preconditioner == PressurePreconditioner::incomplete_cholesky) return {};
    MacIntervalCache local_cache;
    try { normal(seconds); normal(settings.maximum_timestep_s); return Kernel(registry, model, source, settings, cached ? *cached : local_cache).run(seconds, budget, attempts); }
    catch (const Decline&) { return {}; }
}
void validate_mac_interval_fallback(const MacTransportState& before, const MacTransportState& after, MacClosedAdvanceReport& report) {
    const auto reconcile = [&](auto operands) {
        interval_detail::Account account; operands(account); const auto result = account.decide(operands);
        report.interval_audit_exact_fallbacks += result.exact ? 1U : 0U;
        if (!result.passed) throw std::domain_error("MAC reference fallback failed its interval inventory/impulse audit");
    };
    for (std::size_t s = 0; s < before.cells.front().mixture.masses.size(); ++s) reconcile([&](auto& account) {
        for (std::size_t i = 0; i < before.cells.size(); ++i) { account.add(after.cells[i].mixture.masses[s]); account.add(-before.cells[i].mixture.masses[s]); }
    });
    std::vector<std::uint64_t> ids;
    for (const auto* state : {&before, &after}) for (const auto& cell : state->cells)
        for (const auto& p : cell.passive_quantities) ids.push_back(p.identity);
    std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (const auto id : ids) reconcile([&](auto& account) {
        for (const auto* state : {&before, &after}) for (const auto& cell : state->cells) {
            const auto p = std::lower_bound(cell.passive_quantities.begin(), cell.passive_quantities.end(), id,
                [](const auto& entry, auto identity) { return entry.identity < identity; });
            if (p != cell.passive_quantities.end() && p->identity == id) account.add((state == &before ? -1 : 1) * p->quantity);
        }
    });
    for (int a = 0; a < 2; ++a) {
        const auto j0 = a == 0 ? mac_field_values(before.momentum_x, before.full_momentum_x) : mac_field_values(before.momentum_y, before.full_momentum_y);
        const auto j1 = a == 0 ? mac_field_values(after.momentum_x, after.full_momentum_x) : mac_field_values(after.momentum_y, after.full_momentum_y);
        reconcile([&](auto& account) {
            for (std::size_t f = 0; f < j0.size(); ++f) { account.add(j1[f]); account.add(j0[f].negated()); }
            account.add((a == 0 ? report.full_wall_impulse_x : report.full_wall_impulse_y).negated());
            if (a == 1) account.add(report.full_body_impulse_y.negated());
        });
    }
    if (report.entropy_change_j_per_k < -report.entropy_roundoff_j_per_k)
        throw std::domain_error("MAC reference fallback failed its interval entropy audit");
}
}
