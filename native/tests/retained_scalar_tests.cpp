#include "mixed_atmospheres/experimental/retained_scalar.hpp"
#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ma::experimental;
namespace {
void check(bool condition, const char* reason) { if (!condition) throw std::logic_error(reason); }
void hex(double value) { std::cout << '"' << std::hex << std::setw(16) << std::setfill('0') << std::bit_cast<std::uint64_t>(value) << std::dec << '"'; }
RetainedScalar make(double high, double low) {
    CanonicalFailureWitness witness; return make_retained_scalar(high, low, witness);
}
RetainedScalar record(const char* name, const RetainedScalar& before, double increment) {
    CanonicalFailureWitness witness; const auto result = add_retained_scalar(before, increment, witness);
    check(!witness.failed(), "Successful arithmetic sealed a failure");
    std::cout << "ROW [\"" << name << "\"";
    for (double value : {before.high(), before.low(), increment, result.value.high(), result.value.low(),
        result.represented_increment[0], result.represented_increment[1], result.roundoff_remainder}) {
        std::cout << ','; hex(value);
    }
    std::cout << "]\n"; return result.value;
}
void expected_rejection(double high, double low, double increment, bool construct) {
    CanonicalFailureWitness witness; bool rejected = false;
    if (construct) {
        try { static_cast<void>(make_retained_scalar(high, low, witness)); }
        catch (const CanonicalFailure&) { rejected = true; }
    } else {
        const auto before = make(high, low);
        const auto hi = std::bit_cast<std::uint64_t>(before.high()), lo = std::bit_cast<std::uint64_t>(before.low());
        try { static_cast<void>(add_retained_scalar(before, increment, witness)); }
        catch (const CanonicalFailure&) { rejected = true; }
        check(hi == std::bit_cast<std::uint64_t>(before.high()) && lo == std::bit_cast<std::uint64_t>(before.low()), "Rejected update changed its input");
    }
    check(rejected && witness.failed() && witness.capture_valid(), "Expected typed complete arithmetic rejection");
    std::array<char, canonical_witness_json_capacity> buffer{}; const auto status = encode_canonical_witness_json(witness, buffer);
    check(status.result == CanonicalEncodingResult::success, "Arithmetic witness could not encode");
    std::cout << "REJECT "; std::cout.write(buffer.data(), static_cast<std::streamsize>(status.written)); std::cout << '\n';
}
}
int main() {
    try {
        constexpr std::array<int, 5> exponents{-700, -100, 0, 250, 700};
        for (int e : exponents) for (int sign : {-1, 1}) for (int low_sign : {-1, 1}) for (int delta_sign : {-1, 1})
            static_cast<void>(record("separated", make(std::ldexp(static_cast<double>(sign), e),
                std::ldexp(static_cast<double>(low_sign), e - 70)), std::ldexp(static_cast<double>(delta_sign), e - 90)));
        std::cout << "PASS separated_normal_increments\n";
        for (int e : exponents) for (int sign : {-1, 1}) {
            const double high = std::ldexp(static_cast<double>(sign), e);
            static_cast<void>(record("cancellation", make(high, std::ldexp(1.0, e - 70)), -high));
        }
        std::cout << "PASS exact_cancellation\n";
        const double successor = std::nextafter(1.0, 2.0);
        for (int e : exponents) for (int sign : {-1, 1})
            static_cast<void>(record("remainder", make(std::ldexp(static_cast<double>(sign), e),
                std::ldexp(successor, e - 55)), std::ldexp(successor, e - 56)));
        std::cout << "PASS explicit_third_component\n";
        auto accumulated = make(1, 0);
        for (int i = 0; i < 64; ++i) accumulated = record("accumulate", accumulated, std::ldexp(1.0, -70));
        check(accumulated.high() == 1 && accumulated.low() == std::ldexp(1.0, -64), "Small increments were not retained");
        std::cout << "PASS repeated_sub_ulp_storage\n";
        expected_rejection(1, std::ldexp(1.0, -54), std::ldexp(1.0, -150), false);
        expected_rejection(1, std::ldexp(1.0, -54), -std::ldexp(1.0, -150), false);
        expected_rejection(std::numeric_limits<double>::max(), 0, std::numeric_limits<double>::max(), false);
        expected_rejection(std::numeric_limits<double>::denorm_min(), 0, 0, true);
        expected_rejection(1, std::numeric_limits<double>::denorm_min(), 0, true);
        expected_rejection(std::numeric_limits<double>::infinity(), 0, 0, true);
        expected_rejection(std::numeric_limits<double>::quiet_NaN(), 0, 0, true);
        std::cout << "PASS typed_atomic_rejection\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
