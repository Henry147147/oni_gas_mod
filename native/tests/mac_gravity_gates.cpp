// Milestone B gravity gates (docs/completion-plan.md, "Gravity and multiphase
// gates"). Each gate prints GATE <id> PASS|FAIL with its measurements and
// exits nonzero on failure. Both run the production kernel.
//   G1  hydrostatic gas column: a discrete hydrostatic N2 column stays at rest,
//       and its profile converges to the exponential at second order.
//   G2  gravitational species separation: CO2/N2 settles to each species
//       barometric law through Maxwell-Stefan pressure diffusion.
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_diffusion.hpp"
#include <cstring>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double g = 9.81, temperature = 300;
bool verdict(const char* id, bool pass) { std::cout << "GATE " << id << (pass ? " PASS" : " FAIL") << '\n'; return pass; }
double max_speed(const SpeciesRegistry& registry, const FluidThermodynamics& eos, const MacTransportState& s) {
    const auto inertia = mac_face_inertias(registry, s, eos); double speed = 0;
    for (std::size_t f = 0; f < inertia.x.size(); ++f) if (inertia.x[f] > 0) speed = std::max(speed, std::abs(s.momentum_x[f] / inertia.x[f]));
    for (std::size_t f = 0; f < inertia.y.size(); ++f) if (inertia.y[f] > 0) speed = std::max(speed, std::abs(s.momentum_y[f] / inertia.y[f]));
    return speed;
}

// Discrete hydrostatic column of an ideal-gas mixture with specific gas
// constant r: each y face balances A (p_below - p_above) = g (m_below + m_above) / 2,
// the solver's own force law. The bottom cell centre holds the analytic value.
std::vector<double> discrete_column(std::size_t n, double h, double r, double p_ground) {
    std::vector<double> p(n); const double v = h * h;
    p[0] = p_ground * std::exp(-g * .5 * h / (r * temperature));
    for (std::size_t y = 1; y < n; ++y) {
        const double below = p[y - 1] * v / (r * temperature);
        const double mass = (p[y - 1] - g * below / (2 * h)) / (r * temperature / v + g / (2 * h));
        p[y] = mass * r * temperature / v;
    }
    return p;
}

bool g1() {
    const auto& s = ma::testing::setup(); const auto n2 = s.registry.index("nitrogen");
    const double r = gas_constant / s.registry[n2].molar_mass, height = 100, p_ground = 101325;
    std::vector<double> errors; bool rest = true;
    for (const std::size_t n : {64, 128, 256}) {
        const double h = height / static_cast<double>(n);
        const auto p = discrete_column(n, h, r, p_ground);
        MacTransportState st; st.grid = {2, n, h, 1, false, false, std::vector<std::uint8_t>(2 * n)};
        for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < 2; ++x) {
            std::vector<double> masses(s.registry.size()); masses[n2] = p[y] * h * h / (r * temperature);
            st.cells.push_back({s.eos->initialize_at_temperature(masses, temperature, h * h).mixture, {}});
        }
        const auto faces = mac_face_inertias(s.registry, st, *s.eos);
        st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
        // Profile against the analytic exponential at the cell centres.
        double error = 0;
        for (std::size_t y = 0; y < n; ++y) {
            const double z = (static_cast<double>(y) + .5) * h, exact = p_ground * std::exp(-g * z / (r * temperature));
            error = std::max(error, std::abs(s.eos->evaluate(st.cells[2 * y].mixture, h * h).mechanical_pressure_pa - exact) / exact);
        }
        errors.push_back(error);
        MacClosedSettings settings; settings.gravity_m_per_s2 = g;
        MacClosedWorld world(s.registry, s.eos, st, settings);
        double speed = 0, drift = 0;
        for (int k = 0; k < 1000; ++k) {
            world = world.advanced(.1, 4096, MacExecutionPolicy::production).world;
            speed = std::max(speed, max_speed(s.registry, *s.eos, world.state()));
        }
        for (std::size_t i = 0; i < st.cells.size(); ++i) {
            const double before = s.eos->evaluate(st.cells[i].mixture, h * h).mechanical_pressure_pa;
            const double after = s.eos->evaluate(world.state().cells[i].mixture, h * h).mechanical_pressure_pa;
            drift = std::max(drift, std::abs(after - before) / before);
        }
        std::cout << "G1 n=" << n << " profile_error=" << error << " max_speed_m_s=" << speed << " pressure_drift=" << drift << '\n';
        rest = rest && speed <= 1e-9 && drift <= 1e-12;
    }
    const double r1 = errors[1] / errors[0], r2 = errors[2] / errors[1];
    std::cout << "G1 convergence_ratios=" << r1 << ' ' << r2 << '\n';
    return verdict("G1", rest && errors[0] <= 1e-6 && r1 <= .3 && r2 <= .3);
}

// CO2 and N2 registry (ideal gases) for G2.
struct SeparationSetup {
    SpeciesRegistry registry; std::shared_ptr<const FluidThermodynamics> eos;
    SeparationSetup() {
        const auto standard = SpeciesRegistry::standard(); std::vector<ReferenceMaterial> materials;
        for (const char* id : {"nitrogen", "carbon_dioxide"}) {
            const auto& sp = standard[standard.index(id)]; registry.add(sp); ReferenceMaterial m;
            m.id = id; m.phase = ReferencePhase::ideal_gas; m.molar_mass_kg_per_mol = sp.molar_mass; m.cv_j_per_kg_k = sp.cv();
            m.energy_offset_j_per_kg = sp.internal_energy(0); m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e6;
            m.provenance = "gravity gate ideal gas"; materials.push_back(m);
        }
        eos = std::make_shared<const FluidThermodynamics>(registry, "gravity-gates-1", std::move(materials));
    }
};

bool g2() {
    const SeparationSetup s; const auto profile = GasTransportProfile::standard();
    constexpr std::size_t n = 64; const double height = 1, h = height / n, p_ground = 101325, multiplier = 1000;
    const double mn = s.registry[0].molar_mass, mc = s.registry[1].molar_mass;
    // 50/50 by mole: specific gas constant of the mixture.
    const double r = gas_constant / (.5 * mn + .5 * mc);
    const auto p = discrete_column(n, h, r, p_ground);
    MacTransportState st; st.grid = {1, n, h, 1, false, false, std::vector<std::uint8_t>(n)};
    for (std::size_t y = 0; y < n; ++y) {
        const double moles = p[y] * h * h / (gas_constant * temperature);
        const std::vector<double> masses{.5 * moles * mn, .5 * moles * mc};
        st.cells.push_back({s.eos->initialize_at_temperature(masses, temperature, h * h).mixture, {}});
    }
    const auto faces = mac_face_inertias(s.registry, st, *s.eos);
    st.momentum_x.assign(faces.x.size(), 0); st.momentum_y.assign(faces.y.size(), 0);
    const auto pair = std::find_if(profile->pairs().begin(), profile->pairs().end(), [](const GasDiffusionPair& q) {
        return (q.first == "nitrogen" && q.second == "carbon_dioxide") || (q.first == "carbon_dioxide" && q.second == "nitrogen"); });
    if (pair == profile->pairs().end()) throw std::runtime_error("Standard transport profile lacks the N2/CO2 pair");
    const double d = evaluate_gas_diffusivity(*pair, temperature, p_ground, mn, mc, h, multiplier).effective_m2_per_s;
    const double diffusion_time = height * height / d, duration = 10 * diffusion_time;
    MacClosedSettings settings; settings.gravity_m_per_s2 = g; settings.diffusion = {MacDiffusionMode::accelerated, multiplier};
    MacClosedWorld world(s.registry, s.eos, st, settings, profile);
    const long double initial[2] = {[&] { long double m = 0; for (const auto& c : st.cells) m += c.mixture.masses[0]; return m; }(),
                                    [&] { long double m = 0; for (const auto& c : st.cells) m += c.mixture.masses[1]; return m; }()};
    double t = 0;
    while (t < duration) { const double step = std::min(10., duration - t); world = world.advanced(step, 65536, MacExecutionPolicy::production).world; t += step; }
    // Each species' barometric exponent ln(p_k(z) / p_k(z0)) = -M_k g (z - z0) / (R T).
    double worst = 0, conservation = 0;
    for (std::size_t k = 0; k < 2; ++k) {
        const double mk = s.registry[k].molar_mass, scale = mk * g * (height - h) / (gas_constant * temperature);
        const auto partial = [&](std::size_t y) {
            const auto& c = world.state().cells[y].mixture; const auto state = s.eos->evaluate(c, h * h);
            const double moles = c.masses[0] / mn + c.masses[1] / mc;
            return state.mechanical_pressure_pa * (c.masses[k] / mk) / moles;
        };
        const double base = partial(0);
        for (std::size_t y = 1; y < n; ++y) {
            const double dz = static_cast<double>(y) * h;
            const double measured = std::log(partial(y) / base), exact = -mk * g * dz / (gas_constant * temperature);
            worst = std::max(worst, std::abs(measured - exact) / scale);
        }
        long double total = 0; for (const auto& c : world.state().cells) total += c.mixture.masses[k];
        conservation = std::max(conservation, static_cast<double>(std::abs(total - initial[k]) / initial[k]));
    }
    const auto& top = world.state().cells.back().mixture; const auto& bottom = world.state().cells.front().mixture;
    const auto fraction = [&](const MixtureInventory& c) { return (c.masses[1] / mc) / (c.masses[0] / mn + c.masses[1] / mc); };
    const double separation = fraction(bottom) - fraction(top);
    const double expected = .25 * (mc - mn) * g * (height - h) / (gas_constant * temperature);
    std::cout << "G2 n=" << n << " D_m2_s=" << d << " diffusion_time_s=" << diffusion_time << " simulated_s=" << t
              << " exponent_error=" << worst << " co2_separation=" << separation << " expected~" << expected
              << " species_drift=" << conservation << " max_speed_m_s=" << max_speed(s.registry, *s.eos, world.state()) << '\n';
    return verdict("G2", worst <= .01 && conservation <= 1e-14);
}
}

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(10);
        const auto selected = [&](const char* id) {
            if (argc < 2) return true;
            for (int a = 1; a < argc; ++a) if (std::strcmp(argv[a], id) == 0) return true;
            return false;
        };
        bool ok = true;
        if (selected("G1")) ok = g1() && ok;
        if (selected("G2")) ok = g2() && ok;
        return ok ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 2; }
}
