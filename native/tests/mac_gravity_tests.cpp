#include "mac_fixture.hpp"
#include <iomanip>
#include <numbers>

namespace {
using namespace ma;
using namespace ma::experimental;
using ma::testing::setup;
using ma::testing::fingerprint;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void exported(int id, const MacTransportState& s, const MacClosedSubstep& r, double dt, double gravity) {
    ma::testing::export_case(id, s, r, dt);
    std::cout << "GCASE " << id << ' ' << gravity << ' ' << r.gravity_internal_residual_j << ' ' << r.gravity_internal_bound_j << ' ' <<
        r.potential_transport_residual_j << ' ' << r.potential_transport_bound_j << ' ' << r.body_impulse_y << ' ' << r.gravity_internal_change_j << ' ' <<
        r.entropy_change_j_per_k << ' ' << r.entropy_roundoff_j_per_k << '\n';
    for (std::size_t f = 0; f < r.gravity_body_work_y.size(); ++f) std::cout << "GFACE " << id << ' ' << f << ' ' << r.gravity_body_work_y[f] << ' ' <<
        r.gravity_potential_flux_y[f] << ' ' << r.gravity_internal_work_y[f] << '\n';
    for (std::size_t i = 0; i < s.cells.size(); ++i) std::cout << "GUCELL " << id << ' ' << i << ' ' << ma::testing::observed_energy(r.pre_gravity_internal_energy_j[i],r.pre_gravity_energy_remainders_j[i]) << ' ' <<
        ma::testing::observed_energy(r.state.cells[i]) << ' ' << r.gravity_internal_changes_j[i] << '\n';
}
void gravitational_cases() {
    const auto& set = setup();
    for (int control = 0; control < 2; ++control) {
        auto s = set.state(4, 3, false, control == 1, control == 1); const auto& g = s.grid;
        for (std::size_t i = 0; i < s.cells.size(); ++i) if (!g.walls[i]) s.cells[i].mixture = set.mixture(1,
            100000 * (1 + .01 * std::sin(static_cast<double>(i))), 300 + 2 * std::cos(static_cast<double>(i)), control ? .25 : 0);
        const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
        for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 1; x < g.width; ++x) {
            const auto f = g.x_face(x, y); if (!g.walls[x - 1 + y * g.width] && !g.walls[x + y * g.width]) s.momentum_x[f] = inertia.x[f] * .1 * std::sin(static_cast<double>(f) + .5);
        }
        for (std::size_t y = 1; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
            const auto f = g.y_face(x, y); if (!g.walls[x + (y - 1) * g.width] && !g.walls[x + y * g.width]) s.momentum_y[f] = inertia.y[f] * .15 * std::cos(static_cast<double>(f) + .2);
        }
        const auto held = fingerprint(s); MacClosedSettings settings; settings.gravity_m_per_s2 = 9.81;
        const auto r = advance_mac_closed_substep(set.registry, *set.eos, s, .002, settings);
        check(fingerprint(s) == held && r.body_impulse_y < 0 && r.gravity_internal_change_j != 0, "Gravity source/work/source immutability control was bypassed");
        check(std::abs(r.energy_residual_j) <= r.energy_bound_j && r.entropy_change_j_per_k >= -r.entropy_roundoff_j_per_k, "Gravity case energy or entropy control fails");
        exported(control, s, r, .002, 9.81);
    }
}
void hydrostatics() {
    const auto& set = setup(); constexpr double gravity = 9.81, temperature = 300;
    const double rspec = gas_constant * (.78 / set.registry[set.registry.index("nitrogen")].molar_mass + .22 / set.registry[set.registry.index("oxygen")].molar_mass +
        1e-28 / set.registry[set.registry.index("helium")].molar_mass);
    for (int control = 0; control < 3; ++control) {
        auto s = set.state(2, 8, false); const auto& g = s.grid; double p = 200000, previous_density = 0;
        for (std::size_t y = 0; y < g.height; ++y) {
            const bool liquid = control == 1 || (control == 2 && y < 4);
            const double a = liquid ? 1000 / 2.2e9 : 1 / (rspec * temperature), b = liquid ? 1000 : 0;
            if (y) p = (p - .5 * gravity * previous_density - .5 * gravity * b) / (1 + .5 * gravity * a);
            previous_density = a * p + b;
            for (std::size_t x = 0; x < g.width; ++x) s.cells[x + y * g.width].mixture = set.mixture(1, p, temperature, liquid ? 1 : 0);
        }
        MacClosedSettings settings; settings.gravity_m_per_s2 = gravity; settings.maximum_timestep_s = .02;
        const MacClosedWorld world(set.registry, set.eos, s, settings); const auto result = world.advanced(.1, 128);
        const auto inertia = mac_face_inertias(set.registry, result.world.state(), *set.eos); double speed = 0;
        for (std::size_t f = 0; f < inertia.y.size(); ++f) if (inertia.y[f] > 0) speed = std::max(speed, std::abs(result.world.state().momentum_y[f] / inertia.y[f]));
        check(speed < 1e-7, "Hydrostatic gas/liquid/layered state developed material motion");
        std::cout << "HYDRO " << control << ' ' << speed << ' ' << result.report.energy_residual_j << ' ' << result.report.energy_bound_j << '\n';
    }
}
std::pair<double, std::pair<double, double>> mode(std::size_t n) {
    const auto& set = setup(); const double h = 1 / static_cast<double>(n), pi = std::numbers::pi;
    const auto ni = set.registry.index("nitrogen"), he = set.registry.index("helium"); const auto& species = set.registry[ni];
    const double rspec = gas_constant / species.molar_mass, gamma = 1 + rspec / species.cv(), temperature = 300;
    const double gravity = rspec * temperature, sound = std::sqrt(gamma * rspec * temperature), amplitude = .05;
    const double omega = sound * std::sqrt(pi * pi + .25), duration = pi / (2 * omega);
    auto s = set.state(2, n, false); s.grid.periodic_x = true; s.grid.spacing_m = h;
    s.momentum_x.assign(2 * n, 0); s.momentum_y.assign(2 * (n + 1), 0);
    std::vector<double> background(n); double density = -std::expm1(-h) / h;
    for (std::size_t y = 0; y < n; ++y) {
        if (y) density *= (1 - .5 * h) / (1 + .5 * h);
        for (std::size_t x = 0; x < 2; ++x) {
            auto& c = s.cells[x + 2 * y]; std::vector<double> masses(set.registry.size());
            masses[ni] = density * h * h; masses[he] = masses[ni] * 1e-28;
            c.mixture = set.eos->initialize_at_temperature(masses, temperature, h * h).mixture;
            c.passive_quantities = {{17, c.mixture.mass()}, {UINT64_C(0x80000000), c.mixture.mass() * 1e-30}};
        }
        background[y] = set.eos->evaluate(s.cells[2 * y].mixture, h * h).mechanical_pressure_pa;
    }
    const auto inertia0 = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t y = 1; y < n; ++y) for (std::size_t x = 0; x < 2; ++x)
        s.momentum_y[s.grid.y_face(x, y)] = inertia0.y[s.grid.y_face(x, y)] * amplitude * std::exp(.5 * static_cast<double>(y) * h) * std::sin(pi * static_cast<double>(y) * h);
    MacClosedSettings settings; settings.gravity_m_per_s2 = gravity; settings.maximum_timestep_s = duration / static_cast<double>(n / 4);
    const MacClosedWorld original(set.registry, set.eos, s, settings); const auto r = original.advanced(duration, 512);
    const auto& state = r.world.state(); const auto inertia1 = mac_face_inertias(set.registry, state, *set.eos);
    const auto integral_sine = [&](double y) { return std::exp(-.5 * y) * (-.5 * std::sin(pi * y) - pi * std::cos(pi * y)) / (.25 + pi * pi); };
    const auto integral_cosine = [&](double y) { return std::exp(-.5 * y) * (-.5 * std::cos(pi * y) + pi * std::sin(pi * y)) / (.25 + pi * pi); };
    double perr = 0, uerr = 0, mass = 0;
    for (std::size_t y = 0; y < n; ++y) {
        const double left = static_cast<double>(y) * h, right = left + h;
        const double expected_p = amplitude / omega / h * ((gravity - .5 * sound * sound) * (integral_sine(right) - integral_sine(left)) -
            sound * sound * pi * (integral_cosine(right) - integral_cosine(left)));
        for (std::size_t x = 0; x < 2; ++x) {
            const auto& cell = state.cells[x + 2 * y]; const auto current = set.eos->evaluate(cell.mixture, h * h);
            const double u = state.momentum_y[state.grid.y_face(x, y)] / inertia1.y[state.grid.y_face(x, y)];
            perr += .5 * h * std::abs(current.mechanical_pressure_pa - background[y] - expected_p) / (sound * amplitude);
            uerr += .5 * h * std::abs(u) / amplitude; mass += s.cells[x + 2 * y].mixture.mass();
            if (x == 0) {
                const auto& old = s.cells[2 * y]; const auto old_eos = set.eos->evaluate(old.mixture, h * h);
                std::cout << "GSAMPLE " << n << ' ' << y << ' ' << old.mixture.masses[ni] << ' ' << old.mixture.masses[he] << ' ' << ma::testing::observed_energy(old) << ' ' <<
                    cell.mixture.masses[ni] << ' ' << cell.mixture.masses[he] << ' ' << ma::testing::observed_energy(cell) << ' ' << s.momentum_y[s.grid.y_face(0, y)] << ' ' <<
                    state.momentum_y[state.grid.y_face(0, y)] << ' ' << background[y] << ' ' << current.mechanical_pressure_pa << ' ' << current.temperature_k << ' ' <<
                    old_eos.entropy_j_per_k << ' ' << current.entropy_j_per_k << '\n';
            }
        }
    }
    const double entropy = r.report.entropy_change_j_per_k / mass;
    std::cout << "GMODE " << n << ' ' << gravity << ' ' << amplitude << ' ' << sound << ' ' << duration << ' ' << settings.maximum_timestep_s << ' ' <<
        r.report.substeps << ' ' << r.report.rejected_attempts << ' ' << perr << ' ' << uerr << ' ' << entropy << ' ' << r.report.entropy_roundoff_j_per_k / mass << ' ' <<
        r.report.energy_residual_j << ' ' << r.report.energy_bound_j << '\n';
    check(perr > 0 && uerr > 0 && r.report.rejected_attempts == 0, "Stratified wave lacks measurable errors or retried its nominal grid path");
    return {std::abs(entropy), {perr, uerr}};
}
void refinement() {
    double previous_s = 0, previous_p = 0, previous_u = 0;
    for (const auto n : {std::size_t(32), std::size_t(64), std::size_t(128)}) {
        const auto current = mode(n);
        if (n != 32) check(current.first < .8 * previous_s && current.second.first < .7 * previous_p && current.second.second < .7 * previous_u,
            "Stratified pressure/velocity/entropy grid trend fails its declared joint bounds");
        previous_s = current.first; previous_p = current.second.first; previous_u = current.second.second;
    }
    check(previous_p < .02 && previous_u < .02 && previous_s < 1e-6, "Finest stratified wave exceeds its declared absolute bounds");
}
void invalid_and_rollback() {
    const auto& set = setup(); const auto s = set.state(2, 3, false); const auto held = fingerprint(s);
    for (int control = 0; control < 4; ++control) {
        auto bad = s; MacClosedSettings settings; settings.gravity_m_per_s2 = 9.81;
        if (control == 0) settings.gravity_m_per_s2 = -1;
        if (control == 1) settings.gravity_m_per_s2 = std::numeric_limits<double>::quiet_NaN();
        if (control == 2) bad = set.state(2, 3, true);
        bool caught = false;
        try { const MacClosedWorld world(set.registry, set.eos, bad, settings); if (control == 3) static_cast<void>(world.advanced(.03, 1)); }
        catch (const std::exception&) { caught = true; }
        check(caught && fingerprint(s) == held, "Invalid gravity or exhausted budget changed source");
    }
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        for (const auto& m : setup().eos->reference().materials()) std::cout << "MATERIAL " << setup().registry.index(m.id) << ' ' << (m.phase == ReferencePhase::ideal_gas ? 0 : 1) << ' ' <<
            m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
        gravitational_cases(); std::cout << "PASS actual-mass gravity work in gas and wet solid-corner cases\n";
        hydrostatics(); std::cout << "PASS gas, elastic liquid and layered hydrostatic rest\n";
        refinement(); std::cout << "PASS joint stratified pressure, velocity and entropy refinement\n";
        invalid_and_rollback(); std::cout << "PASS invalid gravity topology/settings and interval rollback\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
