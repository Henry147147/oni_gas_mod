#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {
namespace {
constexpr std::size_t term_limit = 32768, report_term_limit = 98304;
void append(Kernel& k, Terms& output, double value) {
    k.finite(value);
    if (value == 0) return; // Exact zero elimination changes no constituent/state.
    k.require(output.size() < term_limit, Code::unsupported_configuration, "Finite-expression capacity exhausted");
    output.push_back(value);
}
}
Terms terms(std::initializer_list<double> values) {
    Terms result;
    for (double value : values) if (value != 0) result.push_back(value);
    return result;
}
Terms terms(const RetainedScalar& value) { return terms({value.high(), value.low()}); }
Terms terms(const RetainedScalarDifference& value) {
    return Terms(value.expansion_terms.begin(), value.expansion_terms.begin() + static_cast<std::ptrdiff_t>(value.expansion_count));
}
Terms terms(const CanonicalGeometryMeasure& value) {
    return Terms(value.volume_terms_m3.begin(), value.volume_terms_m3.begin() + static_cast<std::ptrdiff_t>(value.volume_term_count));
}
Terms joined(Kernel& k, std::initializer_list<Terms> values) {
    Terms result;
    for (const auto& list : values) for (double value : list) append(k, result, value);
    return result;
}
Terms negated(const Terms& values) {
    Terms result; result.reserve(values.size());
    for (double value : values) result.push_back(-value);
    return result;
}
Terms scaled(Kernel& k, const Terms& values, double coefficient) {
    k.finite(coefficient); Terms result;
    for (double value : values) {
        const auto product = retained_product(k, retained_value(k, value), coefficient);
        k.require(product.product_terms[2] == 0 && product.product_terms[3] == 0,
            Code::numerical_precision, "Scalar expression product acquired a low input");
        for (double remainder : product.roundoff_remainders)
            k.require(remainder == 0, Code::numerical_precision, "Scalar expression product exceeds its exact pair");
        append(k, result, product.product_terms[0]); append(k, result, product.product_terms[1]);
    }
    return result;
}
Terms products(Kernel& k, const Terms& left, const Terms& right) {
    Terms result;
    for (double coefficient : right) {
        const auto part = scaled(k, left, coefficient);
        for (double value : part) append(k, result, value);
    }
    return result;
}
Terms row_terms(Kernel& k, const Matrix& matrix, const std::vector<Terms>& values, std::size_t node) {
    k.require(node < matrix.size() && matrix[node].size() == values.size() && values.size() <= 33,
        Code::unsupported_configuration, "Finite matrix-row shape differs");
    k.row(Field::matrix_coefficients, matrix[node]); Terms result;
    for (std::size_t p = 0; p < values.size(); ++p) {
        const std::array<double, 2> indices{static_cast<double>(node), static_cast<double>(p)};
        k.row(Field::node_indices, indices);
        const auto part = scaled(k, values[p], matrix[node][p]);
        for (double value : part) append(k, result, value);
    }
    return result;
}
double projected(Kernel& k, const Terms& values) {
    k.require(values.size() <= term_limit, Code::unsupported_configuration, "Finite-expression input exceeds capacity");
    Sum total(k, 36); for (double value : values) total.add(value); return total.get();
}
double magnitude(Kernel& k, const Terms& values) {
    Sum total(k, 37); for (double value : values) total.add(std::abs(value)); return total.get();
}
bool exact_zero(Kernel& k, const Terms& values) {
    // Fixed bounded Grow-Expansion postcondition for prescribed geometry.
    // It classifies exact zero only; it never supplies stored state or a retry.
    k.require(values.size() <= 32, Code::unsupported_configuration, "Exact-zero certificate exceeds capacity");
    std::array<double, 32> expansion{}; std::size_t count = 0;
    for (double input : values) {
        k.finite(input); std::array<double, 32> next{}; std::size_t used = 0; double q = input;
        for (std::size_t i = 0; i < count; ++i) {
            const double a = q, b = expansion[i], high = k.add(a, b);
            const double recovered_b = k.sub(high, a), recovered_a = k.sub(high, recovered_b);
            const double low = k.add(k.sub(a, recovered_a), k.sub(b, recovered_b));
            if (low != 0) next[used++] = low;
            q = high;
        }
        if (q != 0) { k.require(used < next.size(), Code::unsupported_configuration, "Exact-zero expansion exhausted"); next[used++] = q; }
        expansion = next; count = used;
    }
    return count == 0;
}
CanonicalFiniteExpression expression(Kernel& k, const Terms& values) { return {values, projected(k, values)}; }
double recorded(Kernel& k, CanonicalStepReport& report, std::string_view name, const Terms& values,
    std::uint32_t node, std::uint32_t owner) {
    std::size_t count = values.size();
    for (const auto& item : report.retained.expressions) {
        count += item.expression.terms.size();
        k.require(item.name != name || item.node != node || item.owner != owner,
            Code::unsupported_configuration, "Duplicate finite-expression identity");
    }
    k.require(count <= report_term_limit && report.retained.expressions.size() < 1024,
        Code::unsupported_configuration, "Complete finite-expression report exceeds capacity");
    auto value = expression(k, values); const double result = value.projection;
    report.retained.expressions.push_back({std::string(name), node, owner, std::move(value)});
    return result;
}
const Terms& recorded_terms(Kernel& k, const CanonicalStepReport& report, std::string_view name,
    std::uint32_t node, std::uint32_t owner) {
    for (const auto& item : report.retained.expressions)
        if (item.name == name && item.node == node && item.owner == owner) return item.expression.terms;
    k.fail(Code::unsupported_configuration, "Required finite expression is missing");
}
RetainedScalar stored_update(Kernel& k, CanonicalStepReport& report, std::string_view name,
    const RetainedScalar& before, double increment, std::uint32_t node, std::uint32_t owner) {
    return stored_update(k, report, name, before, retained_value(k, increment), node, owner);
}
RetainedScalar stored_update(Kernel& k, CanonicalStepReport& report, std::string_view name,
    const RetainedScalar& before, const RetainedScalar& increment, std::uint32_t node, std::uint32_t owner) {
    const auto negative = retained_product(k, increment, -1);
    for (double remainder : negative.roundoff_remainders)
        k.require(remainder == 0, Code::numerical_precision, "Stored increment negation acquired a remainder");
    const auto result = retained_difference(k, before, negative.value);
    const auto change = retained_difference(k, result.value, before);
    k.represented(increment.high(), change.value.high(), "Retained update erased or reversed its complete physical increment");
    k.require(report.retained.updates.size() < 256, Code::unsupported_configuration, "Stored-update report exceeds capacity");
    report.retained.updates.push_back({std::string(name), node, owner, before, increment, result, change});
    return result.value;
}
CanonicalMomentumConstruction momentum_row(Kernel& k, const Data& d, const Vec& velocity, std::size_t node) {
    CanonicalMomentumConstruction result; result.node = static_cast<std::uint32_t>(node);
    k.row(Field::matrix_coefficients, d.consistent_mass[node]); k.row(Field::velocity1, velocity);
    RetainedScalar total;
    for (std::size_t p = 0; p < velocity.size(); ++p) {
        const std::array<double, 2> indices{static_cast<double>(node), static_cast<double>(p)};
        k.row(Field::node_indices, indices);
        const auto product = retained_product(k, retained_value(k, velocity[p]), d.consistent_mass[node][p]);
        for (double remainder : product.roundoff_remainders)
            k.require(remainder == 0, Code::numerical_precision, "Scalar momentum product exceeds its exact pair");
        result.product_terms.push_back({product.product_terms[0], product.product_terms[1]});
        const auto negative = retained_product(k, product.value, -1);
        for (double remainder : negative.roundoff_remainders)
            k.require(remainder == 0, Code::numerical_precision, "Momentum product negation has a remainder");
        const auto addition = retained_difference(k, total, negative.value);
        result.sum_remainders.push_back(addition.roundoff_remainders); total = addition.value;
    }
    result.value = total; return result;
}
}
