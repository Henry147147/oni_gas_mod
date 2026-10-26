#pragma once
#include <cmath>
#include <limits>
namespace ma::experimental::game_kernel {
struct DoubleDouble {
  double hi = 0, lo = 0;
  void add(double x) {
    // Under the kernel's nearest rounding without flush-to-zero, adding a
    // zero to a finite pair whose hi + lo rounds to hi returns both parts bit
    // for bit, except a -0 part, which the sum below makes +0.
    if (x == 0 && hi + lo == hi &&
        std::abs(hi) <= std::numeric_limits<double>::max() &&
        std::signbit(hi) == (hi < 0) && std::signbit(lo) == (lo < 0))
      return;
    const double sum = hi + x, z = sum - hi, error = (hi - (sum - z)) + (x - z);
    const double tail = lo + error, next = sum + tail;
    lo = tail - (next - sum);
    hi = next;
  }
  double value() const { return hi + lo; }
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
} // namespace ma::experimental::game_kernel
