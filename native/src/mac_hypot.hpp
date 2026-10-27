#pragma once
#include <cmath>
#include <utility>

namespace ma {
// sqrt(x * x + y * y), rounded to nearest from the exact value, with no
// intermediate overflow or underflow. It is the same function on every
// platform and C library: std::hypot differs between libraries by an ulp in
// about 0.3% of arguments, and glibc's current version binds GLIBC_2.35,
// above the Linux packaging baseline (2.34). The squares are exact
// double-double products (Veltkamp/Dekker splitting, no FMA needed), and one
// Newton step from sqrt(x^2 + y^2) uses the exact residual.
inline double rounded_hypot(double x, double y) {
    x = std::fabs(x); y = std::fabs(y);
    if (std::isinf(x) || std::isinf(y)) return INFINITY;
    if (std::isnan(x) || std::isnan(y)) return x + y;
    if (x < y) std::swap(x, y);
    if (y == 0) return x;
    // Scale the larger argument into [0.5, 1) by an exact power of two.
    int exponent = 0;
    (void)std::frexp(x, &exponent);
    const double a = std::ldexp(x, -exponent), b = std::ldexp(y, -exponent);
    const auto square = [](double v, double& error) {
        const double product = v * v, split = 134217729.0 * v;
        const double high = split - (split - v), low = v - high;
        error = ((high * high - product) + 2 * high * low) + low * low;
        return product;
    };
    double ea = 0, eb = 0;
    const double pa = square(a, ea), pb = square(b, eb);
    const double high = pa + pb, low = (pa - high) + pb + ea + eb;
    double root = std::sqrt(high), er = 0;
    const double rr = square(root, er);
    root += (((high - rr) - er) + low) / (2 * root);
    return std::ldexp(root, exponent);
}
}
