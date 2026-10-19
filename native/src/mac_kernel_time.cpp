#include "mac_kernel_time.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ma::experimental::mac_kernel {
namespace {
constexpr unsigned maximum_level = 20;
void check(bool yes, const char* why) { if (!yes) throw std::invalid_argument(why); }
bool normal_product(double a, double b, double value) { return a == 0 || b == 0 ? value == 0 : std::isnormal(value); }
bool normal_sum(double a, double b, double value) { return std::isnormal(value) || (value == 0 && a == -b); }
}
TimeMortarPiece time_mortar_piece(unsigned left_level, unsigned right_level, std::uint32_t piece) {
    check(left_level <= maximum_level && right_level <= maximum_level, "Time mortar refinement exceeds its limit");
    TimeMortarPiece result; result.level = std::max(left_level, right_level);
    result.count = std::uint32_t{1} << result.level; result.piece = piece;
    check(piece < result.count, "Time mortar piece is outside its macro interval");
    const auto basis = [&](unsigned level) {
        const auto stride = std::uint32_t{1} << (result.level - level);
        const double fraction = (static_cast<double>(piece % stride) + .5) / static_cast<double>(stride);
        return TimeBasisPiece{piece / stride, std::sqrt(3.) * (2 * fraction - 1), 1. / static_cast<double>(stride)};
    };
    result.left = basis(left_level); result.right = basis(right_level); return result;
}
TimeFaceFlux integrate_time_face(double j0, double f0, double f1, double inertia, double area, double seconds, unsigned level) {
    check(level <= maximum_level && std::isfinite(j0) && std::isfinite(f0) && std::isfinite(f1) &&
        inertia > 0 && std::isfinite(inertia) && area > 0 && std::isfinite(area) && seconds > 0 && std::isfinite(seconds),
        "Invalid time-mortar face input");
    const double k = std::sqrt(3.) / 6, h = std::ldexp(seconds, -static_cast<int>(level));
    const double impulse = h * f0, j1 = j0 + impulse, sum = j0 + j1, middle = .5 * sum;
    const double kh = k * h, curve = kh * f1, average = middle - curve, velocity = average / inertia;
    const double timed = velocity * h, volume = timed * area;
    const double acceleration = f0 / inertia, acceleration_time = acceleration * h;
    const double first_time = acceleration_time * h, first_area = first_time * area, first_volume = k * first_area;
    const bool ordinary = std::isnormal(h) && (j0 == 0 || std::isnormal(j0)) && (f0 == 0 || std::isnormal(f0)) &&
        (f1 == 0 || std::isnormal(f1)) && std::isnormal(inertia) && std::isnormal(area) &&
        normal_product(h, f0, impulse) && normal_sum(j0, impulse, j1) && normal_sum(j0, j1, sum) &&
        normal_product(.5, sum, middle) && normal_product(k, h, kh) && normal_product(kh, f1, curve) &&
        normal_sum(middle, -curve, average) && (average == 0 ? velocity == 0 : std::isnormal(velocity)) &&
        normal_product(velocity, h, timed) && normal_product(timed, area, volume) &&
        (f0 == 0 ? acceleration == 0 : std::isnormal(acceleration)) &&
        normal_product(acceleration, h, acceleration_time) && normal_product(acceleration_time, h, first_time) &&
        normal_product(first_time, area, first_area) && normal_product(k, first_area, first_volume);
    if (ordinary) return {j1, volume, first_volume, {}};

    const auto duration = MacEnergy::from_double(seconds).shifted(-static_cast<int>(level));
    const auto one = MacEnergy::from_double(1), I = MacEnergy::from_double(inertia);
    MacEnergySum end; end.add(j0); end.add(duration.multiplied(f0));
    const auto J1 = end.value(); MacEnergySum mid; mid.add(j0); mid.add(J1);
    MacEnergySum mean; mean.add(mid.value().shifted(-1)); mean.add(duration.multiplied(k).multiplied(f1).negated());
    const auto q = mean.value().shared(duration, I).multiplied(area);
    const auto b = duration.multiplied(k).multiplied(f0).shared(duration, I).multiplied(area);
    return {J1.project().value, q.project().value, b.project().value, FullTimeFaceFlux{J1, q, b}};
}
TimeMoment time_cell_first_moment(const TimeBasisPiece& basis, double volume, double first_volume) {
    check(std::isfinite(volume) && std::isfinite(first_volume), "Nonfinite time-mortar volume moment");
    const double first = basis.midpoint_p1 * volume, second = basis.relative_duration * first_volume, result = first + second;
    if (std::isfinite(basis.midpoint_p1) && basis.relative_duration > 0 && basis.relative_duration <= 1 &&
        normal_product(basis.midpoint_p1, volume, first) && normal_product(basis.relative_duration, first_volume, second) &&
        normal_sum(first, second, result)) return {result, {}};
    const auto full = time_cell_first_moment(basis, MacEnergy::from_double(volume), MacEnergy::from_double(first_volume));
    return {full.project().value, full};
}
MacEnergy time_cell_first_moment(const TimeBasisPiece& basis, MacEnergy volume, MacEnergy first_volume) {
    check(std::isfinite(basis.midpoint_p1) && basis.relative_duration > 0 && basis.relative_duration <= 1,
        "Invalid local time basis");
    MacEnergySum result; result.add(volume.multiplied(basis.midpoint_p1)); result.add(first_volume.multiplied(basis.relative_duration));
    return result.value();
}
}
