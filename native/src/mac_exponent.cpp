#include "mixed_atmospheres/mac_exponent.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace ma::experimental {
namespace {
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
void trim(std::vector<std::uint32_t>& words) { while (!words.empty() && words.back() == 0) words.pop_back(); }
int compare_words(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) noexcept {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i > 0; --i) if (a[i-1] != b[i-1]) return a[i-1] < b[i-1] ? -1 : 1;
    return 0;
}
}
struct MacExponent::Large {
    std::atomic<std::size_t> references{1};
    bool negative;
    std::vector<std::uint32_t> words;
    Large(bool negative_value, std::vector<std::uint32_t> value) : negative(negative_value), words(std::move(value)) {}
};
MacExponent::MacExponent(std::int64_t value) noexcept { storage_.small = value; }
void MacExponent::retain() const noexcept { if (large_) static_cast<void>(storage_.large->references.fetch_add(1, std::memory_order_relaxed)); }
void MacExponent::release() noexcept {
    if (large_ && storage_.large->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete storage_.large;
}
MacExponent::MacExponent(const MacExponent& other) noexcept : storage_(other.storage_), large_(other.large_) { retain(); }
MacExponent::MacExponent(MacExponent&& other) noexcept : storage_(other.storage_), large_(other.large_) { other.storage_.small = 0; other.large_ = false; }
MacExponent& MacExponent::operator=(const MacExponent& other) noexcept { if (this != &other) { MacExponent copy(other); swap(copy); } return *this; }
MacExponent& MacExponent::operator=(MacExponent&& other) noexcept {
    if (this != &other) { release(); storage_ = other.storage_; large_ = other.large_; other.storage_.small = 0; other.large_ = false; } return *this;
}
MacExponent::~MacExponent() { release(); }
void MacExponent::swap(MacExponent& other) noexcept { std::swap(storage_, other.storage_); std::swap(large_, other.large_); }
int MacExponent::sign() const noexcept { return large_ ? (storage_.large->negative ? -1 : 1) : (storage_.small < 0 ? -1 : storage_.small > 0 ? 1 : 0); }
std::vector<std::uint32_t> MacExponent::magnitude_words() const {
    if (large_) return storage_.large->words;
    const auto value = storage_.small < 0 ? UINT64_C(0) - static_cast<std::uint64_t>(storage_.small) : static_cast<std::uint64_t>(storage_.small);
    if (value == 0) return {};
    std::vector<std::uint32_t> words{static_cast<std::uint32_t>(value)};
    if (value >> 32) words.push_back(static_cast<std::uint32_t>(value >> 32));
    return words;
}
MacExponent MacExponent::from_words(bool negative, std::vector<std::uint32_t> words) {
    trim(words);
    if (words.size() > maximum_words) precision("MAC exponent limb storage capacity exceeded");
    if (words.empty()) return {};
    if (words.size() <= 2) {
        const std::uint64_t value = words[0] | (words.size() == 2 ? static_cast<std::uint64_t>(words[1]) << 32 : UINT64_C(0));
        constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (!negative && value <= limit) return static_cast<std::int64_t>(value);
        if (negative && value == limit + 1) return std::numeric_limits<std::int64_t>::min();
        if (negative && value <= limit) return -static_cast<std::int64_t>(value);
    }
    MacExponent result; result.storage_.large = new Large(negative, std::move(words)); result.large_ = true; return result;
}
MacExponent MacExponent::from_decimal(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("Empty MAC exponent decimal");
    const bool negative = text.front() == '-'; if (negative) text.remove_prefix(1);
    if (text.size() > maximum_decimal_characters) precision("MAC exponent decimal input capacity exceeded");
    if (text.empty() || (text.front() == '0' && (text.size() != 1 || negative))) throw std::invalid_argument("Noncanonical MAC exponent decimal");
    std::vector<std::uint32_t> words;
    for (char digit : text) {
        if (digit < '0' || digit > '9') throw std::invalid_argument("Invalid MAC exponent decimal digit");
        std::uint64_t carry = static_cast<unsigned>(digit - '0');
        for (auto& word : words) { const auto value = static_cast<std::uint64_t>(word) * 10 + carry; word = static_cast<std::uint32_t>(value); carry = value >> 32; }
        if (carry != 0) {
            if (words.size() == maximum_words) precision("MAC exponent limb storage capacity exceeded");
            words.push_back(static_cast<std::uint32_t>(carry));
        }
    }
    return from_words(negative, std::move(words));
}
std::int64_t MacExponent::to_int64_exact() const { if (large_) precision("MAC exponent exceeds int64 projection"); return storage_.small; }
int MacExponent::to_int_exact() const {
    const auto value = to_int64_exact();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) precision("MAC exponent exceeds int projection");
    return static_cast<int>(value);
}
MacExponent MacExponent::negated() const {
    if (!large_ && storage_.small != std::numeric_limits<std::int64_t>::min()) return -storage_.small;
    return from_words(sign() >= 0, magnitude_words());
}
MacExponent MacExponent::doubled() const { return *this + *this; }
MacExponent operator+(const MacExponent& a, const MacExponent& b) {
    if (!a.large_ && !b.large_) {
        const auto x = a.storage_.small, y = b.storage_.small;
        if (!((y > 0 && x > std::numeric_limits<std::int64_t>::max() - y) || (y < 0 && x < std::numeric_limits<std::int64_t>::min() - y))) return x + y;
    }
    if (a.sign() == 0) return b;
    if (b.sign() == 0) return a;
    auto x = a.magnitude_words(), y = b.magnitude_words();
    if (a.sign() == b.sign()) {
        const auto count = std::max(x.size(), y.size()); std::vector<std::uint32_t> result(count); std::uint64_t carry = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto value = (i < x.size() ? static_cast<std::uint64_t>(x[i]) : 0) + (i < y.size() ? static_cast<std::uint64_t>(y[i]) : 0) + carry;
            result[i] = static_cast<std::uint32_t>(value); carry = value >> 32;
        }
        if (carry != 0) { if (result.size() == MacExponent::maximum_words) precision("MAC exponent limb storage capacity exceeded"); result.push_back(static_cast<std::uint32_t>(carry)); }
        return MacExponent::from_words(a.sign() < 0, std::move(result));
    }
    int order = compare_words(x, y); if (order == 0) return {};
    const bool negative = order > 0 ? a.sign() < 0 : b.sign() < 0; if (order < 0) std::swap(x, y);
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto subtrahend = (i < y.size() ? static_cast<std::uint64_t>(y[i]) : 0) + borrow;
        const auto value = static_cast<std::uint64_t>(x[i]); borrow = value < subtrahend ? 1 : 0;
        x[i] = static_cast<std::uint32_t>((borrow ? (UINT64_C(1) << 32) : 0) + value - subtrahend);
    }
    return MacExponent::from_words(negative, std::move(x));
}
std::strong_ordering operator<=>(const MacExponent& a, const MacExponent& b) noexcept {
    if (!a.large_ && !b.large_) return a.storage_.small <=> b.storage_.small;
    const int sa = a.sign(), sb = b.sign(); if (sa != sb) return sa <=> sb;
    if (a.large_ != b.large_) return (a.large_ ? sa : -sa) <=> 0;
    const int order = compare_words(a.storage_.large->words, b.storage_.large->words); return (sa * order) <=> 0;
}
std::string MacExponent::to_string() const {
    if (!large_) return std::to_string(storage_.small);
    auto words = storage_.large->words; std::vector<std::uint32_t> groups;
    while (!words.empty()) {
        std::uint64_t remainder = 0;
        for (std::size_t i = words.size(); i > 0; --i) {
            const auto value = (remainder << 32) | words[i-1]; words[i-1] = static_cast<std::uint32_t>(value / UINT64_C(1000000000)); remainder = value % UINT64_C(1000000000);
        }
        groups.push_back(static_cast<std::uint32_t>(remainder)); trim(words);
    }
    std::string result = storage_.large->negative ? "-" : ""; result += std::to_string(groups.back());
    for (std::size_t i = groups.size() - 1; i > 0; --i) { const auto part = std::to_string(groups[i-1]); result.append(9 - part.size(), '0'); result += part; }
    return result;
}
std::ostream& operator<<(std::ostream& out, const MacExponent& value) { return value.large_ ? out << value.to_string() : out << value.storage_.small; }
}
