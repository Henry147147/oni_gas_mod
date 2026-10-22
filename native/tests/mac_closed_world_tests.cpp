#include "mac_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <string_view>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void near(double a, double b, double scale, double relative = 2e-11) {
    check(std::isfinite(a) && std::abs(a - b) <= relative * scale, "MAC closed independent comparison failed");
}
using ma::testing::setup;
using ma::testing::fingerprint;
using ma::testing::export_case;
MacTransportState corner_state() {
    const auto& set = setup(); auto s = set.state(4, 3, false, true, true); const auto& g = s.grid;
    for (std::size_t i = 0; i < s.cells.size(); ++i) if (!g.walls[i])
        s.cells[i].mixture = set.mixture(1, 100000 * (1 + .03 * std::sin(static_cast<double>(i))), 300 + 2 * std::cos(static_cast<double>(i)), .2 + .05 * std::sin(static_cast<double>(i)));
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 1; x < g.width; ++x) {
        const auto f = g.x_face(x, y); if (!g.walls[x - 1 + y * g.width] && !g.walls[x + y * g.width]) s.momentum_x[f] = inertia.x[f] * .125 * std::sin(static_cast<double>(f) + .5);
    }
    for (std::size_t y = 1; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto f = g.y_face(x, y); if (!g.walls[x + (y - 1) * g.width] && !g.walls[x + y * g.width]) s.momentum_y[f] = inertia.y[f] * -.2 * std::cos(static_cast<double>(f) + .2);
    }
    return s;
}
void gas_pressure() {
    const auto& set = setup(); auto s = set.state(2, 1, false);
    s.cells[0].mixture = set.mixture(1, 150000, 300); s.cells[1].mixture = set.mixture(1, 50000, 300);
    const auto held = fingerprint(s); const auto result = advance_mac_closed_substep(set.registry, *set.eos, s, .001);
    check(result.pressure.nonlinear_iterations > 0 && result.pressure.pressure_iterations > 0, "Pressure solve was bypassed");
    check(result.state.cells[0].mixture.mass() < s.cells[0].mixture.mass() && result.state.momentum_x[1] > 0, "Pressure failed to drive transport");
    check(fingerprint(s) == held, "Pressure changed immutable source"); export_case(0, s, result, .001);
}
void wet_corner() {
    const auto& set = setup(); const auto s = corner_state(); const auto held = fingerprint(s);
    const auto result = advance_mac_closed_substep(set.registry, *set.eos, s, .002);
    check(result.transport.mixing_heat_j > 0 && result.transport.wall_heat_j > 0, "Wet corner lacks mixing or wall heat");
    check(result.pressure.nonlinear_iterations > 0 && fingerprint(s) == held, "Wet pressure was bypassed or source changed");
    for (std::size_t i = 0; i < s.cells.size(); ++i) if (!s.grid.walls[i]) check(result.state.cells[i].mixture.masses[set.registry.index("helium")] > 0, "Wet pressure lost trace");
    export_case(1, s, result, .002);
}
void constant_contact() {
    const auto& set = setup(); auto s = set.state(4, 3, true, true);
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t f = 0; f < inertia.x.size(); ++f) s.momentum_x[f] = .125 * inertia.x[f];
    for (std::size_t f = 0; f < inertia.y.size(); ++f) s.momentum_y[f] = -.25 * inertia.y[f];
    const auto result = advance_mac_closed_substep(set.registry, *set.eos, s, .001);
    const auto next = mac_face_inertias(set.registry, result.state, *set.eos);
    for (const auto& c : result.state.cells) { const auto e = set.eos->evaluate(c.mixture, 1); near(e.mechanical_pressure_pa, 100000, 100000); near(e.temperature_k, 300, 300); }
    for (std::size_t f = 0; f < next.x.size(); ++f) near(result.state.momentum_x[f] / next.x[f], .125, .125);
    for (std::size_t f = 0; f < next.y.size(); ++f) near(result.state.momentum_y[f] / next.y[f], -.25, .25);
}
void compressed_liquid_identity() {
    const auto& set = setup(); auto s = set.state(3, 2, true);
    for (auto& c : s.cells) c.mixture = set.mixture(1, 2.2e8, 300, 1);
    check(s.cells[0].mixture.condensed_volume(set.registry) > 1 && std::abs(s.cells[0].mixture.temperature(set.registry) - 300) > 1,
        "Elastic-liquid control does not distinguish legacy EOS");
    const auto held = fingerprint(s); const auto result = advance_mac_closed_substep(set.registry, *set.eos, s, .001);
    check(fingerprint(s) == held && fingerprint(result.state) == held, "Uniform compressed liquid changed without a force or flux");
    near(set.eos->evaluate(result.state.cells[0].mixture, 1).temperature_k, 300, 300);
    auto alias = std::make_shared<FluidThermodynamics>(*set.eos); const MacClosedWorld owned(set.registry, alias, s);
    auto changed = set.eos->reference().materials(); changed.back().reference_bulk_modulus_pa *= .5;
    *alias = FluidThermodynamics(set.registry, "changed-mutable-alias", std::move(changed));
    check(owned.thermodynamics().reference().metadata() == set.eos->reference().metadata(), "Mutable caller alias changed owner EOS identity");
    const auto own_step = owned.advanced(.001);
    near(own_step.world.thermodynamics().evaluate(own_step.world.state().cells[0].mixture, 1).mechanical_pressure_pa, 2.2e8, 2.2e8);
}
void interval_atomicity() {
    const auto& set = setup(); auto s = set.state(3, 2, true); const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t f = 0; f < inertia.x.size(); ++f) s.momentum_x[f] = 5 * inertia.x[f];
    MacClosedSettings settings; settings.maximum_timestep_s = .1;
    const MacClosedWorld source(set.registry, set.eos, s, settings); const auto held = fingerprint(source.state());
    const auto advanced = source.advanced(.1, 16);
    check(advanced.report.rejected_attempts > 0 && advanced.report.substeps > 1 && advanced.report.attempts <= 16, "CFL retry/shared budget was bypassed");
    check(source.time_s() == 0 && source.revision() == 1 && fingerprint(source.state()) == held && advanced.world.time_s() == .1 && advanced.world.revision() == 2,
        "Owner clock/revision/source changed outside commit");
    settings.maximum_timestep_s = .001; const MacClosedWorld late(set.registry, set.eos, set.state(3, 2, true), settings);
    const auto before = fingerprint(late.state()); bool rejected = false;
    try { static_cast<void>(late.advanced(.003, 2)); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected && late.time_s() == 0 && late.revision() == 1 && fingerprint(late.state()) == before, "Later interval failure published a partial owner");
    const auto completed = late.advanced(.003, 8); check(completed.report.substeps >= 3 && completed.world.time_s() == .003, "Unfailed interval did not complete");
    std::cout << "INTERVAL " << advanced.report.attempts << ' ' << advanced.report.rejected_attempts << ' ' << advanced.report.substeps << ' ' <<
        advanced.report.energy_residual_j << ' ' << advanced.report.energy_bound_j << '\n';
}
std::pair<double, double> acoustic(std::size_t n) {
    const auto& set = setup(); auto s = set.state(n, n, true); const double h = 1 / static_cast<double>(n); s.grid.spacing_m = h;
    const auto ni = set.registry.index("nitrogen"), he = set.registry.index("helium"); const auto& nitrogen = set.registry[ni];
    const double rspec = gas_constant / nitrogen.molar_mass, gamma = 1 + rspec / nitrogen.cv();
    const double p0 = 100000, t0 = 300, amplitude = .1, rho0 = p0 / (rspec * t0), sound = std::sqrt(gamma * p0 / rho0);
    const double pi = std::numbers::pi, sinc = std::sin(pi * h) / (pi * h), duration = .25 / sound;
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        const double sine = sinc * std::sin(2 * pi * (static_cast<double>(x) + .5) * h);
        const double p = p0 + amplitude * sine, rho = rho0 + amplitude * sine / (sound * sound);
        std::vector<double> masses(set.registry.size()); masses[ni] = rho * h * h; masses[he] = masses[ni] * 1e-28;
        auto& c = s.cells[x + y * n]; c.mixture = set.eos->initialize_at_temperature(masses, p / (rspec * rho), h * h).mixture;
        c.passive_quantities = {{17, c.mixture.mass()}, {UINT64_C(0x80000000), c.mixture.mass() * 1e-30}};
    }
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x)
        s.momentum_x[s.grid.x_face(x, y)] = inertia.x[s.grid.x_face(x, y)] * amplitude / (rho0 * sound) * sinc * std::sin(2 * pi * static_cast<double>(x) * h);
    MacClosedSettings settings; settings.maximum_timestep_s = duration / static_cast<double>(n / 8);
    const MacClosedWorld world(set.registry, set.eos, s, settings); const auto result = world.advanced(duration, 128);
    const auto next = mac_face_inertias(set.registry, result.world.state(), *set.eos); double perr = 0, uerr = 0;
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        const auto i = x + y * n; const auto f = s.grid.x_face(x, y);
        const double p = set.eos->evaluate(result.world.state().cells[i].mixture, h * h).mechanical_pressure_pa;
        const double u = result.world.state().momentum_x[f] / next.x[f];
        perr += h * h * std::abs((p - p0) / amplitude - sinc * std::sin(2 * pi * ((static_cast<double>(x) + .5) * h - .25)));
        uerr += h * h * std::abs(u * rho0 * sound / amplitude - sinc * std::sin(2 * pi * (static_cast<double>(x) * h - .25)));
        if (y == 0) std::cout << "ASAMPLE " << n << ' ' << x << ' ' << p << ' ' << u << '\n';
    }
    check(result.report.pressure_iterations > 0 && result.report.nonlinear_iterations > 0, "Acoustic pressure solve was bypassed");
    std::cout << "ACOUSTIC " << n << ' ' << gamma << ' ' << rho0 << ' ' << sound << ' ' << duration << ' ' << settings.maximum_timestep_s << ' ' <<
        result.report.substeps << ' ' << result.report.rejected_attempts << ' ' << perr << ' ' << uerr << ' ' << result.report.energy_residual_j << ' ' << result.report.energy_bound_j << '\n';
    return {perr, uerr};
}
void acoustic_refinement() {
    std::pair<double, double> previous{0, 0};
    for (const auto n : {std::size_t(16), std::size_t(32), std::size_t(64)}) {
        const auto current = acoustic(n);
        if (previous.first != 0) check(current.first < .7 * previous.first && current.second < .7 * previous.second, "Joint implicit acoustic refinement fails .7 bound");
        previous = current;
    }
}
void transpose() {
    const auto& set = setup(); const auto s = corner_state(); const auto& g = s.grid; auto t = set.state(3, 4, false);
    const auto& tg = t.grid;
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) { t.cells[y + x * tg.width] = s.cells[x + y * g.width]; t.grid.walls[y + x * tg.width] = g.walls[x + y * g.width]; }
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x) t.momentum_y[tg.y_face(y, x)] = s.momentum_x[g.x_face(x, y)];
    for (std::size_t y = 0; y < g.y_height(); ++y) for (std::size_t x = 0; x < g.width; ++x) t.momentum_x[tg.x_face(y, x)] = s.momentum_y[g.y_face(x, y)];
    const auto a = advance_mac_closed_substep(set.registry, *set.eos, s, .002), b = advance_mac_closed_substep(set.registry, *set.eos, t, .002);
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto& ca = a.state.cells[x + y * g.width]; const auto& cb = b.state.cells[y + x * tg.width];
        near(ca.mixture.energy, cb.mixture.energy, std::abs(ca.mixture.energy));
        for (std::size_t k = 0; k < set.registry.size(); ++k) near(ca.mixture.masses[k], cb.mixture.masses[k], ca.mixture.masses[k]);
    }
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.x_width(); ++x)
        near(a.state.momentum_x[g.x_face(x, y)], b.state.momentum_y[tg.y_face(y, x)], std::abs(a.state.momentum_x[g.x_face(x, y)]));
}
void snapshot_branches() {
    const auto& set = setup(); auto initial = corner_state();
    MacClosedSettings settings; settings.numerical_kernel = MacNumericalKernel::production;
    const MacClosedWorld source(set.registry, set.eos, initial, settings);
    const auto before = source.checkpoint(); auto sibling = source;
    const MacCellHeat heat{0, .5, 1e5};
    auto heated = sibling.with_cell_heat(std::span(&heat, 1));
    const auto heated_image = heated.checkpoint();
    auto survivor = [&] { auto temporary = heated; return temporary; }();
    heated = source;
    const auto evolved = sibling.advanced(.001, 1024, MacExecutionPolicy::production);
    const auto restored = MacClosedWorld::from_checkpoint(set.registry, set.eos, before);
    check(source.checkpoint() == before && sibling.checkpoint() == before && heated.checkpoint() == before,
        "Editing or advancing a world changed a copied source");
    check(survivor.checkpoint() == heated_image && heated_image != before,
        "World state did not survive sibling replacement");
    check(restored.advanced(.001, 1024, MacExecutionPolicy::production).world.checkpoint() == evolved.world.checkpoint(),
        "Copied world lost exact checkpoint continuation");
    bool rejected = false;
    const MacCellHeat invalid{initial.cells.size(), 1, 1e5};
    try { static_cast<void>(sibling.with_cell_heat(std::span(&invalid, 1))); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && sibling.checkpoint() == before && survivor.checkpoint() == heated_image,
        "Rejected branch changed another owner");
}
void rejected_domains() {
    const auto& set = setup(); const auto original = set.state(2, 1, false); const auto held = fingerprint(original);
    for (int control = 0; control < 10; ++control) {
        auto bad = original; auto eos = set.eos; auto registry = set.registry; MacClosedSettings settings; bool caught = false;
        if (control == 0) eos.reset();
        if (control == 1) { auto extra = registry[registry.index("oxygen")]; extra.id = "extra"; registry.add(extra); }
        if (control == 2) { bad.cells[0].mixture.masses[registry.index("chlorine")] = 1e-28; }
        if (control == 3) {
            // Empty cells are admitted; energy without material remains invalid.
            bad.cells[0].mixture = MixtureInventory(registry.size()); bad.cells[0].mixture.energy = 1e-100;
            bad.cells[0].passive_quantities.clear();
        }
        if (control == 4) { std::vector<double> m(registry.size()); m[registry.index("water")] = 10; bad.cells[0].mixture = set.eos->initialize_at_temperature(m, 300, 1).mixture; }
        if (control == 5) bad.momentum_x[0] = 1;
        if (control == 6) bad.momentum_y.pop_back();
        if (control == 7) settings.maximum_outgoing_volume_fraction = .5;
        const auto before = fingerprint(bad);
        try {
            const MacClosedWorld world(registry, eos, bad, settings);
            if (control == 8) static_cast<void>(world.advanced(-1));
            if (control == 9) static_cast<void>(world.advanced(.001, 0));
        } catch (const std::exception&) { caught = true; }
        check(caught && fingerprint(bad) == before && fingerprint(original) == held, "Rejected MAC domain changed caller state or was accepted");
    }
    const std::vector<MaterialCell> material{{original.cells[0].mixture, 1, 0}};
    for (const double tolerance : {0., 1e-12, std::numeric_limits<double>::quiet_NaN()}) {
        bool caught = false;
        try { static_cast<void>(advance_occupied_material_pressure(*set.eos, material, {}, .001, PressurePreconditioner::automatic,
            LiquidFillRoundoffPolicy::strict_eos, tolerance)); } catch (const std::invalid_argument&) { caught = true; }
        check(caught && fingerprint(original) == held, "Unsupported occupied root accuracy was accepted");
    }
    for (int control = 0; control < 3; ++control) {
        ReferenceRootOptions options;
        if (control == 0) options.relative_pressure_tolerance = 0;
        if (control == 1) options.relative_volume_tolerance = std::numeric_limits<double>::quiet_NaN();
        if (control == 2) options.maximum_iterations = 0;
        bool caught = false;
        try { static_cast<void>(advance_occupied_material_pressure(*set.eos, material, {}, .001, PressurePreconditioner::automatic,
            LiquidFillRoundoffPolicy::strict_eos, 16 * std::numeric_limits<double>::epsilon(), options)); }
        catch (const ReferenceError& error) { caught = error.code() == ReferenceErrorCode::invalid_model; }
        check(caught && fingerprint(original) == held, "Invalid nested EOS options were accepted or changed input");
    }
}
}
int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        if (argc == 2 && std::string_view(argv[1]) == "--snapshot-only") {
            snapshot_branches(); std::cout << "PASS copied world branches, lifetime, rollback and cold continuation\n"; return 0;
        }
        for (const auto& m : setup().eos->reference().materials()) std::cout << "MATERIAL " << setup().registry.index(m.id) << ' ' <<
            (m.phase == ReferencePhase::ideal_gas ? 0 : 1) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' <<
            m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
        snapshot_branches(); std::cout << "PASS copied world branches, lifetime, rollback and cold continuation\n";
        gas_pressure(); std::cout << "PASS pressure-driven retained gas momentum and trace transport\n";
        wet_corner(); std::cout << "PASS elastic gas-liquid pressure transport at a solid corner\n";
        constant_contact(); std::cout << "PASS constant-p/T variable-density mixture contact\n";
        compressed_liquid_identity(); std::cout << "PASS compressed liquid uses EOS and zero-flux identity\n";
        interval_atomicity(); std::cout << "PASS retained owner CFL retry and later whole-interval rollback\n";
        acoustic_refinement(); std::cout << "PASS joint implicit pressure/velocity acoustic refinement\n";
        transpose(); std::cout << "PASS transposed two-dimensional pressure/transport symmetry\n";
        rejected_domains(); std::cout << "PASS ten domain rejections, three root-accuracy bounds and three nested EOS option rejections\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
