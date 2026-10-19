#pragma once
// Piecewise-linear (PLIC) gas/liquid interface geometry shared by the
// reference reconstruction and the production kernel. Donor coordinates are
// the unit square [-1/2, 1/2]^2; strips are measured as fractions of it.
#include <algorithm>
#include <cmath>
#include <vector>

namespace ma::experimental::interface_geometry {
struct Point { double x = 0, y = 0; };
using Polygon = std::vector<Point>;
// Keeps a*x + b*y <= c (Sutherland-Hodgman for one half-plane).
inline Polygon clip(const Polygon& polygon, double a, double b, double c) {
    Polygon kept; kept.reserve(polygon.size() + 1);
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const auto& start = polygon[k]; const auto& end = polygon[(k + 1) % polygon.size()];
        const double ds = a * start.x + b * start.y - c, de = a * end.x + b * end.y - c;
        if (ds <= 0) kept.push_back(start);
        if ((ds < 0 && de > 0) || (ds > 0 && de < 0)) {
            const double t = ds / (ds - de); kept.push_back({start.x + t * (end.x - start.x), start.y + t * (end.y - start.y)});
        }
    }
    return kept;
}
inline double area(const Polygon& polygon) {
    double twice = 0;
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const auto& start = polygon[k]; const auto& end = polygon[(k + 1) % polygon.size()];
        twice += start.x * end.y - end.x * start.y;
    }
    return .5 * std::abs(twice);
}
// The liquid side {nx*x + ny*y <= c} of the unit cell holding area fraction
// alpha; (nx, ny) is the unit normal pointing from liquid toward gas.
inline Polygon liquid_region(double nx, double ny, double alpha) {
    const Polygon cell{{-.5, -.5}, {.5, -.5}, {.5, .5}, {-.5, .5}};
    double lo = -.5 * (std::abs(nx) + std::abs(ny)), hi = -lo;
    for (int k = 0; k < 64; ++k) {
        const double mid = .5 * (lo + hi); if (mid == lo || mid == hi) break;
        if (area(clip(cell, nx, ny, mid)) < alpha) lo = mid; else hi = mid;
    }
    return clip(cell, nx, ny, .5 * (lo + hi));
}
// Youngs' 3x3 normal from liquid fractions a[dx + 1][dy + 1] (missing
// neighbours repeat the centre). Returns false where the gradient vanishes.
// Scaling by the larger component keeps axis-aligned normals exactly +-1, so
// a flat interface clips without slivers. (std::hypot would import GLIBC_2.35,
// above the 2.34 packaging ceiling.)
inline bool youngs_normal(const double (&a)[3][3], double& nx, double& ny) {
    const double gx = ((a[2][2] + 2 * a[2][1] + a[2][0]) - (a[0][2] + 2 * a[0][1] + a[0][0])) / 8;
    const double gy = ((a[0][2] + 2 * a[1][2] + a[2][2]) - (a[0][0] + 2 * a[1][0] + a[2][0])) / 8;
    const double scale = std::max(std::abs(gx), std::abs(gy));
    if (!std::isfinite(scale) || !(scale > 0)) return false;
    const double u = gx / scale, v = gy / scale, length = std::sqrt(u * u + v * v);
    nx = -u / length; ny = -v / length; return true;
}
// Liquid fraction of the outgoing strip of the given swept width beside the
// face on `axis` (0 = x) at the positive or negative side of the donor.
inline double strip_liquid(const Polygon& region, int axis, bool positive, double width) {
    const double bound = positive ? .5 - width : -.5 + width;
    const auto strip = axis == 0 ? clip(region, positive ? -1. : 1., 0., positive ? -bound : bound)
                                 : clip(region, 0., positive ? -1. : 1., positive ? -bound : bound);
    return std::clamp(area(strip) / width, 0., 1.);
}
}
