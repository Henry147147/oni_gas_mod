#include "mixed_atmospheres/mac_compression.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <cmath>
#include <limits>

namespace ma::experimental {
namespace {
void require(bool condition, const char* why) { if (!condition) throw std::invalid_argument(why); }
[[noreturn]] void precision(const char* why) { throw FluidFatalError(FluidFatalCode::numerical_precision, why); }
MacScaled sum(MacScaled a, MacScaled b) { MacScaledSum values; values.add(a); values.add(b); return mac_scaled_primary(values); }
MacScaled acoustic_phi(MacScaled x) {
    const auto one = MacScaled::from_double(1);
    if (x.is_zero() || x.exponent() <= MacExponent(-53)) return one;
    // exp(-64) is below the significand roundoff; keep the full reciprocal.
    if (x.exponent() >= MacExponent(7)) return one.shared(one, x);
    const auto value = x.require_representable();
    return MacScaled::from_double(-std::expm1(-value) / value);
}
bool resolved_change(MacScaled before, MacScaled after, MacScaled requested) {
    MacScaledSum difference; difference.add(after); difference.add(before.negated()); difference.add(requested.negated());
    MacScaledSum allowance; allowance.add(requested.magnitude().shifted(-47));
    for (std::size_t k = 0; k < difference.size(); ++k)
        allowance.add(difference.sign() < 0 ? difference.component(k) : difference.component(k).negated());
    return allowance.sign() >= 0;
}
void check_pair(const MacCompressionPair& p) {
    MacEnergyAccount energy;
    energy.add(MacEnergy::kinetic(p.inertia_left, p.after_left));
    energy.add(MacEnergy::kinetic(p.inertia_right, p.after_right));
    energy.add(MacEnergy::kinetic(p.inertia_left, p.before_left).negated());
    energy.add(MacEnergy::kinetic(p.inertia_right, p.before_right).negated());
    energy.add(p.heat);
    if (!energy.within_bound()) precision("MAC compression pair kinetic/heat account exceeds its operand bound");
    MacScaledAccount momentum;
    momentum.add(p.after_left); momentum.add(p.after_right);
    momentum.add(p.before_left.negated()); momentum.add(p.before_right.negated()); momentum.add(p.wall_impulse.negated());
    if (!momentum.within_bound()) precision("MAC compression pair momentum/wall account exceeds its operand bound");
}
std::vector<bool> fixed_faces(const MacGrid& g, bool x_axis) {
    std::vector<bool> fixed(x_axis ? g.x_width() * g.height : g.width * g.y_height());
    for (std::size_t y = 0; y < (x_axis ? g.height : g.y_height()); ++y)
        for (std::size_t x = 0; x < (x_axis ? g.x_width() : g.width); ++x) {
            bool left = false, right = false;
            if (x_axis) {
                if (x > 0 || g.periodic_x) left = g.walls[(x + g.width - 1) % g.width + y * g.width] == 0;
                if (x < g.width) right = g.walls[x + y * g.width] == 0;
            } else {
                if (y > 0 || g.periodic_y) left = g.walls[x + ((y + g.height - 1) % g.height) * g.width] == 0;
                if (y < g.height) right = g.walls[x + y * g.width] == 0;
            }
            fixed[x_axis ? g.x_face(x, y) : g.y_face(x, y)] = !left || !right;
        }
    return fixed;
}
}
MacScaled mac_compression_pressure_sensor(double left, double centre, double right) {
    require_mac_energy_environment();
    require(std::isfinite(left) && std::isfinite(centre) && std::isfinite(right) && left >= 0 && centre >= 0 && right >= 0,
        "Invalid MAC compression sensor pressure");
    MacScaledSum numerator, denominator;
    numerator.add(left); numerator.add(MacScaled::from_double(centre).multiplied(-2)); numerator.add(right);
    denominator.add(left); denominator.add(MacScaled::from_double(centre).multiplied(2)); denominator.add(right);
    const auto d = denominator.value();
    return d.is_zero() ? MacScaled{} : numerator.value().magnitude().shared(MacScaled::from_double(1), d);
}
MacCompressionPair relax_mac_compression_pair(double il, double ir, MacScaled jl, MacScaled jr,
    bool fl, bool fr, MacScaled integrated, MacScaled linear_speed) {
    require_mac_energy_environment();
    require(std::isfinite(il) && std::isfinite(ir) && il > 0 && ir > 0 && integrated.mantissa() >= 0 && linear_speed.mantissa() >= 0,
        "Invalid MAC compression inertia/coefficient");
    require((!fl || jl.is_zero()) && (!fr || jr.is_zero()), "Fixed MAC compression endpoint has momentum");
    MacCompressionPair p; p.inertia_left = il; p.inertia_right = ir; p.fixed_left = fl; p.fixed_right = fr;
    p.before_left = p.after_left = jl; p.before_right = p.after_right = jr; p.integrated_density_area_s = integrated;
    p.linear_speed = linear_speed;
    p.closing_velocity = mac_scaled_difference(jl.shared(1, il), jr.shared(1, ir));
    if (p.closing_velocity.mantissa() <= 0 || integrated.is_zero()) return p;
    MacScaledSum inverse; if (!fl) inverse.add(MacScaled::from_double(1).shared(1, il));
    if (!fr) inverse.add(MacScaled::from_double(1).shared(1, ir));
    const auto reciprocal = inverse.value(), one = MacScaled::from_double(1);
    require(!reciprocal.is_zero(), "Compressing MAC pair has no free endpoint");
    p.relaxation_number = integrated.shared(p.closing_velocity, one).shared(reciprocal, one);
    if (linear_speed.is_zero()) {
        MacScaledSum denominator; denominator.add(1); denominator.add(p.relaxation_number);
        // Retain the original quadratic arithmetic/reference unchanged.
        p.impulse = integrated.shared(p.closing_velocity, one).shared(p.closing_velocity, denominator.value());
    } else {
        p.linear_relaxation_number = integrated.shared(linear_speed, one).shared(reciprocal, one);
        p.linear_relaxation_phi = acoustic_phi(p.linear_relaxation_number);
        MacScaledSum denominator; denominator.add(1); denominator.add(p.relaxation_number.shared(p.linear_relaxation_phi, one));
        MacScaledSum speed; speed.add(linear_speed); speed.add(p.closing_velocity);
        p.impulse = integrated.shared(p.closing_velocity, one).shared(speed.value(), one)
            .shared(p.linear_relaxation_phi, denominator.value());
    }
    const auto drop = p.impulse.shared(reciprocal, one);
    const auto midpoint_closing = mac_scaled_difference(p.closing_velocity, drop.shifted(-1));
    if (midpoint_closing.mantissa() <= 0) precision("MAC compression relaxation lost its positive midpoint closing speed");
    p.heat = p.impulse.shared(midpoint_closing, one);
    if (!fl) p.after_left = sum(jl, p.impulse.negated());
    if (!fr) p.after_right = sum(jr, p.impulse);
    if (fl) p.wall_impulse = p.impulse;
    if (fr) p.wall_impulse = p.impulse.negated();
    if (!fl && !resolved_change(jl, p.after_left, p.impulse.negated())) ++p.unresolved_momentum_updates;
    if (!fr && !resolved_change(jr, p.after_right, p.impulse)) ++p.unresolved_momentum_updates;
    check_pair(p);
    return p;
}
MacCompressionAdvance advance_mac_compression(const SpeciesRegistry& registry, const FluidThermodynamics& model,
    const MacTransportState& source, double dt, MacCompressionLaw law) {
    require_mac_energy_environment();
    require(std::isfinite(dt) && dt > 0 && dt <= 1, "Invalid MAC compression timestep");
    require(law == MacCompressionLaw::quadratic || law == MacCompressionLaw::acoustic_quadratic ||
        law == MacCompressionLaw::pressure_sensed_acoustic, "Invalid MAC compression law");
    if (.5 * dt == 0) precision("MAC compression half timestep cannot be represented");
    const auto inertia = mac_face_inertias(registry, source, model); const auto& g = source.grid;
    auto jx = mac_field_values(source.momentum_x, source.full_momentum_x), jy = mac_field_values(source.momentum_y, source.full_momentum_y);
    const auto fixed_x = fixed_faces(g, true), fixed_y = fixed_faces(g, false);
    require(jx.size() == inertia.x.size() && jy.size() == inertia.y.size(), "MAC compression momentum shape differs");
    for (int axis = 0; axis < 2; ++axis) {
        const auto& j = axis == 0 ? jx : jy; const auto& fixed = axis == 0 ? fixed_x : fixed_y;
        const auto& m = axis == 0 ? inertia.x : inertia.y;
        for (std::size_t f = 0; f < j.size(); ++f) {
            require(!fixed[f] || j[f].is_zero(), "Fixed MAC compression face has momentum");
            static_cast<void>(MacEnergy::kinetic(m[f], j[f]).project());
        }
    }
    MacCompressionAdvance result{source, {}}; MacEnergySum heat; MacScaledSum wx, wy;
    if (law != MacCompressionLaw::quadratic) {
        result.report.source_linear_speeds.resize(source.cells.size());
        if (law == MacCompressionLaw::pressure_sensed_acoustic) result.report.source_pressures_pa.resize(source.cells.size());
        for (std::size_t i = 0; i < source.cells.size(); ++i) if (!g.walls[i]) {
            const auto& mixture = source.cells[i].mixture;
            const auto anchor = model.reference().make_isentrope(model.to_reference(mixture, g.volume_m3()));
            const auto compliance = model.compression_compliance(anchor);
            if (law == MacCompressionLaw::pressure_sensed_acoustic)
                result.report.source_pressures_pa[i] = anchor.reference_state().mechanical_pressure_pa;
            // c_s^2 = V^2 / (mass * (-dV/dp)_S). Separate square roots avoid
            // under/overflow in an otherwise representable mass*compliance.
            const auto sound = (g.volume_m3() / std::sqrt(mixture.mass())) / std::sqrt(compliance);
            if (!std::isfinite(sound) || sound <= 0) precision("MAC compression sound speed cannot be represented");
            result.report.source_linear_speeds[i] = MacScaled::from_double(sound).shifted(-1);
        }
    }
    if (law == MacCompressionLaw::pressure_sensed_acoustic) {
        const auto neighbour = [&](std::size_t cell, int axis, bool forward) {
            const auto coordinate = axis == 0 ? cell % g.width : cell / g.width;
            const auto extent = axis == 0 ? g.width : g.height;
            const bool periodic = axis == 0 ? g.periodic_x : g.periodic_y;
            if (!periodic && (forward ? coordinate + 1 == extent : coordinate == 0)) return cell;
            const auto next = forward ? (coordinate + 1) % extent : (coordinate + extent - 1) % extent;
            const auto index = axis == 0 ? next + (cell / g.width) * g.width : cell % g.width + next * g.width;
            return g.walls[index] ? cell : index;
        };
        for (int axis = 0; axis < 2; ++axis) {
            std::vector<MacScaled> curvature(source.cells.size());
            for (std::size_t cell = 0; cell < source.cells.size(); ++cell) if (!g.walls[cell]) {
                const auto& p = result.report.source_pressures_pa;
                curvature[cell] = mac_compression_pressure_sensor(p[neighbour(cell, axis, false)], p[cell], p[neighbour(cell, axis, true)]);
            }
            auto& sensors = result.report.source_pressure_sensors[static_cast<std::size_t>(axis)]; sensors.resize(source.cells.size());
            for (std::size_t cell = 0; cell < source.cells.size(); ++cell) if (!g.walls[cell]) {
                auto value = curvature[cell];
                for (const bool forward : {false, true}) {
                    const auto candidate = curvature[neighbour(cell, axis, forward)];
                    if (mac_scaled_difference(candidate, value).mantissa() > 0) value = candidate;
                }
                sensors[cell] = value;
            }
        }
    }
    for (int axis = 0; axis < 2; ++axis) for (std::size_t sweep = 0; sweep < 2; ++sweep)
        for (std::size_t n = 0; n < source.cells.size(); ++n) {
            const auto cell = sweep == 0 ? n : source.cells.size() - 1 - n;
            if (g.walls[cell]) continue;
            const auto x = cell % g.width, y = cell / g.width;
            const auto l = axis == 0 ? g.x_face(x, y) : g.y_face(x, y);
            const auto r = axis == 0 ? g.x_face((x + 1) % g.x_width(), y) : g.y_face(x, (y + 1) % g.y_height());
            auto& j = axis == 0 ? jx : jy; const auto& m = axis == 0 ? inertia.x : inertia.y;
            const auto& fixed = axis == 0 ? fixed_x : fixed_y;
            const auto integrated = MacScaled::from_double(source.cells[cell].mixture.mass()).multiplied(.5 * dt).shared(1, g.spacing_m);
            auto linear_speed = law == MacCompressionLaw::quadratic ? MacScaled{} : result.report.source_linear_speeds[cell];
            if (law == MacCompressionLaw::pressure_sensed_acoustic)
                linear_speed = linear_speed.shared(result.report.source_pressure_sensors[static_cast<std::size_t>(axis)][cell], MacScaled::from_double(1));
            auto pair = relax_mac_compression_pair(m[l], m[r], j[l], j[r], fixed[l], fixed[r], integrated, linear_speed);
            if (pair.impulse.is_zero()) continue;
            j[l] = pair.after_left; j[r] = pair.after_right; heat.add(pair.heat);
            (axis == 0 ? wx : wy).add(pair.wall_impulse);
            auto& u = result.state.cells[cell].mixture.energy; const double before = u;
            MacEnergySum update; update.add(before); update.add(pair.heat); u = update.value().project().value;
            if (!resolved_change(MacEnergy::from_double(before), MacEnergy::from_double(u), pair.heat)) ++result.report.unresolved_heat_updates;
            result.report.unresolved_momentum_updates += pair.unresolved_momentum_updates;
            result.report.events.push_back({static_cast<std::size_t>(axis), cell, sweep, std::move(pair)});
        }
    store_mac_field(result.state.momentum_x, result.state.full_momentum_x, jx);
    store_mac_field(result.state.momentum_y, result.state.full_momentum_y, jy);
    MacEnergyAccount balance; MacEnergySum entropy, entropy_bound;
    for (std::size_t i = 0; i < source.cells.size(); ++i) if (!g.walls[i]) {
        const auto& old = source.cells[i].mixture; const auto& next = result.state.cells[i].mixture;
        balance.add(next.energy); balance.add(-old.energy);
        const auto a = model.evaluate(old, g.volume_m3()), b = model.evaluate(next, g.volume_m3());
        entropy.add(b.entropy_j_per_k); entropy.add(-a.entropy_j_per_k);
        entropy_bound.add(MacEnergy::from_double(std::abs(old.energy)).shared(1, a.temperature_k).shifted(-45));
        entropy_bound.add(MacEnergy::from_double(std::abs(next.energy)).shared(1, b.temperature_k).shifted(-45));
    }
    const auto old_x = mac_field_values(source.momentum_x, source.full_momentum_x), old_y = mac_field_values(source.momentum_y, source.full_momentum_y);
    for (int axis = 0; axis < 2; ++axis) {
        const auto& a = axis == 0 ? old_x : old_y; const auto& b = axis == 0 ? jx : jy;
        const auto& m = axis == 0 ? inertia.x : inertia.y;
        for (std::size_t f = 0; f < a.size(); ++f) { balance.add(MacEnergy::kinetic(m[f], b[f])); balance.add(MacEnergy::kinetic(m[f], a[f]).negated()); }
        MacScaledAccount impulse; impulse.add((axis == 0 ? wx : wy).value().negated());
        for (std::size_t f = 0; f < a.size(); ++f) { impulse.add(b[f]); impulse.add(a[f].negated()); }
        if (!impulse.within_bound()) precision("MAC compression global momentum/wall account exceeds its operand bound");
    }
    if (!balance.within_bound()) precision("MAC compression represented U+K account exceeds its operand bound");
    MacEnergySum admissible = entropy_bound; for (std::size_t i = 0; i < entropy.size(); ++i) admissible.add(entropy.component(i));
    if (admissible.sign() < 0) throw std::domain_error("MAC compression entropy decreased beyond EOS roundoff");
    result.report.heat = heat.value(); result.report.energy_residual = balance.residual(); result.report.energy_bound = balance.bound();
    result.report.wall_impulse_x = wx.value(); result.report.wall_impulse_y = wy.value();
    result.report.entropy_change_j_per_k = entropy.value().project().value;
    result.report.entropy_roundoff_j_per_k = entropy_bound.value().project().value;
    return result;
}
}
