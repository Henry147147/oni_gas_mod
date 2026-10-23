#pragma once
#include <cmath>
namespace ma::experimental::game_kernel {
struct DoubleDouble {
  double hi = 0, lo = 0;
  void add(double x) {
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
