// Milestone B multiphase gates (docs/completion-plan.md, "Gravity and
// multiphase gates"). Each gate prints GATE <id> PASS|FAIL|OPEN with its
// measurements; FAIL and OPEN exit nonzero. Both run the production kernel.
//   M4  two immiscible liquids (Rayleigh-Taylor), with sharp interfaces: a
//       cell holding both liquids places the denser below a PLIC line.
//   M5  Stefan vapour film: pressure-dependent boiling at a heated wall, with
//       sharp-interface boiling in the production kernel.
#include "mac_kernel_fixture.hpp"
#include "mixed_atmospheres/mac_native_heat.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <numbers>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double g = 9.81, temperature = 300;
int verdict(const char* id, const char* state) { std::cout << "GATE " << id << ' ' << state << '\n'; return std::strcmp(state, "PASS") == 0 ? 0 : 1; }

// Two constant-property liquids, 1000 and 800 kg/m3, under no gas.
struct TwoLiquids {
    SpeciesRegistry registry; std::shared_ptr<const FluidThermodynamics> eos;
    TwoLiquids() {
        const auto standard = SpeciesRegistry::standard(); std::vector<ReferenceMaterial> materials;
        for (const auto& [id, density] : {std::pair<const char*, double>{"water", 1000}, {"liquid_nitrogen", 800}}) {
            const auto& sp = standard[standard.index(id)]; registry.add(sp); ReferenceMaterial m;
            m.id = id; m.phase = ReferencePhase::elastic_liquid; m.molar_mass_kg_per_mol = sp.molar_mass; m.cv_j_per_kg_k = sp.cv();
            m.energy_offset_j_per_kg = sp.internal_energy(0); m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e6;
            m.zero_pressure_density_kg_per_m3 = density; m.reference_bulk_modulus_pa = 2.2e9; m.maximum_density_ratio = 2;
            m.provenance = "Rayleigh-Taylor gate surrogate liquid"; materials.push_back(m);
        }
        eos = std::make_shared<const FluidThermodynamics>(registry, "multiphase-gates-1", std::move(materials));
    }
};
// Liquid columns in discrete hydrostatic balance, built downward from p_top
// with the solver's face force law. heavy(x, y) is the cell's heavy volume fraction.
MacTransportState column(const TwoLiquids& s, std::size_t w, std::size_t h, double spacing, const std::function<double(std::size_t, std::size_t)>& heavy) {
    const double v = spacing * spacing, p_top = 101325;
    MacTransportState st; st.grid = {w, h, spacing, 1, true, false, std::vector<std::uint8_t>(w * h)};
    st.cells.resize(w * h, MacMaterialCell{MixtureInventory(s.registry.size()), {}});
    const auto cell = [&](double fraction, double p) {
        const std::vector<double> masses{fraction * v * 1000 * (1 + p / 2.2e9), (1 - fraction) * v * 800 * (1 + p / 2.2e9)};
        return s.eos->initialize_at_temperature(masses, temperature, v).mixture;
    };
    const auto pressure = [&](const MixtureInventory& m) { return s.eos->evaluate(m, v).mechanical_pressure_pa; };
    for (std::size_t x = 0; x < w; ++x) {
        double above_p = p_top, above_m = 0;
        for (std::size_t y = h; y-- > 0;) {
            const double target = y == h - 1 ? p_top : above_p + g * above_m / (2 * spacing);
            const double fraction = heavy(x, y);
            // p(m) - g m / (2 spacing) = target (the top cell: p = p_top).
            const auto residual = [&](double p) { const auto m = cell(fraction, p); return pressure(m) - (y == h - 1 ? 0 : g * m.mass() / (2 * spacing)) - target; };
            double a = target, fa = residual(a), b = target * (1 + 1e-9) + 1, fb = residual(b);
            for (int it = 0; it < 60 && fb != 0 && fb != fa; ++it) { const double c = b - fb * (b - a) / (fb - fa); a = b; fa = fb; b = c; fb = residual(b); }
            st.cells[x + y * w].mixture = cell(fraction, b);
            above_p = pressure(st.cells[x + y * w].mixture); above_m = st.cells[x + y * w].mixture.mass();
        }
    }
    const auto faces = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
    return st;
}
// Separation of the two liquids: the widest band of cells per column holding
// 1-99% heavy liquid by nominal volume, and the mist (each liquid's share in
// cells where it is under 1%) relative to its total.
struct Separation { std::size_t band = 0; double mist = 0; };
Separation separation(const MacTransportState& st) {
    const auto w = st.grid.width, h = st.grid.height; Separation r; double heavy_total = 0, light_total = 0, heavy_mist = 0, light_mist = 0;
    for (std::size_t x = 0; x < w; ++x) {
        std::size_t band = 0;
        for (std::size_t y = 0; y < h; ++y) {
            const auto& m = st.cells[x + y * w].mixture; const double a = m.masses[0] / 1000, b = m.masses[1] / 800;
            if (a + b == 0) continue;
            const double fraction = a / (a + b);
            if (fraction >= .01 && fraction <= .99) ++band;
            heavy_total += m.masses[0]; light_total += m.masses[1];
            if (fraction < .01) heavy_mist += m.masses[0];
            if (fraction > .99) light_mist += m.masses[1];
        }
        r.band = std::max(r.band, band);
    }
    r.mist = std::max(heavy_mist / heavy_total, light_mist / light_total); return r;
}
// Amplitude of the cos(kx) mode of the interface height (heavy volume per
// column, from nominal volumes: no EOS evaluation, so trace amounts below
// binary64 volume range do not stop the measurement).
double amplitude(const MacTransportState& st, double spacing, bool heavy_on_top) {
    const auto w = st.grid.width, h = st.grid.height;
    double mode = 0;
    for (std::size_t x = 0; x < w; ++x) {
        double heavy = 0;
        for (std::size_t y = 0; y < h; ++y) {
            const auto& m = st.cells[x + y * w].mixture; const double a = m.masses[0] / 1000, b = m.masses[1] / 800;
            if (a + b > 0) heavy += a / (a + b) * spacing;
        }
        // Interface height: depth of light liquid below a heavy top layer, or heavy depth.
        const double height = heavy_on_top ? static_cast<double>(h) * spacing - heavy : heavy;
        mode += height * std::cos(2 * std::numbers::pi * (static_cast<double>(x) + .5) / static_cast<double>(w));
    }
    return 2 * mode / static_cast<double>(w);
}
int m4() {
    const TwoLiquids s; constexpr std::size_t n = 64; const double width = .1, spacing = width / n, k = 2 * std::numbers::pi / width;
    const double atwood = (1000. - 800.) / (1000. + 800.), sigma = std::sqrt(atwood * g * k), perturbation = 1e-4 * width;
    MacClosedSettings settings; settings.gravity_m_per_s2 = g;
    settings.reconstruction = MacMaterialReconstruction::interface_plic;
    // Unstable: heavy over light, interface at mid-height plus eps cos(kx).
    const std::size_t rows = 2 * n;
    const auto interface = [&](std::size_t x) { return .5 * static_cast<double>(rows) * spacing + perturbation * std::cos(k * (static_cast<double>(x) + .5) * spacing); };
    const auto unstable = column(s, n, rows, spacing, [&](std::size_t x, std::size_t y) {
        return std::clamp((static_cast<double>(y + 1) * spacing - interface(x)) / spacing, 0., 1.); });
    MacClosedWorld world(s.registry, s.eos, unstable, settings);
    std::vector<double> times, amplitudes; double t = 0; const double dt = .02;
    const double initial = amplitude(unstable, spacing, true);
    std::cout << "M4 sigma_theory_1_s=" << sigma << " initial_amplitude_m=" << initial << '\n';
    while (t < 2 && (amplitudes.empty() || std::abs(amplitudes.back()) < .05 * width)) {
        world = world.advanced(dt, 65536, MacExecutionPolicy::production).world; t += dt;
        times.push_back(t); amplitudes.push_back(amplitude(world.state(), spacing, true));
    }
    // Growth rate: log-linear fit over the samples between 3x and 0.05 width.
    double sx = 0, sy = 0, sxx = 0, sxy = 0; std::size_t used = 0;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const double a = std::abs(amplitudes[i]);
        if (a < 3 * std::abs(initial) || a > .05 * width) continue;
        const double y = std::log(a); sx += times[i]; sy += y; sxx += times[i] * times[i]; sxy += times[i] * y; ++used;
    }
    const double rate = used >= 3 ? (static_cast<double>(used) * sxy - sx * sy) / (static_cast<double>(used) * sxx - sx * sx) : 0;
    const auto mixed = separation(world.state());
    std::cout << "M4 unstable samples=" << used << " growth_rate_1_s=" << rate << " relative_error=" << std::abs(rate - sigma) / sigma
              << " final_t_s=" << t << " final_amplitude_m=" << (amplitudes.empty() ? 0 : amplitudes.back())
              << " band_cells=" << mixed.band << " mist=" << mixed.mist << '\n';
    // Stable: light over a flat heavy layer (on a face) stays at rest.
    const auto stable = column(s, n, rows, spacing, [&](std::size_t, std::size_t y) { return y < rows / 2 ? 1. : 0.; });
    MacClosedWorld rest(s.registry, s.eos, stable, settings); double speed = 0;
    for (int i = 0; i < 25; ++i) {
        rest = rest.advanced(.04, 65536, MacExecutionPolicy::production).world;
        // Face inertia (m_left + m_right) / 2 straight from the masses.
        const auto& st = rest.state(); const auto& grid = st.grid;
        const auto mass = [&](std::size_t x, std::size_t y) { return st.cells[x + y * grid.width].mixture.mass(); };
        for (std::size_t y = 0; y < grid.height; ++y) for (std::size_t x = 0; x < grid.width; ++x) {
            const double ix = .5 * (mass(x == 0 ? grid.width - 1 : x - 1, y) + mass(x, y));
            speed = std::max(speed, std::abs(st.momentum_x[grid.x_face(x, y)] / ix));
            if (y > 0) speed = std::max(speed, std::abs(st.momentum_y[grid.y_face(x, y)] / (.5 * (mass(x, y - 1) + mass(x, y)))));
        }
    }
    std::cout << "M4 stable max_speed_m_s=" << speed << '\n';
    return verdict("M4", used >= 3 && std::abs(rate - sigma) <= .1 * sigma && speed <= 1e-8 ? "PASS" : "FAIL");
}
// Water and steam calibrated to boil at 373.15 K and 1 atm; a heater liquid
// (the liquid-oxygen slot, given a huge heat capacity and metal-like
// conductivity) that holds the wall temperature; a non-volatile liquid plug
// (the liquid-nitrogen slot) and a nitrogen reservoir held at 1 atm without
// evaporating water into it.
struct Boiling {
    SpeciesRegistry registry; std::shared_ptr<const FluidThermodynamics> eos;
    std::size_t water = 0, steam = 0, air = 0, plug = 0, heater = 0;
    Boiling() {
        const auto standard = SpeciesRegistry::standard(); std::vector<ReferenceMaterial> materials;
        for (const char* id : {"water", "steam", "nitrogen", "liquid_nitrogen", "liquid_oxygen"}) {
            auto sp = standard[standard.index(id)];
            if (std::string(id) == "liquid_oxygen") { sp.cp = 1e9; sp.conductivity = 1000; }
            registry.add(sp); ReferenceMaterial m;
            m.id = id; m.phase = sp.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            m.molar_mass_kg_per_mol = sp.molar_mass; m.cv_j_per_kg_k = sp.cv(); m.energy_offset_j_per_kg = sp.internal_energy(0);
            m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e6;
            m.zero_pressure_density_kg_per_m3 = sp.phase == Phase::gas ? 0 : sp.density;
            m.reference_bulk_modulus_pa = sp.phase == Phase::gas ? 0 : 2.2e9; m.maximum_density_ratio = sp.phase == Phase::gas ? 1 : 2;
            m.provenance = "Stefan gate profile"; materials.push_back(m);
        }
        materials[1].entropy_offset_j_per_kg_k = mac_saturation_entropy_offset(materials[0], materials[1], 373.15, 101325);
        eos = std::make_shared<const FluidThermodynamics>(registry, "stefan-gate-1", std::move(materials));
        water = registry.index("water"); steam = registry.index("steam"); air = registry.index("nitrogen");
        plug = registry.index("liquid_nitrogen"); heater = registry.index("liquid_oxygen");
    }
};
// Stefan vapour film: a wall at T_sat + 10 K (the face beside the heater
// cell) under a steam film, water at T_sat beyond, then the plug and the
// reservoir. Starting from the exact solution at t0 (temperatures, vapour at
// rest, liquid moving away at (1 - rho_v/rho_l) d delta/dt), the film
// thickness (steam volume per unit area) must follow 2 lambda sqrt(alpha t)
// over [t0, 10 t0]. Returns the worst relative error; film_cells resolves
// delta(t0).
double stefan(std::size_t film_cells, double t0, double& mass_drift) {
    const Boiling s; constexpr double t_sat = 373.15, dT = 10, p0 = 101325;
    const auto& w = s.registry[s.water]; const auto& v = s.registry[s.steam];
    const double latent = v.internal_energy(t_sat) + gas_constant * t_sat / v.molar_mass - w.internal_energy(t_sat) - p0 / w.density;
    const double rho_v = p0 * v.molar_mass / (gas_constant * t_sat), alpha = v.conductivity / (rho_v * v.cp), stefan_number = v.cp * dT / latent;
    double lo = 0, hi = 2;
    for (int i = 0; i < 200; ++i) { const double l = .5 * (lo + hi); (l * std::exp(l * l) * std::erf(l) < stefan_number / std::sqrt(std::numbers::pi) ? lo : hi) = l; }
    const double lambda = .5 * (lo + hi);
    const auto delta = [&](double t) { return 2 * lambda * std::sqrt(alpha * t); };
    const double d0 = delta(t0), d1 = delta(10 * t0), h = d0 / (static_cast<double>(film_cells) + .5);
    // The reservoir is 100 times the film's growth: the pressure rises ~1%.
    const auto water_end = 1 + static_cast<std::size_t>(std::ceil((2 * d1 + 4 * d0) / h));
    const auto plug_end = water_end + static_cast<std::size_t>(std::ceil(2 * d0 / h));
    const auto cells = plug_end + static_cast<std::size_t>(std::ceil(100 * (d1 - d0) / h));
    const double volume = h;  // spacing h, depth 1/h: unit face area
    MacTransportState st; st.grid = {cells, 1, h, 1 / h, false, false, std::vector<std::uint8_t>(cells)};
    const auto exact_temperature = [&](double x, double t) { return t_sat + dT - dT * std::erf(x / (2 * std::sqrt(alpha * t))) / std::erf(lambda); };
    const auto liquid = [&](double density) { return volume * density * (1 + p0 / 2.2e9); };
    for (std::size_t i = 0; i < cells; ++i) {
        std::vector<double> m(s.registry.size()); double cell_temperature = t_sat;
        const double a = static_cast<double>(i - 1) * h, b = a + h;  // from the wall face
        if (i == 0) { m[s.heater] = liquid(s.registry[s.heater].density); cell_temperature = t_sat + dT; }
        else if (b <= d0) { cell_temperature = exact_temperature(.5 * (a + b), t0); m[s.steam] = p0 * volume * v.molar_mass / (gas_constant * cell_temperature); }
        else if (a < d0) {
            const double f = (d0 - a) / h;
            m[s.steam] = p0 * f * volume * v.molar_mass / (gas_constant * t_sat); m[s.water] = (1 - f) * liquid(w.density);
        } else if (i < water_end) m[s.water] = liquid(w.density);
        else if (i < plug_end) m[s.plug] = liquid(s.registry[s.plug].density);
        else m[s.air] = p0 * volume * s.registry[s.air].molar_mass / (gas_constant * t_sat);
        st.cells.push_back({s.eos->initialize_at_temperature(m, cell_temperature, volume).mixture, {}});
    }
    const auto faces = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
    std::size_t first_liquid = 1;
    while (!(st.cells[first_liquid].mixture.masses[s.water] > 0)) ++first_liquid;
    const double liquid_speed = (1 - rho_v / w.density) * lambda * std::sqrt(alpha / t0);
    for (std::size_t i = first_liquid + 1; i <= plug_end; ++i) st.momentum_x[st.grid.x_face(i, 0)] = faces.x[st.grid.x_face(i, 0)] * liquid_speed;
    MacClosedSettings settings; settings.thermal_conduction = true; settings.phase_change = MacPhaseChange::saturation;
    settings.reconstruction = MacMaterialReconstruction::interface_plic;
    const double step = t0 / 1000; settings.maximum_timestep_s = step;
    MacClosedWorld world(s.registry, s.eos, st, settings);
    // Steam volume between the wall and the water's far end. A cell the
    // strict EOS cannot read holds only traces (the production kernel admits
    // them) and adds no film.
    const auto film = [&](const MacTransportState& x) {
        double gas = 0;
        for (std::size_t i = 1; i < water_end; ++i) if (x.cells[i].mixture.masses[s.steam] > 0) {
            try { gas += s.eos->evaluate(x.cells[i].mixture, volume).gas_volume_m3; } catch (const ReferenceError&) {}
        }
        return gas;
    };
    const auto h2o = [&](const MacTransportState& x) { long double m = 0; for (const auto& c : x.cells) m += c.mixture.masses[s.water] + c.mixture.masses[s.steam]; return m; };
    const long double initial = h2o(world.state());
    double t = t0, worst = 0; int k = 0;
    while (t < 10 * t0 * (1 - 1e-12)) {
        world = world.advanced(step, 65536, MacExecutionPolicy::production).world; t += step;
        // The closed domain stands for 1 atm: the film's growth would compress
        // even this reservoir by ~1.4 kPa (0.4 K of T_sat), and water conducts
        // 24 times better than steam, so a liquid that far below the interface
        // temperature takes much of the film's heat. After each interval the
        // reservoir vents nitrogen only: its pure-nitrogen cells that the
        // strict EOS can read are scaled by one common factor, at their own
        // temperatures, so their median pressure returns to p0. One factor
        // keeps the reservoir's pressure profile, so the vent creates no
        // pressure step.
        auto held = world.state(); std::vector<std::pair<std::size_t, ReferenceEquilibriumState>> vented;
        for (std::size_t i = plug_end; i < cells; ++i) {
            const auto& m = held.cells[i].mixture;
            if (m.masses[s.plug] != 0 || m.masses[s.water] != 0 || m.masses[s.air] < (1 - 1e-4) * m.mass()) continue;
            try { vented.emplace_back(i, s.eos->evaluate(m, volume)); } catch (const ReferenceError&) {}
        }
        std::vector<double> pressures; for (const auto& entry : vented) pressures.push_back(entry.second.mechanical_pressure_pa);
        std::nth_element(pressures.begin(), pressures.begin() + static_cast<std::ptrdiff_t>(pressures.size() / 2), pressures.end());
        const double factor = p0 / pressures[pressures.size() / 2];
        for (const auto& [i, r] : vented) {
            auto& cell = held.cells[i].mixture; auto target = cell.masses; target[s.air] *= factor;
            cell = s.eos->initialize_at_temperature(target, r.temperature_k, volume).mixture;
        }
        world = MacClosedWorld(s.registry, s.eos, held, settings);
        if (++k % 100 == 0) {
            const double x = film(world.state()), e = delta(t); worst = std::max(worst, std::abs(x - e) / e);
            if (k % 500 == 0) std::cout << "M5 n=" << film_cells << " t_s=" << t << " film_m=" << x << " exact_m=" << e << " error=" << (x - e) / e << '\n';
        }
    }
    mass_drift = static_cast<double>(std::abs(h2o(world.state()) - initial) / initial);
    return worst;
}
// The Stefan run is long (production kernel, thousands of cells, 9000
// intervals per grid), so the gate runs on request: "M5 --run" runs every
// grid; "M5 --quick" the 5-cell regression (worst error within 4%); "M5 --grid
// N" one grid. Sharp-interface boiling (MAC_PHASE_CHANGE.md).
int m5(bool run, bool quick, std::size_t only = 0) {
    if (!run && !quick && only == 0) {
        std::cout << "M5 note: long run; use --run, --quick or --grid N\n";
        return verdict("M5", "OPEN (not run)");
    }
    const double t0 = 1e-3; std::vector<double> errors; double mass = 0;
    const std::vector<std::size_t> grids = quick ? std::vector<std::size_t>{5} : std::vector<std::size_t>{10, 20, 40};
    for (const std::size_t n : grids) {
        if (only != 0 && n != only) continue;
        double m = 0;
        try { errors.push_back(stefan(n, t0, m)); }
        catch (const std::exception& error) { std::cout << "M5 n=" << n << " stopped: " << error.what() << '\n'; return verdict("M5", "FAIL"); }
        mass = std::max(mass, m);
        std::cout << "M5 n=" << n << " worst_error=" << errors.back() << " water_mass_drift=" << m << '\n';
    }
    if (quick) return verdict("M5", errors.back() <= .04 && mass <= 1e-12 ? "PASS" : "FAIL");
    if (only != 0) return verdict("M5", "FAIL (single grid)");
    const bool converging = errors[1] < errors[0] && errors[2] < errors[1];
    return verdict("M5", errors.back() <= .02 && converging && mass <= 1e-12 ? "PASS" : "FAIL");
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(10);
        MacKernelOptions options; options.threads = 12; set_mac_kernel_default_options(options);
        std::vector<std::string> ids; for (int a = 1; a < argc; ++a) ids.emplace_back(argv[a]);
        const auto selected = [&](const char* id) { return ids.empty() || std::find(ids.begin(), ids.end(), id) != ids.end(); };
        int failures = 0;
        if (selected("M4")) failures += m4();
        // "--grid N" runs one M5 grid (diagnostic; never a pass).
        std::size_t grid = 0;
        for (std::size_t a = 0; a + 1 < ids.size(); ++a) if (ids[a] == "--grid") grid = std::stoul(ids[a + 1]);
        const auto flag = [&](const char* name) { return std::find(ids.begin(), ids.end(), name) != ids.end(); };
        if (selected("M5")) failures += m5(flag("--run"), flag("--quick"), grid);
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 2; }
}
