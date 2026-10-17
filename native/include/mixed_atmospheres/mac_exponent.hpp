#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace ma::experimental {
// Exact signed exponent only; significands remain binary64. Ordinary values
// stay inline. Larger immutable magnitudes use bounded shared limb storage.
class MacExponent {
public:
    static constexpr std::size_t maximum_words = 2048; // 65,536 magnitude bits.
    static constexpr std::size_t maximum_decimal_characters = 20000;
    MacExponent() noexcept = default;
    MacExponent(std::int64_t value) noexcept;
    MacExponent(const MacExponent&) noexcept;
    MacExponent(MacExponent&&) noexcept;
    MacExponent& operator=(const MacExponent&) noexcept;
    MacExponent& operator=(MacExponent&&) noexcept;
    ~MacExponent();
    [[nodiscard]] static MacExponent from_decimal(std::string_view);
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::int64_t to_int64_exact() const;
    [[nodiscard]] int to_int_exact() const;
    [[nodiscard]] bool uses_large_storage() const noexcept { return large_; }
    [[nodiscard]] int sign() const noexcept;
    [[nodiscard]] MacExponent negated() const;
    [[nodiscard]] MacExponent doubled() const;
    [[nodiscard]] bool is_odd() const noexcept;
    // Exact floor division, including negative odd and out-of-int64 values.
    [[nodiscard]] MacExponent halved_floor() const;
    void swap(MacExponent&) noexcept;
    friend MacExponent operator+(const MacExponent&, const MacExponent&);
    friend MacExponent operator-(const MacExponent& a, const MacExponent& b) { return a + b.negated(); }
    friend std::strong_ordering operator<=>(const MacExponent&, const MacExponent&) noexcept;
    friend bool operator==(const MacExponent& a, const MacExponent& b) noexcept { return (a <=> b) == 0; }
    friend std::ostream& operator<<(std::ostream&, const MacExponent&);
private:
    struct Large;
    union Storage { std::int64_t small = 0; Large* large; } storage_;
    bool large_ = false;
    void retain() const noexcept;
    void release() noexcept;
    [[nodiscard]] std::vector<std::uint32_t> magnitude_words() const;
    [[nodiscard]] static MacExponent from_words(bool negative, std::vector<std::uint32_t>);
};
}
