#include "mixed_atmospheres/occupied_staggered_hybrid.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace ma::experimental {
namespace {
constexpr double eps = std::numeric_limits<double>::epsilon();
[[noreturn]] void configuration(const char* message) { throw FluidFatalError(FluidFatalCode::unsupported_configuration, message); }
[[noreturn]] void topology(const char* message) { throw FluidFatalError(FluidFatalCode::unsupported_topology, message); }
[[noreturn]] void precision(const char* message) { throw FluidFatalError(FluidFatalCode::numerical_precision, message); }
void finite(double x) { if (!std::isfinite(x)) precision("Nonfinite hybrid geometry arithmetic"); }
std::size_t add_count(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a) precision("Hybrid geometry count overflow");
    return a + b;
}
void capacity(std::size_t n, std::size_t maximum, const char* message) { if (n > maximum) configuration(message); }
template<class T> void reservable(std::size_t n) {
    if (n > std::vector<T>().max_size()) precision("Hybrid geometry allocation count is outside representable range");
}
// Two-component local geometry. No dependence on a wider GNU long double.
struct DD {
    double hi = 0, lo = 0;
    DD() = default;
    explicit DD(double x) : hi(x) {}
    void add(double x) {
        const double s = hi + x, z = s - hi;
        const double e = (hi - (s - z)) + (x - z);
        const double t = lo + e, h = s + t, q = h - s;
        lo = (s - (h - q)) + (t - q); hi = h; finite(hi); finite(lo);
    }
    void add(DD x) { add(x.hi); add(x.lo); }
    void subtract(DD x) { add(-x.hi); add(-x.lo); }
    [[nodiscard]] double value() const { const double x = hi + lo; finite(x); return x; }
};
DD difference(DD a, DD b) { a.subtract(b); return a; }
int sign(DD x) { return x.hi > 0 ? 1 : x.hi < 0 ? -1 : x.lo > 0 ? 1 : x.lo < 0 ? -1 : 0; }
bool less(DD a, DD b) { return sign(difference(a, b)) < 0; }
DD product(double a, double b) {
    const double p = a * b; finite(p); DD result(p); result.add(std::fma(a, b, -p)); return result;
}
// A grid line sums four exact binary64 terms. Unlike the two-component
// geometry accumulator, this expansion never discards a low merge remainder
// before later origin cancellation. General TwoSum is error-free with gradual
// underflow; any nonfinite intermediate rejects rather than claiming a bound.
struct ExactGridSum {
    std::array<double, 5> terms{};
    std::size_t count = 0;
    static std::pair<double, double> two_sum(double a, double b) {
        const double s = a + b; finite(s);
        const double z = s - a; finite(z);
        const double av = s - z, ar = a - av, br = b - z;
        finite(av); finite(ar); finite(br);
        const double remainder = ar + br; finite(remainder);
        return {s, remainder};
    }
    void add(double x) {
        std::size_t output = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto [sum, remainder] = two_sum(x, terms[i]);
            if (remainder != 0) terms[output++] = remainder;
            x = sum;
        }
        if (x != 0 || output == 0) {
            if (output == terms.size()) precision("Hybrid exact grid expansion capacity exceeded");
            terms[output++] = x;
        }
        count = output;
    }
    [[nodiscard]] double represented() const {
        double result = 0;
        for (std::size_t i = 0; i < count; ++i) { result += terms[i]; finite(result); }
        return result;
    }
    [[nodiscard]] double absolute_upper_bound() const {
        double result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto [sum, remainder] = two_sum(result, std::abs(terms[i]));
            // Carry an upward bound even when a positive low term is below
            // half an ulp. An exact zero residual retains a zero bound.
            result = remainder > 0 ? std::nextafter(sum, std::numeric_limits<double>::infinity()) : sum;
            finite(result);
        }
        return result;
    }
};
bool within_grid_line_budget(double error_bound, double spacing) {
    if (error_bound == 0) return true;
    // 32*epsilon = 2^(6-digits). Compare exponents instead of multiplying a
    // subnormal spacing by this factor and underflowing the physical budget.
    static_assert(std::numeric_limits<double>::radix == 2 && std::numeric_limits<double>::digits == 53);
    int error_exponent = 0, spacing_exponent = 0;
    const double error_fraction = std::frexp(error_bound, &error_exponent);
    const double spacing_fraction = std::frexp(spacing, &spacing_exponent);
    const int budget_exponent = spacing_exponent + 6 - std::numeric_limits<double>::digits;
    return error_exponent < budget_exponent || (error_exponent == budget_exponent && error_fraction <= spacing_fraction);
}
DD midpoint(DD a, DD b) {
    const auto width = difference(b, a); DD result = a;
    result.add(.5 * width.hi); result.add(.5 * width.lo);
    if (!less(a, result) || !less(result, b)) precision("Positive hybrid virtual half interval is unrepresentable");
    return result;
}
double width(DD a, DD b) {
    const auto exact = difference(b, a); const double w = exact.value();
    if (!(w > 0) || std::abs(difference(exact, DD(w)).value()) > 32 * eps * w)
        precision("Positive hybrid interval is unresolved on its local width scale");
    return w;
}
double volume(double area, double w) {
    const double result = area * w; finite(result);
    if (!(result > 0)) precision("Positive hybrid volume is unrepresentable");
    // Normalize before multiplying: a subnormal result must satisfy the same
    // relative certificate as a normal result, not a rounded absolute bound.
    int ea = 0, ew = 0, er = 0;
    const double a = std::frexp(area, &ea), x = std::frexp(w, &ew), r = std::frexp(result, &er);
    const int expected_exponent = ea + ew, common = std::max(expected_exponent, er);
    auto expected = product(a, x); expected.hi = std::ldexp(expected.hi, expected_exponent - common);
    expected.lo = std::ldexp(expected.lo, expected_exponent - common);
    const double actual = std::ldexp(r, er - common);
    if (std::abs(difference(DD(actual), expected).value()) > 32 * eps * std::abs(expected.value()))
        precision("Hybrid volume product has material relative rounding error");
    return result;
}
double local_bound(double a, double b) { return (64 * eps) * std::abs(a) + (64 * eps) * std::abs(b); }
std::uint64_t issue_lineage() {
    static std::atomic<std::uint64_t> next{1}; auto n = next.load(std::memory_order_relaxed);
    for (;;) {
        if (n == std::numeric_limits<std::uint64_t>::max()) configuration("Hybrid logical source identity space exhausted");
        if (next.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) return n;
    }
}
void bound_source(const HybridStaggeredState& source) {
    if (!source.bound() ||
        source.source_cell_identities().size() != source.canonical().cells().size())
        configuration("Unbound or unsupported hybrid logical source revision");
    const auto count = source.canonical().cells().size();
    if (source.material_caps().size() != (count == 0 ? 0 : count == 1 ? 1 : 2))
        configuration("Hybrid protected material-cap count changed");
    for (const auto& cap : source.material_caps()) {
        const auto slot = cap.identity.kind == HybridCellKind::right_material_cap ? count - 1 : 0;
        if (cap.identity.material.lineage != source.identity().lineage || cap.identity.material.ordinal == 0 ||
            source.source_cell_identities()[slot] != cap.identity ||
            source.canonical().cells()[slot].mixture.masses != cap.original_species_masses_kg)
            configuration("Hybrid protected material identity or species changed");
    }
}
struct Grid {
    StaggeredCartesianGrid requested;
    double maximum_line_error = 0, maximum_spacing_error = 0;
    explicit Grid(StaggeredCartesianGrid input) : requested(input) {
        if (!std::isfinite(input.spacing_m) || !(input.spacing_m > 0) ||
            !std::isfinite(input.origin_relative_high_m) || !std::isfinite(input.origin_relative_low_m) ||
            input.end_cell <= input.first_cell || input.first_cell < -(INT64_C(1) << 52) || input.end_cell > (INT64_C(1) << 52))
            configuration("Invalid hybrid preferred Cartesian grid");
        const double first = line(input.first_cell), end = line(input.end_cell);
        if (!(end > first)) precision("Hybrid fixed-grid domain is not positively represented");
    }
    double line(std::int64_t k) {
        // k is an exactly represented integer. Its product with binary64 h
        // is a multiple of denorm_min, so this FMA residual is representable
        // even at subnormal scales (overflow was rejected above).
        const double factor = static_cast<double>(k), high = factor * requested.spacing_m; finite(high);
        ExactGridSum exact; exact.add(high); exact.add(std::fma(factor, requested.spacing_m, -high));
        exact.add(requested.origin_relative_high_m); exact.add(requested.origin_relative_low_m);
        const double represented = exact.represented();
        exact.add(-represented);
        const double error = exact.absolute_upper_bound();
        if (!within_grid_line_budget(error, requested.spacing_m)) precision("Hybrid fixed-grid line is unresolved at its local spacing");
        maximum_line_error = std::max(maximum_line_error, error); return represented;
    }
    double spacing(std::int64_t k, double a, double b) {
        const auto actual = difference(DD(b), DD(a)); const double represented = actual.value();
        if (!(represented > 0)) precision("Hybrid fixed-grid bin is not positively represented");
        const double error = std::abs(difference(actual, DD(requested.spacing_m)).value());
        if (error > 32 * eps * requested.spacing_m) precision("Hybrid fixed-grid spacing has material local roundoff");
        maximum_spacing_error = std::max(maximum_spacing_error, error);
        if (k < requested.first_cell || k >= requested.end_cell) configuration("Hybrid grid bin is out of bounds");
        return represented;
    }
    template<class Predicate> std::int64_t lower_bound(Predicate predicate) {
        auto lo = requested.first_cell, hi = requested.end_cell;
        if (!predicate(line(hi))) return hi + 1; // bounded sentinel, never evaluated as a grid line
        while (lo < hi) { const auto mid = lo + (hi - lo) / 2; if (predicate(line(mid))) hi = mid; else lo = mid + 1; }
        return lo;
    }
    std::int64_t containing_left(double q) {
        auto lo = requested.first_cell, hi = requested.end_cell;
        while (hi - lo > 1) { const auto mid = lo + (hi - lo) / 2; if (line(mid) <= q) lo = mid; else hi = mid; }
        return lo;
    }
};
// Random-access target geometry, with no allocation. All counts and overlaps
// can therefore be certified before target/coverage arrays are allocated.
struct Layout {
    const HybridStaggeredState& source;
    Grid& grid;
    std::size_t original = 0, cells = 0, eligible = 0;
    std::int64_t first_line = 0;
    double a = 0, b = 0;
    bool explicit_material = false;
    std::span<const double> explicit_nodes;
    Layout(const HybridStaggeredState& input, Grid& fixed) : source(input), grid(fixed), original(input.canonical().cells().size()) {
        if (original == 0) return;
        const auto nodes = input.canonical().nodes();
        if (nodes.front().relative_position_m < grid.line(grid.requested.first_cell) ||
            nodes.back().relative_position_m > grid.line(grid.requested.end_cell))
            topology("Hybrid occupied slab overruns the fixed-grid domain");
        if (original <= 2) { cells = original; return; }
        a = nodes[1].relative_position_m; b = nodes[original - 1].relative_position_m;
        (void)width(DD(a), DD(b));
        first_line = grid.lower_bound([&](double x) { return !less(difference(DD(x), DD(a)), DD(grid.requested.spacing_m)); });
        const auto after_last = grid.lower_bound([&](double x) { return less(difference(DD(b), DD(x)), DD(grid.requested.spacing_m)); });
        if (first_line < after_last) {
            const auto count = static_cast<std::uint64_t>(after_last - first_line);
            if (count > std::numeric_limits<std::size_t>::max()) precision("Hybrid eligible-line count overflow");
            eligible = static_cast<std::size_t>(count);
        }
        // Every no-eligible-line positive gap is one actual interval. Its
        // width can exceed 2h (up to nearly 3h, depending on grid phase).
        cells = add_count(eligible, 3);
    }
    Layout(const HybridStaggeredState& input, Grid& fixed, std::span<const double> target)
        : source(input), grid(fixed), original(input.canonical().cells().size()), explicit_material(true), explicit_nodes(target) {
        if (original == 0) {
            if (!target.empty()) topology("Exact vacuum cannot acquire an explicit material target");
            return;
        }
        if (target.size() < 2) topology("Explicit hybrid target has no positive material interval");
        cells = target.size() - 1;
        capacity(cells, input.limits().maximum_material_cells, "Explicit hybrid target material-cell capacity exceeded");
        const auto old = input.canonical().nodes();
        for (std::size_t j = 0; j < target.size(); ++j) {
            if (!std::isfinite(target[j])) configuration("Nonfinite explicit hybrid target node");
            if (j != 0 && !(target[j] > target[j - 1])) topology("Explicit hybrid target is not a connected positive partition");
        }
        const auto same_position = [](double first, double second) { return std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second); };
        if (!same_position(target.front(),old.front().relative_position_m) || !same_position(target.back(),old.back().relative_position_m))
            topology("Explicit hybrid target moves a protected physical endpoint");
        if (original <= 2) {
            if (cells != original) topology("Whole slab or adjacent material caps cannot be repartitioned");
            for (std::size_t j = 0; j < target.size(); ++j)
                if (!same_position(target[j],old[j].relative_position_m)) topology("Explicit hybrid target moves protected cap geometry");
        } else {
            if (cells < 3 || !same_position(target[1],old[1].relative_position_m) || !same_position(target[target.size() - 2],old[original - 1].relative_position_m))
                topology("Explicit hybrid target changes a finite protected cap interval");
            a = target[1]; b = target[target.size() - 2];
        }
        if (target.front() < grid.line(grid.requested.first_cell) || target.back() > grid.line(grid.requested.end_cell))
            topology("Explicit hybrid occupied slab overruns its coverage grid");
    }
    double node(std::size_t j) {
        if (j > cells || cells == 0) configuration("Hybrid target node slot out of bounds");
        if (explicit_material) return explicit_nodes[j];
        const auto old = source.canonical().nodes();
        if (original <= 2) return old[j].relative_position_m;
        if (j == 0) return old.front().relative_position_m;
        if (j == 1) return a;
        if (j <= eligible + 1) return grid.line(first_line + static_cast<std::int64_t>(j - 2));
        return j == cells - 1 ? b : old.back().relative_position_m;
    }
    HybridCellIdentity cell_identity(std::size_t i) const {
        if (i >= cells) configuration("Hybrid target cell slot out of bounds");
        const auto old = source.source_cell_identities();
        if (original <= 2) return old[i];
        if (i == 0) return old.front();
        if (i + 1 == cells) return old.back();
        if (explicit_material) return {HybridCellKind::unstructured_interior, {}, 0};
        if (eligible == 0) return {HybridCellKind::whole_interior_gap, {}, 0};
        if (i == 1) return {HybridCellKind::left_transition, {}, 0};
        if (i == cells - 2) return {HybridCellKind::right_transition, {}, 0};
        return {HybridCellKind::interior_cartesian, {}, first_line + static_cast<std::int64_t>(i - 2)};
    }
};
template<class Position> void validate_intervals(std::size_t count, Position at, double area) {
    for (std::size_t i = 0; i < count; ++i) (void)volume(area, width(DD(at(i)), DD(at(i + 1))));
}
template<class Position> DD corner_at(std::size_t index, Position at) {
    const auto cell = index / 2; const DD a(at(cell));
    return index % 2 == 0 ? a : midpoint(a, DD(at(cell + 1)));
}
template<class A, class B> std::size_t positive_overlaps(std::size_t count_a, A at_a, std::size_t count_b, B at_b,
                                                       std::size_t already, std::size_t maximum) {
    std::size_t i = 0, j = 0, count = 0;
    while (i < count_a && j < count_b) {
        const auto al = at_a(i), ar = at_a(i + 1), bl = at_b(j), br = at_b(j + 1);
        const auto a = less(al, bl) ? bl : al, b = less(ar, br) ? ar : br;
        if (less(a, b)) {
            (void)width(a, b); count = add_count(count, 1);
            capacity(add_count(already, count), maximum, "Hybrid positive overlap capacity exceeded");
        }
        const bool advance_a = !less(br, ar), advance_b = !less(ar, br);
        if (advance_a) ++i;
        if (advance_b) ++j;
    }
    if (i != count_a || j != count_b) precision("Hybrid positive overlap partition has incomplete coverage");
    return count;
}
struct OverlapCounts { std::size_t primal = 0, corner = 0; };
OverlapCounts count_overlaps(Layout& layout, std::size_t maximum) {
    if (layout.cells == 0) return {};
    const std::size_t caps = layout.original == 1 ? 1 : 2;
    OverlapCounts result{caps, add_count(caps, caps)};
    capacity(add_count(result.primal, result.corner), maximum, "Hybrid protected identity overlap capacity exceeded");
    if (layout.original <= 2) return result;
    const auto old = [&](std::size_t i) { return layout.source.canonical().nodes()[i + 1].relative_position_m; };
    const auto target = [&](std::size_t i) { return layout.node(i + 1); };
    const auto old_cells = layout.original - 2, target_cells = layout.cells - 2;
    const auto primal = positive_overlaps(old_cells, [&](std::size_t i) { return DD(old(i)); }, target_cells,
        [&](std::size_t i) { return DD(target(i)); }, add_count(result.primal, result.corner), maximum);
    result.primal = add_count(result.primal, primal);
    const auto corner = positive_overlaps(add_count(old_cells, old_cells), [&](std::size_t i) { return corner_at(i, old); },
        add_count(target_cells, target_cells), [&](std::size_t i) { return corner_at(i, target); },
        add_count(result.primal, result.corner), maximum);
    result.corner = add_count(result.corner, corner); return result;
}
template<class Position, class Record, class Balance>
std::size_t coverage(std::size_t cell_count, Position position, double area, Grid& grid, HybridCoveragePartition partition,
                     std::size_t already, std::size_t maximum, Record record, Balance balance) {
    if (cell_count == 0) return 0;
    std::size_t count = 0; auto k = grid.containing_left(position(0));
    for (std::size_t i = 0; i < cell_count; ++i) {
        const double cell_left = position(i), cell_right = position(i + 1);
        const double expected_width = width(DD(cell_left), DD(cell_right));
        HybridCoverageBalance check; check.partition = partition; check.cell_slot = i;
        check.expected_width_m = expected_width; check.expected_volume_m3 = volume(area, expected_width);
        DD sum_width, sum_volume;
        while (k < grid.requested.end_cell) {
            const double gl = grid.line(k), gr = grid.line(k + 1); (void)grid.spacing(k, gl, gr);
            const double a = std::max(cell_left, gl), b = std::min(cell_right, gr);
            if (!(b > a)) precision("Hybrid fixed-grid coverage lost a positive interval");
            const double w = width(DD(a), DD(b)), v = volume(area, w);
            count = add_count(count, 1); capacity(add_count(already, count), maximum, "Hybrid fixed-grid coverage capacity exceeded");
            sum_width.add(w); sum_volume.add(v); ++check.records;
            record(HybridCoverageRecord{partition, i, k, a, b, w, v});
            if (gr <= cell_right) ++k;
            if (cell_right <= gr) break;
        }
        check.covered_width_m = sum_width.value(); check.covered_volume_m3 = sum_volume.value();
        check.width_residual_m = difference(sum_width, DD(check.expected_width_m)).value();
        check.volume_residual_m3 = difference(sum_volume, DD(check.expected_volume_m3)).value();
        // Positive record products have individual local certificates. This
        // additional aggregate certificate uses only this interval's actual
        // width/volume and never a unit, species or formation-energy floor.
        if (static_cast<double>(check.records) * eps > 1.0 / 64)
            precision("Hybrid compensated coverage sum is outside its operation-count domain");
        check.width_arithmetic_bound_m = local_bound(check.expected_width_m, check.covered_width_m);
        check.volume_arithmetic_bound_m3 = local_bound(check.expected_volume_m3, check.covered_volume_m3);
        if (std::abs(check.width_residual_m) > check.width_arithmetic_bound_m ||
            std::abs(check.volume_residual_m3) > check.volume_arithmetic_bound_m3)
            precision("Hybrid fixed-grid coverage fails its local width/volume balance");
        balance(check);
    }
    return count;
}
}

HybridStaggeredState& HybridStaggeredState::operator=(const HybridStaggeredState& other) {
    if (this != &other) { HybridStaggeredState candidate(other); *this = std::move(candidate); }
    return *this;
}
HybridRezonePlan& HybridRezonePlan::operator=(const HybridRezonePlan& other) {
    if (this != &other) { HybridRezonePlan candidate(other); *this = std::move(candidate); }
    return *this;
}
HybridStaggeredState bind_material_endcaps(const OccupiedStaggeredState& original, const HybridGeometryLimits& limits) {
    if (!original.initialized()) configuration("Cannot bind an uninitialized retained material state");
    const auto count = original.cells().size();
    capacity(count, limits.maximum_material_cells, "Hybrid source material-cell capacity exceeded");
    reservable<HybridCellIdentity>(count);
    HybridStaggeredState result; result.limits_ = limits;
    result.canonical_ = original; result.source_cells_.resize(count);
    const std::size_t caps = count == 0 ? 0 : count == 1 ? 1 : 2;
    result.caps_.reserve(caps);
    for (std::size_t i = 0; i < count; ++i) {
        auto& cell = result.source_cells_[i]; cell.material.ordinal = static_cast<std::uint64_t>(i) + 1;
        cell.kind = count == 1 ? HybridCellKind::whole_material_slab :
            i == 0 ? HybridCellKind::left_material_cap : i + 1 == count ? HybridCellKind::right_material_cap : HybridCellKind::source_material;
        if (cell.kind != HybridCellKind::source_material) {
            const auto& inventory = original.cells()[i].mixture; const double mass = inventory.mass(), half = .5 * mass;
            if (!(half > 0) || !std::isfinite(half)) precision("Hybrid protected half mass is not positively representable");
            auto error = product(half, 2); error.add(-mass);
            if (std::abs(error.value()) > 32 * eps * mass) precision("Hybrid protected half mass has material relative roundoff");
            result.caps_.push_back({cell, i, inventory.masses});
        }
    }
    const auto logical = issue_lineage(); result.identity_ = {logical, 0, logical};
    for (auto& cell : result.source_cells_) cell.material.lineage = result.identity_.lineage;
    for (auto& cap : result.caps_) cap.identity = result.source_cells_[cap.initial_source_cell];
    return result;
}
HybridSourceIdentity HybridStaggeredState::next_identity(HybridSourceIdentity prior) {
    if (prior.revision == std::numeric_limits<std::uint64_t>::max()) configuration("Hybrid accepted-rezone revision is exhausted");
    return {prior.lineage, prior.revision + 1, issue_lineage()};
}
HybridRezonePlan HybridRezonePlan::build(const HybridStaggeredState& source, const StaggeredCartesianGrid& requested,
                                       std::span<const double> target, bool explicit_material) {
    bound_source(source); Grid grid(requested);
    Layout layout = explicit_material ? Layout(source, grid, target) : Layout(source, grid);
    const auto& limits = source.limits();
    capacity(layout.cells, limits.maximum_material_cells, "Hybrid planned material-cell capacity exceeded");
    const auto node_count = layout.cells == 0 ? 0 : add_count(layout.cells, 1);
    reservable<double>(node_count); reservable<HybridPlannedCell>(layout.cells);
    const auto source_position = [&](std::size_t i) { return source.canonical().nodes()[i].relative_position_m; };
    const auto target_position = [&](std::size_t i) { return layout.node(i); };
    const double area = source.canonical().area_m2();
    // Each connected primal partition needs at least max(Nold,Nnew)
    // positive overlaps; its corner partition needs twice that many. Check
    // this lower bound before even walking a potentially enormous target.
    const auto minimum_primal = std::max(layout.original, layout.cells);
    capacity(add_count(minimum_primal, add_count(minimum_primal, minimum_primal)), limits.maximum_overlap_records,
        "Hybrid positive overlap lower bound exceeds capacity");
    if (layout.original != 0) {
        const auto first = grid.containing_left(source_position(0));
        const auto after_last = grid.lower_bound([&](double x) { return x >= source_position(layout.original); });
        const auto wet = static_cast<std::uint64_t>(after_last - first);
        if (wet > std::numeric_limits<std::size_t>::max()) precision("Hybrid occupied grid-bin count overflow");
        const auto bins = static_cast<std::size_t>(wet);
        capacity(add_count(std::max(bins, layout.original), std::max(bins, layout.cells)), limits.maximum_coverage_records,
            "Hybrid source/target coverage lower bound exceeds capacity");
    }
    validate_intervals(layout.original, source_position, area); validate_intervals(layout.cells, target_position, area);
    const auto overlaps = count_overlaps(layout, limits.maximum_overlap_records);
    const auto ignore_record = [](const HybridCoverageRecord&) {};
    const auto ignore_balance = [](const HybridCoverageBalance&) {};
    const auto source_records = coverage(layout.original, source_position, area, grid, HybridCoveragePartition::authoritative_source,
        0, limits.maximum_coverage_records, ignore_record, ignore_balance);
    const auto target_records = coverage(layout.cells, target_position, area, grid, HybridCoveragePartition::planned_target,
        source_records, limits.maximum_coverage_records, ignore_record, ignore_balance);
    reservable<HybridCoverageRecord>(source_records); reservable<HybridCoverageRecord>(target_records);
    reservable<HybridCoverageBalance>(layout.original); reservable<HybridCoverageBalance>(layout.cells);
    // All cell, overlap and both coverage counts have passed before any plan
    // array allocation. Source and target geometry remain separate inventories.
    HybridRezonePlan result; result.source_ = source.identity(); result.limits_ = limits; result.grid_ = requested;
    result.nodes_.reserve(node_count); result.cells_.reserve(layout.cells);
    result.caps_.reserve(source.material_caps().size());
    result.source_coverage_.reserve(source_records); result.target_coverage_.reserve(target_records);
    result.source_balances_.reserve(layout.original); result.target_balances_.reserve(layout.cells);
    auto& d = result.diagnostics_; d.source_cells = layout.original; d.planned_target_cells = layout.cells;
    d.eligible_grid_lines = layout.eligible; d.primal_overlap_records = overlaps.primal; d.corner_overlap_records = overlaps.corner;
    d.source_coverage_records = source_records; d.planned_target_coverage_records = target_records;
    d.preferred_cartesian_partition = !explicit_material;
    d.has_interior_gap = layout.original > 2; d.no_eligible_grid_line = !explicit_material && d.has_interior_gap && layout.eligible == 0;
    if (d.has_interior_gap) d.interior_gap_width_m = width(DD(layout.a), DD(layout.b));
    for (std::size_t j = 0; j < node_count; ++j) result.nodes_.push_back(layout.node(j));
    for (std::size_t i = 0; i < layout.cells; ++i) {
        const double a = result.nodes_[i], b = result.nodes_[i + 1], w = width(DD(a), DD(b)); const auto identity = layout.cell_identity(i);
        result.cells_.push_back({identity, a, b, w, volume(area, w)});
        d.minimum_planned_width_m = i == 0 ? w : std::min(d.minimum_planned_width_m, w);
        d.maximum_planned_width_m = std::max(d.maximum_planned_width_m, w);
        if (identity.kind == HybridCellKind::whole_interior_gap) d.whole_gap_width_m = w;
        if (identity.kind == HybridCellKind::left_transition) d.left_transition_width_m = w;
        if (identity.kind == HybridCellKind::right_transition) d.right_transition_width_m = w;
    }
    for (const auto& cap : source.material_caps()) {
        const auto from = cap.identity.kind == HybridCellKind::right_material_cap ? layout.original - 1 : 0;
        const auto to = cap.identity.kind == HybridCellKind::right_material_cap ? layout.cells - 1 : 0;
        result.caps_.push_back({cap.identity.material, from, to});
    }
    const auto written_source = coverage(layout.original, source_position, area, grid, HybridCoveragePartition::authoritative_source,
        0, limits.maximum_coverage_records, [&](auto r) { result.source_coverage_.push_back(r); }, [&](auto b) { result.source_balances_.push_back(b); });
    const auto written_target = coverage(layout.cells, target_position, area, grid, HybridCoveragePartition::planned_target,
        written_source, limits.maximum_coverage_records, [&](auto r) { result.target_coverage_.push_back(r); }, [&](auto b) { result.target_balances_.push_back(b); });
    if (written_source != source_records || written_target != target_records) precision("Hybrid immutable coverage changed between passes");
    d.maximum_grid_line_roundoff_m = grid.maximum_line_error; d.maximum_grid_spacing_residual_m = grid.maximum_spacing_error;
    validate_hybrid_rezone_plan(source, result); return result;
}
HybridRezonePlan plan_hybrid_staggered_rezone(const HybridStaggeredState& source, const StaggeredCartesianGrid& requested) {
    return HybridRezonePlan::build(source, requested, {}, false);
}
HybridRezonePlan plan_hybrid_material_rezone(const HybridStaggeredState& source, std::span<const double> target,
                                           const StaggeredCartesianGrid& coverage_grid) {
    return HybridRezonePlan::build(source, coverage_grid, target, true);
}
void validate_hybrid_rezone_plan(const HybridStaggeredState& source, const HybridRezonePlan& plan) {
    bound_source(source);
    if (plan.source_ != source.identity() || plan.limits_ != source.limits())
        configuration("Hybrid plan belongs to a stale or foreign immutable logical source");
    const auto& d = plan.diagnostics_;
    if (d.source_cells != source.canonical().cells().size() || plan.cells_.size() != d.planned_target_cells ||
        plan.nodes_.size() != (plan.cells_.empty() ? 0 : add_count(plan.cells_.size(), 1)) ||
        plan.caps_.size() != source.material_caps().size() || plan.source_coverage_.size() != d.source_coverage_records ||
        plan.target_coverage_.size() != d.planned_target_coverage_records || plan.source_balances_.size() != d.source_cells ||
        plan.target_balances_.size() != d.planned_target_cells)
        configuration("Hybrid plan is incomplete or moved from");
}
static_assert(std::is_nothrow_move_assignable_v<HybridStaggeredState>);
static_assert(std::is_nothrow_move_assignable_v<HybridRezonePlan>);
}
