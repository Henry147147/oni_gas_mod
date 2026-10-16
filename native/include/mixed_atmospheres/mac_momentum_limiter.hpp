#pragma once
#include "mac_fields.hpp"
#include <span>

namespace ma::experimental {
// A correction removes dJ from the left dual cell and adds it to the right.
// Available heat is the nonnegative upwind kinetic remainder BEFORE deposition
// into material U. It is never a withdrawal from stored internal energy.
struct MacMomentumLimitedPair {
    double inertia_left = 0, inertia_right = 0;
    MacScaled before_left, before_right, after_left, after_right;
    MacEnergy before_heat_left, before_heat_right, after_heat_left, after_heat_right;
    MacScaled requested_impulse, limiter, impulse;
    MacEnergy energy_flux, heat_change_left, heat_change_right;
    std::size_t limiter_roundoff_backoffs = 0;
    std::size_t unresolved_momentum_updates = 0, unresolved_heat_updates = 0;
};
// Optional actual transported kinetic energy before this correction. Keep the
// additive expansions: a rounded estimate can overstate a nearly empty budget.
// References are borrowed only for the synchronous pair calculation.
struct MacMomentumWorkBudget { const MacEnergySum& left; const MacEnergySum& right; };
[[nodiscard]] MacMomentumLimitedPair limit_mac_momentum_pair(double inertia_left,
    double inertia_right, MacScaled momentum_left, MacScaled momentum_right,
    MacEnergy available_heat_left, MacEnergy available_heat_right, MacScaled requested_impulse,
    const MacMomentumWorkBudget* transported_work_budget = nullptr);

struct MacMomentumCorrection { std::size_t left = 0, right = 0; MacScaled impulse; };
struct MacMomentumLimitedBatch {
    std::vector<MacScaled> momentum;
    std::vector<MacEnergy> heat;
    std::vector<MacMomentumLimitedPair> pairs;
    MacEnergy energy_residual, energy_bound;
    MacScaled momentum_residual;
    std::size_t limited_pairs = 0;
};
// Ordered conservative edge corrections, for free occupied dual cells only.
// Every pair retains nonnegative remainder heat. Geometry, reconstruction,
// wall handling, material transport and deposition remain caller obligations.
// Invalid input/failure returns no candidate and never changes the inputs.
[[nodiscard]] MacMomentumLimitedBatch limit_mac_momentum_batch(std::span<const double> inertia,
    std::span<const MacScaled> momentum, std::span<const MacEnergy> available_heat,
    std::span<const MacMomentumCorrection> corrections);
}
