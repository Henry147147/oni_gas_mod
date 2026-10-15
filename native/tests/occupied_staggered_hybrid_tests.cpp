#include "mixed_atmospheres/occupied_staggered_hybrid.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <type_traits>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void near(double a, double b, double relative = 1e-13) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > relative * std::abs(b)) {
        std::ostringstream out; out << std::setprecision(17) << "Expected " << b << ", got " << a; throw std::runtime_error(out.str());
    }
}
template<class F> void fatal(FluidFatalCode code, F&& action) {
    bool caught = false; try { action(); } catch (const FluidFatalError& e) { caught = e.code() == code; }
    check(caught, "Expected typed hybrid geometry failure");
}
struct Setup {
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> model;
    explicit Setup(bool dyadic_liquid = false) {
        for (const auto* id : {"material", "trace"}) {
            SpeciesDefinition d; d.id = id; d.name = id; d.atoms = {{"N", 2}};
            d.phase = dyadic_liquid ? Phase::liquid : Phase::gas; d.cp = dyadic_liquid ? 1 : 1040;
            d.density = 1; d.formation_enthalpy = d.cp * reference_temperature * molecular_mass(d.atoms); registry.add(d);
        }
        std::vector<ReferenceMaterial> profiles;
        for (const auto& d : registry.all()) {
            ReferenceMaterial m; m.id = d.id; m.phase = dyadic_liquid ? ReferencePhase::elastic_liquid : ReferencePhase::ideal_gas;
            m.molar_mass_kg_per_mol = d.molar_mass; m.cv_j_per_kg_k = d.cv(); m.energy_offset_j_per_kg = d.internal_energy(0);
            m.minimum_temperature_k = dyadic_liquid ? .5 : 1e-20; m.maximum_temperature_k = dyadic_liquid ? 4 : 1e9;
            m.zero_pressure_density_kg_per_m3 = dyadic_liquid ? 1 : 0;
            m.reference_bulk_modulus_pa = dyadic_liquid ? 1 : 0; m.maximum_density_ratio = dyadic_liquid ? 2 : 1;
            m.provenance = "Explicit geometry-only numerical surrogate; no physical calibration"; profiles.push_back(m);
        }
        model = std::make_shared<const FluidThermodynamics>(registry, "hybrid-geometry-A0-v1", profiles);
    }
    StaggeredInitialization input(std::vector<double> positions, double area = 1, double origin = 0,
                                 bool wall_right = false, bool liquid = false) const {
        StaggeredInitialization result; result.thermodynamics = model; result.positions_m = std::move(positions);
        result.area_m2 = area; result.frame_origin_m = origin;
        if (wall_right) result.right = StaggeredBoundary::rigid_wall;
        for (std::size_t j = 0; j < result.positions_m.size(); ++j)
            result.velocities_m_per_s.push_back(liquid || (wall_right && j + 1 == result.positions_m.size()) ? 0 : .125 * static_cast<double>(j));
        for (std::size_t i = 0; i + 1 < result.positions_m.size(); ++i) {
            const double v = area * (result.positions_m[i + 1] - result.positions_m[i]);
            const double mass = liquid ? v : v * (1 + .25 * static_cast<double>(i));
            const double tag = liquid ? 0 : mass / 8;
            const double masses[]{mass - tag, tag};
            const auto mixture = model->initialize_at_temperature(masses, liquid ? 1 : 300, v).mixture;
            result.cells.push_back({mixture, mixture.mass() * std::midpoint(result.velocities_m_per_s[i], result.velocities_m_per_s[i + 1])});
        }
        return result;
    }
    OccupiedStaggeredState make(std::vector<double> positions, double area = 1, double origin = 0, bool wall_right = false) const {
        return initialize_occupied_staggered(input(std::move(positions), area, origin, wall_right));
    }
};
std::string fingerprint(const OccupiedStaggeredState& state) {
    std::ostringstream out; out << std::hexfloat << state.model().get() << state.model_metadata();
    for (double x : {state.origin_high_m(), state.origin_low_m(), state.time_seconds(), state.area_m2(), state.gravity_m_per_s2(),
                    state.cumulative_energy_balance_error_j(), state.cumulative_geometry_volume_residual_m3()}) out << x << ';';
    const auto& l = state.initialization_ledger();
    for (double x : {l.prior_bulk_kinetic_energy_j, l.retained_kinetic_energy_j, l.additional_mechanical_energy_j,
        l.requested_internal_energy_change_j, l.represented_internal_energy_change_j, l.declared_added_energy_j,
        l.represented_total_energy_change_j, l.kinetic_partition_roundoff_j, l.momentum_reconciliation_kg_m_per_s,
        l.maximum_momentum_arithmetic_bound_kg_m_per_s}) out << x << ';';
    out << l.unresolved_energy_conversions;
    for (const auto& n : state.nodes()) out << n.relative_position_m << ';' << n.inertia_kg << ';' << n.momentum_kg_m_per_s << ';' << n.fixed;
    for (const auto& c : state.cells()) {
        for (double x : c.mixture.masses) out << x << ';';
        out << c.mixture.energy << ';' << c.occupied_volume_m3 << ';' << c.bulk_momentum_kg_m_per_s << ';' << c.mechanical_variance_j;
        const auto& e = c.thermodynamics;
        for (double x : {e.temperature_k, e.mechanical_pressure_pa, e.gas_pressure_pa, e.gas_volume_m3, e.liquid_volume_m3,
            e.vacuum_volume_m3, e.reference_energy_j, e.thermal_energy_j, e.elastic_energy_j, e.entropy_j_per_k,
            e.helmholtz_energy_j, e.intrinsic_heat_capacity_j_per_k, e.equilibrium_heat_capacity_at_cell_volume_j_per_k,
            e.volume_residual_m3, e.energy_residual_j}) out << x << ';';
        for (double x : e.partial_volumes_m3) out << x << ';';
        for (double x : e.specific_internal_energies_j_per_kg) out << x << ';';
        out << e.has_gas << e.has_vacuum << e.pressure_iterations;
    }
    return out.str();
}
struct IntegerCoverage { std::size_t slot; std::int64_t bin, a, b; };
// Independent all-pairs integer intersections: no planner, binary search,
// virtual-corner overlay or production geometry routine supplies expectations.
std::vector<IntegerCoverage> integer_coverage(const std::vector<std::int64_t>& nodes, std::int64_t origin,
                                            std::int64_t spacing, std::int64_t first, std::int64_t end) {
    std::vector<IntegerCoverage> out;
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) for (auto k = first; k < end; ++k) {
        const auto a = std::max(nodes[i], origin + k * spacing), b = std::min(nodes[i + 1], origin + (k + 1) * spacing);
        if (b > a) out.push_back({i, k, a, b});
    }
    return out;
}
std::vector<std::int64_t> integer_target(const std::vector<std::int64_t>& old, std::int64_t origin,
                                       std::int64_t spacing, std::int64_t first, std::int64_t end) {
    if (old.size() <= 3) return old;
    const auto a = old[1], b = old[old.size() - 2];
    std::vector<std::int64_t> target{old.front(), a};
    for (auto k = first; k <= end; ++k) {
        const auto line = origin + k * spacing;
        if (line - a >= spacing && b - line >= spacing) target.push_back(line);
    }
    target.push_back(b); target.push_back(old.back()); return target;
}
std::size_t integer_overlaps(std::vector<std::int64_t> a, std::vector<std::int64_t> b, bool corners) {
    if (corners) {
        const auto split = [](const auto& nodes) {
            std::vector<std::int64_t> out;
            for (std::size_t i = 0; i + 1 < nodes.size(); ++i) { out.push_back(2 * nodes[i]); out.push_back(nodes[i] + nodes[i + 1]); }
            if (!nodes.empty()) out.push_back(2 * nodes.back());
            return out;
        };
        a = split(a); b = split(b);
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i + 1 < a.size(); ++i) for (std::size_t j = 0; j + 1 < b.size(); ++j)
        if (std::min(a[i + 1], b[j + 1]) > std::max(a[i], b[j])) ++n;
    return n;
}
void compare_coverage(std::span<const HybridCoverageRecord> actual, std::span<const HybridCoverageBalance> balances,
                      const std::vector<IntegerCoverage>& expected, const std::vector<std::int64_t>& nodes,
                      double unit, double area, HybridCoveragePartition partition) {
    check(actual.size() == expected.size(), "Independent integer coverage record count differs");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& e = expected[i]; const auto& a = actual[i];
        check(a.partition == partition && a.cell_slot == e.slot && a.cartesian_index == e.bin, "Coverage identity/scope differs");
        near(a.begin_relative_m, static_cast<double>(e.a) * unit, 0); near(a.end_relative_m, static_cast<double>(e.b) * unit, 0);
        near(a.width_m, static_cast<double>(e.b - e.a) * unit, 0); near(a.volume_m3, area * a.width_m, 0);
        check(a.width_m > 0 && a.volume_m3 > 0, "Coverage published an empty material record");
    }
    check(balances.size() + (nodes.empty() ? 0 : 1) == nodes.size(), "Missing per-interval coverage balances");
    for (std::size_t i = 0; i < balances.size(); ++i) {
        const auto& a = balances[i]; check(a.cell_slot == i && a.partition == partition, "Wrong coverage balance scope");
        near(a.expected_width_m, static_cast<double>(nodes[i + 1] - nodes[i]) * unit, 0);
        near(a.covered_width_m, a.expected_width_m, 0); near(a.covered_volume_m3, a.expected_volume_m3, 0);
        check(a.width_residual_m == 0 && a.volume_residual_m3 == 0, "Exact dyadic union has a residual");
    }
}
void integer_case(std::vector<std::int64_t> requested_nodes, std::int64_t origin, std::int64_t spacing,
                  std::int64_t first, std::int64_t end, bool wall_right = false, double frame = 0) {
    Setup setup; constexpr double unit = .125; std::vector<double> positions;
    for (auto x : requested_nodes) positions.push_back(static_cast<double>(x) * unit);
    const auto canonical = setup.make(positions, 2, frame, wall_right); const auto before = fingerprint(canonical);
    const auto hybrid = bind_material_endcaps(canonical);
    std::vector<std::int64_t> old;
    for (const auto& node : canonical.nodes()) old.push_back(static_cast<std::int64_t>(std::llround(node.relative_position_m / unit)));
    const auto target = integer_target(old, origin, spacing, first, end);
    const auto plan = plan_hybrid_staggered_rezone(hybrid, {static_cast<double>(origin) * unit, 0,
        static_cast<double>(spacing) * unit, first, end});
    validate_hybrid_rezone_plan(hybrid, plan);
    check(plan.node_relative_positions_m().size() == target.size(), "Integer target node count differs");
    for (std::size_t j = 0; j < target.size(); ++j) near(plan.node_relative_positions_m()[j], static_cast<double>(target[j]) * unit, 0);
    compare_coverage(plan.source_coverage(), plan.source_coverage_balances(), integer_coverage(old, origin, spacing, first, end),
        old, unit, 2, HybridCoveragePartition::authoritative_source);
    compare_coverage(plan.planned_target_coverage(), plan.planned_target_coverage_balances(), integer_coverage(target, origin, spacing, first, end),
        target, unit, 2, HybridCoveragePartition::planned_target);
    check(plan.diagnostics().primal_overlap_records == integer_overlaps(old, target, false), "Independent primal-overlap count differs");
    check(plan.diagnostics().corner_overlap_records == integer_overlaps(old, target, true), "Independent half-interval count differs");
    check(fingerprint(hybrid.canonical()) == before, "Geometry plan changed canonical inventory/geometry/ledgers/cache");
    for (std::size_t i = 1; i + 1 < hybrid.source_cell_identities().size(); ++i)
        check(hybrid.source_cell_identities()[i].kind == HybridCellKind::source_material, "Unrezoned source claims a target grid role");
}
void independent_dyadic_partition_overlap_coverage() {
    integer_case({0, 3, 11, 15, 27, 32}, -1, 4, -3, 12);
    integer_case({0, 5, 17, 21, 29, 32}, 1, 4, -4, 12);
    integer_case({0, 3, 11, 15, 27, 32}, -1, 4, -16, 4, true, -1e100);
    integer_case({0, 3, 11, 15, 27, 32}, -1, 4, -3, 12, false, 1e100);
    // Every dyadic grid phase, reflected mass-independent source partition.
    for (std::int64_t phase = 0; phase < 8; ++phase) {
        integer_case({0, 2, 9, 17, 23, 30, 32}, phase - 4, 8, -2, 6);
        integer_case({0, 2, 9, 15, 23, 30, 32}, phase - 4, 8, -2, 6);
    }
}
void every_no_line_positive_gap_has_actual_width() {
    Setup setup;
    for (const auto& nodes : std::vector<std::vector<double>>{{0,.25,.5,.75,1}, {0,.25,1,1.75,2},
             {0,.25,1,2.25,2.5}, {0,.25,.5,1.5,2.75,3}, {0,.0625,1,2,2.9375,3}}) {
        const auto source = bind_material_endcaps(setup.make(nodes)); const auto before = fingerprint(source.canonical());
        const auto plan = plan_hybrid_staggered_rezone(source, {0,0,1,0,4}); const auto& d = plan.diagnostics();
        const double gap = nodes[nodes.size() - 2] - nodes[1];
        check(d.has_interior_gap && d.no_eligible_grid_line && d.eligible_grid_lines == 0, "No-line gap was misclassified");
        check(plan.planned_cells().size() == 3 && plan.planned_cells()[1].identity.kind == HybridCellKind::whole_interior_gap,
            "No-line gap was clipped or assigned a Cartesian cell");
        near(d.interior_gap_width_m, gap, 0); near(d.whole_gap_width_m, gap, 0);
        near(plan.planned_cells()[1].width_m, gap, 0); check(fingerprint(source.canonical()) == before, "No-line plan repartitioned actual inventory");
    }
    const auto source = bind_material_endcaps(setup.make({0,.25,1,2.25,2.5}));
    const auto one = plan_hybrid_staggered_rezone(source, {.25,0,1,-1,4});
    check(one.diagnostics().eligible_grid_lines == 1 && !one.diagnostics().no_eligible_grid_line, "Exact single eligible line was lost");
    near(one.diagnostics().left_transition_width_m, 1, 0); near(one.diagnostics().right_transition_width_m, 1, 0);
}
void vacuum_whole_slab_and_adjacent_caps() {
    Setup setup; StaggeredInitialization empty; empty.thermodynamics = setup.model;
    const auto vacuum = bind_material_endcaps(initialize_occupied_staggered(empty), {0,0,0});
    const auto v = plan_hybrid_staggered_rezone(vacuum, {0,0,1,-3,7});
    check(v.node_relative_positions_m().empty() && v.planned_cells().empty() && v.protected_caps().empty() &&
        v.source_coverage().empty() && v.planned_target_coverage().empty() && !v.diagnostics().has_interior_gap,
        "Vacuum acquired cells, cap mass or coverage");
    const auto whole = bind_material_endcaps(setup.make({0,1})); const auto p = plan_hybrid_staggered_rezone(whole, {-.125,0,.25,-1,5});
    check(whole.material_caps().size() == 1 && whole.source_cell_identities()[0].kind == HybridCellKind::whole_material_slab,
        "One slab was duplicated into two caps");
    check(p.planned_cells().size() == 1 && p.protected_caps().size() == 1 && !p.diagnostics().has_interior_gap,
        "Whole material slab acquired an interior partition");
    check(p.diagnostics().primal_overlap_records == 1 && p.diagnostics().corner_overlap_records == 2, "Whole-slab identity counts differ");
    const auto adjacent = bind_material_endcaps(setup.make({0,.375,1}));
    const auto a = plan_hybrid_staggered_rezone(adjacent, {0,0,.125,0,8});
    check(a.planned_cells().size() == 2 && a.protected_caps().size() == 2 && !a.diagnostics().has_interior_gap &&
        !a.diagnostics().no_eligible_grid_line, "Adjacent material caps created a zero-width transition");
    near(a.planned_cells()[0].end_relative_m, a.planned_cells()[1].begin_relative_m, 0);
    check(a.planned_cells()[0].identity.material != a.planned_cells()[1].identity.material, "Adjacent caps share a material identity");
    check(a.diagnostics().primal_overlap_records == 2 && a.diagnostics().corner_overlap_records == 4, "Adjacent cap identity counts differ");
    integer_case({0,3,8},0,1,0,8); integer_case({0,8},-1,2,-1,5);
}
void exact_copy_logical_identity_lifetime_and_immutability() {
    Setup setup; auto original = setup.make({0,1,2,4},1,1e100); const auto before = fingerprint(original);
    const auto source = bind_material_endcaps(original), copy = source;
    check(source.identity().lineage != 0 && source.identity().revision == 0 && copy.identity() == source.identity(), "Copy lost logical snapshot identity");
    check(&source != &copy && &source.canonical() != &copy.canonical(), "Test needs separate value objects");
    check(source.canonical().model().get() == original.model().get() && fingerprint(source.canonical()) == before,
        "Bind changed canonical/model state or borrowed mutable arrays");
    const auto plan = plan_hybrid_staggered_rezone(source, {0,0,.5,0,8});
    validate_hybrid_rezone_plan(copy, plan); const auto plan_copy = plan; validate_hybrid_rezone_plan(source, plan_copy);
    const auto foreign = bind_material_endcaps(original); check(foreign.identity() != source.identity(), "Independent bind reused a logical source");
    fatal(FluidFatalCode::unsupported_configuration, [&] { validate_hybrid_rezone_plan(foreign, plan); });
    (void)advance_occupied_staggered(original, 1e-6); // existing material helper only; the wrapper exposes no pressure adapter
    check(original.time_seconds() > 0 && fingerprint(source.canonical()) == before, "Original owner mutation leaked into immutable bound snapshot");
    const auto evolved = bind_material_endcaps(original);
    check(fingerprint(evolved.canonical()) == fingerprint(original), "Bind lost nonzero time/cumulative/thermodynamic history");
    fatal(FluidFatalCode::unsupported_configuration, [&] { validate_hybrid_rezone_plan(evolved, plan); });
    const auto survivor = [&] { Setup temporary; const auto local = temporary.make({0,.5,2}); return bind_material_endcaps(local); }();
    validate_hybrid_rezone_plan(survivor, plan_hybrid_staggered_rezone(survivor, {0,0,.25,0,8}));
    check(survivor.canonical().model()->registry_size() == 2, "Wrapper retained dangling source/model data");
}
void exact_capacity_boundaries_and_early_large_domain_rejection() {
    Setup setup; const auto original = setup.make({0,1,2,3,4}); const auto before = fingerprint(original);
    const auto source = bind_material_endcaps(original, {6,18,16});
    const auto p = plan_hybrid_staggered_rezone(source, {0,0,.5,0,8});
    check(p.planned_cells().size() == 6 && p.diagnostics().primal_overlap_records == 6 &&
        p.diagnostics().corner_overlap_records == 12 && p.source_coverage().size() == 8 && p.planned_target_coverage().size() == 8,
        "Hand-counted exact capacity case differs");
    fatal(FluidFatalCode::unsupported_configuration, [&] { (void)bind_material_endcaps(original, {3,18,16}); });
    for (const auto limits : {HybridGeometryLimits{5,18,16}, HybridGeometryLimits{6,17,16}, HybridGeometryLimits{6,18,15}}) {
        const auto limited = bind_material_endcaps(original, limits);
        fatal(FluidFatalCode::unsupported_configuration, [&] { (void)plan_hybrid_staggered_rezone(limited, {0,0,.5,0,8}); });
        check(fingerprint(limited.canonical()) == before, "Capacity failure changed an authoritative source");
    }
    const auto slab = bind_material_endcaps(setup.make({0,1}), {1,3,2});
    fatal(FluidFatalCode::unsupported_configuration, [&] {
        (void)plan_hybrid_staggered_rezone(slab, {0,0,std::ldexp(1.0,-50),0,INT64_C(1)<<50});
    });
    // The caller permits huge target-cell capacity, but overlap capacity
    // must still reject before a multi-trillion-interval validation walk.
    const auto many = bind_material_endcaps(setup.make({0,1,3,4}), {std::numeric_limits<std::size_t>::max(),18,16});
    fatal(FluidFatalCode::unsupported_configuration, [&] {
        (void)plan_hybrid_staggered_rezone(many, {0,0,std::ldexp(1.0,-50),0,INT64_C(1)<<52});
    });
}
void local_scale_grid_representation_and_positive_tiny_cap() {
    Setup setup;
    for (int exponent : {-300,300}) {
        const double unit = std::ldexp(1.0,exponent); const auto source = bind_material_endcaps(setup.make({0,unit,2*unit,4*unit},1,1e100));
        const auto p = plan_hybrid_staggered_rezone(source, {0,0,.5*unit,0,8});
        near(p.node_relative_positions_m().front(),0,0); near(p.node_relative_positions_m().back(),4*unit,0);
        for (const auto& r : p.source_coverage()) check(r.width_m > 0 && r.volume_m3 > 0, "Scaled source coverage disappeared");
    }
    const double tiny = std::ldexp(1.0,-100);
    const auto source = bind_material_endcaps(setup.make({0,tiny,1,2}));
    const auto p = plan_hybrid_staggered_rezone(source,{0,0,.25,0,8});
    near(p.planned_cells()[0].width_m,tiny,0); near(p.source_coverage()[0].width_m,tiny,0);
    check(source.material_caps()[0].original_species_masses_kg[1] > 0 &&
        source.material_caps()[0].original_species_masses_kg == source.canonical().cells()[0].mixture.masses,
        "Tiny protected material or trace was replaced");
    const auto ordinary = bind_material_endcaps(setup.make({0,.25,.5,.75,1}));
    const auto compensated = plan_hybrid_staggered_rezone(ordinary,{1e100,-1e100,.125,0,8});
    check(compensated.node_relative_positions_m().size() > 0, "Compensated grid cancellation failed");
    const auto rounded = plan_hybrid_staggered_rezone(ordinary,{.1,0,.125,-1,8});
    check(rounded.diagnostics().maximum_grid_line_roundoff_m > 0, "Represented-grid rounding was hidden");
    fatal(FluidFatalCode::numerical_precision, [&] { (void)plan_hybrid_staggered_rezone(ordinary,{1e100,0,.125,-1,8}); });
}
void exact_requested_grid_certificate_survives_cancellation() {
    Setup setup; StaggeredInitialization empty; empty.thermodynamics = setup.model;
    const auto source = bind_material_endcaps(initialize_occupied_staggered(empty), {0,0,0});
    const auto before = fingerprint(source.canonical());
    const auto accepted = plan_hybrid_staggered_rezone(source, {1e100,-1e100,.125,0,8});
    constexpr std::int64_t magnitude = INT64_C(3) << 47;
    const double base_spacing = std::nextafter(1.0,2.0);
    for (int exponent : {-1022,-600,0,600}) for (std::int64_t direction : {-1,1}) {
        const std::int64_t k = direction*magnitude;
        // Integer rational oracle, independent of product/FMA/expansion:
        // k*nextafter(1,2) = k + direction*3/32. The closest binary64
        // line is k+direction*4/32, whose signed error is direction/32.
        const std::int64_t numerator = 32*k + direction*3;
        const double nearest = static_cast<double>(k) + static_cast<double>(direction)/8;
        const auto represented_numerator = static_cast<std::int64_t>(32*nearest);
        const double exact_signed_error = static_cast<double>(represented_numerator-numerator)/32;
        near(exact_signed_error,static_cast<double>(direction)/32,0);
        const double spacing = std::ldexp(base_spacing,exponent);
        check(std::ldexp(std::abs(exact_signed_error),exponent) > 32*std::numeric_limits<double>::epsilon()*spacing,
            "Independent bad-line oracle must exceed the unchanged local grid budget");
        // The two rounded adjacent lines are one scaled unit apart, so the
        // old spacing-only certificate cannot expose the erroneous line.
        const double adjacent = nearest + 1;
        check(std::abs((adjacent-nearest)-base_spacing) <= 32*std::numeric_limits<double>::epsilon()*base_spacing,
            "Counterexample must retain an individually acceptable rounded bin spacing");
        const double origin = std::ldexp(1e100,exponent);
        fatal(FluidFatalCode::numerical_precision,[&] {
            (void)plan_hybrid_staggered_rezone(source,{origin,-origin,spacing,k,k+1});
        });
        // Huge cancellation is itself supported when all actual line terms
        // are represented exactly. Rejection cannot be a magnitude cutoff.
        const auto exact = plan_hybrid_staggered_rezone(source,{origin,-origin,std::ldexp(.125,exponent),0,8});
        near(exact.diagnostics().maximum_grid_line_roundoff_m,0,0);
    }
    validate_hybrid_rezone_plan(source,accepted);
    check(fingerprint(source.canonical()) == before,"Failed grid certification changed immutable vacuum source/model/ledger");
}
void subnormal_volume_certificate_without_unit_floor() {
    Setup liquid(true); const double d = std::numeric_limits<double>::denorm_min();
    const auto exact = bind_material_endcaps(initialize_occupied_staggered(liquid.input({0,2*d},1,0,false,true)));
    const auto accepted = plan_hybrid_staggered_rezone(exact,{0,0,2*d,0,1});
    check(accepted.source_coverage().size() == 1 && accepted.source_coverage()[0].volume_m3 == 2*d,
        "Exactly representable positive subnormal volume was rejected or dropped");
    // The original material helper stores fl(1.25*(2*d))=2*d. A0 binding
    // copies it exactly, but grid-volume arithmetic must reject this 20%
    // relative product error rather than accept an underflowed eps*scale.
    const auto rounded = bind_material_endcaps(initialize_occupied_staggered(liquid.input({0,2*d},1.25,0,false,true)));
    const auto before = fingerprint(rounded.canonical());
    fatal(FluidFatalCode::numerical_precision, [&] { (void)plan_hybrid_staggered_rezone(rounded,{0,0,2*d,0,1}); });
    check(fingerprint(rounded.canonical()) == before, "Subnormal precision rejection changed canonical state");
}
void invalid_domain_grid_and_failures_after_success() {
    Setup setup; fatal(FluidFatalCode::unsupported_configuration,[&] { (void)bind_material_endcaps(OccupiedStaggeredState{}); });
    const auto source = bind_material_endcaps(setup.make({0,.25,.75,1})); const auto before = fingerprint(source.canonical());
    const auto valid = plan_hybrid_staggered_rezone(source,{0,0,.125,0,8});
    for (const auto& grid : std::vector<StaggeredCartesianGrid>{{0,0,0,0,8},{0,0,-1,0,8},
            {0,0,std::numeric_limits<double>::quiet_NaN(),0,8},{0,0,std::numeric_limits<double>::infinity(),0,8},
            {std::numeric_limits<double>::quiet_NaN(),0,1,0,8},{0,0,1,4,4},{0,0,1,0,(INT64_C(1)<<52)+1}})
        fatal(FluidFatalCode::unsupported_configuration,[&] { (void)plan_hybrid_staggered_rezone(source,grid); });
    fatal(FluidFatalCode::unsupported_topology,[&] { (void)plan_hybrid_staggered_rezone(source,{0,0,.125,1,8}); });
    fatal(FluidFatalCode::unsupported_topology,[&] { (void)plan_hybrid_staggered_rezone(source,{0,0,.125,0,7}); });
    validate_hybrid_rezone_plan(source,valid);
    check(fingerprint(source.canonical()) == before, "Failure after accepted plan changed immutable source/cache/ledger");
    auto moved_source = source; const auto moved_to = std::move(moved_source);
    validate_hybrid_rezone_plan(moved_to,valid);
    fatal(FluidFatalCode::unsupported_configuration,[&] { (void)plan_hybrid_staggered_rezone(moved_source,{0,0,.125,0,8}); });
    auto moved_plan = valid; const auto destination = std::move(moved_plan); validate_hybrid_rezone_plan(source,destination);
    fatal(FluidFatalCode::unsupported_configuration,[&] { validate_hybrid_rezone_plan(source,moved_plan); });
}
template<class T> concept HasInventory = requires(T x) { x.mixture; };
static_assert(!HasInventory<HybridPlannedCell> && !HasInventory<HybridCoverageRecord>);
static_assert(std::is_same_v<decltype(std::declval<HybridStaggeredState&>().canonical()), const OccupiedStaggeredState&>);
static_assert(std::is_same_v<decltype(std::declval<HybridRezonePlan&>().node_relative_positions_m()), std::span<const double>>);
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"independent integer partition, overlap and coverage oracles",independent_dyadic_partition_overlap_coverage},
        {"every positive no-line gap retains its actual width",every_no_line_positive_gap_has_actual_width},
        {"exact vacuum, whole slab and adjacent material caps",vacuum_whole_slab_and_adjacent_caps},
        {"exact canonical copy, logical authentication and lifetime",exact_copy_logical_identity_lifetime_and_immutability},
        {"exact capacity boundaries and enormous-grid early rejection",exact_capacity_boundaries_and_early_large_domain_rejection},
        {"local scales, represented grid and genuine tiny cap",local_scale_grid_representation_and_positive_tiny_cap},
        {"exact requested-line certificate under reflected/scaled cancellation",exact_requested_grid_certificate_survives_cancellation},
        {"positive subnormal volume and relative precision rejection",subnormal_volume_certificate_without_unit_floor},
        {"typed invalid geometry and failure after accepted plan",invalid_domain_grid_and_failures_after_success}
    };
    std::size_t passed=0;
    for (const auto& [name,test] : tests) try { test();++passed;std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    std::cout<<passed<<'/'<<tests.size()<<" hybrid A0 geometry groups passed; no mechanical rezone, pressure adapter or front gate claimed\n";
    return passed==tests.size()?0:1;
}
