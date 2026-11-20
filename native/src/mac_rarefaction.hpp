// Compatible pressure work on the two normal faces of an expanding gas cell.
#pragma once
#include "mac_hypot.hpp"
#include <algorithm>
#include <cmath>

namespace ma::experimental {
// In two dimensions each expanding direction receives its share of the net
// volume increase. Compression along the other axis reduces the available work.
inline double expansion_volume(double dx, double dy, bool vertical) {
    const double axis = vertical ? dy : dx;
    if (!(axis > 0)) return 0;
    return std::max(0., std::min(axis, dx + dy));
}

struct MacExpansionImpulse {
    double impulse = 0;
    double work = 0;
};
inline MacExpansionImpulse expansion_impulse(double pressure, double sound, double volume,
    double velocity_jump, double available_heat, double il, double ir, double jl, double jr) {
    if (!(available_heat > 0 && volume > 0 && pressure > 0 && sound > 0 && velocity_jump > 0 && il > 0 && ir > 0)) return {};
    const double separating = jr / ir - jl / il;
    if (!(separating > 0)) return {};
    // Only the unresolved kinetic variance assigned to this cell is available.
    // Scale p dV by the expansion Mach number, saturating at the reversible
    // work of the gas. In a smooth resolved fan the fraction vanishes with h.
    const double work = std::min(available_heat, volume * pressure * std::min(1., velocity_jump / sound));
    if (!(work > 0)) return {};
    const double smaller_inertia = std::min(il, ir);
    const double reduced_mass = smaller_inertia / (1 + smaller_inertia / std::max(il, ir));
    const double speed = ma::rounded_hypot(separating, std::sqrt(2.) * (std::sqrt(work) / std::sqrt(reduced_mass)));
    const double impulse = work / (.5 * separating + .5 * speed);
    // A transfer that cannot move either stored momentum cannot consume heat.
    if (!(impulse > 0) || !std::isfinite(impulse) || jl - impulse == jl || jr + impulse == jr) return {};
    return {impulse, work};
}
}
