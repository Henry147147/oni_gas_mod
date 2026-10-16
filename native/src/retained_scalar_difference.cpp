#include "mixed_atmospheres/experimental/retained_scalar.hpp"
#include <bit>
#include <cmath>
#include <span>

namespace ma::experimental {
namespace {
using Field = CanonicalWitnessField;
struct Pair { double high, low; };
class DifferenceArithmetic {
public:
    DifferenceArithmetic(const RetainedScalar& left, const RetainedScalar& right, CanonicalFailureWitness& witness)
        : inputs_{left.high(), left.low(), right.high(), right.low()}, witness_(witness) {
        if (witness.failed()) canonical_fail(witness, witness.code(), witness.reason());
        if (!witness.begin_stage(CanonicalStage::diagnostics) || !witness.set_identity("retained-scalar-difference-v1", 0))
            canonical_fail(witness, witness.code(), witness.reason());
        require_canonical_arithmetic_environment(witness);
        if (!witness.begin_stage(CanonicalStage::diagnostics)) canonical_fail(witness, witness.code(), witness.reason());
        for (double value : inputs_) classify(value);
    }
    Pair two_sum(double a, double b) {
        const double high = add(a, b), recovered_b = sub(high, a), recovered_a = sub(high, recovered_b);
        const double error_a = sub(a, recovered_a), error_b = sub(b, recovered_b);
        const double low = add(error_a, error_b);
        if (tape_count_ + 2 > tape_.size()) fail("Retained difference arithmetic tape capacity exhausted");
        tape_[tape_count_++] = high; tape_[tape_count_++] = low;
        return {high, low};
    }
    void grow(std::array<double, 4>& expansion, std::size_t& count, double term) {
        // Fixed-size Grow-Expansion with zero elimination. The input pair is
        // normalized, so low/high begins a nonoverlapping expansion. Two
        // additions can produce at most four components and five TwoSums.
        std::array<double, 4> next{}; std::size_t used = 0;
        double current = term;
        for (std::size_t i = 0; i < count; ++i) {
            const auto pair = two_sum(current, expansion[i]);
            if (pair.low != 0) append(next, used, pair.low);
            current = pair.high;
        }
        if (current != 0) append(next, used, current);
        expansion = next; count = used;
    }
    void require_sign(Pair stored, double exact_leading) {
        const double leading = stored.high != 0 ? stored.high : stored.low;
        if ((leading == 0) != (exact_leading == 0) ||
            (leading != 0 && std::signbit(leading) != std::signbit(exact_leading)))
            fail("Retained difference erased or reversed its exact total");
    }
private:
    double add(double a, double b) {
        a_ = a; b_ = b; operands_known_ = true; result_known_ = false;
        classify(a); classify(b);
        result_ = a + b; result_known_ = true; ++operations_;
        classify(result_); return result_;
    }
    double sub(double a, double b) { return add(a, -b); }
    void classify(double value) {
        const auto raw = CanonicalDiagnosticNumber::capture(value);
        if (!raw.has(CanonicalDiagnosticNumber::finite) || raw.has(CanonicalDiagnosticNumber::subnormal)) {
            rejected_ = value; rejected_known_ = true;
            fail("Retained difference requires normal finite arithmetic or exact zero");
        }
    }
    void append(std::array<double, 4>& values, std::size_t& count, double value) {
        if (count == values.size()) fail("Retained difference expansion capacity exhausted");
        values[count++] = value;
    }
    [[noreturn]] void fail(std::string_view reason) {
        bool ok = witness_.add_scalar(Field::operand_left, inputs_[0], 0) && witness_.add_scalar(Field::operand_left, inputs_[1], 1) &&
            witness_.add_scalar(Field::operand_right, inputs_[2], 0) && witness_.add_scalar(Field::operand_right, inputs_[3], 1) &&
            witness_.add_scalar(Field::basis_index, static_cast<double>(operations_));
        if (ok && operands_known_) ok = witness_.add_scalar(Field::operand_left, a_, 2) && witness_.add_scalar(Field::operand_right, b_, 2);
        if (ok && result_known_) ok = witness_.add_scalar(Field::arithmetic_result, result_);
        if (ok && rejected_known_) ok = witness_.add_scalar(Field::arithmetic_result, rejected_, 1);
        if (ok) ok = witness_.add_row(Field::matrix_products, std::span<const double>(tape_.data(), tape_count_));
        if (!ok) canonical_fail(witness_, witness_.code(), witness_.reason());
        canonical_fail(witness_, CanonicalFatalCode::numerical_precision, reason);
    }
    std::array<double, 4> inputs_;
    CanonicalFailureWitness& witness_;
    double a_ = 0, b_ = 0, result_ = 0, rejected_ = 0;
    bool operands_known_ = false, result_known_ = false, rejected_known_ = false;
    std::size_t operations_ = 0, tape_count_ = 0;
    std::array<double, 12> tape_{}; // Five grow TwoSums, then one normalization.
};
}

RetainedScalarDifference subtract_retained_scalar(const RetainedScalar& left, const RetainedScalar& right,
    CanonicalFailureWitness& witness) {
    DifferenceArithmetic arithmetic(left, right, witness);
    RetainedScalarDifference result;
    if (left.low() != 0) result.expansion_terms[result.expansion_count++] = left.low();
    if (left.high() != 0) result.expansion_terms[result.expansion_count++] = left.high();
    arithmetic.grow(result.expansion_terms, result.expansion_count, -right.low());
    arithmetic.grow(result.expansion_terms, result.expansion_count, -right.high());
    Pair stored{0, 0};
    if (result.expansion_count == 1) stored.high = result.expansion_terms[0];
    else if (result.expansion_count >= 2) {
        stored = arithmetic.two_sum(result.expansion_terms[result.expansion_count - 1], result.expansion_terms[result.expansion_count - 2]);
        for (std::size_t i = 0; i + 2 < result.expansion_count; ++i) result.roundoff_remainders[i] = result.expansion_terms[i];
    }
    arithmetic.require_sign(stored, result.expansion_count == 0 ? 0 : result.expansion_terms[result.expansion_count - 1]);
    result.value = RetainedScalar(stored.high, stored.low);
    return result;
}
}
