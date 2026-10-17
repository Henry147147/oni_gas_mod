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
    MacScaledSum sum; sum.add(before); sum.add(change); const auto value = represented(sum);
    static_cast<void>(value.project()); return value;
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
// Largest theta in [0,1] admitted by Q + theta*A - theta^2*B >= 0.
// Use the rationalized root when A<=0 to avoid subtractive cancellation.
MacScaled quadratic_heat_limit(MacEnergy heat, MacEnergy linear, MacEnergy quadratic) {
    const auto one = MacScaled::from_double(1);
    MacEnergySum endpoint; endpoint.add(heat); endpoint.add(linear); endpoint.add(quadratic.negated());
    if (endpoint.sign() >= 0) return one;
    if (heat.is_zero()) return linear.mantissa() <= 0 ? MacScaled{} : linear.shared(one, quadratic);
    const auto discriminant = add(product(linear, linear), product(quadratic, heat).shifted(2));
    const auto root = discriminant.square_root();
    return linear.mantissa() <= 0 ? heat.shifted(1).shared(one, add(root, linear.negated())) :
        add(linear, root).shared(one, quadratic.shifted(1));
}
MacMomentumLimitedPair limit_pair(double il, double ir, MacScaled jl, MacScaled jr,
    MacEnergy ql, MacEnergy qr, MacScaled requested, const MacMomentumWorkBudget* work_budget, bool quadratic) {
    require_mac_energy_environment();
    require(std::isfinite(il) && std::isfinite(ir) && il > 0 && ir > 0 && ql.mantissa() >= 0 && qr.mantissa() >= 0,
        "Invalid MAC correction inertia or available heat");
    static_cast<void>(jl.project()); static_cast<void>(jr.project());
    if (work_budget) require(work_budget->left.sign() >= 0 && work_budget->right.sign() >= 0,
        "Negative available MAC transported work energy");
    MacMomentumLimitedPair p; p.inertia_left = il; p.inertia_right = ir;
    p.before_left = p.after_left = jl; p.before_right = p.after_right = jr;
    p.before_heat_left = p.after_heat_left = ql; p.before_heat_right = p.after_heat_right = qr;
    p.requested_impulse = requested; const auto one = MacScaled::from_double(1); p.limiter = one;
    if (requested.is_zero()) { check_pair(p); return p; }
    const auto ul = jl.shared(1, il), ur = jr.shared(1, ir);
    const auto difference = mac_scaled_difference(ul, ur);
    const auto velocity_sum = add(ul, ur);
    const auto linear = product(requested, difference).shifted(-1);
    for (int side = 0; side < 2; ++side) {
        const auto coefficient = MacEnergy::kinetic(side == 0 ? il : ir, requested);
        const auto heat = side == 0 ? ql : qr;
        if (quadratic) {
            const auto ratio = quadratic_heat_limit(heat, linear, coefficient);
            if (less(ratio, p.limiter)) p.limiter = ratio;
        } else {
            // Q + a*A - a^2*B >= Q + a*(A-B), 0<=a<=1 and B>=0.
            const auto cost = add(coefficient, linear.negated());
            if (cost.mantissa() > 0 && less(heat, cost)) {
                const auto ratio = heat.shared(one, cost);
                if (less(ratio, p.limiter)) p.limiter = ratio;
            }
        }
    }
    if (work_budget) {
        const auto requested_work = product(velocity_sum, requested).shifted(-1);
        if (!requested_work.is_zero()) {
            const auto& available = requested_work.mantissa() > 0 ? work_budget->left : work_budget->right;
            MacEnergySum remainder = available; remainder.add(requested_work.magnitude().negated());
            if (remainder.sign() < 0) {
                const auto ratio = represented(available).shared(one, requested_work.magnitude());
                if (less(ratio, p.limiter)) p.limiter = ratio;
            }
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
            p.energy_flux = product(velocity_sum, p.impulse).shifted(-1);
            bool work_admitted = true;
            if (work_budget) {
                auto remaining_left = work_budget->left, remaining_right = work_budget->right;
                remaining_left.add(p.energy_flux.negated()); remaining_right.add(p.energy_flux);
                work_admitted = remaining_left.sign() >= 0 && remaining_right.sign() >= 0;
            }
            if (work_admitted) {
                p.after_heat_left = represented(left); p.after_heat_right = represented(right); accepted = true; break;
            }
        }
        if (attempt == 64 || p.limiter.is_zero()) break;
        const double reduced = std::nextafter(p.limiter.mantissa(), 0.);
        p.limiter = reduced < .5 ? MacScaled::from_parts(2 * reduced, p.limiter.exponent() - 1) :
            MacScaled::from_parts(reduced, p.limiter.exponent());
        ++p.limiter_roundoff_backoffs;
    }
    if (!accepted) precision("MAC correction cannot resolve nonnegative heat and transported work at its limiter");
    p.after_left = update_momentum(jl, p.impulse.negated()); p.after_right = update_momentum(jr, p.impulse);
    if (!resolved(jl, p.after_left, p.impulse.negated())) ++p.unresolved_momentum_updates;
    if (!resolved(jr, p.after_right, p.impulse)) ++p.unresolved_momentum_updates;
    if (!resolved(ql, p.after_heat_left, p.heat_change_left)) ++p.unresolved_heat_updates;
    if (!resolved(qr, p.after_heat_right, p.heat_change_right)) ++p.unresolved_heat_updates;
    check_pair(p); return p;
}
MacMomentumLimitedBatch limit_batch(std::span<const double> inertia, std::span<const MacScaled> momentum,
    std::span<const MacEnergy> heat, std::span<const MacMomentumCorrection> corrections, bool quadratic) {
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
        auto pair = limit_pair(inertia[edge.left], inertia[edge.right], result.momentum[edge.left], result.momentum[edge.right],
            result.heat[edge.left], result.heat[edge.right], edge.impulse, nullptr, quadratic);
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
MacMomentumLimitedPair limit_mac_momentum_pair(double il, double ir, MacScaled jl, MacScaled jr,
    MacEnergy ql, MacEnergy qr, MacScaled requested, const MacMomentumWorkBudget* work_budget) {
    return limit_pair(il, ir, jl, jr, ql, qr, requested, work_budget, false);
}
MacMomentumLimitedPair limit_mac_momentum_pair_quadratic(double il, double ir, MacScaled jl, MacScaled jr,
    MacEnergy ql, MacEnergy qr, MacScaled requested, const MacMomentumWorkBudget* work_budget) {
    return limit_pair(il, ir, jl, jr, ql, qr, requested, work_budget, true);
}
MacMomentumLimitedBatch limit_mac_momentum_batch(std::span<const double> inertia, std::span<const MacScaled> momentum,
    std::span<const MacEnergy> heat, std::span<const MacMomentumCorrection> corrections) {
    return limit_batch(inertia, momentum, heat, corrections, false);
}
MacMomentumLimitedBatch limit_mac_momentum_batch_quadratic(std::span<const double> inertia, std::span<const MacScaled> momentum,
    std::span<const MacEnergy> heat, std::span<const MacMomentumCorrection> corrections) {
    return limit_batch(inertia, momentum, heat, corrections, true);
}
}
