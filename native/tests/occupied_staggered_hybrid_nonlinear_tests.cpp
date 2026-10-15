#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <optional>
#include <tuple>
#include <type_traits>

// Test-only nonlinear passive gate. No pressure/body timestep or method change.
// Independent binary arithmetic copied from the accepted passive test unchanged.
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
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }
void within(double actual, double expected, double bound, const char *message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || !std::isfinite(bound) || bound < 0 ||
        std::abs(actual - expected) > bound) {
        std::ostringstream out;
        out << message << std::setprecision(17) << ": actual=" << actual << " expected=" << expected
            << " bound=" << bound;
        throw std::runtime_error(out.str());
    }
}
template <class F> void fatal(FluidFatalCode code, F &&function) {
    bool caught = false;
    try {
        function();
    } catch (const FluidFatalError &e) {
        caught = e.code() == code;
    }
    check(caught, "Expected typed first-event rejection");
}

// Independent bounded integer rational oracle. This is test-only arithmetic;
// it does not call the remapper's packet/normalization/heat helpers, and does
// not depend on long double being wider than double on Windows.
struct U128 {
    std::array<std::uint32_t, 4> a{};
    explicit U128(std::uint64_t x = 0)
        : a{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(x >> 32), 0, 0} {}
    [[nodiscard]] int bits() const {
        for (std::size_t i = 4; i != 0; --i)
            if (a[i - 1] != 0)
                return static_cast<int>((i - 1) * 32) + static_cast<int>(std::bit_width(a[i - 1]));
        return 0;
    }
    [[nodiscard]] int compare(const U128 &b) const {
        for (std::size_t i = 4; i != 0; --i)
            if (a[i - 1] != b.a[i - 1])
                return a[i - 1] > b.a[i - 1] ? 1 : -1;
        return 0;
    }
    void shift(unsigned n = 1) {
        for (unsigned k = 0; k < n; ++k) {
            std::uint32_t carry = 0;
            for (auto &x : a) {
                const auto next = x >> 31;
                x = (x << 1) | carry;
                carry = next;
            }
            check(carry == 0, "Independent rational oracle shift overflow");
        }
    }
    void subtract(const U128 &b) {
        check(compare(b) >= 0, "Independent rational oracle negative remainder");
        std::uint64_t borrow = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const std::uint64_t sub = static_cast<std::uint64_t>(b.a[i]) + borrow, old = a[i];
            a[i] = static_cast<std::uint32_t>(old - sub);
            borrow = old < sub ? 1 : 0;
        }
        check(borrow == 0, "Independent rational oracle subtraction underflow");
    }
    void add(const U128 &b) {
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const std::uint64_t v = static_cast<std::uint64_t>(a[i]) + b.a[i] + carry;
            a[i] = static_cast<std::uint32_t>(v);
            carry = v >> 32;
        }
        check(carry == 0, "Independent rational oracle addition overflow");
    }
    void times(std::uint32_t b) {
        std::uint64_t carry = 0;
        for (auto &x : a) {
            const std::uint64_t v = static_cast<std::uint64_t>(x) * b + carry;
            x = static_cast<std::uint32_t>(v);
            carry = v >> 32;
        }
        check(carry == 0, "Independent rational oracle product overflow");
    }
};
U128 multiply(std::uint64_t x, std::uint64_t y) {
    U128 result;
    const U128 left(x), right(y);
    for (std::size_t i = 0; i < 2; ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < 2; ++j) {
            const std::uint64_t v = static_cast<std::uint64_t>(left.a[i]) * right.a[j] + result.a[i + j] + carry;
            result.a[i + j] = static_cast<std::uint32_t>(v);
            carry = v >> 32;
        }
        result.a[i + 2] = static_cast<std::uint32_t>(carry);
    }
    return result;
}
double rational(U128 numerator, std::uint64_t denominator, int power = 0, bool negative = false) {
    check(denominator != 0, "Independent rational oracle zero denominator");
    if (numerator.bits() == 0)
        return 0;
    U128 divisor(denominator);
    int exponent = numerator.bits() - divisor.bits();
    if (exponent >= 0)
        divisor.shift(static_cast<unsigned>(exponent));
    else
        numerator.shift(static_cast<unsigned>(-exponent));
    if (numerator.compare(divisor) < 0) {
        numerator.shift();
        --exponent;
    }
    check(numerator.compare(divisor) >= 0, "Independent rational oracle normalization failure");
    numerator.subtract(divisor);
    std::uint64_t significand = UINT64_C(1) << 52;
    for (int bit = 51; bit >= 0; --bit) {
        numerator.shift();
        if (numerator.compare(divisor) >= 0) {
            numerator.subtract(divisor);
            significand |= UINT64_C(1) << bit;
        }
    }
    numerator.shift();
    const int rounding = numerator.compare(divisor);
    if (rounding > 0 || (rounding == 0 && (significand & 1) != 0))
        ++significand;
    if (significand == (UINT64_C(1) << 53)) {
        significand >>= 1;
        ++exponent;
    }
    double result = std::ldexp(static_cast<double>(significand), exponent + power - 52);
    check(std::isfinite(result) && result > 0, "Independent rational oracle result outside selected normal range");
    return negative ? -result : result;
}
struct Binary {
    std::uint64_t mantissa;
    int exponent;
};
Binary positive_binary(double x) {
    const auto bits = std::bit_cast<std::uint64_t>(std::abs(x));
    const int exponent = static_cast<int>((bits >> 52) & 2047);
    check(exponent != 0 && exponent != 2047, "Actual-energy oracle requires the selected finite normal operands");
    return {(bits & ((UINT64_C(1) << 52) - 1)) | (UINT64_C(1) << 52), exponent - 1023 - 52};
}
double exact_node_k(const StaggeredNode &node) {
    if (node.momentum_kg_m_per_s == 0)
        return 0;
    const auto j = positive_binary(node.momentum_kg_m_per_s), i = positive_binary(node.inertia_kg);
    return rational(multiply(j.mantissa, j.mantissa), 2 * i.mantissa, 2 * j.exponent - i.exponent);
}
// A growing TwoSum expansion independently sums the actual stored scalars,
// including signed before/after differences without subtracting bulk totals.
struct ExactSum {
    std::vector<double> terms;
    void add(double x) {
        std::size_t count = 0;
        for (double y : terms) {
            const double sum = x + y, z = sum - x, remainder = (x - (sum - z)) + (y - z);
            check(std::isfinite(sum) && std::isfinite(remainder), "Independent actual-sum range failure");
            if (remainder != 0)
                terms[count++] = remainder;
            x = sum;
        }
        terms.resize(count);
        if (x != 0 || terms.empty())
            terms.push_back(x);
    }
    [[nodiscard]] double value() const {
        double sum = 0;
        for (double x : terms)
            sum += x;
        return sum;
    }
};
constexpr double duration = .125;
struct Config {
    unsigned n = 32, d = 4, phase = 0;
    int carrier = 1;
    bool reverse = false, reflected = false, uniform = false;
    [[nodiscard]] std::string id() const {
        return "n" + std::to_string(n) + "_d" + std::to_string(d) + "_p" + std::to_string(phase) + "_a" +
               std::to_string(carrier) + (reverse ? "_cycle" : "_linear") +
               (reflected ? "_reflected" : "_forward") + (uniform ? "_uniform" : "_nonlinear");
    }
};
double phi(const Config &c, double tau) {
    return static_cast<double>(c.phase) / (4 * c.n) -
           c.carrier * (c.reverse ? std::min(tau, duration - tau) : tau);
}
double velocity(const Config &c, double q) {
    if (c.uniform)
        return c.reflected ? -1 : 1;
    const double r = c.reflected ? 1 - q : q;
    const double u = 1 + r * (.125 + r * (.0625 + r * .03125));
    return c.reflected ? -u : u;
}
double sampled_mass(std::size_t species, unsigned left, unsigned right, unsigned denominator) {
    // Exact integer primitive on the declared dyadic quarter-grid. All rho*Y
    // coefficients are integers times 2^-45; a common 60*D^5 denominator fits
    // this bounded U128 oracle. Only the final rational conversion rounds.
    constexpr std::int64_t b43 = INT64_C(1) << 43, b42 = INT64_C(1) << 42, b39 = INT64_C(1) << 39,
                           b37 = INT64_C(1) << 37;
    const std::array<std::array<std::int64_t, 5>, 3> coefficients{
        {{{3 * b43 - 32, b42 - 24, 3 * b39 - 16, -b39 - 4, -b37 - 1}},
         {{b43, b42, 5 * b39, b39, b37}},
         {{32, 24, 16, 4, 1}}}};
    check(species < 3 && denominator <= 256 && std::has_single_bit(denominator) && right > left &&
              right <= denominator,
          "Sampling oracle outside declared quarter-grid domain");
    U128 positive, negative;
    const unsigned power = std::countr_zero(denominator);
    for (std::size_t k = 0; k < 5; ++k) {
        std::uint64_t a = 1, b = 1;
        for (std::size_t j = 0; j <= k; ++j) {
            a *= left;
            b *= right;
        }
        const auto coefficient = coefficients[species][k];
        auto term = multiply(static_cast<std::uint64_t>(coefficient < 0 ? -coefficient : coefficient), b - a);
        term.shift(power * static_cast<unsigned>(4 - k));
        term.times(60 / static_cast<std::uint32_t>(k + 1));
        (coefficient < 0 ? negative : positive).add(term);
    }
    positive.subtract(negative);
    return rational(positive, 60, -45 - 5 * static_cast<int>(power));
}
struct Setup {
    std::shared_ptr<const FluidThermodynamics> model;
    Setup() {
        SpeciesRegistry registry;
        std::vector<ReferenceMaterial> profiles;
        const std::array<const char *, 3> ids{"surrogate-A", "surrogate-B", "surrogate-trace"};
        const std::array<double, 3> cp{1040, 920, 850};
        const std::array<Composition, 3> atoms{{{{"N", 2}}, {{"O", 2}}, {{"C", 1}, {"O", 2}}}};
        for (std::size_t i = 0; i < 3; ++i) {
            SpeciesDefinition s;
            s.id = ids[i];
            s.name = ids[i];
            s.phase = Phase::gas;
            s.atoms = atoms[i];
            s.cp = cp[i];
            s.formation_enthalpy = s.cp * reference_temperature * molecular_mass(s.atoms);
            registry.add(s);
            const auto &r = registry[registry.size() - 1];
            ReferenceMaterial p;
            p.id = r.id;
            p.phase = ReferencePhase::ideal_gas;
            p.molar_mass_kg_per_mol = r.molar_mass;
            p.cv_j_per_kg_k = r.cv();
            p.energy_offset_j_per_kg = r.internal_energy(0);
            p.minimum_temperature_k = 1e-20;
            p.maximum_temperature_k = 1e9;
            p.provenance = "Nonlinear passive numerical surrogate, cp 1040/920/850; no physical calibration";
            profiles.push_back(p);
        }
        model = std::make_shared<const FluidThermodynamics>(registry, "hybrid-nonlinear-passive-v1", profiles);
    }
    HybridStaggeredState initialize(const Config &c, const HybridGeometryLimits &limits = {}) const {
        const double h = 1.0 / c.n;
        std::vector<double> q{0, h};
        const int end = 4 * static_cast<int>(c.n);
        for (int k = -static_cast<int>(c.n); k <= 2 * static_cast<int>(c.n); ++k) {
            const int x = static_cast<int>(c.phase) + 4 * k;
            if (x >= 8 && x <= end - 8)
                q.push_back(static_cast<double>(x) / (4 * c.n));
        }
        q.push_back(1 - h);
        q.push_back(1);
        StaggeredInitialization input;
        input.thermodynamics = model;
        input.positions_m = q;
        for (double x : q)
            input.velocities_m_per_s.push_back(velocity(c, x));
        for (std::size_t i = 0; i + 1 < q.size(); ++i) {
            const auto denominator = 4 * c.n;
            const auto a = static_cast<unsigned>((c.reflected ? 1 - q[i + 1] : q[i]) * denominator),
                       b = static_cast<unsigned>((c.reflected ? 1 - q[i] : q[i + 1]) * denominator);
            std::vector<double> masses;
            for (std::size_t k = 0; k < 3; ++k)
                masses.push_back(sampled_mass(k, a, b, denominator));
            auto mixture = model->initialize_at_temperature(masses, 300, q[i + 1] - q[i]).mixture;
            input.cells.push_back({mixture, mixture.mass() * std::midpoint(input.velocities_m_per_s[i],
                                                                           input.velocities_m_per_s[i + 1])});
        }
        return bind_material_endcaps(initialize_occupied_staggered(input), limits);
    }
};

// Every double is exported by its IEEE bits; decimal serialization never
// substitutes a nearby value in the independent rational oracle.
class Json {
    struct Level {
        bool first = true, object = false;
    };
    std::vector<Level> levels_;
    bool keyed_ = false;
    void before() {
        if (keyed_) {
            keyed_ = false;
            return;
        }
        if (!levels_.empty()) {
            check(!levels_.back().object, "JSON object requires a key");
            comma();
        }
    }
    void comma() {
        if (!levels_.back().first)
            out << ',';
        levels_.back().first = false;
    }

  public:
    std::ostream &out;
    explicit Json(std::ostream &s) : out(s) {}
    void text(const std::string &s) {
        out << '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\')
                out << '\\' << static_cast<char>(c);
            else if (c == '\n')
                out << "\\n";
            else if (c == '\r')
                out << "\\r";
            else if (c == '\t')
                out << "\\t";
            else {
                check(c >= 32, "Unexpected control character in JSON");
                out << static_cast<char>(c);
            }
        }
        out << '"';
    }
    void key(const char *s) {
        check(!levels_.empty() && levels_.back().object && !keyed_, "Bad JSON key");
        comma();
        text(s);
        out << ':';
        keyed_ = true;
    }
    void value(const std::string &s) {
        before();
        text(s);
    }
    void value(double x) {
        // Preserve even an erroneous nonfinite bit pattern in first-failure
        // evidence. The independent oracle rejects it as a physical value.
        before();
        std::ostringstream bits;
        bits << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(x);
        text(bits.str());
    }
    void value(bool x) {
        before();
        out << (x ? "true" : "false");
    }
    template <class T>
        requires std::is_integral_v<T>
    void value(T x) {
        before();
        out << x;
    }
    void object() {
        before();
        out << '{';
        levels_.push_back({true, true});
    }
    void array() {
        before();
        out << '[';
        levels_.push_back({true, false});
    }
    void end() {
        check(!levels_.empty() && !keyed_, "Bad JSON end");
        out << (levels_.back().object ? '}' : ']');
        levels_.pop_back();
    }
    template <class T> void field(const char *k, const T &x) {
        key(k);
        value(x);
    }
    template <class R> void scalars(const R &r) {
        array();
        for (const auto &x : r)
            value(x);
        end();
    }
};
void identity(Json &j, HybridSourceIdentity id) {
    j.array();
    j.value(id.lineage);
    j.value(id.revision);
    j.value(id.snapshot);
    j.end();
}
void cell_identity(Json &j, const HybridCellIdentity &id) {
    j.array();
    j.value(static_cast<int>(id.kind));
    j.value(id.material.lineage);
    j.value(id.material.ordinal);
    j.value(id.cartesian_index);
    j.end();
}
void grid_json(Json &j, const StaggeredCartesianGrid &g) {
    j.array();
    j.value(g.origin_relative_high_m);
    j.value(g.origin_relative_low_m);
    j.value(g.spacing_m);
    j.value(g.first_cell);
    j.value(g.end_cell);
    j.end();
}
void packet(Json &j, const StaggeredMechanicalPacket &p) {
    j.array();
    j.value(p.mass_kg);
    j.value(p.momentum_kg_m_per_s);
    j.value(p.kinetic_j);
    j.end();
}
void coverage(Json &j, std::span<const HybridCoverageRecord> records) {
    j.array();
    for (const auto &r : records) {
        j.array();
        j.value(static_cast<int>(r.partition));
        j.value(r.cell_slot);
        j.value(r.cartesian_index);
        for (double x : {r.begin_relative_m, r.end_relative_m, r.width_m, r.volume_m3})
            j.value(x);
        j.end();
    }
    j.end();
}
void balances(Json &j, std::span<const HybridCoverageBalance> records) {
    j.array();
    for (const auto &r : records) {
        j.array();
        j.value(static_cast<int>(r.partition));
        j.value(r.cell_slot);
        j.value(r.records);
        for (double x :
             {r.expected_width_m, r.covered_width_m, r.width_residual_m, r.width_arithmetic_bound_m,
              r.expected_volume_m3, r.covered_volume_m3, r.volume_residual_m3, r.volume_arithmetic_bound_m3})
            j.value(x);
        j.end();
    }
    j.end();
}
void state_json(Json &j, const HybridStaggeredState &hybrid) {
    const auto &s = hybrid.canonical();
    j.object();
    j.key("identity");
    identity(j, hybrid.identity());
    j.field("model_metadata", s.model_metadata());
    j.field("model_handle", reinterpret_cast<std::uintptr_t>(s.model().get()));
    j.key("profiles");
    j.array();
    for (const auto &p : s.model()->reference().materials()) {
        j.object();
        j.field("id", p.id);
        j.field("molar", p.molar_mass_kg_per_mol);
        j.field("cv", p.cv_j_per_kg_k);
        j.field("u0", p.energy_offset_j_per_kg);
        j.field("Tmin", p.minimum_temperature_k);
        j.field("Tmax", p.maximum_temperature_k);
        j.field("provenance", p.provenance);
        j.end();
    }
    j.end();
    j.key("frame");
    j.scalars(std::array{s.origin_high_m(), s.origin_low_m(), s.area_m2(), s.gravity_m_per_s2(), s.time_seconds(),
                         s.cumulative_energy_balance_error_j(), s.cumulative_geometry_volume_residual_m3()});
    const auto &l = s.initialization_ledger();
    j.key("initialization");
    j.scalars(std::array{l.prior_bulk_kinetic_energy_j, l.retained_kinetic_energy_j,
                         l.additional_mechanical_energy_j, l.requested_internal_energy_change_j,
                         l.represented_internal_energy_change_j, l.declared_added_energy_j,
                         l.represented_total_energy_change_j, l.kinetic_partition_roundoff_j,
                         l.momentum_reconciliation_kg_m_per_s, l.maximum_momentum_arithmetic_bound_kg_m_per_s});
    j.field("unresolved_initialization", l.unresolved_energy_conversions);
    j.key("limits");
    j.array();
    j.value(hybrid.limits().maximum_material_cells);
    j.value(hybrid.limits().maximum_overlap_records);
    j.value(hybrid.limits().maximum_coverage_records);
    j.end();
    j.key("nodes");
    j.array();
    for (const auto &n : s.nodes()) {
        j.object();
        j.field("q", n.relative_position_m);
        j.field("I", n.inertia_kg);
        j.field("J", n.momentum_kg_m_per_s);
        j.field("u", n.velocity_m_per_s());
        j.field("K", exact_node_k(n));
        j.field("fixed", n.fixed);
        j.end();
    }
    j.end();
    j.key("cells");
    j.array();
    for (const auto &c : s.cells()) {
        j.object();
        j.key("m");
        j.scalars(c.mixture.masses);
        j.field("M", c.mixture.mass());
        j.field("U", c.mixture.energy);
        j.field("V", c.occupied_volume_m3);
        j.field("Pbulk", c.bulk_momentum_kg_m_per_s);
        j.field("variance", c.mechanical_variance_j);
        const auto &t = c.thermodynamics;
        j.field("T", t.temperature_k);
        j.field("p", t.mechanical_pressure_pa);
        j.field("gas_p", t.gas_pressure_pa);
        j.field("S", t.entropy_j_per_k);
        j.field("thermal", t.thermal_energy_j);
        j.field("reference", t.reference_energy_j);
        j.field("elastic", t.elastic_energy_j);
        j.field("cv", t.intrinsic_heat_capacity_j_per_k);
        j.field("eqcv", t.equilibrium_heat_capacity_at_cell_volume_j_per_k);
        j.field("volume_residual", t.volume_residual_m3);
        j.field("energy_residual", t.energy_residual_j);
        j.field("helmholtz", t.helmholtz_energy_j);
        j.key("partial_volumes");
        j.scalars(t.partial_volumes_m3);
        j.key("specific_U");
        j.scalars(t.specific_internal_energies_j_per_kg);
        j.field("gasV", t.gas_volume_m3);
        j.field("liquidV", t.liquid_volume_m3);
        j.field("vacuumV", t.vacuum_volume_m3);
        j.field("hasgas", t.has_gas);
        j.field("hasvacuum", t.has_vacuum);
        j.field("iterations", t.pressure_iterations);
        j.end();
    }
    j.end();
    j.key("ids");
    j.array();
    for (const auto &id : hybrid.source_cell_identities())
        cell_identity(j, id);
    j.end();
    j.key("caps");
    j.array();
    for (const auto &c : hybrid.material_caps()) {
        j.object();
        j.key("id");
        cell_identity(j, c.identity);
        j.field("initial_cell", c.initial_source_cell);
        j.key("m");
        j.scalars(c.original_species_masses_kg);
        j.end();
    }
    j.end();
    j.field("has_grid", hybrid.has_coverage_grid());
    j.key("grid");
    grid_json(j, hybrid.coverage_grid());
    j.key("coverage");
    coverage(j, hybrid.authoritative_coverage());
    j.key("coverage_balances");
    balances(j, hybrid.authoritative_coverage_balances());
    j.end();
}
std::string fingerprint(const HybridStaggeredState &state) {
    std::ostringstream out;
    Json j(out);
    state_json(j, state);
    return out.str();
}
void plan_json(Json &j, const HybridRezonePlan &p) {
    j.object();
    j.key("source");
    identity(j, p.source_identity());
    j.key("grid");
    grid_json(j, p.preferred_grid());
    j.key("q");
    j.scalars(p.node_relative_positions_m());
    j.key("cells");
    j.array();
    for (const auto &c : p.planned_cells()) {
        j.object();
        j.key("id");
        cell_identity(j, c.identity);
        j.field("a", c.begin_relative_m);
        j.field("b", c.end_relative_m);
        j.field("width", c.width_m);
        j.field("V", c.volume_m3);
        j.end();
    }
    j.end();
    j.key("protected");
    j.array();
    for (const auto &c : p.protected_caps()) {
        j.array();
        j.value(c.material.lineage);
        j.value(c.material.ordinal);
        j.value(c.source_cell);
        j.value(c.planned_target_cell);
        j.end();
    }
    j.end();
    j.key("source_coverage");
    coverage(j, p.source_coverage());
    j.key("target_coverage");
    coverage(j, p.planned_target_coverage());
    j.key("source_balances");
    balances(j, p.source_coverage_balances());
    j.key("target_balances");
    balances(j, p.planned_target_coverage_balances());
    const auto &d = p.diagnostics();
    j.key("diagnostics");
    j.object();
    j.field("source_cells", d.source_cells);
    j.field("target_cells", d.planned_target_cells);
    j.field("eligible", d.eligible_grid_lines);
    j.field("primal_overlaps", d.primal_overlap_records);
    j.field("corner_overlaps", d.corner_overlap_records);
    j.field("min_width", d.minimum_planned_width_m);
    j.field("max_width", d.maximum_planned_width_m);
    j.field("grid_roundoff", d.maximum_grid_line_roundoff_m);
    j.field("spacing_residual", d.maximum_grid_spacing_residual_m);
    j.end();
    j.end();
}
void report_json(Json &j, const HybridRezoneReport &h) {
    const auto &r = h.mechanics;
    j.object();
    j.key("before");
    identity(j, h.before);
    j.key("after");
    identity(j, h.after);
    j.field("identity", r.identity);
    j.key("primal");
    j.array();
    for (const auto &p : r.primal_records) {
        j.object();
        j.field("donor", p.donor);
        j.field("target", p.target);
        j.field("length", p.length_m);
        j.field("centroid", p.centroid_relative_m);
        j.field("nonref", p.nonreference_energy_j);
        j.field("potential", p.potential_j);
        j.key("m");
        j.scalars(p.masses_kg);
        j.end();
    }
    j.end();
    j.key("dual");
    j.array();
    for (const auto &p : r.dual_records) {
        j.object();
        j.field("donor", p.donor_corner);
        j.field("target", p.target_corner);
        j.field("length", p.length_m);
        j.key("packet");
        packet(j, p.packet);
        j.end();
    }
    j.end();
    j.key("cells");
    j.array();
    for (const auto &c : r.cells) {
        j.object();
        j.key("rawL");
        packet(j, c.raw_left);
        j.key("rawR");
        packet(j, c.raw_right);
        j.key("normL");
        packet(j, c.normalized_left);
        j.key("normR");
        packet(j, c.normalized_right);
        j.key("L");
        packet(j, c.left);
        j.key("R");
        packet(j, c.right);
        j.field("mass_residual", c.raw_minus_primal_mass_kg);
        j.field("mass_bound", c.quadrature_mass_bound_kg);
        j.field("deltaM", c.normalization_delta_mass_kg);
        j.field("deltaJ", c.normalization_delta_momentum_kg_m_per_s);
        j.field("deltaH", c.normalization_delta_kinetic_j);
        j.field("deltaJ_bound", c.normalization_momentum_bound_kg_m_per_s);
        j.field("deltaH_bound", c.normalization_kinetic_bound_j);
        j.field("lambda", c.lambda);
        j.field("rebalanceM", c.rebalance_transfer_mass_kg);
        j.field("rebalanceJ_residual", c.rebalance_momentum_residual_kg_m_per_s);
        j.field("rebalanceH_residual", c.rebalance_kinetic_residual_j);
        j.field("Q", c.mixing_heat_j);
        j.field("Qarith", c.arithmetic_energy_j);
        j.field("wallQ", c.wall_heat_j);
        j.field("gravity", c.gravity_relocation_j);
        j.field("nonref", c.transported_nonreference_energy_j);
        j.field("Uround", c.stored_u_roundoff_j);
        j.end();
    }
    j.end();
    j.key("nodes");
    j.array();
    for (const auto &n : r.nodes) {
        j.object();
        j.field("I", n.inertia_kg);
        j.field("J", n.provisional_momentum_kg_m_per_s);
        j.field("H", n.provisional_kinetic_j);
        j.field("Htransport", n.transported_kinetic_j);
        j.field("Q", n.mixing_heat_j);
        j.field("Qbound", n.mixing_arithmetic_bound_j);
        j.field("wallJ", n.wall_impulse_kg_m_per_s);
        j.field("wallQ", n.wall_heat_j);
        j.end();
    }
    j.end();
    j.key("mass_residuals");
    j.scalars(r.species_mass_residual_kg);
    j.key("mass_bounds");
    j.scalars(r.species_mass_bound_kg);
    j.field("gatherJ", r.gather_momentum_residual_kg_m_per_s);
    j.field("gatherH", r.gather_kinetic_residual_j);
    j.field("P_residual", r.momentum_residual_kg_m_per_s);
    j.field("P_bound", r.momentum_arithmetic_bound_kg_m_per_s);
    j.field("E_residual", r.energy_residual_j);
    j.field("E_bound", r.energy_arithmetic_bound_j);
    j.field("S_change", r.entropy_change_j_per_k);
    j.field("S_bound", r.entropy_arithmetic_bound_j_per_k);
    j.field("Q", r.mixing_heat_j);
    j.field("Qarith", r.arithmetic_energy_j);
    j.field("gravity", r.gravity_relocation_j);
    j.field("wallJ", r.wall_impulse_kg_m_per_s);
    j.field("wallQ", r.wall_heat_j);
    j.field("V_residual", r.geometry_volume_residual_m3);
    j.field("absV_residual", r.absolute_geometry_volume_residual_m3);
    j.field("unresolved_U", r.unresolved_u_updates);
    j.key("caps");
    j.array();
    for (const auto &c : h.caps) {
        j.object();
        j.field("material_lineage", c.material.lineage);
        j.field("material_ordinal", c.material.ordinal);
        j.field("source", c.source_cell);
        j.field("target", c.target_cell);
        j.field("right", c.right_outer);
        j.key("outer_before");
        packet(j, c.outer_before);
        j.key("outer_after");
        packet(j, c.outer_after);
        j.field("Ubefore", c.internal_energy_before_j);
        j.field("Uafter", c.internal_energy_after_j);
        j.field("Q", c.inner_mixing_heat_j);
        j.end();
    }
    j.end();
    j.key("inserted");
    j.scalars(h.inserted_interior_nodes_m);
    j.key("retired");
    j.scalars(h.retired_interior_nodes_m);
    j.key("old_ids");
    j.array();
    for (const auto &id : h.previous_cells)
        cell_identity(j, id);
    j.end();
    j.key("new_ids");
    j.array();
    for (const auto &id : h.accepted_cells)
        cell_identity(j, id);
    j.end();
    j.end();
}

struct Actual {
    std::array<double, 3> m{};
    double p = 0, k = 0, u = 0, thermal = 0, s = 0, ss = 0;
};
Actual actual(const OccupiedStaggeredState &s) {
    std::array<ExactSum, 3> m;
    ExactSum p, k, u, t, entropy, scale;
    for (const auto &c : s.cells()) {
        for (std::size_t i = 0; i < 3; ++i)
            m[i].add(c.mixture.masses[i]);
        u.add(c.mixture.energy);
        t.add(c.thermodynamics.thermal_energy_j);
        entropy.add(c.thermodynamics.entropy_j_per_k);
        scale.add(std::abs(c.thermodynamics.entropy_j_per_k));
        scale.add(c.thermodynamics.intrinsic_heat_capacity_j_per_k);
    }
    for (const auto &n : s.nodes()) {
        p.add(n.momentum_kg_m_per_s);
        k.add(exact_node_k(n));
    }
    return {{m[0].value(), m[1].value(), m[2].value()},
            p.value(),
            k.value(),
            u.value(),
            t.value(),
            entropy.value(),
            scale.value()};
}
double energy_delta(const OccupiedStaggeredState &a, const OccupiedStaggeredState &b) {
    ExactSum e;
    for (const auto &c : b.cells())
        e.add(c.mixture.energy);
    for (const auto &c : a.cells())
        e.add(-c.mixture.energy);
    for (const auto &n : b.nodes())
        e.add(exact_node_k(n));
    for (const auto &n : a.nodes())
        e.add(-exact_node_k(n));
    return e.value();
}
std::vector<std::int64_t> cart_ids(std::span<const HybridCellIdentity> ids) {
    std::vector<std::int64_t> result;
    for (const auto &i : ids)
        if (i.kind == HybridCellKind::interior_cartesian)
            result.push_back(i.cartesian_index);
    return result;
}
std::vector<std::int64_t> cart_ids(const HybridRezonePlan &p) {
    std::vector<HybridCellIdentity> ids;
    for (const auto &c : p.planned_cells())
        ids.push_back(c.identity);
    return cart_ids(ids);
}
void outer_identity(const HybridStaggeredState &a, const HybridStaggeredState &b) {
    for (bool right : {false, true}) {
        const auto &x = right ? a.canonical().nodes().back() : a.canonical().nodes().front();
        const auto &y = right ? b.canonical().nodes().back() : b.canonical().nodes().front();
        check(same(x.relative_position_m, y.relative_position_m) && same(x.inertia_kg, y.inertia_kg) &&
                  same(x.momentum_kg_m_per_s, y.momentum_kg_m_per_s) &&
                  same(x.velocity_m_per_s(), y.velocity_m_per_s()) && same(exact_node_k(x), exact_node_k(y)) &&
                  x.fixed == y.fixed,
              "Physical outer packet/trace changed");
        const auto &c = right ? a.canonical().cells().back() : a.canonical().cells().front();
        const auto &d = right ? b.canonical().cells().back() : b.canonical().cells().front();
        check(c.mixture.masses == d.mixture.masses && same(c.occupied_volume_m3, d.occupied_volume_m3),
              "Protected cap species/volume changed");
    }
    check(a.material_caps().size() == b.material_caps().size(), "Protected cap count changed");
    for (std::size_t i = 0; i < a.material_caps().size(); ++i)
        check(a.material_caps()[i].identity == b.material_caps()[i].identity, "Protected cap ID changed");
}
void eos(const HybridStaggeredState &initial, const HybridStaggeredState &state, const Config &config) {
    const auto &a = initial.canonical();
    const auto &s = state.canonical();
    check(a.model().get() == s.model().get() && a.model_metadata() == s.model_metadata(),
          "Immutable EOS identity changed");
    check(a.origin_high_m() == s.origin_high_m() && a.origin_low_m() == s.origin_low_m() &&
              s.time_seconds() == 0 && s.gravity_m_per_s2() == 0 && s.area_m2() == 1,
          "Carrier mutated physical time/origin/body/area");
    const auto &profiles = s.model()->reference().materials();
    ExactSum volume;
    const double umin = config.uniform ? velocity(config, 0) : velocity(config, 0), umax = velocity(config, 1);
    for (std::size_t i = 0; i < s.cells().size(); ++i) {
        const auto &c = s.cells()[i];
        volume.add(c.occupied_volume_m3);
        check(same(c.occupied_volume_m3, s.nodes()[i + 1].relative_position_m - s.nodes()[i].relative_position_m),
              "Actual occupied volume/geometry mismatch");
        ExactSum formation, cv, moles, entropy;
        for (std::size_t k = 0; k < 3; ++k) {
            const double m = c.mixture.masses[k];
            check(m > 0, "Positive constituent lost");
            formation.add(m * profiles[k].energy_offset_j_per_kg);
            cv.add(m * profiles[k].cv_j_per_kg_k);
            moles.add(m / profiles[k].molar_mass_kg_per_mol);
        }
        const double t = (c.mixture.energy - formation.value()) / cv.value(),
                     p = gas_constant * t * moles.value() / c.occupied_volume_m3;
        entropy.add(cv.value() * std::log(t / entropy_reference_temperature_k));
        for (std::size_t k = 0; k < 3; ++k) {
            const double n = c.mixture.masses[k] / profiles[k].molar_mass_kg_per_mol;
            entropy.add(n * gas_constant * (std::log(c.occupied_volume_m3) - std::log(n)));
        }
        within(c.thermodynamics.temperature_k, t, 64 * eps * t, "Independent actual caloric EOS");
        within(c.thermodynamics.mechanical_pressure_pa, p, 64 * eps * p, "Independent actual pressure EOS");
        within(c.thermodynamics.entropy_j_per_k, entropy.value(),
               128 * eps * (std::abs(entropy.value()) + cv.value()), "Independent actual mixture entropy");
        const auto recomputed = s.model()->evaluate(c.mixture, c.occupied_volume_m3);
        check(same(recomputed.temperature_k, c.thermodynamics.temperature_k) &&
                  same(recomputed.mechanical_pressure_pa, c.thermodynamics.mechanical_pressure_pa) &&
                  same(recomputed.entropy_j_per_k, c.thermodynamics.entropy_j_per_k),
              "Stale actual EOS cache");
    }
    check(volume.value() == 1, "Actual slab volume coverage changed");
    for (const auto &n : s.nodes())
        within(n.velocity_m_per_s(), std::clamp(n.velocity_m_per_s(), umin, umax),
               128 * eps * std::max(std::abs(umin), std::abs(umax)), "Positive packet velocity range");
}

struct History {
    ExactSum q, qa, round, budget, rebalance, normalM, normalJ, normalH;
    std::array<ExactSum, 2> tv, drift, event_drift, other_drift, event_tv, other_tv, wrong, velocity_bound, capq,
        capround;
    double max_rate = 0, max_width = 0;
    unsigned events = 0, equal = 0;
};
void history_json(Json &j, const History &h) {
    j.object();
    j.field("Q", h.q.value());
    j.field("Qarith", h.qa.value());
    j.field("storedU", h.round.value());
    j.field("Kbound", h.budget.value());
    j.field("rebalance", h.rebalance.value());
    j.field("deltaM", h.normalM.value());
    j.field("deltaJ", h.normalJ.value());
    j.field("deltaH", h.normalH.value());
    const auto pair = [&](const char *name, const auto &a) {
        j.key(name);
        j.array();
        for (const auto &x : a)
            j.value(x.value());
        j.end();
    };
    pair("TV", h.tv);
    pair("drift", h.drift);
    pair("event_drift", h.event_drift);
    pair("other_drift", h.other_drift);
    pair("event_TV", h.event_tv);
    pair("other_TV", h.other_tv);
    pair("wrong", h.wrong);
    pair("velocity_bound", h.velocity_bound);
    pair("capQ", h.capq);
    pair("capUround", h.capround);
    j.field("max_rate", h.max_rate);
    j.field("max_width_over_h", h.max_width);
    j.field("events", h.events);
    j.field("equal_events", h.equal);
    j.end();
}
void config_json(Json &j, const Config &c) {
    j.object();
    j.field("id", c.id());
    j.field("N", c.n);
    j.field("d", c.d);
    j.field("phase", c.phase);
    j.field("carrier", c.carrier);
    j.field("reverse", c.reverse);
    j.field("reflected", c.reflected);
    j.field("uniform", c.uniform);
    j.end();
}
void dump(const std::filesystem::path &path, const Config &c, unsigned step, double tau,
          const HybridStaggeredState &before, const HybridStaggeredState &after,
          const std::optional<HybridRezonePlan> &plan, const std::optional<HybridRezoneReport> &report,
          const History &history, const std::string &error = "") {
    std::ofstream out(path);
    check(static_cast<bool>(out), "Cannot write full nonlinear event/failure evidence");
    Json j(out);
    j.object();
    j.field("schema", 1);
    j.key("config");
    config_json(j, c);
    j.field("step", step);
    j.field("tau", tau);
    j.field("error", error);
    j.key("before");
    state_json(j, before);
    j.key("after");
    state_json(j, after);
    if (plan) {
        j.key("plan");
        plan_json(j, *plan);
    }
    if (report) {
        j.key("report");
        report_json(j, *report);
    }
    j.key("history");
    history_json(j, history);
    j.end();
    out << '\n';
}
void snapshot(const std::filesystem::path &path, const Config &c, unsigned step, double tau,
              const HybridStaggeredState &state, const History &history,
              const HybridStaggeredState *before = nullptr, const HybridRezonePlan *plan = nullptr,
              const HybridRezoneReport *report = nullptr) {
    std::ofstream out(path);
    check(static_cast<bool>(out), "Cannot write full nonlinear profile");
    Json j(out);
    j.object();
    j.field("schema", 1);
    j.key("config");
    config_json(j, c);
    j.field("step", step);
    j.field("tau", tau);
    j.key("state");
    state_json(j, state);
    j.key("history");
    history_json(j, history);
    if (before) {
        j.key("before");
        state_json(j, *before);
    }
    if (plan) {
        j.key("plan");
        plan_json(j, *plan);
    }
    if (report) {
        j.key("report");
        report_json(j, *report);
    }
    j.end();
    out << '\n';
}

void run_case(const Setup &setup, const Config &c, const std::filesystem::path &root) {
    const auto directory = root / c.id();
    std::filesystem::create_directory(directory);
    std::cout << "BEGIN_NONLINEAR " << c.id() << '\n' << std::flush;
    auto state = setup.initialize(c);
    const auto initial = state;
    const auto start = actual(initial.canonical());
    History h;
    for (const auto &cell : initial.canonical().cells())
        h.max_width = std::max(h.max_width, cell.occupied_volume_m3 * c.n);
    double initial_min = initial.canonical().nodes().front().velocity_m_per_s(), initial_max = initial_min;
    for (const auto &node : initial.canonical().nodes()) {
        initial_min = std::min(initial_min, node.velocity_m_per_s());
        initial_max = std::max(initial_max, node.velocity_m_per_s());
    }
    const double initial_range = initial_max - initial_min;
    snapshot(directory / "sample-0.json", c, 0, 0, state, h);
    const unsigned steps = c.n * c.d / 8;
    check(steps * 8 == c.n * c.d && steps % 4 == 0, "Carrier sample times are not exact steps");
    const double dt = duration / steps;
    const auto first = plan_hybrid_staggered_rezone(
        state, {phi(c, 0), 0, 1.0 / c.n, -static_cast<std::int64_t>(c.n), 2 * static_cast<std::int64_t>(c.n)});
    check(first.node_relative_positions_m().size() == state.canonical().nodes().size(),
          "Independent initial material partition differs from initial grid plan");
    for (std::size_t i = 0; i < first.node_relative_positions_m().size(); ++i)
        check(same(first.node_relative_positions_m()[i], state.canonical().nodes()[i].relative_position_m),
              "Initial material nodes do not match read-only plan");
    auto old_ids = cart_ids(first);
    std::array<bool, 4> emitted{};
    std::ofstream perstep(directory / "steps.jsonl");
    check(static_cast<bool>(perstep), "Cannot write per-call actual ledgers");
    for (unsigned step = 1; step <= steps; ++step) {
        const auto before = state;
        const auto old = actual(before.canonical());
        const double tau = step * dt;
        std::optional<HybridRezonePlan> plan;
        std::optional<HybridRezoneReport> report;
        try {
            plan = plan_hybrid_staggered_rezone(state, {phi(c, tau), 0, 1.0 / c.n, -static_cast<std::int64_t>(c.n),
                                                        2 * static_cast<std::int64_t>(c.n)});
            const auto new_ids = cart_ids(*plan);
            const bool count = plan->planned_cells().size() != before.canonical().cells().size(),
                       event = count || new_ids != old_ids, equal = event && !count;
            report = rezone_hybrid_staggered_into(state, *plan);
            const auto &r = report->mechanics;
            const auto now = actual(state.canonical());
            check(state.identity().lineage == initial.identity().lineage && state.identity().revision == step &&
                      state.identity().snapshot != before.identity().snapshot,
                  "Accepted history rebound/reset/reused snapshot");
            outer_identity(initial, state);
            eos(initial, state, c);
            check(cart_ids(state.source_cell_identities()) == new_ids,
                  "Accepted event identity differs from plan");
            check(state.canonical().initialization_ledger().declared_added_energy_j ==
                      initial.canonical().initialization_ledger().declared_added_energy_j,
                  "Initializer conversion ledger changed");
            for (std::size_t s = 0; s < 3; ++s) {
                ExactSum delta;
                for (const auto &x : state.canonical().cells())
                    delta.add(x.mixture.masses[s]);
                for (const auto &x : before.canonical().cells())
                    delta.add(-x.mixture.masses[s]);
                within(delta.value(), 0, r.species_mass_bound_kg[s],
                       "Independent actual per-call constituent balance");
                within(now.m[s], start.m[s], 128 * eps * start.m[s],
                       "Independent cumulative own-species mass balance");
            }
            ExactSum dp;
            for (const auto &n : state.canonical().nodes())
                dp.add(n.momentum_kg_m_per_s);
            for (const auto &n : before.canonical().nodes())
                dp.add(-n.momentum_kg_m_per_s);
            within(dp.value(), 0, r.momentum_arithmetic_bound_kg_m_per_s, "Independent actual per-call momentum");
            within(now.p, start.p, 128 * eps * std::abs(start.p), "Independent cumulative canonical momentum");
            const double de = energy_delta(before.canonical(), state.canonical()),
                         totalde = energy_delta(initial.canonical(), state.canonical());
            within(de, 0, r.energy_arithmetic_bound_j, "Independent actual per-call U+K+PE");
            within(de, r.energy_residual_j, 8 * eps * (old.k + now.k), "Actual canonical energy/report agreement");
            within(totalde, 0, 256 * eps * (start.thermal + now.thermal + start.k + now.k),
                   "Independent cumulative actual energy");
            check(now.s - start.s >= -128 * eps * (start.ss + now.ss), "Independent cumulative entropy loss");
            check(r.gravity_relocation_j == 0 && r.wall_heat_j == 0 && r.wall_impulse_kg_m_per_s == 0,
                  "Passive carrier introduced external work/impulse");
            within(state.canonical().cumulative_energy_balance_error_j() -
                       before.canonical().cumulative_energy_balance_error_j(),
                   r.energy_residual_j,
                   2 * eps *
                       (std::abs(state.canonical().cumulative_energy_balance_error_j()) +
                        std::abs(before.canonical().cumulative_energy_balance_error_j()) +
                        std::abs(r.energy_residual_j)),
                   "Signed energy arithmetic ledger increment");
            within(state.canonical().cumulative_geometry_volume_residual_m3() -
                       before.canonical().cumulative_geometry_volume_residual_m3(),
                   r.geometry_volume_residual_m3,
                   2 * eps *
                       (std::abs(state.canonical().cumulative_geometry_volume_residual_m3()) +
                        std::abs(before.canonical().cumulative_geometry_volume_residual_m3()) +
                        std::abs(r.geometry_volume_residual_m3)),
                   "Signed geometry arithmetic ledger increment");
            h.q.add(r.mixing_heat_j);
            h.qa.add(r.arithmetic_energy_j);
            h.budget.add(64 * eps * old.k);
            h.budget.add(64 * eps * now.k);
            h.budget.add(std::abs(r.gather_kinetic_residual_j));
            for (const auto &cell : r.cells) {
                h.round.add(cell.stored_u_roundoff_j);
                h.budget.add(std::abs(cell.rebalance_kinetic_residual_j));
                h.rebalance.add(cell.rebalance_transfer_mass_kg);
                h.normalM.add(cell.normalization_delta_mass_kg);
                h.normalJ.add(cell.normalization_delta_momentum_kg_m_per_s);
                h.normalH.add(cell.normalization_delta_kinetic_j);
            }
            within(h.q.value() + h.qa.value() - (start.k - now.k), 0, h.budget.value(),
                   "Independent cumulative requested heat/canonical kinetic loss");
            const double totalmass = start.m[0] + start.m[1] + start.m[2],
                         minimumk = .5 * start.p * (start.p / totalmass);
            check(h.q.value() <= start.k - minimumk + std::abs(h.qa.value()) + h.budget.value(),
                  "Compatible heat exceeds available initial mechanical variance");
            const auto totals = state.canonical().totals();
            within(totals.bulk_kinetic_energy_j + totals.mechanical_variance_j, now.k, 64 * eps * now.k,
                   "Canonical nodal K vs bulk plus variance");
            std::array<double, 2> inner_u{}, inner_old{}, inner_du{}, inner_arith{};
            for (std::size_t side = 0; side < 2; ++side) {
                const std::size_t bj = side == 0 ? 1 : before.canonical().cells().size() - 1,
                                  j = side == 0 ? 1 : state.canonical().cells().size() - 1;
                const double uold = before.canonical().nodes()[bj].velocity_m_per_s(),
                             unew = state.canonical().nodes()[j].velocity_m_per_s(), du = unew - uold;
                h.tv[side].add(std::abs(du));
                h.drift[side].add(du);
                (event ? h.event_drift[side] : h.other_drift[side]).add(du);
                (event ? h.event_tv[side] : h.other_tv[side]).add(std::abs(du));
                h.wrong[side].add(std::max(0.0, side == 0 ? -du : du));
                const auto &nr = r.nodes[j];
                const double local = 64 * eps *
                                     (std::abs(nr.provisional_momentum_kg_m_per_s) / nr.inertia_kg +
                                      std::abs(unew) + std::abs(uold));
                inner_u[side] = unew;
                inner_old[side] = uold;
                inner_du[side] = du;
                inner_arith[side] = local;
                h.velocity_bound[side].add(local);
                h.max_rate = std::max(h.max_rate, std::abs(du) / dt);
                const auto &cr = side == 0 ? r.cells.front() : r.cells.back();
                h.capq[side].add(cr.mixing_heat_j);
                h.capround[side].add(cr.stored_u_roundoff_j);
                check(h.tv[side].value() <= initial_range + h.velocity_bound[side].value(),
                      "Inner total variation exceeds initial velocity range plus local arithmetic");
                check(h.wrong[side].value() <= h.velocity_bound[side].value(),
                      "Inner wrong-way variation exceeds propagated velocity arithmetic");
                const auto &c0 =
                    side == 0 ? initial.canonical().cells().front() : initial.canonical().cells().back();
                const auto &c1 = side == 0 ? state.canonical().cells().front() : state.canonical().cells().back();
                ExactSum cap;
                cap.add(c1.mixture.energy);
                cap.add(-c0.mixture.energy);
                cap.add(-h.capq[side].value());
                cap.add(-h.capround[side].value());
                within(cap.value(), 0,
                       8 * eps * (std::abs(h.capq[side].value()) + std::abs(h.capround[side].value())),
                       "Actual cap U vs compatible cap heat and stored-U roundoff");
            }
            for (const auto &cell : state.canonical().cells())
                h.max_width = std::max(h.max_width, cell.occupied_volume_m3 * c.n);
            if (event)
                ++h.events;
            if (equal)
                ++h.equal;
            if (c.uniform) {
                for (const auto &n : state.canonical().nodes())
                    within(n.velocity_m_per_s(), velocity(c, 0), 128 * eps * std::abs(velocity(c, 0)),
                           "Uniform velocity fixed call-count-independent bound");
                check(h.q.value() <= h.budget.value(), "Uniform velocity produced nonarithmetic heat");
            }
            std::array<bool, 4> select{state.canonical().cells().size() > before.canonical().cells().size(),
                                       state.canonical().cells().size() < before.canonical().cells().size(), equal,
                                       c.reverse && tau > duration / 2 && event};
            for (std::size_t k = 0; k < select.size(); ++k)
                if (select[k] && !emitted[k]) {
                    dump(directory / ("event-" + std::to_string(k) + ".json"), c, step, tau, before, state, plan,
                         report, h);
                    emitted[k] = true;
                }
            Json j(perstep);
            j.object();
            j.field("step", step);
            j.field("tau", tau);
            j.field("phi", phi(c, tau));
            j.key("old_ids");
            j.scalars(old_ids);
            j.key("new_ids");
            j.scalars(new_ids);
            j.field("old_cells", before.canonical().cells().size());
            j.field("new_cells", state.canonical().cells().size());
            j.field("event", event);
            j.field("equal", equal);
            j.key("m");
            j.scalars(now.m);
            j.field("P", now.p);
            j.field("K", now.k);
            j.field("U", now.u);
            j.field("thermal", now.thermal);
            j.field("S", now.s);
            j.field("entropy_scale", now.ss);
            j.field("deltaE", de);
            j.field("total_deltaE", totalde);
            j.key("inner_u");
            j.scalars(inner_u);
            j.key("inner_old");
            j.scalars(inner_old);
            j.key("inner_du");
            j.scalars(inner_du);
            j.key("inner_arith");
            j.scalars(inner_arith);
            j.field("Q", r.mixing_heat_j);
            j.field("Qarith", r.arithmetic_energy_j);
            j.field("Eres", r.energy_residual_j);
            j.field("Vres", r.geometry_volume_residual_m3);
            j.field("energy_ledger", state.canonical().cumulative_energy_balance_error_j());
            j.field("geometry_ledger", state.canonical().cumulative_geometry_volume_residual_m3());
            j.key("history");
            history_json(j, h);
            j.end();
            perstep << '\n';
            old_ids = new_ids;
            if (step % (steps / 4) == 0)
                snapshot(directory / ("sample-" + std::to_string(step / (steps / 4)) + ".json"), c, step, tau,
                         state, h, &before, &*plan, &*report);
        } catch (const std::exception &e) {
            dump(root / "FIRST_FAILURE.json", c, step, tau, before, state, plan, report, h, e.what());
            perstep.flush();
            throw;
        }
    }
    check(h.rebalance.value() > 0, "Variable density failed to exercise actual material half-mass rebalance");
    std::cout << "PASS_NATIVE_NONLINEAR " << c.id() << " events=" << h.events << " equal=" << h.equal
              << " physical_outer_rate=0 physical_outer_TV=0\n"
              << std::flush;
}
void rollback(const Setup &setup, const std::filesystem::path &root) {
    const Config c{8, 4, 2, 1, false, false, false};
    HybridGeometryLimits limits;
    limits.maximum_material_cells = 7;
    auto state = setup.initialize(c, limits);
    const auto previous = plan_hybrid_staggered_rezone(state, {phi(c, 1.0 / 32), 0, .125, -8, 16});
    const auto report = rezone_hybrid_staggered_into(state, previous);
    (void)report;
    check(state.canonical().cells().size() == 7 && state.identity().revision == 1,
          "Rollback fixture failed genuine accepted seven-cell first step");
    const auto exact = fingerprint(state);
    fatal(FluidFatalCode::unsupported_configuration, [&] { (void)rezone_hybrid_staggered_into(state, previous); });
    check(fingerprint(state) == exact, "Stale-plan rejection mutated full state");
    fatal(FluidFatalCode::unsupported_configuration, [&] {
        (void)plan_hybrid_staggered_rezone(state, {phi(c, 1.0 / 16), 0, .125, -8, 16});
    });
    check(fingerprint(state) == exact, "Capacity insertion failure mutated full canonical/cache/ownership state");
    std::ofstream out(root / "rollback.json");
    Json j(out);
    j.object();
    j.field("stale_rejected", true);
    j.field("eight_cell_insertion_rejected", true);
    j.key("accepted");
    state_json(j, state);
    j.end();
    out << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 1 || (argc == 3 && std::string(argv[1]) == "--output"),
              "Usage: test [--output fresh-directory]");
        const std::filesystem::path output = argc == 3 ? argv[2] : "hybrid-nonlinear-profiles";
        // CTest may rerun in one build; preserve each run in a fresh numbered
        // child rather than overwrite a former failure or accept stale data.
        std::filesystem::create_directories(output);
        std::size_t run = 1;
        while (std::filesystem::exists(output / ("run-" + std::to_string(run))))
            ++run;
        const auto root = output / ("run-" + std::to_string(run));
        std::filesystem::create_directory(root);
        std::cout << "NONLINEAR_OUTPUT " << std::filesystem::absolute(root).string() << '\n' << std::flush;
        Setup setup;
        rollback(setup, root);
        std::size_t count = 0;
        for (bool reflected : {false, true})
            for (bool reverse : {false, true})
                for (int carrier : {-1, 1})
                    for (unsigned phase = 0; phase < 4; ++phase) {
                        std::vector<std::pair<unsigned, unsigned>> configs{
                            {16, 4}, {32, 4}, {64, 4}, {32, 2}, {32, 8}, {32, 16}, {32, 64}, {32, 256}, {64, 16}};
                        for (const auto &[n, d] : configs) {
                            run_case(setup, {n, d, phase, carrier, reverse, reflected, false}, root);
                            ++count;
                        }
                    }
        for (bool reflected : {false, true})
            for (int carrier : {-1, 1})
                for (unsigned phase = 0; phase < 4; ++phase) {
                    run_case(setup, {32, 256, phase, carrier, false, reflected, true}, root);
                    ++count;
                }
        check(count == 304, "Incomplete predeclared nonlinear matrix");
        std::ofstream complete(root / "COMPLETE.json");
        complete << "{\"schema\":1,\"cases\":304,\"nonlinear\":288,\"uniform\":16,\"native_physical_checks\":true,"
                    "\"independent_oracle_required\":true,\"whole_A2\":false}\n";
        std::cout << "NONLINEAR_PASSIVE cases=304; native physical/identity checks passed; independent nonlinear "
                     "oracle required; all-node-rate/whole-A2/pressure/front unvalidated\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL nonlinear passive fatal without retry: " << e.what() << '\n';
        return 2;
    }
}
