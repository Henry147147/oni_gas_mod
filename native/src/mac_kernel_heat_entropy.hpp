#pragma once
// Private diagnostic helpers; physical heat and interval audits do not use
// their integral. Keep the actual stored endpoints when a large heat transfer
// would round (after - before) and lose the colder endpoint.
#include "mac_kernel_detail.hpp"
#include <numeric>

namespace ma::experimental::mac_kernel {
inline double heat_path_energy(double before, double after, double x) {
    DD energy; energy.add((1 - x) * before); energy.add(x * after);
    return energy.value();
}
// Diagnostic dS = dU/T integral of one mixed cell's heat at fixed mass and
// volume (adaptive Simpson, as the retained thermal oracle's). temperature(x)
// reads a convex interpolation of the two stored U endpoints; it never changes them.
template<class Temperature> double mixed_cell_entropy(const Temperature& temperature, double change, long double allowance) {
    if (change == 0) return 0;
    // The old depth-20 tree admitted at most 2^22 + 1 samples. Retain that
    // work limit, but let a narrow endpoint layer use deeper local refinement.
    std::size_t samples = 0;
    const auto sample = [&](double x) {
        if (++samples > (std::size_t{1} << 22) + 1) throw EosRefinement{};
        return static_cast<long double>(change) / temperature(x);
    };
    const auto fa = sample(0), fm = sample(.5), fb = sample(1), initial = (fa + 4 * fm + fb) / 6;
    const auto refine = [&](auto&& self, double a, double b, long double left, long double middle, long double right,
        long double estimate, long double tolerance, unsigned depth) -> long double {
        const auto center = std::midpoint(a, b), ql = std::midpoint(a, center), qr = std::midpoint(center, b);
        const auto fl = sample(ql), fr = sample(qr);
        const auto first = (center - a) / 6 * (left + 4 * fl + middle), second = (b - center) / 6 * (middle + 4 * fr + right);
        const auto refined = first + second, error = refined - estimate;
        if (!std::isfinite(refined)) throw ArithmeticRefinement{};
        if (std::abs(error) <= 15 * tolerance) return refined + error / 15;
        if (depth == 0 || ql == a || qr == b) throw EosRefinement{};
        return self(self, a, center, left, fl, middle, first, tolerance / 2, depth - 1) +
            self(self, center, b, middle, fr, right, second, tolerance / 2, depth - 1);
    };
    const double value = static_cast<double>(refine(refine, 0, 1, fa, fm, fb, initial, allowance / 8, 32));
    if (!std::isfinite(value)) throw ArithmeticRefinement{};
    return value;
}
}
