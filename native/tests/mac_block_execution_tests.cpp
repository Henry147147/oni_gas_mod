#include "../src/mac_block_execution.hpp"
#include <atomic>
#include <barrier>
#include <bit>
#include <cfenv>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <xmmintrin.h>

namespace {
using namespace ma::experimental::interval_detail;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
struct EnvironmentGuard {
    std::fenv_t environment{};
    unsigned sse = _mm_getcsr();
    EnvironmentGuard() { check(std::fegetenv(&environment) == 0, "Cannot save test arithmetic environment"); }
    ~EnvironmentGuard() { (void)std::fesetenv(&environment); _mm_setcsr(sse); }
};
void coverage() {
    const auto workers = block_worker_count();
    std::barrier barrier(static_cast<std::ptrdiff_t>(workers));
    std::vector<std::atomic<unsigned>> visits(workers);
    independent_blocks(visits.size(), [&](std::size_t block) {
        if (block < workers) barrier.arrive_and_wait();
        ++visits[block];
    }, 0);
    for (const auto& visit : visits) check(visit == 1, "Independent block executed more or less than once");
    std::vector<std::size_t> rows(65537, 0);
    independent_rows(rows.size(), [&](std::size_t row) { rows[row] = row + 1; });
    for (std::size_t row = 0; row < rows.size(); ++row) check(rows[row] == row + 1, "Odd final block lost or repeated a row");
    independent_rows(0, [](std::size_t) { throw std::runtime_error("Empty operation ran"); });
}
void nesting_and_exceptions() {
    std::vector<std::size_t> values(65 * 40);
    independent_blocks(65, [&](std::size_t outer) {
        independent_blocks(40, [&](std::size_t inner) { values[40 * outer + inner] = 40 * outer + inner; }, 0);
    }, 0);
    for (std::size_t i = 0; i < values.size(); ++i) check(values[i] == i, "Nested work mixed row ownership");
    bool rejected = false;
    try {
        independent_blocks(65, [](std::size_t block) { if (block == 3 || block == 8) throw std::runtime_error(std::to_string(block)); }, 0);
    } catch (const std::runtime_error& error) { rejected = std::string(error.what()) == "3"; }
    check(rejected, "Block exception depends on scheduling instead of its lowest index");
    independent_blocks(65, [&](std::size_t block) { values[block] = block + 17; }, 0);
    for (std::size_t i = 0; i < 65; ++i) check(values[i] == i + 17, "Failed job corrupted a later dispatch");
}
void arithmetic(int rounding, bool flush) {
    const EnvironmentGuard guard;
    check(std::fesetround(rounding) == 0, "Cannot select caller rounding");
    _mm_setcsr((_mm_getcsr() & ~0x8040U) | (flush ? 0x8040U : 0U));
    const auto controls = _mm_getcsr() & ~0x3fU;
    check(std::feclearexcept(FE_ALL_EXCEPT) == 0, "Cannot clear caller arithmetic flags");
    std::vector<std::uint64_t> values(65); std::atomic<unsigned> errors{};
    volatile double one = 1, increment = 0x1p-53;
    const double expected = one + increment;
    (void)std::feclearexcept(FE_ALL_EXCEPT);
    independent_blocks(values.size(), [&](std::size_t block) {
        if (std::fegetround() != rounding || (_mm_getcsr() & ~0x3fU) != controls) ++errors;
        const double value = one + increment; values[block] = std::bit_cast<std::uint64_t>(value);
        // The last static partition belongs to a worker whenever workers>1;
        // the caller must receive exceptions it did not raise itself.
        if (block + 1 == values.size()) { (void)std::feraiseexcept(FE_UNDERFLOW); _mm_setcsr(_mm_getcsr() | 2U); }
    }, 0);
    for (const auto value : values) check(value == std::bit_cast<std::uint64_t>(expected), "Worker inherited a different rounding mode");
    check(errors == 0 && std::fegetround() == rounding && (_mm_getcsr() & ~0x3fU) == controls,
        "Worker arithmetic controls differ from or changed the caller");
    check((std::fetestexcept(FE_INEXACT | FE_UNDERFLOW) & (FE_INEXACT | FE_UNDERFLOW)) == (FE_INEXACT | FE_UNDERFLOW) &&
        (_mm_getcsr() & 2U) != 0, "Worker sticky exceptions did not reach the caller");
}
void concurrent_callers() {
    std::atomic<unsigned> errors{};
    const auto caller = [&](std::size_t offset) {
        try {
            for (int iteration = 0; iteration < 12; ++iteration) {
                std::vector<std::size_t> values(65537);
                independent_rows(values.size(), [&](std::size_t i) { values[i] = offset + i; });
                for (std::size_t i = 0; i < values.size(); ++i) if (values[i] != offset + i) ++errors;
            }
            arithmetic(offset == 10 ? FE_DOWNWARD : FE_UPWARD, offset == 10);
        } catch (...) { ++errors; }
    };
    std::thread a(caller, 10), b(caller, 100); a.join(); b.join();
    check(errors == 0, "Concurrent callers shared operands, exceptions or arithmetic controls");
}
void retirement() {
    std::atomic<unsigned> phase{}; std::vector<std::size_t> first(65), second(65);
    std::thread caller([&] {
        independent_blocks(first.size(), [&](std::size_t block) {
            if (block == 0) { phase = 1; while (phase.load() != 2) std::this_thread::yield(); }
            first[block] = block + 1;
        }, 0);
    });
    while (phase.load() != 1) std::this_thread::yield();
    retire_block_executor();
    independent_blocks(second.size(), [&](std::size_t block) { second[block] = block + 2; }, 0);
    phase = 2; caller.join();
    for (std::size_t i = 0; i < first.size(); ++i)
        check(first[i] == i + 1 && second[i] == i + 2, "Retiring an active executor mixed or destroyed its retained job");
    retire_block_executor(); coverage(); retire_block_executor();
}
}
int main() {
    try {
        coverage(); nesting_and_exceptions();
        arithmetic(FE_DOWNWARD, true); arithmetic(FE_UPWARD, false); arithmetic(FE_TONEAREST, false);
        concurrent_callers(); retirement();
        std::cout << "PASS independent blocks, nested/concurrent callers, ordered exceptions and caller arithmetic; workers=" << block_worker_count() << '\n';
        return 0;
    } catch (const std::exception& error) { retire_block_executor(); std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
