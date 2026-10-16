#include "mixed_atmospheres/mac_momentum_limiter.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacScaled value(double x) { return MacScaled::from_double(x); }
void emit(MacScaled x) { std::cout << ' ' << x.mantissa() << ' ' << x.exponent(); }
void export_pair(std::size_t id, const MacMomentumLimitedPair& p) {
    std::cout << "PAIR " << id << ' ' << p.inertia_left << ' ' << p.inertia_right << ' ' << p.limiter_roundoff_backoffs
        << ' ' << p.unresolved_momentum_updates << ' ' << p.unresolved_heat_updates;
    for (const auto& x : {p.before_left, p.before_right, p.before_heat_left, p.before_heat_right, p.requested_impulse, p.limiter,
        p.impulse, p.after_left, p.after_right, p.after_heat_left, p.after_heat_right, p.energy_flux, p.heat_change_left, p.heat_change_right}) emit(x);
    std::cout << '\n';
}
MacMomentumLimitedPair pair(std::size_t id, double il, double ir, double jl, double jr, double ql, double qr, double requested) {
    auto result = limit_mac_momentum_pair(il, ir, value(jl), value(jr), value(ql), value(qr), value(requested));
    export_pair(id, result); return result;
}
void analytic() {
    const auto p = pair(0, 2, 4, 0, 4, .25, .5, .25);
    check(p.limiter.require_representable() == 1 && p.after_left.require_representable() == -.25 &&
        p.after_right.require_representable() == 4.25 && p.after_heat_left.require_representable() == .109375 &&
        p.after_heat_right.require_representable() == .3671875 && p.energy_flux.require_representable() == .125, "Analytic correction differs");
    static_cast<void>(pair(1, 2, 4, 6, -8, .25, .5, .5));
    const auto held = pair(2, 2, 4, 0, 4, .25, .5, 0);
    check(held.after_left.is_zero() && held.after_right.require_representable() == 4 && held.energy_flux.is_zero(), "Zero correction changes state");
}
void limiting() {
    const auto limited = pair(3, 2, 4, 0, 4, .01, .02, 1);
    check(limited.limiter.mantissa() > 0 && limited.limiter.require_representable() < 1 && limited.after_heat_left.mantissa() >= 0 &&
        limited.after_heat_right.mantissa() >= 0, "Heat budget failed to limit antidiffusion");
    const auto blocked = pair(4, 2, 4, 0, 4, 0, 0, 1);
    check(blocked.limiter.is_zero() && blocked.impulse.is_zero(), "Zero heat permits kinetic-energy creation");
    const auto dissipative = pair(5, 2, 4, 2, 0, 0, 0, .25);
    check(dissipative.limiter.require_representable() == 1 && dissipative.after_heat_left.mantissa() > 0 &&
        dissipative.after_heat_right.mantissa() > 0, "Dissipative correction needs an artificial heat floor");
}
void symmetry() {
    const auto a = pair(6, 2, 4, 0, 4, .25, .5, .25);
    const auto b = pair(7, 4, 2, 4, 0, .5, .25, -.25);
    const auto c = pair(8, 2, 4, 2048, 4100, .25, .5, .25);
    check(a.after_left.require_representable() == b.after_right.require_representable() &&
        a.after_right.require_representable() == b.after_left.require_representable() &&
        a.after_heat_left.require_representable() == b.after_heat_right.require_representable() &&
        a.energy_flux.require_representable() == -b.energy_flux.require_representable(), "Pair reflection differs");
    check(a.after_heat_left.require_representable() == c.after_heat_left.require_representable() &&
        a.after_heat_right.require_representable() == c.after_heat_right.require_representable() &&
        c.energy_flux.require_representable() == a.energy_flux.require_representable() + 1024 * .25, "Galilean heat/work differs");
}
void range() {
    const auto tiny = MacScaled::from_parts(.75, -1941);
    const auto p = limit_mac_momentum_pair(1, 2, {}, tiny, MacEnergy::from_parts(.5, -3880),
        MacEnergy::from_parts(.5, -3880), tiny.shifted(-2));
    export_pair(9, p);
    check(!p.impulse.is_zero() && p.impulse.project().rounded && p.after_left.mantissa() < 0 &&
        p.after_heat_left.mantissa() > 0, "Subrange correction or heat was discarded");
    const auto q = limit_mac_momentum_pair(1, 2, {}, value(2), MacEnergy::from_parts(.5, -4000),
        MacEnergy::from_parts(.5, -4000), value(1));
    export_pair(10, q);
    check(q.limiter.mantissa() > 0 && q.limiter.project().rounded && q.impulse.mantissa() > 0 &&
        q.after_left.mantissa() < 0, "Below-range limiter was projected to zero");
    const auto r = limit_mac_momentum_pair(1, 2, value(1e200), value(2e200), value(1), value(1), MacScaled::from_parts(.5, -1200));
    export_pair(11, r);
    check(r.impulse.mantissa() > 0 && r.unresolved_momentum_updates == 2, "Unresolved primary J updates were not reported");
}
double minmod(double a, double b) { return a * b > 0 ? std::copysign(std::min(std::abs(a), std::abs(b)), a) : 0; }
double smooth(std::size_t n, bool corrected) {
    const double mass = 1. / static_cast<double>(n), cfl = .2;
    std::vector<double> inertia(n, mass), initial(n), u(n), internal(n, mass);
    for (std::size_t i = 0; i < n; ++i) initial[i] = u[i] = 1 + .2 * std::sin(2 * std::numbers::pi * (static_cast<double>(i) + .5) / static_cast<double>(n));
    MacEnergyAccount total;
    for (std::size_t i = 0; i < n; ++i) { total.add(-internal[i]); total.add(MacEnergy::kinetic(mass, value(mass * u[i])).negated()); }
    std::size_t limited = 0;
    for (std::size_t step = 0; step < 5 * n; ++step) {
        std::vector<MacScaled> momentum(n); std::vector<MacEnergy> heat(n); std::vector<MacMomentumCorrection> edges;
        for (std::size_t i = 0; i < n; ++i) {
            const auto previous = (i + n - 1) % n, next = (i + 1) % n;
            momentum[i] = value(mass * ((1 - cfl) * u[i] + cfl * u[previous]));
            heat[i] = MacEnergy::variance(value(mass * cfl * (1 - cfl)), value(u[i] - u[previous]));
            if (corrected) edges.push_back({i, next, value(.5 * mass * cfl * (1 - cfl) * minmod(u[i] - u[previous], u[next] - u[i]))});
        }
        const auto candidate = limit_mac_momentum_batch(inertia, momentum, heat, edges); limited += candidate.limited_pairs;
        const auto old_internal = internal;
        for (std::size_t i = 0; i < n; ++i) {
            u[i] = candidate.momentum[i].shared(1, mass).require_representable();
            internal[i] = (1 - cfl) * old_internal[i] + cfl * old_internal[(i + n - 1) % n] + candidate.heat[i].require_representable();
            check(internal[i] > 0 && candidate.heat[i].mantissa() >= 0, "Smooth correction lost positive internal energy/heat");
        }
    }
    double error = 0;
    for (std::size_t i = 0; i < n; ++i) {
        error += mass * std::abs(u[i] - initial[i]); total.add(internal[i]); total.add(MacEnergy::kinetic(mass, value(mass * u[i])));
        if (corrected) std::cout << "PROFILE " << n << ' ' << i << ' ' << mass << ' ' << initial[i] << ' ' << u[i] << ' ' << internal[i] << '\n';
    }
    check(total.within_bound(), "Smooth momentum/heat advection exceeds total-energy operand bound");
    std::cout << "SMOOTH " << n << ' ' << corrected << ' ' << error << ' ' << limited; emit(total.residual()); emit(total.bound()); std::cout << '\n';
    return error;
}
void advection() {
    const auto a = smooth(32, true), b = smooth(64, true), c = smooth(128, true), donor = smooth(128, false);
    check(b < .35 * a && c < .35 * b && c < .3 * donor, "Compatible correction fails second-order smooth refinement or donor comparison");
}
void invalid() {
    std::size_t rejected = 0;
    for (int kind = 0; kind < 6; ++kind) try {
        if (kind < 4) static_cast<void>(limit_mac_momentum_pair(kind == 0 ? 0 : kind == 1 ? std::numeric_limits<double>::infinity() : 1,
            1, {}, {}, value(kind == 2 ? -1 : 0), value(kind == 3 ? -1 : 0), value(1)));
        else {
            const std::array<double, 2> mass{1, 1}; const std::array<MacScaled, 2> j{}; const std::array<MacEnergy, 2> q{};
            const std::array<MacMomentumCorrection, 1> edges{{{0, kind == 4 ? 2U : 0U, value(1)}}};
            static_cast<void>(limit_mac_momentum_batch(mass, j, q, edges));
            check(j[0].is_zero() && j[1].is_zero() && q[0].is_zero() && q[1].is_zero(), "Invalid correction changed input");
        }
    } catch (const std::invalid_argument&) { ++rejected; }
    check(rejected == 6, "Invalid correction input was accepted"); std::cout << "REJECTED " << rejected << '\n';
}
}
int main() { try {
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    analytic(); std::cout << "PASS analytic paired impulse and compatible work\n";
    limiting(); std::cout << "PASS nonnegative heat-budget limiting\n";
    symmetry(); std::cout << "PASS reflection and Galilean work\n";
    range(); std::cout << "PASS full-range impulse limiter and unresolved updates\n";
    advection(); std::cout << "PASS smooth momentum advection refinement\n";
    invalid(); std::cout << "PASS immutable invalid-input rejection\n"; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
