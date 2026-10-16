#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {
namespace {
std::uint32_t node(std::size_t index, std::span<const std::size_t> indices) {
    return static_cast<std::uint32_t>(indices.empty() ? index : indices[index]);
}
Vec index_values(std::size_t size, std::span<const std::size_t> indices) {
    Vec result(size);
    for (std::size_t i = 0; i < size; ++i) result[i] = static_cast<double>(indices.empty() ? i : indices[i]);
    return result;
}
void shape(Kernel& k, const Matrix& a, std::size_t n, std::span<const std::size_t> indices) {
    k.require(n != 0 && n <= 33 && a.size() == n && (indices.empty() || indices.size() == n),
        Code::unsupported_configuration, "Canonical square matrix shape differs");
    for (std::size_t i = 0; i < n; ++i) {
        k.require(a[i].size() == n, Code::unsupported_configuration, "Canonical matrix row shape differs");
        if (!indices.empty()) k.require(indices[i] < 33 && (i == 0 || indices[i] > indices[i - 1]),
            Code::unsupported_configuration, "Canonical matrix node map differs");
    }
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j <= i; ++j) {
        k.row_scope(node(i, indices)); k.row(Field::matrix_coefficients, a[i]);
        k.scalar(Field::matrix_coefficients, a[i][j], 0); k.scalar(Field::matrix_coefficients, a[j][i], 1);
        k.finite(a[i][j]); k.finite(a[j][i]);
        k.require(a[i][j] == a[j][i], Code::numerical_precision, "Canonical matrix is not symmetric");
    }
}
std::array<double, 2> scalar_product(Kernel& k, double a, double b) {
    const auto result = retained_product(k, retained_value(k, a), b);
    k.require(result.product_terms[2] == 0 && result.product_terms[3] == 0,
        Code::numerical_precision, "Scalar linear product acquired a low input");
    for (double remainder : result.roundoff_remainders)
        k.require(remainder == 0, Code::numerical_precision, "Scalar linear product exceeds its exact pair");
    return {result.product_terms[0], result.product_terms[1]};
}
struct RowProduct { double value = 0, magnitude = 0; Vec products, errors; };
RowProduct direct_row(Kernel& k, const Matrix& a, const Vec& x, std::size_t i, const Vec& indices) {
    k.row_scope(static_cast<std::uint32_t>(indices[i]));
    k.row(Field::matrix_coefficients, a[i]); k.row(Field::matrix_operands, x); k.row(Field::node_indices, indices);
    RowProduct result; result.products.resize(x.size()); result.errors.resize(x.size()); Sum value(k, 0), magnitude(k, 1);
    for (std::size_t j = 0; j < x.size(); ++j) {
        k.row(Field::matrix_products, std::span<const double>(result.products.data(), j));
        const auto pair = scalar_product(k, a[i][j], x[j]);
        result.products[j] = pair[0]; result.errors[j] = pair[1];
        value.add(pair[0]); value.add(pair[1]); magnitude.add(std::abs(pair[0])); magnitude.add(std::abs(pair[1]));
    }
    k.row(Field::matrix_products, result.products); k.row(Field::matrix_products, result.errors, 1);
    result.value = value.get(); result.magnitude = magnitude.get();
    return result;
}
struct ScaledFactor { Vec scales; Matrix lower; };
ScaledFactor factor_spd(Kernel& k, const Matrix& a, const Vec& rhs, const Vec& indices) {
    const auto n = rhs.size(); ScaledFactor factor{Vec(n), zeros(n)};
    auto& scales = factor.scales; auto& lower = factor.lower;
    for (std::size_t i = 0; i < n; ++i) {
        k.row_scope(static_cast<std::uint32_t>(indices[i]));
        k.row(Field::matrix_coefficients, a[i]); k.row(Field::node_indices, indices);
        scales[i] = k.sqrt(a[i][i]);
    }
    k.factorization();
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j <= i; ++j) {
        k.row_scope(static_cast<std::uint32_t>(indices[i]));
        k.row(Field::matrix_coefficients, a[i]);
        k.row(Field::node_indices, indices); k.row(Field::matrix_operands, rhs, 0); k.row(Field::matrix_operands, scales, 1);
        k.row(Field::matrix_operands, std::span<const double>(lower[i].data(), j), 2);
        k.row(Field::matrix_operands, std::span<const double>(lower[j].data(), j), 3);
        Sum sum_value(k); Vec products(j), errors(j);
        sum_value.add(k.div(k.div(a[i][j], scales[i]), scales[j]));
        for (std::size_t p = 0; p < j; ++p) {
            k.row(Field::matrix_products, std::span<const double>(products.data(), p));
            const auto pair = scalar_product(k, lower[i][p], lower[j][p]); products[p] = pair[0]; errors[p] = pair[1];
            sum_value.add(-pair[0]); sum_value.add(-pair[1]);
        }
        k.row(Field::matrix_products, products); k.row(Field::matrix_products, errors, 1);
        const double value = sum_value.get();
        lower[i][j] = i == j ? k.sqrt(value) : k.div(value, lower[j][j]);
    }
    return factor;
}
Vec triangular_solution(Kernel& k, const Matrix& a, const Vec& rhs, const Vec& indices, const ScaledFactor& factor) {
    const auto n = rhs.size(); Vec y(n), x(n); const auto& scales = factor.scales; const auto& lower = factor.lower;
    for (std::size_t i = 0; i < n; ++i) {
        k.row_scope(static_cast<std::uint32_t>(indices[i]));
        k.row(Field::matrix_coefficients, a[i], 0); k.row(Field::matrix_coefficients, lower[i], 1);
        k.row(Field::node_indices, indices); k.row(Field::matrix_operands, rhs, 0); k.row(Field::matrix_operands, scales, 1);
        k.row(Field::matrix_operands, std::span<const double>(y.data(), i), 2);
        Sum value(k); Vec products(i), errors(i);
        value.add(k.div(rhs[i], scales[i]));
        for (std::size_t j = 0; j < i; ++j) {
            k.row(Field::matrix_products, std::span<const double>(products.data(), j));
            const auto pair = scalar_product(k, lower[i][j], y[j]); products[j] = pair[0]; errors[j] = pair[1];
            value.add(-pair[0]); value.add(-pair[1]);
        }
        k.row(Field::matrix_products, products); k.row(Field::matrix_products, errors, 1); y[i] = k.div(value.get(), lower[i][i]);
    }
    for (std::size_t i = n; i-- != 0;) {
        Vec column(n), products(n - i - 1), errors(n - i - 1);
        for (std::size_t j = i; j < n; ++j) column[j] = lower[j][i];
        k.row_scope(static_cast<std::uint32_t>(indices[i]));
        k.row(Field::matrix_coefficients, a[i], 0); k.row(Field::matrix_coefficients, column, 1);
        k.row(Field::node_indices, indices); k.row(Field::matrix_operands, rhs, 0); k.row(Field::matrix_operands, scales, 1);
        k.row(Field::matrix_operands, std::span<const double>(x.data() + i + 1, n - i - 1), 2);
        k.scalar(Field::matrix_operands, y[i]); Sum value(k); value.add(y[i]);
        for (std::size_t j = i + 1; j < n; ++j) {
            k.row(Field::matrix_products, std::span<const double>(products.data(), j - i - 1));
            const auto pair = scalar_product(k, lower[j][i], x[j]); products[j - i - 1] = pair[0]; errors[j - i - 1] = pair[1];
            value.add(-pair[0]); value.add(-pair[1]);
        }
        k.row(Field::matrix_products, products); k.row(Field::matrix_products, errors, 1); x[i] = k.div(value.get(), lower[i][i]);
    }
    for (std::size_t i = 0; i < n; ++i) x[i] = k.div(x[i], scales[i]);
    return x;
}
double complete_residual(Kernel& k, const RowProduct& product, const Terms& rhs) {
    k.row(Field::matrix_operands, rhs, 1); Sum value(k, 2);
    for (double term : rhs) value.add(term);
    for (std::size_t j = 0; j < product.products.size(); ++j) { value.add(-product.products[j]); value.add(-product.errors[j]); }
    return value.get();
}
Vec solve_complete(Kernel& k, const Matrix& a, const Vec& rhs, const std::vector<Terms>& full_rhs,
    const Vec& indices, double& maximum_ratio) {
    k.require(full_rhs.size() == rhs.size(), Code::unsupported_configuration, "Complete linear RHS shape differs");
    const auto factor = factor_spd(k, a, rhs, indices);
    auto x = triangular_solution(k, a, rhs, indices, factor);
    // Two fixed residual-correction passes use the same factorization. The
    // residual includes complete primary momentum and exact row products,
    // rather than refining a RHS that has already lost its low component.
    // No failed predicate selects a fallback, precision, norm or iteration cap.
    for (std::size_t pass = 0; pass < 2; ++pass) {
        Vec residual(rhs.size());
        for (std::size_t i = 0; i < rhs.size(); ++i)
            residual[i] = complete_residual(k, direct_row(k, a, x, i, indices), full_rhs[i]);
        const auto correction = triangular_solution(k, a, residual, indices, factor);
        for (std::size_t i = 0; i < rhs.size(); ++i) {
            k.row_scope(static_cast<std::uint32_t>(indices[i]));
            k.scalar(Field::matrix_operands, x[i], 0); k.scalar(Field::matrix_operands, correction[i], 1);
            x[i] = k.add(x[i], correction[i]);
        }
    }
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        const auto product = direct_row(k, a, x, i, indices);
        const double error = complete_residual(k, product, full_rhs[i]);
        const double magnitude = sum(k, {std::abs(rhs[i]), product.magnitude}); k.scalar(Field::linear_residual, error);
        k.within(error, magnitude, 256 * epsilon, "SPD solution fails the complete original physical row");
        maximum_ratio = std::max(maximum_ratio, k.ratio(error, magnitude));
    }
    return x;
}
}

Vec physical_row_products(Kernel& k, const Matrix& a, const Vec& x, std::size_t i) {
    return direct_row(k, a, x, i, index_values(x.size(), {})).products;
}
Vec multiply(Kernel& k, const Matrix& a, const Vec& x, double& maximum_ratio, std::span<const std::size_t> indices) {
    shape(k, a, x.size(), indices);
    const auto physical_indices = index_values(x.size(), indices); Vec result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto direct = direct_row(k, a, x, i, physical_indices);
        result[i] = direct.value; Sum remainder(k, 2);
        for (std::size_t j = 0; j < x.size(); ++j) { remainder.add(direct.products[j]); remainder.add(direct.errors[j]); }
        remainder.add(-result[i]); const double error = remainder.get();
        k.scalar(Field::linear_residual, error);
        k.within(error, direct.magnitude, 256 * epsilon, "Physical row projection exceeds its local bound");
        maximum_ratio = std::max(maximum_ratio, k.ratio(error, direct.magnitude));
    }
    return result;
}
Vec solve_spd(Kernel& k, const Matrix& a, const Vec& rhs, double& maximum_ratio, std::span<const std::size_t> indices) {
    shape(k, a, rhs.size(), indices);
    // Work in physical node coordinates. A global even/odd congruence mixes
    // distant liquid and gas rows, then cancellation can erase local forces.
    // This is the only solve path, with the same original-row error criterion.
    const auto physical_indices = index_values(rhs.size(), indices);
    std::vector<Terms> full_rhs; full_rhs.reserve(rhs.size());
    for (double value : rhs) full_rhs.push_back(terms({value}));
    return solve_complete(k, a, rhs, full_rhs, physical_indices, maximum_ratio);
}
Vec decode_velocity(Kernel& k, const Data& d, double& maximum_ratio, bool check_dependent_pi) {
    k.scope(Stage::canonical_decode);
    k.baseline(Field::velocity1, {});
    const auto n = d.momentum.size(); Vec velocity(n); std::vector<std::size_t> free;
    for (std::size_t j = 0; j < n; ++j) {
        if (constrained(d, j)) {
            velocity[j] = boundary_velocity(d, j); k.baseline_value(Field::velocity1, j, velocity[j]);
        } else free.push_back(j);
    }
    Matrix a = zeros(free.size()); Vec rhs(free.size()); std::vector<Terms> full_rhs(free.size());
    Vec prescribed, prescribed_nodes;
    for (std::size_t j = 0; j < n; ++j) if (constrained(d, j)) {
        prescribed.push_back(velocity[j]); prescribed_nodes.push_back(static_cast<double>(j));
    }
    for (std::size_t i = 0; i < free.size(); ++i) {
        const auto j = free[i]; k.row_scope(static_cast<std::uint32_t>(j));
        k.row(Field::matrix_coefficients, d.consistent_mass[j]);
        k.row(Field::velocity1, prescribed); k.row(Field::node_indices, prescribed_nodes);
        k.scalar(Field::pi1, d.primary.momentum[j].high());
        k.scalar(Field::retained_momentum_low, d.primary.momentum[j].low(), static_cast<std::uint32_t>(j));
        full_rhs[i] = terms(d.primary.momentum[j]);
        for (std::size_t p = 0; p < n; ++p) if (constrained(d, p)) {
            const auto known = retained_product(k, retained_value(k, velocity[p]), d.consistent_mass[j][p]);
            for (double term : known.product_terms) if (term != 0) full_rhs[i].push_back(-term);
        }
        rhs[i] = projected(k, full_rhs[i]);
        for (std::size_t p = 0; p < free.size(); ++p) a[i][p] = d.consistent_mass[j][free[p]];
    }
    shape(k, a, rhs.size(), free);
    const auto decoded = solve_complete(k, a, rhs, full_rhs, index_values(free.size(), free), maximum_ratio);
    for (std::size_t i = 0; i < free.size(); ++i) velocity[free[i]] = decoded[i];
    k.baseline(Field::velocity1, velocity);
    const auto product = multiply(k, d.consistent_mass, velocity, maximum_ratio);
    for (std::size_t j = 0; j < n; ++j) {
        // During an advance, prescribed-node pi is dependent on this solve.
        // Its old value is not an equation for a prescribed endpoint velocity.
        if (constrained(d, j) && !check_dependent_pi) continue;
        const auto terms = physical_row_products(k, d.consistent_mass, velocity, j); Sum scale(k, 1);
        scale.add(std::abs(d.primary.momentum[j].high())); scale.add(std::abs(d.primary.momentum[j].low()));
        for (double term : terms) scale.add(std::abs(term));
        const double error = sum(k, {product[j], -d.primary.momentum[j].high(), -d.primary.momentum[j].low()});
        k.scalar(Field::pi1, d.primary.momentum[j].high());
        k.scalar(Field::retained_momentum_low, d.primary.momentum[j].low(), static_cast<std::uint32_t>(j));
        k.scalar(Field::compatibility1, error);
        k.within(error, scale.get(), 256 * epsilon, "Actual canonical momentum cannot be decoded compatibly");
    }
    return velocity;
}
double dot(Kernel& k, std::span<const double> a, std::span<const double> b) {
    k.require(a.size() == b.size() && a.size() <= 33, Code::unsupported_configuration, "Canonical dot shape differs");
    Sum result(k);
    for (std::size_t i = 0; i < a.size(); ++i) result.add(k.mul(a[i], b[i]));
    return result.get();
}
double kinetic(Kernel& k, const Data& d, double& maximum_ratio) {
    const auto product = multiply(k, d.consistent_mass, d.velocities_m_per_s, maximum_ratio);
    const double result = k.mul(.5, dot(k, d.velocities_m_per_s, product));
    k.require(result >= 0, Code::numerical_precision, "Negative resolved consistent kinetic energy");
    return result;
}
}
