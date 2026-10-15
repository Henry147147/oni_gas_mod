#include "mixed_atmospheres/occupied_staggered_hybrid.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double eps = std::numeric_limits<double>::epsilon();
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }
void within(double actual, double expected, double bound, const char* message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || !std::isfinite(bound) || bound < 0 || std::abs(actual-expected) > bound) {
        std::ostringstream out; out << message << std::setprecision(17) << ": actual=" << actual << " expected=" << expected << " bound=" << bound;
        throw std::runtime_error(out.str());
    }
}
template<class F> void fatal(FluidFatalCode code, F&& function) {
    bool caught = false; try { function(); } catch (const FluidFatalError& e) { caught = e.code() == code; }
    check(caught,"Expected typed first-event rejection");
}

// Independent bounded integer rational oracle. This is test-only arithmetic;
// it does not call the remapper's packet/normalization/heat helpers, and does
// not depend on long double being wider than double on Windows.
struct U128 {
    std::array<std::uint32_t,4> a{};
    explicit U128(std::uint64_t x = 0) : a{static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(x>>32),0,0} {}
    [[nodiscard]] int bits() const {
        for (std::size_t i = 4; i != 0; --i) if (a[i-1] != 0) return static_cast<int>((i-1)*32)+static_cast<int>(std::bit_width(a[i-1]));
        return 0;
    }
    [[nodiscard]] int compare(const U128& b) const {
        for (std::size_t i = 4; i != 0; --i) if (a[i-1] != b.a[i-1]) return a[i-1] > b.a[i-1] ? 1 : -1;
        return 0;
    }
    void shift(unsigned n = 1) {
        for (unsigned k = 0; k < n; ++k) {
            std::uint32_t carry = 0;
            for (auto& x : a) { const auto next = x>>31; x = (x<<1)|carry; carry = next; }
            check(carry == 0,"Independent rational oracle shift overflow");
        }
    }
    void subtract(const U128& b) {
        check(compare(b) >= 0,"Independent rational oracle negative remainder"); std::uint64_t borrow = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const std::uint64_t sub = static_cast<std::uint64_t>(b.a[i])+borrow, old = a[i];
            a[i] = static_cast<std::uint32_t>(old-sub); borrow = old < sub ? 1 : 0;
        }
        check(borrow == 0,"Independent rational oracle subtraction underflow");
    }
    void add(const U128& b) {
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < 4; ++i) { const std::uint64_t v = static_cast<std::uint64_t>(a[i])+b.a[i]+carry; a[i] = static_cast<std::uint32_t>(v); carry = v>>32; }
        check(carry == 0,"Independent rational oracle addition overflow");
    }
    void times(std::uint32_t b) {
        std::uint64_t carry = 0;
        for (auto& x : a) { const std::uint64_t v = static_cast<std::uint64_t>(x)*b+carry; x = static_cast<std::uint32_t>(v); carry = v>>32; }
        check(carry == 0,"Independent rational oracle product overflow");
    }
};
U128 multiply(std::uint64_t x, std::uint64_t y) {
    U128 result; const U128 left(x), right(y);
    for (std::size_t i = 0; i < 2; ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < 2; ++j) {
            const std::uint64_t v = static_cast<std::uint64_t>(left.a[i])*right.a[j]+result.a[i+j]+carry;
            result.a[i+j] = static_cast<std::uint32_t>(v); carry = v>>32;
        }
        result.a[i+2] = static_cast<std::uint32_t>(carry);
    }
    return result;
}
double rational(U128 numerator, std::uint64_t denominator, int power = 0, bool negative = false) {
    check(denominator != 0,"Independent rational oracle zero denominator"); if (numerator.bits() == 0) return 0;
    U128 divisor(denominator); int exponent = numerator.bits()-divisor.bits();
    if (exponent >= 0) divisor.shift(static_cast<unsigned>(exponent)); else numerator.shift(static_cast<unsigned>(-exponent));
    if (numerator.compare(divisor) < 0) { numerator.shift(); --exponent; }
    check(numerator.compare(divisor) >= 0,"Independent rational oracle normalization failure"); numerator.subtract(divisor);
    std::uint64_t significand = UINT64_C(1)<<52;
    for (int bit = 51; bit >= 0; --bit) {
        numerator.shift(); if (numerator.compare(divisor) >= 0) { numerator.subtract(divisor); significand |= UINT64_C(1)<<bit; }
    }
    numerator.shift(); const int rounding = numerator.compare(divisor);
    if (rounding > 0 || (rounding == 0 && (significand&1) != 0)) ++significand;
    if (significand == (UINT64_C(1)<<53)) { significand >>= 1; ++exponent; }
    double result = std::ldexp(static_cast<double>(significand),exponent+power-52);
    check(std::isfinite(result) && result > 0,"Independent rational oracle result outside selected normal range");
    return negative ? -result : result;
}
std::uint64_t magnitude(std::int64_t n) { check(n != std::numeric_limits<std::int64_t>::min(),"Independent signed oracle overflow"); return static_cast<std::uint64_t>(n < 0 ? -n : n); }
std::int64_t fixed52(double x) {
    const double scaled = std::ldexp(x,52); check(std::isfinite(scaled) && std::abs(scaled) < std::ldexp(1.0,63),"Velocity outside fixed oracle range");
    const auto n = static_cast<std::int64_t>(scaled); check(static_cast<double>(n) == scaled,"Velocity is not exact on selected binary oracle lattice"); return n;
}
struct EventOracle { double inertia, momentum, velocity, impulse, heat, cap_heat, transported_h, kinetic; };
EventOracle inner_oracle(unsigned n, unsigned d, double v, double w) {
    const std::int64_t vi = fixed52(v), wi = fixed52(w);
    const std::int64_t j = static_cast<std::int64_t>(2*d)*vi+static_cast<std::int64_t>(d-1)*wi;
    const unsigned a = 3*d-1, b = d-1; const auto delta = magnitude(wi-vi);
    auto q = multiply(delta,delta); q.times(b); auto cap = q; cap.times(d);
    auto h = multiply(magnitude(vi),magnitude(vi)); h.times(2*d);
    auto h2 = multiply(magnitude(wi),magnitude(wi)); h2.times(b); h.add(h2);
    return {rational(U128(a),2ULL*d*n),rational(U128(magnitude(j)),2ULL*d*n,-52,j < 0),
        rational(U128(magnitude(j)),a,-52,j < 0),
        rational(U128(static_cast<std::uint64_t>(b)*magnitude(wi)),2ULL*d*n,-52,wi < 0),
        rational(q,2ULL*a*n,-104),rational(cap,2ULL*a*a*n,-104),
        rational(h,4ULL*d*n,-104),rational(multiply(magnitude(j),magnitude(j)),4ULL*d*n*a,-104)};
}
struct Binary { std::uint64_t mantissa; int exponent; };
Binary positive_binary(double x) {
    const auto bits = std::bit_cast<std::uint64_t>(std::abs(x)); const int exponent = static_cast<int>((bits>>52)&2047);
    check(exponent != 0 && exponent != 2047,"Actual-energy oracle requires the selected finite normal operands");
    return {(bits&((UINT64_C(1)<<52)-1))|(UINT64_C(1)<<52),exponent-1023-52};
}
double exact_node_k(const StaggeredNode& node) {
    if (node.momentum_kg_m_per_s == 0) return 0;
    const auto j = positive_binary(node.momentum_kg_m_per_s), i = positive_binary(node.inertia_kg);
    return rational(multiply(j.mantissa,j.mantissa),2*i.mantissa,2*j.exponent-i.exponent);
}
// A growing TwoSum expansion independently sums the actual stored scalars,
// including signed before/after differences without subtracting bulk totals.
struct ExactSum {
    std::vector<double> terms;
    void add(double x) {
        std::size_t count = 0;
        for (double y : terms) {
            const double sum = x+y, z = sum-x, remainder = (x-(sum-z))+(y-z);
            check(std::isfinite(sum) && std::isfinite(remainder),"Independent actual-sum range failure");
            if (remainder != 0) terms[count++] = remainder;
            x = sum;
        }
        terms.resize(count); if (x != 0 || terms.empty()) terms.push_back(x);
    }
    [[nodiscard]] double value() const { double sum = 0; for (double x : terms) sum += x; return sum; }
};
struct Actual {
    std::array<double,2> masses{}; double momentum = 0, energy = 0, kinetic = 0, thermal = 0, entropy = 0, entropy_scale = 0;
};
Actual actual(const OccupiedStaggeredState& state) {
    std::array<ExactSum,2> masses; ExactSum p,e,k,thermal,s,ss;
    for (const auto& c : state.cells()) {
        for (std::size_t i = 0; i < 2; ++i) masses[i].add(c.mixture.masses[i]);
        e.add(c.mixture.energy); thermal.add(c.thermodynamics.thermal_energy_j); s.add(c.thermodynamics.entropy_j_per_k);
        ss.add(c.thermodynamics.intrinsic_heat_capacity_j_per_k); ss.add(std::abs(c.thermodynamics.entropy_j_per_k));
    }
    for (const auto& node : state.nodes()) { p.add(node.momentum_kg_m_per_s); const double h = exact_node_k(node); k.add(h); e.add(h); }
    return {{masses[0].value(),masses[1].value()},p.value(),e.value(),k.value(),thermal.value(),s.value(),ss.value()};
}
double energy_difference(const OccupiedStaggeredState& before, const OccupiedStaggeredState& after) {
    ExactSum sum;
    for (const auto& c : after.cells()) sum.add(c.mixture.energy);
    for (const auto& c : before.cells()) sum.add(-c.mixture.energy);
    for (const auto& node : after.nodes()) sum.add(exact_node_k(node));
    for (const auto& node : before.nodes()) sum.add(-exact_node_k(node));
    return sum.value();
}
std::string fingerprint(const HybridStaggeredState& hybrid) {
    const auto& state = hybrid.canonical(); std::ostringstream out; out << std::hexfloat;
    out << hybrid.identity().lineage << ':' << hybrid.identity().revision << ':' << hybrid.identity().snapshot << ':' << state.model().get() << ':' << state.model_metadata();
    for (double x : {state.origin_high_m(),state.origin_low_m(),state.time_seconds(),state.area_m2(),state.gravity_m_per_s2(),state.cumulative_energy_balance_error_j(),state.cumulative_geometry_volume_residual_m3()}) out << ';' << x;
    const auto& l = state.initialization_ledger();
    for (double x : {l.prior_bulk_kinetic_energy_j,l.retained_kinetic_energy_j,l.additional_mechanical_energy_j,l.requested_internal_energy_change_j,l.represented_internal_energy_change_j,l.declared_added_energy_j,l.represented_total_energy_change_j,l.kinetic_partition_roundoff_j,l.momentum_reconciliation_kg_m_per_s,l.maximum_momentum_arithmetic_bound_kg_m_per_s}) out << ';' << x;
    out << ';' << l.unresolved_energy_conversions;
    for (const auto& node : state.nodes()) out << ';' << node.relative_position_m << ';' << node.inertia_kg << ';' << node.momentum_kg_m_per_s << ';' << node.fixed;
    for (const auto& c : state.cells()) {
        for (double m : c.mixture.masses) out << ';' << m;
        out << ';' << c.mixture.energy << ';' << c.occupied_volume_m3 << ';' << c.bulk_momentum_kg_m_per_s << ';' << c.mechanical_variance_j;
        const auto& e = c.thermodynamics;
        for (double x : {e.temperature_k,e.mechanical_pressure_pa,e.gas_pressure_pa,e.gas_volume_m3,e.liquid_volume_m3,e.vacuum_volume_m3,e.reference_energy_j,e.thermal_energy_j,e.elastic_energy_j,e.entropy_j_per_k,e.helmholtz_energy_j,e.intrinsic_heat_capacity_j_per_k,e.equilibrium_heat_capacity_at_cell_volume_j_per_k,e.volume_residual_m3,e.energy_residual_j}) out << ';' << x;
        for (double x : e.partial_volumes_m3) out << ';' << x;
        for (double x : e.specific_internal_energies_j_per_kg) out << ';' << x;
        out << ';' << e.has_gas << ';' << e.has_vacuum << ';' << e.pressure_iterations;
    }
    const auto& limits = hybrid.limits(); out << ';' << limits.maximum_material_cells << ';' << limits.maximum_overlap_records << ';' << limits.maximum_coverage_records << ';' << hybrid.has_coverage_grid();
    const auto& grid = hybrid.coverage_grid(); out << ';' << grid.origin_relative_high_m << ';' << grid.origin_relative_low_m << ';' << grid.spacing_m << ';' << grid.first_cell << ';' << grid.end_cell;
    for (const auto& c : hybrid.source_cell_identities()) out << ';' << static_cast<int>(c.kind) << ';' << c.material.lineage << ';' << c.material.ordinal << ';' << c.cartesian_index;
    for (const auto& cap : hybrid.material_caps()) { out << ';' << static_cast<int>(cap.identity.kind) << ';' << cap.identity.material.lineage << ';' << cap.identity.material.ordinal << ';' << cap.initial_source_cell; for (double m : cap.original_species_masses_kg) out << ';' << m; }
    for (const auto& c : hybrid.authoritative_coverage()) out << ';' << static_cast<int>(c.partition) << ';' << c.cell_slot << ';' << c.cartesian_index << ';' << c.begin_relative_m << ';' << c.end_relative_m << ';' << c.width_m << ';' << c.volume_m3;
    for (const auto& c : hybrid.authoritative_coverage_balances()) out << ';' << static_cast<int>(c.partition) << ';' << c.cell_slot << ';' << c.records << ';' << c.expected_width_m << ';' << c.covered_width_m << ';' << c.width_residual_m << ';' << c.width_arithmetic_bound_m << ';' << c.expected_volume_m3 << ';' << c.covered_volume_m3 << ';' << c.volume_residual_m3 << ';' << c.volume_arithmetic_bound_m3;
    return out.str();
}
std::string ledger(const OccupiedStaggeredState& state) {
    const auto& l = state.initialization_ledger(); std::ostringstream out; out << std::hexfloat;
    for (double x : {l.prior_bulk_kinetic_energy_j,l.retained_kinetic_energy_j,l.additional_mechanical_energy_j,l.requested_internal_energy_change_j,l.represented_internal_energy_change_j,l.declared_added_energy_j,l.represented_total_energy_change_j,l.kinetic_partition_roundoff_j,l.momentum_reconciliation_kg_m_per_s,l.maximum_momentum_arithmetic_bound_kg_m_per_s}) out << x << ';';
    out << l.unresolved_energy_conversions; return out.str();
}
struct Setup {
    SpeciesRegistry registry; std::shared_ptr<const FluidThermodynamics> model;
    Setup() {
        std::vector<ReferenceMaterial> profiles;
        for (const char* id : {"carrier","tag"}) {
            SpeciesDefinition species; species.id = id; species.name = id; species.phase = Phase::gas;
            species.atoms = {{"N",2}}; species.cp = 1040; species.formation_enthalpy = species.cp*reference_temperature*molecular_mass(species.atoms); registry.add(species);
            const auto& s = registry[registry.size()-1]; ReferenceMaterial p; p.id = s.id; p.phase = ReferencePhase::ideal_gas;
            p.molar_mass_kg_per_mol = s.molar_mass; p.cv_j_per_kg_k = s.cv(); p.energy_offset_j_per_kg = s.internal_energy(0);
            p.minimum_temperature_k = 1e-20; p.maximum_temperature_k = 1e9; p.provenance = "Thermally identical passive event labels; explicit test surrogate"; profiles.push_back(p);
        }
        model = std::make_shared<const FluidThermodynamics>(registry,"hybrid-first-event-test-v1",profiles);
    }
    HybridStaggeredState initialize(unsigned n, bool phase_half, bool reflected, HybridGeometryLimits limits = {}) const {
        const double h = 1.0/n; std::vector<double> positions{0,h};
        if (phase_half) { for (unsigned k = 2; k <= n-3; ++k) positions.push_back((static_cast<double>(k)+.5)*h); }
        else for (unsigned k = 2; k <= n-2; ++k) positions.push_back(static_cast<double>(k)*h);
        positions.push_back(1-h); positions.push_back(1);
        StaggeredInitialization input; input.thermodynamics = model; input.positions_m = positions;
        for (double q : positions) input.velocities_m_per_s.push_back(reflected ? -(1+.1*(1-q)) : 1+.1*q);
        for (std::size_t i = 0; i+1 < positions.size(); ++i) {
            const double width = positions[i+1]-positions[i]; auto mix = model->initialize_at_temperature(std::vector{.75*width,.25*width},300,width).mixture;
            input.cells.push_back({mix,width*std::midpoint(input.velocities_m_per_s[i],input.velocities_m_per_s[i+1])});
        }
        // Exactly one real initializer and one bind; all subsequent states
        // below are actual preferred-planner mechanical-rezone publications.
        return bind_material_endcaps(initialize_occupied_staggered(input),limits);
    }
};
StaggeredCartesianGrid grid(unsigned n, unsigned d, unsigned step, bool phase_half, bool reflected) {
    const double h = 1.0/n, tau = static_cast<double>(step)*h/d;
    const double phi = (phase_half ? .5*h : 0)-tau;
    return {reflected ? -phi : phi,0,h,-static_cast<std::int64_t>(n),2*static_cast<std::int64_t>(n)};
}
std::vector<double> independent_nodes(unsigned n, unsigned d, unsigned step, bool phase_half, bool reflected) {
    // Exact integer geometry at unit h/(2d), independent of A0 layout/queries.
    const std::int64_t unit = 2*static_cast<std::int64_t>(d), end = static_cast<std::int64_t>(n)*unit;
    std::int64_t origin = (phase_half ? static_cast<std::int64_t>(d) : 0)-2*static_cast<std::int64_t>(step);
    if (reflected) origin = -origin;
    std::vector<std::int64_t> integer{0,unit};
    for (std::int64_t k = -static_cast<std::int64_t>(n); k <= 2*static_cast<std::int64_t>(n); ++k) {
        const auto q = origin+k*unit; if (q >= 2*unit && q <= end-2*unit) integer.push_back(q);
    }
    integer.push_back(end-unit); integer.push_back(end);
    std::vector<double> result; for (auto q : integer) result.push_back(static_cast<double>(q)/(static_cast<double>(n)*static_cast<double>(unit)));
    return result;
}
void geometry_and_coverage(const HybridRezonePlan& plan, const HybridStaggeredState& source, const std::vector<double>& expected) {
    check(plan.diagnostics().preferred_cartesian_partition,"First-event test bypassed preferred planner");
    check(plan.node_relative_positions_m().size() == expected.size(),"Independent first-event target count differs");
    for (std::size_t j = 0; j < expected.size(); ++j) check(same(plan.node_relative_positions_m()[j],expected[j]),"Independent first-event node table differs");
    const auto& g = plan.preferred_grid();
    const auto coverage = [&](std::span<const HybridCoverageRecord> rows, std::span<const HybridCoverageBalance> balances, std::span<const double> nodes) {
        std::size_t index = 0;
        for (std::size_t i = 0; i+1 < nodes.size(); ++i) {
            ExactSum sum; std::size_t count = 0;
            for (auto k = g.first_cell; k < g.end_cell; ++k) {
                const double a = std::max(nodes[i],g.origin_relative_high_m+static_cast<double>(k)*g.spacing_m);
                const double b = std::min(nodes[i+1],g.origin_relative_high_m+static_cast<double>(k+1)*g.spacing_m);
                if (b <= a) continue;
                check(index < rows.size(),"Independent coverage missing positive record"); const auto& r = rows[index++];
                check(r.cell_slot == i && r.cartesian_index == k && r.begin_relative_m == a && r.end_relative_m == b && r.width_m == b-a && r.volume_m3 == b-a,"Independent coverage intersection differs");
                sum.add(b-a); ++count;
            }
            check(i < balances.size() && count == balances[i].records && sum.value() == nodes[i+1]-nodes[i],"Independent coverage does not cover full positive material interval");
        }
        check(index == rows.size() && balances.size()+1 == nodes.size(),"Extra coverage or missing material balance");
    };
    std::vector<double> old; for (const auto& node : source.canonical().nodes()) old.push_back(node.relative_position_m);
    coverage(plan.source_coverage(),plan.source_coverage_balances(),old); coverage(plan.planned_target_coverage(),plan.planned_target_coverage_balances(),expected);
}
void endpoints(const HybridStaggeredState& initial, const HybridStaggeredState& current) {
    const auto& a = initial.canonical(); const auto& b = current.canonical();
    for (bool right : {false,true}) {
        const auto& x = right ? a.nodes().back() : a.nodes().front(); const auto& y = right ? b.nodes().back() : b.nodes().front();
        check(same(x.relative_position_m,y.relative_position_m) && same(x.inertia_kg,y.inertia_kg) && same(x.momentum_kg_m_per_s,y.momentum_kg_m_per_s) &&
            same(exact_node_k(x),exact_node_k(y)) && same(x.velocity_m_per_s(),y.velocity_m_per_s()) && x.fixed == y.fixed,"Physical outer packet/trace changed at a genuine event");
        const auto& old = right ? a.cells().back() : a.cells().front(); const auto& now = right ? b.cells().back() : b.cells().front();
        check(old.mixture.masses == now.mixture.masses && same(old.occupied_volume_m3,now.occupied_volume_m3),"Finite cap material or width changed");
    }
    check(initial.material_caps().size() == current.material_caps().size(),"Cap identities changed count");
    for (std::size_t i = 0; i < initial.material_caps().size(); ++i) check(initial.material_caps()[i].identity == current.material_caps()[i].identity &&
        initial.material_caps()[i].original_species_masses_kg == current.material_caps()[i].original_species_masses_kg,"Cap material identity changed");
}
double validate_step(const HybridStaggeredState& initial, const HybridStaggeredState& before, const HybridStaggeredState& after, const HybridRezoneReport& report) {
    const auto& a = before.canonical(); const auto& b = after.canonical(); const auto& r = report.mechanics;
    check(!r.identity && !r.primal_records.empty() && !r.dual_records.empty(),"First-event driver used fake identity transport");
    check(after.identity().lineage == before.identity().lineage && after.identity().revision == before.identity().revision+1 && after.identity().snapshot != before.identity().snapshot,"Accepted event did not publish fresh immutable snapshot");
    endpoints(initial,after);
    check(a.model().get() == b.model().get() && a.model_metadata() == b.model_metadata() && ledger(a) == ledger(b),"Event replaced immutable EOS or initialization ledger");
    check(a.origin_high_m() == b.origin_high_m() && a.origin_low_m() == b.origin_low_m() && b.time_seconds() == 0 && b.gravity_m_per_s2() == 0,"Passive carrier changed canonical time/origin or introduced gravity");
    check(r.wall_impulse_kg_m_per_s == 0 && r.wall_heat_j == 0 && r.gravity_relocation_j == 0,"Pure rezone introduced external work/impulse");
    const auto old = actual(a), now = actual(b), start = actual(initial.canonical());
    for (std::size_t s = 0; s < 2; ++s) {
        ExactSum delta; for (const auto& c : b.cells()) delta.add(c.mixture.masses[s]); for (const auto& c : a.cells()) delta.add(-c.mixture.masses[s]);
        within(delta.value(),0,r.species_mass_bound_kg[s],"Independent constituent-local per-event mass balance");
        within(now.masses[s],start.masses[s],128*eps*start.masses[s],"Actual cumulative species balance");
    }
    ExactSum momentum; for (const auto& node : b.nodes()) momentum.add(node.momentum_kg_m_per_s); for (const auto& node : a.nodes()) momentum.add(-node.momentum_kg_m_per_s);
    within(momentum.value(),0,r.momentum_arithmetic_bound_kg_m_per_s,"Independent actual event momentum");
    within(now.momentum,start.momentum,128*eps*std::abs(start.momentum),"Actual cumulative momentum");
    const double delta_e = energy_difference(a,b), cumulative_e = energy_difference(initial.canonical(),b);
    within(delta_e,0,r.energy_arithmetic_bound_j,"Independent actual U+J^2/(2I) event energy");
    within(delta_e,r.energy_residual_j,8*eps*(old.kinetic+now.kinetic),"Independent energy oracle/report disagreement");
    within(cumulative_e,0,256*eps*(start.thermal+now.thermal+start.kinetic+now.kinetic),"Independent actual cumulative physical energy");
    check(now.entropy-start.entropy >= -128*eps*(now.entropy_scale+start.entropy_scale),"Actual cumulative EOS entropy loss");
    for (const auto& c : b.cells()) {
        const auto& profiles = b.model()->reference().materials(); ExactSum formation,capacity,moles;
        for (std::size_t s = 0; s < 2; ++s) { formation.add(c.mixture.masses[s]*profiles[s].energy_offset_j_per_kg); capacity.add(c.mixture.masses[s]*profiles[s].cv_j_per_kg_k); moles.add(c.mixture.masses[s]/profiles[s].molar_mass_kg_per_mol); }
        const double t = (c.mixture.energy-formation.value())/capacity.value();
        const double p = gas_constant*t*moles.value()/c.occupied_volume_m3;
        within(c.thermodynamics.temperature_k,t,64*eps*t,"Independent ideal-gas caloric EOS");
        within(c.thermodynamics.mechanical_pressure_pa,p,64*eps*p,"Independent ideal-gas pressure EOS");
        const auto checked = b.model()->evaluate(c.mixture,c.occupied_volume_m3);
        check(same(checked.temperature_k,c.thermodynamics.temperature_k) && same(checked.mechanical_pressure_pa,c.thermodynamics.mechanical_pressure_pa) && same(checked.entropy_j_per_k,c.thermodynamics.entropy_j_per_k),"Published EOS cache differs from actual conserved state");
    }
    const auto totals = b.totals(); within(totals.bulk_kinetic_energy_j+totals.mechanical_variance_j,now.kinetic,64*eps*now.kinetic,"Bulk/variance canonical K identity");
    ExactSum ledger_residual; ledger_residual.add(b.cumulative_energy_balance_error_j()); ledger_residual.add(-a.cumulative_energy_balance_error_j()); ledger_residual.add(-r.energy_residual_j);
    within(ledger_residual.value(),0,2*eps*(std::abs(a.cumulative_energy_balance_error_j())+std::abs(r.energy_residual_j)+std::abs(b.cumulative_energy_balance_error_j())),"Signed energy residual ledger addition");
    return cumulative_e;
}
void retirement_oracle(unsigned n, unsigned d, bool reflected, bool startup, double tau, const HybridStaggeredState& before, const HybridStaggeredState& after, const HybridRezoneReport& report) {
    const auto& a = before.canonical(); const auto& b = after.canonical(); const auto& r = report.mechanics;
    const std::size_t old_j = reflected ? a.cells().size()-1 : 1, new_j = reflected ? b.cells().size()-1 : 1;
    const std::size_t adjacent = reflected ? old_j-1 : old_j+1, cap = reflected ? b.cells().size()-1 : 0;
    const double v = a.nodes()[old_j].velocity_m_per_s(), w = a.nodes()[adjacent].velocity_m_per_s();
    check(v != w,"First-event field became constant and cannot exercise projection");
    const auto expected = inner_oracle(n,d,v,w); const auto& node = b.nodes()[new_j]; const auto& nr = r.nodes[new_j];
    within(node.inertia_kg,expected.inertia,0,"Independent event inertia");
    within(node.momentum_kg_m_per_s,expected.momentum,8*eps*std::abs(expected.momentum),"Independent event momentum");
    within(node.velocity_m_per_s(),expected.velocity,16*eps*std::abs(expected.velocity),"Independent event velocity");
    within(node.momentum_kg_m_per_s-a.nodes()[old_j].momentum_kg_m_per_s,expected.impulse,8*eps*(std::abs(node.momentum_kg_m_per_s)+std::abs(a.nodes()[old_j].momentum_kg_m_per_s)),"Independent local transported impulse");
    within(nr.transported_kinetic_j,expected.transported_h,8*eps*expected.transported_h,"Independent transported H");
    within(nr.provisional_kinetic_j,expected.kinetic,8*eps*expected.kinetic,"Independent node K");
    const double heat_bound = 64*eps*expected.transported_h+64*eps*expected.kinetic;
    within(nr.mixing_heat_j,expected.heat,heat_bound,"Independent positive convex Q");
    const double cap_bound = heat_bound/(3-1.0/d)+16*eps*expected.cap_heat;
    within(r.cells[cap].mixing_heat_j,expected.cap_heat,cap_bound,"Independent cap heat fraction");
    check(expected.heat > heat_bound && nr.mixing_heat_j > 0 && r.cells[cap].mixing_heat_j > 0,"First event did not resolve genuine positive mechanical heat");
    check(node.velocity_m_per_s() != v,"Shared cap node did not undergo the actual projection event");
    ExactSum delta_u; delta_u.add(b.cells()[cap].mixture.energy); delta_u.add(-a.cells()[reflected ? a.cells().size()-1 : 0].mixture.energy); delta_u.add(-r.cells[cap].mixing_heat_j);
    within(delta_u.value(),r.cells[cap].stored_u_roundoff_j,8*eps*std::abs(r.cells[cap].mixing_heat_j),"Actual cap U and signed stored-U rounding");
    const double actual_du = b.cells()[cap].mixture.energy-a.cells()[reflected ? a.cells().size()-1 : 0].mixture.energy;
    check(actual_du > 0,"Actual stored cap U was frozen instead of receiving resolved heat");
    std::cout << std::setprecision(17) << "FIRST_EVENT N=" << n << " C=" << 1.0/d << " reflected=" << reflected << " startup=" << startup << " tau=" << tau
        << " cells=" << a.cells().size() << "->" << b.cells().size() << " inner_du=" << node.velocity_m_per_s()-v << " inner_du_over_dt=" << (node.velocity_m_per_s()-v)*n*d
        << " Qnode=" << nr.mixing_heat_j << " Qcap=" << r.cells[cap].mixing_heat_j << " actual_cap_dU=" << actual_du << " outer_du_over_dt=0\n";
}
void one_case(unsigned n, unsigned d, bool phase_half, bool reflected) {
    Setup setup; auto state = setup.initialize(n,phase_half,reflected); const auto initial = state;
    const unsigned crossing = phase_half ? d/2 : d, steps = crossing+1; const double dt = 1.0/(n*d);
    unsigned insertions = 0, retirements = 0; double heat = 0, last_energy = 0;
    for (unsigned step = 1; step <= steps; ++step) {
        const auto before = state; const auto plan = plan_hybrid_staggered_rezone(state,grid(n,d,step,phase_half,reflected));
        const auto expected = independent_nodes(n,d,step,phase_half,reflected); geometry_and_coverage(plan,state,expected);
        const std::size_t expected_count = step == crossing ? n : n-1;
        check(expected.size()-1 == expected_count,"First-event independent expected count is wrong");
        const auto report = rezone_hybrid_staggered_into(state,plan); last_energy = validate_step(initial,before,state,report); heat += report.mechanics.mixing_heat_j;
        if (state.canonical().cells().size() > before.canonical().cells().size()) { ++insertions; check(step == crossing,"Insertion occurred at a false clock/event"); }
        if (state.canonical().cells().size() < before.canonical().cells().size()) {
            ++retirements; const bool startup = !phase_half && step == 1;
            check(startup || step == crossing+1,"Retirement occurred at a false clock/event");
            retirement_oracle(n,d,reflected,startup,step*dt,before,state,report);
        }
        check(state.authoritative_coverage().size() == plan.planned_target_coverage().size(),"Accepted coverage did not follow actual mechanical publication");
    }
    check(insertions == 1 && retirements == (phase_half ? 1U : 2U),"Driver did not execute genuine insertion and retirement");
    check(state.identity().revision == steps,"First-event history reset/rebound its source");
    std::cout << std::setprecision(17) << "FIRST_EVENT_SUMMARY N=" << n << " C=" << 1.0/d << " reflected=" << reflected << " phase_half=" << phase_half << " calls=" << steps
        << " insertions=" << insertions << " retirements=" << retirements << " Qsum=" << heat << " actual_cumulative_dE=" << last_energy << " trace_jump_rate=0 trace_TV=0\n";
}
void oracle_self_checks() {
    within(rational(U128(1),10),.1,0,"Rational nearest binary64 decimal rounding");
    auto two53 = U128(1); two53.shift(53); auto tie = two53; tie.add(U128(1));
    within(rational(tie,1,-53),1,0,"Rational ties-to-even low endpoint"); tie.add(U128(2));
    within(rational(tie,1,-53),std::nextafter(std::nextafter(1.0,2.0),2.0),0,"Rational ties-to-even high endpoint");
    const auto x = multiply(UINT64_C(1)<<52,UINT64_C(1)<<52); within(rational(x,2,-104),.5,0,"Independent full 106-bit product");
    const auto carry = (UINT64_C(1)<<62)-1;
    within(rational(multiply(carry,carry),carry),static_cast<double>(carry),0,"Independent 124-bit carry product and division");
    const auto o = inner_oracle(8,4,1,2); within(o.inertia,11.0/64,0,"Rational event inertia known fraction");
    within(o.momentum,7.0/32,0,"Rational event J known fraction"); within(o.velocity,14.0/11,0,"Rational event u known fraction");
    within(o.heat,3.0/176,0,"Rational event heat known fraction"); within(o.cap_heat,3.0/484,0,"Rational cap heat known fraction");
}
void minimal_events_and_reflections() { for (unsigned d : {2U,4U,16U,256U}) for (bool reflected : {false,true}) one_case(8,d,true,reflected); }
void comparable_startup_is_distinct() { for (unsigned d : {2U,4U,16U,256U}) for (bool reflected : {false,true}) one_case(32,d,false,reflected); }
void real_insertion_capacity_and_authenticated_rollback() {
    Setup setup;
    for (bool reflected : {false,true}) {
        auto state = setup.initialize(8,true,reflected,{7,4096,4096}); const auto initial = state;
        const auto accepted_plan = plan_hybrid_staggered_rezone(state,grid(8,4,1,true,reflected));
        const auto report = rezone_hybrid_staggered_into(state,accepted_plan); (void)validate_step(initial,initial,state,report);
        check(state.canonical().cells().size() == 7 && state.identity().revision == 1,"Capacity rollback lacks a real accepted predecessor");
        const auto before = fingerprint(state);
        fatal(FluidFatalCode::unsupported_configuration,[&] { (void)plan_hybrid_staggered_rezone(state,grid(8,4,2,true,reflected)); });
        check(fingerprint(state) == before,"Real 7-to-8 insertion capacity failure changed canonical state/snapshot/coverage/cache/ledgers");
        fatal(FluidFatalCode::unsupported_configuration,[&] { (void)rezone_hybrid_staggered_into(state,accepted_plan); });
        const auto foreign = setup.initialize(8,true,reflected,{7,4096,4096}); const auto foreign_plan = plan_hybrid_staggered_rezone(foreign,grid(8,4,1,true,reflected));
        fatal(FluidFatalCode::unsupported_configuration,[&] { (void)rezone_hybrid_staggered_into(state,foreign_plan); });
        fatal(FluidFatalCode::unsupported_topology,[&] { (void)plan_hybrid_staggered_rezone(state,{0,0,.125,1,8}); });
        check(fingerprint(state) == before,"Failure after real first-event success changed complete immutable state");
        endpoints(initial,state);
    }
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"independent exact-binary rational oracle calibration",oracle_self_checks},
        {"preferred 7-to-7-to-8-to-7 events and reflected packet/heat oracles",minimal_events_and_reflections},
        {"N32 startup retirement distinguished from later first crossing",comparable_startup_is_distinct},
        {"real max-cells7 insertion rejection and complete rollback",real_insertion_capacity_and_authenticated_rollback}
    };
    std::size_t passed = 0;
    for (const auto& [name,test] : tests) try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    std::cout << passed << '/' << tests.size() << " hybrid first-event test groups passed; inner-cap projection jumps remain; no whole-A2/repeated-rate/refinement/pressure/front gate claimed\n";
    return passed == tests.size() ? 0 : 1;
}
