#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_transport.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <numbers>
#include <sstream>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double eps = std::numeric_limits<double>::epsilon();
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void near(double a, double b, double scale, double tolerance = 128 * eps) {
    check(std::isfinite(a) && std::abs(a - b) <= tolerance * scale, "Independent MAC comparison failed");
}
const SpeciesRegistry& registry() { static const auto r = SpeciesRegistry::standard(); return r; }
MacTransportState initial(std::size_t w, std::size_t h, bool periodic, bool corner = false) {
    const auto& r = registry(); MacTransportState result;
    result.grid = {w, h, 1, 1, periodic, periodic && h > 1, std::vector<std::uint8_t>(w * h)};
    if (corner) result.grid.walls[1 + w] = 1;
    for (std::size_t i = 0; i < w * h; ++i) {
        MacMaterialCell cell{MixtureInventory(r.size()), {}};
        if (!result.grid.walls[i]) {
            const double mass = corner ? 1 + .125 * static_cast<double>(i) : 1;
            cell.mixture.masses[r.index("nitrogen")] = mass * (corner ? .95 : 1);
            if (corner) cell.mixture.masses[r.index("water")] = mass * .05;
            cell.mixture.masses[r.index("helium")] = mass * 1e-28; cell.mixture.set_temperature(r, 300);
            cell.passive_quantities = {{17, mass * (1 + .125 * static_cast<double>(i))}, {UINT64_C(0x80000000), mass * 1e-30}};
        }
        result.cells.push_back(std::move(cell));
    }
    const auto inertia = mac_face_inertias(r, result); result.momentum_x.resize(inertia.x.size()); result.momentum_y.resize(inertia.y.size());
    if (corner) {
        const auto& g = result.grid;
        for (std::size_t y = 0; y < h; ++y) for (std::size_t x = 1; x < w; ++x) {
            const auto f = g.x_face(x, y);
            if (!g.walls[x - 1 + y * w] && !g.walls[x + y * w]) result.momentum_x[f] = inertia.x[f] * .125 * std::sin(static_cast<double>(f) + .5);
        }
        for (std::size_t y = 1; y < h; ++y) for (std::size_t x = 0; x < w; ++x) {
            const auto f = g.y_face(x, y);
            if (!g.walls[x + (y - 1) * w] && !g.walls[x + y * w]) result.momentum_y[f] = inertia.y[f] * -.2 * std::cos(static_cast<double>(f) + .2);
        }
    }
    return result;
}
MacMassFluxes carrier(const MacTransportState& state, double dt) {
    const auto& g = state.grid; const auto inertia = mac_face_inertias(registry(), state);
    MacMassFluxes result{std::vector<double>(inertia.x.size()), std::vector<double>(inertia.y.size())};
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = g.periodic_x ? 0 : 1; x < g.width; ++x) {
        const auto l = (x + g.width - 1) % g.width + y * g.width, r = x + y * g.width, f = g.x_face(x, y);
        if (g.walls[l] || g.walls[r]) continue;
        const double u = state.momentum_x[f] / inertia.x[f];
        result.x[f] = dt * g.spacing_m * g.depth_m * u * state.cells[u >= 0 ? l : r].mixture.mass() / g.volume_m3();
    }
    for (std::size_t y = g.periodic_y ? 0 : 1; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto b = x + ((y + g.height - 1) % g.height) * g.width, t = x + y * g.width, f = g.y_face(x, y);
        if (g.walls[b] || g.walls[t]) continue;
        const double u = state.momentum_y[f] / inertia.y[f];
        result.y[f] = dt * g.spacing_m * g.depth_m * u * state.cells[u >= 0 ? b : t].mixture.mass() / g.volume_m3();
    }
    return result;
}
std::string fingerprint(const MacTransportState& state) {
    std::ostringstream text; text << std::hexfloat << state.grid.width << state.grid.height << state.grid.spacing_m << state.grid.depth_m <<
        state.grid.periodic_x << state.grid.periodic_y;
    for (auto wall : state.grid.walls) text << static_cast<int>(wall);
    for (const auto& cell : state.cells) {
        text << cell.mixture.energy; for (auto value : cell.mixture.masses) text << value;
        for (const auto& p : cell.passive_quantities) text << p.identity << p.quantity;
    }
    for (auto value : state.momentum_x) text << value;
    for (auto value : state.momentum_y) text << value;
    return text.str();
}
void exported(int id, const MacTransportState& before, const MacMassFluxes& flux, const MacTransportAdvance& after) {
    const auto& g = before.grid;
    std::cout << "CASE " << id << ' ' << g.width << ' ' << g.height << ' ' << g.periodic_x << ' ' << g.periodic_y << ' ' <<
        after.report.energy_residual_j << ' ' << after.report.energy_bound_j << '\n';
    for (std::size_t i = 0; i < g.walls.size(); ++i) std::cout << "WALL " << id << ' ' << i << ' ' << static_cast<int>(g.walls[i]) << '\n';
    for (int endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& s = endpoint == 0 ? before : after.state;
        for (std::size_t i = 0; i < s.cells.size(); ++i) {
            std::cout << "CELL " << id << ' ' << endpoint << ' ' << i << ' ' << s.cells[i].mixture.energy << '\n';
            for (std::size_t k = 0; k < registry().size(); ++k) if (s.cells[i].mixture.masses[k] != 0)
                std::cout << "MASS " << id << ' ' << endpoint << ' ' << i << ' ' << k << ' ' << s.cells[i].mixture.masses[k] << '\n';
            for (const auto& p : s.cells[i].passive_quantities) std::cout << "PASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto& r = axis == 0 ? after.report.x : after.report.y;
        const auto& old_j = axis == 0 ? before.momentum_x : before.momentum_y;
        const auto& new_j = axis == 0 ? after.state.momentum_x : after.state.momentum_y; const auto& f = axis == 0 ? flux.x : flux.y;
        for (std::size_t i = 0; i < old_j.size(); ++i) {
            std::cout << "FACE " << id << ' ' << axis << ' ' << i << ' ' << f[i] << ' ' << old_j[i] << ' ' << new_j[i];
            for (const auto value : {r.old_inertia[i], r.new_inertia[i], r.old_kinetic[i], r.new_kinetic[i], r.transported_kinetic[i],
                r.mixing_heat[i], r.wall_heat[i], r.wall_impulse[i], r.dual_mass_residual[i]}) std::cout << ' ' << value;
            std::cout << '\n';
        }
        for (const auto& edge : r.fluxes) std::cout << "DUAL " << id << ' ' << axis << ' ' << edge.left << ' ' << edge.right << ' ' << edge.mass_kg << '\n';
    }
}
void exact_mixing() {
    auto state = initial(2, 1, true); state.momentum_x = {1, -1};
    MacMassFluxes flux{{.125, .125}, std::vector<double>(4)}; const auto held = fingerprint(state);
    const auto result = advance_mac_transport(registry(), state, flux);
    check(result.state.momentum_x == std::vector<double>({.75, -.75}), "Exact two-stream momentum differs");
    check(result.report.mixing_heat_j == .4375 && result.report.wall_heat_j == 0, "Exact upwind variance heat differs");
    check(fingerprint(state) == held, "Transport changed its source"); exported(0, state, flux, result);
}
void corner() {
    const auto state = initial(4, 3, false, true); const auto flux = carrier(state, .125); const auto held = fingerprint(state);
    const auto result = advance_mac_transport(registry(), state, flux);
    check(result.report.wall_heat_j > 0 && result.report.mixing_heat_j > 0, "Solid-corner control lacks wall reaction or mixing");
    check(fingerprint(state) == held, "Solid-corner advance changed source"); exported(1, state, flux, result);
}
void uniform() {
    auto state = initial(4, 3, true); const auto& g = state.grid;
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const double factor = 1 + .25 * std::sin(static_cast<double>(i));
        for (auto& mass : state.cells[i].mixture.masses) mass *= factor;
        state.cells[i].mixture.energy *= factor;
        for (auto& p : state.cells[i].passive_quantities) p.quantity *= factor;
    }
    auto inertia = mac_face_inertias(registry(), state);
    for (std::size_t i = 0; i < inertia.x.size(); ++i) state.momentum_x[i] = .125 * inertia.x[i];
    for (std::size_t i = 0; i < inertia.y.size(); ++i) state.momentum_y[i] = -.25 * inertia.y[i];
    const auto held = fingerprint(state);
    const auto still = advance_mac_transport(registry(), state, {std::vector<double>(inertia.x.size()), std::vector<double>(inertia.y.size())});
    check(fingerprint(still.state) == held && fingerprint(state) == held, "Zero-flux transport changed stored state");
    const auto result = advance_mac_transport(registry(), state, carrier(state, .125)); inertia = mac_face_inertias(registry(), result.state);
    for (std::size_t i = 0; i < inertia.x.size(); ++i) near(result.state.momentum_x[i] / inertia.x[i], .125, .125);
    for (std::size_t i = 0; i < inertia.y.size(); ++i) near(result.state.momentum_y[i] / inertia.y[i], -.25, .25);
    check(result.state.grid.width == g.width && result.report.wall_heat_j == 0, "Periodic geometry or wall classification changed");
}
void rejected_controls() {
    const auto state = initial(2, 1, true); const auto held = fingerprint(state);
    for (const int control : {0, 1, 2, 3}) {
        auto bad = state; auto flux = carrier(state, .125);
        if (control == 0) flux.x = {2, 2};
        if (control == 1) flux.y.pop_back();
        if (control == 2) bad.momentum_y[0] = 1;
        if (control == 3) {
            std::fill(bad.cells[1].mixture.masses.begin(), bad.cells[1].mixture.masses.end(), 0);
            bad.cells[1].mixture.energy = 0; bad.cells[1].passive_quantities.clear();
            // A zero-flux empty cell is valid. It cannot export positive mass.
            flux.x[0] = .125;
        }
        const auto before = fingerprint(bad); bool caught = false;
        try { static_cast<void>(advance_mac_transport(registry(), bad, flux)); } catch (const std::exception&) { caught = true; }
        check(caught && fingerprint(bad) == before && fingerprint(state) == held, "Invalid/CFL transport changed caller state");
    }
}
struct Wave { MacTransportState state; double velocity_error = 0, tag_error = 0; };
Wave wave(std::size_t n, double courant) {
    auto state = initial(n, n, true); auto& g = state.grid; const double h = 1 / static_cast<double>(n); g.spacing_m = h;
    const double pi = std::numbers::pi, sinc = std::sin(pi * h) / (pi * h), final_time = .125;
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        auto& cell = state.cells[x + y * n]; const double sine = sinc * std::sin(2 * pi * (static_cast<double>(x) + .5) * h);
        for (auto& mass : cell.mixture.masses) mass *= h * h;
        cell.mixture.energy *= h * h;
        cell.passive_quantities = {{17, h * h * (1 + .2 * sine)}, {UINT64_C(0x80000000), h * h * 1e-30}};
    }
    const auto inertia = mac_face_inertias(registry(), state);
    for (std::size_t i = 0; i < state.momentum_x.size(); ++i) state.momentum_x[i] = .5 * inertia.x[i];
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x)
        state.momentum_y[g.y_face(x, y)] = inertia.y[g.y_face(x, y)] * .25 * sinc * std::sin(2 * pi * (static_cast<double>(x) + .5) * h);
    const double dt = courant * h / .75; double time = 0; std::size_t steps = 0;
    const auto limit = static_cast<std::size_t>(std::ceil(final_time / dt)) + 2;
    while (time < final_time) {
        check(steps++ < limit, "MAC wave step-count bound exceeded"); const double step = std::min(dt, final_time - time);
        auto candidate = advance_mac_transport(registry(), state, carrier(state, step)); state = std::move(candidate.state); time += step;
    }
    Wave result{std::move(state), 0, 0}; const auto next = mac_face_inertias(registry(), result.state);
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        const double sine = sinc * std::sin(2 * pi * ((static_cast<double>(x) + .5) * h - .5 * final_time));
        result.velocity_error += h * h * std::abs(result.state.momentum_y[result.state.grid.y_face(x, y)] / next.y[result.state.grid.y_face(x, y)] - .25 * sine);
        const auto& cell = result.state.cells[x + y * n]; result.tag_error += h * h * std::abs(cell.passive_quantities[0].quantity / cell.mixture.mass() - (1 + .2 * sine));
        if (y == 0) std::cout << "SAMPLE " << n << ' ' << courant << ' ' << x << ' ' <<
            result.state.momentum_y[result.state.grid.y_face(x, y)] / next.y[result.state.grid.y_face(x, y)] << ' ' <<
            cell.passive_quantities[0].quantity / cell.mixture.mass() << '\n';
    }
    std::cout << "WAVE " << n << ' ' << courant << ' ' << steps << ' ' << result.velocity_error << ' ' << result.tag_error << '\n'; return result;
}
Wave grid_refinement() {
    double velocity = 0, tag = 0; Wave baseline;
    for (const auto n : {std::size_t(16), std::size_t(32), std::size_t(64)}) {
        auto result = wave(n, .2);
        if (n == 16) baseline = result;
        if (velocity != 0) check(result.velocity_error < .7 * velocity && result.tag_error < .7 * tag, "MAC grid refinement fails declared .7 joint bound");
        velocity = result.velocity_error; tag = result.tag_error;
    }
    return baseline;
}
void time_refinement(const Wave& a) {
    const auto b = wave(16, .1), c = wave(16, .05);
    double first = 0, second = 0;
    for (std::size_t i = 0; i < a.state.momentum_y.size(); ++i) {
        first += std::abs(a.state.momentum_y[i] - b.state.momentum_y[i]); second += std::abs(b.state.momentum_y[i] - c.state.momentum_y[i]);
    }
    check(first > 0 && second < .7 * first, "MAC fixed-grid timestep differences fail declared .7 bound");
    std::cout << "TIMESTEP " << first << ' ' << second << '\n';
}
void transpose() {
    const auto source = initial(4, 3, false, true); const auto& g = source.grid; const auto flux = carrier(source, .125);
    const auto result = advance_mac_transport(registry(), source, flux);
    auto transposed = initial(3, 4, false); auto& t = transposed.grid;
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        transposed.cells[y + x * t.width] = source.cells[x + y * g.width]; t.walls[y + x * t.width] = g.walls[x + y * g.width];
    }
    MacMassFluxes mapped{std::vector<double>(t.x_width() * t.height), std::vector<double>(t.width * t.y_height())};
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) {
        transposed.momentum_y[t.y_face(y, x)] = source.momentum_x[g.x_face(x, y)]; mapped.y[t.y_face(y, x)] = flux.x[g.x_face(x, y)];
    }
    for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x) {
        transposed.momentum_x[t.x_face(y, x)] = source.momentum_y[g.y_face(x, y)]; mapped.x[t.x_face(y, x)] = flux.y[g.y_face(x, y)];
    }
    const auto other = advance_mac_transport(registry(), transposed, mapped);
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto& a = result.state.cells[x + y * g.width]; const auto& b = other.state.cells[y + x * t.width];
        near(a.mixture.energy, b.mixture.energy, std::abs(a.mixture.energy));
        for (std::size_t s = 0; s < registry().size(); ++s) near(a.mixture.masses[s], b.mixture.masses[s], a.mixture.masses[s]);
    }
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x)
        near(result.state.momentum_x[g.x_face(x, y)], other.state.momentum_y[t.y_face(y, x)], std::abs(result.state.momentum_x[g.x_face(x, y)]));
}
void parallel_validation_control() {
    const ma::testing::Setup s;
    const auto saved = mac_kernel_default_options(); auto options = saved; options.threads = 1;
    set_mac_kernel_default_options(options);
    const auto source = s.state(64, 35, false, true, true);
    const auto image = [&] { return MacClosedWorld(s.registry, s.eos, source).checkpoint(); };
    const auto held = image();
    const auto serial = mac_face_inertias(s.registry, source, *s.eos);
    const auto legacy = mac_face_inertias(s.registry, source);
    options.threads = 12; set_mac_kernel_default_options(options);
    const auto parallel = mac_face_inertias(s.registry, source, *s.eos);
    const auto other = mac_face_inertias(s.registry, source);
    check(serial.x == parallel.x && serial.y == parallel.y && legacy.x == other.x && legacy.y == other.y &&
        image() == held, "Parallel validation changed inertias or source bytes");
    auto invalid = source;
    invalid.cells[10].mixture.energy = std::numeric_limits<double>::quiet_NaN();
    invalid.cells[2050].passive_quantities[0].quantity = -1;
    const auto error = [&](unsigned threads) {
        options.threads = threads; set_mac_kernel_default_options(options);
        try { static_cast<void>(mac_face_inertias(s.registry, invalid, *s.eos)); }
        catch (const std::exception& e) { return std::string(e.what()); }
        return std::string{};
    };
    const auto first = error(1), many = error(12);
    check(!first.empty() && first == many, "Validation errors depend on worker scheduling");
    set_mac_kernel_default_options(saved);
    std::cout << "PASS parallel input validation: inertias, primary bytes and first error\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--parallel-validation-only") { parallel_validation_control(); return 0; }
        check(argc == 1, "Usage: ma_mac_transport_tests [--parallel-validation-only]");
        parallel_validation_control();
        std::cout << std::setprecision(17);
        exact_mixing(); std::cout << "PASS exact kinetic mixing and immutable source\n";
        corner(); std::cout << "PASS solid-corner dual mass, wall impulse and heat\n";
        uniform(); std::cout << "PASS periodic variable-density constant velocity\n";
        rejected_controls(); std::cout << "PASS CFL, shape, constrained momentum and empty-cell rollback\n";
        const auto baseline = grid_refinement(); std::cout << "PASS joint 2D shear and passive grid refinement\n";
        time_refinement(baseline); std::cout << "PASS fixed-grid timestep refinement\n";
        transpose(); std::cout << "PASS transposed solid-corner symmetry\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
