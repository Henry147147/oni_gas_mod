#include "canonical_material_internal.hpp"

namespace ma::experimental::canonical_detail {
namespace {
struct Mode { std::size_t a = 0, b = 0; int sign = 0; };
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
std::vector<Mode> modes(std::size_t n) {
    std::vector<Mode> result; result.reserve(n);
    for (std::size_t j = 0; j < n / 2; ++j) {
        result.push_back({j, n - 1 - j, 1}); result.push_back({j, n - 1 - j, -1});
    }
    if (n % 2 != 0) result.push_back({n / 2, n / 2, 0});
    return result;
}
Matrix congruence(Kernel& k, const Matrix& a, const std::vector<Mode>& basis, const Vec& indices) {
    Matrix result = zeros(basis.size());
    for (std::size_t i = 0; i < basis.size(); ++i) for (std::size_t j = 0; j <= i; ++j) {
        const auto x = basis[i], y = basis[j];
        k.row_scope(static_cast<std::uint32_t>(indices[x.a]));
        k.row(Field::node_indices, indices); k.row(Field::matrix_coefficients, a[x.a], 0);
        if (x.b != x.a) k.row(Field::matrix_coefficients, a[x.b], 1);
        const Vec coordinates{static_cast<double>(x.a), static_cast<double>(x.b), static_cast<double>(x.sign),
            static_cast<double>(y.a), static_cast<double>(y.b), static_cast<double>(y.sign)};
        k.row(Field::matrix_operands, coordinates); k.scalar(Field::basis_index, static_cast<double>(i));
        double value;
        if (x.sign == 0 && y.sign == 0) value = a[x.a][y.a];
        else if (x.sign == 0) value = sum(k, {a[x.a][y.a], y.sign == 1 ? a[x.a][y.b] : -a[x.a][y.b]});
        else if (y.sign == 0) value = sum(k, {a[x.a][y.a], x.sign == 1 ? a[x.b][y.a] : -a[x.b][y.a]});
        else if (x.sign == y.sign) {
            const double diagonal = sum(k, {a[x.a][y.a], a[x.b][y.b]});
            const double cross = sum(k, {a[x.a][y.b], a[x.b][y.a]});
            value = sum(k, {diagonal, x.sign == 1 ? cross : -cross});
        } else {
            const double diagonal = sum(k, {a[x.a][y.a], -a[x.b][y.b]});
            const double cross = x.sign == 1 ? sum(k, {a[x.b][y.a], -a[x.a][y.b]}) :
                sum(k, {a[x.a][y.b], -a[x.b][y.a]});
            value = sum(k, {diagonal, cross});
        }
        result[i][j] = k.finite(value); result[j][i] = value; // Copy, never separately recompute symmetry.
    }
    return result;
}
Vec transpose(Kernel& k, const Vec& x, const std::vector<Mode>& basis, const Vec& indices, bool inverse) {
    Vec result(basis.size());
    for (std::size_t i = 0; i < basis.size(); ++i) {
        const auto mode = basis[i]; k.row_scope(static_cast<std::uint32_t>(indices[mode.a]));
        k.row(Field::matrix_operands, x); k.row(Field::node_indices, indices);
        k.scalar(Field::basis_index, static_cast<double>(i));
        const double value = mode.sign == 0 ? k.finite(x[mode.a]) :
            sum(k, {x[mode.a], mode.sign == 1 ? x[mode.b] : -x[mode.b]});
        result[i] = inverse && mode.sign != 0 ? k.mul(.5, value) : value;
    }
    return result;
}
Vec expand(Kernel& k, const Vec& x, const std::vector<Mode>& basis, const Vec& indices, bool inverse_transpose) {
    Vec result(x.size());
    for (std::size_t i = 0; i < basis.size();) {
        const auto mode = basis[i]; k.row_scope(static_cast<std::uint32_t>(indices[mode.a]));
        k.row(Field::matrix_operands, x); k.row(Field::node_indices, indices);
        k.scalar(Field::basis_index, static_cast<double>(i));
        if (mode.sign == 0) { result[mode.a] = k.finite(x[i]); ++i; continue; }
        const double even = inverse_transpose ? k.mul(.5, x[i]) : x[i];
        const double odd = inverse_transpose ? k.mul(.5, x[i + 1]) : x[i + 1];
        result[mode.a] = sum(k, {even, odd}); result[mode.b] = sum(k, {even, -odd}); i += 2;
    }
    return result;
}
struct RowProduct { double value = 0, magnitude = 0; Vec products; };
RowProduct direct_row(Kernel& k, const Matrix& a, const Vec& x, std::size_t i, const Vec& indices) {
    k.row_scope(static_cast<std::uint32_t>(indices[i]));
    k.row(Field::matrix_coefficients, a[i]); k.row(Field::matrix_operands, x); k.row(Field::node_indices, indices);
    RowProduct result; result.products.resize(x.size()); Sum value(k, 0), magnitude(k, 1);
    for (std::size_t j = 0; j < x.size(); ++j) {
        k.row(Field::matrix_products, std::span<const double>(result.products.data(), j));
        result.products[j] = k.mul(a[i][j], x[j]); value.add(result.products[j]); magnitude.add(std::abs(result.products[j]));
    }
    k.row(Field::matrix_products, result.products); result.value = value.get(); result.magnitude = magnitude.get();
    return result;
}
Vec direct_product(Kernel& k, const Matrix& a, const Vec& x, const Vec& indices) {
    Vec result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) result[i] = direct_row(k, a, x, i, indices).value;
    return result;
}
Vec dense_spd(Kernel& k, const Matrix& a, const Vec& rhs, const Matrix& original,
    const std::vector<Mode>& basis, const Vec& indices, double& maximum_ratio) {
    const auto n = rhs.size(); Vec scales(n), y(n), x(n), basis_indices(n); Matrix lower = zeros(n);
    for (std::size_t i = 0; i < n; ++i) basis_indices[i] = indices[basis[i].a];
    for (std::size_t i = 0; i < n; ++i) {
        k.row_scope(static_cast<std::uint32_t>(indices[basis[i].a]));
        k.row(Field::matrix_coefficients, original[basis[i].a], 0); k.row(Field::matrix_coefficients, a[i], 1);
        k.row(Field::node_indices, indices); k.scalar(Field::basis_index, static_cast<double>(i));
        scales[i] = k.sqrt(a[i][i]);
    }
    k.factorization();
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j <= i; ++j) {
        k.row_scope(static_cast<std::uint32_t>(indices[basis[i].a]));
        k.row(Field::matrix_coefficients, original[basis[i].a], 0); k.row(Field::matrix_coefficients, a[i], 1);
        k.row(Field::node_indices, indices); k.row(Field::matrix_operands, rhs, 0); k.row(Field::matrix_operands, scales, 1);
        k.row(Field::matrix_operands, std::span<const double>(lower[i].data(), j), 2);
        k.row(Field::matrix_operands, std::span<const double>(lower[j].data(), j), 3);
        k.scalar(Field::basis_index, static_cast<double>(i)); Sum sum_value(k); Vec products(j);
        sum_value.add(k.div(k.div(a[i][j], scales[i]), scales[j]));
        for (std::size_t p = 0; p < j; ++p) {
            k.row(Field::matrix_products, std::span<const double>(products.data(), p));
            products[p] = k.mul(lower[i][p], lower[j][p]); sum_value.add(-products[p]);
        }
        k.row(Field::matrix_products, products);
        const double value = sum_value.get();
        lower[i][j] = i == j ? k.sqrt(value) : k.div(value, lower[j][j]);
    }
    for (std::size_t i = 0; i < n; ++i) {
        k.row_scope(static_cast<std::uint32_t>(indices[basis[i].a]));
        k.row(Field::matrix_coefficients, a[i], 0); k.row(Field::matrix_coefficients, lower[i], 1);
        k.row(Field::node_indices, indices); k.row(Field::matrix_operands, rhs, 0); k.row(Field::matrix_operands, scales, 1);
        k.row(Field::matrix_operands, std::span<const double>(y.data(), i), 2);
        k.scalar(Field::basis_index, static_cast<double>(i)); Sum value(k); Vec products(i);
        value.add(k.div(rhs[i], scales[i]));
        for (std::size_t j = 0; j < i; ++j) {
            k.row(Field::matrix_products, std::span<const double>(products.data(), j));
            products[j] = k.mul(lower[i][j], y[j]); value.add(-products[j]);
        }
        k.row(Field::matrix_products, products); y[i] = k.div(value.get(), lower[i][i]);
    }
    for (std::size_t i = n; i-- != 0;) {
        Vec column(n), products(n - i - 1);
        for (std::size_t j = i; j < n; ++j) column[j] = lower[j][i];
        k.row_scope(static_cast<std::uint32_t>(indices[basis[i].a]));
        k.row(Field::matrix_coefficients, a[i], 0); k.row(Field::matrix_coefficients, column, 1);
        k.row(Field::node_indices, indices); k.row(Field::matrix_operands, rhs, 0); k.row(Field::matrix_operands, scales, 1);
        k.row(Field::matrix_operands, std::span<const double>(x.data() + i + 1, n - i - 1), 2);
        k.scalar(Field::basis_index, static_cast<double>(i)); k.scalar(Field::matrix_operands, y[i]); Sum value(k); value.add(y[i]);
        for (std::size_t j = i + 1; j < n; ++j) {
            k.row(Field::matrix_products, std::span<const double>(products.data(), j - i - 1));
            products[j - i - 1] = k.mul(lower[j][i], x[j]); value.add(-products[j - i - 1]);
        }
        k.row(Field::matrix_products, products); x[i] = k.div(value.get(), lower[i][i]);
    }
    for (std::size_t i = 0; i < n; ++i) x[i] = k.div(x[i], scales[i]);
    for (std::size_t i = 0; i < n; ++i) {
        const auto product = direct_row(k, a, x, i, basis_indices);
        const double error = difference(k, product.value, rhs[i]);
        const double scale = sum(k, {std::abs(rhs[i]), product.magnitude});
        k.scalar(Field::linear_residual, error); k.scalar(Field::basis_index, static_cast<double>(i));
        k.within(error, scale, 256 * epsilon, "Transformed SPD backward error exceeds its local bound");
        maximum_ratio = std::max(maximum_ratio, k.ratio(error, scale));
    }
    return x;
}
}

Vec physical_row_products(Kernel& k, const Matrix& a, const Vec& x, std::size_t i) {
    return direct_row(k, a, x, i, index_values(x.size(), {})).products;
}
Vec multiply(Kernel& k, const Matrix& a, const Vec& x, double& maximum_ratio, std::span<const std::size_t> indices) {
    shape(k, a, x.size(), indices);
    const auto physical_indices = index_values(x.size(), indices); const auto basis = modes(x.size());
    const auto coordinates = transpose(k, x, basis, physical_indices, true);
    const auto transformed = congruence(k, a, basis, physical_indices);
    Vec basis_indices(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) basis_indices[i] = physical_indices[basis[i].a];
    const auto values = direct_product(k, transformed, coordinates, basis_indices);
    const auto result = expand(k, values, basis, physical_indices, true);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto direct = direct_row(k, a, x, i, physical_indices);
        const double error = difference(k, result[i], direct.value);
        k.scalar(Field::matrix_products, result[i], 0); k.scalar(Field::matrix_products, direct.value, 1);
        k.within(error, direct.magnitude, 256 * epsilon, "Parity product differs from the independent physical row");
        maximum_ratio = std::max(maximum_ratio, k.ratio(error, direct.magnitude));
    }
    return result;
}
Vec solve_spd(Kernel& k, const Matrix& a, const Vec& rhs, double& maximum_ratio, std::span<const std::size_t> indices) {
    shape(k, a, rhs.size(), indices);
    const auto physical_indices = index_values(rhs.size(), indices); const auto basis = modes(rhs.size());
    const auto transformed = congruence(k, a, basis, physical_indices);
    const auto b = transpose(k, rhs, basis, physical_indices, false);
    const auto y = dense_spd(k, transformed, b, a, basis, physical_indices, maximum_ratio);
    const auto x = expand(k, y, basis, physical_indices, false);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto product = direct_row(k, a, x, i, physical_indices);
        const double error = difference(k, product.value, rhs[i]);
        const double scale = sum(k, {std::abs(rhs[i]), product.magnitude});
        k.scalar(Field::linear_residual, error); k.scalar(Field::matrix_operands, rhs[i]);
        k.within(error, scale, 256 * epsilon, "SPD solution fails the original physical row");
        maximum_ratio = std::max(maximum_ratio, k.ratio(error, scale));
    }
    return x;
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
    Matrix a = zeros(free.size()); Vec rhs(free.size());
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
        Sum value(k); value.add(d.primary.momentum[j].high()); value.add(d.primary.momentum[j].low());
        for (std::size_t p = 0; p < n; ++p) if (constrained(d, p)) {
            const auto known = retained_product(k, retained_value(k, velocity[p]), d.consistent_mass[j][p]);
            for (double term : known.product_terms) value.add(-term);
        }
        rhs[i] = value.get();
        for (std::size_t p = 0; p < free.size(); ++p) a[i][p] = d.consistent_mass[j][free[p]];
    }
    const auto decoded = solve_spd(k, a, rhs, maximum_ratio, free);
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
