#include "mixed_atmospheres/experimental/retained_scalar.hpp"
#include <bit>
#include <cfenv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace ma::experimental;
namespace {
void check(bool condition, const char* reason) { if (!condition) throw std::logic_error(reason); }
void hex(double value) {
    std::cout << '"' << std::hex << std::setw(16) << std::setfill('0') << std::bit_cast<std::uint64_t>(value) << std::dec << '"';
}
RetainedScalar make(double high, double low) {
    CanonicalFailureWitness witness; return make_retained_scalar(high, low, witness);
}
std::string encoded(const CanonicalFailureWitness& witness) {
    std::array<char, canonical_witness_json_capacity> buffer{};
    const auto status = encode_canonical_witness_json(witness, buffer);
    check(status.result == CanonicalEncodingResult::success, "Difference witness could not encode");
    return std::string(buffer.data(), status.written);
}
auto components(const RetainedScalar& value) {
    return std::array<std::uint64_t, 2>{std::bit_cast<std::uint64_t>(value.high()), std::bit_cast<std::uint64_t>(value.low())};
}
void record(const char* group, const RetainedScalar& left, const RetainedScalar& right) {
    const auto old_left = components(left), old_right = components(right);
    CanonicalFailureWitness witness;
    const auto result = subtract_retained_scalar(left, right, witness);
    check(!witness.failed() && old_left == components(left) && old_right == components(right), "Difference changed input or sealed a failure");
    check(result.expansion_count <= 4, "Difference expansion exceeds its fixed bound");
    std::cout << "DIFFERENCE [\"" << group << "\"";
    for (double value : {left.high(), left.low(), right.high(), right.low(), result.value.high(), result.value.low()}) { std::cout << ','; hex(value); }
    std::cout << ',' << result.expansion_count;
    for (double value : result.expansion_terms) { std::cout << ','; hex(value); }
    for (double value : result.roundoff_remainders) { std::cout << ','; hex(value); }
    std::cout << "]\n";
}
void rejection(const char* name, const RetainedScalar& left, const RetainedScalar& right) {
    const auto old_left = components(left), old_right = components(right);
    CanonicalFailureWitness witness; bool rejected = false;
    try { static_cast<void>(subtract_retained_scalar(left, right, witness)); }
    catch (const CanonicalFailure&) { rejected = true; }
    check(rejected && witness.failed() && witness.capture_valid(), "Expected typed complete retained difference rejection");
    check(old_left == components(left) && old_right == components(right), "Rejected difference changed an input");
    std::cout << "REJECT [\"" << name << "\"," << encoded(witness) << "]\n";
}
struct RestoreRounding {
    int previous = std::fegetround();
    ~RestoreRounding() { std::fesetround(previous); }
};
}
int main() {
    try {
        constexpr std::array<int, 5> exponents{-600, -100, 0, 200, 600};
        const double successor = std::nextafter(1.0, 2.0);
        for (int e : exponents) for (int sign : {-1, 1}) for (int low_sign : {-1, 1})
            record("head-cancellation", make(std::ldexp(static_cast<double>(sign) * 1.4375, e), std::ldexp(static_cast<double>(low_sign), e - 70)),
                make(std::ldexp(static_cast<double>(sign) * 1.4375, e), 0));
        std::cout << "PASS head_cancellation_retains_small_difference\n";
        for (int e : exponents) for (int sign : {-1, 1}) for (int low_sign : {-1, 1})
            record("low-cancellation", make(std::ldexp(static_cast<double>(sign) * 1.4375, e), std::ldexp(static_cast<double>(low_sign) * successor, e - 70)),
                make(std::ldexp(static_cast<double>(sign) * 1.4375, e), std::ldexp(static_cast<double>(low_sign), e - 70)));
        std::cout << "PASS low_cancellation_retains_small_difference\n";
        for (int e : exponents) for (int gap : {-200, 200})
        for (int left_sign : {-1, 1}) for (int left_low_sign : {-1, 1})
        for (int right_sign : {-1, 1}) for (int right_low_sign : {-1, 1})
            record("separated", make(std::ldexp(static_cast<double>(left_sign) * successor, e), std::ldexp(static_cast<double>(left_low_sign) * successor, e - 70)),
                make(std::ldexp(static_cast<double>(right_sign) * 1.4375, e + gap), std::ldexp(static_cast<double>(right_low_sign), e + gap - 70)));
        std::cout << "PASS separated_scales_have_explicit_remainders\n";
        auto value = make(1, 0);
        for (int i = 0; i < 64; ++i) {
            CanonicalFailureWitness witness;
            const auto update = add_retained_scalar(value, std::ldexp(1.0, -70), witness);
            record("chain", update.value, value);
            value = update.value;
        }
        std::cout << "PASS completed_update_difference_chain\n";
        for (int e : exponents) for (int sign : {-1, 1}) for (int low_sign : {-1, 1}) {
            const auto same = make(std::ldexp(static_cast<double>(sign) * successor, e), std::ldexp(static_cast<double>(low_sign), e - 70));
            record("edge", same, same);
        }
        const auto zero = make(0, 0), one = make(1, 0);
        const double maximum = std::numeric_limits<double>::max(), minimum = std::numeric_limits<double>::min();
        record("edge", zero, zero); record("edge", make(-0.0, 0), zero);
        record("edge", one, zero); record("edge", zero, one);
        const auto positive_max = make(maximum, 0), negative_max = make(-maximum, 0);
        record("edge", positive_max, positive_max); record("edge", negative_max, negative_max);
        std::cout << "PASS zero_alias_and_identity_edges\n";
        rejection("overflow", positive_max, negative_max);
        rejection("subnormal-head-difference", make(minimum, 0), make(std::nextafter(minimum, 1.0), 0));
        rejection("subnormal-low-difference", make(1, minimum), make(1, std::nextafter(minimum, 1.0)));
        {
            RestoreRounding restore;
            check(std::fesetround(FE_UPWARD) == 0, "Cannot set standalone difference control rounding");
            rejection("unsupported-rounding", one, zero);
        }
        check(std::fegetround() == FE_TONEAREST, "Standalone difference control did not restore nearest rounding");
        std::cout << "PASS typed_atomic_difference_rejection\n";
        CanonicalFailureWitness sealed;
        check(sealed.begin_stage(CanonicalStage::diagnostics) && sealed.set_identity("sealed-first-difference-control", 10), "Cannot prepare sealed difference witness");
        sealed.seal(CanonicalFatalCode::unsupported_configuration, "Already rejected before difference");
        const auto before = encoded(sealed); bool rejected = false;
        try { static_cast<void>(subtract_retained_scalar(one, zero, sealed)); }
        catch (const CanonicalFailure&) { rejected = true; }
        check(rejected && before == encoded(sealed), "Difference overwrote an existing first failure");
        std::cout << "PASS first_failure_is_preserved\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
