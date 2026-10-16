#include "canonical_material_internal.hpp"
#include <new>
#include <type_traits>

namespace ma::experimental::canonical_detail {
namespace {
constexpr double residual_target = 2e-12;
struct Trial {
    Vec volume_change, pressure, stiffness, scaled, residual, scales, old_residual;
    std::vector<Terms> scaled_terms;
    double norm = 0;
};
std::vector<std::size_t> free_nodes(const Data& d) {
    std::vector<std::size_t> result;
    for (std::size_t j = 0; j < d.positions_m.size(); ++j) if (!constrained(d, j)) result.push_back(j);
    return result;
}
Matrix free_matrix(const Data& d, const std::vector<std::size_t>& free) {
    Matrix result = zeros(free.size());
    for (std::size_t i = 0; i < free.size(); ++i) for (std::size_t j = 0; j < free.size(); ++j)
        result[i][j] = d.consistent_mass[free[i]][free[j]];
    return result;
}
Vec selected(const Vec& values, const std::vector<std::size_t>& indices) {
    Vec result(indices.size()); for (std::size_t i = 0; i < indices.size(); ++i) result[i] = values[indices[i]]; return result;
}
bool domain_trial_error(ReferenceErrorCode code) noexcept {
    return code == ReferenceErrorCode::temperature_outside_model || code == ReferenceErrorCode::compression_outside_model ||
        code == ReferenceErrorCode::insufficient_thermal_energy;
}
bool trial(Kernel& k, const Data& d, const Vec& z, const Vec& u0, const Vec& c0, double h,
    const std::vector<std::size_t>& free, const Matrix& ff, const std::vector<ReferenceIsentrope>& anchors,
    bool permit_inadmissible, Trial& output, double& maximum_linear) {
    const auto n = z.size(); const double half_h = k.mul(.5, h), h2half = k.mul(.5, k.mul(h, h));
    const double inverse_h2half = k.div(1, h2half);
    for (std::size_t j = 0; j + 1 < n; ++j) {
        k.scope(Stage::pressure_trial, static_cast<std::uint32_t>(j));
        k.scalar(Field::position, d.positions_m[j], 0); k.scalar(Field::position, d.positions_m[j + 1], 1);
        k.scalar(Field::requested_displacement, z[j], 0); k.scalar(Field::requested_displacement, z[j + 1], 1);
        const auto before = retained_difference(k, d.primary.relative_positions_m[j + 1], d.primary.relative_positions_m[j]);
        const double length = projected(k, joined(k, {terms(before), terms({z[j + 1], -z[j]})}));
        if (length <= 0) {
            if (permit_inadmissible) return false;
            k.fail(Code::unsupported_topology, "Initial canonical Newton trial crosses fine geometry");
        }
    }
    Trial value; value.volume_change = volume_change(k, d, z); value.pressure.resize(7); value.stiffness.resize(7);
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::pressure_trial, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.row(Field::species_mass, d.cells[i].masses_kg);
        k.scalar(Field::stored_internal_energy, d.cells[i].internal_energy_j);
        k.scalar(Field::occupied_volume, d.cells[i].volume_m3); k.scalar(Field::requested_volume_change, value.volume_change[i]);
        try {
            k.charge(1); const auto response = d.thermodynamics->reference().isentropic_response(anchors[i], value.volume_change[i]);
            k.scalar(Field::balanced_energy_change, response.internal_energy_change_j);
            k.scalar(Field::stored_internal_energy, response.endpoint.internal_energy_j, 1);
            k.scalar(Field::occupied_volume, response.endpoint.volume_m3, 1);
            k.finite(response.state.mechanical_pressure_pa, Field::mechanical_pressure); k.finite(response.state.temperature_k, Field::temperature);
            if (response.state.has_vacuum || response.state.vacuum_volume_m3 > 0 || response.state.mechanical_pressure_pa <= 0 || response.state.temperature_k <= 0) {
                if (permit_inadmissible) return false;
                k.fail(Code::unsupported_topology, "Initial canonical pressure trial exits its occupied positive-pressure branch");
            }
            value.pressure[i] = k.positive(response.work_pressure_pa, Field::work_pressure);
            k.charge(1);
            value.stiffness[i] = -k.finite(d.thermodynamics->reference().isentropic_work_pressure_derivative(anchors[i], value.volume_change[i]), Field::work_stiffness);
            k.require(value.stiffness[i] > 0, Code::numerical_precision, "Canonical work-pressure stiffness is not positive");
        } catch (const ReferenceError& error) {
            k.scalar(Field::constitutive_error_code, static_cast<double>(error.code()));
            if (permit_inadmissible && domain_trial_error(error.code())) return false;
            k.fail(reference_failure_code(error.code()), "Canonical trial constitutive query failed");
        }
    }
    k.scope(Stage::pressure_trial);
    const Vec zf = selected(z, free), uf = selected(u0, free);
    const auto lz = multiply(k, ff, zf, maximum_linear, free), lu = multiply(k, ff, uf, maximum_linear, free);
    const auto exterior = exterior_force(k, d), body = gravity_force(k, d);
    Vec q(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::pressure_trial, static_cast<std::uint32_t>(j));
        k.scalar(Field::requested_displacement, z[j]); k.scalar(Field::velocity0, u0[j]);
        q[j] = difference(k, z[j], k.mul(h, u0[j]));
    }
    const auto lq = multiply(k, d.consistent_mass, q, maximum_linear);
    value.old_residual.resize(n); value.scaled.resize(n); value.residual.resize(n); value.scales.resize(n); value.scaled_terms.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::pressure_trial, static_cast<std::uint32_t>(j));
        k.row(Field::matrix_coefficients, d.consistent_mass[j]); k.row(Field::velocity0, u0);
        k.row(Field::requested_displacement, z); const auto parts = pressure_components(k, d, value.pressure, j);
        k.row(Field::pressure_force, parts); k.scalar(Field::exterior_force, exterior[j]); k.scalar(Field::pi0, d.momentum[j]);
        k.scalar(Field::gravity_acceleration, d.gravity_down_m_per_s2); k.scalar(Field::gravity_force, body[j]);
        k.scalar(Field::retained_momentum_low, d.primary.momentum[j].low());
        Sum old_residual(k, 0); old_residual.add(k.div(lq[j], h2half));
        for (double part : parts) old_residual.add(-part);
        old_residual.add(-exterior[j]); old_residual.add(-body[j]);
        value.old_residual[j] = old_residual.get();
        if (constrained(d, j)) {
            value.residual[j] = sum(k, {value.old_residual[j], k.div(c0[j], h)});
            value.scaled[j] = k.mul(h2half, value.residual[j]);
            value.scaled_terms[j] = scaled(k, terms({value.residual[j]}), h2half);
            Sum scale(k, 1); scale.add(std::abs(k.div(lq[j], h2half))); scale.add(std::abs(k.div(c0[j], h)));
            for (double part : parts) scale.add(std::abs(part));
            scale.add(std::abs(exterior[j])); scale.add(std::abs(body[j])); value.scales[j] = scale.get();
            continue;
        }
        const auto found = std::lower_bound(free.begin(), free.end(), j); const auto local = static_cast<std::size_t>(found - free.begin());
        Terms coupling;
        for (std::size_t p = 0; p < n; ++p) if (constrained(d, p))
            coupling = joined(k, {coupling, scaled(k, terms({boundary_velocity(d, p)}), d.consistent_mass[j][p])});
        Terms scaled_value = joined(k, {terms({lz[local]}), scaled(k, terms({lu[local]}), -half_h),
            scaled(k, terms(d.primary.momentum[j]), -half_h), scaled(k, coupling, half_h)});
        for (double part : parts) scaled_value = joined(k, {scaled_value, scaled(k, terms({part}), -h2half)});
        scaled_value = joined(k, {scaled_value, scaled(k, terms({exterior[j]}), -h2half)});
        scaled_value = joined(k, {scaled_value, scaled(k, terms({body[j]}), -h2half)});
        k.row(Field::matrix_products, scaled_value);
        value.scaled_terms[j] = scaled_value;
        value.scaled[j] = projected(k, scaled_value); value.residual[j] = k.div(value.scaled[j], h2half);
        Sum scale(k, 1);
        for (std::size_t p = 0; p < free.size(); ++p) scale.add(k.mul(inverse_h2half, std::abs(k.mul(ff[local][p], zf[p]))));
        for (std::size_t p = 0; p < free.size(); ++p) scale.add(k.div(std::abs(k.mul(ff[local][p], uf[p])), h));
        scale.add(k.div(std::abs(d.primary.momentum[j].high()), h));
        scale.add(k.div(std::abs(d.primary.momentum[j].low()), h));
        for (std::size_t p = 0; p < n; ++p) if (constrained(d, p)) scale.add(k.div(std::abs(k.mul(d.consistent_mass[j][p], boundary_velocity(d, p))), h));
        for (double part : parts) scale.add(std::abs(part));
        scale.add(std::abs(exterior[j]));
        scale.add(std::abs(body[j]));
        value.scales[j] = scale.get();
        const double equivalent = sum(k, {value.old_residual[j], k.div(c0[j], h)});
        k.within(difference(k, value.residual[j], equivalent), value.scales[j], 256 * epsilon, "Actual-pi0 residual differs from its c0 diagnostic identity");
        value.norm = std::max(value.norm, k.ratio(value.residual[j], value.scales[j]));
    }
    output = std::move(value); return true;
}
Matrix jacobian(Kernel& k, const Data& d, const Matrix& ff, const std::vector<std::size_t>& free, const Trial& trial_value, double h) {
    Matrix pressure = zeros(free.size()), result = zeros(free.size());
    const double h2half = k.mul(.5, k.mul(h, h));
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::pressure_trial, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.scalar(Field::work_stiffness, trial_value.stiffness[i]);
        const double coefficient = k.mul(k.mul(k.mul(h2half, trial_value.stiffness[i]), d.area_m2), d.area_m2);
        const auto left = std::lower_bound(free.begin(), free.end(), d.owner_left[i]);
        const auto right = std::lower_bound(free.begin(), free.end(), d.owner_right[i]);
        const bool has_left = left != free.end() && *left == d.owner_left[i];
        const bool has_right = right != free.end() && *right == d.owner_right[i];
        const auto a = static_cast<std::size_t>(left - free.begin()), b = static_cast<std::size_t>(right - free.begin());
        if (has_left) pressure[a][a] = sum(k, {pressure[a][a], coefficient});
        if (has_right) pressure[b][b] = sum(k, {pressure[b][b], coefficient});
        if (has_left && has_right) { const double cross = sum(k, {pressure[b][a], -coefficient}); pressure[b][a] = cross; pressure[a][b] = cross; }
    }
    for (std::size_t i = 0; i < free.size(); ++i) for (std::size_t j = 0; j <= i; ++j) {
        const double value = sum(k, {ff[i][j], pressure[i][j]}); result[i][j] = value; result[j][i] = value;
    }
    return result;
}
void node_frame(Kernel& k, Stage stage, const Data& old, const Data* next, const Vec& u0,
    const Vec& delta, const Vec& pressure, std::size_t j, bool owner_readings_known = false) {
    k.scope(stage, static_cast<std::uint32_t>(j));
    k.row(Field::matrix_coefficients, old.consistent_mass[j]); k.row(Field::velocity0, u0);
    k.scalar(Field::gravity_acceleration, old.gravity_down_m_per_s2);
    k.scalar(Field::retained_momentum_low, old.primary.momentum[j].low(), 0);
    k.scalar(Field::retained_position_low, old.primary.relative_positions_m[j].low(), 0);
    if (next != nullptr) k.row(Field::velocity1, next->velocities_m_per_s);
    if (next != nullptr) {
        k.scalar(Field::retained_momentum_low, next->primary.momentum[j].low(), 1);
        k.scalar(Field::retained_position_low, next->primary.relative_positions_m[j].low(), 1);
    }
    if (!delta.empty()) k.row(Field::delta_velocity, delta);
    const auto parts = pressure_components(k, old, pressure, j); k.row(Field::pressure_force, parts);
    for (std::size_t i = 0; i < 7; ++i) if (j == old.owner_left[i] || j == old.owner_right[i]) {
        const auto index = static_cast<std::uint32_t>(i * 2);
        k.scalar(Field::stored_internal_energy, old.cells[i].internal_energy_j, index);
        k.scalar(Field::occupied_volume, old.cells[i].volume_m3, index);
        k.scalar(Field::temperature, old.thermodynamics_readings[i].temperature_k, index);
        for (std::size_t s = 0; s < old.cells[i].masses_kg.size(); ++s)
            k.scalar(Field::species_mass, old.cells[i].masses_kg[s], static_cast<std::uint32_t>(i * 4 + s));
        if (next != nullptr && owner_readings_known) {
            k.scalar(Field::stored_internal_energy, next->cells[i].internal_energy_j, index + 1);
            k.scalar(Field::occupied_volume, next->cells[i].volume_m3, index + 1);
        }
    }
}
void increment(Kernel& k, double requested, double actual, double scale, std::string_view reason) {
    k.represented(requested, actual, reason); k.within(difference(k, actual, requested), scale, 256 * epsilon, reason);
}
void store_geometry(Kernel& k, const Data& old, Data& next, const Vec& z, CanonicalStepReport& report) {
    const auto n = z.size(); const auto anchor = old.left.kind == CanonicalBoundaryKind::fixed ? std::size_t{0} :
        old.right.kind == CanonicalBoundaryKind::fixed ? n - 1 : n / 2;
    k.scope(Stage::geometry); const double shift = z[anchor];
    k.scalar(Field::position, old.origin_high_m, 0); k.scalar(Field::position, old.origin_low_m, 1);
    k.scalar(Field::requested_displacement, shift);
    next.primary.origin_m = stored_update(k, report, "origin", old.primary.origin_m, shift);
    next.origin_high_m = next.primary.origin_m.high(); next.origin_low_m = next.primary.origin_m.low();
    report.retained.origin_change = retained_difference(k, next.primary.origin_m, old.primary.origin_m);
    const auto origin_change = terms(report.retained.origin_change);
    k.represented(shift, projected(k, origin_change), "Retained origin lost its requested increment", Field::requested_displacement, Field::represented_displacement);
    report.represented_displacement.resize(n);
    report.retained.relative_position_change.resize(n); report.retained.displacement.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::geometry, static_cast<std::uint32_t>(j)); k.scalar(Field::position, old.positions_m[j]);
        k.scalar(Field::position, old.positions_m[j], 0); k.scalar(Field::requested_displacement, z[j], 0);
        k.scalar(Field::retained_position_low, old.primary.relative_positions_m[j].low(), 0);
        const auto relative = retained_difference(k, retained_value(k, z[j]), retained_value(k, shift));
        for (double remainder : relative.roundoff_remainders)
            k.require(remainder == 0, Code::numerical_precision, "Scalar trial displacement difference exceeds two components");
        const auto position = stored_update(k, report, "relative-position", old.primary.relative_positions_m[j],
            relative.value, static_cast<std::uint32_t>(j));
        next.primary.relative_positions_m[j] = position; next.positions_m[j] = position.high();
        k.scalar(Field::position, position.high(), 1); k.scalar(Field::retained_position_low, position.low(), 1);
        report.retained.relative_position_change[j] = retained_difference(k, position, old.primary.relative_positions_m[j]);
        const auto relative_change = terms(report.retained.relative_position_change[j]);
        k.represented(projected(k, terms(relative)), projected(k, relative_change), "Retained relative position lost its requested increment",
            Field::requested_displacement, Field::represented_displacement);
        const auto displacement = joined(k, {origin_change, relative_change});
        k.scalar(Field::position, old.primary.origin_m.high(), 4); k.scalar(Field::position, old.primary.origin_m.low(), 5);
        k.scalar(Field::position, next.primary.origin_m.high(), 6); k.scalar(Field::position, next.primary.origin_m.low(), 7);
        report.retained.displacement[j] = expression(k, displacement);
        const double stored = report.retained.displacement[j].projection;
        report.represented_displacement[j] = stored;
        k.represented(z[j], stored, "Canonical physical position lost its requested increment", Field::requested_displacement, Field::represented_displacement);
        if (constrained(old, j)) k.require(exact_zero(k, joined(k, {displacement, terms({-z[j]})})), Code::numerical_precision,
            "Prescribed displacement is not exact in complete retained geometry");
    }
    report.prescribed_displacements_exact = true;
    for (std::size_t j = 0; j + 1 < n; ++j) {
        k.scope(Stage::geometry, static_cast<std::uint32_t>(j));
        k.scalar(Field::position, old.positions_m[j], 0); k.scalar(Field::position, old.positions_m[j + 1], 1);
        k.scalar(Field::position, next.positions_m[j], 2); k.scalar(Field::position, next.positions_m[j + 1], 3);
        k.row(Field::requested_displacement, z);
        const auto before = retained_difference(k, old.primary.relative_positions_m[j + 1], old.primary.relative_positions_m[j]);
        const auto after = retained_difference(k, next.primary.relative_positions_m[j + 1], next.primary.relative_positions_m[j]);
        k.require(after.value.high() > 0, Code::unsupported_topology, "Stored retained fine nodes crossed");
        const auto defect = joined(k, {terms(after), negated(terms(before)), terms({-z[j + 1], z[j]})});
        k.within(projected(k, defect), projected(k, terms(after)), 32 * epsilon, "Stored fine geometry differs from its requested increment");
    }
}
bool same_values(const Vec& a, const Vec& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i])) return false;
    return true;
}
std::vector<Terms> scalar_terms(const Vec& values) {
    std::vector<Terms> result; result.reserve(values.size());
    for (double value : values) result.push_back(terms({value}));
    return result;
}
struct ProductRows { Matrix zero, one, delta; };
void product_frame(Kernel& k, Stage stage, const Data& old, const Data& next, const Vec& u0,
    const CanonicalStepReport& report, const ProductRows& products, std::size_t node, bool owners_known = false) {
    node_frame(k, stage, old, &next, u0, report.delta_velocity, report.work_pressure, node, owners_known);
    k.row(Field::matrix_products, products.zero[node], 0); k.row(Field::matrix_products, products.one[node], 1);
    k.row(Field::matrix_products, products.delta[node], 2);
    k.scalar(Field::compatibility0, report.compatibility0[node]);
    if (!report.compatibility1.empty()) k.scalar(Field::compatibility1, report.compatibility1[node]);
}
void update_owners(Kernel& k, const Data& old, Data& next, const std::vector<ReferenceIsentrope>& anchors,
    const Trial& accepted, CanonicalStepReport& report) {
    report.retained.volume_change.resize(7); report.retained.internal_energy_change.resize(7);
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::thermodynamics, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.row(Field::species_mass, old.cells[i].masses_kg); k.scalar(Field::stored_internal_energy, old.cells[i].internal_energy_j);
        k.scalar(Field::occupied_volume, old.cells[i].volume_m3); k.scalar(Field::requested_volume_change, accepted.volume_change[i]);
        k.scalar(Field::work_pressure, accepted.pressure[i]); k.charge(1);
        const auto response = old.thermodynamics->reference().isentropic_response(anchors[i], accepted.volume_change[i]);
        k.require(same_values(response.endpoint.masses_kg, old.cells[i].masses_kg), Code::numerical_precision, "Isentropic response changed retained constituent bits");
        next.owner_geometry[i] = measure_geometry(k, next.primary.relative_positions_m[next.owner_left[i]],
            next.primary.relative_positions_m[next.owner_right[i]], next.area_m2);
        next.cells[i].volume_m3 = next.owner_geometry[i].projected_volume_m3;
        next.primary.internal_energy_j[i] = stored_update(k, report, "internal-energy", old.primary.internal_energy_j[i],
            response.internal_energy_change_j, canonical_absent_index, static_cast<std::uint32_t>(i));
        next.cells[i].internal_energy_j = next.primary.internal_energy_j[i].high();
        const auto dv_terms = joined(k, {terms(next.owner_geometry[i]), negated(terms(old.owner_geometry[i]))});
        report.retained.volume_change[i] = expression(k, dv_terms);
        report.retained.internal_energy_change[i] = retained_difference(k, next.primary.internal_energy_j[i], old.primary.internal_energy_j[i]);
        const auto du_terms = terms(report.retained.internal_energy_change[i]);
        const auto incidence_terms = scaled(k, joined(k, {report.retained.displacement[old.owner_right[i]].terms,
            negated(report.retained.displacement[old.owner_left[i]].terms)}), old.area_m2);
        const double incidence = recorded(k, report, "owner.incidence-volume", incidence_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        const double dv = report.retained.volume_change[i].projection, du = projected(k, du_terms);
        k.represented(accepted.volume_change[i], dv, "Stored owner volume lost its requested change", Field::requested_volume_change, Field::represented_volume_change);
        k.represented(response.internal_energy_change_j, du, "Stored owner U lost its balanced change", Field::balanced_energy_change, Field::represented_energy_change);
        k.within(projected(k, joined(k, {dv_terms, terms({-accepted.volume_change[i]})})), next.cells[i].volume_m3,
            32 * epsilon, "Stored owner volume differs from accepted trial geometry");
        next.thermodynamics_readings[i] = evaluate_owner(k, next, next.cells[i], i);
        const auto& before = old.thermodynamics_readings[i]; const auto& after = next.thermodynamics_readings[i];
        const double local_scale = sum(k, {before.thermal_energy_j, before.elastic_energy_j, after.thermal_energy_j, after.elastic_energy_j});
        k.within(projected(k, joined(k, {du_terms, terms({-response.internal_energy_change_j})})), local_scale, 64 * epsilon,
            "Stored U increment exceeds its local thermal/elastic arithmetic bound");
        k.within(projected(k, joined(k, {dv_terms, negated(incidence_terms)})), next.cells[i].volume_m3, 32 * epsilon,
            "Owner stored volume differs from B times complete stored displacement");
        const double left_force = -k.mul(old.area_m2, accepted.pressure[i]), right_force = k.mul(old.area_m2, accepted.pressure[i]);
        const auto left_work_terms = scaled(k, report.retained.displacement[old.owner_left[i]].terms, left_force);
        const auto right_work_terms = scaled(k, report.retained.displacement[old.owner_right[i]].terms, right_force);
        const auto incidence_work_terms = scaled(k, incidence_terms, accepted.pressure[i]);
        const auto cap_defect_terms = joined(k, {left_work_terms, right_work_terms, negated(incidence_work_terms)});
        const double left_work = recorded(k, report, "owner.left-work", left_work_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        const double right_work = recorded(k, report, "owner.right-work", right_work_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        const double incidence_work = projected(k, incidence_work_terms);
        const double cap_defect = recorded(k, report, "owner.cap-work-defect", cap_defect_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.within(cap_defect,
            sum(k, {std::abs(left_work), std::abs(right_work), std::abs(incidence_work)}), 256 * epsilon,
            "Cap work differs from owner incidence work");
        report.cap_impulse_left.push_back(projected(k, scaled(k, terms({left_force}), report.seconds)));
        report.cap_impulse_right.push_back(projected(k, scaled(k, terms({right_force}), report.seconds)));
        report.cap_work_left.push_back(left_work); report.cap_work_right.push_back(right_work);
        report.incidence_volume_change.push_back(incidence); report.represented_volume_change.push_back(dv);
        report.volume_storage_difference.push_back(projected(k, joined(k, {dv_terms, negated(incidence_terms)})));
        report.represented_energy_change.push_back(du); report.balanced_energy_change.push_back(response.internal_energy_change_j);
        report.eos_represented_volume_change.push_back(response.represented_volume_change_m3);
        report.eos_represented_energy_change.push_back(response.represented_internal_energy_change_j);
        report.endpoint_volume_resolves.push_back(response.endpoint_volume_resolves_change);
        report.endpoint_energy_resolves.push_back(response.endpoint_energy_resolves_change);
        const auto store_terms = joined(k, {du_terms, terms({-response.internal_energy_change_j})});
        const auto secant_terms = joined(k, {terms({response.internal_energy_change_j}), scaled(k, terms({accepted.volume_change[i]}), accepted.pressure[i])});
        const auto volume_terms = scaled(k, joined(k, {incidence_terms, terms({-accepted.volume_change[i]})}), accepted.pressure[i]);
        const auto defect_terms = joined(k, {du_terms, left_work_terms, right_work_terms});
        const double store = recorded(k, report, "owner.stored-energy-defect", store_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        const double secant = recorded(k, report, "owner.secant-defect", secant_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        const double volume = recorded(k, report, "owner.volume-work-defect", volume_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        const double defect = recorded(k, report, "owner.work-defect", defect_terms, canonical_absent_index, static_cast<std::uint32_t>(i));
        report.stored_energy_defect.push_back(store); report.secant_defect.push_back(secant);
        report.volume_work_defect.push_back(volume); report.owner_work_defect.push_back(defect);
        k.within(projected(k, joined(k, {defect_terms, negated(store_terms), negated(secant_terms), negated(volume_terms), negated(cap_defect_terms)})),
            sum(k, {std::abs(du), std::abs(incidence_work), std::abs(response.internal_energy_change_j)}), 128 * epsilon,
            "Owner work storage/secant/incidence split does not close");
        check_thermodynamics(k, old, next, i, report);
        const double independent = independent_work_pressure(k, old, i, accepted.volume_change[i]);
        const double error = difference(k, independent, accepted.pressure[i]);
        const double scale = std::max(std::abs(independent), std::abs(accepted.pressure[i]));
        k.within(error, scale, 2e-10, "Independent bounded Simpson work-pressure comparison failed");
        report.maximum_work_oracle_ratio = std::max(report.maximum_work_oracle_ratio, k.ratio(error, scale));
    }
    Sum drift(k); for (double value : report.entropy_change) drift.add(value); report.total_homogeneous_entropy_change = drift.get();
}
Data advance_data(Kernel& k, const Data& old, double h) {
    k.scope(Stage::pressure_trial); k.step_context(h, old.completed_time_seconds); k.deadline(); k.positive(h, Field::timestep);
    k.owner_nodes(old.owner_left, old.owner_right); k.baseline(Field::pi0, old.momentum);
    k.baseline(Field::velocity0, old.velocities_m_per_s);
    Data next = old; CanonicalStepReport report; report.seconds = h;
    const auto n = old.positions_m.size(); const auto free = free_nodes(old); const auto ff = free_matrix(old, free);
    const auto u0 = decode_velocity(k, old, report.linear_residual_ratio);
    k.require(same_values(u0, old.velocities_m_per_s), Code::numerical_precision, "Completed velocity cache differs from deterministic canonical decoding");
    k.baseline(Field::velocity0, u0); k.baseline(Field::velocity1, {});
    report.decoded_product0 = multiply(k, old.consistent_mass, u0, report.linear_residual_ratio);
    report.compatibility0.resize(n);
    const auto initial_velocity_terms = scalar_terms(u0);
    std::vector<Terms> initial_products(n), final_products(n), compatibility0(n), compatibility1(n), velocity_delta(n), mass_delta(n), required_impulse(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::canonical_decode, static_cast<std::uint32_t>(j));
        initial_products[j] = row_terms(k, old.consistent_mass, initial_velocity_terms, j);
        compatibility0[j] = joined(k, {initial_products[j], negated(terms(old.primary.momentum[j]))});
        report.compatibility0[j] = recorded(k, report, "node.compatibility0", compatibility0[j], static_cast<std::uint32_t>(j));
    }
    std::vector<ReferenceIsentrope> anchors; anchors.reserve(7);
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::thermodynamics, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.row(Field::species_mass, old.cells[i].masses_kg); k.scalar(Field::stored_internal_energy, old.cells[i].internal_energy_j);
        k.scalar(Field::occupied_volume, old.cells[i].volume_m3); k.charge(2);
        anchors.push_back(old.thermodynamics->reference().make_isentrope(old.cells[i]));
    }
    Vec z(n); for (std::size_t j = 0; j < n; ++j) z[j] = k.mul(h, u0[j]);
    Trial current; k.scope(Stage::pressure_trial);
    static_cast<void>(trial(k, old, z, u0, report.compatibility0, h, free, ff, anchors, false, current, report.linear_residual_ratio));
    while (current.norm > residual_target) {
        k.newton(); const auto hessian = jacobian(k, old, ff, free, current, h);
        Vec rhs(free.size()); for (std::size_t i = 0; i < free.size(); ++i) rhs[i] = -current.scaled[free[i]];
        const auto direction = solve_spd(k, hessian, rhs, report.linear_residual_ratio, free);
        bool accepted = false; double fraction = 1;
        for (std::size_t attempt = 0; attempt < 24; ++attempt) {
            if (attempt != 0) { fraction = k.mul(.5, fraction); k.backtrack(); }
            Vec candidate = z;
            for (std::size_t i = 0; i < free.size(); ++i) {
                const auto j = free[i]; k.scope(Stage::pressure_trial, static_cast<std::uint32_t>(j));
                k.row(Field::requested_displacement, z); k.scalar(Field::requested_displacement, z[j], 0);
                k.scalar(Field::requested_displacement, z[j]); const double requested = k.mul(fraction, direction[i]);
                candidate[j] = sum(k, {z[j], requested});
                k.scalar(Field::represented_displacement, candidate[j], 1);
                k.represented(requested, difference(k, candidate[j], z[j]), "Canonical Newton displacement increment is unrepresentable",
                    Field::requested_displacement, Field::represented_displacement);
            }
            Trial proposed;
            const bool admissible = trial(k, old, candidate, u0, report.compatibility0, h, free, ff, anchors, true, proposed, report.linear_residual_ratio);
            k.scalar(Field::backtracking_fraction, fraction); k.scalar(Field::residual_ratio, current.norm, 0);
            k.row(Field::requested_displacement, candidate);
            if (admissible) k.scalar(Field::residual_ratio, proposed.norm, 1);
            if (admissible && (proposed.norm < current.norm || proposed.norm <= residual_target)) {
                z = std::move(candidate); current = std::move(proposed); accepted = true; break;
            }
        }
        k.require(accepted, Code::numerical_precision, "Canonical same-timestep backtracking exhausted its fixed budget");
    }
    report.requested_displacement = z; report.requested_volume_change = current.volume_change;
    report.work_pressure = current.pressure; report.work_stiffness = current.stiffness;
    report.scaled_residual = current.scaled; report.force_residual = current.residual; report.force_scales = current.scales;
    report.old_style_trial_residual = current.old_residual; report.force_residual_ratio = current.norm;
    for (std::size_t j = 0; j < n; ++j)
        static_cast<void>(recorded(k, report, "trial.scaled-residual", current.scaled_terms[j], static_cast<std::uint32_t>(j)));
    report.pressure_force = pressure_force(k, old, current.pressure); report.exterior_force = exterior_force(k, old);
    report.gravity_force = gravity_force(k, old);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::pressure_trial, static_cast<std::uint32_t>(j));
        k.scalar(Field::gravity_acceleration, old.gravity_down_m_per_s2); k.scalar(Field::gravity_force, report.gravity_force[j]);
        k.row(Field::matrix_coefficients, old.consistent_mass[j]); Terms weights;
        for (double coefficient : old.consistent_mass[j]) weights = joined(k, {weights, terms({coefficient})});
        const auto load = scaled(k, weights, -old.gravity_down_m_per_s2);
        const double projection = recorded(k, report, "node.gravity-load", load, static_cast<std::uint32_t>(j));
        k.require(std::bit_cast<std::uint64_t>(projection) == std::bit_cast<std::uint64_t>(report.gravity_force[j]),
            Code::numerical_precision, "Actual gravity force differs from its declared mass-load projection");
        static_cast<void>(recorded(k, report, "node.gravity-projection-defect", joined(k, {terms({report.gravity_force[j]}), negated(load)}), static_cast<std::uint32_t>(j)));
    }
    report.requested_impulse.resize(n); report.represented_impulse.resize(n); report.free_addition_defect.resize(n);
    report.constrained_linearity_defect.resize(n); report.constrained_join_defect.resize(n); report.constrained_impulse_defect.resize(n);
    report.constrained_linearity_bound.resize(n); report.constrained_join_bound.resize(n); report.constrained_impulse_bound.resize(n);
    k.baseline(Field::pi1, {});
    for (const auto j : free) {
        node_frame(k, Stage::free_impulse, old, nullptr, u0, {}, current.pressure, j);
        Sum force(k); const auto parts = pressure_components(k, old, current.pressure, j);
        for (double value : parts) force.add(value);
        k.scalar(Field::gravity_force, report.gravity_force[j]); force.add(report.gravity_force[j]); force.add(report.exterior_force[j]);
        const auto impulse = retained_product(k, retained_value(k, force.get()), h);
        for (double remainder : impulse.roundoff_remainders)
            k.require(remainder == 0, Code::numerical_precision, "Scalar force impulse exceeds its exact product pair");
        required_impulse[j] = terms(impulse.value);
        const double required = recorded(k, report, "node.requested-impulse", required_impulse[j], static_cast<std::uint32_t>(j));
        report.requested_impulse[j] = required;
        const auto momentum = stored_update(k, report, "free-impulse", old.primary.momentum[j], impulse.value, static_cast<std::uint32_t>(j));
        next.primary.momentum[j] = momentum; next.momentum[j] = momentum.high(); k.baseline_value(Field::pi1, j, next.momentum[j]);
        const auto actual_terms = terms(retained_difference(k, momentum, old.primary.momentum[j]));
        const double actual = projected(k, actual_terms); report.represented_impulse[j] = actual;
        const double scale = magnitude(k, joined(k, {terms(old.primary.momentum[j]), terms(momentum), required_impulse[j]}));
        increment(k, required, actual, scale, "Free force-primary canonical impulse was erased or reversed");
        report.free_addition_defect[j] = recorded(k, report, "node.free-addition-defect", joined(k, {actual_terms, negated(required_impulse[j])}), static_cast<std::uint32_t>(j));
        k.within(report.free_addition_defect[j], scale, 256 * epsilon, "Retained free-impulse addition exceeds its arithmetic bound");
    }
    next.velocities_m_per_s = decode_velocity(k, next, report.linear_residual_ratio, false);
    report.decoded_product1 = multiply(k, old.consistent_mass, next.velocities_m_per_s, report.linear_residual_ratio);
    for (std::size_t j = 0; j < n; ++j) if (constrained(old, j)) {
        k.scope(Stage::constrained_impulse, static_cast<std::uint32_t>(j));
        auto construction = momentum_row(k, old, next.velocities_m_per_s, j);
        next.primary.momentum[j] = construction.value; next.momentum[j] = construction.value.high();
        report.retained.constrained_momentum.push_back(std::move(construction)); k.baseline_value(Field::pi1, j, next.momentum[j]);
    }
    report.delta_velocity.resize(n); report.compatibility1.resize(n);
    report.retained.velocity_change.resize(n); report.retained.momentum_change.resize(n);
    const auto final_velocity_terms = scalar_terms(next.velocities_m_per_s);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::constrained_impulse, static_cast<std::uint32_t>(j));
        report.retained.velocity_change[j] = retained_difference(k, retained_value(k, next.velocities_m_per_s[j]), retained_value(k, u0[j]));
        velocity_delta[j] = terms(report.retained.velocity_change[j]);
        report.delta_velocity[j] = projected(k, velocity_delta[j]);
        report.retained.momentum_change[j] = retained_difference(k, next.primary.momentum[j], old.primary.momentum[j]);
        final_products[j] = row_terms(k, old.consistent_mass, final_velocity_terms, j);
        compatibility1[j] = joined(k, {final_products[j], negated(terms(next.primary.momentum[j]))});
        report.compatibility1[j] = recorded(k, report, "node.compatibility1", compatibility1[j], static_cast<std::uint32_t>(j));
    }
    report.mass_delta_velocity.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::constrained_impulse, static_cast<std::uint32_t>(j));
        mass_delta[j] = row_terms(k, old.consistent_mass, velocity_delta, j);
        report.mass_delta_velocity[j] = recorded(k, report, "node.mass-delta-velocity", mass_delta[j], static_cast<std::uint32_t>(j));
    }
    ProductRows products{Matrix(n), Matrix(n), Matrix(n)};
    for (std::size_t j = 0; j < n; ++j) {
        products.zero[j] = physical_row_products(k, old.consistent_mass, u0, j);
        products.one[j] = physical_row_products(k, old.consistent_mass, next.velocities_m_per_s, j);
        products.delta[j] = physical_row_products(k, old.consistent_mass, report.delta_velocity, j);
    }
    for (std::size_t j = 0; j < n; ++j) {
        product_frame(k, Stage::constrained_impulse, old, next, u0, report, products, j);
        const double a0 = magnitude(k, initial_products[j]), a1 = magnitude(k, final_products[j]), ad = magnitude(k, mass_delta[j]);
        k.within(report.compatibility0[j], sum(k, {a0, magnitude(k, terms(old.primary.momentum[j]))}), 256 * epsilon, "Start canonical compatibility failed");
        k.within(report.compatibility1[j], sum(k, {a1, magnitude(k, terms(next.primary.momentum[j]))}), 256 * epsilon, "Endpoint canonical compatibility failed");
        if (!constrained(old, j)) continue;
        required_impulse[j] = joined(k, {mass_delta[j], compatibility0[j], negated(compatibility1[j])});
        const double required = recorded(k, report, "node.requested-impulse", required_impulse[j], static_cast<std::uint32_t>(j));
        const auto actual_terms = terms(report.retained.momentum_change[j]);
        const double actual = projected(k, actual_terms);
        report.requested_impulse[j] = required; report.represented_impulse[j] = actual;
        k.scalar(Field::mass_delta_velocity, report.mass_delta_velocity[j]);
        const auto linearity_terms = joined(k, {mass_delta[j], initial_products[j], negated(final_products[j])});
        const double linearity = recorded(k, report, "node.constrained-linearity-defect", linearity_terms, static_cast<std::uint32_t>(j));
        const double linearity_scale = sum(k, {ad, a0, a1});
        const double join = recorded(k, report, "node.constrained-join-defect", joined(k, {required_impulse[j], negated(actual_terms), negated(linearity_terms)}), static_cast<std::uint32_t>(j));
        const double join_scale = magnitude(k, joined(k, {mass_delta[j], initial_products[j], final_products[j], compatibility0[j], compatibility1[j],
            terms(old.primary.momentum[j]), terms(next.primary.momentum[j]), required_impulse[j], actual_terms}));
        const double defect = recorded(k, report, "node.constrained-impulse-defect", joined(k, {required_impulse[j], negated(actual_terms)}), static_cast<std::uint32_t>(j));
        const double total_scale = sum(k, {linearity_scale, join_scale});
        k.represented(required, actual, "Constrained canonical impulse was erased or reversed");
        k.represented(actual, required, "Constrained impulse projection erased a nonzero stored change");
        k.scalar(Field::linearity_defect, linearity); k.scalar(Field::join_defect, join); k.scalar(Field::impulse_defect, defect);
        k.within(linearity, linearity_scale, 256 * epsilon, "Constrained product-linearity arithmetic exceeded its bound");
        k.within(join, join_scale, 256 * epsilon, "Constrained compatibility/join arithmetic exceeded its bound");
        k.within(defect, total_scale, 256 * epsilon, "Constrained required/stored impulse difference exceeded its bound");
        report.constrained_linearity_defect[j] = linearity; report.constrained_join_defect[j] = join; report.constrained_impulse_defect[j] = defect;
        report.constrained_linearity_bound[j] = k.mul(256 * epsilon, linearity_scale);
        report.constrained_join_bound[j] = k.mul(256 * epsilon, join_scale); report.constrained_impulse_bound[j] = k.mul(256 * epsilon, total_scale);
    }
    report.reaction.assign(n, 0); report.actual_force.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        product_frame(k, Stage::reaction, old, next, u0, report, products, j);
        const auto parts = pressure_components(k, old, current.pressure, j);
        if (constrained(old, j)) {
            Sum reaction(k); reaction.add(k.div(report.requested_impulse[j], h));
            for (double part : parts) reaction.add(-part);
            reaction.add(-report.exterior_force[j]); reaction.add(-report.gravity_force[j]); report.reaction[j] = reaction.get();
        }
        Sum total(k, 1); for (double part : parts) total.add(part);
        total.add(report.gravity_force[j]); total.add(report.exterior_force[j]); total.add(report.reaction[j]); report.actual_force[j] = total.get();
        k.scalar(Field::gravity_force, report.gravity_force[j]);
        k.scalar(Field::reaction_force, report.reaction[j]); k.scalar(Field::assembled_force, report.actual_force[j]);
        const double assembled = projected(k, scaled(k, terms({report.actual_force[j]}), h)); k.scalar(Field::assembled_impulse, assembled);
        k.represented(report.requested_impulse[j], assembled, "Actual force assembly erased or reversed its required impulse");
        k.represented(assembled, report.represented_impulse[j], "Stored impulse erased or reversed actual assembled force");
        k.represented(report.represented_impulse[j], assembled, "Actual force assembly erased stored canonical impulse");
        Terms component_impulses = scaled(k, terms({report.reaction[j], report.exterior_force[j], report.gravity_force[j]}), h);
        for (double part : parts) component_impulses = joined(k, {component_impulses, scaled(k, terms({part}), h)});
        const auto defect_terms = joined(k, {component_impulses, negated(required_impulse[j])});
        const double defect = recorded(k, report, "node.reaction-impulse-defect", defect_terms, static_cast<std::uint32_t>(j));
        k.within(defect, magnitude(k, joined(k, {component_impulses, required_impulse[j]})), 256 * epsilon,
            "Actual reaction/force assembly does not reproduce required canonical impulse");
    }
    store_geometry(k, old, next, z, report); update_owners(k, old, next, anchors, current, report);
    update_readings(k, next, report.maximum_quadrature_ratio);
    finite_ledgers(k, old, next, u0, report);
    k.scope(Stage::publication); k.scalar(Field::completed_time, old.completed_time_seconds); k.scalar(Field::timestep, h);
    next.primary.completed_time_seconds = stored_update(k, report, "completed-time", old.primary.completed_time_seconds, h);
    next.completed_time_seconds = next.primary.completed_time_seconds.high();
    report.retained.time_change = retained_difference(k, next.primary.completed_time_seconds, old.primary.completed_time_seconds);
    report.represented_seconds = projected(k, terms(report.retained.time_change));
    k.represented(h, report.represented_seconds, "Completed canonical time lost its requested increment", Field::timestep, Field::completed_time);
    k.require(old.completed_steps < std::numeric_limits<std::uint64_t>::max(), Code::unsupported_configuration, "Canonical completed-step index exhausted");
    next.completed_steps = old.completed_steps + 1;
    next.cumulative_energy_error_j = sum(k, {old.cumulative_energy_error_j, report.energy_error_j});
    report.newton_updates = k.newton_updates; report.backtracks = k.backtracks; report.factorizations = k.factorizations;
    report.constitutive_charges = k.charged_here(); next.last_step = std::move(report);
    validate_scalar_projections(k, next);
    return next;
}
}
}

namespace ma::experimental {
void advance_canonical_material_into(const CanonicalMaterialState& input, double seconds, CanonicalMaterialState& output,
    CanonicalBudgetContext& budget, CanonicalFailureWitness& witness) {
    using namespace canonical_detail;
    const std::string_view identity = input.data_ ? std::string_view(input.data_->diagnostic_model_identity) : std::string_view("uninitialized-canonical-state");
    Kernel kernel(budget, witness, false, identity, input.data_ ? input.data_->completed_steps : 0);
    try {
        kernel.scope(Stage::publication);
        kernel.require(&input != &output, Code::unsupported_configuration, "Canonical input/output state objects alias");
        kernel.require(static_cast<bool>(input.data_), Code::unsupported_configuration, "Canonical input is uninitialized, not vacuum");
        kernel.require(canonical_material_advance_ready(), Code::unsupported_configuration,
            "Retained canonical pressure step is unavailable in this build");
        auto candidate = advance_data(kernel, *input.data_, seconds);
        kernel.scope(Stage::publication); kernel.deadline();
        kernel.require(read_canonical_arithmetic_environment().supported(), Code::numerical_precision, "Canonical arithmetic controls changed during advance");
        auto complete = std::make_shared<const CanonicalSnapshot>(std::move(candidate));
        static_assert(std::is_nothrow_move_assignable_v<decltype(output.data_)>);
        output.data_ = std::move(complete);
    } catch (const CanonicalFailure&) { throw; }
    catch (const ReferenceError& error) {
        kernel.scalar(Field::constitutive_error_code, static_cast<double>(error.code()));
        kernel.fail(reference_failure_code(error.code()), "Canonical advance reference model rejected its input");
    }
    catch (const std::bad_alloc&) { kernel.fail(Code::unsupported_configuration, "Canonical advance allocation failed"); }
    catch (const std::exception&) { kernel.fail(Code::unsupported_configuration, "Canonical advance model or bounds validation failed"); }
}
}
