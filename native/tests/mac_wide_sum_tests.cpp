#include "mixed_atmospheres/mac_energy.hpp"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool equal(MacEnergy a, MacEnergy b) { return a.mantissa() == b.mantissa() && a.exponent() == b.exponent(); }
// The original, uncapped grow-expansion recurrence. It visits every component
// and never uses the production prefix search.
struct Reference {
    std::vector<MacEnergy> terms;
    void add(MacEnergy value) {
        if (value.is_zero()) return;
        std::vector<MacEnergy> next; next.reserve(terms.size() + 1);
        for (const auto& term : terms) {
            const auto pair = add_mac_energy(value, term);
            if (!pair.low.is_zero()) next.push_back(pair.low);
            value = pair.high;
        }
        if (!value.is_zero()) next.push_back(value);
        terms = std::move(next);
    }
};
std::uint64_t comparisons = 0;
void compare(const MacEnergySum& actual, const Reference& expected) {
    check(actual.size() == expected.terms.size(), "Wide expansion component count differs from original recurrence");
    for (std::size_t i = 0; i < actual.size(); ++i)
        check(equal(actual.component(i), expected.terms[i]), "Wide expansion component differs from original recurrence");
    ++comparisons;
}
MacEnergy isolated(std::size_t i, MacExponent base) {
    return MacEnergy::from_parts(i % 2 == 0 ? .625 : .75, base + static_cast<std::int64_t>(512 * i));
}
void encoded(MacEnergy value) { std::cout << value.mantissa() << ' ' << value.exponent(); }
void export_sum(int id, const std::vector<MacEnergy>& inputs, const MacEnergySum& sum) {
    std::cout << "WIDE " << id << ' ' << inputs.size() << ' ' << sum.size() << ' ' << sum.sign() << ' ';
    encoded(sum.value()); std::cout << '\n';
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        std::cout << "INPUT " << id << ' ' << i << ' '; encoded(inputs[i]); std::cout << '\n';
    }
    for (std::size_t i = 0; i < sum.size(); ++i) {
        std::cout << "PART " << id << ' ' << i << ' '; encoded(sum.component(i)); std::cout << '\n';
    }
}
void wide_reference(int id) {
    constexpr std::size_t count = 5003;
    std::vector<std::size_t> order(count); std::iota(order.begin(), order.end(), 0);
    if (id == 1) std::reverse(order.begin(), order.end());
    if (id == 2) {
        std::uint64_t seed = UINT64_C(0x64cdbe973f120a85);
        for (std::size_t i = order.size() - 1; i > 0; --i) {
            seed = seed * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            std::swap(order[i], order[static_cast<std::size_t>(seed % (i + 1))]);
        }
    }
    std::vector<MacEnergy> inputs;
    const MacExponent base = INT64_C(-28000000000000);
    for (const auto i : order) {
        auto value = isolated(i, base);
        if (id == 2 && i % 3 == 0) value = value.negated();
        inputs.push_back(value);
        if (id == 2 && i % 37 == 0) {
            // Carries, ties and cancellation inside widely separated clusters.
            inputs.push_back(MacEnergy::from_parts(.5, value.exponent() - static_cast<std::int64_t>(52 + i % 4)));
            if (i % 74 == 0) inputs.push_back(value.negated());
        }
    }
    MacEnergySum actual; Reference expected;
    for (const auto& input : inputs) { actual.add(input); expected.add(input); compare(actual, expected); }
    check(actual.size() > 4096, "Wide recurrence control did not cross the former capacity limit");
    export_sum(id, inputs, actual);
    auto copy = actual; compare(copy, expected);
    auto moved = std::move(copy); compare(moved, expected);
    check(copy.size() == 0, "Moved-from wide sum is not empty");
    copy.add(3); check(copy.value().require_representable() == 3, "Moved-from wide sum cannot be reused");
    for (auto i = inputs.rbegin(); i != inputs.rend(); ++i) {
        moved.add(i->negated()); expected.add(i->negated()); compare(moved, expected);
    }
    check(moved.size() == 0 && moved.sign() == 0, "Wide exact cancellation retained a residual");
}
void large_exponents() {
    const auto base = MacExponent::from_decimal("-57896044618658097711785492504343953926634992332820282019728792003956564819968");
    std::vector<MacEnergy> inputs; MacEnergySum sum;
    for (std::size_t i = 0; i < 5003; ++i) { inputs.push_back(isolated(i, base)); sum.add(inputs.back()); }
    check(sum.size() == inputs.size(), "Large-integer exponents lost isolated terms");
    for (std::size_t i = 0; i < inputs.size(); ++i)
        check(equal(sum.component(i), inputs[i]), "Large-integer prefix search changed an isolated component");
    export_sum(3, inputs, sum);
    for (auto i = inputs.rbegin(); i != inputs.rend(); ++i) sum.add(i->negated());
    check(sum.size() == 0, "Large-integer wide cancellation lost a term");
}
void colony_count() {
    constexpr std::size_t count = 250000;
    const MacExponent base = INT64_C(-28000000000000);
    MacEnergySum sum;
    for (std::size_t i = 0; i < count; ++i) sum.add(isolated(i, base));
    check(sum.size() == count && sum.sign() == 1 && equal(sum.value(), isolated(count - 1, base)),
        "Colony-count exact accumulator lost a remote positive term");
    for (std::size_t i = 0; i < count; ++i)
        check(equal(sum.component(i), isolated(i, base)), "Colony-count component differs");
    for (std::size_t i = count; i > 0; --i) sum.add(isolated(i - 1, base).negated());
    check(sum.size() == 0 && sum.sign() == 0, "Colony-count exact cancellation failed");
    std::cout << "COLONY_COUNT " << count << " 0\n";
}
void account_tail() {
    // Exactly on the 128-epsilon bound, then one remote positive/negative
    // residual. The below-range tail must decide which side passes.
    const auto epsilon = MacEnergy::from_double(1).shifted(-45);
    const auto tail = MacEnergy::from_parts(.625, INT64_C(-28000000000000));
    for (int sign : {-1, 1}) {
        MacEnergyAccount account;
        account.add(1 + epsilon.require_representable());
        account.add(-(1 - epsilon.require_representable()));
        check(account.within_bound(), "Exactly-on-bound account was rejected");
        for (std::size_t i = 0; i < 5003; ++i) {
            const auto remote = isolated(i, INT64_C(-28000100000000));
            account.add(remote); account.add(remote.negated());
        }
        account.add(sign < 0 ? tail.negated() : tail);
        check(account.terms().size() == 2 && equal(account.terms().component(0), sign < 0 ? tail.negated() : tail),
            "Account residual discarded its deciding below-range operand");
        check(account.within_bound() == (sign < 0), "A remote tail changed an exact conservation-bound decision");
    }
    // A direct exact comparison at the bound: sum = +/-tail after all 5,003
    // remote positive terms have been cancelled in an unrelated order.
    MacEnergySum comparison;
    for (std::size_t i = 0; i < 5003; ++i) comparison.add(isolated(i, INT64_C(-28000000000000)));
    comparison.add(epsilon); comparison.add(epsilon.negated());
    for (std::size_t i = 1; i < 5003; ++i) comparison.add(isolated(i, INT64_C(-28000000000000)).negated());
    check(comparison.size() == 1 && equal(comparison.value(), tail) && comparison.sign() == 1,
        "Cancellation concealed a remote positive account term");
    comparison.add(tail.multiplied(-2));
    check(equal(comparison.value(), tail.negated()) && comparison.sign() == -1,
        "Remote sign decision changed after wide cancellation");
    std::cout << "ACCOUNT_TAIL 2 1 -1\n";
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    for (int id = 0; id < 3; ++id) wide_reference(id);
    large_exponents(); colony_count(); account_tail();
    std::cout << "REFERENCE_PREFIXES " << comparisons << '\n';
    std::cout << "PASS uncapped exact expansions, original recurrence, colony-count retention and wide cancellation\n";
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
