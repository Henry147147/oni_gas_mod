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
    check(status.result == CanonicalEncodingResult::success, "Product witness could not encode");
    return std::string(buffer.data(), status.written);
}
void record(const char* group, const RetainedScalar& input, double coefficient) {
    const auto high = std::bit_cast<std::uint64_t>(input.high()), low = std::bit_cast<std::uint64_t>(input.low());
    CanonicalFailureWitness witness;
    const auto result = multiply_retained_scalar(input, coefficient, witness);
    check(!witness.failed(), "Successful retained product sealed a failure");
    check(high == std::bit_cast<std::uint64_t>(input.high()) && low == std::bit_cast<std::uint64_t>(input.low()), "Product changed its input");
    std::cout << "SCALE [\"" << group << "\"";
    for (double value : {input.high(), input.low(), coefficient}) { std::cout << ','; hex(value); }
    for (double value : result.product_terms) { std::cout << ','; hex(value); }
    for (double value : {result.value.high(), result.value.low()}) { std::cout << ','; hex(value); }
    for (double value : result.roundoff_remainders) { std::cout << ','; hex(value); }
    std::cout << "]\n";
}
void rejection(const char* name, const RetainedScalar& input, double coefficient) {
    const auto high = std::bit_cast<std::uint64_t>(input.high()), low = std::bit_cast<std::uint64_t>(input.low());
    CanonicalFailureWitness witness; bool rejected = false;
    try { static_cast<void>(multiply_retained_scalar(input, coefficient, witness)); }
    catch (const CanonicalFailure&) { rejected = true; }
    check(rejected && witness.failed() && witness.capture_valid(), "Expected typed complete retained product rejection");
    check(high == std::bit_cast<std::uint64_t>(input.high()) && low == std::bit_cast<std::uint64_t>(input.low()), "Rejected product changed its input");
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
        constexpr std::array<int, 3> factors{-200, 0, 200};
        const double successor = std::nextafter(1.0, 2.0);
        for (int e : exponents) for (int f : factors)
        for (int sign : {-1, 1}) for (int low_sign : {-1, 1}) for (int factor_sign : {-1, 1})
            record("scaled", make(std::ldexp(static_cast<double>(sign) * successor, e),
                std::ldexp(static_cast<double>(low_sign) * successor, e - 70)),
                std::ldexp(static_cast<double>(factor_sign) * 1.375, f));
        std::cout << "PASS signed_scaled_components\n";
        record("rounding", make(successor, std::nextafter(std::ldexp(1.0, -54), 1.0)), successor);
        record("rounding", make(1.5, std::ldexp(successor, -70)), 1.5);
        record("rounding", make(std::nextafter(2.0, 0.0), std::ldexp(1.0, -80)), std::nextafter(2.0, 0.0));
        record("rounding", make(1.125, std::ldexp(successor, -70)), 1.375);
        record("rounding", make(-1.125, -std::ldexp(successor, -70)), -1.375);
        record("rounding", make(std::ldexp(successor, 900), std::ldexp(successor, 830)), std::ldexp(successor, -200));
        record("rounding", make(std::ldexp(successor, -700), std::ldexp(successor, -770)), std::ldexp(successor, 100));
        record("rounding", make(1 + std::ldexp(1.0, -51), 0), 1.5);
        std::cout << "PASS nonzero_product_errors\n";
        const double maximum = std::numeric_limits<double>::max(), minimum = std::numeric_limits<double>::min();
        record("edge", make(0, 0), 1.5); record("edge", make(-0.0, 0), -1.5);
        record("edge", make(1, std::ldexp(1.0, -70)), 0); record("edge", make(1, std::ldexp(1.0, -70)), -0.0);
        record("edge", make(maximum, 0), 1); record("edge", make(-maximum, 0), 1);
        record("edge", make(minimum, 0), 1); record("edge", make(maximum / 2, 0), 2);
        std::cout << "PASS zero_identity_and_range_edges\n";
        rejection("overflow", make(maximum, 0), 2);
        rejection("subnormal-product", make(minimum, 0), .5);
        rejection("zero-product-underflow", make(minimum, 0), minimum);
        rejection("low-component-underflow", make(1, std::ldexp(1.0, -1000)), std::ldexp(1.0, -100));
        rejection("zero-error-underflow", make(std::ldexp(successor, -1000), 0), successor);
        rejection("half-minimum-error", make(std::ldexp(std::nextafter(2.0, 0.0), -1022), 0), 1.5);
        rejection("subnormal-error", make(std::ldexp(successor, -970), 0), successor);
        rejection("infinite-coefficient", make(1, 0), std::numeric_limits<double>::infinity());
        rejection("nan-coefficient", make(1, 0), std::numeric_limits<double>::quiet_NaN());
        const auto one = make(1, 0);
        {
            RestoreRounding restore;
            check(std::fesetround(FE_UPWARD) == 0, "Cannot set standalone product control rounding");
            rejection("unsupported-rounding", one, 1.5);
        }
        check(std::fegetround() == FE_TONEAREST, "Standalone product control did not restore nearest rounding");
        std::cout << "PASS typed_atomic_product_rejection\n";
        CanonicalFailureWitness sealed;
        check(sealed.begin_stage(CanonicalStage::diagnostics) && sealed.set_identity("sealed-first-product-control", 9), "Cannot prepare sealed witness");
        sealed.seal(CanonicalFatalCode::unsupported_configuration, "Already rejected before product");
        const auto before = encoded(sealed); bool rejected = false;
        try { static_cast<void>(multiply_retained_scalar(one, 2, sealed)); }
        catch (const CanonicalFailure&) { rejected = true; }
        check(rejected && before == encoded(sealed), "Product overwrote an existing first failure");
        std::cout << "PASS first_failure_is_preserved\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
