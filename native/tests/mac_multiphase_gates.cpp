// Milestone B multiphase gates (docs/completion-plan.md, "Gravity and
// multiphase gates"). Each gate prints GATE <id> PASS|FAIL|OPEN with its
// measurements; FAIL and OPEN exit nonzero. Both run the production kernel.
//   M4  two immiscible liquids (Rayleigh-Taylor), with sharp interfaces: a
//       cell holding both liquids places the denser below a PLIC line.
//   M5  Stefan vapour film. Open until phase change exists.
#include "mac_kernel_fixture.hpp"
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
int m5() {
    std::cout << "M5 note: phase change is not implemented\n";
    return verdict("M5", "OPEN (phase change not implemented)");
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
        if (selected("M5")) failures += m5();
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 2; }
}
