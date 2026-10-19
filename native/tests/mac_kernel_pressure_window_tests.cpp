// Component controls for the pressure window. Fixed masses and inertias;
// this is not a remap, full-fluid local-step or owner acceptance certificate.
#include "../src/mac_kernel_pressure_window.hpp"
#include "../src/mac_kernel_detail.hpp"
#include <iomanip>
#include <iostream>
#include <numbers>

using namespace ma::experimental;
using namespace ma::experimental::mac_kernel;
namespace {
void check(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
struct Fixture {
    MacGrid grid;
    Model model;
    Fields fields;
    std::vector<double> initial_pressure, inertia;
    Fixture(std::size_t width, std::size_t height, bool periodic, bool fast = false)
        : grid{width, height, 1, 1, periodic, periodic, std::vector<std::uint8_t>(width * height)} {
        Species gas; gas.gas = true; gas.molar_mass = reference_gas_constant; gas.cv = 2.5; gas.tmin = 0; gas.tmax = 1e10;
        model.species = {gas}; model.gases = {0}; model.registry_size = 1;
        const auto n = width * height; fields.m.resize(n); fields.U.resize(n); initial_pressure.resize(n);
        fields.jx.resize(grid.x_width() * height); fields.jy.resize(width * grid.y_height());
        for (std::size_t i = 0; i < n; ++i) {
            const auto x = i % width; fields.m[i] = fast && x >= 6 && x < 10 ? .01 : 1;
            initial_pressure[i] = 1 + 1e-4 * std::sin(2 * std::numbers::pi * (static_cast<double>(x) + .5) / static_cast<double>(width));
            fields.U[i] = 2.5 * initial_pressure[i];
        }
        const Grid g(grid); inertia.resize(g.NX + g.NY);
        for (std::size_t f = 0; f < g.NX; ++f) inertia[f] = .5 * ((g.xl[f] == none ? 0 : fields.m[g.xl[f]]) + (g.xr[f] == none ? 0 : fields.m[g.xr[f]]));
        for (std::size_t f = 0; f < g.NY; ++f) inertia[g.NX + f] = .5 * ((g.yl[f] == none ? 0 : fields.m[g.yl[f]]) + (g.yr[f] == none ? 0 : fields.m[g.yr[f]]));
    }
};
// Independent RK4 ODE for fixed-mass ideal gas and canonical face momenta.
// It uses absolute pressure and the continuous adiabatic law, with no moment,
// mortar, multigrid or discrete-work helper from the implementation.
std::vector<double> reference(const Fixture& f, double seconds, std::size_t count) {
    const Grid g(f.grid); const auto n = g.N; std::vector<double> state(n + f.inertia.size());
    std::fill(state.begin(), state.begin() + static_cast<std::ptrdiff_t>(n), 1);
    const auto rhs = [&](const std::vector<double>& x) {
        std::vector<double> out(x.size()), pressure(n);
        for (std::size_t i = 0; i < n; ++i) pressure[i] = f.initial_pressure[i] * std::pow(x[i], -1.4);
        const auto face = [&](Index l, Index r, std::size_t j) {
            if (l == none || r == none) return;
            const double q = x[n + j] / f.inertia[j]; out[l] += q; out[r] -= q; out[n + j] = pressure[l] - pressure[r];
        };
        for (std::size_t j = 0; j < g.NX; ++j) face(g.xl[j], g.xr[j], j);
        for (std::size_t j = 0; j < g.NY; ++j) face(g.yl[j], g.yr[j], g.NX + j);
        return out;
    };
    const double h = seconds / static_cast<double>(count);
    for (std::size_t step = 0; step < count; ++step) {
        const auto a = rhs(state); auto x = state;
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = state[i] + .5 * h * a[i];
        const auto b = rhs(x);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = state[i] + .5 * h * b[i];
        const auto c = rhs(x);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = state[i] + h * c[i];
        const auto d = rhs(x);
        for (std::size_t i = 0; i < x.size(); ++i) state[i] += (h / 6) * (a[i] + 2 * b[i] + 2 * c[i] + d[i]);
    }
    return state;
}
void run(unsigned id, bool fast, unsigned level, unsigned mask_kind) {
    Fixture f(16, 2, true, fast); const auto held = f.fields;
    std::vector<std::uint8_t> mask(f.grid.walls.size());
    for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = static_cast<std::uint8_t>(mask_kind == 2 || (mask_kind == 1 && i % 16 >= 6 && i % 16 < 10));
    PressureTimeOptions options{.1, 0, .45, level, 1024}; Pool one(1), twelve(12); PressureTimeWorkspace warm, cold;
    const auto result = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, one, warm);
    const auto parallel = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, twelve, cold);
    check(result.energy == parallel.energy && result.volume == parallel.volume && result.momentum_x == parallel.momentum_x &&
        result.momentum_y == parallel.momentum_y && result.qx == parallel.qx && result.qy == parallel.qy &&
        result.report.coarse_newton == parallel.report.coarse_newton && result.report.fine_newton == parallel.report.fine_newton &&
        result.report.outer_linear == parallel.report.outer_linear && result.report.fine_linear == parallel.report.fine_linear,
        "Pressure window changed with worker count");
    const auto repeat = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, one, warm);
    check(repeat.energy == result.energy && repeat.volume == result.volume && repeat.momentum_x == result.momentum_x &&
        repeat.report.cycles == result.report.cycles, "Warm pressure window retained trial state");
    const auto r1 = reference(f, options.seconds, 1000), r2 = reference(f, options.seconds, 2000); const auto n = mask.size();
    double pressure_error = 0, temperature_error = 0, resolution = 0; MacEnergyAccount account;
    for (std::size_t i = 0; i < n; ++i) {
        const double p = .4 * result.energy[i] / result.volume[i], p1 = f.initial_pressure[i] * std::pow(r1[i], -1.4), p2 = f.initial_pressure[i] * std::pow(r2[i], -1.4);
        pressure_error = std::max(pressure_error, std::abs(p - p2) / 1e-4); resolution = std::max(resolution, std::abs(p1 - p2) / 1e-4);
        const double u2 = f.fields.U[i] * std::pow(r2[i], -.4);
        temperature_error = std::max(temperature_error, std::abs(result.energy[i] - u2) / (f.fields.m[i] * 2.5));
        account.add(result.energy[i]); account.add(-f.fields.U[i]);
        std::cout << "CELL " << id << ' ' << i << ' ' << f.fields.m[i] << ' ' << f.fields.U[i] << ' ' << result.energy[i] << ' ' << result.volume[i] << ' ' << r1[i] << ' ' << r2[i] << '\n';
    }
    auto momentum = result.momentum_x; momentum.insert(momentum.end(), result.momentum_y.begin(), result.momentum_y.end());
    for (std::size_t j = 0; j < momentum.size(); ++j) {
        account.add(MacEnergy::kinetic(f.inertia[j], momentum[j]));
        const auto q = j < result.qx.size() ? result.qx[j] : result.qy[j - result.qx.size()];
        std::cout << "FACE " << id << ' ' << j << ' ' << f.inertia[j] << ' ' << momentum[j] << ' ' << q << '\n';
    }
    check(account.within_bound(), "Pressure window failed original 128-epsilon energy account");
    check(pressure_error < .01 && temperature_error < .1 && resolution < .001, "Pressure window disagrees with resolved ODE");
    check(f.fields.U == held.U && f.fields.m == held.m && f.fields.jx == held.jx && f.fields.jy == held.jy, "Pressure window changed source fields");
    const auto& r = result.report;
    check(r.cell_intervals == r.coarse_cells + (r.fine_cells << level), "Pressure window miscounted local work");
    std::cout << "WINDOW " << id << ' ' << level << ' ' << r.coarse_cells << ' ' << r.fine_cells << ' ' << r.cell_intervals << ' '
        << r.coarse_newton << ' ' << r.fine_newton << ' ' << r.outer_linear << ' ' << r.fine_linear << ' ' << r.cycles << ' '
        << pressure_error << ' ' << temperature_error << ' ' << resolution << ' ' << account.residual().project().value << ' '
        << account.bound().project().value << ' ' << r.maximum_closure << ' ' << warm.storage_bytes() << '\n';
}
void inverse_controls() {
    MacGrid graph{3, 1, 1, 1, false, false, {0, 1, 0}};
    PressureMomentMultigrid mg(std::make_shared<const interval_detail::PressureMultigridTopology>(graph)); Pool pool(12);
    std::vector<double> comp{1, 0, 2}, wx(4), wy(6), ra{.3, 0, -.2}, rb{.5, 0, .1}, a(3, 17), b(3, 19);
    check(!mg.precondition_pair(ra, rb, a, b, false) && a == std::vector<double>(3, 17) && b == std::vector<double>(3, 19),
        "Unprepared inverse published output");
    check(mg.prepare(comp, comp, wx, wy) && mg.precondition_pair(ra, rb, a, b, false) && a[1] == 17 && b[1] == 19,
        "Local inverse cleared inactive entries");
    const auto saved_a = a, saved_b = b;
    mg.set_executor([](const void* ctx, std::size_t blocks, interval_detail::BlockFunction fn, const void* job) {
        static_cast<Pool*>(const_cast<void*>(ctx))->run(blocks, [&](std::size_t block) { fn(job, block); });
    }, &pool);
    check(mg.precondition_pair(ra, rb, a, b, false) && a == saved_a && b == saved_b, "Fixed inverse depends on worker count");
    check(mg.precondition_pair(ra, rb, a, b) && a[1] == 0 && b[1] == 0, "Default inverse did not clear inactive entries");
    a[1] = 17; b[1] = 19;
    check(mg.solve(ra, rb, a, b, 1e-12, 0, 512, false).converged && a[1] == 17 && b[1] == 19, "Local solve cleared inactive entries");
    const auto before_a = a, before_b = b; ra[0] = std::numeric_limits<double>::quiet_NaN();
    check(!mg.precondition_pair(ra, rb, a, b, false) && a == before_a && b == before_b, "Nonfinite inverse published output");
    std::cout << "INVERSE 7\n";
}
void admission() {
    Fixture f(16, 2, true); const auto held = f.fields; std::vector<std::uint8_t> mask(32, 1);
    PressureTimeOptions options{.1, 0, .45, 3, 256}; Pool pool(1); PressureTimeWorkspace workspace; unsigned rejected = 0;
    const auto attempt = [&](const Fields& fields, const std::vector<std::uint8_t>& cells, const PressureTimeOptions& opt) {
        try { static_cast<void>(advance_pressure_time_window(f.model, fields, f.grid, cells, opt, pool, workspace)); }
        catch (const std::invalid_argument&) { ++rejected; return; }
        throw std::runtime_error("Malformed pressure window admitted");
    };
    auto bad_options = options; bad_options.cell_interval_budget = 255; attempt(f.fields, mask, bad_options);
    bad_options = options; bad_options.fine_level = 21; attempt(f.fields, mask, bad_options);
    bad_options = options; bad_options.seconds = 0; attempt(f.fields, mask, bad_options);
    bad_options = options; bad_options.gravity = std::numeric_limits<double>::quiet_NaN(); attempt(f.fields, mask, bad_options);
    bad_options = options; bad_options.gravity = .1; attempt(f.fields, mask, bad_options);
    bad_options = options; bad_options.maximum_outgoing_fraction = .5; attempt(f.fields, mask, bad_options);
    auto cells = mask; cells[0] = 2; attempt(f.fields, cells, options); cells.pop_back(); attempt(f.fields, cells, options);
    auto bad = f.fields; bad.U.pop_back(); attempt(bad, mask, options);
    bad = f.fields; bad.m[0] = -1; attempt(bad, mask, options);
    bad = f.fields; bad.jx[0] = std::numeric_limits<double>::infinity(); attempt(bad, mask, options);
    const auto cold = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, pool, workspace);
    bad_options = options; bad_options.cell_interval_budget = 1; attempt(f.fields, mask, bad_options);
    const auto after = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, pool, workspace);
    check(after.energy == cold.energy && after.volume == cold.volume && after.momentum_x == cold.momentum_x && f.fields.U == held.U && f.fields.jx == held.jx,
        "Rejected pressure window changed source or continuation");
    std::cout << "ADMISSION " << rejected << '\n';
}
void large() {
    Fixture f(193, 173, true); std::vector<std::uint8_t> mask(f.grid.walls.size());
    for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = static_cast<std::uint8_t>(i % 193 >= 64 && i % 193 < 128);
    PressureTimeOptions options{.1, 0, .45, 1, 100000}; Pool one(1), twelve(12); PressureTimeWorkspace warm, cold;
    const auto serial = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, one, warm);
    const auto parallel = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, twelve, cold);
    check(serial.energy == parallel.energy && serial.volume == parallel.volume && serial.momentum_x == parallel.momentum_x && serial.momentum_y == parallel.momentum_y &&
        serial.report.coarse_newton == parallel.report.coarse_newton && serial.report.fine_newton == parallel.report.fine_newton &&
        serial.report.outer_linear == parallel.report.outer_linear && serial.report.fine_linear == parallel.report.fine_linear && serial.report.cycles == parallel.report.cycles,
        "Large pressure window depends on worker count");
    std::cout << "THREAD " << mask.size() << ' ' << serial.report.cell_intervals << ' ' << serial.report.coarse_newton << ' '
        << serial.report.fine_newton << ' ' << serial.report.outer_linear << ' ' << serial.report.fine_linear << ' ' << warm.storage_bytes() << '\n';
}
void wall_body_vacuum() {
    Fixture f(13, 9, false); const auto n = f.grid.walls.size();
    for (std::size_t y = 3; y < 8; ++y) { const auto i = 6 + 13*y; f.grid.walls[i] = 1; f.fields.m[i] = f.fields.U[i] = 0; }
    f.fields.m[14] = f.fields.U[14] = 0;
    const Grid grid(f.grid); std::vector<std::uint8_t> mask(n);
    for (std::size_t i = 0; i < n; ++i) mask[i] = static_cast<std::uint8_t>(i % 13 < 4);
    PressureTimeOptions options{.02, .1, .45, 2, 1024}; Pool pool(12); PressureTimeWorkspace workspace;
    const auto result = advance_pressure_time_window(f.model, f.fields, f.grid, mask, options, pool, workspace);
    MacEnergyAccount energy, x, y;
    for (std::size_t i = 0; i < n; ++i) { energy.add(result.energy[i]); energy.add(-f.fields.U[i]); }
    for (std::size_t j = 0; j < result.momentum_x.size(); ++j) {
        const double inertia = .5*((grid.xl[j] == none ? 0 : f.fields.m[grid.xl[j]])+(grid.xr[j] == none ? 0 : f.fields.m[grid.xr[j]]));
        if (inertia != 0) energy.add(MacEnergy::kinetic(inertia,result.momentum_x[j]));
        x.add(result.momentum_x[j]);
    }
    for (std::size_t j = 0; j < result.momentum_y.size(); ++j) {
        const double inertia = .5*((grid.yl[j] == none ? 0 : f.fields.m[grid.yl[j]])+(grid.yr[j] == none ? 0 : f.fields.m[grid.yr[j]]));
        if (inertia != 0) energy.add(MacEnergy::kinetic(inertia,result.momentum_y[j]));
        energy.add(-result.body_work_y[j]); y.add(result.momentum_y[j]);
    }
    x.add(result.wall_x.negated()); y.add(result.wall_y.negated()); y.add(result.body_y.negated());
    check(energy.within_bound() && x.within_bound() && y.within_bound(), "Wall/vacuum/gravity pressure-window account failed");
    std::cout << "BALANCE " << n << ' ' << energy.residual().project().value << ' ' << energy.bound().project().value << ' '
        << x.residual().project().value << ' ' << x.bound().project().value << ' ' << y.residual().project().value << ' ' << y.bound().project().value << '\n';
    // An otherwise valid root must fail after work if the macro remap would
    // exhaust donor volume. The same source and cache must remain reusable.
    const auto held = f.fields; auto violent = f.fields;
    for (std::size_t j = 0; j < violent.jx.size(); ++j) if (grid.xl[j] != none && grid.xr[j] != none) violent.jx[j] = 100;
    bool rejected = false;
    try { static_cast<void>(advance_pressure_time_window(f.model,violent,f.grid,mask,options,pool,workspace)); }
    catch (const std::domain_error&) { rejected = true; }
    const auto repeat = advance_pressure_time_window(f.model,f.fields,f.grid,mask,options,pool,workspace);
    check(rejected && held.U == f.fields.U && held.jx == f.fields.jx && repeat.energy == result.energy && repeat.momentum_x == result.momentum_x,
        "Mid-window rejection changed source or cached continuation");
    std::cout << "ROLLBACK 1\n";
}
void caloric_boundary() {
    Fixture f(16, 2, true); std::fill(f.fields.U.begin(), f.fields.U.end(), 2.5);
    for (std::size_t j = 0; j < f.fields.jx.size(); ++j) f.fields.jx[j] = .01 * std::sin(2 * std::numbers::pi * static_cast<double>(j % 16) / 16);
    f.model.species[0].tmax = 1.00001;
    PressureTimeOptions options{.1,0,.45,0,32}; Pool pool(1); PressureTimeWorkspace workspace; std::vector<std::uint8_t> mask(32);
    const auto held = f.fields; bool rejected = false;
    try { static_cast<void>(advance_pressure_time_window(f.model,f.fields,f.grid,mask,options,pool,workspace)); }
    catch (const EosRefinement&) { rejected = true; }
    check(rejected && f.fields.U == held.U && f.fields.jx == held.jx, "Out-of-domain caloric endpoint published");
    f.model.species[0].tmax = 1e10;
    const auto after = advance_pressure_time_window(f.model,f.fields,f.grid,mask,options,pool,workspace);
    PressureTimeWorkspace cold;
    const auto expected = advance_pressure_time_window(f.model,f.fields,f.grid,mask,options,pool,cold);
    check(after.energy == expected.energy && after.volume == expected.volume && after.momentum_x == expected.momentum_x,
        "Caloric rejection leaked cached trial state");
    std::cout << "CALORIC 1\n";
}
}
int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17);
        run(0, false, 0, 0); run(1, false, 2, 1); run(2, true, 3, 1); run(3, true, 3, 2);
        inverse_controls(); admission(); large(); wall_body_vacuum(); caloric_boundary();
        std::cout << "PASS pressure-only nonlinear windows, shared faces, energy and resolved references\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
