#include "mixed_atmospheres/experimental/canonical_material_diagnostics.hpp"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <charconv>
#include <limits>
#include <system_error>

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ma::experimental {
namespace {
static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::radix == 2 &&
    std::numeric_limits<double>::digits == 53);
static_assert(canonical_witness_scalar_capacity + canonical_witness_row_capacity *
    canonical_witness_row_length == canonical_witness_numeric_capacity);

bool printable(std::string_view value) noexcept {
    return value.size() <= 256 && std::all_of(value.begin(), value.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte >= 32 && byte <= 126;
    });
}
std::array<char, 257> copied_text(std::string_view value) noexcept {
    std::array<char, 257> result{};
    std::copy(value.begin(), value.end(), result.begin());
    return result;
}
bool valid_field(CanonicalWitnessField field) noexcept {
    return static_cast<std::uint16_t>(field) < static_cast<std::uint16_t>(CanonicalWitnessField::count);
}

// Exact coefficient construction and decimal rounding use integer operations
// only. Floating formatting can classify a subnormal as zero under DAZ before
// it reaches a bit-based conversion algorithm.
class DecimalCoefficient {
public:
    explicit DecimalCoefficient(std::uint64_t value) noexcept {
        while (value != 0) {
            limbs_[size_++] = static_cast<std::uint32_t>(value % base);
            value /= base;
        }
    }
    bool multiply(std::uint32_t factor) noexcept {
        // factor <= 2^29 < base, so each intermediate is less than 2^59.
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < size_; ++i) {
            const auto product = static_cast<std::uint64_t>(limbs_[i]) * factor + carry;
            limbs_[i] = static_cast<std::uint32_t>(product % base); carry = product / base;
        }
        if (carry != 0) {
            if (size_ == limbs_.size()) return false;
            limbs_[size_++] = static_cast<std::uint32_t>(carry);
        }
        return true;
    }
    std::size_t digits(std::span<char> output) const noexcept {
        if (size_ == 0) return 0;
        const auto head = std::to_chars(output.data(), output.data() + output.size(), limbs_[size_ - 1]);
        if (head.ec != std::errc{}) return 0;
        auto used = static_cast<std::size_t>(head.ptr - output.data());
        for (std::size_t i = size_ - 1; i != 0; --i) {
            if (output.size() - used < 9) return 0;
            auto value = limbs_[i - 1];
            for (std::size_t digit = 9; digit != 0; --digit) {
                output[used + digit - 1] = static_cast<char>('0' + value % 10U); value /= 10U;
            }
            used += 9;
        }
        return used;
    }
private:
    static constexpr std::uint64_t base = 1000000000;
    // Binary64 needs at most 767 exact coefficient digits; storage holds 864.
    std::array<std::uint32_t, 96> limbs_{};
    std::size_t size_ = 0;
};

std::size_t decimal_from_bits(std::uint64_t bits, std::span<char> output) noexcept {
    if (output.size() < 64) return 0;
    const bool negative = (bits >> 63) != 0;
    const auto exponent = static_cast<unsigned>((bits >> 52) & 2047U);
    const auto fraction = bits & UINT64_C(0x000fffffffffffff);
    if (exponent == 2047) return 0;
    char* next = output.data();
    if (negative) *next++ = '-';
    if (exponent == 0 && fraction == 0) { *next++ = '0'; return static_cast<std::size_t>(next - output.data()); }
    const auto significand = exponent == 0 ? fraction : fraction | UINT64_C(0x0010000000000000);
    const int power = exponent == 0 ? -1074 : static_cast<int>(exponent) - 1075;
    DecimalCoefficient coefficient(significand);
    int decimal_scale = 0;
    if (power >= 0) {
        auto remaining = static_cast<unsigned>(power);
        while (remaining >= 29) { if (!coefficient.multiply(536870912)) return 0; remaining -= 29; }
        if (remaining != 0 && !coefficient.multiply(1U << remaining)) return 0;
    } else {
        auto remaining = static_cast<unsigned>(-power);
        decimal_scale = power;
        while (remaining >= 12) { if (!coefficient.multiply(244140625)) return 0; remaining -= 12; }
        std::uint32_t factor = 1;
        for (unsigned i = 0; i < remaining; ++i) factor *= 5;
        if (factor != 1 && !coefficient.multiply(factor)) return 0;
    }
    std::array<char, 864> digits{};
    const auto exact_length = coefficient.digits(digits);
    if (exact_length == 0) return 0;
    int scientific_exponent = static_cast<int>(exact_length) - 1 + decimal_scale;
    auto length = std::min(exact_length, std::size_t{17});
    if (exact_length > 17) {
        bool tail_nonzero = false;
        for (std::size_t i = 18; i < exact_length; ++i) tail_nonzero = tail_nonzero || digits[i] != '0';
        const bool round_up = digits[17] > '5' || (digits[17] == '5' && (tail_nonzero || ((digits[16] - '0') & 1) != 0));
        if (round_up) {
            std::size_t i = 17;
            while (i != 0 && digits[i - 1] == '9') digits[--i] = '0';
            if (i == 0) { digits[0] = '1'; ++scientific_exponent; }
            else ++digits[i - 1];
        }
    }
    if (scientific_exponent < -324 || scientific_exponent > 308) return 0;
    while (length > 1 && digits[length - 1] == '0') --length;
    if (scientific_exponent < -4 || scientific_exponent >= 17) {
        *next++ = digits[0];
        if (length > 1) { *next++ = '.'; for (std::size_t i = 1; i < length; ++i) *next++ = digits[i]; }
        *next++ = 'e'; *next++ = scientific_exponent < 0 ? '-' : '+';
        const auto magnitude = static_cast<unsigned>(scientific_exponent < 0 ? -scientific_exponent : scientific_exponent);
        if (magnitude < 10) *next++ = '0';
        const auto result = std::to_chars(next, output.data() + output.size(), magnitude);
        if (result.ec != std::errc{}) return 0;
        next = result.ptr;
    } else {
        const int point = scientific_exponent + 1;
        if (point <= 0) {
            *next++ = '0'; *next++ = '.';
            for (int i = 0; i < -point; ++i) *next++ = '0';
            for (std::size_t i = 0; i < length; ++i) *next++ = digits[i];
        } else {
            const auto whole = static_cast<std::size_t>(point);
            for (std::size_t i = 0; i < whole; ++i) *next++ = i < length ? digits[i] : '0';
            if (whole < length) { *next++ = '.'; for (std::size_t i = whole; i < length; ++i) *next++ = digits[i]; }
        }
    }
    return static_cast<std::size_t>(next - output.data());
}

class Writer {
public:
    explicit Writer(std::span<char> output) noexcept : output_(output.first(std::min(output.size(), canonical_witness_json_capacity))) {}
    void text(std::string_view value) noexcept {
        if (status_ != CanonicalEncodingResult::success) return;
        if (value.size() > output_.size() - used_) { status_ = CanonicalEncodingResult::buffer_too_small; return; }
        std::copy(value.begin(), value.end(), output_.begin() + static_cast<std::ptrdiff_t>(used_));
        used_ += value.size();
    }
    void integer(std::uint64_t value) noexcept {
        std::array<char, 32> buffer{};
        const auto encoded = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (encoded.ec != std::errc{}) { status_ = CanonicalEncodingResult::encoding_failure; return; }
        text({buffer.data(), static_cast<std::size_t>(encoded.ptr - buffer.data())});
    }
    void index(std::uint32_t value) noexcept {
        if (value == canonical_absent_index) text("null"); else integer(value);
    }
    void boolean(bool value) noexcept { text(value ? "true" : "false"); }
    void quoted(std::string_view value) noexcept {
        text("\"");
        for (char c : value) {
            if (c == '\\' || c == '"') text("\\");
            text({&c, 1});
        }
        text("\"");
    }
    void number(const CanonicalDiagnosticNumber& value) noexcept {
        constexpr char hex[] = "0123456789abcdef";
        std::array<char, 16> raw{};
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const auto shift = static_cast<unsigned>((raw.size() - 1 - i) * 4);
            raw[i] = hex[(value.bits >> shift) & 15U];
        }
        text("["); integer(value.flags); text(",\""); text({raw.data(), raw.size()}); text("\",");
        if (value.has(CanonicalDiagnosticNumber::present) && value.has(CanonicalDiagnosticNumber::finite)) {
            std::array<char, 64> buffer{};
            const auto length = decimal_from_bits(value.bits, buffer);
            if (length == 0) { status_ = CanonicalEncodingResult::encoding_failure; return; }
            text({buffer.data(), length});
        } else text("null");
        text("]");
    }
    [[nodiscard]] CanonicalEncodingResult status() const noexcept { return status_; }
    [[nodiscard]] std::size_t used() const noexcept { return used_; }
private:
    std::span<char> output_;
    std::size_t used_ = 0;
    CanonicalEncodingResult status_ = CanonicalEncodingResult::success;
};
}

CanonicalDiagnosticNumber CanonicalDiagnosticNumber::capture(double value) noexcept {
    CanonicalDiagnosticNumber result;
    result.bits = std::bit_cast<std::uint64_t>(value);
    const auto exponent = result.bits & UINT64_C(0x7ff0000000000000);
    const auto fraction = result.bits & UINT64_C(0x000fffffffffffff);
    result.flags = present;
    if (exponent != UINT64_C(0x7ff0000000000000)) result.flags |= finite;
    if ((result.bits >> 63) != 0) result.flags |= negative;
    if (exponent == 0 && fraction == 0) result.flags |= zero;
    if (exponent == 0 && fraction != 0) result.flags |= subnormal;
    return result;
}

void CanonicalFailureWitness::reset() noexcept { *this = CanonicalFailureWitness{}; }
void CanonicalFailureWitness::invalid_capture(std::string_view reason) noexcept {
    if (failed_) return;
    capture_valid_ = false;
    code_ = CanonicalFatalCode::unsupported_configuration;
    reason_ = copied_text(reason); // Only bounded, internal literals reach here.
    failed_ = true;
}
bool CanonicalFailureWitness::begin_stage(CanonicalStage stage) noexcept {
    if (failed_) return false;
    if (stage == CanonicalStage::none || static_cast<std::uint8_t>(stage) >= static_cast<std::uint8_t>(CanonicalStage::count)) {
        invalid_capture("Invalid canonical witness stage"); return false;
    }
    stage_ = stage; scalar_count_ = 0; row_count_ = 0; context_ = {};
    return true;
}
bool CanonicalFailureWitness::set_identity(std::string_view identity, std::uint64_t completed_step) noexcept {
    if (failed_) return false;
    if (identity.empty() || !printable(identity)) {
        invalid_capture("Witness identity must be 1 to 256 printable ASCII bytes"); return false;
    }
    identity_ = copied_text(identity); completed_step_ = completed_step;
    return true;
}
bool CanonicalFailureWitness::set_context(const CanonicalWitnessContext& context) noexcept {
    if (failed_) return false;
    context_ = context; return true;
}
bool CanonicalFailureWitness::add_scalar(CanonicalWitnessField field, double value, std::uint32_t index) noexcept {
    if (failed_) return false;
    if (!valid_field(field)) { invalid_capture("Invalid canonical witness field"); return false; }
    if (scalar_count_ == scalars_.size()) { invalid_capture("Canonical witness scalar capacity exhausted"); return false; }
    scalars_[scalar_count_++] = {field, index, CanonicalDiagnosticNumber::capture(value)};
    return true;
}
bool CanonicalFailureWitness::add_row(CanonicalWitnessField field, std::span<const double> values, std::uint32_t index) noexcept {
    if (failed_) return false;
    if (!valid_field(field)) { invalid_capture("Invalid canonical witness field"); return false; }
    if (values.size() > canonical_witness_row_length) { invalid_capture("Canonical witness row exceeds 33 entries"); return false; }
    if (row_count_ == rows_.size()) { invalid_capture("Canonical witness row capacity exhausted"); return false; }
    auto& row = rows_[row_count_++]; row.field = field; row.index = index; row.length = values.size();
    for (std::size_t i = 0; i < values.size(); ++i) row.numbers[i] = CanonicalDiagnosticNumber::capture(values[i]);
    return true;
}
void CanonicalFailureWitness::seal(CanonicalFatalCode code, std::string_view reason) noexcept {
    if (failed_) return;
    if (static_cast<std::uint8_t>(code) > static_cast<std::uint8_t>(CanonicalFatalCode::numerical_precision)) {
        invalid_capture("Invalid canonical fatal code"); return;
    }
    if (reason.empty() || !printable(reason)) {
        invalid_capture("Witness reason must be 1 to 256 printable ASCII bytes"); return;
    }
    code_ = code; reason_ = copied_text(reason); failed_ = true;
}
std::size_t CanonicalFailureWitness::numeric_slots() const noexcept {
    std::size_t count = scalar_count_;
    for (std::size_t i = 0; i < row_count_; ++i) count += rows_[i].length;
    return count;
}

CanonicalFailure::CanonicalFailure(const CanonicalFailureWitness& witness) noexcept : code_(witness.code()), stage_(witness.stage()) {
    reason_ = copied_text(witness.reason());
}
[[noreturn]] void canonical_fail(CanonicalFailureWitness& witness, CanonicalFatalCode code, std::string_view reason) {
    witness.seal(code, reason);
    throw CanonicalFailure(witness);
}

CanonicalEncodingStatus encode_canonical_witness_json(const CanonicalFailureWitness& witness,
    std::span<char> output) noexcept {
    const auto source_address = reinterpret_cast<std::uintptr_t>(&witness);
    const auto target_address = reinterpret_cast<std::uintptr_t>(output.data());
    const auto capacity = std::min(output.size(), canonical_witness_json_capacity);
    if (capacity != 0 && (target_address >= source_address ? target_address - source_address < sizeof(witness) :
        source_address - target_address < capacity)) return {CanonicalEncodingResult::invalid_output, 0};
    Writer writer(output);
    writer.text("{\"schema\":\"ma-canonical-failure-witness-v1\",\"failed\":"); writer.boolean(witness.failed());
    writer.text(",\"capture_valid\":"); writer.boolean(witness.capture_valid());
    writer.text(",\"stage\":"); writer.integer(static_cast<std::uint8_t>(witness.stage()));
    writer.text(",\"code\":");
    if (witness.failed()) writer.integer(static_cast<std::uint8_t>(witness.code())); else writer.text("null");
    writer.text(",\"reason\":"); writer.quoted(witness.reason());
    writer.text(",\"model_identity\":"); writer.quoted(witness.model_identity());
    writer.text(",\"completed_step\":"); writer.integer(witness.completed_step());
    const auto& context = witness.context();
    writer.text(",\"context\":{\"node\":"); writer.index(context.node);
    writer.text(",\"owner\":"); writer.index(context.owner);
    writer.text(",\"newton_updates\":"); writer.integer(context.newton_updates);
    writer.text(",\"backtracks\":"); writer.integer(context.backtracks);
    writer.text(",\"factorizations\":"); writer.integer(context.factorizations);
    writer.text(",\"top_level_queries\":"); writer.integer(context.top_level_queries);
    writer.text(",\"constitutive_charges\":"); writer.integer(context.constitutive_charges);
    writer.text(",\"oracle_iterations\":"); writer.integer(context.oracle_iterations);
    writer.text(",\"predicate_sequence\":"); writer.integer(context.predicate_sequence);
    writer.text("},\"numeric_slots\":"); writer.integer(witness.numeric_slots());
    writer.text(",\"scalars\":[");
    bool separator = false;
    for (const auto& scalar : witness.scalars()) {
        if (separator) writer.text(",");
        separator = true;
        writer.text("["); writer.integer(static_cast<std::uint16_t>(scalar.field)); writer.text(","); writer.index(scalar.index);
        writer.text(","); writer.number(scalar.number); writer.text("]");
    }
    writer.text("],\"rows\":["); separator = false;
    for (const auto& row : witness.rows()) {
        if (separator) writer.text(",");
        separator = true;
        writer.text("["); writer.integer(static_cast<std::uint16_t>(row.field)); writer.text(","); writer.index(row.index); writer.text(",[");
        for (std::size_t i = 0; i < row.length; ++i) { if (i != 0) writer.text(","); writer.number(row.numbers[i]); }
        writer.text("]]");
    }
    writer.text("]}");
    return {writer.status(), writer.status() == CanonicalEncodingResult::success ? writer.used() : 0};
}

bool CanonicalArithmeticEnvironment::supported() const noexcept {
    return binary64_supported && x86_supported && nearest && ftz_disabled && daz_disabled && exceptions_masked;
}
CanonicalArithmeticEnvironment read_canonical_arithmetic_environment() noexcept {
    CanonicalArithmeticEnvironment result;
    result.binary64_supported = std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53;
    result.rounding_mode = std::fegetround();
#if defined(_M_X64) || defined(__x86_64__)
    result.x86_supported = true;
    result.mxcsr = _mm_getcsr();
    result.nearest = result.rounding_mode == FE_TONEAREST && (result.mxcsr & 0x6000U) == 0;
    result.ftz_disabled = (result.mxcsr & 0x8000U) == 0;
    result.daz_disabled = (result.mxcsr & 0x0040U) == 0;
    result.exceptions_masked = (result.mxcsr & 0x1f80U) == 0x1f80U;
#endif
    return result;
}
void require_canonical_arithmetic_environment(CanonicalFailureWitness& witness) {
    if (witness.failed()) canonical_fail(witness, witness.code(), witness.reason());
    if (!witness.begin_stage(CanonicalStage::preflight)) canonical_fail(witness, witness.code(), witness.reason());
    const auto environment = read_canonical_arithmetic_environment();
    const bool captured = witness.add_scalar(CanonicalWitnessField::rounding_mode, environment.rounding_mode) &&
        witness.add_scalar(CanonicalWitnessField::mxcsr, environment.mxcsr) &&
        witness.add_scalar(CanonicalWitnessField::binary64_supported, environment.binary64_supported ? 1 : 0) &&
        witness.add_scalar(CanonicalWitnessField::x86_supported, environment.x86_supported ? 1 : 0);
    if (!captured) canonical_fail(witness, witness.code(), witness.reason());
    if (!environment.supported()) canonical_fail(witness, CanonicalFatalCode::numerical_precision,
        "Canonical arithmetic requires binary64 x86-64 nearest rounding, FTZ/DAZ off and masked SSE exceptions");
}
}
