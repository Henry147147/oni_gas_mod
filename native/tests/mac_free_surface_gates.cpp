// Milestone B free-surface gates (docs/completion-plan.md, "Gravity and
// multiphase gates"): water under air, production kernel, sharp interfaces
// (material selector 3). Each gate prints GATE <id> PASS|FAIL with its
// measurements and exits nonzero on failure.
//   M1  still water stays at rest with a fixed surface.
//   M2  dam break front against Martin & Moyce (1952).
//   M3  standing-wave period and damping against linear theory.
#include "mac_kernel_fixture.hpp"
#include <cstring>
#include <functional>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double g = 9.81, temperature = 300, p_top = 101325;
bool verdict(const char* id, bool pass) { std::cout << "GATE " << id << (pass ? " PASS" : " FAIL") << '\n'; return pass; }

MacClosedSettings sharp() {
    MacClosedSettings settings; settings.gravity_m_per_s2 = g;
    settings.reconstruction = MacMaterialReconstruction::interface_plic;
    return settings;
}
// Liquid volume fraction of every cell from the EOS.
std::vector<double> fractions(const MacTransportState& s) {
    const auto& set = ma::testing::setup(); const double v = s.grid.volume_m3(); std::vector<double> alpha(s.cells.size());
    for (std::size_t i = 0; i < s.cells.size(); ++i) {
        const auto& m = s.cells[i].mixture;
        alpha[i] = m.masses[set.registry.index("water")] > 0 ? set.eos->evaluate(m, v).liquid_volume_m3 / v : 0;
    }
    return alpha;
}
long double water_mass(const MacTransportState& s) {
    const auto w = ma::testing::setup().registry.index("water"); long double m = 0;
    for (const auto& c : s.cells) m += c.mixture.masses[w];
    return m;
}
double max_speed(const MacTransportState& s) {
    const auto& set = ma::testing::setup(); const auto inertia = mac_face_inertias(set.registry, s, *set.eos); double speed = 0;
    for (std::size_t f = 0; f < inertia.x.size(); ++f) if (inertia.x[f] > 0) speed = std::max(speed, std::abs(s.momentum_x[f] / inertia.x[f]));
    for (std::size_t f = 0; f < inertia.y.size(); ++f) if (inertia.y[f] > 0) speed = std::max(speed, std::abs(s.momentum_y[f] / inertia.y[f]));
    return speed;
}

// Discrete hydrostatic water/air state. Each column is built from the top
// cell (air at p_top) downward so that every y face balances the solver's
// force law A (p_below - p_above) = g (m_below + m_above) / 2, solved for
// the cell's inventory scale against the EOS. fill(x, y) is the cell's
// liquid volume fraction.
MacTransportState hydrostatic(std::size_t w, std::size_t h, double spacing, bool periodic_x,
    const std::function<double(std::size_t, std::size_t)>& fill) {
    const auto& set = ma::testing::setup(); const double v = spacing * spacing;
    MacTransportState st; st.grid = {w, h, spacing, 1, periodic_x, false, std::vector<std::uint8_t>(w * h)};
    st.cells.resize(w * h, MacMaterialCell{MixtureInventory(set.registry.size()), {}});
    const auto scaled = [&](const MixtureInventory& base, double factor) {
        std::vector<double> masses(base.masses.size());
        for (std::size_t k = 0; k < masses.size(); ++k) masses[k] = base.masses[k] * factor;
        return set.eos->initialize_at_temperature(masses, temperature, v).mixture;
    };
    const auto pressure = [&](const MixtureInventory& m) { return set.eos->evaluate(m, v).mechanical_pressure_pa; };
    for (std::size_t x = 0; x < w; ++x) {
        double above_p = 0, above_m = 0;
        for (std::size_t yy = h; yy-- > 0;) {
            const double alpha = fill(x, yy);
            if (yy == h - 1) {
                st.cells[x + yy * w].mixture = set.mixture(v, p_top, temperature, alpha);
            } else {
                // Target: p(m) - g m / (2 spacing) = p_above + g m_above / (2 spacing).
                const double target = above_p + g * above_m / (2 * spacing);
                const auto base = set.mixture(v, target, temperature, alpha);
                const auto residual = [&](double factor) { const auto m = scaled(base, factor); return pressure(m) - g * m.mass() / (2 * spacing) - target; };
                double a = 1, fa = residual(a), b = 1 + 1e-6, fb = residual(b);
                for (int it = 0; it < 60 && fb != 0 && fb != fa; ++it) {
                    const double c = b - fb * (b - a) / (fb - fa); a = b; fa = fb; b = c; fb = residual(b);
                    if (std::abs(b - a) <= 1e-17 * std::abs(b)) break;
                }
                st.cells[x + yy * w].mixture = scaled(base, b);
            }
            above_p = pressure(st.cells[x + yy * w].mixture); above_m = st.cells[x + yy * w].mixture.mass();
        }
    }
    const auto faces = mac_face_inertias(set.registry, st, *set.eos);
    st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
    return st;
}
MacClosedWorld world_of(const MacTransportState& st) {
    const auto& set = ma::testing::setup(); return MacClosedWorld(set.registry, set.eos, st, sharp());
}

bool m1() {
    constexpr std::size_t n = 32; const double h = 1. / n;
    const auto st = hydrostatic(n, n, h, false, [&](std::size_t, std::size_t y) { return y < n / 2 ? 1. : 0.; });
    auto world = world_of(st); const auto initial = water_mass(st); const auto alpha0 = fractions(st);
    double speed = 0;
    for (int k = 0; k < 100; ++k) { world = world.advanced(.1, 4096, MacExecutionPolicy::production).world; speed = std::max(speed, max_speed(world.state())); }
    const auto alpha = fractions(world.state()); double drift = 0; long double above = 0;
    for (std::size_t x = 0; x < n; ++x) {
        double before = 0, after = 0;
        for (std::size_t y = 0; y < n; ++y) { before += alpha0[x + y * n] * h; after += alpha[x + y * n] * h; }
        drift = std::max(drift, std::abs(after - before));
    }
    const auto w = ma::testing::setup().registry.index("water");
    for (std::size_t i = (n / 2 + 1) * n; i < n * n; ++i) above += world.state().cells[i].mixture.masses[w];
    const double conservation = static_cast<double>(std::abs(water_mass(world.state()) - initial) / initial);
    std::cout << "M1 max_speed_m_s=" << speed << " surface_drift_m=" << drift << " water_above_surface_cell_kg=" << static_cast<double>(above)
              << " water_drift=" << conservation << '\n';
    return verdict("M1", speed <= 1e-8 && drift <= 1e-9 && above == 0 && conservation <= 1e-14);
}

// Martin & Moyce (1952), column of height 2a: T = t sqrt(2g/a), Z = x/a.
constexpr double mm_t[] = {.41, .84, 1.19, 1.43, 1.63, 1.82, 1.97, 2.2, 2.32, 2.5, 2.64, 2.82, 2.96};
constexpr double mm_z[] = {1.11, 1.23, 1.44, 1.67, 1.89, 2.11, 2.33, 2.56, 2.78, 3, 3.22, 3.44, 3.67};
// The experiment's front lags its release; it is compared with the simulation
// at T_data - 0.175 (Lethe's validation convention, user decision 2026-10-19).
constexpr double time_shift = .175;
struct DamBreak { std::vector<double> z; double band = 0, conservation = 0, mist = 0; };
DamBreak dam_break(std::size_t across) {
    const double a = .05715, h = a / static_cast<double>(across), unit = std::sqrt(2 * g / a);
    const std::size_t w = 5 * across, ht = 5 * across / 2;
    const auto st = hydrostatic(w, ht, h, false, [&](std::size_t x, std::size_t y) { return x < across && y < 2 * across ? 1. : 0.; });
    auto world = world_of(st); const auto initial = water_mass(st); DamBreak r; double t = 0;
    for (const double sample : mm_t) {
        const double target = std::max(0., sample - time_shift) / unit;
        while (t < target) { const double step = std::min(.005, target - t); world = world.advanced(step, 65536, MacExecutionPolicy::production).world; t += step; }
        const auto alpha = fractions(world.state()); double length = 0;
        for (std::size_t x = 0; x < w; ++x) length += alpha[x] * h;
        r.z.push_back(length / a);
        // Band width across the interface: through each partly filled cell
        // (1% to 99% full; emptier cells are mist, reported separately), the
        // shorter of its contiguous horizontal and vertical runs (a thin film
        // along the floor is one cell wide, however long).
        const auto partly = [&](std::size_t i) { return alpha[i] > 1e-2 && alpha[i] < 1 - 1e-2; };
        {
            const auto& set = ma::testing::setup(); const auto water = set.registry.index("water"); long double mist = 0;
            for (std::size_t i = 0; i < w * ht; ++i) if (alpha[i] < 1e-2) mist += world.state().cells[i].mixture.masses[water];
            r.mist = std::max(r.mist, static_cast<double>(mist / initial));
        }
        std::vector<std::size_t> run_x(w * ht), run_y(w * ht);
        for (std::size_t y = 0; y < ht; ++y) for (std::size_t x = 0; x < w;) {
            std::size_t end = x; while (end < w && partly(end + y * w)) ++end;
            for (std::size_t k = x; k < end; ++k) run_x[k + y * w] = end - x;
            x = end == x ? x + 1 : end;
        }
        for (std::size_t x = 0; x < w; ++x) for (std::size_t y = 0; y < ht;) {
            std::size_t end = y; while (end < ht && partly(x + end * w)) ++end;
            for (std::size_t k = y; k < end; ++k) run_y[x + k * w] = end - y;
            y = end == y ? y + 1 : end;
        }
        for (std::size_t i = 0; i < w * ht; ++i) if (partly(i)) r.band = std::max(r.band, static_cast<double>(std::min(run_x[i], run_y[i])));
    }
    r.conservation = static_cast<double>(std::abs(water_mass(world.state()) - initial) / initial);
    return r;
}
bool m2(bool quick) {
    std::vector<std::size_t> grids{16, 32, 64}; if (quick) grids = {16};
    std::vector<DamBreak> runs;
    for (const auto n : grids) {
        runs.push_back(dam_break(n)); const auto& r = runs.back();
        std::cout << "M2 cells_across=" << n << " band_cells=" << r.band << " max_mist=" << r.mist << " water_drift=" << r.conservation << " Z at T_data-" << time_shift << ':';
        for (std::size_t i = 0; i < r.z.size(); ++i) std::cout << ' ' << mm_t[i] - time_shift << '=' << r.z[i];
        std::cout << '\n';
    }
    const auto& fine = runs.back(); double data_error = 0, grid_error = 0, band = 0, conservation = 0;
    for (std::size_t i = 0; i < fine.z.size(); ++i) if (mm_t[i] - time_shift >= .5) {
        data_error = std::max(data_error, std::abs(fine.z[i] - mm_z[i]) / mm_z[i]);
        if (runs.size() > 1) grid_error = std::max(grid_error, std::abs(fine.z[i] - runs[runs.size() - 2].z[i]) / fine.z[i]);
    }
    for (const auto& r : runs) { band = std::max(band, r.band); conservation = std::max(conservation, r.conservation); }
    std::cout << "M2 data_error=" << data_error << " grid_error=" << grid_error << " band_cells=" << band << " water_drift=" << conservation << '\n';
    if (quick) return verdict("M2-quick", data_error <= .1 && band <= 3 && conservation <= 1e-14);
    return verdict("M2", data_error <= .1 && grid_error <= .03 && band <= 3 && conservation <= 1e-14);
}

struct Wave { double period = 0, change = 0; };
Wave standing_wave(std::size_t n) {
    const double h = 1. / static_cast<double>(n), depth = .5, amplitude = 1e-3, k = 2 * std::numbers::pi;
    const std::size_t rows = 3 * n / 4;
    const auto surface = [&](std::size_t x) { return depth + amplitude * std::cos(k * (static_cast<double>(x) + .5) * h); };
    const auto st = hydrostatic(n, rows, h, true, [&](std::size_t x, std::size_t y) {
        return std::clamp((surface(x) - static_cast<double>(y) * h) / h, 0., 1.); });
    auto world = world_of(st);
    const double omega = std::sqrt(g * k * std::tanh(k * depth)), period = 2 * std::numbers::pi / omega, dt = period / 200;
    std::vector<double> times, elevation;
    for (double t = 0; t < 3.25 * period; t += dt) {
        const auto& set = ma::testing::setup(); const auto water = set.registry.index("water"); double column = 0;
        for (std::size_t y = 0; y < rows; ++y) {
            const auto& m = world.state().cells[y * n].mixture;
            if (m.masses[water] > 0) column += set.eos->evaluate(m, h * h).liquid_volume_m3 / h;
        }
        times.push_back(t); elevation.push_back(column - depth);
        world = world.advanced(dt, 65536, MacExecutionPolicy::production).world;
    }
    // Period from downward zero crossings (linear interpolation); loss from
    // successive maxima (parabolic peaks).
    std::vector<double> crossings, peaks;
    for (std::size_t i = 1; i < times.size(); ++i) if (elevation[i - 1] > 0 && elevation[i] <= 0)
        crossings.push_back(times[i - 1] + dt * elevation[i - 1] / (elevation[i - 1] - elevation[i]));
    for (std::size_t i = 1; i + 1 < times.size(); ++i) if (elevation[i] > elevation[i - 1] && elevation[i] >= elevation[i + 1]) {
        const double a = elevation[i - 1], b = elevation[i], c = elevation[i + 1], d = a - 2 * b + c;
        peaks.push_back(d == 0 ? b : b - (a - c) * (a - c) / (8 * d));
    }
    Wave r;
    if (crossings.size() >= 2) r.period = (crossings.back() - crossings.front()) / static_cast<double>(crossings.size() - 1);
    // Relative amplitude change per period (negative: loss).
    if (peaks.size() >= 2) r.change = std::pow(peaks.back() / peaks.front(), 1 / static_cast<double>(peaks.size() - 1)) - 1;
    return r;
}
bool m3(bool quick) {
    const double k = 2 * std::numbers::pi, exact = 2 * std::numbers::pi / std::sqrt(g * k * std::tanh(k * .5));
    std::vector<std::size_t> grids{32, 64, 128}; if (quick) grids = {32};
    std::vector<double> errors; double change = 0;
    for (const auto n : grids) {
        const auto r = standing_wave(n); const double error = std::abs(r.period - exact) / exact;
        errors.push_back(error); change = r.change;
        std::cout << "M3 cells_per_wavelength=" << n << " period_s=" << r.period << " linear_theory_s=" << exact << " period_error=" << error
                  << " amplitude_change_per_period=" << r.change << '\n';
    }
    bool converging = true; for (std::size_t i = 1; i < errors.size(); ++i) converging = converging && errors[i] <= errors[i - 1];
    // Coarse regression (32 cells): period within 5%, amplitude change within
    // 5% per period (measured -3.6%).
    if (quick) return verdict("M3-quick", errors.back() <= .05 && std::abs(change) <= .05);
    // An inviscid wave neither loses more than 2% per period nor grows.
    return verdict("M3", errors.back() <= .01 && converging && change >= -.02 && change <= .02);
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(10);
        MacKernelOptions options; options.threads = 12; set_mac_kernel_default_options(options);
        bool quick = false; std::vector<std::string> ids;
        for (int a = 1; a < argc; ++a) { if (std::strcmp(argv[a], "--quick") == 0) quick = true; else ids.emplace_back(argv[a]); }
        const auto selected = [&](const char* id) { return ids.empty() || std::find(ids.begin(), ids.end(), id) != ids.end(); };
        bool ok = true;
        if (selected("M1")) ok = m1() && ok;
        if (selected("M2")) ok = m2(quick) && ok;
        if (selected("M3")) ok = m3(quick) && ok;
        return ok ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 2; }
}
