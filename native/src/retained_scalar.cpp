#include "mixed_atmospheres/experimental/retained_scalar.hpp"
#include <bit>
#include <cmath>
#include <span>

namespace ma::experimental {
namespace {
using Field = CanonicalWitnessField;
using Code = CanonicalFatalCode;
struct Pair { double high, low; };
class Arithmetic {
public:
    Arithmetic(CanonicalFailureWitness& witness, double high, double low, double increment)
        : witness_(witness), high_(high), low_(low), increment_(increment) {
        if (witness.failed()) canonical_fail(witness, witness.code(), witness.reason());
        if (!witness.begin_stage(CanonicalStage::diagnostics) || !witness.set_identity("retained-scalar-v1", 0))
            canonical_fail(witness, witness.code(), witness.reason());
        require_canonical_arithmetic_environment(witness);
        classify(high); classify(low); classify(increment);
    }
    double add(double a, double b) {
        a_ = a; b_ = b; operands_known_ = true; result_known_ = false;
        classify(a); classify(b); result_ = a + b; result_known_ = true; ++operations_;
        classify(result_); return result_;
    }
    double sub(double a, double b) { return add(a, -b); }
    Pair two_sum(double a, double b) {
        const double high = add(a, b);
        const double recovered_b = sub(high, a);
        const double recovered_a = sub(high, recovered_b);
        const double error_a = sub(a, recovered_a), error_b = sub(b, recovered_b);
        const double low = add(error_a, error_b);
        tape_[tape_count_++] = high; tape_[tape_count_++] = low;
        return {high, low};
    }
    [[noreturn]] void fail(std::string_view reason) {
        bool ok = witness_.add_scalar(Field::operand_left, high_, 0) && witness_.add_scalar(Field::operand_left, low_, 1) &&
            witness_.add_scalar(Field::operand_right, increment_, 0) &&
            witness_.add_scalar(Field::basis_index, static_cast<double>(operations_));
        if (ok && operands_known_) ok = witness_.add_scalar(Field::operand_left, a_, 2) && witness_.add_scalar(Field::operand_right, b_, 2);
        if (ok && result_known_) ok = witness_.add_scalar(Field::arithmetic_result, result_);
        if (ok) ok = witness_.add_row(Field::matrix_products, std::span<const double>(tape_.data(), tape_count_));
        if (!ok) canonical_fail(witness_, witness_.code(), witness_.reason());
        canonical_fail(witness_, Code::numerical_precision, reason);
    }
private:
    void classify(double value) {
        const auto raw = CanonicalDiagnosticNumber::capture(value);
        if (!raw.has(CanonicalDiagnosticNumber::finite) || raw.has(CanonicalDiagnosticNumber::subnormal)) {
            // Include the rejected raw operand even when arithmetic never began.
            if (!witness_.add_scalar(Field::arithmetic_result, value, 1))
                canonical_fail(witness_, witness_.code(), witness_.reason());
            fail("Retained scalar requires normal finite arithmetic or exact zero");
        }
    }
    CanonicalFailureWitness& witness_;
    double high_, low_, increment_, a_ = 0, b_ = 0, result_ = 0;
    bool operands_known_ = false, result_known_ = false;
    std::size_t operations_ = 0, tape_count_ = 0;
    std::array<double, 16> tape_{}; // At most six TwoSums: 12 copied output values.
};
}
RetainedScalar make_retained_scalar(double high, double low, CanonicalFailureWitness& witness) {
    Arithmetic arithmetic(witness, high, low, 0);
    const auto normalized = arithmetic.two_sum(high, low);
    return RetainedScalar(normalized.high, normalized.low);
}
RetainedScalarUpdate add_retained_scalar(const RetainedScalar& input, double increment, CanonicalFailureWitness& witness) {
    Arithmetic arithmetic(witness, input.high_, input.low_, increment);
    const auto a = arithmetic.two_sum(input.high_, increment);
    const auto b = arithmetic.two_sum(input.low_, a.low);
    const auto c = arithmetic.two_sum(a.high, b.high);
    const auto d = arithmetic.two_sum(c.low, b.low);
    const auto e = arithmetic.two_sum(c.high, d.high);
    // Each TwoSum is error-free in the admitted arithmetic range:
    // input.high + input.low + increment = e.high + e.low + d.low.
    const auto delta = arithmetic.two_sum(increment, -d.low);
    const double leading = delta.high != 0 ? delta.high : delta.low;
    if (increment != 0 && (leading == 0 || std::signbit(leading) != std::signbit(increment)))
        arithmetic.fail("Retained scalar erased or reversed its required increment");
    RetainedScalarUpdate result;
    result.value = RetainedScalar(e.high, e.low); result.requested_increment = increment;
    result.represented_increment = {delta.high, delta.low}; result.roundoff_remainder = d.low;
    return result;
}
}
