#include "mixed_atmospheres/mac_primary.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <array>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>

namespace {
using namespace ma::experimental;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
std::size_t records = 0;
void verify(double high, MacEnergy low, MacEnergy increment, MacEnergy extra = {}) {
    MacEnergySum total; total.add(high); total.add(low); total.add(increment); total.add(extra);
    const auto result = store_mac_primary_remainder(total);
    check(canonical_mac_primary(result.value, result.remainder), "Noncanonical stored primary pair");
    const auto remainder = mac_stored_remainder_value(result.remainder);
    auto residual = total; residual.add(-result.value); residual.add(remainder.negated());
    for (std::size_t k = 0; k < result.discarded.size(); ++k) residual.add(result.discarded.component(k).negated());
    check(residual.sign() == 0, "Unreported primary storage error");
    MacEnergyAccount account;
    account.add(high); account.add(low); account.add(increment); account.add(extra); account.add(-result.value); account.add(remainder.negated());
    check(account.within_bound(), "Primary storage exceeds original operand bound");
    if (result.discarded.sign() != 0) {
        check(!remainder.is_zero(), "Primary discarded a representable correction");
        MacEnergySum margin; margin.add(MacEnergy::from_parts(.5, remainder.exponent() - 53));
        const int sign = result.discarded.sign();
        for (std::size_t k = 0; k < result.discarded.size(); ++k)
            margin.add(sign < 0 ? result.discarded.component(k) : result.discarded.component(k).negated());
        check(margin.sign() >= 0, "Primary third part exceeds half a low-part quantum");
    }
    check(result.high_rounded == (total.sign() != 0 && [&] {
        auto difference = total; difference.add(-result.value); return difference.sign() != 0;
    }()), "Primary high-rounding flag differs from operands");
    std::cout << "PRFULL " << records++ << ' ' << high << ' ' << low.mantissa() << ' ' << low.exponent() << ' '
        << increment.mantissa() << ' ' << increment.exponent() << ' ' << extra.mantissa() << ' ' << extra.exponent() << ' '
        << result.value << ' ' << remainder.mantissa() << ' '
        << remainder.exponent() << ' ' << result.remainder.projected << ' ' << !result.remainder.full.is_zero() << ' '
        << result.high_rounded << ' ' << result.discarded.size() << '\n';
    for (std::size_t k = 0; k < result.discarded.size(); ++k) {
        const auto part = result.discarded.component(k);
        std::cout << "PRTAIL " << part.mantissa() << ' ' << part.exponent() << '\n';
    }
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        std::mt19937_64 rng(414719);
        for (int n = 0; n < 1000; ++n) {
            const int exponent = static_cast<int>(rng() % 1801) - 900;
            const double high = std::ldexp(1. + static_cast<double>(rng() % 1024) / 1024., exponent) * (rng() % 2 ? 1 : -1);
            const auto low = MacEnergy::from_parts(.5, exponent - 55 - static_cast<int>(rng() % 3000));
            const auto increment = MacEnergy::from_parts(rng() % 2 ? .75 : -.75, exponent + static_cast<int>(rng() % 4000) - 3900);
            verify(high, low, increment);
        }
        for (const int exponent : {-1075, -2000, -4096}) {
            verify(1, {}, MacEnergy::from_parts(.5, exponent));
            verify(1, MacEnergy::from_parts(.5, exponent), MacEnergy::from_double(-1));
        }
        for (double sign : {-1., 1.}) {
            verify(sign, {}, MacEnergy::from_double(sign * 0x1p-53));
            verify(sign, MacEnergy::from_double(sign * 0x1p-53), MacEnergy::from_parts(sign * .5, -2000));
            verify(sign, MacEnergy::from_double(sign * 0x1p-53), MacEnergy::from_parts(-sign * .5, -2000));
            verify(sign * std::numeric_limits<double>::denorm_min(), {}, MacEnergy::from_parts(sign * .5, -1075));
        }
        // The sum estimate can round a low-part tie before a later component
        // decides its direction. The stored correction must inspect all parts.
        for (double sign : {-1., 1.}) for (double direction : {-1., 1.}) for (double tiny : {-1., 1.})
            verify(sign, MacEnergy::from_double(sign * 0x1p-54),
                MacEnergy::from_double(sign * (direction < 0 ? -0x1p-108 : 0x1p-107)),
                MacEnergy::from_double(tiny * 0x1p-160));
        bool malformed = false, overflow = false;
        try { (void)mac_stored_remainder_value({0, MacEnergy::from_double(1)}); }
        catch (const std::invalid_argument&) { malformed = true; }
        try {
            MacEnergySum huge; huge.add(std::numeric_limits<double>::max()); huge.add(std::numeric_limits<double>::max());
            (void)store_mac_primary_remainder(huge);
        } catch (const std::overflow_error&) { overflow = true; }
        catch (const ma::FluidFatalError& error) { overflow = error.code() == ma::FluidFatalCode::numerical_precision; }
        check(malformed && overflow, "Malformed or overflowing primary storage was admitted");
        std::cout << "PRFULLPASS " << records << '\n'; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
