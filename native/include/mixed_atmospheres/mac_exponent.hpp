#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace ma::experimental {
// Exact signed exponent only; significands remain binary64. Ordinary values
// stay inline. Larger immutable magnitudes use bounded shared limb storage.
class MacExponent {
public:
    static constexpr std::size_t maximum_words = 2048; // 65,536 magnitude bits.
    static constexpr std::size_t maximum_decimal_characters = 20000;
    MacExponent() noexcept = default;
    MacExponent(std::int64_t value) noexcept { storage_.small = value; }
    MacExponent(const MacExponent& other) noexcept : storage_(other.storage_), large_(other.large_) { if (large_) retain(); }
    MacExponent(MacExponent&& other) noexcept : storage_(other.storage_), large_(other.large_) { other.storage_.small = 0; other.large_ = false; }
    MacExponent& operator=(const MacExponent& other) noexcept {
        if (this != &other) { MacExponent copy(other); swap(copy); } return *this;
    }
    MacExponent& operator=(MacExponent&& other) noexcept {
        if (this != &other) { if (large_) release(); storage_ = other.storage_; large_ = other.large_; other.storage_.small = 0; other.large_ = false; } return *this;
    }
    ~MacExponent() { if (large_) release(); }
    [[nodiscard]] static MacExponent from_decimal(std::string_view);
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::int64_t to_int64_exact() const {
        if (!large_) return storage_.small;
        return int64_projection_error();
    }
    [[nodiscard]] int to_int_exact() const {
        if (!large_ && storage_.small >= std::numeric_limits<int>::min() && storage_.small <= std::numeric_limits<int>::max())
            return static_cast<int>(storage_.small);
        return int_projection_error();
    }
    [[nodiscard]] bool uses_large_storage() const noexcept { return large_; }
    [[nodiscard]] int sign() const noexcept;
    [[nodiscard]] MacExponent negated() const {
        if (!large_ && storage_.small != std::numeric_limits<std::int64_t>::min()) return -storage_.small;
        return negated_large();
    }
    [[nodiscard]] MacExponent doubled() const { return *this + *this; }
    [[nodiscard]] bool is_odd() const noexcept;
    // Exact floor division, including negative odd and out-of-int64 values.
    [[nodiscard]] MacExponent halved_floor() const;
    void swap(MacExponent& other) noexcept { std::swap(storage_, other.storage_); std::swap(large_, other.large_); }
    friend MacExponent operator+(const MacExponent& a, const MacExponent& b) {
        if (!a.large_ && !b.large_) {
            const auto x = a.storage_.small, y = b.storage_.small;
            if (!((y > 0 && x > std::numeric_limits<std::int64_t>::max() - y) ||
                  (y < 0 && x < std::numeric_limits<std::int64_t>::min() - y))) return x + y;
        }
        return add_large(a, b);
    }
    friend MacExponent operator-(const MacExponent& a, const MacExponent& b) { return a + b.negated(); }
    friend std::strong_ordering operator<=>(const MacExponent& a, const MacExponent& b) noexcept {
        if (!a.large_ && !b.large_) return a.storage_.small <=> b.storage_.small;
        return compare_large(a, b);
    }
    friend bool operator==(const MacExponent& a, const MacExponent& b) noexcept { return (a <=> b) == 0; }
    friend std::ostream& operator<<(std::ostream&, const MacExponent&);
private:
    struct Large;
    union Storage { std::int64_t small = 0; Large* large; } storage_;
    bool large_ = false;
    void retain() const noexcept;
    void release() noexcept;
    [[nodiscard]] MacExponent negated_large() const;
    [[nodiscard]] static MacExponent add_large(const MacExponent&, const MacExponent&);
    [[nodiscard]] static std::strong_ordering compare_large(const MacExponent&, const MacExponent&) noexcept;
    [[noreturn]] static std::int64_t int64_projection_error();
    [[noreturn]] static int int_projection_error();
    [[nodiscard]] std::vector<std::uint32_t> magnitude_words() const;
    [[nodiscard]] static MacExponent from_words(bool negative, std::vector<std::uint32_t>);
};
}
