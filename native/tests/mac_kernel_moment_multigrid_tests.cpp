// An independent real block matrix checks the coupled complex solve. No
// pressure-root formula or complex transformation is used by this oracle.
#include "../src/mac_kernel_detail.hpp"
#include "mac_kernel_fixture.hpp"
#include <future>
#include <iomanip>
#include <iostream>

using namespace ma::experimental;
using namespace ma::experimental::mac_kernel;
namespace {
void check(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
struct Matrix {
    MacGrid grid; std::vector<double> comp, diagonal, wx, wy;
    struct Edge { std::size_t a, b; double w; };
    std::vector<Edge> edges;
    explicit Matrix(MacGrid source) : grid(std::move(source)), comp(grid.walls.size()), diagonal(comp.size()),
        wx(grid.x_width() * grid.height), wy(grid.width * grid.y_height()) {
        for (std::size_t i = 0; i < comp.size(); ++i) if (!grid.walls[i]) {
            comp[i] = .2 + .05 * std::sin(.7 * static_cast<double>(i));
            diagonal[i] = comp[i] + (i % 7 == 0 ? .3 : 0); // Vacuum boundary contribution.
        }
        const auto edge = [&](std::size_t a, std::size_t b, std::size_t face, std::vector<double>& w) {
            if (grid.walls[a] || grid.walls[b] || a == b) return;
            const double value = 1 + .2 * std::cos(.3 * static_cast<double>(face));
            edges.push_back({a, b, value}); w[face] = value;
        };
        for (std::size_t y = 0; y < grid.height; ++y) for (std::size_t x = 0; x < grid.width; ++x) {
            const auto i = x + grid.width * y;
            if (x + 1 < grid.width || grid.periodic_x) edge(i, (x + 1) % grid.width + grid.width * y,
                grid.x_face((x + 1) % grid.x_width(), y), wx);
            if (y + 1 < grid.height || grid.periodic_y) edge(i, x + grid.width * ((y + 1) % grid.height),
                grid.y_face(x, (y + 1) % grid.y_height()), wy);
        }
    }
    std::vector<double> h(const std::vector<double>& v) const {
        std::vector<double> out(v.size());
        for (std::size_t i = 0; i < v.size(); ++i) out[i] = (diagonal[i] - comp[i]) * v[i];
        for (const auto& e : edges) { const double f = e.w * (v[e.a] - v[e.b]); out[e.a] += f; out[e.b] -= f; }
        return out;
    }
    std::pair<std::vector<double>, std::vector<double>> apply(const std::vector<double>& a, const std::vector<double>& b) const {
        auto ca = a, cb = b;
        for (std::size_t i = 0; i < a.size(); ++i) if (!grid.walls[i]) { ca[i] /= comp[i]; cb[i] /= comp[i]; }
        const auto ha = h(ca), hb = h(cb); auto ra = a, rb = b;
        for (std::size_t i = 0; i < a.size(); ++i) {
            ra[i] += 2 * ha[i] - std::sqrt(3.) * hb[i]; rb[i] += std::sqrt(3.) * ha[i] - hb[i];
        }
        return {ra, rb};
    }
};
void executor(const void* p, std::size_t blocks, interval_detail::BlockFunction fn, const void* job) {
    static_cast<Pool*>(const_cast<void*>(p))->run(blocks, [&](std::size_t b) { fn(job, b); });
}
void run(unsigned id, MacGrid grid) {
    Matrix matrix(std::move(grid)); const auto n = matrix.comp.size();
    const auto topology = std::make_shared<const interval_detail::PressureMultigridTopology>(matrix.grid);
    PressureMomentMultigrid mg(topology); Pool pool(12);
    std::vector<double> a(n), b(n), da(n, 17), db(n, 19);
    for (std::size_t i = 0; i < n; ++i) if (!matrix.grid.walls[i]) {
        a[i] = .2 * std::sin(.21 * static_cast<double>(i + 1)); b[i] = .3 * std::cos(.37 * static_cast<double>(i + 2));
    }
    auto [ra, rb] = matrix.apply(a, b);
    check(!mg.apply_pair(a, b, da, db) && da == std::vector<double>(n, 17) && db == std::vector<double>(n, 19),
        "Unprepared direct operator published output");
    for (unsigned shape = 0; shape < 4; ++shape) {
        bool rejected = false;
        try {
            static_cast<void>(mg.apply_pair(std::span<const double>(a).first(n - (shape == 0)),
                std::span<const double>(b).first(n - (shape == 1)),
                std::span<double>(da).first(n - (shape == 2)), std::span<double>(db).first(n - (shape == 3))));
        } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && da == std::vector<double>(n, 17) && db == std::vector<double>(n, 19),
            "Malformed direct operator request published output");
    }
    check(mg.prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy), "Valid coupled coefficients rejected");
    std::vector<double> op_a(n, 17), op_b(n, 19);
    check(mg.apply_pair(a, b, op_a, op_b), "Prepared direct operator declined");
    auto ca_input = a, cb_input = b;
    for (std::size_t i = 0; i < n; ++i) if (!matrix.grid.walls[i]) {
        ca_input[i] /= matrix.comp[i]; cb_input[i] /= matrix.comp[i];
    }
    const auto ha_input = matrix.h(ca_input), hb_input = matrix.h(cb_input);
    double op_error = 0, op_scale = 0;
    for (std::size_t i = 0; i < n; ++i) {
        op_error = std::max({op_error, std::abs(op_a[i] - ra[i]), std::abs(op_b[i] - rb[i])});
        op_scale = std::max(op_scale, std::abs(a[i]) + std::abs(b[i]) + 2 * std::abs(ha_input[i]) + 2 * std::abs(hb_input[i]));
    }
    check(op_error <= 128 * eps * op_scale, "Direct operator failed independent real block equation");
    auto alias_a = a, alias_b = b;
    check(mg.apply_pair(alias_a, alias_b, alias_a, alias_b) && alias_a == op_a && alias_b == op_b,
        "Direct operator depends on input/output aliasing");
    auto held_op_a = op_a, held_op_b = op_b;
    const auto result = mg.solve(ra, rb, da, db, 1e-12, 1e-15);
    check(result.converged && !result.flagged && result.iterations < 128, "Coupled solve did not converge");
    const auto [actual_a, actual_b] = matrix.apply(da, db); double error = 0, residual = 0, scale = 0;
    for (std::size_t i = 0; i < n; ++i) {
        error = std::max({error, std::abs(da[i] - a[i]), std::abs(db[i] - b[i])});
        residual = std::max({residual, std::abs(actual_a[i] - ra[i]), std::abs(actual_b[i] - rb[i])});
        scale = std::max({scale, std::abs(ra[i]), std::abs(rb[i])});
    }
    check(error < 1e-9 && residual < 2e-11 * scale, "Independent real block equation failed");
    const auto held_a = da, held_b = db;
    mg.set_executor(executor, &pool);
    check(mg.apply_pair(a, b, op_a, op_b) && op_a == held_op_a && op_b == held_op_b,
        "Direct operator depends on worker count");
    std::fill(op_a.begin(), op_a.end(), 17); std::fill(op_b.begin(), op_b.end(), 19);
    check(mg.apply_pair(a, b, op_a, op_b, false), "Local direct operator declined");
    for (std::size_t i = 0; i < n; ++i) check(matrix.grid.walls[i] ? (op_a[i] == 17 && op_b[i] == 19) :
        (op_a[i] == held_op_a[i] && op_b[i] == held_op_b[i]), "Local operator touched unrelated output");
    auto invalid_input = a; invalid_input[topology->active_cells().front()] = std::numeric_limits<double>::quiet_NaN();
    held_op_a = op_a; held_op_b = op_b;
    check(!mg.apply_pair(invalid_input, b, op_a, op_b, false) && op_a == held_op_a && op_b == held_op_b,
        "Nonfinite operator published output");
    auto overflow_input = a;
    overflow_input[topology->active_cells().front()] = std::numeric_limits<double>::max();
    check(!mg.apply_pair(overflow_input, b, op_a, op_b) && op_a == held_op_a && op_b == held_op_b,
        "Overflowed direct operator published output");
    const auto parallel = mg.solve(ra, rb, da, db, 1e-12, 1e-15);
    check(parallel.converged && parallel.iterations == result.iterations && da == held_a && db == held_b,
        "Coupled solve depends on thread count");
    // Reuse topology with different coefficients and compare a cold workspace.
    for (auto& c : matrix.comp) c *= 1.7;
    for (auto& d : matrix.diagonal) d *= 1.7;
    std::tie(ra, rb) = matrix.apply(a, b);
    check(mg.prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy) && mg.solve(ra, rb, da, db, 1e-12, 1e-15).converged,
        "Coupled coefficient refresh failed");
    PressureMomentMultigrid cold(topology); auto ca = da, cb = db;
    check(cold.prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy) && cold.solve(ra, rb, ca, cb, 1e-12, 1e-15).converged &&
        ca == da && cb == db, "Coupled continuation depends on stale coefficients");
    const auto saved = matrix.comp; matrix.comp[topology->active_cells().front()] = std::numeric_limits<double>::quiet_NaN();
    check(!mg.prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy), "Nonfinite coupled coefficient admitted");
    check(!mg.apply_pair(a, b, op_a, op_b) && op_a == held_op_a && op_b == held_op_b,
        "Rejected coefficients left the direct operator prepared");
    const auto failed = mg.solve(ra, rb, da, db, 1e-12, 1e-15);
    check(failed.flagged && !failed.converged && da == ca && db == cb, "Rejected coupled solve published a correction");
    matrix.comp = saved;
    check(mg.prepare(matrix.comp, matrix.diagonal, matrix.wx, matrix.wy) && mg.solve(ra, rb, da, db, 1e-12, 1e-15).converged &&
        ca == da && cb == db, "Rejected coefficients leaked into coupled continuation");
    if (result.iterations > 1) {
        const auto limited = mg.solve(ra, rb, da, db, 0, 0, 1);
        check(!limited.converged && ca == da && cb == db, "Unconverged coupled solve published its iterate");
    }
    std::cout << "CMG " << id << ' ' << n << ' ' << result.iterations << ' ' << error << ' ' << residual << ' ' << mg.storage_bytes() << '\n';
}
void owners() {
    using namespace ma::testing;
    using namespace ma::testing::kernel;
    const auto& s = setup(); auto scene = wave(193, 173);
    scene.settings.numerical_kernel = MacNumericalKernel::production;
    scene.settings.pressure_integration = MacPressureIntegration::two_moment_coupled;
    const MacClosedWorld source(s.registry, s.eos, scene.state, scene.settings);
    const std::array<MacCellHeat, 1> heat{{{0, .002, 1e6}}}; const auto hot = source.with_cell_heat(heat);
    MacKernelOptions options; options.threads = 12;
    const auto advance = [&](const MacClosedWorld& world) { return advance_mac_production(world, .002, 64, options).world.checkpoint(); };
    const auto expected = advance(hot); auto concurrent = std::async(std::launch::async, [&] { return advance(hot); });
    static_cast<void>(advance(source));
    check(concurrent.get() == expected, "Concurrent coupled owners shared coefficients");
    // A geometry change with the same number of cells must invalidate topology.
    std::swap(scene.state.grid.width, scene.state.grid.height);
    scene.state.momentum_x.resize(scene.state.grid.x_width() * scene.state.grid.height);
    scene.state.momentum_y.resize(scene.state.grid.width * scene.state.grid.y_height());
    std::fill(scene.state.momentum_x.begin(), scene.state.momentum_x.end(), 0);
    std::fill(scene.state.momentum_y.begin(), scene.state.momentum_y.end(), 0);
    const MacClosedWorld reshaped(s.registry, s.eos, scene.state, scene.settings); const auto changed = advance(reshaped);
    auto cold = std::async(std::launch::async, [&] { return advance(reshaped); });
    check(cold.get() == changed && advance(hot) == expected, "Coupled topology key lost geometry or cold continuation");
    std::cout << "PASS concurrent, heated and reshaped coupled owners\n";
}
void flagged_sweep() {
    using namespace ma::testing;
    const auto& s = setup(); auto state = s.state(3, 2, true);
    for (auto& cell : state.cells) {
        auto masses = cell.mixture.masses; for (auto& mass : masses) mass = std::ldexp(mass, 420);
        cell.mixture = s.eos->initialize_at_temperature(masses, 300, 1).mixture; cell.passive_quantities.clear();
    }
    state.cells[0].passive_quantities = {{17, 0x1p450}};
    for (auto& j : state.momentum_x) j = 0x1p-700;
    state.grid.spacing_m = 0x1p-500; state.grid.depth_m = 0x1p1000;
    const MacClosedWorld reference(s.registry, s.eos, state, {});
    auto settings = reference.settings(); settings.numerical_kernel = MacNumericalKernel::production;
    settings.pressure_integration = MacPressureIntegration::two_moment_coupled;
    const MacClosedWorld source(s.registry, s.eos, state, settings); const auto held = source.checkpoint();
    const auto next = advance_mac_production(source, .002, 1, {}); const auto report = last_mac_production_report();
    check(next.report.attempts == 1 && !report.reference_fallback && report.maximum_volume_closure <= 16 * eps &&
        fingerprint(next.world.state()) == fingerprint(reference.advanced(.002, 1).world.state()) && source.checkpoint() == held,
        "Coupled normal sweep correction changed closure, range, budget or strict state");
    std::cout << "PASS flagged intermediate with normal final sweep retains strict endpoint in one attempt\n";
}
void breakdown() {
    const MacGrid grid{3, 1, 1, 1, false, false, {0, 1, 0}};
    PressureMomentMultigrid mg(std::make_shared<const interval_detail::PressureMultigridTopology>(grid));
    const std::vector<double> comp{1, 0, 1}, wx(4), wy(6), ra{1, 0, std::sqrt(3.)}, rb{0, 0, 2};
    std::vector<double> a(3, 7), b(3, 9); check(mg.prepare(comp, comp, wx, wy), "Singleton preparation failed");
    const auto result = mg.solve(ra, rb, a, b, 1e-12, 0);
    check(result.flagged && !result.converged && a == std::vector<double>(3, 7) && b == std::vector<double>(3, 9),
        "Undecidable transpose product did not fail closed");
    std::cout << "PASS undecidable bilinear product preserves caller correction\n";
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        run(0, {9, 7, 1, 1, true, true, std::vector<std::uint8_t>(63)});
        MacGrid walls{13, 9, 1, 1, false, false, std::vector<std::uint8_t>(117)};
        for (std::size_t y = 0; y < 9; ++y) walls.walls[6 + 13 * y] = 1;
        for (auto i : {101U, 103U, 89U, 115U, 14U, 27U}) walls.walls[i] = 1;
        run(1, std::move(walls)); run(2, {65, 1, 1, 1, false, false, std::vector<std::uint8_t>(65)});
        run(3, {2, 33, 1, 1, true, true, std::vector<std::uint8_t>(66)});
        MacGrid isolated{65, 65, 1, 1, false, false, std::vector<std::uint8_t>(4225)};
        for (std::size_t i = 0; i < isolated.walls.size(); ++i) isolated.walls[i] = static_cast<std::uint8_t>((i % 65 + i / 65) % 2);
        run(4, std::move(isolated));
        run(5, {193, 173, 1, 1, true, true, std::vector<std::uint8_t>(33389)});
        owners(); flagged_sweep(); breakdown(); std::cout << "PASS coupled real block oracle, refresh, singleton storage and twelve workers\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
