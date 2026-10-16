#include "mixed_atmospheres/mac_momentum_limiter.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>

namespace ma::experimental {
namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::invalid_argument(reason); }
[[noreturn]] void precision(const char* reason) { throw FluidFatalError(FluidFatalCode::numerical_precision, reason); }
MacEnergy represented(const MacEnergySum& sum) {
    const auto value = sum.value();
    if (sum.sign() != 0 && (value.is_zero() || (value.mantissa() < 0 ? -1 : 1) != sum.sign()))
        precision("MAC correction energy sum lost its nonzero sign");
    MacEnergyAccount account;
    for (std::size_t i = 0; i < sum.size(); ++i) account.add(sum.component(i));
    account.add(value.negated());
    if (!account.within_bound()) precision("MAC correction energy rounding exceeds its operand bound");
    return value;
}
MacEnergy add(MacEnergy a, MacEnergy b) { MacEnergySum sum; sum.add(a); sum.add(b); return represented(sum); }
MacEnergy product(MacEnergy a, MacEnergy b) {
    return (b.mantissa() < 0 ? a.negated() : a).shared(b.magnitude(), MacEnergy::from_double(1));
}
bool less(MacEnergy a, MacEnergy b) { MacEnergySum sum; sum.add(a); sum.add(b.negated()); return sum.sign() < 0; }
bool resolved(MacEnergy before, MacEnergy after, MacEnergy change) {
    MacEnergySum difference; difference.add(after); difference.add(before.negated()); difference.add(change.negated());
    MacEnergySum allowance; allowance.add(change.magnitude().shifted(-47));
    for (std::size_t i = 0; i < difference.size(); ++i)
        allowance.add(difference.sign() < 0 ? difference.component(i) : difference.component(i).negated());
    return allowance.sign() >= 0;
}
MacScaled update_momentum(MacScaled before, MacScaled change) {
    MacScaledSum sum; sum.add(before); sum.add(change); return mac_scaled_primary(sum);
}
void check_pair(const MacMomentumLimitedPair& p) {
    for (int side = 0; side < 2; ++side) {
        const auto inertia = side == 0 ? p.inertia_left : p.inertia_right;
        const auto before = side == 0 ? p.before_left : p.before_right;
        const auto after = side == 0 ? p.after_left : p.after_right;
        const auto before_heat = side == 0 ? p.before_heat_left : p.before_heat_right;
        const auto after_heat = side == 0 ? p.after_heat_left : p.after_heat_right;
        MacEnergyAccount energy; energy.add(MacEnergy::kinetic(inertia, after));
        energy.add(MacEnergy::kinetic(inertia, before).negated());
        energy.add(after_heat); energy.add(before_heat.negated());
        energy.add(side == 0 ? p.energy_flux : p.energy_flux.negated());
        if (!energy.within_bound()) precision("MAC correction local kinetic/heat/work account exceeds its operand bound");
    }
    MacScaledAccount momentum; momentum.add(p.after_left); momentum.add(p.after_right);
    momentum.add(p.before_left.negated()); momentum.add(p.before_right.negated());
    if (!momentum.within_bound()) precision("MAC correction pair momentum account exceeds its operand bound");
}
}
MacMomentumLimitedPair limit_mac_momentum_pair(double il, double ir, MacScaled jl, MacScaled jr,
    MacEnergy ql, MacEnergy qr, MacScaled requested) {
    require_mac_energy_environment();
    require(std::isfinite(il) && std::isfinite(ir) && il > 0 && ir > 0 && ql.mantissa() >= 0 && qr.mantissa() >= 0,
        "Invalid MAC correction inertia or available heat");
    static_cast<void>(jl.project()); static_cast<void>(jr.project());
    MacMomentumLimitedPair p; p.inertia_left = il; p.inertia_right = ir;
    p.before_left = p.after_left = jl; p.before_right = p.after_right = jr;
    p.before_heat_left = p.after_heat_left = ql; p.before_heat_right = p.after_heat_right = qr;
    p.requested_impulse = requested; const auto one = MacScaled::from_double(1); p.limiter = one;
    if (requested.is_zero()) { check_pair(p); return p; }
    const auto ul = jl.shared(1, il), ur = jr.shared(1, ir);
    const auto difference = mac_scaled_difference(ul, ur);
    const auto linear = product(requested, difference).shifted(-1);
    // Q + a*A - a^2*B >= Q + a*(A-B), for 0<=a<=1 and B>=0.
    // The sufficient linear bound avoids square roots or projected tiny ratios.
    for (int side = 0; side < 2; ++side) {
        const auto cost = add(MacEnergy::kinetic(side == 0 ? il : ir, requested), linear.negated());
        const auto heat = side == 0 ? ql : qr;
        if (cost.mantissa() > 0 && less(heat, cost)) {
            const auto ratio = heat.shared(one, cost);
            if (less(ratio, p.limiter)) p.limiter = ratio;
        }
    }
    bool accepted = false;
    for (std::size_t attempt = 0; attempt <= 64; ++attempt) {
        p.impulse = requested.shared(p.limiter, one);
        const auto a = product(p.impulse, difference).shifted(-1);
        p.heat_change_left = add(a, MacEnergy::kinetic(il, p.impulse).negated());
        p.heat_change_right = add(a, MacEnergy::kinetic(ir, p.impulse).negated());
        MacEnergySum left, right; left.add(ql); left.add(p.heat_change_left); right.add(qr); right.add(p.heat_change_right);
        if (left.sign() >= 0 && right.sign() >= 0) {
            p.after_heat_left = represented(left); p.after_heat_right = represented(right); accepted = true; break;
        }
        if (attempt == 64 || p.limiter.is_zero()) break;
        p.limiter = MacScaled::from_parts(std::nextafter(p.limiter.mantissa(), 0.), p.limiter.exponent());
        ++p.limiter_roundoff_backoffs;
    }
    if (!accepted) precision("MAC correction cannot resolve a nonnegative remainder at its limiter");
    p.after_left = update_momentum(jl, p.impulse.negated()); p.after_right = update_momentum(jr, p.impulse);
    p.energy_flux = product(add(ul, ur), p.impulse).shifted(-1);
    if (!resolved(jl, p.after_left, p.impulse.negated())) ++p.unresolved_momentum_updates;
    if (!resolved(jr, p.after_right, p.impulse)) ++p.unresolved_momentum_updates;
    if (!resolved(ql, p.after_heat_left, p.heat_change_left)) ++p.unresolved_heat_updates;
    if (!resolved(qr, p.after_heat_right, p.heat_change_right)) ++p.unresolved_heat_updates;
    check_pair(p); return p;
}
MacMomentumLimitedBatch limit_mac_momentum_batch(std::span<const double> inertia, std::span<const MacScaled> momentum,
    std::span<const MacEnergy> heat, std::span<const MacMomentumCorrection> corrections) {
    require_mac_energy_environment();
    require(inertia.size() == momentum.size() && heat.size() == momentum.size() && inertia.size() <= 16000000 &&
        corrections.size() <= 16000000, "Invalid MAC correction batch shape or capacity");
    for (std::size_t i = 0; i < inertia.size(); ++i) {
        require(std::isfinite(inertia[i]) && inertia[i] > 0 && heat[i].mantissa() >= 0, "Invalid MAC correction batch endpoint");
        static_cast<void>(momentum[i].project());
    }
    for (const auto& edge : corrections) require(edge.left < inertia.size() && edge.right < inertia.size() && edge.left != edge.right,
        "Invalid MAC correction edge");
    MacMomentumLimitedBatch result; result.momentum.assign(momentum.begin(), momentum.end()); result.heat.assign(heat.begin(), heat.end());
    result.pairs.reserve(corrections.size()); const auto one = MacScaled::from_double(1);
    for (const auto& edge : corrections) {
        auto pair = limit_mac_momentum_pair(inertia[edge.left], inertia[edge.right], result.momentum[edge.left], result.momentum[edge.right],
            result.heat[edge.left], result.heat[edge.right], edge.impulse);
        result.momentum[edge.left] = pair.after_left; result.momentum[edge.right] = pair.after_right;
        result.heat[edge.left] = pair.after_heat_left; result.heat[edge.right] = pair.after_heat_right;
        if (less(pair.limiter, one)) ++result.limited_pairs;
        result.pairs.push_back(std::move(pair));
    }
    MacEnergyAccount energy; MacScaledAccount impulse;
    for (std::size_t i = 0; i < inertia.size(); ++i) {
        energy.add(MacEnergy::kinetic(inertia[i], result.momentum[i])); energy.add(MacEnergy::kinetic(inertia[i], momentum[i]).negated());
        energy.add(result.heat[i]); energy.add(heat[i].negated());
        impulse.add(result.momentum[i]); impulse.add(momentum[i].negated());
    }
    if (!energy.within_bound() || !impulse.within_bound()) precision("MAC correction batch conservation exceeds its operand bound");
    result.energy_residual = energy.residual(); result.energy_bound = energy.bound(); result.momentum_residual = impulse.residual();
    return result;
}
}
