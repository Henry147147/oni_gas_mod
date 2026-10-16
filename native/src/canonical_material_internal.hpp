#pragma once
#include "mixed_atmospheres/experimental/canonical_material.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace ma::experimental::canonical_detail {
using Vec = CanonicalVector;
using Matrix = CanonicalMatrix;
using Data = CanonicalSnapshot;
using Field = CanonicalWitnessField;
using Stage = CanonicalStage;
using Code = CanonicalFatalCode;
inline constexpr double epsilon = std::numeric_limits<double>::epsilon();

// One bounded working frame, copied rather than borrowing operands. Successful
// arithmetic upserts scratch entries; only an actual failure emits/seals the
// external witness. Sixteen scalar slots are reserved for environment/known
// per-node context, including both endpoints of an owner failure.
struct FailureFrame {
    std::array<CanonicalWitnessScalar, 232> scalars{};
    std::array<CanonicalWitnessRow, 8> rows{};
    std::size_t scalar_count = 0, row_count = 0;
    bool overflow = false;
    void clear() noexcept { scalar_count = 0; row_count = 0; overflow = false; }
    void erase(Field field, std::uint32_t index = canonical_absent_index) noexcept {
        for (std::size_t i = 0; i < scalar_count; ++i) if (scalars[i].field == field && scalars[i].index == index) {
            for (std::size_t j = i + 1; j < scalar_count; ++j) scalars[j - 1] = scalars[j];
            --scalar_count; return;
        }
    }
    void scalar(Field field, double value, std::uint32_t index) noexcept {
        for (std::size_t i = 0; i < scalar_count; ++i) if (scalars[i].field == field && scalars[i].index == index) {
            scalars[i].number = CanonicalDiagnosticNumber::capture(value); return;
        }
        if (scalar_count == scalars.size()) { overflow = true; return; }
        scalars[scalar_count++] = {field, index, CanonicalDiagnosticNumber::capture(value)};
    }
    void row(Field field, std::span<const double> values, std::uint32_t index) noexcept {
        if (values.size() > 33) { overflow = true; return; }
        std::size_t selected = row_count;
        for (std::size_t i = 0; i < row_count; ++i) if (rows[i].field == field && rows[i].index == index) { selected = i; break; }
        if (selected == rows.size()) { overflow = true; return; }
        if (selected == row_count) ++row_count;
        auto& target = rows[selected]; target.field = field; target.index = index; target.length = values.size();
        for (std::size_t i = 0; i < values.size(); ++i) target.numbers[i] = CanonicalDiagnosticNumber::capture(values[i]);
    }
};

class Kernel {
public:
    Kernel(CanonicalBudgetContext& budget, CanonicalFailureWitness& witness, bool initializing,
        std::string_view identity, std::uint64_t completed_step)
        : budget_(budget), witness_(witness), initializing_(initializing), before_(budget.counts_) {
        if (witness_.failed()) canonical_fail(witness_, witness_.code(), witness_.reason());
        if (!witness_.begin_stage(Stage::preflight) || !witness_.set_identity(identity, completed_step))
            canonical_fail(witness_, witness_.code(), witness_.reason());
        require_canonical_arithmetic_environment(witness_);
        scope(Stage::preflight);
    }
    void scope(Stage stage, std::uint32_t node = canonical_absent_index, std::uint32_t owner = canonical_absent_index) noexcept {
        stage_ = stage; node_ = node; owner_ = owner; frame.clear();
    }
    void row_scope(std::uint32_t node) noexcept { node_ = node; owner_ = canonical_absent_index; frame.clear(); }
    void step_context(double seconds, double completed_time) noexcept {
        seconds_ = CanonicalDiagnosticNumber::capture(seconds); time_ = CanonicalDiagnosticNumber::capture(completed_time);
    }
    void owner_nodes(const std::array<std::size_t, 7>& left, const std::array<std::size_t, 7>& right) noexcept {
        owner_left_ = left; owner_right_ = right; owner_nodes_known_ = true;
    }
    void baseline(Field field, std::span<const double> values) {
        const auto selected = baseline_index(field);
        require(selected < baselines_.size() && values.size() <= 33, Code::unsupported_configuration, "Invalid canonical baseline context");
        baselines_[selected] = {};
        for (std::size_t i = 0; i < values.size(); ++i) baselines_[selected][i] = CanonicalDiagnosticNumber::capture(values[i]);
    }
    void baseline_value(Field field, std::size_t index, double value) {
        const auto selected = baseline_index(field);
        require(selected < baselines_.size() && index < 33, Code::unsupported_configuration, "Invalid canonical baseline node");
        baselines_[selected][index] = CanonicalDiagnosticNumber::capture(value);
    }
    void scalar(Field field, double value, std::uint32_t index = canonical_absent_index) noexcept { frame.scalar(field, value, index); }
    void row(Field field, std::span<const double> values, std::uint32_t index = canonical_absent_index) noexcept { frame.row(field, values, index); }
    [[noreturn]] void relay_arithmetic_failure(const CanonicalFailureWitness& child) {
        for (const auto& value : child.scalars())
            scalar(value.field, std::bit_cast<double>(value.number.bits), value.index);
        for (const auto& value : child.rows()) {
            std::array<double, 33> copied{};
            for (std::size_t i = 0; i < value.length; ++i) copied[i] = std::bit_cast<double>(value.numbers[i].bits);
            row(value.field, std::span<const double>(copied.data(), value.length), value.index);
        }
        if (!child.failed() || !child.capture_valid()) frame.overflow = true;
        fail(child.code(), child.reason());
    }
    [[noreturn]] void fail(Code code, std::string_view reason) {
        if (witness_.failed()) canonical_fail(witness_, witness_.code(), witness_.reason());
        if (!witness_.begin_stage(stage_)) canonical_fail(witness_, witness_.code(), witness_.reason());
        CanonicalWitnessContext context;
        context.node = node_; context.owner = owner_;
        context.newton_updates = static_cast<std::uint32_t>(newton_updates);
        context.backtracks = static_cast<std::uint32_t>(backtracks);
        context.factorizations = static_cast<std::uint32_t>(factorizations);
        context.top_level_queries = budget_.counts_.top_level_queries;
        context.constitutive_charges = budget_.counts_.constitutive_charges;
        context.oracle_iterations = budget_.counts_.oracle_iterations;
        context.predicate_sequence = budget_.counts_.predicates;
        bool captured = witness_.set_context(context);
        const auto environment = read_canonical_arithmetic_environment();
        captured = captured && witness_.add_scalar(Field::rounding_mode, environment.rounding_mode) &&
            witness_.add_scalar(Field::mxcsr, environment.mxcsr) &&
            witness_.add_scalar(Field::binary64_supported, environment.binary64_supported ? 1 : 0) &&
            witness_.add_scalar(Field::x86_supported, environment.x86_supported ? 1 : 0);
        const auto context_scalar = [&](Field field, const CanonicalDiagnosticNumber& value, std::uint32_t index) {
            if (!captured || !value.has(CanonicalDiagnosticNumber::present)) return;
            for (std::size_t i = 0; i < frame.scalar_count; ++i)
                if (frame.scalars[i].field == field && (frame.scalars[i].index == index || frame.scalars[i].index == canonical_absent_index)) return;
            captured = witness_.add_scalar(field, std::bit_cast<double>(value.bits), index);
        };
        context_scalar(Field::timestep, seconds_, canonical_absent_index);
        context_scalar(Field::completed_time, time_, canonical_absent_index);
        const auto context_node = [&](std::size_t index) {
            if (index >= 33) return;
            constexpr std::array<Field, 4> fields{Field::pi0, Field::pi1, Field::velocity0, Field::velocity1};
            for (std::size_t i = 0; i < fields.size(); ++i) context_scalar(fields[i], baselines_[i][index], static_cast<std::uint32_t>(index));
        };
        if (node_ != canonical_absent_index) context_node(node_);
        else if (owner_nodes_known_ && owner_ < 7) {
            context_node(owner_left_[owner_]);
            if (owner_right_[owner_] != owner_left_[owner_]) context_node(owner_right_[owner_]);
        }
        for (std::size_t i = 0; captured && i < frame.scalar_count; ++i) {
            const auto& item = frame.scalars[i];
            captured = witness_.add_scalar(item.field, std::bit_cast<double>(item.number.bits), item.index);
        }
        for (std::size_t i = 0; captured && i < frame.row_count; ++i) {
            const auto& item = frame.rows[i]; std::array<double, 33> values{};
            for (std::size_t j = 0; j < item.length; ++j) values[j] = std::bit_cast<double>(item.numbers[j].bits);
            captured = witness_.add_row(item.field, std::span<const double>(values.data(), item.length), item.index);
        }
        if (!captured) canonical_fail(witness_, witness_.code(), witness_.reason());
        if (frame.overflow) witness_.reject_capture("Canonical producer failure-frame capacity exhausted");
        canonical_fail(witness_, code, reason);
    }
    void require(bool condition, Code code, std::string_view reason) {
        if (budget_.counts_.predicates == std::numeric_limits<std::uint64_t>::max()) fail(Code::unsupported_configuration, "Predicate sequence exhausted");
        ++budget_.counts_.predicates;
        if (!condition || frame.overflow) fail(code, reason);
    }
    double finite(double value, Field field = Field::arithmetic_result) {
        const auto bits = CanonicalDiagnosticNumber::capture(value);
        if (!bits.has(CanonicalDiagnosticNumber::finite) || bits.has(CanonicalDiagnosticNumber::subnormal)) {
            scalar(field, value); require(false, Code::numerical_precision, "Nonfinite or subnormal canonical arithmetic");
        }
        return value;
    }
    double positive(double value, Field field) {
        scalar(field, value); finite(value, field);
        require(value > 0, Code::unsupported_configuration, "Expected a positive canonical quantity"); return value;
    }
    double nonnegative(double value, Field field) {
        scalar(field, value); finite(value, field);
        require(value >= 0, Code::unsupported_configuration, "Expected a nonnegative canonical quantity"); return value;
    }
    double add(double a, double b) {
        prepare_operands(a, b);
        finite(a, Field::operand_left); finite(b, Field::operand_right);
        const double value = a + b;
        const auto raw = CanonicalDiagnosticNumber::capture(value);
        if (!raw.has(CanonicalDiagnosticNumber::finite) || raw.has(CanonicalDiagnosticNumber::subnormal)) {
            operands(a, b, value); require(false, Code::numerical_precision, "Canonical addition range failure");
        }
        return value;
    }
    double sub(double a, double b) { finite(b, Field::operand_right); return add(a, -b); }
    double mul(double a, double b) {
        prepare_operands(a, b);
        finite(a, Field::operand_left); finite(b, Field::operand_right);
        const double value = a * b;
        const auto raw = CanonicalDiagnosticNumber::capture(value);
        if (!raw.has(CanonicalDiagnosticNumber::finite) || raw.has(CanonicalDiagnosticNumber::subnormal) ||
            (a != 0 && b != 0 && raw.has(CanonicalDiagnosticNumber::zero))) {
            operands(a, b, value); require(false, Code::numerical_precision, "Canonical nonzero product is unrepresentable");
        }
        return value;
    }
    double div(double a, double b) {
        prepare_operands(a, b);
        finite(a, Field::operand_left); finite(b, Field::operand_right);
        if (b == 0) require(false, Code::numerical_precision, "Canonical division by zero; no result was evaluated");
        const double value = a / b;
        const auto raw = CanonicalDiagnosticNumber::capture(value);
        if (!raw.has(CanonicalDiagnosticNumber::finite) || raw.has(CanonicalDiagnosticNumber::subnormal) ||
            (a != 0 && raw.has(CanonicalDiagnosticNumber::zero))) {
            operands(a, b, value); require(false, Code::numerical_precision, "Canonical nonzero quotient is unrepresentable");
        }
        return value;
    }
    double sqrt(double value) {
        frame.erase(Field::arithmetic_result); scalar(Field::operand_left, value);
        finite(value, Field::operand_left); require(value > 0, Code::numerical_precision, "Nonpositive canonical square-root argument");
        return finite(std::sqrt(value));
    }
    void represented(double requested, double actual, std::string_view reason,
        Field requested_field = Field::requested_impulse, Field actual_field = Field::represented_impulse) {
        scalar(requested_field, requested); scalar(actual_field, actual);
        finite(requested); finite(actual);
        require(requested == 0 || (actual != 0 && std::signbit(requested) == std::signbit(actual)), Code::numerical_precision, reason);
    }
    double ratio(double error, double scale) {
        finite(error); finite(scale); require(scale >= 0, Code::numerical_precision, "Negative canonical comparison scale");
        if (scale == 0) { require(error == 0, Code::numerical_precision, "Nonzero canonical defect has zero scale"); return 0; }
        return div(std::abs(error), scale);
    }
    void within(double error, double scale, double tolerance, std::string_view reason) {
        frame.erase(Field::arithmetic_bound);
        scalar(Field::arithmetic_result, error); scalar(Field::row_scale, scale);
        const double bound = mul(tolerance, scale); scalar(Field::arithmetic_bound, bound);
        finite(error); require(scale >= 0 && std::abs(error) <= bound, Code::numerical_precision, reason);
    }
    void deadline() {
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - budget_.start_).count();
        require(elapsed <= 30, Code::numerical_precision, "Canonical invocation wall budget exhausted");
    }
    void charge(std::size_t amount) {
        deadline(); scalar(Field::constitutive_charge, static_cast<double>(amount));
        require(amount != 0 && amount <= 32768 && budget_.counts_.constitutive_charges <= 32768 - amount,
            Code::numerical_precision, "Canonical constitutive-entry budget exhausted");
        budget_.counts_.constitutive_charges += amount; ++budget_.counts_.top_level_queries;
    }
    void oracle_tick() {
        deadline(); require(budget_.counts_.oracle_iterations < 500000, Code::numerical_precision, "Canonical independent-oracle budget exhausted");
        ++budget_.counts_.oracle_iterations;
    }
    void factorization() {
        require(factorizations < (initializing_ ? 1U : 42U), Code::numerical_precision, "Canonical factorization budget exhausted");
        ++factorizations; ++budget_.counts_.factorizations;
    }
    void newton() {
        require(newton_updates < 40, Code::numerical_precision, "Canonical Newton-update budget exhausted");
        ++newton_updates; ++budget_.counts_.newton_updates;
    }
    void backtrack() { ++backtracks; ++budget_.counts_.backtracks; deadline(); }
    [[nodiscard]] std::size_t charged_here() const noexcept { return budget_.counts_.constitutive_charges - before_.constitutive_charges; }
    [[nodiscard]] CanonicalResourceCounts counts() const noexcept { return budget_.counts_; }
    FailureFrame frame;
    std::size_t newton_updates = 0, backtracks = 0, factorizations = 0;
private:
    static std::size_t baseline_index(Field field) noexcept {
        switch (field) {
        case Field::pi0: return 0;
        case Field::pi1: return 1;
        case Field::velocity0: return 2;
        case Field::velocity1: return 3;
        default: return 4;
        }
    }
    void prepare_operands(double a, double b) noexcept {
        frame.erase(Field::arithmetic_result); scalar(Field::operand_left, a); scalar(Field::operand_right, b);
    }
    void operands(double a, double b, double result) noexcept {
        scalar(Field::operand_left, a); scalar(Field::operand_right, b); scalar(Field::arithmetic_result, result);
    }
    CanonicalBudgetContext& budget_;
    CanonicalFailureWitness& witness_;
    bool initializing_;
    CanonicalResourceCounts before_;
    Stage stage_ = Stage::preflight;
    std::uint32_t node_ = canonical_absent_index, owner_ = canonical_absent_index;
    CanonicalDiagnosticNumber seconds_, time_;
    std::array<std::array<CanonicalDiagnosticNumber, 33>, 4> baselines_{};
    std::array<std::size_t, 7> owner_left_{}, owner_right_{};
    bool owner_nodes_known_ = false;
};

struct Sum {
    explicit Sum(Kernel& kernel, std::uint32_t role = 0) : k(kernel), role_(role) { record(); }
    void add(double value) {
        record();
        k.finite(value); const double next = k.add(head, value);
        const double error = std::abs(head) >= std::abs(value) ? k.add(k.sub(head, next), value) : k.add(k.sub(value, next), head);
        correction = k.add(correction, error); head = next;
        record();
    }
    [[nodiscard]] double get() { record(); return k.add(head, correction); }
    void record() const { k.scalar(Field::sum_head, head, role_); k.scalar(Field::sum_correction, correction, role_); }
    Kernel& k;
    double head = 0, correction = 0;
    std::uint32_t role_;
};
struct Accumulated {
    double head = 0, correction = 0;
    void add(Kernel& k, double value, std::uint32_t role) {
        Sum total(k, role); total.head = head; total.correction = correction;
        total.add(value); head = total.head; correction = total.correction;
    }
    double get(Kernel& k, std::uint32_t role) const {
        Sum total(k, role); total.head = head; total.correction = correction; return total.get();
    }
};
inline double sum(Kernel& k, std::initializer_list<double> values) { Sum result(k); for (double value : values) result.add(value); return result.get(); }
inline double difference(Kernel& k, double a, double b) { return sum(k, {a, -k.finite(b)}); }
inline Matrix zeros(std::size_t n) { return Matrix(n, Vec(n, 0)); }
inline bool constrained(const Data& d, std::size_t node) {
    return (node == 0 && d.left.kind != CanonicalBoundaryKind::free) ||
        (node + 1 == d.positions_m.size() && d.right.kind != CanonicalBoundaryKind::free);
}
inline double boundary_velocity(const Data& d, std::size_t node) { return node == 0 ? d.left.velocity_m_per_s : d.right.velocity_m_per_s; }

[[nodiscard]] Vec multiply(Kernel& k, const Matrix& matrix, const Vec& values, double& maximum_ratio,
    std::span<const std::size_t> node_indices = {});
[[nodiscard]] Vec solve_spd(Kernel& k, const Matrix& matrix, const Vec& rhs, double& maximum_ratio,
    std::span<const std::size_t> node_indices = {});
[[nodiscard]] Vec decode_velocity(Kernel& k, const Data& data, double& maximum_ratio, bool check_dependent_pi = true);
[[nodiscard]] double dot(Kernel& k, std::span<const double> a, std::span<const double> b);
[[nodiscard]] Vec physical_row_products(Kernel& k, const Matrix& a, const Vec& x, std::size_t row);
[[nodiscard]] double kinetic(Kernel& k, const Data& data, double& maximum_ratio);
struct ReferenceMetrics {
    CanonicalGeometryMeasure geometry;
    CanonicalSpecies masses{};
    std::array<double, 3> consistent{};
};
[[nodiscard]] RetainedScalar retained_value(Kernel& k, double value);
[[nodiscard]] RetainedScalarUpdate retained_increment(Kernel& k, const RetainedScalar& value, double increment);
[[nodiscard]] RetainedScalarDifference retained_difference(Kernel& k, const RetainedScalar& left, const RetainedScalar& right);
[[nodiscard]] RetainedScalarProduct retained_product(Kernel& k, const RetainedScalar& value, double coefficient);
[[nodiscard]] CanonicalGeometryMeasure measure_geometry(Kernel& k, const RetainedScalar& left, const RetainedScalar& right, double area);
[[nodiscard]] double physical_position(Kernel& k, const Data& data, std::size_t node);
void initialize_retained_momentum(Kernel& k, Data& data, const Vec& requested_velocity);
void validate_scalar_projections(Kernel& k, const Data& data);
using Terms = CanonicalVector;
[[nodiscard]] Terms terms(std::initializer_list<double> values);
[[nodiscard]] Terms terms(const RetainedScalar& value);
[[nodiscard]] Terms terms(const RetainedScalarDifference& value);
[[nodiscard]] Terms terms(const CanonicalGeometryMeasure& value);
[[nodiscard]] Terms joined(Kernel& k, std::initializer_list<Terms> values);
[[nodiscard]] Terms negated(const Terms& values);
[[nodiscard]] Terms scaled(Kernel& k, const Terms& values, double coefficient);
[[nodiscard]] Terms products(Kernel& k, const Terms& left, const Terms& right);
[[nodiscard]] Terms row_terms(Kernel& k, const Matrix& matrix, const std::vector<Terms>& values, std::size_t node);
[[nodiscard]] double projected(Kernel& k, const Terms& values);
[[nodiscard]] double magnitude(Kernel& k, const Terms& values);
[[nodiscard]] bool exact_zero(Kernel& k, const Terms& values);
[[nodiscard]] CanonicalFiniteExpression expression(Kernel& k, const Terms& values);
[[nodiscard]] double recorded(Kernel& k, CanonicalStepReport& report, std::string_view name, const Terms& values,
    std::uint32_t node = canonical_absent_index, std::uint32_t owner = canonical_absent_index);
[[nodiscard]] RetainedScalar stored_update(Kernel& k, CanonicalStepReport& report, std::string_view name,
    const RetainedScalar& before, double increment, std::uint32_t node = canonical_absent_index,
    std::uint32_t owner = canonical_absent_index);
[[nodiscard]] RetainedScalar stored_update(Kernel& k, CanonicalStepReport& report, std::string_view name,
    const RetainedScalar& before, const RetainedScalar& increment, std::uint32_t node = canonical_absent_index,
    std::uint32_t owner = canonical_absent_index);
[[nodiscard]] CanonicalMomentumConstruction momentum_row(Kernel& k, const Data& data, const Vec& velocity, std::size_t node);
[[nodiscard]] const Terms& recorded_terms(Kernel& k, const CanonicalStepReport& report, std::string_view name,
    std::uint32_t node = canonical_absent_index, std::uint32_t owner = canonical_absent_index);
void finite_ledgers(Kernel& k, const Data& old, Data& next, const Vec& u0, CanonicalStepReport& report);
[[nodiscard]] ReferenceMetrics reference_metrics(Kernel& k, const Data& data, const CanonicalReferencePiece& piece);
[[nodiscard]] double owner_volume(Kernel& k, const Data& data, std::size_t owner);
[[nodiscard]] Vec volume_change(Kernel& k, const Data& data, const Vec& displacement);
[[nodiscard]] Vec exterior_force(Kernel& k, const Data& data);
[[nodiscard]] Vec gravity_force(Kernel& k, const Data& data);
[[nodiscard]] std::array<double, 7> pressure_components(Kernel& k, const Data& data, std::span<const double> pressure, std::size_t node);
[[nodiscard]] Vec pressure_force(Kernel& k, const Data& data, std::span<const double> pressure);
void update_readings(Kernel& k, Data& data, double& maximum_quadrature_ratio);
[[nodiscard]] ReferenceEquilibriumState evaluate_owner(Kernel& k, const Data& data, const ReferenceConservedCell& cell, std::size_t owner);
void supported(Kernel& k, const ReferenceConservedCell& cell, const ReferenceEquilibriumState& state);
void check_thermodynamics(Kernel& k, const Data& before, const Data& after, std::size_t owner, CanonicalStepReport& report);
[[nodiscard]] double independent_work_pressure(Kernel& k, const Data& data, std::size_t owner, double delta_volume);
[[nodiscard]] Code reference_failure_code(ReferenceErrorCode code) noexcept;
}
