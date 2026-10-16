#include "mixed_atmospheres/experimental/canonical_material_diagnostics.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace ma::experimental;
using Field = CanonicalWitnessField;
constexpr auto precision = CanonicalFatalCode::numerical_precision;
static_assert(std::is_base_of_v<std::exception, CanonicalFailure>);
static_assert(!std::is_base_of_v<std::runtime_error, CanonicalFailure>);

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Function> void test(const char* name, Function function, std::size_t& count) {
    function(); ++count; std::cout << "PASS " << name << '\n';
}
std::string encoded(const CanonicalFailureWitness& witness) {
    std::array<char, canonical_witness_json_capacity> buffer{};
    const auto status = encode_canonical_witness_json(witness, buffer);
    check(status.result == CanonicalEncodingResult::success && status.written <= buffer.size(), "Witness encoding failed");
    return {buffer.data(), status.written};
}
CanonicalFailureWitness witness() {
    CanonicalFailureWitness result;
    check(result.begin_stage(CanonicalStage::free_impulse), "Cannot start diagnostic stage");
    check(result.set_identity("diagnostic-control-model-v1:0123456789abcdef", 42), "Cannot retain model identity");
    CanonicalWitnessContext context;
    context.node = 5; context.owner = 2; context.newton_updates = 3; context.backtracks = 4;
    context.factorizations = 5; context.top_level_queries = 6; context.constitutive_charges = 7; context.oracle_iterations = 8;
    context.predicate_sequence = 9;
    check(result.set_context(context), "Cannot retain context");
    return result;
}
template<class Function> void rejects(Function function, CanonicalFailureWitness& evidence) {
    bool rejected = false;
    try { function(); }
    catch (const std::runtime_error&) { throw std::runtime_error("Canonical failure entered legacy runtime_error hierarchy"); }
    catch (const CanonicalFailure& error) {
        rejected = true;
        check(evidence.failed() && error.code() == evidence.code() && error.stage() == evidence.stage(), "Failure identity changed");
        check(error.what() == evidence.reason(), "Failure reason changed");
    }
    check(rejected, "Expected typed canonical rejection");
}
struct EnvironmentGuard {
    std::fenv_t saved{};
#if defined(_M_X64) || defined(__x86_64__)
    unsigned int mxcsr = _mm_getcsr();
#endif
    EnvironmentGuard() { check(std::fegetenv(&saved) == 0, "Cannot save test environment"); }
    ~EnvironmentGuard() {
        static_cast<void>(std::fesetenv(&saved));
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(mxcsr);
#endif
    }
    EnvironmentGuard(const EnvironmentGuard&) = delete;
    EnvironmentGuard& operator=(const EnvironmentGuard&) = delete;
};
}

int main() {
    using namespace ma::experimental;
    try {
        std::size_t passed = 0;
        std::string nonfinite_control_json;
        std::string boundary_control_json;
        std::array<std::string, 3> environment_trace_json{};
        test("integer-bit classification retains finite values signed zeros subnormals infinities and NaN payloads", [] {
            constexpr std::array<std::uint64_t, 10> values{UINT64_C(0), UINT64_C(0x8000000000000000),
                UINT64_C(1), UINT64_C(0x8000000000000001), UINT64_C(0x000fffffffffffff),
                UINT64_C(0x3ff0000000000000), UINT64_C(0xffefffffffffffff), UINT64_C(0x7ff0000000000000),
                UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000012345)};
            for (const auto bits : values) {
                const auto value = CanonicalDiagnosticNumber::capture(std::bit_cast<double>(bits));
                check(value.bits == bits && value.has(CanonicalDiagnosticNumber::present), "Raw bits changed");
                const auto exponent = (bits >> 52) & 2047U; const auto fraction = bits & UINT64_C(0x000fffffffffffff);
                check(value.has(CanonicalDiagnosticNumber::finite) == (exponent != 2047U), "Finite classification failed");
                check(value.has(CanonicalDiagnosticNumber::negative) == ((bits >> 63) != 0), "Sign classification failed");
                check(value.has(CanonicalDiagnosticNumber::zero) == (exponent == 0 && fraction == 0), "Zero classification failed");
                check(value.has(CanonicalDiagnosticNumber::subnormal) == (exponent == 0 && fraction != 0), "Subnormal classification failed");
            }
        }, passed);
        test("failure owns bounded reason and bypasses smaller-timestep runtime_error catches", [] {
            auto evidence = witness();
            check(evidence.add_scalar(Field::requested_impulse, 1, 5), "Cannot record impulse");
            rejects([&] { canonical_fail(evidence, precision, "Required impulse was erased"); }, evidence);
            check(evidence.context().node == 5 && evidence.context().owner == 2 && evidence.completed_step() == 42, "Context lost");
            check(evidence.context().predicate_sequence == 9, "Failure predicate sequence lost");
        }, passed);
        test("first sealed failure is immutable through all producer mutation methods", [] {
            auto evidence = witness(); check(evidence.add_scalar(Field::pi0, -0.0), "Cannot record signed zero");
            evidence.seal(precision, "First failure"); const auto before = encoded(evidence);
            check(!evidence.begin_stage(CanonicalStage::publication), "Sealed stage changed");
            check(!evidence.set_identity("other", 43) && !evidence.set_context({}), "Sealed identity/context changed");
            check(!evidence.add_scalar(Field::pi1, 9) && !evidence.add_row(Field::matrix_coefficients, {}), "Sealed values changed");
            evidence.seal(CanonicalFatalCode::unsupported_topology, "Second failure");
            rejects([&] { canonical_fail(evidence, CanonicalFatalCode::unsupported_configuration, "Third failure"); }, evidence);
            check(encoded(evidence) == before, "First failure bytes changed");
        }, passed);
        test("caller arrays and identity strings are copied before their retirement", [] {
            auto evidence = witness(); std::array<double, 3> values{1, -2, 3}; std::string identity = "caller-owned";
            check(evidence.add_row(Field::matrix_coefficients, values, 5) && evidence.set_identity(identity, 7), "Cannot copy caller data");
            values.fill(99); identity.assign("replacement"); evidence.seal(precision, "Owned witness");
            check(evidence.rows()[0].numbers[1].bits == std::bit_cast<std::uint64_t>(-2.0), "Row borrowed caller storage");
            check(evidence.model_identity() == "caller-owned", "Identity borrowed caller storage");
        }, passed);
        test("scalar capacity exhaustion is explicit and retains all earlier values", [] {
            auto evidence = witness();
            for (std::size_t i = 0; i < canonical_witness_scalar_capacity; ++i)
                check(evidence.add_scalar(Field::matrix_products, static_cast<double>(i), static_cast<std::uint32_t>(i)), "Premature capacity failure");
            check(!evidence.add_scalar(Field::pi1, 1), "Scalar overflow accepted");
            check(evidence.failed() && !evidence.capture_valid() && evidence.scalars().size() == 248, "Overflow not explicit");
            check(evidence.scalars().back().number.bits == std::bit_cast<std::uint64_t>(247.0), "Last retained scalar changed");
        }, passed);
        test("row count and length limits reject before storing truncated rows", [] {
            auto evidence = witness(); std::array<double, 33> row{};
            for (std::size_t i = 0; i < 8; ++i) check(evidence.add_row(Field::matrix_coefficients, row), "Premature row limit");
            check(!evidence.add_row(Field::matrix_coefficients, row) && evidence.rows().size() == 8, "Row overflow accepted");
            evidence.reset(); std::array<double, 34> long_row{};
            check(!evidence.add_row(Field::matrix_coefficients, long_row) && evidence.rows().empty() && !evidence.capture_valid(), "Long row truncated");
        }, passed);
        test("invalid enum and metadata inputs produce explicit capture failures", [] {
            auto evidence = witness();
            check(!evidence.add_scalar(Field::count, 1) && !evidence.capture_valid(), "Unknown field accepted");
            evidence.reset(); check(!evidence.begin_stage(CanonicalStage::count), "Unknown stage accepted");
            evidence.reset(); check(!evidence.set_identity("bad\nidentity", 0), "Control byte accepted");
            evidence.reset(); check(!evidence.set_identity(std::string(257, 'a'), 0), "Oversized identity accepted");
            evidence.reset(); evidence.seal(precision, std::string(257, 'a'));
            check(evidence.failed() && !evidence.capture_valid(), "Oversized reason accepted");
        }, passed);
        test("small output buffers report failure separately and leave evidence unchanged", [] {
            auto evidence = witness(); evidence.seal(precision, "Encoding buffer control"); const auto before = encoded(evidence);
            std::array<char, 8> tiny{}; const auto status = encode_canonical_witness_json(evidence, tiny);
            check(status.result == CanonicalEncodingResult::buffer_too_small && status.written == 0, "Partial encoding reported success");
            const auto empty = encode_canonical_witness_json(evidence, {});
            check(empty.result == CanonicalEncodingResult::buffer_too_small && empty.written == 0, "Empty encoding reported success");
            check(encoded(evidence) == before, "Encoding failure changed evidence");
        }, passed);
        test("overlapping output is rejected before it can overwrite sealed evidence", [] {
            auto evidence = witness(); evidence.seal(precision, "Overlap control"); const auto before = encoded(evidence);
            auto bytes = std::span<char>(reinterpret_cast<char*>(&evidence), sizeof(evidence));
            const auto status = encode_canonical_witness_json(evidence, bytes);
            check(status.result == CanonicalEncodingResult::invalid_output && status.written == 0, "Overlapping encoding accepted");
            check(encoded(evidence) == before, "Overlap corrupted evidence");
        }, passed);
        test("nonfinite numbers encode raw payloads with null decimals and valid escaping", [&] {
            auto evidence = witness();
            check(evidence.add_scalar(Field::pi0, std::bit_cast<double>(UINT64_C(0x7ff8000000012345))), "Cannot record NaN");
            check(evidence.add_scalar(Field::pi1, -std::numeric_limits<double>::infinity()), "Cannot record infinity");
            evidence.seal(precision, "Quoted \"reason\" with \\ path"); const auto json = encoded(evidence);
            check(json.find("7ff8000000012345\",null]") != std::string::npos, "NaN payload lost");
            check(json.find("fff0000000000000\",null]") != std::string::npos, "Infinity became a decimal");
            check(json.find("Quoted \\\"reason\\\" with \\\\ path") != std::string::npos, "JSON escaping failed");
            nonfinite_control_json = json;
        }, passed);
        test("stage preparation retires scratch values while retaining model and completed-step identity", [] {
            auto evidence = witness(); check(evidence.add_scalar(Field::pi0, 3), "Cannot record scratch");
            check(evidence.begin_stage(CanonicalStage::constrained_impulse), "Cannot advance stage");
            check(evidence.numeric_slots() == 0 && evidence.context().node == canonical_absent_index, "Stale scratch retained");
            check(evidence.completed_step() == 42 && !evidence.model_identity().empty(), "Model identity retired with scratch");
        }, passed);
        test("arithmetic preflight is read-only and records its actual observations", [] {
            const auto before = read_canonical_arithmetic_environment(); auto evidence = witness();
            if (before.supported()) require_canonical_arithmetic_environment(evidence);
            else rejects([&] { require_canonical_arithmetic_environment(evidence); }, evidence);
            const auto after = read_canonical_arithmetic_environment();
            check(before.rounding_mode == after.rounding_mode && before.mxcsr == after.mxcsr, "Preflight changed floating controls");
            check(evidence.scalars().size() == 4 && evidence.scalars()[1].number.bits ==
                std::bit_cast<std::uint64_t>(static_cast<double>(before.mxcsr)), "Preflight observation missing");
        }, passed);
        test("non-nearest test environment rejects without the library restoring it", [] {
            EnvironmentGuard guard; check(std::fesetround(FE_UPWARD) == 0, "Cannot set test rounding mode");
            const auto before = read_canonical_arithmetic_environment(); auto evidence = witness();
            rejects([&] { require_canonical_arithmetic_environment(evidence); }, evidence);
            const auto after = read_canonical_arithmetic_environment();
            check(!before.nearest && before.rounding_mode == after.rounding_mode && before.mxcsr == after.mxcsr, "Library changed rounding mode");
        }, passed);
        test("FTZ DAZ and unmasked SSE controls reject while raw subnormal evidence survives", [&] {
#if defined(_M_X64) || defined(__x86_64__)
            std::size_t control = 0;
            for (const unsigned int mask : {0x8000U, 0x0040U, 0x10000U}) {
                EnvironmentGuard guard;
                const auto base = _mm_getcsr();
                const auto changed = mask == 0x10000U ? base & ~0x0080U : base | mask;
                _mm_setcsr(changed); auto evidence = witness();
                rejects([&] { require_canonical_arithmetic_environment(evidence); }, evidence);
                check(_mm_getcsr() == changed, "Library changed unsupported MXCSR");
                const auto raw = CanonicalDiagnosticNumber::capture(std::bit_cast<double>(UINT64_C(1)));
                check(raw.bits == 1 && raw.has(CanonicalDiagnosticNumber::subnormal) && !raw.has(CanonicalDiagnosticNumber::zero), "Trace flushed in diagnostic capture");
                auto trace = witness();
                check(trace.add_scalar(Field::mass, std::bit_cast<double>(UINT64_C(1))), "Cannot retain subnormal under unsupported controls");
                trace.seal(precision, "Raw subnormal encoding control");
                environment_trace_json[control++] = encoded(trace);
                check(environment_trace_json[control - 1].find("0000000000000001\",4.9406564584124654e-324]") != std::string::npos,
                    "Finite subnormal decimal was flushed during encoding");
            }
#else
            check(!read_canonical_arithmetic_environment().supported(), "Unsupported architecture passed preflight");
#endif
        }, passed);
        test("explicit reset permits a later operation without keeping the previous failure", [] {
            auto evidence = witness(); evidence.seal(precision, "Retired operation"); evidence.reset();
            check(!evidence.failed() && evidence.capture_valid() && evidence.numeric_slots() == 0 && evidence.model_identity().empty(), "Reset retained failure");
            check(evidence.begin_stage(CanonicalStage::initialization), "Reset witness unusable");
        }, passed);
        test("finite decimal boundary patterns remain encodable under upward rounding with FTZ and DAZ", [&] {
            EnvironmentGuard guard;
            check(std::fesetround(FE_UPWARD) == 0, "Cannot set boundary test rounding mode");
#if defined(_M_X64) || defined(__x86_64__)
            _mm_setcsr(_mm_getcsr() | 0x8040U | 0x1f80U);
#endif
            const auto before = read_canonical_arithmetic_environment();
            auto evidence = witness(); std::uint32_t index = 0;
            constexpr std::array<unsigned, 11> exponents{0, 1, 2, 997, 998, 999, 1009, 1023, 1039, 2045, 2046};
            constexpr std::array<std::uint64_t, 5> mantissas{0, 1, UINT64_C(0x0007ffffffffffff),
                UINT64_C(0x0008000000000000), UINT64_C(0x000fffffffffffff)};
            for (const auto exponent : exponents) for (const auto mantissa : mantissas)
                for (const auto sign : {UINT64_C(0), UINT64_C(0x8000000000000000)}) {
                    const auto bits = sign | (static_cast<std::uint64_t>(exponent) << 52) | mantissa;
                    check(evidence.add_scalar(Field::mass, std::bit_cast<double>(bits), index++), "Boundary capture failed");
                }
            constexpr std::array<std::uint64_t, 5> centers{std::bit_cast<std::uint64_t>(.1),
                std::bit_cast<std::uint64_t>(1e-4), std::bit_cast<std::uint64_t>(1e16),
                std::bit_cast<std::uint64_t>(1e17), std::bit_cast<std::uint64_t>(1e-5)};
            for (const auto center : centers) for (const auto offset : {UINT64_C(0), UINT64_C(1), UINT64_C(2)})
                check(evidence.add_scalar(Field::mass, std::bit_cast<double>(center + offset - 1), index++), "Decimal-neighbor capture failed");
            evidence.seal(precision, "Decimal boundary bit-pattern control");
            boundary_control_json = encoded(evidence);
            const auto after = read_canonical_arithmetic_environment();
            check(index == 125 && evidence.numeric_slots() == 125, "Boundary fixture coverage changed");
            check(before.rounding_mode == after.rounding_mode && before.mxcsr == after.mxcsr, "Decimal encoding changed floating controls");
        }, passed);

        // One maximal witness is emitted for an independent JSON/parser check.
        // It is diagnostic fixture data, not a mechanical or thermodynamic case.
        auto maximum = witness();
        check(maximum.set_identity(std::string(256, '\\'), UINT64_MAX), "Maximum identity rejected");
        std::array<double, 33> row{}; row.fill(-std::numeric_limits<double>::max());
        for (std::size_t i = 0; i < 248; ++i)
            check(maximum.add_scalar(Field::matrix_products, -std::numeric_limits<double>::max(), UINT32_MAX - 1), "Maximum scalar capacity rejected");
        for (std::size_t i = 0; i < 8; ++i) check(maximum.add_row(Field::matrix_coefficients, row, UINT32_MAX - 1), "Maximum row capacity rejected");
        maximum.seal(precision, std::string(256, '"'));
        const auto full_json = encoded(maximum);
        check(maximum.numeric_slots() == 512 && full_json.size() <= canonical_witness_json_capacity, "Maximum witness exceeded bound");
        std::cout << "{\"Passed\":true,\"Tests\":" << passed << ",\"MaximalWitness\":" << full_json
            << ",\"NonfiniteWitness\":" << nonfinite_control_json << ",\"EnvironmentTraceWitnesses\":[";
        bool separator = false;
        for (const auto& json : environment_trace_json) {
            if (json.empty()) continue;
            if (separator) std::cout << ',';
            separator = true; std::cout << json;
        }
        std::cout << "],\"DecimalBoundaryWitness\":" << boundary_control_json << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Canonical diagnostics control failed: " << error.what() << '\n'; return 1;
    }
}
