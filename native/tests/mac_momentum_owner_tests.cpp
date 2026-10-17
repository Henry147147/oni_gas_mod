#include "mac_fixture.hpp"
#include <array>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double dt = 1e-5;
auto mode = MacMomentumReconstruction::limited_linear;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
bool fixed(const MacGrid& g, int axis, std::size_t f) {
    const auto x = axis == 0 ? f % g.x_width() : f % g.width;
    const auto y = axis == 0 ? f / g.x_width() : f / g.width;
    if (axis == 0) return ((!g.periodic_x && (x == 0 || x == g.width)) ||
        g.walls[(x + g.width - 1) % g.width + y * g.width] || g.walls[x % g.width + y * g.width]);
    return ((!g.periodic_y && (y == 0 || y == g.height)) ||
        g.walls[x + ((y + g.height - 1) % g.height) * g.width] || g.walls[x + (y % g.height) * g.width]);
}
MacTransportState source(bool walls = false) {
    const auto& set = ma::testing::setup(); auto s = set.state(6, walls ? 5 : 4, !walls, true, walls);
    for (std::size_t i = 0; i < s.cells.size(); ++i) if (!s.grid.walls[i])
        s.cells[i].mixture = set.mixture(1, 100000 * (1 + .03 * std::sin(static_cast<double>(i))),
            300 + 2 * std::cos(static_cast<double>(i)), .2 + .05 * std::sin(static_cast<double>(i)));
    const auto m = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t i = 0; i < s.momentum_x.size(); ++i) if (!fixed(s.grid, 0, i))
        s.momentum_x[i] = m.x[i] * (.25 + .125 * std::sin(static_cast<double>(i) + .2));
    for (std::size_t i = 0; i < s.momentum_y.size(); ++i) if (!fixed(s.grid, 1, i))
        s.momentum_y[i] = m.y[i] * (-.25 + .125 * std::cos(static_cast<double>(i) + .3));
    return s;
}
// Isolate the pressure/momentum reference; full default heat coupling has its
// own owning tests, including the same accepted-stage and atomicity checks.
MacClosedSettings settings() {
    MacClosedSettings s; s.maximum_timestep_s = dt; s.shock_heat = MacShockHeatRegularization::none;
    s.compression_viscosity = MacCompressionViscosity::quadratic; s.momentum_reconstruction = mode; return s;
}
void positive(const MacTransportState& s) {
    const auto& set = ma::testing::setup();
    for (std::size_t i = 0; i < s.cells.size(); ++i) if (!s.grid.walls[i]) {
        const auto& c = s.cells[i];
        check(set.eos->evaluate(c.mixture, s.grid.volume_m3()).temperature_k > 0 &&
            c.mixture.masses[set.registry.index("helium")] > 0 && !c.passive_quantities.empty() && c.passive_quantities.back().quantity > 0,
            "Owner momentum reconstruction lost EOS admissibility or positive trace inventory");
    }
}
void default_and_material() {
    const auto& set = ma::testing::setup(); const auto s = source(); const auto held = ma::testing::fingerprint(s);
    const auto options = settings(); const auto step = advance_mac_closed_substep(set.registry, *set.eos, s, dt, options);
    check(MacClosedSettings{}.momentum_reconstruction == MacMomentumReconstruction::limited_linear &&
        options.momentum_reconstruction == mode && step.reconstruction.has_value() &&
        !step.transport.x.corrections.empty() && !step.transport.y.corrections.empty(), "Default owner did not reconstruct both momentum components");
    const auto direct = advance_mac_parcel_transport(set.registry, step.pressure_state, *set.eos, *step.reconstruction, mode);
    check(ma::testing::fingerprint(step.state) == ma::testing::fingerprint(direct.state) && ma::testing::fingerprint(s) == held,
        "Owner parcel momentum mode differs from the actual transport component or changed its source");
    positive(step.state); ma::testing::export_case(0, s, step, dt);
}
void gravity_and_walls() {
    const auto& set = ma::testing::setup(); const auto s = source(true); auto options = settings();
    options.gravity_m_per_s2 = 9.81; options.reconstruction = MacMaterialReconstruction::donor_cell;
    const auto held = ma::testing::fingerprint(s); const auto r = advance_mac_closed_substep(set.registry, *set.eos, s, dt, options);
    check(!r.reconstruction.has_value() && !r.transport.x.corrections.empty() && !r.transport.y.corrections.empty() &&
        ma::testing::fingerprint(s) == held, "Raw-material gravity/wall path bypassed reconstructed momentum or changed source");
    positive(r.state); ma::testing::export_case(1, s, r, dt);
    std::cout << "GCASE 1 " << options.gravity_m_per_s2 << ' ' << r.gravity_internal_residual_j << ' ' << r.gravity_internal_bound_j << ' '
        << r.potential_transport_residual_j << ' ' << r.potential_transport_bound_j << ' ' << r.body_impulse_y << ' ' << r.gravity_internal_change_j << ' '
        << r.entropy_change_j_per_k << ' ' << r.entropy_roundoff_j_per_k << '\n';
    for (std::size_t f = 0; f < r.gravity_body_work_y.size(); ++f) std::cout << "GFACE 1 " << f << ' ' << r.gravity_body_work_y[f] << ' '
        << r.gravity_potential_flux_y[f] << ' ' << r.gravity_internal_work_y[f] << '\n';
    for (std::size_t i = 0; i < s.cells.size(); ++i) std::cout << "GUCELL 1 " << i << ' ' << r.pre_gravity_internal_energy_j[i] << ' '
        << r.state.cells[i].mixture.energy << ' ' << r.gravity_internal_changes_j[i] << '\n';
}
void owner_state(int endpoint, const MacTransportState& s) {
    const auto& set = ma::testing::setup(); const auto m = mac_face_inertias(set.registry, s, *set.eos);
    for (std::size_t i = 0; i < s.cells.size(); ++i) {
        const auto& c = s.cells[i]; std::cout << "OSTATE " << endpoint << ' ' << i << ' ' << c.mixture.energy << '\n';
        for (std::size_t k = 0; k < c.mixture.masses.size(); ++k) if (c.mixture.masses[k] != 0)
            std::cout << "OMASS " << endpoint << ' ' << i << ' ' << k << ' ' << c.mixture.masses[k] << '\n';
        for (const auto& p : c.passive_quantities) std::cout << "OPASSIVE " << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto j = axis == 0 ? mac_field_values(s.momentum_x, s.full_momentum_x) : mac_field_values(s.momentum_y, s.full_momentum_y);
        const auto& inertia = axis == 0 ? m.x : m.y;
        for (std::size_t i = 0; i < j.size(); ++i) std::cout << "OFACE " << endpoint << ' ' << axis << ' ' << i << ' ' << inertia[i] << ' '
            << j[i].mantissa() << ' ' << j[i].exponent() << '\n';
    }
}
void clock_and_reports() {
    const auto& set = ma::testing::setup(); const auto s = source(); auto options = settings(); options.compression_viscosity = MacCompressionViscosity::none;
    const MacClosedWorld owner(set.registry, set.eos, s, options); const auto held = ma::testing::fingerprint(owner.state());
    auto manual = s; std::array<std::size_t, 4> counts{};
    for (int k = 0; k < 2; ++k) {
        auto step = advance_mac_closed_substep(set.registry, *set.eos, manual, dt, options);
        ma::testing::export_case(2 + k, manual, step, dt);
        for (const auto* f : {&step.transport.x, &step.transport.y}) {
            counts[0] += f->corrections.size(); counts[1] += f->limited_corrections;
            counts[2] += f->unresolved_momentum_corrections; counts[3] += f->unresolved_remainder_corrections;
        }
        manual = std::move(step.state);
    }
    const auto result = owner.advanced(2 * dt, 4); const auto& r = result.report;
    check(r.substeps == 2 && r.attempts == 2 && r.rejected_attempts == 0 && counts[0] > 0 &&
        counts == std::array<std::size_t, 4>{r.momentum_correction_events, r.limited_momentum_corrections,
            r.unresolved_momentum_corrections, r.unresolved_remainder_corrections}, "Accepted owner correction diagnostics do not match its actual substeps");
    check(ma::testing::fingerprint(result.world.state()) == ma::testing::fingerprint(manual) && owner.time_s() == 0 && owner.revision() == 1 &&
        result.world.time_s() == 2 * dt && result.world.revision() == 2 && ma::testing::fingerprint(owner.state()) == held,
        "Owning correction interval changed source, replay, clock or revision");
    positive(result.world.state()); owner_state(0, owner.state()); owner_state(1, result.world.state());
    std::cout << "OINTERVAL " << dt << ' ' << 2 * dt << ' ' << result.world.time_s() << ' ' << result.world.revision() << ' '
        << r.attempts << ' ' << r.substeps << ' ' << r.rejected_attempts << ' ' << r.momentum_correction_events << ' ' << r.limited_momentum_corrections << ' '
        << r.unresolved_momentum_corrections << ' ' << r.unresolved_remainder_corrections;
    for (const auto& value : {r.energy_residual_wide_j, r.energy_bound_wide_j}) std::cout << ' ' << value.mantissa() << ' ' << value.exponent();
    std::cout << ' ' << r.entropy_change_j_per_k << ' ' << r.entropy_roundoff_j_per_k << '\n';
}
void donor_reference() {
    const auto& set = ma::testing::setup(); const auto s = source(); auto options = settings(); options.momentum_reconstruction = MacMomentumReconstruction::donor_cell;
    const auto donor = advance_mac_closed_substep(set.registry, *set.eos, s, dt, options);
    const auto corrected = advance_mac_closed_substep(set.registry, *set.eos, s, dt, settings());
    const auto direct = advance_mac_parcel_transport(set.registry, donor.pressure_state, *set.eos, *donor.reconstruction);
    check(ma::testing::fingerprint(donor.state) == ma::testing::fingerprint(direct.state) && donor.transport.x.corrections.empty() && donor.transport.y.corrections.empty() &&
        ma::testing::fingerprint(donor.pressure_state) == ma::testing::fingerprint(corrected.pressure_state) &&
        donor.mass_fluxes_kg.x == corrected.mass_fluxes_kg.x && donor.mass_fluxes_kg.y == corrected.mass_fluxes_kg.y,
        "Explicit donor reference changed the pressure/material path");
    bool changed = false;
    for (std::size_t i = 0; i < s.cells.size(); ++i) {
        const auto& a = donor.state.cells[i]; const auto& b = corrected.state.cells[i];
        check(a.mixture.masses == b.mixture.masses && a.passive_quantities.size() == b.passive_quantities.size(), "Owner momentum mode changed constituent/passive transfers");
        for (std::size_t k = 0; k < a.passive_quantities.size(); ++k) check(a.passive_quantities[k].identity == b.passive_quantities[k].identity &&
            a.passive_quantities[k].quantity == b.passive_quantities[k].quantity, "Owner momentum mode changed actual passive transfer");
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto& a = axis == 0 ? donor.transport.x : donor.transport.y; const auto& b = axis == 0 ? corrected.transport.x : corrected.transport.y;
        for (std::size_t i = 0; i < a.energies.size(); ++i) check(a.full_wall_impulse[i].mantissa() == b.full_wall_impulse[i].mantissa() &&
            a.full_wall_impulse[i].exponent() == b.full_wall_impulse[i].exponent() && a.energies[i].wall_heat.mantissa() == b.energies[i].wall_heat.mantissa() &&
            a.energies[i].wall_heat.exponent() == b.energies[i].wall_heat.exponent(), "Owner correction changed wall reaction or wall heat");
    }
    changed = donor.state.momentum_x != corrected.state.momentum_x || donor.state.momentum_y != corrected.state.momentum_y;
    check(changed, "Explicit default/donor comparison did not exercise a momentum change"); ma::testing::export_case(4, s, donor, dt);
}
void full_range() {
    const auto& set = ma::testing::setup(); auto s = set.state(6, 3, true); const auto m = mac_face_inertias(set.registry, s, *set.eos);
    std::vector<MacScaled> j(s.momentum_x.size()); const auto unit = MacScaled::from_parts(.75, -1200);
    for (std::size_t i = 0; i < j.size(); ++i) j[i] = unit.multiplied(1 + .125 * std::sin(static_cast<double>(i % 6))).multiplied(m.x[i]);
    store_mac_field(s.momentum_x, s.full_momentum_x, j); const auto held = ma::testing::fingerprint(s);
    const auto r = advance_mac_closed_substep(set.registry, *set.eos, s, dt, settings());
    check(!r.state.full_momentum_x.empty() && !r.volume_fluxes_m3.full_x.empty() && !r.mass_fluxes_kg.full_x.empty() &&
        !r.transport.x.corrections.empty() && r.transport.x.unresolved_momentum_corrections > 0 && r.transport.unresolved_heat_updates > 0 &&
        ma::testing::fingerprint(s) == held, "Owner discarded full-range correction/sweep or unresolved update diagnostics");
    positive(r.state); ma::testing::export_case(5, s, r, dt);
}
void rollback_and_invalid() {
    const auto& set = ma::testing::setup(); const auto s = source(); const auto held = ma::testing::fingerprint(s); auto invalid = settings();
    invalid.momentum_reconstruction = static_cast<MacMomentumReconstruction>(99); std::size_t rejected = 0;
    try { static_cast<void>(advance_mac_closed_substep(set.registry, *set.eos, s, dt, invalid)); } catch (const std::invalid_argument&) { ++rejected; }
    try { const MacClosedWorld bad(set.registry, set.eos, s, invalid); } catch (const std::invalid_argument&) { ++rejected; }
    auto options = settings(); options.compression_viscosity = MacCompressionViscosity::none; const MacClosedWorld owner(set.registry, set.eos, s, options);
    bool failed = false; try { static_cast<void>(owner.advanced(2 * dt, 1)); } catch (const std::runtime_error&) { failed = true; }
    check(rejected == 2 && failed && owner.time_s() == 0 && owner.revision() == 1 && ma::testing::fingerprint(owner.state()) == held &&
        ma::testing::fingerprint(s) == held, "Invalid momentum setting or later failure changed caller/owner state");
    const auto a = owner.advanced(2 * dt, 4), b = owner.advanced(2 * dt, 4);
    check(a.report.substeps == 2 && ma::testing::fingerprint(a.world.state()) == ma::testing::fingerprint(b.world.state()), "A failed corrected interval left cached partial state");
    std::cout << "OROLLBACK " << rejected << ' ' << failed << ' ' << owner.time_s() << ' ' << owner.revision() << '\n';
}
void work_capacity() {
    const auto one = MacScaled::from_double(1), half = MacEnergy::from_double(.5);
    for (int id = 0; id < 4; ++id) {
        MacEnergySum left, right;
        if (id == 0) { left.add(1); left.add(MacEnergy::from_parts(-.5, -53)); right.add(1); }
        if (id == 1) { left.add(1); right.add(1); right.add(MacEnergy::from_parts(-.5, -53)); }
        if (id == 2) { left.add(MacEnergy::from_parts(.75, -1200)); right.add(1); }
        if (id == 3) right.add(1);
        const MacMomentumWorkBudget budget{left, right}; const auto requested = id == 1 ? one.negated() : one;
        const auto p = limit_mac_momentum_pair(1, 1, one, one, half, half, requested, &budget);
        auto a = left, b = right; a.add(p.energy_flux.negated()); b.add(p.energy_flux);
        check(a.sign() >= 0 && b.sign() >= 0 && p.after_heat_left.mantissa() >= 0 && p.after_heat_right.mantissa() >= 0,
            "Correction exceeded actual transported work or heat capacity");
        if (id < 2) check(p.limiter_roundoff_backoffs > 0 && p.limiter.project().value < 1,
            "Rounded-up transported budget was treated as its exact expansion");
        if (id == 2) check(p.limiter.mantissa() > 0 && p.limiter.project().rounded && p.impulse.mantissa() > 0 &&
            p.unresolved_momentum_updates > 0, "Below-binary64 work capacity was projected away");
        if (id == 3) check(p.impulse.is_zero() && p.energy_flux.is_zero() && p.after_left.mantissa() == one.mantissa() &&
            p.after_left.exponent() == one.exponent() && p.after_heat_left.mantissa() == half.mantissa() &&
            p.after_heat_left.exponent() == half.exponent(), "Zero transported capacity was not an exact no-op");
        std::cout << "OWORK " << id << " 1 1 " << p.limiter_roundoff_backoffs << ' ' << p.unresolved_momentum_updates << ' ' << p.unresolved_heat_updates;
        for (const auto& v : {p.before_left,p.before_right,p.before_heat_left,p.before_heat_right,p.requested_impulse,p.limiter,p.impulse,
            p.after_left,p.after_right,p.after_heat_left,p.after_heat_right,p.energy_flux,p.heat_change_left,p.heat_change_right})
            std::cout << ' ' << v.mantissa() << ' ' << v.exponent();
        std::cout << '\n';
        for (int side = 0; side < 2; ++side) {
            const auto& terms = side == 0 ? left : right;
            for (std::size_t k = 0; k < terms.size(); ++k) { const auto v = terms.component(k);
                std::cout << "OWORKTERM " << id << ' ' << side << ' ' << k << ' ' << v.mantissa() << ' ' << v.exponent() << '\n';
            }
        }
    }
    MacEnergySum bad, good; bad.add(-1); good.add(1); const MacMomentumWorkBudget invalid{bad, good}; bool rejected = false;
    try { static_cast<void>(limit_mac_momentum_pair(1, 1, one, one, half, half, one, &invalid)); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && bad.sign() < 0 && good.sign() > 0, "Invalid work budget accepted or changed its inputs");
    std::cout << "OWORK_INVALID 1\n";
}
}
int main(int argc, char** argv) { try {
    if (argc == 2 && std::string(argv[1]) == "--quadratic") mode = MacMomentumReconstruction::quadratic_limited_linear;
    else check(argc == 1, "Invalid momentum owner control arguments");
    require_mac_energy_environment(); std::cout << std::setprecision(17);
    const auto& set = ma::testing::setup();
    std::cout << "OTRACE " << set.registry.index("helium") << ' ' << UINT64_C(0x80000000) << '\n';
    for (const auto& m : set.eos->reference().materials()) std::cout << "MATERIAL " << set.registry.index(m.id) << ' '
        << (m.phase == ReferencePhase::ideal_gas ? 0 : 1) << ' ' << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' '
        << m.energy_offset_j_per_kg << ' ' << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    default_and_material(); std::cout << "PASS default reconstructed owner with nonuniform material parcels\n";
    gravity_and_walls(); std::cout << "PASS reconstructed raw-material gravity and solid walls\n";
    clock_and_reports(); std::cout << "PASS accepted correction diagnostics and owning clock\n";
    donor_reference(); std::cout << "PASS explicit donor momentum reference and unchanged material transfers\n";
    full_range(); std::cout << "PASS full-range owning sweeps corrections and unresolved heat\n";
    rollback_and_invalid(); std::cout << "PASS invalid mode and later whole-interval rollback\n";
    work_capacity(); std::cout << "PASS exact transported-work budgets and full-range limiting\n"; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
