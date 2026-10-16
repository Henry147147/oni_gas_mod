#include "mixed_atmospheres/experimental/retained_scalar.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <span>

namespace ma::experimental {
namespace {
using Field = CanonicalWitnessField;
struct Pair { double high, low; };

// Normal significands have 53 bits; their exact product has 105 or 106.
// This computes its high 64 bits using bounded unsigned 32-bit limbs. It
// classifies representability only and never supplies a floating-point result.
bool exact_binary64_product(double a, double b) noexcept {
    if (a == 0 || b == 0) return true;
    constexpr std::uint64_t mask32 = 0xffffffffULL, mask52 = (1ULL << 52) - 1;
    const auto ma = (std::bit_cast<std::uint64_t>(a) & mask52) | (1ULL << 52);
    const auto mb = (std::bit_cast<std::uint64_t>(b) & mask52) | (1ULL << 52);
    const auto a0 = ma & mask32, a1 = ma >> 32, b0 = mb & mask32, b1 = mb >> 32;
    const auto p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0;
    const auto carry = ((p00 >> 32) + (p01 & mask32) + (p10 & mask32)) >> 32;
    const auto upper = a1 * b1 + (p01 >> 32) + (p10 >> 32) + carry;
    const auto width = 64 + static_cast<int>(std::bit_width(upper));
    return width - std::countr_zero(ma) - std::countr_zero(mb) <= 53;
}

class ProductArithmetic {
public:
    ProductArithmetic(const RetainedScalar& input, double coefficient, CanonicalFailureWitness& witness)
        : input_(input), coefficient_(coefficient), witness_(witness) {
        if (witness.failed()) canonical_fail(witness, witness.code(), witness.reason());
        if (!witness.begin_stage(CanonicalStage::diagnostics) || !witness.set_identity("retained-scalar-product-v1", 0))
            canonical_fail(witness, witness.code(), witness.reason());
        require_canonical_arithmetic_environment(witness);
        if (!witness.begin_stage(CanonicalStage::diagnostics)) canonical_fail(witness, witness.code(), witness.reason());
        classify(input.high()); classify(input.low()); classify(coefficient);
    }
    double add(double a, double b) {
        prepare(a, b); const double value = a + b; result_ = value; result_known_ = true;
        ++operations_; classify(value); return value;
    }
    double sub(double a, double b) { return add(a, -b); }
    Pair two_sum(double a, double b) {
        const double high = add(a, b), recovered_b = sub(high, a), recovered_a = sub(high, recovered_b);
        const double error_a = sub(a, recovered_a), error_b = sub(b, recovered_b);
        const double low = add(error_a, error_b);
        record(high, low); return {high, low};
    }
    Pair two_product(double a, double b) {
        prepare(a, b); const double high = a * b; result_ = high; result_known_ = true;
        ++operations_; classify(high);
        if (a != 0 && b != 0 && high == 0) fail("Retained product erased a nonzero component product");
        // The error-free FMA transform is valid only when its error is
        // representable. In particular, normal high alone is insufficient.
        fma_addend_ = -high; fma_known_ = true;
        const double low = std::fma(a, b, fma_addend_); result_ = low;
        ++operations_; classify(low);
        if (low == 0 && !exact_binary64_product(a, b))
            fail("Retained product multiplication error underflowed to zero");
        record(high, low); return {high, low};
    }
    Pair compress(Pair value, double term, double& remainder) {
        const auto a = two_sum(value.high, term), b = two_sum(value.low, a.low);
        const auto c = two_sum(a.high, b.high), d = two_sum(c.low, b.low);
        const auto e = two_sum(c.high, d.high);
        remainder = d.low;
        return e;
    }
    void require_output(Pair value) {
        if (coefficient_ == 0 || (input_.high() == 0 && input_.low() == 0)) {
            if (value.high != 0 || value.low != 0) fail("Retained zero product became nonzero");
            return;
        }
        const double leading = value.high != 0 ? value.high : value.low;
        const double input_leading = input_.high() != 0 ? input_.high() : input_.low();
        if (leading == 0 || std::signbit(leading) != (std::signbit(input_leading) != std::signbit(coefficient_)))
            fail("Retained product erased or reversed its total value");
    }
private:
    void prepare(double a, double b) {
        a_ = a; b_ = b; operands_known_ = true; result_known_ = false; fma_known_ = false;
        classify(a); classify(b);
    }
    void classify(double value) {
        const auto raw = CanonicalDiagnosticNumber::capture(value);
        if (!raw.has(CanonicalDiagnosticNumber::finite) || raw.has(CanonicalDiagnosticNumber::subnormal)) {
            rejected_ = value; rejected_known_ = true;
            fail("Retained product requires normal finite arithmetic or exact zero");
        }
    }
    void record(double high, double low) {
        if (tape_count_ + 2 > tape_.size()) fail("Retained product arithmetic tape capacity exhausted");
        tape_[tape_count_++] = high; tape_[tape_count_++] = low;
    }
    [[noreturn]] void fail(std::string_view reason) {
        bool ok = witness_.add_scalar(Field::operand_left, input_.high(), 0) &&
            witness_.add_scalar(Field::operand_left, input_.low(), 1) &&
            witness_.add_scalar(Field::operand_right, coefficient_, 0) &&
            witness_.add_scalar(Field::basis_index, static_cast<double>(operations_));
        if (ok && operands_known_) ok = witness_.add_scalar(Field::operand_left, a_, 2) && witness_.add_scalar(Field::operand_right, b_, 2);
        if (ok && result_known_) ok = witness_.add_scalar(Field::arithmetic_result, result_);
        if (ok && fma_known_) ok = witness_.add_scalar(Field::operand_right, fma_addend_, 3);
        if (ok && rejected_known_) ok = witness_.add_scalar(Field::arithmetic_result, rejected_, 1);
        for (std::size_t start = 0; ok && start < tape_count_; start += 33)
            ok = witness_.add_row(Field::matrix_products,
                std::span<const double>(tape_.data() + start, std::min<std::size_t>(33, tape_count_ - start)),
                static_cast<std::uint32_t>(start / 33));
        if (!ok) canonical_fail(witness_, witness_.code(), witness_.reason());
        canonical_fail(witness_, CanonicalFatalCode::numerical_precision, reason);
    }
    const RetainedScalar& input_;
    double coefficient_, a_ = 0, b_ = 0, result_ = 0, rejected_ = 0, fma_addend_ = 0;
    CanonicalFailureWitness& witness_;
    bool operands_known_ = false, result_known_ = false, rejected_known_ = false, fma_known_ = false;
    std::size_t operations_ = 0, tape_count_ = 0;
    std::array<double, 48> tape_{}; // 2 TwoProducts and 4 * 5 TwoSums: 44 values.
};
}

RetainedScalarProduct multiply_retained_scalar(const RetainedScalar& input, double coefficient, CanonicalFailureWitness& witness) {
    ProductArithmetic arithmetic(input, coefficient, witness);
    const auto high = arithmetic.two_product(input.high(), coefficient);
    const auto low = arithmetic.two_product(input.low(), coefficient);
    RetainedScalarProduct result;
    result.product_terms = {high.high, high.low, low.high, low.low};
    Pair value{0, 0};
    for (std::size_t i = 0; i < result.product_terms.size(); ++i)
        value = arithmetic.compress(value, result.product_terms[i], result.roundoff_remainders[i]);
    arithmetic.require_output(value);
    result.value = RetainedScalar(value.high, value.low);
    return result;
}
}
