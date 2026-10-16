#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {
RetainedScalar retained_value(Kernel& k, double value) {
    CanonicalFailureWitness child;
    try {
        // Preserve a supplied -0 high component as well as every nonzero bit.
        const auto result = make_retained_scalar(value, std::copysign(0.0, value), child);
        k.require(std::bit_cast<std::uint64_t>(result.high()) == std::bit_cast<std::uint64_t>(value) && result.low() == 0,
            Code::numerical_precision, "Scalar import changed its supplied primary bits");
        return result;
    } catch (const CanonicalFailure&) {
        if (child.failed()) k.relay_arithmetic_failure(child);
        throw;
    }
}
RetainedScalarUpdate retained_increment(Kernel& k, const RetainedScalar& value, double increment) {
    CanonicalFailureWitness child;
    try { return add_retained_scalar(value, increment, child); }
    catch (const CanonicalFailure&) { k.relay_arithmetic_failure(child); }
}
RetainedScalarDifference retained_difference(Kernel& k, const RetainedScalar& left, const RetainedScalar& right) {
    CanonicalFailureWitness child;
    try { return subtract_retained_scalar(left, right, child); }
    catch (const CanonicalFailure&) { k.relay_arithmetic_failure(child); }
}
RetainedScalarProduct retained_product(Kernel& k, const RetainedScalar& value, double coefficient) {
    CanonicalFailureWitness child;
    try { return multiply_retained_scalar(value, coefficient, child); }
    catch (const CanonicalFailure&) { k.relay_arithmetic_failure(child); }
}
CanonicalGeometryMeasure measure_geometry(Kernel& k, const RetainedScalar& left, const RetainedScalar& right, double area) {
    k.positive(area, Field::occupied_volume);
    CanonicalGeometryMeasure result;
    result.length_m = retained_difference(k, right, left);
    k.positive(result.length_m.value.high(), Field::position);
    Sum volume(k, 35);
    for (std::size_t i = 0; i < result.length_m.expansion_count; ++i) {
        const auto product = retained_product(k, retained_value(k, result.length_m.expansion_terms[i]), area);
        k.require(product.product_terms[2] == 0 && product.product_terms[3] == 0,
            Code::numerical_precision, "Scalar geometry factor acquired an unexpected low operand");
        for (double value : product.roundoff_remainders)
            k.require(value == 0, Code::numerical_precision, "A scalar geometry product exceeds two exact components");
        for (const auto part : {product.product_terms[1], product.product_terms[0]}) if (part != 0) {
            k.require(result.volume_term_count < result.volume_terms_m3.size(), Code::unsupported_configuration,
                "Geometry product certificate capacity exceeded");
            result.volume_terms_m3[result.volume_term_count++] = part;
            volume.add(part);
        }
    }
    result.projected_volume_m3 = k.positive(volume.get(), Field::occupied_volume);
    std::copy(result.volume_terms_m3.begin(), result.volume_terms_m3.end(), result.projection_residual_terms_m3.begin());
    result.projection_residual_terms_m3.back() = -result.projected_volume_m3;
    return result;
}
double physical_position(Kernel& k, const Data& d, std::size_t node) {
    const auto& relative = d.primary.relative_positions_m[node];
    return sum(k, {d.primary.origin_m.high(), d.primary.origin_m.low(), relative.high(), relative.low()});
}
void initialize_retained_momentum(Kernel& k, Data& d, const Vec& requested_velocity) {
    const auto n = requested_velocity.size();
    d.primary.momentum.assign(n, RetainedScalar{}); d.momentum.resize(n);
    d.initialization.requested_velocity_m_per_s = requested_velocity;
    d.initialization.momentum_product_terms.resize(n * n);
    d.initialization.momentum_sum_remainders.resize(n * n);
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::canonical_decode, static_cast<std::uint32_t>(j));
        k.row(Field::matrix_coefficients, d.consistent_mass[j]); k.row(Field::velocity0, requested_velocity);
        RetainedScalar total;
        for (std::size_t p = 0; p < n; ++p) {
            const std::array<double, 2> indices{static_cast<double>(j), static_cast<double>(p)};
            k.row(Field::node_indices, indices);
            const auto product = retained_product(k, retained_value(k, requested_velocity[p]), d.consistent_mass[j][p]);
            for (double value : product.roundoff_remainders)
                k.require(value == 0, Code::numerical_precision, "Scalar initial momentum product exceeds two exact components");
            d.initialization.momentum_product_terms[j * n + p] = {product.product_terms[0], product.product_terms[1]};
            const auto negative = retained_product(k, product.value, -1);
            for (double value : negative.roundoff_remainders)
                k.require(value == 0, Code::numerical_precision, "Exact momentum negation acquired a remainder");
            const auto addition = retained_difference(k, total, negative.value);
            d.initialization.momentum_sum_remainders[j * n + p] = addition.roundoff_remainders;
            total = addition.value;
        }
        d.primary.momentum[j] = total; d.momentum[j] = total.high();
    }
}
void validate_scalar_projections(Kernel& k, const Data& d) {
    const auto n = d.positions_m.size();
    k.require(d.primary.relative_positions_m.size() == n && d.primary.momentum.size() == n,
        Code::unsupported_configuration, "Retained primary shape differs from its projections");
    const auto same = [](double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); };
    for (std::size_t j = 0; j < n; ++j) {
        k.scope(Stage::publication, static_cast<std::uint32_t>(j));
        k.scalar(Field::position, d.positions_m[j]); k.scalar(Field::retained_position_low, d.primary.relative_positions_m[j].low());
        k.scalar(Field::matrix_operands, d.momentum[j], 0); k.scalar(Field::matrix_operands, d.primary.momentum[j].high(), 1);
        k.scalar(Field::retained_momentum_low, d.primary.momentum[j].low());
        k.require(same(d.positions_m[j], d.primary.relative_positions_m[j].high()) && same(d.momentum[j], d.primary.momentum[j].high()),
            Code::numerical_precision, "Scalar node projection differs from its primary value");
    }
    for (std::size_t i = 0; i < 7; ++i) {
        k.scope(Stage::publication, canonical_absent_index, static_cast<std::uint32_t>(i));
        k.scalar(Field::stored_internal_energy, d.cells[i].internal_energy_j, 0);
        k.scalar(Field::stored_internal_energy, d.primary.internal_energy_j[i].high(), 1);
        k.scalar(Field::retained_internal_energy_low, d.primary.internal_energy_j[i].low());
        k.scalar(Field::occupied_volume, d.cells[i].volume_m3, 0);
        k.scalar(Field::occupied_volume, d.owner_geometry[i].projected_volume_m3, 1);
        k.require(same(d.cells[i].internal_energy_j, d.primary.internal_energy_j[i].high()) &&
            same(d.cells[i].volume_m3, d.owner_geometry[i].projected_volume_m3),
            Code::numerical_precision, "Scalar thermodynamic input differs from its retained source");
    }
    k.scope(Stage::publication); k.scalar(Field::completed_time, d.completed_time_seconds);
    k.scalar(Field::retained_time_low, d.primary.completed_time_seconds.low());
    k.require(same(d.origin_high_m, d.primary.origin_m.high()) && same(d.origin_low_m, d.primary.origin_m.low()) &&
        same(d.completed_time_seconds, d.primary.completed_time_seconds.high()),
        Code::numerical_precision, "Scalar origin/time projection differs from its primary value");
}
}

namespace ma::experimental {
bool canonical_material_advance_ready() noexcept { return false; }
}
