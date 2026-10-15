#include "mixed_atmospheres/occupied_staggered_remap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <type_traits>

namespace ma::experimental {
namespace {
constexpr double eps = std::numeric_limits<double>::epsilon();
[[noreturn]] void fail(FluidFatalCode code, const char* message) { throw FluidFatalError(code, message); }
[[noreturn]] void precision(const char* message) { fail(FluidFatalCode::numerical_precision, message); }
void finite(double value) { if (!std::isfinite(value)) precision("Nonfinite staggered remap arithmetic"); }
double checked(long double x) { const auto y = static_cast<double>(x); finite(y); return y; }
// Compensated local events/sums; no dependence on GNU's wider long double.
struct DD {
    double hi = 0, lo = 0;
    DD() = default;
    explicit DD(double x) : hi(x) {}
    void add(double x) {
        const double s = hi + x, z = s - hi;
        const double e = (hi - (s - z)) + (x - z);
        const double t = lo + e, h = s + t, q = h - s;
        lo = (s - (h - q)) + (t - q); hi = h;
        finite(hi); finite(lo);
    }
    void add(DD x) { add(x.hi); add(x.lo); }
    void subtract(DD x) { add(-x.hi); add(-x.lo); }
    [[nodiscard]] double value() const { const double x = hi + lo; finite(x); return x; }
};
DD difference(DD a, DD b) { a.subtract(b); return a; }
int sign(DD x) { return x.hi > 0 ? 1 : x.hi < 0 ? -1 : x.lo > 0 ? 1 : x.lo < 0 ? -1 : 0; }
bool less(DD a, DD b) { return sign(difference(a, b)) < 0; }
DD midpoint(DD a, DD b) {
    auto width = difference(b, a); DD result = a; result.add(.5 * width.hi); result.add(.5 * width.lo); return result;
}
DD product(double a, double b) {
    const double p = a * b; finite(p); DD result(p); result.add(std::fma(a, b, -p)); return result;
}
double positive_product(double a, double b) {
    const double p = a * b; finite(p);
    if (a != 0 && b != 0 && p == 0) precision("Nonzero remap product cannot be represented");
    return p;
}
double half_mass(double mass) {
    const double h = .5 * mass;
    if (!(h > 0)) precision("Positive half mass is unrepresentable");
    auto residual = product(h, 2); residual.add(-mass);
    if (std::abs(residual.value()) > 32 * eps * mass) precision("Half mass has material local rounding error");
    return h;
}
double kinetic(double mass, double velocity) {
    if (velocity == 0) return 0;
    if (.5 * velocity == 0) precision("Positive remap kinetic energy is unrepresentable");
    return positive_product(positive_product(mass, .5 * velocity), velocity);
}
double node_kinetic(const StaggeredNode& node) {
    return node.momentum_kg_m_per_s == 0 ? 0 : positive_product(node.momentum_kg_m_per_s, .5 * node.velocity_m_per_s());
}
// Compare q*d with v*n using normalized FMA products, so subnormal shares
// are certified relative to their actual local scalar, not a unit floor.
double ratio_residual(double q, double d, double v, double n) {
    int eq = 0, ed = 0, ev = 0, en = 0;
    const double fq = std::frexp(q, &eq), fd = std::frexp(d, &ed);
    const double fv = std::frexp(v, &ev), fn = std::frexp(n, &en);
    const int e1 = eq + ed, e2 = ev + en, common = std::max(e1, e2);
    auto actual = product(fq, fd), expected = product(fv, fn);
    actual.hi = std::ldexp(actual.hi, e1 - common); actual.lo = std::ldexp(actual.lo, e1 - common);
    expected.hi = std::ldexp(expected.hi, e2 - common); expected.lo = std::ldexp(expected.lo, e2 - common);
    const double scale = std::abs(expected.value());
    if (!(scale > 0)) precision("Share certification is outside local numerical range");
    return std::abs(difference(actual, expected).value()) / scale;
}
double share(double value, double part, double total) {
    if (value == 0) return 0;
    int ev = 0, ep = 0, et = 0;
    const double v = std::frexp(value, &ev), p = std::frexp(part, &ep), t = std::frexp(total, &et);
    const double amount = std::ldexp((v * p) / t, ev + ep - et);
    finite(amount);
    if (amount == 0 || std::signbit(amount) != std::signbit(value) || ratio_residual(amount, total, value, part) > 16 * eps)
        precision("Positive-overlap share cannot resolve its local scalar");
    return amount;
}
struct Atom { std::size_t old_corner, target_corner; DD begin, end; double length; };
struct Primal { std::size_t old_cell, target_cell; DD begin, end; double length; };
struct AccumPacket {
    DD m, p, h;
    long double mass_error = 0, momentum_error = 0, energy_error = 0;
    void add(StaggeredMechanicalPacket a, long double mb, long double pb, long double hb) {
        m.add(a.mass_kg); p.add(a.momentum_kg_m_per_s); h.add(a.kinetic_j);
        mass_error += mb; momentum_error += pb; energy_error += hb;
    }
    [[nodiscard]] StaggeredMechanicalPacket get() const { return {m.value(), p.value(), h.value()}; }
};
struct PrimalAccum { std::vector<DD> masses; DD nonreference, potential, entropy, length; long double mass_error = 0; };
std::vector<DD> corners(std::span<const double> positions) {
    std::vector<DD> result; result.reserve(2 * positions.size() - 1);
    for (std::size_t i = 0; i + 1 < positions.size(); ++i) {
        DD l(positions[i]), r(positions[i + 1]); const auto mid = midpoint(l, r);
        if (!less(l, mid) || !less(mid, r)) precision("Positive virtual half interval cannot be represented");
        result.push_back(l); result.push_back(mid);
    }
    result.emplace_back(positions.back()); return result;
}
std::vector<Atom> overlay(std::span<const DD> old, std::span<const DD> target) {
    std::vector<Atom> result; std::size_t i = 0, j = 0;
    while (i + 1 < old.size() && j + 1 < target.size()) {
        const auto a = less(old[i], target[j]) ? target[j] : old[i];
        const auto b = less(old[i + 1], target[j + 1]) ? old[i + 1] : target[j + 1];
        if (less(a, b)) {
            const auto length = difference(b, a); const double represented = length.value();
            if (!(represented > 0) || std::abs(difference(length, DD(represented)).value()) > 32 * eps * represented)
                precision("Positive atomic overlap is unrepresentable");
            result.push_back({i, j, a, b, represented});
        }
        const bool old_first = !less(target[j + 1], old[i + 1]);
        const bool target_first = !less(old[i + 1], target[j + 1]);
        if (old_first) ++i;
        if (target_first) ++j;
    }
    return result;
}
std::vector<double> distribute(double value, std::span<const double> lengths, double total) {
    std::vector<double> amounts(lengths.size());
    if (lengths.empty()) precision("Positive donor has no overlap");
    const auto largest = static_cast<std::size_t>(std::max_element(lengths.begin(), lengths.end()) - lengths.begin());
    DD remaining(value);
    for (std::size_t i = 0; i < lengths.size(); ++i) if (i != largest) { amounts[i] = share(value, lengths[i], total); remaining.add(-amounts[i]); }
    amounts[largest] = remaining.value();
    if (value != 0) {
        if (amounts[largest] == 0 || std::signbit(amounts[largest]) != std::signbit(value))
            precision("Final positive-overlap remainder cannot be represented");
        // Accumulated direct share errors and length summation, propagated
        // onto the largest remainder. No trace uses another species' scale.
        const double amplification = std::abs(value / amounts[largest]);
        if (!std::isfinite(amplification) || ratio_residual(amounts[largest], total, value, lengths[largest]) >
            32 * eps * amplification) precision("Final overlap remainder exceeds propagated local arithmetic");
    }
    return amounts;
}
DD formation(const FluidThermodynamics& model, const MixtureInventory& mixture, double volume) {
    const auto reference = model.to_reference(mixture, volume); DD sum;
    const auto& materials = model.reference().materials();
    for (std::size_t s = 0; s < materials.size(); ++s) sum.add(product(reference.masses_kg[s], materials[s].energy_offset_j_per_kg));
    return sum;
}
double local_potential(double mass, double gravity, DD center) {
    if (gravity == 0) return 0;
    const double mg = positive_product(mass, gravity);
    auto pe = product(mg, center.hi); pe.add(product(mg, center.lo)); return pe.value();
}
void validate_plan(const OccupiedStaggeredState& state, const StaggeredRezonePlan& plan) {
    if (!state.initialized()) fail(FluidFatalCode::unsupported_configuration, "Uninitialized remap state");
    if (state.cells().empty()) {
        if (!plan.node_relative_positions_m.empty() || !plan.target_cell_ids.empty())
            fail(FluidFatalCode::unsupported_topology, "Vacuum cannot acquire a target material partition");
        return;
    }
    const auto& p = plan.node_relative_positions_m;
    if (p.size() < 2 || plan.target_cell_ids.size() != p.size() - 1)
        fail(FluidFatalCode::unsupported_configuration, "Incomplete rezone target geometry/mapping");
    if (p.front() != state.nodes().front().relative_position_m || p.back() != state.nodes().back().relative_position_m)
        fail(FluidFatalCode::unsupported_topology, "Rezone must preserve occupied endpoints exactly");
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (!std::isfinite(p[i])) fail(FluidFatalCode::unsupported_configuration, "Nonfinite rezone node");
        if (i > 0 && !(p[i] > p[i - 1])) fail(FluidFatalCode::unsupported_topology, "Rezone requires positive connected intervals");
        if (i > 1 && plan.target_cell_ids[i - 1] <= plan.target_cell_ids[i - 2])
            fail(FluidFatalCode::unsupported_configuration, "Rezone cell IDs must be ordered and unique");
    }
}
}

StaggeredRezonePlan plan_staggered_cartesian_rezone(const OccupiedStaggeredState& state, const StaggeredCartesianGrid& grid) {
    if (!state.initialized() || !std::isfinite(grid.spacing_m) || grid.spacing_m <= 0 ||
        !std::isfinite(grid.origin_relative_high_m) || !std::isfinite(grid.origin_relative_low_m) ||
        grid.end_cell <= grid.first_cell || grid.first_cell < -(INT64_C(1) << 52) || grid.end_cell > (INT64_C(1) << 52))
        fail(FluidFatalCode::unsupported_configuration, "Invalid retained Cartesian rezone grid");
    StaggeredRezonePlan result; if (state.cells().empty()) return result;
    const auto line = [&](std::int64_t index) {
        auto position = product(static_cast<double>(index), grid.spacing_m);
        position.add(grid.origin_relative_high_m); position.add(grid.origin_relative_low_m); return position;
    };
    const DD left(state.nodes().front().relative_position_m), right(state.nodes().back().relative_position_m);
    if (less(left, line(grid.first_cell)) || less(line(grid.end_cell), right))
        fail(FluidFatalCode::unsupported_topology, "Occupied slab overruns the remap grid; outflow/collision is unsupported");
    const auto find_cell = [&](DD position) {
        auto lo = grid.first_cell, hi = grid.end_cell;
        while (hi - lo > 1) { const auto mid = lo + (hi - lo) / 2; if (less(position, line(mid))) hi = mid; else lo = mid; }
        return lo;
    };
    const auto first = find_cell(left), last = find_cell(right);
    result.node_relative_positions_m.push_back(left.value());
    std::vector<DD> exact{left};
    for (auto cell = first; cell <= last; ++cell) {
        const auto end = line(cell + 1);
        result.target_cell_ids.push_back(cell);
        if (!less(end, right)) { exact.push_back(right); result.node_relative_positions_m.push_back(right.value()); break; }
        exact.push_back(end); result.node_relative_positions_m.push_back(end.value());
    }
    for (std::size_t i = 0; i + 1 < exact.size(); ++i) {
        const double expected = difference(exact[i + 1], exact[i]).value();
        const double represented = result.node_relative_positions_m[i + 1] - result.node_relative_positions_m[i];
        if (!(expected > 0) || !(represented > 0) || std::abs(represented - expected) > 32 * eps * expected)
            precision("Cartesian cut cannot be stored at its local positive interval scale");
    }
    validate_plan(state, result); return result;
}

StaggeredRemapCandidate rezone_occupied_staggered(const OccupiedStaggeredState& source, const StaggeredRezonePlan& plan) {
    validate_plan(source, plan);
    StaggeredRemapCandidate result{source, {}, plan.target_cell_ids};
    auto& state = result.state; auto& report = result.report; report.target_cell_ids = plan.target_cell_ids;
    const auto old_count = source.cells_.size(), count = plan.target_cell_ids.size();
    bool identity = old_count == count;
    if (identity) for (std::size_t j = 0; j < source.nodes_.size(); ++j)
        identity = identity && source.nodes_[j].relative_position_m == plan.node_relative_positions_m[j];
    if (identity) { report.identity = true; return result; }
    std::vector<double> old_positions; old_positions.reserve(source.nodes_.size());
    for (const auto& node : source.nodes_) old_positions.push_back(node.relative_position_m);
    const auto old_events = corners(old_positions), target_events = corners(plan.node_relative_positions_m);
    const auto atoms = overlay(old_events, target_events);
    std::vector<Primal> overlaps;
    for (const auto& atom : atoms) {
        const auto donor = atom.old_corner / 2, target = atom.target_corner / 2;
        if (!overlaps.empty() && overlaps.back().old_cell == donor && overlaps.back().target_cell == target) {
            overlaps.back().end = atom.end; overlaps.back().length = difference(atom.end, overlaps.back().begin).value();
        } else overlaps.push_back({donor, target, atom.begin, atom.end, atom.length});
    }
    const auto species_count = source.model_->registry_size();
    std::vector<PrimalAccum> primal(count);
    for (auto& receiver : primal) receiver.masses.resize(species_count);
    std::vector<std::vector<std::size_t>> primal_rows(old_count), dual_rows(2 * old_count);
    for (std::size_t k = 0; k < overlaps.size(); ++k) primal_rows[overlaps[k].old_cell].push_back(k);
    for (std::size_t k = 0; k < atoms.size(); ++k) dual_rows[atoms[k].old_corner].push_back(k);
    std::vector<AccumPacket> dual(2 * count);
    DD old_energy, old_momentum, old_kinetic, old_potential, old_entropy, gathered_p, gathered_h;
    long double physical_energy_scale = 0, entropy_scale = 0, entropy_closure_bound = 0, momentum_scale = 0;
    long double packet_momentum_bound = 0, normalization_momentum_bound = 0;
    std::vector<DD> old_species(species_count), new_species(species_count);
    std::vector<long double> species_bound(species_count);
    for (const auto& node : source.nodes_) {
        old_kinetic.add(node_kinetic(node)); old_momentum.add(node.momentum_kg_m_per_s);
        momentum_scale += std::abs(node.momentum_kg_m_per_s);
    }
    for (std::size_t i = 0; i < old_count; ++i) {
        const auto& cell = source.cells_[i]; const double mass = cell.mixture.mass();
        const double length = difference(old_events[2 * i + 2], old_events[2 * i]).value();
        const auto& row = primal_rows[i]; std::vector<double> lengths; lengths.reserve(row.size()); DD geometry;
        for (const auto index : row) { lengths.push_back(overlaps[index].length); geometry.add(overlaps[index].length); }
        geometry.add(-length);
        if (std::abs(geometry.value()) > 32 * eps * length) precision("Primal intersections do not resolve donor geometry");
        auto nonreference = DD(cell.mixture.energy); nonreference.subtract(formation(*source.model_, cell.mixture, cell.occupied_volume_m3));
        if (!(nonreference.value() > 0)) precision("Stored nonreference thermal/elastic energy is unresolved");
        const auto energies = distribute(nonreference.value(), lengths, length);
        const auto entropies = distribute(cell.thermodynamics.entropy_j_per_k, lengths, length);
        std::vector<StaggeredPrimalRecord> records(row.size());
        for (std::size_t k = 0; k < row.size(); ++k) {
            const auto& overlap = overlaps[row[k]];
            records[k] = {i, overlap.target_cell, overlap.length, midpoint(overlap.begin, overlap.end).value(), energies[k], 0,
                std::vector<double>(species_count)};
        }
        for (std::size_t s = 0; s < species_count; ++s) {
            old_species[s].add(cell.mixture.masses[s]);
            const auto amounts = distribute(cell.mixture.masses[s], lengths, length);
            for (std::size_t k = 0; k < row.size(); ++k) {
                records[k].masses_kg[s] = amounts[k]; auto& target = primal[records[k].target]; target.masses[s].add(amounts[k]);
                if (amounts[k] != 0) {
                    const double relative_error = ratio_residual(amounts[k], length, cell.mixture.masses[s], lengths[k]);
                    const long double bound = (relative_error + 8 * eps) * static_cast<long double>(amounts[k]);
                    target.mass_error += bound; species_bound[s] += bound;
                }
            }
        }
        const double pe = local_potential(mass, source.gravity_, midpoint(old_events[2 * i], old_events[2 * i + 2]));
        const auto largest = static_cast<std::size_t>(std::max_element(lengths.begin(), lengths.end()) - lengths.begin());
        DD remaining_pe(pe);
        for (std::size_t k = 0; k < row.size(); ++k) if (k != largest) {
            DD amount; for (double m : records[k].masses_kg) amount.add(m);
            records[k].potential_j = local_potential(amount.value(), source.gravity_, midpoint(overlaps[row[k]].begin, overlaps[row[k]].end));
            remaining_pe.add(-records[k].potential_j);
        }
        records[largest].potential_j = remaining_pe.value();
        for (std::size_t k = 0; k < row.size(); ++k) {
            const auto& record = records[k]; auto& target = primal[record.target];
            target.nonreference.add(energies[k]); target.entropy.add(entropies[k]); target.potential.add(record.potential_j);
            target.length.add(record.length_m); report.primal_records.push_back(record);
        }
        old_energy.add(cell.mixture.energy); old_potential.add(pe); old_entropy.add(cell.thermodynamics.entropy_j_per_k);
        physical_energy_scale += cell.thermodynamics.thermal_energy_j + cell.thermodynamics.elastic_energy_j;
        entropy_scale += cell.thermodynamics.intrinsic_heat_capacity_j_per_k + std::abs(cell.thermodynamics.entropy_j_per_k);
        entropy_closure_bound += (std::abs(cell.thermodynamics.energy_residual_j) +
            std::abs(cell.thermodynamics.mechanical_pressure_pa * cell.thermodynamics.volume_residual_m3)) / cell.thermodynamics.temperature_k;
        const double half = half_mass(mass);
        for (std::size_t side = 0; side < 2; ++side) {
            const auto corner = 2 * i + side; const double velocity = source.nodes_[i + side].velocity_m_per_s();
            const StaggeredMechanicalPacket packet{half, velocity == 0 ? 0 : positive_product(half, velocity), kinetic(half, velocity)};
            gathered_p.add(packet.momentum_kg_m_per_s); gathered_h.add(packet.kinetic_j);
            const auto& drow = dual_rows[corner]; std::vector<double> dl; dl.reserve(drow.size()); DD sum_length;
            for (const auto index : drow) { dl.push_back(atoms[index].length); sum_length.add(atoms[index].length); }
            const double corner_length = difference(old_events[corner + 1], old_events[corner]).value(); sum_length.add(-corner_length);
            if (std::abs(sum_length.value()) > 32 * eps * corner_length) precision("Dual intersections do not resolve donor half interval");
            const auto dm = distribute(packet.mass_kg, dl, corner_length), dp = distribute(packet.momentum_kg_m_per_s, dl, corner_length),
                       dh = distribute(packet.kinetic_j, dl, corner_length);
            for (std::size_t k = 0; k < drow.size(); ++k) {
                const auto target = atoms[drow[k]].target_corner;
                const auto scalar_bound = [&](double q, double original) -> long double {
                    if (original == 0) return 0;
                    return (ratio_residual(q, corner_length, original, dl[k]) + 8 * eps) * std::abs(static_cast<long double>(q));
                };
                const auto mb = scalar_bound(dm[k], packet.mass_kg), pb = scalar_bound(dp[k], packet.momentum_kg_m_per_s), hb = scalar_bound(dh[k], packet.kinetic_j);
                const StaggeredMechanicalPacket piece{dm[k], dp[k], dh[k]}; dual[target].add(piece, mb, pb, hb); packet_momentum_bound += pb;
                report.dual_records.push_back({corner, target, dl[k], piece}); momentum_scale += std::abs(piece.momentum_kg_m_per_s);
            }
        }
    }
    report.gather_momentum_residual_kg_m_per_s = difference(gathered_p, old_momentum).value();
    report.gather_kinetic_residual_j = difference(gathered_h, old_kinetic).value();
    state.cells_.clear(); state.cells_.resize(count); state.nodes_.assign(count + 1, StaggeredNode{});
    state.anchor_ = source.anchor_ == old_count ? count : 0;
    report.cells.resize(count); report.nodes.resize(count + 1);
    std::vector<StaggeredMechanicalPacket> balanced(2 * count);
    const auto sum_pair = [](double a, double b) { DD sum(a); sum.add(b); return sum; };
    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = state.cells_[i]; cell.mixture = MixtureInventory(species_count);
        for (std::size_t s = 0; s < species_count; ++s) { cell.mixture.masses[s] = primal[i].masses[s].value(); new_species[s].add(cell.mixture.masses[s]); }
        const double mass = cell.mixture.mass(); if (!(mass > 0) || !std::isfinite(mass)) precision("Target mass is not positive finite");
        cell.occupied_volume_m3 = positive_product(state.area_, plan.node_relative_positions_m[i + 1] - plan.node_relative_positions_m[i]);
        DD length_error(plan.node_relative_positions_m[i + 1]); length_error.add(-plan.node_relative_positions_m[i]); length_error.subtract(primal[i].length);
        const double geometry_error = state.area_ * length_error.value(); finite(geometry_error);
        if (std::abs(geometry_error) > 32 * eps * cell.occupied_volume_m3) precision("Stored target volume cannot resolve positive intersections");
        report.geometry_volume_residual_m3 += geometry_error; report.absolute_geometry_volume_residual_m3 += std::abs(geometry_error);
        auto& r = report.cells[i]; r.raw_left = dual[2 * i].get(); r.raw_right = dual[2 * i + 1].get();
        const double raw_mass = sum_pair(r.raw_left.mass_kg, r.raw_right.mass_kg).value();
        auto mass_error = sum_pair(r.raw_left.mass_kg, r.raw_right.mass_kg); mass_error.add(-mass);
        r.raw_minus_primal_mass_kg = mass_error.value();
        r.quadrature_mass_bound_kg = checked(primal[i].mass_error + dual[2 * i].mass_error + dual[2 * i + 1].mass_error +
            16 * static_cast<long double>(eps) * (mass + raw_mass));
        if (!(raw_mass > 0) || std::abs(r.raw_minus_primal_mass_kg) > r.quadrature_mass_bound_kg)
            precision("Primal/dual mass discrepancy exceeds propagated arithmetic");
        r.lambda = mass / raw_mass; finite(r.lambda);
        if (!(r.lambda > 0) || std::abs(r.lambda - 1) > r.quadrature_mass_bound_kg / raw_mass + 2 * eps)
            precision("Quadrature normalization is outside measured local roundoff");
        const bool left_small = r.raw_left.mass_kg <= r.raw_right.mass_kg;
        const auto small_raw = left_small ? r.raw_left : r.raw_right, large_raw = left_small ? r.raw_right : r.raw_left;
        const double small_mass = share(mass, small_raw.mass_kg, raw_mass);
        DD large_mass_sum(mass); large_mass_sum.add(-small_mass); const double large_mass = large_mass_sum.value();
        const auto normalize = [&](StaggeredMechanicalPacket raw, double desired) {
            if (!(desired > 0)) precision("Positive normalized quadrature mass is unrepresentable");
            const auto delta_mass = difference(DD(desired), DD(raw.mass_kg));
            if (std::abs(delta_mass.value()) / raw.mass_kg > r.quadrature_mass_bound_kg / raw_mass + 32 * eps)
                precision("Normalization materially changes an individual positive corner mass");
            return StaggeredMechanicalPacket{desired, share(raw.momentum_kg_m_per_s, desired, raw.mass_kg), share(raw.kinetic_j, desired, raw.mass_kg)};
        };
        const auto ns = normalize(small_raw, small_mass), nl = normalize(large_raw, large_mass);
        r.normalized_left = left_small ? ns : nl; r.normalized_right = left_small ? nl : ns;
        r.normalization_delta_mass_kg = difference(sum_pair(ns.mass_kg, nl.mass_kg), sum_pair(r.raw_left.mass_kg, r.raw_right.mass_kg)).value();
        r.normalization_delta_momentum_kg_m_per_s = difference(sum_pair(ns.momentum_kg_m_per_s, nl.momentum_kg_m_per_s),
            sum_pair(r.raw_left.momentum_kg_m_per_s, r.raw_right.momentum_kg_m_per_s)).value();
        r.normalization_delta_kinetic_j = difference(sum_pair(ns.kinetic_j, nl.kinetic_j), sum_pair(r.raw_left.kinetic_j, r.raw_right.kinetic_j)).value();
        const double p_abs = std::abs(r.raw_left.momentum_kg_m_per_s) + std::abs(r.raw_right.momentum_kg_m_per_s);
        const double h_abs = r.raw_left.kinetic_j + r.raw_right.kinetic_j;
        const double relative_mass_budget = r.quadrature_mass_bound_kg / raw_mass + 32 * eps;
        r.normalization_momentum_bound_kg_m_per_s = (relative_mass_budget + 32 * eps) * p_abs;
        r.normalization_kinetic_bound_j = (relative_mass_budget + 32 * eps) * h_abs;
        if (std::abs(r.normalization_delta_momentum_kg_m_per_s) > r.normalization_momentum_bound_kg_m_per_s ||
            std::abs(r.normalization_delta_kinetic_j) > r.normalization_kinetic_bound_j)
            precision("Normalization changed momentum/kinetic energy beyond its mass arithmetic bound");
        normalization_momentum_bound += r.normalization_momentum_bound_kg_m_per_s;
        r.arithmetic_energy_j = -r.normalization_delta_kinetic_j;
        r.left = r.normalized_left; r.right = r.normalized_right; const double half = half_mass(mass);
        auto& heavy = r.left.mass_kg >= r.right.mass_kg ? r.left : r.right;
        auto& light = r.left.mass_kg >= r.right.mass_kg ? r.right : r.left;
        const double delta = heavy.mass_kg - half;
        if (delta < 0) precision("Normalized corners cannot satisfy positive half-mass rebalance");
        r.rebalance_transfer_mass_kg = delta;
        if (delta > 0) {
            const double dp = share(heavy.momentum_kg_m_per_s, delta, heavy.mass_kg), dh = share(heavy.kinetic_j, delta, heavy.mass_kg);
            DD hp(heavy.momentum_kg_m_per_s); hp.add(-dp); DD hh(heavy.kinetic_j); hh.add(-dh);
            DD lp(light.momentum_kg_m_per_s); lp.add(dp); DD lh(light.kinetic_j); lh.add(dh);
            heavy.momentum_kg_m_per_s = hp.value(); heavy.kinetic_j = hh.value(); light.momentum_kg_m_per_s = lp.value(); light.kinetic_j = lh.value();
        }
        r.left.mass_kg = r.right.mass_kg = half;
        r.rebalance_momentum_residual_kg_m_per_s = difference(sum_pair(r.left.momentum_kg_m_per_s, r.right.momentum_kg_m_per_s), sum_pair(ns.momentum_kg_m_per_s, nl.momentum_kg_m_per_s)).value();
        r.rebalance_kinetic_residual_j = difference(sum_pair(r.left.kinetic_j, r.right.kinetic_j), sum_pair(ns.kinetic_j, nl.kinetic_j)).value();
        if (std::abs(r.rebalance_momentum_residual_kg_m_per_s) > 32 * eps * p_abs || std::abs(r.rebalance_kinetic_residual_j) > 32 * eps * h_abs)
            precision("Half-mass rebalance failed represented P/H conservation");
        balanced[2 * i] = r.left; balanced[2 * i + 1] = r.right;
        r.transported_nonreference_energy_j = primal[i].nonreference.value();
    }
    for (std::size_t j = 0; j <= count; ++j) {
        const auto left = j == 0 ? StaggeredMechanicalPacket{} : balanced[2 * j - 1];
        const auto right = j == count ? StaggeredMechanicalPacket{} : balanced[2 * j];
        auto& node = state.nodes_[j]; auto& r = report.nodes[j];
        node.relative_position_m = plan.node_relative_positions_m[j]; node.inertia_kg = sum_pair(left.mass_kg, right.mass_kg).value();
        node.fixed = (j == 0 && source.nodes_.front().fixed) || (j == count && source.nodes_.back().fixed);
        node.momentum_kg_m_per_s = sum_pair(left.momentum_kg_m_per_s, right.momentum_kg_m_per_s).value();
        r.inertia_kg = node.inertia_kg; r.provisional_momentum_kg_m_per_s = node.momentum_kg_m_per_s; r.provisional_kinetic_j = node_kinetic(node);
        r.transported_kinetic_j = sum_pair(left.kinetic_j, right.kinetic_j).value();
        r.mixing_heat_j = difference(sum_pair(left.kinetic_j, right.kinetic_j), DD(r.provisional_kinetic_j)).value();
        r.mixing_arithmetic_bound_j = 64 * eps * (r.transported_kinetic_j + r.provisional_kinetic_j);
        if (r.mixing_heat_j < -r.mixing_arithmetic_bound_j) precision("Positive mechanical packets violate kinetic convexity");
        if (node.fixed) { r.wall_impulse_kg_m_per_s = -node.momentum_kg_m_per_s; r.wall_heat_j = r.provisional_kinetic_j; node.momentum_kg_m_per_s = 0; }
        const auto allocate = [&](double energy, bool wall) {
            if (j == 0) { (wall ? report.cells[0].wall_heat_j : report.cells[0].mixing_heat_j) += energy; return; }
            if (j == count) { (wall ? report.cells[count - 1].wall_heat_j : report.cells[count - 1].mixing_heat_j) += energy; return; }
            const auto amounts = distribute(energy, std::vector<double>{left.mass_kg, right.mass_kg}, node.inertia_kg);
            (wall ? report.cells[j - 1].wall_heat_j : report.cells[j - 1].mixing_heat_j) += amounts[0];
            (wall ? report.cells[j].wall_heat_j : report.cells[j].mixing_heat_j) += amounts[1];
        };
        allocate(r.mixing_heat_j, false); allocate(r.wall_heat_j, true);
        report.mixing_heat_j += r.mixing_heat_j; report.wall_heat_j += r.wall_heat_j; report.wall_impulse_kg_m_per_s += r.wall_impulse_kg_m_per_s;
    }
    DD new_energy, new_kinetic, new_potential, new_momentum, new_entropy;
    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = state.cells_[i]; auto& r = report.cells[i]; const double mass = cell.mixture.mass();
        const double pe = local_potential(mass, state.gravity_, midpoint(DD(plan.node_relative_positions_m[i]), DD(plan.node_relative_positions_m[i + 1])));
        DD relocation = primal[i].potential; relocation.add(-pe); r.gravity_relocation_j = relocation.value();
        auto energy = formation(*state.model_, cell.mixture, cell.occupied_volume_m3); energy.add(primal[i].nonreference);
        energy.add(r.mixing_heat_j); energy.add(r.arithmetic_energy_j); energy.add(r.wall_heat_j); energy.add(r.gravity_relocation_j);
        cell.mixture.energy = energy.value(); const auto u_roundoff = difference(DD(cell.mixture.energy), energy);
        r.stored_u_roundoff_j = u_roundoff.value(); if (r.stored_u_roundoff_j != 0) ++report.unresolved_u_updates;
        const double ul = state.nodes_[i].velocity_m_per_s(), ur = state.nodes_[i + 1].velocity_m_per_s();
        cell.bulk_momentum_kg_m_per_s = positive_product(mass, std::midpoint(ul, ur));
        const double difference_velocity = ur - ul; finite(difference_velocity);
        if (difference_velocity != 0 && .5 * difference_velocity == 0) precision("Remapped mechanical variance is unrepresentable");
        cell.mechanical_variance_j = kinetic(mass, .5 * difference_velocity);
        cell.thermodynamics = state.model_->evaluate(cell.mixture, cell.occupied_volume_m3);
        if (cell.thermodynamics.vacuum_volume_m3 > 32 * eps * cell.occupied_volume_m3)
            fail(FluidFatalCode::unsupported_topology, "Remap creates unsupported internal liquid void");
        new_energy.add(cell.mixture.energy); new_potential.add(pe); new_entropy.add(cell.thermodynamics.entropy_j_per_k);
        report.gravity_relocation_j += r.gravity_relocation_j; report.arithmetic_energy_j += r.arithmetic_energy_j;
        physical_energy_scale += cell.thermodynamics.thermal_energy_j + cell.thermodynamics.elastic_energy_j + std::abs(r.gravity_relocation_j);
        entropy_scale += cell.thermodynamics.intrinsic_heat_capacity_j_per_k + std::abs(cell.thermodynamics.entropy_j_per_k);
        entropy_closure_bound += (std::abs(cell.thermodynamics.energy_residual_j) +
            std::abs(cell.thermodynamics.mechanical_pressure_pa * cell.thermodynamics.volume_residual_m3)) / cell.thermodynamics.temperature_k;
    }
    for (const auto& node : state.nodes_) {
        new_momentum.add(node.momentum_kg_m_per_s); new_kinetic.add(node_kinetic(node)); momentum_scale += std::abs(node.momentum_kg_m_per_s);
    }
    static_cast<void>(state.totals()); // independently range-check all published totals
    report.species_mass_residual_kg.resize(species_count); report.species_mass_bound_kg.resize(species_count);
    for (std::size_t s = 0; s < species_count; ++s) {
        report.species_mass_residual_kg[s] = difference(new_species[s], old_species[s]).value();
        report.species_mass_bound_kg[s] = checked(species_bound[s] + 16 * static_cast<long double>(eps) * old_species[s].value());
        if (std::abs(report.species_mass_residual_kg[s]) > report.species_mass_bound_kg[s] ||
            std::abs(report.species_mass_residual_kg[s]) > 128 * eps * old_species[s].value())
            precision("Remap failed constituent-local global mass conservation");
    }
    auto momentum_error = difference(new_momentum, old_momentum); momentum_error.add(-report.wall_impulse_kg_m_per_s);
    report.momentum_residual_kg_m_per_s = momentum_error.value();
    report.momentum_arithmetic_bound_kg_m_per_s = checked(packet_momentum_bound + normalization_momentum_bound + 64 * eps * momentum_scale);
    if (std::abs(report.momentum_residual_kg_m_per_s) > report.momentum_arithmetic_bound_kg_m_per_s)
        precision("Remap failed actual global momentum conservation");
    auto energy_error = difference(new_energy, old_energy); energy_error.add(difference(new_kinetic, old_kinetic)); energy_error.add(difference(new_potential, old_potential));
    report.energy_residual_j = energy_error.value();
    report.energy_arithmetic_bound_j = checked(256 * static_cast<long double>(eps) * (physical_energy_scale + old_kinetic.value() + new_kinetic.value() +
        std::abs(old_potential.value()) + std::abs(new_potential.value())));
    if (std::abs(report.energy_residual_j) > report.energy_arithmetic_bound_j)
        precision("Remap failed actual energy balance at thermal/elastic/mechanical scales");
    report.entropy_change_j_per_k = difference(new_entropy, old_entropy).value();
    report.entropy_arithmetic_bound_j_per_k = checked(128 * static_cast<long double>(eps) * entropy_scale + entropy_closure_bound);
    if (report.entropy_change_j_per_k < -report.entropy_arithmetic_bound_j_per_k)
        fail(FluidFatalCode::unsupported_configuration, "Remap materially decreases actual EOS entropy");
    state.cumulative_energy_error_ += report.energy_residual_j; finite(state.cumulative_energy_error_);
    state.cumulative_geometry_error_ += report.geometry_volume_residual_m3; finite(state.cumulative_geometry_error_);
    return result;
}
StaggeredRemapReport rezone_occupied_staggered_into(OccupiedStaggeredState& state, const StaggeredRezonePlan& plan) {
    auto candidate = rezone_occupied_staggered(state, plan);
    static_assert(std::is_nothrow_move_assignable_v<OccupiedStaggeredState>);
    state = std::move(candidate.state); return std::move(candidate.report);
}
}
