#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_primary.hpp"
#include <array>
#include <iomanip>
#include <random>
using namespace ma::testing;
namespace {
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
std::size_t identity = 0;
void verify(double value, double remainder, double increment) {
    const auto next = add_mac_primary(value, remainder, increment);
    MacEnergySum residual;
    for (const double operand : {value, remainder, increment, -next.value, -next.remainder, -next.discarded}) residual.add(operand);
    check(residual.sign() == 0, "Primary write lost an unreported operand");
    check(canonical_mac_primary(next.value, next.remainder), "Primary write is not normalized");
    if (next.remainder == 0) check(next.discarded == 0, "Empty low part discarded a representable correction");
    else {
        const double ulp = std::nextafter(std::abs(next.remainder), std::numeric_limits<double>::infinity()) - std::abs(next.remainder);
        MacEnergySum allowance; allowance.add(std::abs(next.discarded));
        allowance.add(MacEnergy::from_double(ulp).shifted(-1).negated());
        check(allowance.sign() <= 0, "Third part exceeds half a low-part ulp");
    }
    MacEnergySum first; first.add(value); first.add(increment); first.add(-(value + increment));
    check(next.high_rounded == (first.sign() != 0), "High-part rounding flag differs from exact operands");
    std::cout << "PRADD " << identity++ << ' ' << value << ' ' << remainder << ' ' << increment << ' '
        << next.value << ' ' << next.remainder << ' ' << next.discarded << ' ' << next.high_rounded << '\n';
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        for (const auto& test : std::array<std::array<double, 3>, 8>{{
            {1, 0, 0x1p-54}, {1, 0x1p-54, 0x1p-54}, {1, 0x1p-54, -1},
            {-1, -0x1p-54, 1}, {0, 0, std::numeric_limits<double>::denorm_min()},
            {std::numeric_limits<double>::min(), 0, -std::numeric_limits<double>::denorm_min()},
            {0x1p1000, 0x1p945, 0x1p800}, {1, 0x1p-100, 0x1p-1000}}}) verify(test[0], test[1], test[2]);
        std::mt19937_64 rng(414719);
        for (std::size_t n = 0; n < 256; ++n) {
            const int exponent = static_cast<int>(rng() % 1801) - 900;
            const double value = std::ldexp(1. + static_cast<double>(rng() % 1024) / 1024., exponent) * (rng() % 2 ? 1 : -1);
            const double low = std::ldexp(static_cast<double>(static_cast<int>(rng() % 33) - 16), exponent - 58);
            const auto normalized = primary_detail::two_sum(value, low);
            const int shift = static_cast<int>(rng() % 220) - 110;
            const double increment = std::ldexp(1. + static_cast<double>(rng() % 1024) / 1024., exponent + shift) * (rng() % 2 ? 1 : -1);
            verify(normalized.high, normalized.low, increment);
        }
        double value = 0x1p40, remainder = 0, plain = value;
        for (std::size_t n = 0; n < 100000; ++n) {
            const auto next = add_mac_primary(value, remainder, 0x1p-20);
            check(next.discarded == 0, "Dyadic repeated additions lost their correction");
            value = next.value; remainder = next.remainder; plain += 0x1p-20;
        }
        MacEnergySum exact; exact.add(value); exact.add(remainder); exact.add(-0x1p40); exact.add(-100000 * 0x1p-20);
        check(exact.sign() == 0 && value > plain && plain == 0x1p40, "Primary remainder did not recover lost heat");
        bool overflow = false, malformed = false;
        try { (void)add_mac_primary(std::numeric_limits<double>::max(), 0, std::numeric_limits<double>::max()); }
        catch (const std::overflow_error&) { overflow = true; }
        try { (void)add_mac_primary(1, 1, 0); } catch (const std::invalid_argument&) { malformed = true; }
        check(overflow && malformed && !canonical_mac_primary(0, 1), "Malformed primary state was admitted");
        std::cout << "PRREPEATED " << value << ' ' << remainder << ' ' << plain << " 100000\nPRPASS " << identity << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
