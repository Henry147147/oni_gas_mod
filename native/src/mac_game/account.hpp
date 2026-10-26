#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>
namespace ma::experimental::game_kernel {
struct DoubleDouble {
  double hi = 0, lo = 0;
  void add(double x) {
    // Under the kernel's nearest rounding without flush-to-zero, adding a
    // zero to a finite pair whose hi + lo rounds to hi returns both parts bit
    // for bit, except a -0 part, which the sum below makes +0. (The earlier
    // conditions leave no NaN or infinite part, so a -0 test is a bit test.)
    if (x == 0 && hi + lo == hi &&
        std::abs(hi) <= std::numeric_limits<double>::max() &&
        !negative_zero(hi) && !negative_zero(lo))
      return;
    const double sum = hi + x, z = sum - hi, error = (hi - (sum - z)) + (x - z);
    const double tail = lo + error, next = sum + tail;
    lo = tail - (next - sum);
    hi = next;
  }
  double value() const { return hi + lo; }
  static bool negative_zero(double v) {
    return std::bit_cast<std::uint64_t>(v) == std::uint64_t{1} << 63;
  }
};
struct Balance {
  DoubleDouble residual, operands;
  void add(double value) {
    residual.add(value);
    operands.add(std::abs(value));
  }
  bool closed() const {
    return std::abs(residual.value()) <= 1e-12 * operands.value();
  }
};
// Whole-state audit sums: species masses, U+K+G, face J and passives, with
// the absolute operands of the energy and momentum sums.
struct Totals {
  std::vector<DoubleDouble> species;
  DoubleDouble energy, jx, jy, energy_operands, jx_operands, jy_operands;
  std::map<std::uint64_t, DoubleDouble> passives;
};
} // namespace ma::experimental::game_kernel
