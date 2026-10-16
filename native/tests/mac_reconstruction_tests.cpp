#include "mac_fixture.hpp"
#include <iomanip>
#include <numbers>

namespace {
using namespace ma;
using namespace ma::experimental;
using ma::testing::setup;
using ma::testing::fingerprint;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void near(double a, double b, double scale, double relative = 2e-11) { check(std::isfinite(a) && std::abs(a - b) <= relative * scale, "Reconstruction independent comparison failed"); }
std::string fingerprint(const MacParcelPlan& p) {
    std::ostringstream s; s << std::hexfloat << p.eos_backtracks << ' ' << p.unresolved_stencil_cells;
    const auto cell = [&](const MacMaterialCell& c) { s << ' ' << c.mixture.energy; for (auto v : c.mixture.masses) s << ' ' << v; for (const auto& v : c.passive_quantities) s << ' ' << v.identity << ' ' << v.quantity; };
    for (auto v : p.source_volumes_m3) s << ' ' << v;
    for (auto v : p.retained_volumes_m3) s << ' ' << v;
    for (auto v : p.limiter) s << ' ' << v;
    for (const auto& c : p.retained) cell(c);
    for (const auto& v : p.parcels) { s << ' ' << v.axis << ' ' << v.face << ' ' << v.donor << ' ' << v.target << ' ' << v.volume_m3; cell(v.contents); }
    return s.str();
}
MacMassFluxes uniform_sweeps(const MacTransportState& s, double x, double y) {
    return {std::vector<double>(s.grid.x_width() * s.grid.height, x), std::vector<double>(s.grid.width * s.grid.y_height(), y)};
}
void mixed_contacts() {
    const auto& set = setup(); auto s = set.state(6, 4, true, true); const auto& g = s.grid;
    const auto oi = set.registry.index("oxygen"), ni = set.registry.index("nitrogen"), he = set.registry.index("helium");
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const double alpha = .25 + .1 * std::sin(2 * std::numbers::pi * (static_cast<double>(x) + .5) / static_cast<double>(g.width));
        const double oxygen = .2 + .04 * std::cos(2 * std::numbers::pi * (static_cast<double>(y) + .5) / static_cast<double>(g.height));
        auto m = set.mixture(1, 100000, 300, alpha, oxygen);
        m.masses[he] *= 1 + .2 * std::sin(static_cast<double>(x + y));
        m = set.eos->initialize_at_temperature(m.masses, 300, 1).mixture;
        s.cells[x + y * g.width].mixture = std::move(m);
    }
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t f = 0; f < inertia.x.size(); ++f) s.momentum_x[f] = inertia.x[f] * .125;
    for (std::size_t f = 0; f < inertia.y.size(); ++f) s.momentum_y[f] = inertia.y[f] * .0625;
    const auto held = fingerprint(s); const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, s, std::vector<double>(s.cells.size(), 1), uniform_sweeps(s, .125, .0625));
    const auto pheld = fingerprint(plan); const auto r = advance_mac_parcel_transport(set.registry, s, *set.eos, plan);
    check(fingerprint(s) == held && fingerprint(plan) == pheld && !plan.parcels.empty(), "Reconstructed contact changed source/plan or bypassed fluxes");
    for (const auto& c : r.state.cells) {
        const auto e = set.eos->evaluate(c.mixture, 1); near(e.temperature_k, 300, 300); near(e.mechanical_pressure_pa, 100000, 100000);
        check(c.mixture.masses[he] > 0 && c.mixture.masses[oi] > 0 && c.mixture.masses[ni] > 0, "Independent gas constituents were erased");
    }
    ma::testing::export_transport(0, s, r, plan);
}
void cold_liquid() {
    const auto& set = setup(); auto s = set.state(6, 2, true);
    auto definitions = set.eos->reference().materials();
    for (auto& material : definitions) if (material.id == "water") material.minimum_temperature_k = 300;
    const FluidThermodynamics cold(set.registry, "mac-reconstruction-cold-boundary-v1", std::move(definitions));
    for (std::size_t i = 0; i < s.cells.size(); ++i) s.cells[i].mixture = set.mixture(1, 2.2e9 * (.05 + .15 * static_cast<double>(i % 6)), 300.001, 1);
    const auto plan = reconstruct_mac_parcels(set.registry, cold, s, std::vector<double>(s.cells.size(), 1), uniform_sweeps(s, .1, 0));
    check(plan.eos_backtracks > 0, "Cold compressed liquid did not exercise a shared EOS limiter");
    const auto r = advance_mac_parcel_transport(set.registry, s, cold, plan);
    for (const auto& c : r.state.cells) check(cold.evaluate(c.mixture, 1).temperature_k >= 300 - 1e-10, "Reconstructed cold liquid left EOS temperature range");
    ma::testing::export_transport(1, s, r, plan);
}
void zero_identity() {
    const auto& set = setup(); const auto s = set.state(4, 3, true, true); const auto held = fingerprint(s);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, s, std::vector<double>(s.cells.size(), 1), uniform_sweeps(s, 0, 0));
    const auto r = advance_mac_parcel_transport(set.registry, s, *set.eos, plan);
    check(plan.parcels.empty() && fingerprint(r.state) == held && fingerprint(s) == held, "Zero reconstructed volume flux changed primary state");
}
void invalid_plans() {
    const auto& set = setup(); const auto s = set.state(4, 3, true, true); const auto held = fingerprint(s);
    const auto plan = reconstruct_mac_parcels(set.registry, *set.eos, s, std::vector<double>(s.cells.size(), 1), uniform_sweeps(s, .125, .0625));
    for (int control = 0; control < 8; ++control) {
        auto bad = plan;
        if (control == 0) bad.parcels[0].target = bad.parcels[0].donor;
        if (control == 1) bad.parcels.push_back(bad.parcels[0]);
        if (control == 2) bad.parcels[0].contents.mixture.masses[set.registry.index("helium")] *= .1;
        if (control == 3) bad.retained[0].mixture.energy += .125;
        if (control == 4) bad.source_volumes_m3.pop_back();
        if (control == 5) bad.parcels[0].volume_m3 *= 1.01;
        if (control == 6) bad.parcels[0].contents.passive_quantities[0].quantity = -1;
        if (control == 7) bad.limiter[0] = 2;
        const auto before = fingerprint(bad); bool caught = false;
        try { static_cast<void>(advance_mac_parcel_transport(set.registry, s, *set.eos, bad)); } catch (const std::exception&) { caught = true; }
        check(caught && fingerprint(s) == held && fingerprint(bad) == before, "Malformed reconstructed plan changed inputs or was accepted");
    }
    auto trace = s; for (auto& c : trace.cells) c.passive_quantities = {{17, std::numeric_limits<double>::denorm_min()}};
    const auto before = fingerprint(trace); bool caught = false;
    try { static_cast<void>(reconstruct_mac_parcels(set.registry, *set.eos, trace, std::vector<double>(trace.cells.size(), 1), uniform_sweeps(trace, .125, 0))); }
    catch (const FluidFatalError& e) { caught = e.code() == FluidFatalCode::numerical_precision; }
    check(caught && fingerprint(trace) == before, "Unrepresentable positive passive transfer was erased");
    MacClosedSettings invalid; invalid.reconstruction = static_cast<MacMaterialReconstruction>(-1); caught = false;
    try { const MacClosedWorld world(set.registry, set.eos, s, invalid); } catch (const std::invalid_argument&) { caught = true; }
    check(caught && fingerprint(s) == held, "Invalid material reconstruction selector was admitted");
}
double wave(std::size_t n) {
    const auto& set = setup(); auto s = set.state(n, n, true); const double h = 1 / static_cast<double>(n); s.grid.spacing_m = h;
    const auto ni = set.registry.index("nitrogen"), he = set.registry.index("helium"); const double pi = std::numbers::pi, sinc = std::sin(pi * h) / (pi * h);
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        auto& c = s.cells[x + y * n]; std::vector<double> m(set.registry.size()); m[ni] = h * h; m[he] = h * h * 1e-28;
        c.mixture = set.eos->initialize_at_temperature(m, 300, h * h).mixture;
        const double sine = sinc * sinc * std::sin(2 * pi * (static_cast<double>(x + y) + 1) * h);
        c.passive_quantities = {{17, h * h * (1 + .2 * sine)}, {UINT64_C(0x80000000), h * h * 1e-30}};
    }
    const auto inertia = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t f = 0; f < inertia.x.size(); ++f) s.momentum_x[f] = .5 * inertia.x[f];
    for (std::size_t f = 0; f < inertia.y.size(); ++f) s.momentum_y[f] = .25 * inertia.y[f];
    constexpr double endpoint = .125; const double dt = .2 * h / .75; double time = 0; std::size_t steps = 0;
    const auto bound = static_cast<std::size_t>(std::ceil(endpoint / dt)) + 2;
    while (time < endpoint) {
        check(steps++ < bound, "Reconstructed wave exceeded its step bound"); const double step = std::min(dt, endpoint - time);
        auto plan = reconstruct_mac_parcels(set.registry, *set.eos, s, std::vector<double>(n * n, h * h), uniform_sweeps(s, step * h * .5, step * h * .25));
        auto r = advance_mac_parcel_transport(set.registry, s, *set.eos, plan); s = std::move(r.state); time += step;
    }
    double error = 0;
    for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        const auto& c = s.cells[x + y * n]; const double tag = c.passive_quantities[0].quantity / c.mixture.mass();
        const double exact = 1 + .2 * sinc * sinc * std::sin(2 * pi * ((static_cast<double>(x + y) + 1) * h - .75 * endpoint));
        error += h * h * std::abs(tag - exact); check(tag >= .8 - 1e-12 && tag <= 1.2 + 1e-12 && c.mixture.masses[he] > 0, "Oblique advection lost bounds or trace");
        if (y == 0) std::cout << "RSAMPLE " << n << ' ' << x << ' ' << tag << '\n';
    }
    std::cout << "RWAVE " << n << ' ' << steps << ' ' << error << '\n'; return error;
}
void refinement() {
    double previous = 0;
    for (auto n : {std::size_t(16), std::size_t(32), std::size_t(64)}) {
        const double error = wave(n); if (previous != 0) check(error < .45 * previous, "Reconstructed oblique tag fails its declared .45 refinement bound"); previous = error;
    }
}
}
int main() {
    try {
        std::cout << std::setprecision(17);
        for (const auto& m : setup().eos->reference().materials()) std::cout << "MATERIAL " << setup().registry.index(m.id) << ' ' << (m.phase == ReferencePhase::ideal_gas ? 0 : 1) << ' ' <<
            m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
        mixed_contacts(); std::cout << "PASS shared constituent/U limiter preserves mixed constant-p/T contacts\n";
        cold_liquid(); std::cout << "PASS cold compressed-liquid EOS slope limiting\n";
        zero_identity(); std::cout << "PASS exact zero-flux reconstructed identity\n";
        invalid_plans(); std::cout << "PASS eight malformed parcels, selector and unrepresentable positive trace rollback\n";
        refinement(); std::cout << "PASS oblique two-dimensional bounded passive refinement\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
