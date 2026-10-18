#include "../src/mac_pressure_multigrid.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>

namespace {
using namespace ma::experimental;
using namespace ma::experimental::interval_detail;
constexpr auto none = std::numeric_limits<std::size_t>::max();
void check(bool yes, const char* reason) { if (!yes) throw std::runtime_error(reason); }
struct Face { std::size_t left = none, right = none; };
struct Matrix {
    MacGrid grid; std::vector<double> h; std::array<std::vector<double>, 2> g;
    std::array<std::vector<Face>, 2> faces;
    explicit Matrix(MacGrid source) : grid(std::move(source)), h(grid.walls.size()) {
        const auto cell = [&](std::size_t i) { return grid.walls[i] ? none : i; };
        faces[0].resize(grid.x_width() * grid.height); faces[1].resize(grid.width * grid.y_height());
        for (std::size_t y = 0; y < grid.height; ++y) for (std::size_t x = 0; x < grid.x_width(); ++x) {
            auto& f = faces[0][grid.x_face(x, y)];
            if (x > 0 || grid.periodic_x) f.left = cell((x + grid.width - 1) % grid.width + y * grid.width);
            if (x < grid.width) f.right = cell(x + y * grid.width);
        }
        for (std::size_t y = 0; y < grid.y_height(); ++y) for (std::size_t x = 0; x < grid.width; ++x) {
            auto& f = faces[1][grid.y_face(x, y)];
            if (y > 0 || grid.periodic_y) f.left = cell(x + ((y + grid.height - 1) % grid.height) * grid.width);
            if (y < grid.height) f.right = cell(x + y * grid.width);
        }
        for (std::size_t i = 0; i < h.size(); ++i) if (!grid.walls[i]) h[i] = .01 * (1 + .1 * std::sin(static_cast<double>(i)));
        for (std::size_t a = 0; a < 2; ++a) {
            g[a].resize(faces[a].size());
            for (std::size_t f = 0; f < faces[a].size(); ++f) if (faces[a][f].left != none && faces[a][f].right != none)
                g[a][f] = 1 + .2 * std::cos(static_cast<double>(f + 17 * a));
        }
    }
    std::vector<double> apply(const std::vector<double>& x) const {
        std::vector<double> out(x.size()); for (std::size_t i = 0; i < x.size(); ++i) out[i] = h[i] * x[i];
        for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < faces[a].size(); ++f) {
            const auto& pair = faces[a][f]; if (pair.left == none || pair.right == none) continue;
            const double v = g[a][f] * (x[pair.left] - x[pair.right]); out[pair.left] += v; out[pair.right] -= v;
        }
        return out;
    }
};
long double dot(const std::vector<double>& a, const std::vector<double>& b) {
    long double sum = 0; for (std::size_t i = 0; i < a.size(); ++i) sum += static_cast<long double>(a[i]) * b[i]; return sum;
}
void run(int id, MacGrid grid) {
    Matrix matrix(std::move(grid)); const auto topology = std::make_shared<const PressureMultigridTopology>(matrix.grid); PressureMultigrid mg(topology);
    check(mg.prepare(matrix.h, matrix.g[0], matrix.g[1]), "Valid multigrid coefficients rejected");
    const auto n = matrix.h.size(); std::vector<double> u(n), v(n), bu(n), bv(n), truth(n);
    for (std::size_t i = 0; i < n; ++i) if (!matrix.grid.walls[i]) {
        u[i] = std::sin(.21 * static_cast<double>(i + 1)); v[i] = std::cos(.37 * static_cast<double>(i + 2));
        truth[i] = .3 * u[i] + .4 * v[i];
    }
    if (id == 1) for (std::size_t i = 0; i < n; ++i) if (i % matrix.grid.width >= 6) u[i] = 0;
    check(mg.apply(u, bu) && mg.apply(v, bv), "Multigrid application failed");
    check(dot(u, bu) > 0 && dot(v, bv) > 0, "Multigrid preconditioner lost positivity");
    const auto uv = dot(u, bv), vu = dot(bu, v);
    check(std::abs(uv - vu) < 1e-10L * (std::abs(uv) + std::abs(vu)), "Multigrid preconditioner is not symmetric");
    if (id == 1) for (std::size_t i = 0; i < n; ++i) if (i % matrix.grid.width >= 6) check(bu[i] == 0, "Coarsening coupled disconnected rooms");
    const auto rhs = matrix.apply(truth); std::vector<double> x(n), r = rhs, z(n), direction(n);
    check(mg.apply(r, z), "Initial multigrid PCG preconditioner failed"); direction = z;
    auto rz = dot(r, z); const auto initial = dot(r, r); std::size_t iterations = 0;
    for (; iterations < 256; ++iterations) {
        const auto applied = matrix.apply(direction); const double alpha = static_cast<double>(rz / dot(direction, applied));
        for (std::size_t i = 0; i < n; ++i) { x[i] += alpha * direction[i]; r[i] -= alpha * applied[i]; }
        if (dot(r, r) <= 1e-24L * initial) { ++iterations; break; }
        check(mg.apply(r, z), "Multigrid PCG preconditioner failed"); const auto next = dot(r, z); const double beta = static_cast<double>(next / rz); rz = next;
        for (std::size_t i = 0; i < n; ++i) direction[i] = z[i] + beta * direction[i];
    }
    check(iterations < 64, "Multigrid PCG did not converge on the independent manufactured field");
    const auto actual = matrix.apply(x); double error = 0, residual = 0, scale = 0;
    for (std::size_t i = 0; i < n; ++i) { error = std::max(error, std::abs(x[i] - truth[i])); residual = std::max(residual, std::abs(actual[i] - rhs[i])); scale = std::max(scale, std::abs(rhs[i])); }
    check(error < 1e-9 && residual < 2e-11 * scale, "Multigrid PCG returned the wrong field or residual");
    std::cout << "MGCASE " << id << ' ' << matrix.grid.width << ' ' << matrix.grid.height << ' ' << matrix.grid.periodic_x << ' ' << matrix.grid.periodic_y << ' '
        << topology->levels().size() << ' ' << iterations << '\n';
    for (std::size_t i = 0; i < n; ++i) std::cout << "MGCELL " << id << ' ' << i << ' ' << static_cast<int>(matrix.grid.walls[i]) << ' ' << matrix.h[i] << '\n';
    for (std::size_t a = 0; a < 2; ++a) for (std::size_t f = 0; f < matrix.g[a].size(); ++f) std::cout << "MGFACE " << id << ' ' << a << ' ' << f << ' ' << matrix.g[a][f] << '\n';
    for (std::size_t l = 0; l < topology->levels().size(); ++l) {
        const auto& level = topology->levels()[l]; const auto& value = mg.values()[l];
        std::cout << "MGLEVEL " << id << ' ' << l << ' ' << level.stride << ' ' << level.coordinates.size() << ' ' << level.edges.size() << '\n';
        for (std::size_t i = 0; i < level.coordinates.size(); ++i) {
            std::cout << "MGNODE " << id << ' ' << l << ' ' << i << ' ' << level.coordinates[i][0] << ' ' << level.coordinates[i][1] << ' ' << value.compliance[i] << ' ' << value.diagonal[i] << '\n';
            if (l + 1 < topology->levels().size()) {
                std::cout << "MGPARENT " << id << ' ' << l << ' ' << i << ' ' << level.parent[i] << '\n';
                for (auto k = level.prolong_offsets[i]; k < level.prolong_offsets[i + 1]; ++k)
                    std::cout << "MGP " << id << ' ' << l << ' ' << i << ' ' << level.prolong[k].node << ' ' << level.prolong[k].weight << '\n';
            }
        }
        for (std::size_t e = 0; e < level.edges.size(); ++e) {
            const auto& edge = level.edges[e]; std::cout << "MGEDGE " << id << ' ' << l << ' ' << e << ' ' << edge.left << ' ' << edge.right << ' ' << edge.axis << ' ' << value.conductance[e] << '\n';
        }
        if (l + 1 < topology->levels().size()) for (std::size_t i = 0; i < topology->levels()[l + 1].coordinates.size(); ++i)
            for (auto k = level.restrict_offsets[i]; k < level.restrict_offsets[i + 1]; ++k)
                std::cout << "MGR " << id << ' ' << l << ' ' << i << ' ' << level.restrict[k].node << ' ' << level.restrict[k].weight << '\n';
    }
    for (const auto& [name, vector] : std::array<std::pair<const char*, const std::vector<double>*>, 7>{{{"u", &u}, {"v", &v}, {"bu", &bu}, {"bv", &bv}, {"rhs", &rhs}, {"truth", &truth}, {"solution", &x}}})
        for (std::size_t i = 0; i < n; ++i) std::cout << "MGV " << id << ' ' << name << ' ' << i << ' ' << (*vector)[i] << '\n';
    for (auto& h : matrix.h) h *= 1.7;
    for (auto& axis : matrix.g) for (auto& g : axis) g *= .8;
    check(mg.prepare(matrix.h, matrix.g[0], matrix.g[1]) && mg.apply(u, bu), "Multigrid coefficient refresh failed");
    PressureMultigrid fresh(topology); check(fresh.prepare(matrix.h, matrix.g[0], matrix.g[1]) && fresh.apply(u, bv) && bu == bv, "Multigrid continuation depends on old coefficients/workspace");
    const auto saved = matrix.h; matrix.h[topology->active_cells().front()] = std::numeric_limits<double>::quiet_NaN();
    check(!mg.prepare(matrix.h, matrix.g[0], matrix.g[1]), "Invalid multigrid coefficients admitted"); matrix.h = saved;
    check(mg.prepare(matrix.h, matrix.g[0], matrix.g[1]) && mg.apply(u, bu) && bu == bv, "Rejected coefficient update leaked into continuation");
    std::cout << "MGREFRESH " << id << " 1\n";
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        run(0, {9, 7, 1, 1, true, true, std::vector<std::uint8_t>(63)});
        MacGrid walls{13, 9, 1, 1, false, false, std::vector<std::uint8_t>(117)};
        for (std::size_t y = 0; y < 9; ++y) walls.walls[6 + 13 * y] = 1;
        for (const auto i : {101U, 103U, 89U, 115U, 14U, 27U}) walls.walls[i] = 1;
        run(1, std::move(walls)); run(2, {65, 1, 1, 1, false, false, std::vector<std::uint8_t>(65)});
        run(3, {2, 33, 1, 1, true, true, std::vector<std::uint8_t>(66)});
        MacGrid isolated{13, 11, 1, 1, false, false, std::vector<std::uint8_t>(143)};
        for (std::size_t i = 0; i < isolated.walls.size(); ++i) isolated.walls[i] = static_cast<std::uint8_t>((i % 13 + i / 13) % 2);
        run(4, std::move(isolated)); // More isolated cells than the dense coarse-solve limit.
        std::cout << "PASS symmetric multigrid, odd periodic grids, disconnected pores and coefficient refresh\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
