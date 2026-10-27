#pragma once
#include "../mac_hypot.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

// Original linear law below half strain;
// thereafter dyadic affine segments keep volume positive. Each segment has
// an analytically integrated quadratic elastic energy. No Newton/bisection.
namespace ma::experimental::game_kernel::positive_liquid {
struct Segment {
  double b = 1, a = 1, d = 0, lower = 0;
  double upper = std::numeric_limits<double>::infinity();
};
inline Segment segment(double pressure, double stiffness) {
  if (!(pressure >= 0 && std::isfinite(pressure) && stiffness > 0 &&
        std::isfinite(stiffness)))
    throw std::runtime_error("Invalid positive-liquid pressure/stiffness");
  if (pressure < .5 * stiffness)
    return {1, 1, 0, 0, .5 * stiffness};
  const double ratio = pressure / stiffness;
  if (!std::isfinite(ratio))
    throw std::runtime_error("Positive-liquid branch ratio is unsupported");
  const int n = std::max(0, std::ilogb(ratio) + 1);
  const double a = std::ldexp(.5, -2 * n),
               b = std::ldexp(.75, -n),
               lo = std::ldexp(.5 * stiffness, n),
               hi = std::ldexp(stiffness, n);
  if (!(a > 0 && b > 0 && std::isfinite(lo)))
    throw std::runtime_error("Positive-liquid segment is unrepresentable");
  return {b, a, .0625 + .1875 * n, lo, hi};
}
inline double volume_fraction(double pressure, double stiffness) {
  const auto s = segment(pressure, stiffness);
  return s.b - s.a * (pressure / stiffness);
}
inline double specific_elastic(double pressure, double stiffness,
                               double density) {
  if (pressure < .5 * stiffness)
    return .5 * pressure * pressure / (density * stiffness);
  const auto s = segment(pressure, stiffness);
  return .5 * s.a * pressure * (pressure / stiffness) / density +
         s.d * stiffness / density;
}
struct Coefficients {
  double b = 0, c = 0, d = 0, lower = 0;
  double upper = std::numeric_limits<double>::infinity();
};
template <class Each>
Coefficients coefficients(double pressure, Each each) {
  Coefficients out;
  each([&](double nominal, double stiffness) {
    const auto s = segment(pressure, stiffness);
    out.b += nominal * s.b;
    out.c += nominal / stiffness * s.a;
    out.d += nominal * stiffness * s.d;
    out.lower = std::max(out.lower, s.lower);
    out.upper = std::min(out.upper, s.upper);
  });
  return out;
}
inline double positive_quadratic(double a, double b, double c) {
  if (!(a >= 0 && c >= 0))
    throw std::runtime_error("Invalid positive-liquid quadratic");
  if (a == 0)
    return c == 0 ? 0 : c / b;
  if (c == 0)
    return b < 0 ? -b / a : 0;
  const double d = ma::rounded_hypot(b, 2 * std::sqrt(a) * std::sqrt(c));
  return b >= 0 ? 2 * c / (b + d) : (d - b) / (2 * a);
}
struct Root {
  double pressure = 0;
  Coefficients coefficients;
  unsigned intervals = 0;
};
template <class Each>
Root pressure(double volume, double ratio, double thermal, double nRT,
              bool given_temperature, double guess, Each each) {
  double p = guess;
  for (unsigned step = 0; step < 4096; ++step) {
    const auto q = coefficients(p, each);
    const double available = thermal - q.d;
    double next;
    if (given_temperature)
      next = positive_quadratic(q.c, volume - q.b, nRT);
    else if (ratio == 0)
      next = q.b <= volume ? 0 : (q.b - volume) / q.c;
    else if (available <= 0) {
      if (q.lower == 0)
        throw std::runtime_error("Positive-liquid thermal energy is invalid");
      p = std::nextafter(q.lower, 0.);
      continue;
    } else
      next = positive_quadratic(q.c * (1 + .5 * ratio), volume - q.b,
                                ratio * available);
    if (!(next >= 0 && std::isfinite(next)))
      throw std::runtime_error("Positive-liquid root is unrepresentable");
    // Adjacent closed forms can disagree by one ULP at a constitutive knot.
    // Accept that pressure and derive all final fields from its own segment.
    if (next >= std::nextafter(q.lower, 0.) &&
        next <= std::nextafter(q.upper,
                              std::numeric_limits<double>::infinity()))
      return {next, coefficients(next, each), step + 1};
    if (next < q.lower)
      p = std::nextafter(q.lower, 0.);
    else
      p = std::nextafter(q.upper, std::numeric_limits<double>::infinity());
  }
  throw std::runtime_error("Positive-liquid constitutive interval budget");
}
} // namespace positive_liquid
