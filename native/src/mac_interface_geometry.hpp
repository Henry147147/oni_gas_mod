#pragma once
// Piecewise-linear (PLIC) gas/liquid interface geometry shared by the
// reference reconstruction and the production kernel. Donor coordinates are
// the unit square [-1/2, 1/2]^2; strips are measured as fractions of it.
#include <algorithm>
#include <array>
#include <cmath>

namespace ma::experimental::interface_geometry {
// Area of {nx*x + ny*y <= c} inside the rectangle [x0, x1] x [y0, y1].
// Integrated exactly along the coordinate with the smaller normal component,
// as a clamped linear length, so a rectangle wholly beyond the line returns
// exactly zero rather than polygon-clipping roundoff near 1e-17 of the cell.
inline double area_below(double nx, double ny, double c, double x0, double x1, double y0, double y1) {
    const bool along_x = std::abs(ny) >= std::abs(nx);
    const double nu = along_x ? nx : ny, nv = along_x ? ny : nx;
    const double u0 = along_x ? x0 : y0, u1 = along_x ? x1 : y1, v0 = along_x ? y0 : x0, v1 = along_x ? y1 : x1;
    const double height = v1 - v0;
    if (nv == 0) return c >= 0 ? (u1 - u0) * height : 0;
    // Length inside [v0, v1] at u: clamp(a + b u, 0, height).
    const double a = nv > 0 ? c / nv - v0 : v1 - c / nv, b = nv > 0 ? -nu / nv : nu / nv;
    const auto length = [&](double u) { return std::clamp(a + b * u, 0., height); };
    if (b == 0) return (u1 - u0) * length(u0);
    std::array<double, 4> points{u0, std::clamp(-a / b, u0, u1), std::clamp((height - a) / b, u0, u1), u1};
    std::sort(points.begin() + 1, points.begin() + 3);
    double total = 0;
    for (std::size_t k = 0; k + 1 < points.size(); ++k) {
        const double p = points[k], q = points[k + 1];
        if (q > p) total += (q - p) * .5 * (length(p) + length(q));
    }
    return total;
}
// Offset c of the line {nx*x + ny*y = c} enclosing liquid area fraction
// alpha; (nx, ny) is the unit normal pointing from liquid toward gas.
inline double line_offset(double nx, double ny, double alpha) {
    double lo = -.5 * (std::abs(nx) + std::abs(ny)), hi = -lo;
    for (int k = 0; k < 64; ++k) {
        const double mid = .5 * (lo + hi); if (mid == lo || mid == hi) break;
        if (area_below(nx, ny, mid, -.5, .5, -.5, .5) < alpha) lo = mid; else hi = mid;
    }
    return .5 * (lo + hi);
}
// Liquid area of the outgoing strip of the given swept width beside the face
// on `axis` (0 = x) at the positive or negative side of the donor.
inline double strip_liquid_area(double nx, double ny, double c, int axis, bool positive, double width) {
    const double low = positive ? .5 - width : -.5, high = positive ? .5 : -.5 + width;
    return axis == 0 ? area_below(nx, ny, c, low, high, -.5, .5) : area_below(nx, ny, c, -.5, .5, low, high);
}
// Youngs' 3x3 normal from liquid fractions a[dx + 1][dy + 1] (missing
// neighbours repeat the centre). Returns false where the gradient vanishes.
// Scaling by the larger component keeps axis-aligned normals exactly +-1.
// (std::hypot would import GLIBC_2.35, above the 2.34 packaging ceiling.)
inline bool youngs_normal(const double (&a)[3][3], double& nx, double& ny) {
    const double gx = ((a[2][2] + 2 * a[2][1] + a[2][0]) - (a[0][2] + 2 * a[0][1] + a[0][0])) / 8;
    const double gy = ((a[0][2] + 2 * a[1][2] + a[2][2]) - (a[0][0] + 2 * a[1][0] + a[2][0])) / 8;
    const double scale = std::max(std::abs(gx), std::abs(gy));
    if (!std::isfinite(scale) || !(scale > 0)) return false;
    const double u = gx / scale, v = gy / scale, length = std::sqrt(u * u + v * v);
    nx = -u / length; ny = -v / length; return true;
}
// Liquid and gas volumes (cell fractions) of each outgoing strip, in order.
// Each strip is filled exactly (liquid + gas = width), and together the strips
// never take more of a phase than the donor holds (its EOS fractions alpha and
// gas). A trace phase thinner than the line's representable offset therefore
// stays consistent: it leaves only through strips that hold it.
template<std::size_t N> void allocate_strips(const std::array<double, N>& estimated_liquid, const std::array<double, N>& width,
    std::size_t count, double alpha, double gas, std::array<double, N>& liquid, std::array<double, N>& vapour) {
    double liquid_left = alpha, gas_left = gas;
    for (std::size_t n = 0; n < count; ++n) {
        const double w = width[n], lowest = std::max(0., w - gas_left), highest = std::min(w, liquid_left);
        liquid[n] = lowest <= highest ? std::clamp(estimated_liquid[n], lowest, highest) : highest;
        vapour[n] = w - liquid[n];
        liquid_left = std::max(0., liquid_left - liquid[n]); gas_left = std::max(0., gas_left - vapour[n]);
    }
}
}
