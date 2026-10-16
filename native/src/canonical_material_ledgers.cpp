#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {
namespace {
void append_expression(Kernel& k, Terms& output, const Terms& input) { output = joined(k, {output, input}); }
std::vector<Terms> scalar_values(const Vec& values) {
    std::vector<Terms> result; result.reserve(values.size());
    for (double value : values) result.push_back(terms({value}));
    return result;
}
}
void finite_ledgers(Kernel& k, const Data& old, Data& next, const Vec& u0, CanonicalStepReport& report) {
    const auto n = old.momentum.size(); const double h = report.seconds;
    k.scope(Stage::conservation);
    report.old_kinetic_energy_j = kinetic(k, old, report.linear_residual_ratio);
    report.new_kinetic_energy_j = next.resolved_kinetic_energy_j;
    k.require(std::bit_cast<std::uint64_t>(report.old_kinetic_energy_j) == std::bit_cast<std::uint64_t>(old.resolved_kinetic_energy_j),
        Code::numerical_precision, "Completed kinetic cache differs from its consistent quadratic");
    const double k0 = report.old_kinetic_energy_j, k1 = report.new_kinetic_energy_j;
    const auto old_velocity = scalar_values(u0), new_velocity = scalar_values(next.velocities_m_per_s);
    std::vector<Terms> mean(n), delta_velocity(n), pi_bar(n), x_bar(n), delta_x(n), c0(n), c1(n);
    for (std::size_t j = 0; j < n; ++j) {
        delta_velocity[j] = terms(report.retained.velocity_change[j]);
        mean[j] = scaled(k, joined(k, {old_velocity[j], new_velocity[j]}), .5);
    }
    Terms kinetic_defect = terms({k1, -k0}), residual_work, kinematic_work, assembly_work;
    Terms momentum_change, momentum_residual, assembly_impulse, energy_scale, impulse_scale, gravity_work, potential_change, gravity_impulse;
    Terms first_change, first_prediction, first_scale, relative_change, relative_canonical, relative_compatibility, product_scale;
    Terms pbar_total, cbar_total, delta_c_total;
    report.momentum_residual.resize(n); report.kinematic_defect.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        const auto node = static_cast<std::uint32_t>(j);
        k.scope(Stage::conservation, node); k.row(Field::velocity0, u0); k.row(Field::velocity1, next.velocities_m_per_s);
        k.scalar(Field::pi0, old.primary.momentum[j].high()); k.scalar(Field::retained_momentum_low, old.primary.momentum[j].low(), 0);
        k.scalar(Field::pi1, next.primary.momentum[j].high()); k.scalar(Field::retained_momentum_low, next.primary.momentum[j].low(), 1);
        const auto dpi = terms(report.retained.momentum_change[j]);
        const auto& displacement = report.retained.displacement[j].terms;
        pi_bar[j] = scaled(k, joined(k, {terms(old.primary.momentum[j]), terms(next.primary.momentum[j])}), .5);
        x_bar[j] = scaled(k, joined(k, {terms(old.primary.relative_positions_m[j]), terms(next.primary.relative_positions_m[j])}), .5);
        delta_x[j] = terms(report.retained.relative_position_change[j]);
        c0[j] = joined(k, {row_terms(k, old.consistent_mass, old_velocity, j), negated(terms(old.primary.momentum[j]))});
        c1[j] = joined(k, {row_terms(k, old.consistent_mass, new_velocity, j), negated(terms(next.primary.momentum[j]))});
        const auto dc = joined(k, {c1[j], negated(c0[j])});
        const auto cb = scaled(k, joined(k, {c0[j], c1[j]}), .5);
        const auto impulse = scaled(k, terms({report.actual_force[j]}), h);
        const auto rpi = joined(k, {dpi, negated(impulse)});
        const auto g = joined(k, {displacement, negated(scaled(k, mean[j], h))});
        report.momentum_residual[j] = recorded(k, report, "node.momentum-residual", rpi, node);
        report.kinematic_defect[j] = recorded(k, report, "node.kinematic-defect", g, node);
        const auto work = products(k, mean[j], dpi);
        const auto rw = products(k, mean[j], rpi);
        const auto gw = scaled(k, g, -report.actual_force[j]);
        append_expression(k, kinetic_defect, negated(work)); append_expression(k, residual_work, rw); append_expression(k, kinematic_work, gw);
        append_expression(k, energy_scale, work); append_expression(k, energy_scale, rw); append_expression(k, energy_scale, gw);
        append_expression(k, momentum_change, dpi); append_expression(k, momentum_residual, rpi); append_expression(k, impulse_scale, dpi);
        const auto parts = pressure_components(k, old, report.work_pressure, j);
        Terms assembly = terms({report.actual_force[j], -report.exterior_force[j], -report.reaction[j], -report.gravity_force[j]});
        for (double part : parts) {
            append_expression(k, assembly, terms({-part}));
            append_expression(k, impulse_scale, scaled(k, terms({part}), h));
        }
        append_expression(k, impulse_scale, scaled(k, terms({report.exterior_force[j], report.reaction[j], report.gravity_force[j]}), h));
        append_expression(k, gravity_impulse, scaled(k, terms({report.gravity_force[j]}), h));
        append_expression(k, gravity_work, scaled(k, displacement, report.gravity_force[j]));
        static_cast<void>(recorded(k, report, "node.force-assembly-defect", assembly, node));
        append_expression(k, assembly_impulse, scaled(k, assembly, h));
        append_expression(k, assembly_work, products(k, displacement, assembly));
        Terms weights;
        for (double coefficient : old.consistent_mass[j]) append_expression(k, weights, terms({coefficient}));
        const auto first = products(k, weights, displacement);
        append_expression(k, potential_change, scaled(k, first, old.gravity_down_m_per_s2));
        const auto prediction = joined(k, {products(k, weights, g), scaled(k, products(k, weights, mean[j]), h), negated(scaled(k, pi_bar[j], h))});
        append_expression(k, first_change, first); append_expression(k, first_prediction, prediction);
        append_expression(k, first_scale, first); append_expression(k, first_scale, prediction);
        const auto direct = joined(k, {products(k, x_bar[j], row_terms(k, old.consistent_mass, delta_velocity, j)),
            products(k, delta_x[j], row_terms(k, old.consistent_mass, mean, j))});
        const auto canonical = joined(k, {products(k, x_bar[j], dpi), products(k, delta_x[j], pi_bar[j])});
        const auto compatible = joined(k, {products(k, x_bar[j], dc), products(k, delta_x[j], cb)});
        append_expression(k, relative_change, direct); append_expression(k, relative_canonical, canonical); append_expression(k, relative_compatibility, compatible);
        append_expression(k, product_scale, direct); append_expression(k, product_scale, canonical); append_expression(k, product_scale, compatible);
        append_expression(k, pbar_total, pi_bar[j]); append_expression(k, cbar_total, cb); append_expression(k, delta_c_total, dc);
    }
    k.scope(Stage::conservation);
    const auto exterior_left = scaled(k, report.retained.displacement.front().terms, report.exterior_force.front());
    const auto exterior_right = scaled(k, report.retained.displacement.back().terms, report.exterior_force.back());
    const auto reaction_left = scaled(k, terms({report.requested_displacement.front()}), report.reaction.front());
    const auto reaction_right = scaled(k, terms({report.requested_displacement.back()}), report.reaction.back());
    report.exterior_work_left_j = recorded(k, report, "port.exterior-work-left", exterior_left);
    report.exterior_work_right_j = recorded(k, report, "port.exterior-work-right", exterior_right);
    report.reaction_work_left_j = recorded(k, report, "port.reaction-work-left", reaction_left);
    report.reaction_work_right_j = recorded(k, report, "port.reaction-work-right", reaction_right);
    const auto ext_pi_left = scaled(k, terms({report.exterior_force.front()}), h), ext_pi_right = scaled(k, terms({report.exterior_force.back()}), h);
    const auto reaction_pi_left = scaled(k, terms({report.reaction.front()}), h), reaction_pi_right = scaled(k, terms({report.reaction.back()}), h);
    report.exterior_impulse_left = recorded(k, report, "port.exterior-impulse-left", ext_pi_left);
    report.exterior_impulse_right = recorded(k, report, "port.exterior-impulse-right", ext_pi_right);
    report.reaction_impulse_left = recorded(k, report, "port.reaction-impulse-left", reaction_pi_left);
    report.reaction_impulse_right = recorded(k, report, "port.reaction-impulse-right", reaction_pi_right);
    report.kinetic_defect_j = recorded(k, report, "energy.kinetic-defect", kinetic_defect);
    report.momentum_residual_work_j = recorded(k, report, "energy.momentum-residual-work", residual_work);
    report.kinematic_defect_work_j = recorded(k, report, "energy.kinematic-defect-work", kinematic_work);
    static_cast<void>(recorded(k, report, "energy.force-assembly-work", assembly_work));
    report.gravitational_potential_change_j = recorded(k, report, "energy.gravitational-potential-change", potential_change);
    report.gravity_work_j = recorded(k, report, "energy.gravity-work", gravity_work);
    const auto gravity_defect = joined(k, {gravity_work, potential_change});
    report.gravity_arithmetic_defect_j = recorded(k, report, "energy.gravity-defect", gravity_defect);
    k.scalar(Field::gravity_potential_change, report.gravitational_potential_change_j);
    k.scalar(Field::gravity_work, report.gravity_work_j); k.scalar(Field::gravity_defect, report.gravity_arithmetic_defect_j);
    k.within(report.gravity_arithmetic_defect_j, magnitude(k, gravity_defect), 256 * epsilon,
        "Gravity work does not close the stable potential-energy change within projection arithmetic");
    report.trial_residual_work_j = dot(k, report.requested_displacement, report.force_residual);
    Terms lhs = joined(k, {terms({k1, -k0}), potential_change}), owner_defects;
    Terms physical_scale = joined(k, {terms({k0, k1}), terms({std::abs(report.gravity_work_j), std::abs(report.gravitational_potential_change_j)})});
    append_expression(k, energy_scale, terms({k0, k1})); append_expression(k, energy_scale, kinetic_defect);
    append_expression(k, energy_scale, gravity_work); append_expression(k, energy_scale, potential_change); append_expression(k, energy_scale, gravity_defect);
    for (std::size_t i = 0; i < 7; ++i) {
        append_expression(k, lhs, terms(report.retained.internal_energy_change[i]));
        const auto& defect = recorded_terms(k, report, "owner.work-defect", canonical_absent_index, static_cast<std::uint32_t>(i));
        append_expression(k, owner_defects, defect); append_expression(k, energy_scale, defect);
        append_expression(k, energy_scale, terms(report.retained.internal_energy_change[i]));
        append_expression(k, energy_scale, recorded_terms(k, report, "owner.left-work", canonical_absent_index, static_cast<std::uint32_t>(i)));
        append_expression(k, energy_scale, recorded_terms(k, report, "owner.right-work", canonical_absent_index, static_cast<std::uint32_t>(i)));
        const auto& a = old.thermodynamics_readings[i]; const auto& b = next.thermodynamics_readings[i];
        append_expression(k, physical_scale, terms({a.thermal_energy_j, a.elastic_energy_j, b.thermal_energy_j, b.elastic_energy_j}));
    }
    for (const auto& work : {exterior_left, exterior_right, reaction_left, reaction_right}) {
        append_expression(k, lhs, negated(work)); append_expression(k, energy_scale, work);
    }
    const auto accounted = joined(k, {residual_work, kinematic_work, kinetic_defect, owner_defects, assembly_work, gravity_defect});
    const auto remainder = joined(k, {lhs, negated(accounted)});
    report.energy_error_j = recorded(k, report, "energy.physical-error", lhs);
    static_cast<void>(recorded(k, report, "energy.accounted-error", accounted));
    report.energy_budget_j = k.mul(2e-11, projected(k, physical_scale));
    report.accounting_remainder_j = recorded(k, report, "energy.accounting-remainder", remainder);
    report.accounting_bound_j = k.mul(256 * epsilon, magnitude(k, energy_scale));
    k.scalar(Field::energy_error, report.energy_error_j); k.scalar(Field::arithmetic_bound, report.energy_budget_j);
    k.require(std::abs(report.energy_error_j) <= report.energy_budget_j, Code::numerical_precision, "Actual retained-state physical energy gate failed");
    k.scalar(Field::energy_error, report.accounting_remainder_j); k.scalar(Field::arithmetic_bound, report.accounting_bound_j);
    k.require(std::abs(report.accounting_remainder_j) <= report.accounting_bound_j, Code::numerical_precision, "Complete retained-state finite energy identity failed");
    report.gravity_impulse = recorded(k, report, "momentum.gravity-impulse", gravity_impulse);
    const auto momentum_error = joined(k, {momentum_change, negated(ext_pi_left), negated(ext_pi_right), negated(reaction_pi_left), negated(reaction_pi_right), negated(gravity_impulse)});
    const auto momentum_remainder = joined(k, {momentum_error, negated(momentum_residual), negated(assembly_impulse)});
    report.momentum_error = recorded(k, report, "momentum.physical-error", momentum_error);
    static_cast<void>(recorded(k, report, "momentum.assembly-impulse", assembly_impulse));
    report.momentum_accounting_remainder = recorded(k, report, "momentum.accounting-remainder", momentum_remainder);
    report.momentum_accounting_bound = k.mul(256 * epsilon, magnitude(k, impulse_scale));
    k.within(report.momentum_accounting_remainder, magnitude(k, impulse_scale), 256 * epsilon, "Retained finite momentum identity failed");
    const auto first_error = joined(k, {first_change, negated(scaled(k, pbar_total, h))});
    report.first_moment_change = recorded(k, report, "first-moment.change", first_change);
    report.first_moment_error = recorded(k, report, "first-moment.error", first_error);
    static_cast<void>(recorded(k, report, "first-moment.predicted-error", first_prediction));
    report.first_moment_accounting_remainder = recorded(k, report, "first-moment.accounting-remainder", joined(k, {first_error, negated(first_prediction)}));
    report.first_moment_bound = k.mul(256 * epsilon, magnitude(k, first_scale));
    k.within(report.first_moment_accounting_remainder, magnitude(k, first_scale), 256 * epsilon, "Retained stable first-moment identity failed");
    report.relative_position_velocity_change = recorded(k, report, "bilinear.relative-change", relative_change);
    static_cast<void>(recorded(k, report, "bilinear.relative-canonical-change", relative_canonical));
    report.position_velocity_compatibility_change = recorded(k, report, "bilinear.compatibility-change", relative_compatibility);
    report.position_velocity_accounting_remainder = recorded(k, report, "bilinear.accounting-remainder",
        joined(k, {relative_change, negated(relative_canonical), negated(relative_compatibility)}));
    report.position_velocity_bound = k.mul(256 * epsilon, magnitude(k, product_scale));
    k.within(report.position_velocity_accounting_remainder, magnitude(k, product_scale), 256 * epsilon, "Retained relative bilinear identity failed");
    const auto origin_mean = scaled(k, joined(k, {terms(old.primary.origin_m), terms(next.primary.origin_m)}), .5);
    const auto origin_change = terms(report.retained.origin_change);
    const auto origin_canonical = joined(k, {products(k, origin_mean, momentum_change), products(k, origin_change, pbar_total)});
    const auto origin_compatible = joined(k, {products(k, origin_mean, delta_c_total), products(k, origin_change, cbar_total)});
    report.origin_position_velocity_change = recorded(k, report, "bilinear.origin-canonical-change", origin_canonical);
    report.origin_compatibility_change = recorded(k, report, "bilinear.origin-compatibility-change", origin_compatible);
    report.position_velocity_change = recorded(k, report, "bilinear.physical-change", joined(k, {relative_change, origin_canonical, origin_compatible}));
}
}
