#include "canonical_material_internal.hpp"
#include <new>
#include <type_traits>

namespace ma::experimental::canonical_detail {
namespace {
constexpr double residual_target = 2e-12;
struct Trial {
    Vec volume_change, pressure, stiffness, scaled, residual, scales, old_residual;
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
        const double length = sum(k, {difference(k, d.positions_m[j + 1], d.positions_m[j]), difference(k, z[j + 1], z[j])});
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
    const auto exterior = exterior_force(k, d);
    Vec q(n); for (std::size_t j = 0; j < n; ++j) q[j] = difference(k, z[j], k.mul(h, u0[j]));
    const auto lq = multiply(k, d.consistent_mass, q, maximum_linear);
    value.old_residual.resize(n); value.scaled.resize(n); value.residual.resize(n); value.scales.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::pressure_trial, static_cast<std::uint32_t>(j));
        k.row(Field::matrix_coefficients, d.consistent_mass[j]); k.row(Field::velocity0, u0);
        k.row(Field::requested_displacement, z); const auto parts = pressure_components(k, d, value.pressure, j);
        k.row(Field::pressure_force, parts); k.scalar(Field::exterior_force, exterior[j]); k.scalar(Field::pi0, d.momentum[j]);
        Sum old_residual(k, 0); old_residual.add(k.div(lq[j], h2half));
        for (double part : parts) old_residual.add(-part);
        old_residual.add(-exterior[j]);
        value.old_residual[j] = old_residual.get();
        if (constrained(d, j)) {
            value.residual[j] = sum(k, {value.old_residual[j], k.div(c0[j], h)});
            value.scaled[j] = k.mul(h2half, value.residual[j]);
            Sum scale(k, 1); scale.add(std::abs(k.div(lq[j], h2half))); scale.add(std::abs(k.div(c0[j], h)));
            for (double part : parts) scale.add(std::abs(part));
            scale.add(std::abs(exterior[j])); value.scales[j] = scale.get();
            continue;
        }
        const auto found = std::lower_bound(free.begin(), free.end(), j); const auto local = static_cast<std::size_t>(found - free.begin());
        Sum coupling(k, 2);
        for (std::size_t p = 0; p < n; ++p) if (constrained(d, p)) coupling.add(k.mul(d.consistent_mass[j][p], boundary_velocity(d, p)));
        Sum scaled(k, 0); Vec terms;
        const auto term = [&](double entry) { terms.push_back(entry); k.row(Field::matrix_products, terms); scaled.add(entry); };
        term(lz[local]); term(-k.mul(half_h, lu[local])); term(-k.mul(half_h, d.momentum[j]));
        term(k.mul(half_h, coupling.get()));
        for (double part : parts) term(-k.mul(h2half, part));
        term(-k.mul(h2half, exterior[j]));
        value.scaled[j] = scaled.get(); value.residual[j] = k.div(value.scaled[j], h2half);
        Sum scale(k, 1);
        for (std::size_t p = 0; p < free.size(); ++p) scale.add(k.mul(inverse_h2half, std::abs(k.mul(ff[local][p], zf[p]))));
        for (std::size_t p = 0; p < free.size(); ++p) scale.add(k.div(std::abs(k.mul(ff[local][p], uf[p])), h));
        scale.add(k.div(std::abs(d.momentum[j]), h));
        for (std::size_t p = 0; p < n; ++p) if (constrained(d, p)) scale.add(k.div(std::abs(k.mul(d.consistent_mass[j][p], boundary_velocity(d, p))), h));
        for (double part : parts) scale.add(std::abs(part));
        scale.add(std::abs(exterior[j]));
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
    if (next != nullptr) k.row(Field::velocity1, next->velocities_m_per_s);
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
void add_origin(Kernel& k, double& high, double& low, double increment_value) {
    const double next = k.add(high, increment_value), recovered = k.sub(next, high);
    const double error = sum(k, {k.sub(high, k.sub(next, recovered)), k.sub(increment_value, recovered)});
    low = sum(k, {low, error}); const double folded = sum(k, {next, low});
    low = sum(k, {k.sub(next, folded), low}); high = folded;
}
void store_geometry(Kernel& k, const Data& old, Data& next, const Vec& z, CanonicalStepReport& report) {
    const auto n = z.size(); const auto anchor = old.left.kind == CanonicalBoundaryKind::fixed ? std::size_t{0} :
        old.right.kind == CanonicalBoundaryKind::fixed ? n - 1 : n / 2;
    k.scope(Stage::geometry); const double shift = z[anchor];
    k.scalar(Field::position, old.origin_high_m, 0); k.scalar(Field::position, old.origin_low_m, 1);
    k.scalar(Field::requested_displacement, shift);
    add_origin(k, next.origin_high_m, next.origin_low_m, shift);
    const double origin_change = sum(k, {next.origin_high_m, -old.origin_high_m, next.origin_low_m, -old.origin_low_m});
    k.represented(shift, origin_change, "Compensated canonical origin lost its requested increment", Field::requested_displacement, Field::represented_displacement);
    report.represented_displacement.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::geometry, static_cast<std::uint32_t>(j)); k.scalar(Field::position, old.positions_m[j]);
        k.scalar(Field::position, old.positions_m[j], 0); k.scalar(Field::requested_displacement, z[j], 0);
        const double relative = difference(k, z[j], shift);
        next.positions_m[j] = sum(k, {old.positions_m[j], relative});
        k.scalar(Field::position, next.positions_m[j], 1); k.scalar(Field::requested_displacement, relative, 1);
        const double relative_change = difference(k, next.positions_m[j], old.positions_m[j]);
        k.represented(relative, relative_change, "Canonical relative position lost its requested increment", Field::requested_displacement, Field::represented_displacement);
        const double stored = sum(k, {origin_change, next.positions_m[j], -old.positions_m[j]});
        report.represented_displacement[j] = stored;
        k.represented(z[j], stored, "Canonical physical position lost its requested increment", Field::requested_displacement, Field::represented_displacement);
        if (constrained(old, j)) k.require(stored == z[j], Code::numerical_precision, "Prescribed displacement is not exact in stored canonical geometry");
    }
    report.prescribed_displacements_exact = true;
    for (std::size_t j = 0; j + 1 < n; ++j) {
        k.scope(Stage::geometry, static_cast<std::uint32_t>(j));
        k.scalar(Field::position, old.positions_m[j], 0); k.scalar(Field::position, old.positions_m[j + 1], 1);
        k.scalar(Field::position, next.positions_m[j], 2); k.scalar(Field::position, next.positions_m[j + 1], 3);
        k.row(Field::requested_displacement, z);
        const double before = difference(k, old.positions_m[j + 1], old.positions_m[j]);
        const double after = difference(k, next.positions_m[j + 1], next.positions_m[j]);
        k.require(after > 0, Code::unsupported_topology, "Stored canonical fine nodes crossed");
        k.within(sum(k, {after, -before, -z[j + 1], z[j]}), after, 32 * epsilon, "Stored fine geometry differs from its requested increment");
    }
}
double absolute_sum(Kernel& k, const Vec& values, std::uint32_t role = 1) {
    Sum result(k, role); for (double value : values) result.add(std::abs(value)); return result.get();
}
bool same_values(const Vec& a, const Vec& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::bit_cast<std::uint64_t>(a[i]) != std::bit_cast<std::uint64_t>(b[i])) return false;
    return true;
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
    const auto incidence = volume_change(k, old, report.represented_displacement);
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::thermodynamics, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.row(Field::species_mass, old.cells[i].masses_kg); k.scalar(Field::stored_internal_energy, old.cells[i].internal_energy_j);
        k.scalar(Field::occupied_volume, old.cells[i].volume_m3); k.scalar(Field::requested_volume_change, accepted.volume_change[i]);
        k.scalar(Field::work_pressure, accepted.pressure[i]); k.charge(1);
        const auto response = old.thermodynamics->reference().isentropic_response(anchors[i], accepted.volume_change[i]);
        k.require(same_values(response.endpoint.masses_kg, old.cells[i].masses_kg), Code::numerical_precision, "Isentropic response changed retained constituent bits");
        next.cells[i].volume_m3 = owner_volume(k, next, i); next.cells[i].internal_energy_j = response.endpoint.internal_energy_j;
        const double dv = difference(k, next.cells[i].volume_m3, old.cells[i].volume_m3);
        const double du = difference(k, next.cells[i].internal_energy_j, old.cells[i].internal_energy_j);
        k.represented(accepted.volume_change[i], dv, "Stored owner volume lost its requested change", Field::requested_volume_change, Field::represented_volume_change);
        k.represented(response.internal_energy_change_j, du, "Stored owner U lost its balanced change", Field::balanced_energy_change, Field::represented_energy_change);
        k.within(difference(k, dv, accepted.volume_change[i]), next.cells[i].volume_m3, 32 * epsilon, "Stored owner volume differs from accepted trial geometry");
        next.thermodynamics_readings[i] = evaluate_owner(k, next, next.cells[i], i);
        const auto& before = old.thermodynamics_readings[i]; const auto& after = next.thermodynamics_readings[i];
        const double local_scale = sum(k, {before.thermal_energy_j, before.elastic_energy_j, after.thermal_energy_j, after.elastic_energy_j});
        k.within(difference(k, du, response.internal_energy_change_j), local_scale, 64 * epsilon,
            "Stored U increment exceeds its local thermal/elastic arithmetic bound");
        k.within(difference(k, dv, incidence[i]), next.cells[i].volume_m3, 32 * epsilon, "Owner stored volume differs from B times stored displacement");
        const double left_force = -k.mul(old.area_m2, accepted.pressure[i]), right_force = k.mul(old.area_m2, accepted.pressure[i]);
        const double left_work = k.mul(left_force, report.represented_displacement[old.owner_left[i]]);
        const double right_work = k.mul(right_force, report.represented_displacement[old.owner_right[i]]);
        const double incidence_work = k.mul(accepted.pressure[i], incidence[i]);
        k.within(sum(k, {left_work, right_work, -incidence_work}),
            sum(k, {std::abs(left_work), std::abs(right_work), std::abs(incidence_work)}), 256 * epsilon,
            "Cap work differs from owner incidence work");
        report.cap_impulse_left.push_back(k.mul(report.seconds, left_force)); report.cap_impulse_right.push_back(k.mul(report.seconds, right_force));
        report.cap_work_left.push_back(left_work); report.cap_work_right.push_back(right_work);
        report.incidence_volume_change.push_back(incidence[i]); report.represented_volume_change.push_back(dv);
        report.volume_storage_difference.push_back(difference(k, dv, incidence[i]));
        report.represented_energy_change.push_back(du); report.balanced_energy_change.push_back(response.internal_energy_change_j);
        report.eos_represented_volume_change.push_back(response.represented_volume_change_m3);
        report.eos_represented_energy_change.push_back(response.represented_internal_energy_change_j);
        report.endpoint_volume_resolves.push_back(response.endpoint_volume_resolves_change);
        report.endpoint_energy_resolves.push_back(response.endpoint_energy_resolves_change);
        const double store = difference(k, du, response.internal_energy_change_j);
        const double secant = sum(k, {response.internal_energy_change_j, k.mul(accepted.pressure[i], accepted.volume_change[i])});
        const double volume = k.mul(accepted.pressure[i], difference(k, incidence[i], accepted.volume_change[i]));
        const double defect = sum(k, {du, incidence_work});
        report.stored_energy_defect.push_back(store); report.secant_defect.push_back(secant);
        report.volume_work_defect.push_back(volume); report.owner_work_defect.push_back(defect);
        k.within(sum(k, {defect, -store, -secant, -volume}),
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
void finite_ledgers(Kernel& k, const Data& old, Data& next, const Vec& u0, const ProductRows& products, CanonicalStepReport& report) {
    const auto n = old.momentum.size(); const double h = report.seconds;
    k.scope(Stage::conservation);
    report.old_kinetic_energy_j = kinetic(k, old, report.linear_residual_ratio);
    report.new_kinetic_energy_j = next.resolved_kinetic_energy_j;
    k.require(std::bit_cast<std::uint64_t>(report.old_kinetic_energy_j) == std::bit_cast<std::uint64_t>(old.resolved_kinetic_energy_j),
        Code::numerical_precision, "Completed resolved-kinetic cache differs from its full consistent quadratic");
    const double k0 = report.old_kinetic_energy_j, k1 = report.new_kinetic_energy_j;
    Vec mean(n), pibar(n), relative_bar(n), relative_delta(n), delta_compatibility(n), mean_compatibility(n);
    report.momentum_residual.resize(n); report.kinematic_defect.resize(n);
    Sum kinetic_defect(k, 14); kinetic_defect.add(k1); kinetic_defect.add(-k0);
    Sum residual_work(k, 15), kinematic_work(k, 16), energy_terms(k, 17), impulse_terms(k, 18);
    Sum momentum_change(k, 19), momentum_residual(k, 20), first_prediction(k, 21), first_terms(k, 22);
    Sum relative_canonical(k, 23), relative_compatibility(k, 24), product_terms(k, 25);
    Sum pbar_total(k, 26), cbar_total(k, 27), delta_c_total(k, 28);
    const auto weights = multiply(k, old.consistent_mass, Vec(n, 1), report.linear_residual_ratio);
    for (std::size_t j = 0; j < n; ++j) {
        product_frame(k, Stage::conservation, old, next, u0, report, products, j, true);
        mean[j] = sum(k, {k.mul(.5, u0[j]), k.mul(.5, next.velocities_m_per_s[j])});
        pibar[j] = sum(k, {k.mul(.5, old.momentum[j]), k.mul(.5, next.momentum[j])});
        relative_bar[j] = sum(k, {k.mul(.5, old.positions_m[j]), k.mul(.5, next.positions_m[j])});
        relative_delta[j] = difference(k, next.positions_m[j], old.positions_m[j]);
        delta_compatibility[j] = difference(k, report.compatibility1[j], report.compatibility0[j]);
        mean_compatibility[j] = sum(k, {k.mul(.5, report.compatibility0[j]), k.mul(.5, report.compatibility1[j])});
        const double dpi = report.represented_impulse[j], force = report.actual_force[j];
        report.momentum_residual[j] = difference(k, dpi, k.mul(h, force));
        report.kinematic_defect[j] = difference(k, report.represented_displacement[j], k.mul(h, mean[j]));
        k.scalar(Field::momentum_residual, report.momentum_residual[j]); k.scalar(Field::kinematic_defect, report.kinematic_defect[j]);
        k.scalar(Field::assembled_force, force); k.scalar(Field::represented_impulse, dpi);
        k.scalar(Field::old_kinetic_energy, k0); k.scalar(Field::new_kinetic_energy, k1);
        const double kinetic_work = k.mul(mean[j], dpi);
        const double residual = k.mul(mean[j], report.momentum_residual[j]);
        const double geometry = -k.mul(report.kinematic_defect[j], force);
        kinetic_defect.add(-kinetic_work); residual_work.add(residual); kinematic_work.add(geometry);
        energy_terms.add(std::abs(kinetic_work)); energy_terms.add(std::abs(residual)); energy_terms.add(std::abs(geometry));
        momentum_change.add(dpi); momentum_residual.add(report.momentum_residual[j]);
        impulse_terms.add(std::abs(dpi));
        const auto parts = pressure_components(k, old, report.work_pressure, j);
        for (double part : parts) impulse_terms.add(std::abs(k.mul(h, part)));
        impulse_terms.add(std::abs(k.mul(h, report.exterior_force[j]))); impulse_terms.add(std::abs(k.mul(h, report.reaction[j])));
        const double first_defect = k.mul(weights[j], report.kinematic_defect[j]);
        const double first_velocity = k.mul(h, k.mul(weights[j], mean[j])), first_pi = k.mul(h, pibar[j]);
        first_prediction.add(first_defect); first_prediction.add(first_velocity); first_prediction.add(-first_pi);
        first_terms.add(std::abs(first_defect)); first_terms.add(std::abs(first_velocity)); first_terms.add(std::abs(first_pi));
        const double canonical0 = k.mul(relative_bar[j], dpi), canonical1 = k.mul(relative_delta[j], pibar[j]);
        const double compatible0 = k.mul(relative_bar[j], delta_compatibility[j]);
        const double compatible1 = k.mul(relative_delta[j], mean_compatibility[j]);
        relative_canonical.add(canonical0); relative_canonical.add(canonical1);
        relative_compatibility.add(compatible0); relative_compatibility.add(compatible1);
        for (double term : {canonical0, canonical1, compatible0, compatible1}) product_terms.add(std::abs(term));
        pbar_total.add(pibar[j]); cbar_total.add(mean_compatibility[j]); delta_c_total.add(delta_compatibility[j]);
    }
    Sum first_change(k, 29), relative_product_change(k, 30);
    for (std::size_t e = 0; e < old.reference_pieces.size(); ++e) {
        const auto& piece = old.reference_pieces[e]; const auto a = piece.left, b = piece.right; const auto& c = old.pieces[e].consistent_mass;
        k.scope(Stage::conservation, canonical_absent_index, static_cast<std::uint32_t>(piece.owner));
        k.row(Field::matrix_coefficients, c); k.row(Field::represented_displacement, report.represented_displacement);
        k.row(Field::velocity0, u0); k.row(Field::velocity1, next.velocities_m_per_s);
        const double w0 = sum(k, {c[0], c[1]}), w1 = sum(k, {c[1], c[2]});
        const double first0 = k.mul(w0, report.represented_displacement[a]), first1 = k.mul(w1, report.represented_displacement[b]);
        first_change.add(first0); first_change.add(first1); first_terms.add(std::abs(first0)); first_terms.add(std::abs(first1));
        const double dua = report.delta_velocity[a], dub = report.delta_velocity[b];
        const std::array<double, 8> terms{
            k.mul(k.mul(relative_bar[a], c[0]), dua), k.mul(k.mul(relative_bar[a], c[1]), dub),
            k.mul(k.mul(relative_bar[b], c[1]), dua), k.mul(k.mul(relative_bar[b], c[2]), dub),
            k.mul(k.mul(relative_delta[a], c[0]), mean[a]), k.mul(k.mul(relative_delta[a], c[1]), mean[b]),
            k.mul(k.mul(relative_delta[b], c[1]), mean[a]), k.mul(k.mul(relative_delta[b], c[2]), mean[b])};
        k.row(Field::matrix_products, terms);
        for (double term : terms) { relative_product_change.add(term); product_terms.add(std::abs(term)); }
    }
    k.scope(Stage::conservation);
    report.exterior_work_left_j = k.mul(report.exterior_force.front(), report.represented_displacement.front());
    report.exterior_work_right_j = k.mul(report.exterior_force.back(), report.represented_displacement.back());
    report.reaction_work_left_j = k.mul(report.reaction.front(), report.requested_displacement.front());
    report.reaction_work_right_j = k.mul(report.reaction.back(), report.requested_displacement.back());
    report.exterior_impulse_left = k.mul(h, report.exterior_force.front()); report.exterior_impulse_right = k.mul(h, report.exterior_force.back());
    report.reaction_impulse_left = k.mul(h, report.reaction.front()); report.reaction_impulse_right = k.mul(h, report.reaction.back());
    report.kinetic_defect_j = kinetic_defect.get(); report.momentum_residual_work_j = residual_work.get();
    report.kinematic_defect_work_j = kinematic_work.get(); report.trial_residual_work_j = dot(k, report.requested_displacement, report.force_residual);
    Sum owner_defects(k, 32), physical_scale(k, 33), lhs(k, 34), accounted(k, 35);
    lhs.add(k1); lhs.add(-k0); physical_scale.add(k0); physical_scale.add(k1);
    energy_terms.add(std::abs(k0)); energy_terms.add(std::abs(k1)); energy_terms.add(std::abs(report.kinetic_defect_j));
    for (std::size_t i = 0; i < 7; ++i) {
        lhs.add(report.represented_energy_change[i]); owner_defects.add(report.owner_work_defect[i]);
        const auto& a = old.thermodynamics_readings[i]; const auto& b = next.thermodynamics_readings[i];
        for (double value : {a.thermal_energy_j, a.elastic_energy_j, b.thermal_energy_j, b.elastic_energy_j}) physical_scale.add(value);
        for (double value : {report.represented_energy_change[i], k.mul(report.work_pressure[i], report.incidence_volume_change[i]),
            report.owner_work_defect[i], report.cap_work_left[i], report.cap_work_right[i]}) energy_terms.add(std::abs(value));
    }
    for (double value : {report.exterior_work_left_j, report.exterior_work_right_j, report.reaction_work_left_j, report.reaction_work_right_j}) {
        lhs.add(-value); energy_terms.add(std::abs(value));
    }
    report.energy_error_j = lhs.get(); report.energy_budget_j = k.mul(2e-11, physical_scale.get());
    accounted.add(report.momentum_residual_work_j); accounted.add(report.kinematic_defect_work_j);
    accounted.add(report.kinetic_defect_j); accounted.add(owner_defects.get());
    report.accounting_remainder_j = difference(k, report.energy_error_j, accounted.get());
    report.accounting_bound_j = k.mul(256 * epsilon, energy_terms.get());
    k.scalar(Field::energy_error, report.energy_error_j); k.scalar(Field::arithmetic_bound, report.energy_budget_j);
    k.require(std::abs(report.energy_error_j) <= report.energy_budget_j, Code::numerical_precision, "Actual canonical physical energy gate failed");
    k.scalar(Field::energy_error, report.accounting_remainder_j); k.scalar(Field::arithmetic_bound, report.accounting_bound_j);
    k.require(std::abs(report.accounting_remainder_j) <= report.accounting_bound_j, Code::numerical_precision, "Complete finite canonical energy identity failed");
    report.momentum_error = sum(k, {momentum_change.get(), -report.exterior_impulse_left, -report.exterior_impulse_right,
        -report.reaction_impulse_left, -report.reaction_impulse_right});
    report.momentum_accounting_remainder = difference(k, report.momentum_error, momentum_residual.get());
    report.momentum_accounting_bound = k.mul(256 * epsilon, impulse_terms.get());
    k.within(report.momentum_accounting_remainder, impulse_terms.get(), 256 * epsilon, "Canonical finite momentum identity failed");
    report.first_moment_change = first_change.get(); report.first_moment_error = difference(k, report.first_moment_change, k.mul(h, pbar_total.get()));
    report.first_moment_accounting_remainder = difference(k, report.first_moment_error, first_prediction.get());
    report.first_moment_bound = k.mul(256 * epsilon, first_terms.get());
    k.within(report.first_moment_accounting_remainder, first_terms.get(), 256 * epsilon, "Canonical stable first-moment identity failed");
    report.relative_position_velocity_change = relative_product_change.get();
    report.position_velocity_compatibility_change = relative_compatibility.get();
    report.position_velocity_accounting_remainder = sum(k, {report.relative_position_velocity_change,
        -relative_canonical.get(), -report.position_velocity_compatibility_change});
    report.position_velocity_bound = k.mul(256 * epsilon, product_terms.get());
    k.within(report.position_velocity_accounting_remainder, product_terms.get(), 256 * epsilon, "Canonical relative bilinear identity failed");
    const double origin_mean = sum(k, {k.mul(.5, old.origin_high_m), k.mul(.5, old.origin_low_m),
        k.mul(.5, next.origin_high_m), k.mul(.5, next.origin_low_m)});
    const double origin_change = sum(k, {next.origin_high_m, -old.origin_high_m, next.origin_low_m, -old.origin_low_m});
    report.origin_position_velocity_change = sum(k, {k.mul(origin_mean, momentum_change.get()), k.mul(origin_change, pbar_total.get())});
    report.origin_compatibility_change = sum(k, {k.mul(origin_mean, delta_c_total.get()), k.mul(origin_change, cbar_total.get())});
    report.position_velocity_change = sum(k, {report.relative_position_velocity_change, report.origin_position_velocity_change, report.origin_compatibility_change});
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
    for (std::size_t j = 0; j < n; ++j) report.compatibility0[j] = difference(k, report.decoded_product0[j], old.momentum[j]);
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
    report.pressure_force = pressure_force(k, old, current.pressure); report.exterior_force = exterior_force(k, old);
    report.requested_impulse.resize(n); report.represented_impulse.resize(n); report.free_addition_defect.resize(n);
    report.constrained_linearity_defect.resize(n); report.constrained_join_defect.resize(n); report.constrained_impulse_defect.resize(n);
    report.constrained_linearity_bound.resize(n); report.constrained_join_bound.resize(n); report.constrained_impulse_bound.resize(n);
    k.baseline(Field::pi1, {});
    for (const auto j : free) {
        node_frame(k, Stage::free_impulse, old, nullptr, u0, {}, current.pressure, j);
        Sum force(k); const auto parts = pressure_components(k, old, current.pressure, j);
        for (double value : parts) force.add(value);
        force.add(report.exterior_force[j]);
        const double required = k.mul(h, force.get()); report.requested_impulse[j] = required;
        next.momentum[j] = sum(k, {old.momentum[j], required}); k.baseline_value(Field::pi1, j, next.momentum[j]);
        const double actual = difference(k, next.momentum[j], old.momentum[j]); report.represented_impulse[j] = actual;
        const double scale = sum(k, {std::abs(old.momentum[j]), std::abs(next.momentum[j]), std::abs(required)});
        increment(k, required, actual, scale, "Free force-primary canonical impulse was erased or reversed");
        report.free_addition_defect[j] = difference(k, actual, required);
    }
    next.velocities_m_per_s = decode_velocity(k, next, report.linear_residual_ratio, false);
    report.decoded_product1 = multiply(k, old.consistent_mass, next.velocities_m_per_s, report.linear_residual_ratio);
    for (std::size_t j = 0; j < n; ++j) if (constrained(old, j)) {
        next.momentum[j] = report.decoded_product1[j]; k.baseline_value(Field::pi1, j, next.momentum[j]);
    }
    report.delta_velocity.resize(n); report.compatibility1.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::constrained_impulse, static_cast<std::uint32_t>(j));
        report.delta_velocity[j] = difference(k, next.velocities_m_per_s[j], u0[j]);
        report.compatibility1[j] = difference(k, report.decoded_product1[j], next.momentum[j]);
    }
    report.mass_delta_velocity = multiply(k, old.consistent_mass, report.delta_velocity, report.linear_residual_ratio);
    ProductRows products{Matrix(n), Matrix(n), Matrix(n)};
    for (std::size_t j = 0; j < n; ++j) {
        products.zero[j] = physical_row_products(k, old.consistent_mass, u0, j);
        products.one[j] = physical_row_products(k, old.consistent_mass, next.velocities_m_per_s, j);
        products.delta[j] = physical_row_products(k, old.consistent_mass, report.delta_velocity, j);
    }
    for (std::size_t j = 0; j < n; ++j) {
        product_frame(k, Stage::constrained_impulse, old, next, u0, report, products, j);
        const double a0 = absolute_sum(k, products.zero[j], 1), a1 = absolute_sum(k, products.one[j], 2), ad = absolute_sum(k, products.delta[j], 3);
        k.within(report.compatibility0[j], sum(k, {a0, std::abs(old.momentum[j])}), 256 * epsilon, "Start canonical compatibility failed");
        k.within(report.compatibility1[j], sum(k, {a1, std::abs(next.momentum[j])}), 256 * epsilon, "Endpoint canonical compatibility failed");
        if (!constrained(old, j)) continue;
        const double required = sum(k, {report.mass_delta_velocity[j], report.compatibility0[j]});
        const double actual = difference(k, next.momentum[j], old.momentum[j]);
        report.requested_impulse[j] = required; report.represented_impulse[j] = actual;
        k.scalar(Field::mass_delta_velocity, report.mass_delta_velocity[j]);
        const double linearity = sum(k, {report.mass_delta_velocity[j], report.decoded_product0[j], -report.decoded_product1[j]});
        const double linearity_scale = sum(k, {ad, a0, a1});
        const double join = sum(k, {required, -actual, -linearity});
        const double join_scale = sum(k, {std::abs(report.mass_delta_velocity[j]), std::abs(report.decoded_product0[j]),
            std::abs(report.decoded_product1[j]), std::abs(report.compatibility0[j]), std::abs(old.momentum[j]),
            std::abs(next.momentum[j]), std::abs(required), std::abs(actual)});
        const double defect = difference(k, required, actual), total_scale = sum(k, {linearity_scale, join_scale});
        k.represented(required, actual, "Constrained canonical impulse was erased or reversed");
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
            reaction.add(-report.exterior_force[j]); report.reaction[j] = reaction.get();
        }
        Sum total(k, 1); for (double part : parts) total.add(part);
        total.add(report.exterior_force[j]); total.add(report.reaction[j]); report.actual_force[j] = total.get();
        k.scalar(Field::reaction_force, report.reaction[j]); k.scalar(Field::assembled_force, report.actual_force[j]);
        const double assembled = k.mul(h, report.actual_force[j]); k.scalar(Field::assembled_impulse, assembled);
        k.represented(report.requested_impulse[j], assembled, "Actual force assembly erased or reversed its required impulse");
        k.represented(assembled, report.represented_impulse[j], "Stored impulse erased or reversed actual assembled force");
        k.represented(report.represented_impulse[j], assembled, "Actual force assembly erased stored canonical impulse");
        Sum defect(k, 2), scale(k, 3); defect.add(k.mul(h, report.reaction[j])); scale.add(std::abs(k.mul(h, report.reaction[j])));
        for (double part : parts) { const double value = k.mul(h, part); defect.add(value); scale.add(std::abs(value)); }
        const double external = k.mul(h, report.exterior_force[j]); defect.add(external); scale.add(std::abs(external));
        defect.add(-report.requested_impulse[j]); scale.add(std::abs(report.requested_impulse[j]));
        k.within(defect.get(), scale.get(), 256 * epsilon, "Actual reaction/force assembly does not reproduce required canonical impulse");
    }
    store_geometry(k, old, next, z, report); update_owners(k, old, next, anchors, current, report);
    update_readings(k, next, report.maximum_quadrature_ratio);
    finite_ledgers(k, old, next, u0, products, report);
    k.scope(Stage::publication); k.scalar(Field::completed_time, old.completed_time_seconds); k.scalar(Field::timestep, h);
    next.completed_time_seconds = sum(k, {old.completed_time_seconds, h});
    report.represented_seconds = difference(k, next.completed_time_seconds, old.completed_time_seconds);
    k.represented(h, report.represented_seconds, "Completed canonical time lost its requested increment", Field::timestep, Field::completed_time);
    k.require(old.completed_steps < std::numeric_limits<std::uint64_t>::max(), Code::unsupported_configuration, "Canonical completed-step index exhausted");
    next.completed_steps = old.completed_steps + 1;
    next.cumulative_energy_error_j = sum(k, {old.cumulative_energy_error_j, report.energy_error_j});
    report.newton_updates = k.newton_updates; report.backtracks = k.backtracks; report.factorizations = k.factorizations;
    report.constitutive_charges = k.charged_here(); next.last_step = std::move(report);
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
