#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <tuple>

// Independent exact-binary/sum test oracles copied from the frozen first-event test.
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
double internal_difference(const OccupiedStaggeredState& before, const OccupiedStaggeredState& after) {
    ExactSum sum;
    for (const auto& c : after.cells()) sum.add(c.mixture.energy);
    for (const auto& c : before.cells()) sum.add(-c.mixture.energy);
    return sum.value();
}
std::string ledger(const OccupiedStaggeredState& state) {
    const auto& l = state.initialization_ledger(); std::ostringstream out; out << std::hexfloat;
    for (double x : {l.prior_bulk_kinetic_energy_j,l.retained_kinetic_energy_j,l.additional_mechanical_energy_j,l.requested_internal_energy_change_j,l.represented_internal_energy_change_j,l.declared_added_energy_j,l.represented_total_energy_change_j,l.kinetic_partition_roundoff_j,l.momentum_reconciliation_kg_m_per_s,l.maximum_momentum_arithmetic_bound_kg_m_per_s}) out << x << ';';
    out << l.unresolved_energy_conversions; return out.str();
}


constexpr double duration = .125;
struct Csv {
    std::ofstream summaries, steps, nodes, cells, comparisons;
    explicit Csv(const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);
        const auto open = [&](std::ofstream& stream, const char* name, const char* heading) {
            stream.open(directory/name); check(static_cast<bool>(stream),"Cannot write passive diagnostic evidence"); stream << std::setprecision(17) << heading << '\n';
        };
        open(summaries,"summary.csv","case,N,C,phase,carrier,reverse,steps,tag_amount_L1,point_nodal_L1,dual_average_L1,linear_point_L1,initial_dual_L1,max_width_over_h,heat_per_time,cap_heat_left,cap_heat_right,mass_carrier_error,mass_tag_error,momentum_error,actual_energy_error,entropy_change,kinetic_heat_ledger,kinetic_budget,arithmetic_energy,stored_u_roundoff,represented_U_gain,inner_left_signed,inner_right_signed,inner_left_TV_per_time,inner_right_TV_per_time,inner_max_rate,left_event_signed,left_other_signed,right_event_signed,right_other_signed,events,equal_count_identity_events,outer_rate,outer_TV");
        open(steps,"steps.csv","case,step,tau,phi,old_cells,new_cells,event,old_cartesian_first,old_cartesian_last,new_cartesian_first,new_cartesian_last,equal_count_identity_event,inner_left_du,inner_right_du,inner_left_event_signed,inner_right_event_signed,Q,Qarith,stored_U_roundoff,Qcum,Qarithcum,actual_energy_error,mass_carrier_error,mass_tag_error,momentum_error,entropy_change,energy_ledger,geometry_ledger");
        open(nodes,"nodes.csv","case,stage,step,index,cap_near,q,I,J,u,exact_point,point_error,dual_left,dual_right,exact_dual_average,dual_error");
        open(cells,"cells.csv","case,stage,step,index,cap_near,left,right,width,mass,tag_mass,exact_tag_amount,tag_amount_error,U,T,P,cap");
        open(comparisons,"timestep.csv","family,C_coarse,C_fine,point_node_union_L1,tag_cell_union_L1,absolute_heat_rate_change");
    }
};
struct Config {
    unsigned n, d, phase; int carrier; bool reverse;
    [[nodiscard]] auto key() const { return std::tuple{n,d,phase,carrier,reverse}; }
    [[nodiscard]] std::string id() const { return "n"+std::to_string(n)+"_d"+std::to_string(d)+"_p"+std::to_string(phase)+"_a"+std::to_string(carrier)+(reverse ? "_reverse" : "_linear"); }
};
double exact_u(double q) { return 1+.1*q; }
double tag_integral(double a, double b) {
    const double width = b-a, middle = std::midpoint(a,b);
    return .25*width+.1*std::sin(2*std::numbers::pi*middle)*std::sin(std::numbers::pi*width)/std::numbers::pi;
}
double phi(const Config& c, double tau) {
    const double travel = c.reverse && tau > duration/2 ? duration-tau : tau;
    return static_cast<double>(c.phase)/(4*c.n)-c.carrier*travel;
}
struct Setup {
    std::shared_ptr<const FluidThermodynamics> model;
    Setup() {
        SpeciesRegistry registry; std::vector<ReferenceMaterial> profiles;
        for (const char* id : {"carrier","tag"}) {
            SpeciesDefinition s; s.id = id; s.name = id; s.phase = Phase::gas; s.atoms = {{"N",2}}; s.cp = 1040;
            s.formation_enthalpy = s.cp*reference_temperature*molecular_mass(s.atoms); registry.add(s); const auto& r = registry[registry.size()-1];
            ReferenceMaterial p; p.id = r.id; p.phase = ReferencePhase::ideal_gas; p.molar_mass_kg_per_mol = r.molar_mass;
            p.cv_j_per_kg_k = r.cv(); p.energy_offset_j_per_kg = r.internal_energy(0); p.minimum_temperature_k = 1e-20; p.maximum_temperature_k = 1e9;
            p.provenance = "Thermally identical passive diagnostic labels; no physical calibration"; profiles.push_back(p);
        }
        model = std::make_shared<const FluidThermodynamics>(registry,"hybrid-passive-diagnostic-v1",profiles);
    }
    HybridStaggeredState initialize(const Config& c) const {
        const double h = 1.0/c.n; std::vector<double> q{0,h};
        // Independent integer quarter-grid source geometry, not a mechanical
        // rezone/reset used to prepare every step. This initializer runs once.
        const int end = 4*static_cast<int>(c.n);
        for (int k = -static_cast<int>(c.n); k <= 2*static_cast<int>(c.n); ++k) {
            const int x = static_cast<int>(c.phase)+4*k; if (x >= 8 && x <= end-8) q.push_back(static_cast<double>(x)/(4*c.n));
        }
        q.push_back(1-h); q.push_back(1); StaggeredInitialization input; input.thermodynamics = model; input.positions_m = q;
        for (double x : q) input.velocities_m_per_s.push_back(exact_u(x));
        for (std::size_t i = 0; i+1 < q.size(); ++i) {
            const double width = q[i+1]-q[i], tag = tag_integral(q[i],q[i+1]);
            const auto mixture = model->initialize_at_temperature(std::vector{width-tag,tag},300,width).mixture;
            input.cells.push_back({mixture,mixture.mass()*std::midpoint(input.velocities_m_per_s[i],input.velocities_m_per_s[i+1])});
        }
        return bind_material_endcaps(initialize_occupied_staggered(input));
    }
};
struct PointField { std::vector<double> q,u; };
struct TagField { std::vector<double> q,density; };
PointField point_field(const OccupiedStaggeredState& s) {
    PointField field; for (const auto& node : s.nodes()) { field.q.push_back(node.relative_position_m); field.u.push_back(node.velocity_m_per_s()); } return field;
}
TagField tag_field(const OccupiedStaggeredState& s) {
    TagField field; for (const auto& node : s.nodes()) field.q.push_back(node.relative_position_m);
    for (const auto& c : s.cells()) field.density.push_back(c.mixture.masses[1]/c.occupied_volume_m3);
    return field;
}
double interpolate(const PointField& f, double q) {
    check(q >= f.q.front() && q <= f.q.back(),"Independent point interpolation outside occupied slab");
    if (q == f.q.back()) return f.u.back();
    const auto at = static_cast<std::size_t>(std::upper_bound(f.q.begin(),f.q.end(),q)-f.q.begin()-1);
    return std::lerp(f.u[at],f.u[at+1],(q-f.q[at])/(f.q[at+1]-f.q[at]));
}
double linear_absolute_integral(double width, double a, double b) {
    const double aa = std::abs(a), bb = std::abs(b);
    if (a == 0 || b == 0 || std::signbit(a) == std::signbit(b)) return .5*width*(aa+bb);
    const double scale = std::max(aa,bb), x = aa/scale, y = bb/scale;
    return .5*width*scale*(x*x+y*y)/(x+y);
}
double point_union(const PointField& a, const PointField& b) {
    check(a.q.front() == b.q.front() && a.q.back() == b.q.back(),"Point-field comparison changed physical occupied slab");
    std::vector<double> q = a.q; q.insert(q.end(),b.q.begin(),b.q.end()); std::sort(q.begin(),q.end()); q.erase(std::unique(q.begin(),q.end()),q.end());
    ExactSum sum; for (std::size_t i = 0; i+1 < q.size(); ++i) sum.add(linear_absolute_integral(q[i+1]-q[i],interpolate(a,q[i])-interpolate(b,q[i]),interpolate(a,q[i+1])-interpolate(b,q[i+1])));
    return sum.value();
}
double tag_union(const TagField& a, const TagField& b) {
    check(a.q.front() == b.q.front() && a.q.back() == b.q.back(),"Tag-field comparison changed physical occupied slab");
    std::size_t i = 0,j = 0; ExactSum sum;
    while (i < a.density.size() && j < b.density.size()) {
        const double left = std::max(a.q[i],b.q[j]), right = std::min(a.q[i+1],b.q[j+1]);
        if (right > left) sum.add((right-left)*std::abs(a.density[i]-b.density[j]));
        const bool advance_a = a.q[i+1] <= b.q[j+1], advance_b = b.q[j+1] <= a.q[i+1]; if (advance_a) ++i; if (advance_b) ++j;
    }
    check(i == a.density.size() && j == b.density.size(),"Independent tag-union coverage incomplete"); return sum.value();
}
struct Errors { double tag = 0,point = 0,dual = 0,linear = 0,width = 0; };
Errors errors(const OccupiedStaggeredState& s, unsigned n) {
    Errors e; ExactSum tag,point,dual; const auto field = point_field(s); const PointField affine{{0,1},{1,1.1}}; e.linear = point_union(field,affine);
    for (std::size_t i = 0; i < s.cells().size(); ++i) { const auto& c = s.cells()[i]; tag.add(std::abs(c.mixture.masses[1]-tag_integral(field.q[i],field.q[i+1]))); e.width = std::max(e.width,(field.q[i+1]-field.q[i])*n); }
    for (std::size_t j = 0; j < s.nodes().size(); ++j) {
        const auto& node = s.nodes()[j]; const double left = j == 0 ? field.q.front() : std::midpoint(field.q[j-1],field.q[j]);
        const double right = j+1 == s.nodes().size() ? field.q.back() : std::midpoint(field.q[j],field.q[j+1]);
        point.add(node.inertia_kg*std::abs(node.velocity_m_per_s()-exact_u(field.q[j])));
        dual.add(node.inertia_kg*std::abs(node.velocity_m_per_s()-exact_u(std::midpoint(left,right))));
    }
    e.tag = tag.value(); e.point = point.value(); e.dual = dual.value(); return e;
}
void profile(Csv& csv, const std::string& id, const char* stage, unsigned step, const OccupiedStaggeredState& s, bool full) {
    const auto nodes = s.nodes();
    for (std::size_t j = 0; j < nodes.size(); ++j) {
        const bool near = j < 6 || j+6 >= nodes.size(); if (!full && !near) continue; const auto& node = nodes[j];
        const double q = node.relative_position_m, left = j == 0 ? q : std::midpoint(nodes[j-1].relative_position_m,q);
        const double right = j+1 == nodes.size() ? q : std::midpoint(q,nodes[j+1].relative_position_m), average = exact_u(std::midpoint(left,right));
        csv.nodes << id << ',' << stage << ',' << step << ',' << j << ',' << near << ',' << q << ',' << node.inertia_kg << ',' << node.momentum_kg_m_per_s << ',' << node.velocity_m_per_s() << ',' << exact_u(q) << ',' << node.velocity_m_per_s()-exact_u(q) << ',' << left << ',' << right << ',' << average << ',' << node.velocity_m_per_s()-average << '\n';
    }
    for (std::size_t i = 0; i < s.cells().size(); ++i) {
        const bool near = i < 5 || i+5 >= s.cells().size(); if (!full && !near) continue; const auto& c = s.cells()[i];
        const double a = nodes[i].relative_position_m,b = nodes[i+1].relative_position_m,tag = tag_integral(a,b);
        csv.cells << id << ',' << stage << ',' << step << ',' << i << ',' << near << ',' << a << ',' << b << ',' << b-a << ',' << c.mixture.mass() << ',' << c.mixture.masses[1] << ',' << tag << ',' << c.mixture.masses[1]-tag << ',' << c.mixture.energy << ',' << c.thermodynamics.temperature_k << ',' << c.thermodynamics.mechanical_pressure_pa << ',' << (i == 0 || i+1 == s.cells().size()) << '\n';
    }
}
void outer_identity(const HybridStaggeredState& initial, const HybridStaggeredState& current) {
    const auto& a = initial.canonical(); const auto& b = current.canonical();
    for (bool right : {false,true}) {
        const auto& x = right ? a.nodes().back() : a.nodes().front(); const auto& y = right ? b.nodes().back() : b.nodes().front();
        check(same(x.relative_position_m,y.relative_position_m) && same(x.inertia_kg,y.inertia_kg) && same(x.momentum_kg_m_per_s,y.momentum_kg_m_per_s) && same(exact_node_k(x),exact_node_k(y)) && same(x.velocity_m_per_s(),y.velocity_m_per_s()) && x.fixed == y.fixed,"Repeated rezone changed physical outer packet/trace");
        const auto& c = right ? a.cells().back() : a.cells().front(); const auto& d = right ? b.cells().back() : b.cells().front();
        check(c.mixture.masses == d.mixture.masses && same(c.occupied_volume_m3,d.occupied_volume_m3),"Repeated rezone changed protected cap material/volume");
    }
    for (std::size_t i = 0; i < initial.material_caps().size(); ++i) check(initial.material_caps()[i].identity == current.material_caps()[i].identity,"Repeated rezone rekeyed cap identities");
}
void eos_and_metadata(const OccupiedStaggeredState& initial, const OccupiedStaggeredState& state) {
    check(initial.model().get() == state.model().get() && initial.model_metadata() == state.model_metadata() && ledger(initial) == ledger(state),"Passive diagnostic changed immutable model or initialization ledger");
    check(state.time_seconds() == initial.time_seconds() && state.origin_high_m() == initial.origin_high_m() && state.origin_low_m() == initial.origin_low_m() && state.gravity_m_per_s2() == 0,"Manufactured carrier mutated physical clock/origin/gravity");
    const auto& p = state.model()->reference().materials(); ExactSum represented_volume;
    for (std::size_t i = 0; i < state.cells().size(); ++i) {
        const auto& c = state.cells()[i]; represented_volume.add(c.occupied_volume_m3);
        check(same(c.occupied_volume_m3,state.nodes()[i+1].relative_position_m-state.nodes()[i].relative_position_m),"Passive stored volume differs from actual represented geometry");
        ExactSum formation,capacity,moles;
        for (std::size_t s = 0; s < 2; ++s) { formation.add(c.mixture.masses[s]*p[s].energy_offset_j_per_kg); capacity.add(c.mixture.masses[s]*p[s].cv_j_per_kg_k); moles.add(c.mixture.masses[s]/p[s].molar_mass_kg_per_mol); }
        const double t = (c.mixture.energy-formation.value())/capacity.value(), pressure = gas_constant*t*moles.value()/c.occupied_volume_m3;
        within(c.thermodynamics.temperature_k,t,64*eps*t,"Independent passive caloric EOS"); within(c.thermodynamics.mechanical_pressure_pa,pressure,64*eps*pressure,"Independent passive ideal-gas pressure");
        const auto recomputed = state.model()->evaluate(c.mixture,c.occupied_volume_m3);
        check(same(recomputed.temperature_k,c.thermodynamics.temperature_k) && same(recomputed.mechanical_pressure_pa,c.thermodynamics.mechanical_pressure_pa) && same(recomputed.entropy_j_per_k,c.thermodynamics.entropy_j_per_k),"Passive published EOS cache disagrees with actual conserved state");
        for (double mass : c.mixture.masses) check(mass > 0,"Passive transport dropped a positive species");
        const double fraction = c.mixture.masses[1]/c.mixture.mass(); check(fraction >= .15-64*eps*.15 && fraction <= .35+64*eps*.35,"Passive species fraction left analytic initial extrema");
    }
    check(represented_volume.value() == 1,"Passive represented occupied slab has a coverage gap or overlap");
    for (const auto& node : state.nodes()) check(node.velocity_m_per_s() >= 1-64*eps && node.velocity_m_per_s() <= 1.1+64*eps*1.1,"Passive velocity left initial extrema");
}
struct Result {
    Config config; PointField velocity; TagField tag; Errors error;
    double heat_rate = 0,energy_error = 0;
};
std::vector<std::int64_t> cartesian_ids(std::span<const HybridCellIdentity> cells) {
    std::vector<std::int64_t> ids;
    for (const auto& cell : cells) if (cell.kind == HybridCellKind::interior_cartesian) ids.push_back(cell.cartesian_index);
    check(!ids.empty(),"Bounded passive diagnostic unexpectedly lost all Cartesian interior cells"); return ids;
}
std::vector<std::int64_t> cartesian_ids(const HybridRezonePlan& plan) {
    std::vector<HybridCellIdentity> cells; for (const auto& cell : plan.planned_cells()) cells.push_back(cell.identity); return cartesian_ids(cells);
}
Result run_case(const Setup& setup, Config c, Csv& csv) {
    const auto id = c.id(); std::cout << "BEGIN_PASSIVE " << id << '\n';
    auto state = setup.initialize(c); const auto initial = state; const auto start = actual(state.canonical()); const auto initial_error = errors(state.canonical(),c.n);
    profile(csv,id,"initial",0,state.canonical(),true); const unsigned steps = c.n*c.d/8; check(steps*8 == c.n*c.d,"Selected carrier timestep does not end exactly at T");
    // Bind intentionally labels original material as unrezoned. Inspect a
    // read-only initial plan to authenticate its matching grid geometry/IDs;
    // do not publish an identity rezone or mutate the initial material state.
    const auto initial_plan = plan_hybrid_staggered_rezone(state,{phi(c,0),0,1.0/c.n,-static_cast<std::int64_t>(c.n),2*static_cast<std::int64_t>(c.n)});
    check(initial_plan.node_relative_positions_m().size() == state.canonical().nodes().size(),"Initial passive source and preferred grid differ");
    for (std::size_t j = 0; j < state.canonical().nodes().size(); ++j) check(same(initial_plan.node_relative_positions_m()[j],state.canonical().nodes()[j].relative_position_m),"Read-only initial grid plan does not match actual original material geometry");
    auto previous_ids = cartesian_ids(initial_plan);
    const double dt = duration/steps; ExactSum qsum,qarith,stored_u,kinetic_budget,cap_left,cap_right,cap_stored_left,cap_stored_right;
    std::array<ExactSum,2> signed_inner,tv_inner,event_inner,other_inner; double inner_rate = 0,last_energy = 0,maximum_width = initial_error.width; unsigned events = 0,equal_count_events = 0;
    for (unsigned step = 1; step <= steps; ++step) {
        const double tau = step*dt; const auto before = state; const auto old = actual(before.canonical());
        const auto plan = plan_hybrid_staggered_rezone(state,{phi(c,tau),0,1.0/c.n,-static_cast<std::int64_t>(c.n),2*static_cast<std::int64_t>(c.n)});
        if (step != 1) check(cartesian_ids(before.source_cell_identities()) == previous_ids,"Previous accepted Cartesian identities disagree with event bookkeeping");
        const auto next_ids = cartesian_ids(plan);
        const bool count_change = plan.planned_cells().size() != before.canonical().cells().size();
        const bool equal_count_event = !count_change && next_ids != previous_ids, event = count_change || next_ids != previous_ids;
        if (event) profile(csv,id,"before_event",step,before.canonical(),false);
        // A typed topology/capacity/EOS error is fatal to this diagnostic.
        // There is no retry, field overwrite, reinitialization or rebind.
        const auto report = rezone_hybrid_staggered_into(state,plan); const auto& r = report.mechanics; const auto now = actual(state.canonical());
        check(state.identity().revision == step && state.identity().snapshot != before.identity().snapshot && state.identity().lineage == initial.identity().lineage,"Repeated history reset/rebound or reused a snapshot");
        outer_identity(initial,state); eos_and_metadata(initial.canonical(),state.canonical());
        check(cartesian_ids(state.source_cell_identities()) == next_ids,"Accepted Cartesian identities do not match planned event");
        for (const auto& cell : state.canonical().cells()) maximum_width = std::max(maximum_width,cell.occupied_volume_m3*c.n);
        for (std::size_t s = 0; s < 2; ++s) {
            ExactSum delta; for (const auto& x : state.canonical().cells()) delta.add(x.mixture.masses[s]); for (const auto& x : before.canonical().cells()) delta.add(-x.mixture.masses[s]);
            within(delta.value(),0,r.species_mass_bound_kg[s],"Independent passive per-call species balance"); within(now.masses[s],start.masses[s],128*eps*start.masses[s],"Independent passive cumulative species balance");
        }
        ExactSum delta_p; for (const auto& node : state.canonical().nodes()) delta_p.add(node.momentum_kg_m_per_s);
        for (const auto& node : before.canonical().nodes()) delta_p.add(-node.momentum_kg_m_per_s);
        within(delta_p.value(),0,r.momentum_arithmetic_bound_kg_m_per_s,"Independent passive per-call actual momentum");
        within(now.momentum,start.momentum,128*eps*std::abs(start.momentum),"Independent passive cumulative momentum");
        const double delta_energy = energy_difference(before.canonical(),state.canonical()); last_energy = energy_difference(initial.canonical(),state.canonical());
        within(delta_energy,0,r.energy_arithmetic_bound_j,"Independent passive per-call actual U+K+PE balance");
        within(delta_energy,r.energy_residual_j,8*eps*(old.kinetic+now.kinetic),"Passive exact-binary kinetic energy/report agreement");
        within(last_energy,0,256*eps*(start.thermal+now.thermal+start.kinetic+now.kinetic),"Independent passive cumulative physical energy balance");
        check(now.entropy-start.entropy >= -128*eps*(start.entropy_scale+now.entropy_scale),"Passive cumulative EOS entropy loss");
        check(r.gravity_relocation_j == 0 && r.wall_heat_j == 0 && r.wall_impulse_kg_m_per_s == 0,"Passive diagnostic introduced external work/impulse");
        ExactSum ledger_error; ledger_error.add(state.canonical().cumulative_energy_balance_error_j()); ledger_error.add(-before.canonical().cumulative_energy_balance_error_j()); ledger_error.add(-r.energy_residual_j);
        within(ledger_error.value(),0,2*eps*(std::abs(state.canonical().cumulative_energy_balance_error_j())+std::abs(before.canonical().cumulative_energy_balance_error_j())+std::abs(r.energy_residual_j)),"Passive signed arithmetic ledger addition");
        ExactSum geometry_ledger; geometry_ledger.add(state.canonical().cumulative_geometry_volume_residual_m3()); geometry_ledger.add(-before.canonical().cumulative_geometry_volume_residual_m3()); geometry_ledger.add(-r.geometry_volume_residual_m3);
        within(geometry_ledger.value(),0,2*eps*(std::abs(state.canonical().cumulative_geometry_volume_residual_m3())+std::abs(before.canonical().cumulative_geometry_volume_residual_m3())+std::abs(r.geometry_volume_residual_m3)),"Passive represented geometry ledger addition");
        const auto totals = state.canonical().totals(); within(totals.bulk_kinetic_energy_j+totals.mechanical_variance_j,now.kinetic,64*eps*now.kinetic,"Passive canonical K=bulk+variance");
        qsum.add(r.mixing_heat_j); qarith.add(r.arithmetic_energy_j); cap_left.add(r.cells.front().mixing_heat_j); cap_right.add(r.cells.back().mixing_heat_j);
        cap_stored_left.add(r.cells.front().stored_u_roundoff_j); cap_stored_right.add(r.cells.back().stored_u_roundoff_j);
        kinetic_budget.add(64*eps*old.kinetic); kinetic_budget.add(64*eps*now.kinetic); kinetic_budget.add(std::abs(r.gather_kinetic_residual_j));
        double step_stored = 0; for (const auto& x : r.cells) { stored_u.add(x.stored_u_roundoff_j); step_stored += x.stored_u_roundoff_j; kinetic_budget.add(std::abs(x.rebalance_kinetic_residual_j)); }
        within(qsum.value()+qarith.value()-(start.kinetic-now.kinetic),0,kinetic_budget.value(),"Independent cumulative requested heat/kinetic-loss ledger");
        const double total_mass = start.masses[0]+start.masses[1], minimum_k = .5*start.momentum*(start.momentum/total_mass);
        check(qsum.value() <= start.kinetic-minimum_k+std::abs(qarith.value())+kinetic_budget.value(),"Numerical heat exceeds available initial mechanical variance");
        std::array<double,2> du{};
        for (std::size_t side = 0; side < 2; ++side) {
            const auto old_index = side == 0 ? 1 : before.canonical().cells().size()-1, new_index = side == 0 ? 1 : state.canonical().cells().size()-1;
            du[side] = state.canonical().nodes()[new_index].velocity_m_per_s()-before.canonical().nodes()[old_index].velocity_m_per_s();
            signed_inner[side].add(du[side]); tv_inner[side].add(std::abs(du[side])); (event ? event_inner[side] : other_inner[side]).add(du[side]); inner_rate = std::max(inner_rate,std::abs(du[side])/dt);
        }
        if (event) { ++events; profile(csv,id,"after_event",step,state.canonical(),false); }
        if (equal_count_event) ++equal_count_events;
        csv.steps << id << ',' << step << ',' << tau << ',' << phi(c,tau) << ',' << before.canonical().cells().size() << ',' << state.canonical().cells().size() << ',' << event << ',' << previous_ids.front() << ',' << previous_ids.back() << ',' << next_ids.front() << ',' << next_ids.back() << ',' << equal_count_event << ',' << du[0] << ',' << du[1] << ',' << event_inner[0].value() << ',' << event_inner[1].value() << ',' << r.mixing_heat_j << ',' << r.arithmetic_energy_j << ',' << step_stored << ',' << qsum.value() << ',' << qarith.value() << ',' << last_energy << ',' << now.masses[0]-start.masses[0] << ',' << now.masses[1]-start.masses[1] << ',' << now.momentum-start.momentum << ',' << now.entropy-start.entropy << ',' << state.canonical().cumulative_energy_balance_error_j() << ',' << state.canonical().cumulative_geometry_volume_residual_m3() << '\n';
        previous_ids = next_ids;
    }
    const auto now = actual(state.canonical()); const auto e = errors(state.canonical(),c.n); profile(csv,id,"final",steps,state.canonical(),true);
    for (std::size_t side = 0; side < 2; ++side) {
        const auto old_j = side == 0 ? 1 : initial.canonical().cells().size()-1, new_j = side == 0 ? 1 : state.canonical().cells().size()-1;
        const double direct = state.canonical().nodes()[new_j].velocity_m_per_s()-initial.canonical().nodes()[old_j].velocity_m_per_s();
        within(signed_inner[side].value(),direct,8*eps*(tv_inner[side].value()+std::abs(direct)),"Inner signed jump telescope");
        within(event_inner[side].value()+other_inner[side].value(),direct,8*eps*(tv_inner[side].value()+std::abs(direct)),"Inner event/non-event decomposition");
        const auto& old_cap = side == 0 ? initial.canonical().cells().front() : initial.canonical().cells().back();
        const auto& new_cap = side == 0 ? state.canonical().cells().front() : state.canonical().cells().back();
        const double heat = side == 0 ? cap_left.value() : cap_right.value(), roundoff = side == 0 ? cap_stored_left.value() : cap_stored_right.value();
        ExactSum cap_u; cap_u.add(new_cap.mixture.energy); cap_u.add(-old_cap.mixture.energy); cap_u.add(-heat); cap_u.add(-roundoff);
        within(cap_u.value(),0,8*eps*(std::abs(heat)+std::abs(roundoff)),"Actual cumulative cap U differs from compatible heat plus signed representation ledger");
    }
    const double heat_ledger = qsum.value()+qarith.value()-(start.kinetic-now.kinetic);
    csv.summaries << id << ',' << c.n << ',' << 1.0/c.d << ',' << c.phase*.25 << ',' << c.carrier << ',' << c.reverse << ',' << steps << ',' << e.tag << ',' << e.point << ',' << e.dual << ',' << e.linear << ',' << initial_error.dual << ',' << maximum_width << ',' << qsum.value()/duration << ',' << cap_left.value() << ',' << cap_right.value() << ',' << now.masses[0]-start.masses[0] << ',' << now.masses[1]-start.masses[1] << ',' << now.momentum-start.momentum << ',' << last_energy << ',' << now.entropy-start.entropy << ',' << heat_ledger << ',' << kinetic_budget.value() << ',' << qarith.value() << ',' << stored_u.value() << ',' << internal_difference(initial.canonical(),state.canonical()) << ',' << signed_inner[0].value() << ',' << signed_inner[1].value() << ',' << tv_inner[0].value()/duration << ',' << tv_inner[1].value()/duration << ',' << inner_rate << ',' << event_inner[0].value() << ',' << other_inner[0].value() << ',' << event_inner[1].value() << ',' << other_inner[1].value() << ',' << events << ',' << equal_count_events << ",0,0\n";
    csv.summaries.flush(); csv.steps.flush(); csv.nodes.flush(); csv.cells.flush();
    std::cout << std::setprecision(17) << "PASSIVE " << id << " tag_L1=" << e.tag << " point_L1=" << e.point << " dual_L1=" << e.dual << " linear_L1=" << e.linear << " Q_per_time=" << qsum.value()/duration << " actual_dE=" << last_energy << " inner_max_du_dt=" << inner_rate << " inner_TV_per_time=" << (tv_inner[0].value()+tv_inner[1].value())/duration << " events=" << events << " equal_count_identity_events=" << equal_count_events << " max_history_width_over_h=" << maximum_width << " outer_rate=0 outer_TV=0\n";
    return {c,point_field(state.canonical()),tag_field(state.canonical()),e,qsum.value()/duration,last_energy};
}
void independent_integral_calibration() {
    within(tag_integral(0,1),.25,8*eps*.25,"Independent full sinusoidal tag integral");
    within(tag_integral(0,.5),.125+.1/std::numbers::pi,8*eps*.16,"Independent half sinusoidal tag integral");
    const PointField rising{{0,.25,1},{0,.25,1}},falling{{0,.5,.75,1},{1,.5,.25,0}};
    within(point_union(rising,falling),.5,8*eps*.5,"Independent unequal-node union absolute affine integral");
    within(tag_union({{0,.5,1},{0,2}},{{0,.25,.75,1},{1,1,1}}),1,8*eps,"Independent unequal-cell union integral");
}
void decreasing(double fine, double coarse, const char* message) { check((fine == 0 && coarse == 0) || fine < coarse,message); }
}
int main(int argc, char** argv) {
    try {
        check(argc == 1 || (argc == 3 && std::string(argv[1]) == "--output"),"Usage: test [--output directory]");
        const std::filesystem::path directory = argc == 3 ? argv[2] : "hybrid-passive-profiles"; Csv csv(directory); independent_integral_calibration(); Setup setup;
        std::map<decltype(Config{}.key()),Result> results;
        const auto measure = [&](Config c) -> const Result& {
            const auto key = c.key(); const auto found = results.find(key); if (found != results.end()) return found->second;
            return results.emplace(key,run_case(setup,c,csv)).first->second;
        };
        // Complete the bounded data table before evaluating convergence trends.
        for (bool reverse : {false,true}) for (int carrier : {-1,1}) for (unsigned phase = 0; phase < 4; ++phase) {
            for (unsigned n : {16U,32U,64U}) (void)measure({n,4,phase,carrier,reverse});
            for (unsigned d : {2U,4U,8U,16U,64U,256U}) (void)measure({32,d,phase,carrier,reverse});
        }
        std::size_t failures = 0;
        const auto verify = [&](const std::function<void()>& f) { try { f(); } catch (const std::exception& e) { ++failures; std::cerr << "FAIL passive diagnostic trend: " << e.what() << '\n'; } };
        for (bool reverse : {false,true}) for (int carrier : {-1,1}) for (unsigned phase = 0; phase < 4; ++phase) {
            const std::string family = "p"+std::to_string(phase)+"_a"+std::to_string(carrier)+(reverse ? "_reverse" : "_linear");
            const auto& a = measure({16,4,phase,carrier,reverse}); const auto& b = measure({32,4,phase,carrier,reverse}); const auto& c = measure({64,4,phase,carrier,reverse});
            verify([&] { check(b.error.tag < .8*a.error.tag && c.error.tag < .8*b.error.tag,(family+": unchanged <.8 tag grid criterion").c_str()); });
            verify([&] { check(b.error.point < .8*a.error.point && c.error.point < .8*b.error.point,(family+": unchanged <.8 point-nodal grid criterion").c_str()); });
            verify([&] { decreasing(b.error.dual,a.error.dual,(family+": dual error grid decay").c_str()); decreasing(c.error.dual,b.error.dual,(family+": dual error grid decay").c_str()); });
            verify([&] { decreasing(b.error.linear,a.error.linear,(family+": interpolated point profile grid decay").c_str()); decreasing(c.error.linear,b.error.linear,(family+": interpolated point profile grid decay").c_str()); });
            verify([&] { decreasing(b.heat_rate,a.heat_rate,(family+": heat/T grid decay").c_str()); decreasing(c.heat_rate,b.heat_rate,(family+": heat/T grid decay").c_str()); });
            const std::array<unsigned,6> ds{2,4,8,16,64,256}; double previous_u = 0,previous_tag = 0,previous_heat = 0;
            for (std::size_t i = 0; i+1 < ds.size(); ++i) {
                const auto& coarse = measure({32,ds[i],phase,carrier,reverse}); const auto& fine = measure({32,ds[i+1],phase,carrier,reverse});
                const double u = point_union(coarse.velocity,fine.velocity), tag = tag_union(coarse.tag,fine.tag), heat = std::abs(coarse.heat_rate-fine.heat_rate);
                csv.comparisons << family << ',' << 1.0/ds[i] << ',' << 1.0/ds[i+1] << ',' << u << ',' << tag << ',' << heat << '\n';
                std::cout << std::setprecision(17) << "TIMESTEP " << family << " C=" << 1.0/ds[i] << "->" << 1.0/ds[i+1] << " node_union=" << u << " tag_union=" << tag << " heat_rate_change=" << heat << '\n';
                if (i != 0) {
                    verify([&] { decreasing(u,previous_u,(family+": node-union timestep differences do not decrease").c_str()); });
                    verify([&] { decreasing(tag,previous_tag,(family+": tag-union timestep differences do not decrease").c_str()); });
                    verify([&] { decreasing(heat,previous_heat,(family+": heat/T timestep changes do not decrease").c_str()); });
                }
                previous_u = u; previous_tag = tag; previous_heat = heat;
            }
        }
        csv.comparisons.flush(); std::cout << "PASSIVE_DIAGNOSTIC cases=" << results.size() << " trend_failures=" << failures << "; exact physical outer identity retained; inner cap jumps remain; no whole-A2/nonlinear/pressure/front gate claimed\n";
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "FAIL passive diagnostic fatal without retry: " << error.what() << '\n'; return 2; }
}
