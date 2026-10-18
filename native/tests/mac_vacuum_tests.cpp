#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_native_heat.hpp"
#include <array>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void full(MacScaled value) { std::cout << ' ' << value.mantissa() << ' ' << value.exponent(); }
MacTransportState chamber(int axis, int direction, bool empty = false) {
    const auto& set = testing::setup();
    auto state = set.state(axis == 0 ? 12U : 2U, axis == 0 ? 2U : 12U, false);
    state.grid.periodic_x = axis == 1; state.grid.periodic_y = axis == 0;
    state.momentum_x.assign(state.grid.x_width() * state.grid.height, 0);
    state.momentum_y.assign(state.grid.width * state.grid.y_height(), 0);
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto coordinate = axis == 0 ? i % state.grid.width : i / state.grid.width;
        if (empty || (direction > 0 ? coordinate >= 3 : coordinate < 9))
            state.cells[i] = {MixtureInventory(set.registry.size()), {}};
    }
    return state;
}
void account(const MacClosedWorld& before, const MacClosedAdvance& advance) {
    const auto& set = testing::setup(); const auto& a = before.state(); const auto& b = advance.world.state();
    const auto ia = mac_face_inertias(set.registry, a, *set.eos), ib = mac_face_inertias(set.registry, b, *set.eos);
    for (std::size_t s = 0; s < set.registry.size(); ++s) {
        MacScaledAccount mass;
        for (std::size_t i = 0; i < a.cells.size(); ++i) { mass.add(b.cells[i].mixture.masses[s]); mass.add(-a.cells[i].mixture.masses[s]); }
        check(mass.within_bound(), "Vacuum transport changed a constituent");
    }
    MacEnergyAccount energy;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        energy.add(b.cells[i].mixture.energy); energy.add(-a.cells[i].mixture.energy);
        if (before.settings().gravity_m_per_s2 != 0) {
            const auto potential = MacEnergy::from_double(before.settings().gravity_m_per_s2)
                .multiplied(static_cast<double>(i / a.grid.width) + .5).multiplied(a.grid.spacing_m);
            energy.add(potential.multiplied(b.cells[i].mixture.mass())); energy.add(potential.multiplied(a.cells[i].mixture.mass()).negated());
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto ja = mac_field_values(axis == 0 ? a.momentum_x : a.momentum_y, axis == 0 ? a.full_momentum_x : a.full_momentum_y);
        const auto jb = mac_field_values(axis == 0 ? b.momentum_x : b.momentum_y, axis == 0 ? b.full_momentum_x : b.full_momentum_y);
        const auto& ma = axis == 0 ? ia.x : ia.y; const auto& mb = axis == 0 ? ib.x : ib.y;
        MacScaledAccount impulse;
        impulse.add((axis == 0 ? advance.report.full_wall_impulse_x : advance.report.full_wall_impulse_y).negated());
        if (axis == 1) impulse.add(advance.report.full_body_impulse_y.negated());
        for (std::size_t f = 0; f < ja.size(); ++f) {
            energy.add(MacEnergy::kinetic(mb[f], jb[f])); energy.add(MacEnergy::kinetic(ma[f], ja[f]).negated());
            impulse.add(jb[f]); impulse.add(ja[f].negated());
        }
        check(impulse.within_bound(), "Vacuum transport changed momentum without a boundary/body impulse");
    }
    check(energy.within_bound(), "Vacuum transport changed closed U+K+G");
}
void emit(int id, int endpoint, const MacClosedWorld& world) {
    const auto& state = world.state();
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto& cell = state.cells[i]; const auto reading = world.thermodynamics().evaluate(cell.mixture, state.grid.volume_m3());
        std::cout << "VCELL " << id << ' ' << endpoint << ' ' << i << ' ' << cell.mixture.energy << ' '
            << reading.mechanical_pressure_pa << ' ' << reading.temperature_k << ' ' << reading.entropy_j_per_k << '\n';
        for (std::size_t s = 0; s < cell.mixture.masses.size(); ++s) if (cell.mixture.masses[s] != 0)
            std::cout << "VMASS " << id << ' ' << endpoint << ' ' << i << ' ' << s << ' ' << cell.mixture.masses[s] << '\n';
        for (const auto& p : cell.passive_quantities)
            std::cout << "VPASSIVE " << id << ' ' << endpoint << ' ' << i << ' ' << p.identity << ' ' << p.quantity << '\n';
    }
    for (int axis = 0; axis < 2; ++axis) {
        const auto values = mac_field_values(axis == 0 ? state.momentum_x : state.momentum_y,
            axis == 0 ? state.full_momentum_x : state.full_momentum_y);
        for (std::size_t f = 0; f < values.size(); ++f) {
            std::cout << "VFACE " << id << ' ' << endpoint << ' ' << axis << ' ' << f; full(values[f]); std::cout << '\n';
        }
    }
}
void expansion(int id, int axis, int direction, int mode) {
    const auto& set = testing::setup(); auto state = chamber(axis, direction);
    MacClosedSettings settings; settings.maximum_timestep_s = 1e-4;
    if (mode == 1) {
        settings.reconstruction = MacMaterialReconstruction::swept_first_moment;
        settings.momentum_reconstruction = MacMomentumReconstruction::swept_energy_flux;
        settings.compression_viscosity = MacCompressionViscosity::coupled_pressure;
    }
    if (mode == 2) {
        settings.thermal_conduction = true; settings.viscosity = {1.8e-5, 1e-5};
        if (axis == 1) settings.gravity_m_per_s2 = 9.81;
    }
    const MacClosedWorld source(set.registry, set.eos, state, settings); const auto image = source.checkpoint();
    const auto direct = advance_mac_closed_substep(set.registry, *set.eos, state, 1e-4, settings);
    check(direct.active_cells.size() == 6 && direct.pressure.cells.size() == 6, "Vacuum acquired a pressure unknown");
    const auto next = source.advanced(2e-4); account(source, next);
    std::size_t arrived = 0, vacant = 0;
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const auto& cell = next.world.state().cells[i];
        if (cell.mixture.mass() == 0) { ++vacant; check(cell.mixture.energy == 0 && cell.passive_quantities.empty(), "Vacuum acquired an inventory"); }
        else {
            if (state.cells[i].mixture.mass() == 0) ++arrived;
            check(cell.mixture.masses[set.registry.index("helium")] > 0 && cell.passive_quantities.back().quantity > 0,
                "Gas arrival lost a positive trace/passive");
        }
    }
    check(arrived > 0 && vacant > 0 && next.report.rejected_attempts == 0, "Expansion did not retain exact empty cells or nominal steps");
    const auto restored = MacClosedWorld::from_checkpoint(set.registry, set.eos, next.world.checkpoint());
    check(restored.checkpoint() == next.world.checkpoint() && source.checkpoint() == image &&
        restored.advanced(1e-4).world.checkpoint() == next.world.advanced(1e-4).world.checkpoint(), "Vacuum continuation or immutable source differs");
    std::cout << "VGRID " << id << ' ' << state.grid.width << ' ' << state.grid.height << ' ' << state.grid.periodic_x << ' '
        << state.grid.periodic_y << ' ' << axis << ' ' << direction << ' ' << mode << ' ' << settings.gravity_m_per_s2 << '\n';
    emit(id, 0, source); emit(id, 1, next.world);
    std::cout << "VRESULT " << id << ' ' << arrived << ' ' << vacant << ' ' << next.report.substeps << ' ' << next.report.rejected_attempts;
    for (const auto& value : {next.report.full_wall_impulse_x, next.report.full_wall_impulse_y, next.report.full_body_impulse_y,
        next.report.energy_residual_wide_j, next.report.energy_bound_wide_j}) full(value);
    std::cout << ' ' << next.report.entropy_change_j_per_k << ' ' << next.report.entropy_roundoff_j_per_k << '\n';
}
void empty_and_invalid() {
    const auto& set = testing::setup(); auto empty = chamber(0, 1, true); empty.grid.periodic_y = false;
    empty.momentum_y.assign(empty.grid.width * empty.grid.y_height(), 0);
    const auto held = testing::fingerprint(empty); MacClosedSettings settings;
    settings.thermal_conduction = true; settings.viscosity = {1.8e-5, 1e-5}; settings.gravity_m_per_s2 = 9.81;
    const MacClosedWorld world(set.registry, set.eos, empty, settings);
    for (const auto policy : {MacExecutionPolicy::reference, MacExecutionPolicy::exact_equilibrium}) {
        const auto next = world.advanced(.001, 4096, policy);
        check(testing::fingerprint(next.world.state()) == held && world.time_s() == 0, "Entire vacuum changed canonical fields");
    }
    const auto contact = exchange_mac_native_heat(world, 0, {500, 10}, 1000, .1);
    check(contact.world.checkpoint() == world.checkpoint() && contact.body_temperature_k == 500 && contact.report.stored_fluid_heat_j == 0,
        "Native heat contact manufactured heat in vacuum");
    for (int mode = 0; mode < 4; ++mode) {
        auto bad = empty;
        if (mode == 0) bad.cells[0].mixture.energy = 1e-100;
        if (mode == 1) bad.cells[0].passive_quantities = {{17, 1e-100}};
        if (mode == 2) bad.momentum_x[1] = 1e-100;
        if (mode == 3) {
            std::vector<MacScaled> momentum(bad.momentum_x.size()); momentum[1] = MacScaled::from_parts(.5, -1200);
            store_mac_field(bad.momentum_x, bad.full_momentum_x, momentum);
        }
        const auto before = testing::fingerprint(bad); bool caught = false;
        try { const MacClosedWorld rejected(set.registry, set.eos, bad, settings); }
        catch (const std::exception&) { caught = true; }
        check(caught && testing::fingerprint(bad) == before, "Massless energy/passive/momentum was accepted or changed caller state");
    }
    const MacMassFluxes zero{std::vector<double>(empty.momentum_x.size()), std::vector<double>(empty.momentum_y.size())};
    const std::vector<double> volumes(empty.cells.size(), 1);
    const auto plan = reconstruct_mac_swept_parcels(set.registry, *set.eos, empty, volumes, zero);
    check(testing::fingerprint(advance_mac_parcel_transport(set.registry, empty, *set.eos, plan,
        MacMomentumReconstruction::swept_energy_flux).state) == held, "Vacuum parcel identity changed");
    auto invented = plan; invented.retained[0].mixture.masses[set.registry.index("helium")] = 1e-100;
    bool caught = false;
    try { static_cast<void>(advance_mac_parcel_transport(set.registry, empty, *set.eos, invented)); }
    catch (const std::exception&) { caught = true; }
    check(caught && testing::fingerprint(empty) == held, "Vacuum parcel invented a constituent");
    std::cout << "VEMPTY 2 4 1 1\n";
}
void closing_gap_rollback() {
    const auto& set = testing::setup(); auto state = set.state(3, 1, false);
    state.cells[1] = {MixtureInventory(set.registry.size()), {}};
    MacClosedSettings settings; settings.maximum_timestep_s = .01;
    settings.reconstruction = MacMaterialReconstruction::swept_first_moment;
    settings.momentum_reconstruction = MacMomentumReconstruction::swept_energy_flux;
    settings.compression_viscosity = MacCompressionViscosity::coupled_pressure;
    const MacClosedWorld source(set.registry, set.eos, state, settings); const auto image = source.checkpoint();
    bool caught = false;
    try { static_cast<void>(source.advanced(.01, 1)); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught && source.checkpoint() == image, "Exhausted vacuum-CFL attempt published a partial interval");
    const auto completed = source.advanced(.01, 4096); account(source, completed);
    check(completed.report.rejected_attempts > 0 && source.checkpoint() == image &&
        completed.world.state().cells[1].mixture.mass() > 0, "Vacuum closure did not retry and fill conservatively");
    std::cout << "VRETRY " << completed.report.rejected_attempts << " 1\n";
}
void complete_transport_depletion() {
    const auto& set = testing::setup(); auto source = set.state(5, 2, true); const auto& g = source.grid;
    for (std::size_t i = 0; i < source.cells.size(); ++i)
        if (i % g.width != 1) source.cells[i] = {MixtureInventory(set.registry.size()), {}};
    const auto inertia = mac_face_inertias(set.registry, source, *set.eos);
    for (std::size_t y = 0; y < g.height; ++y) for (const auto x : {1U, 2U})
        source.momentum_x[g.x_face(x, y)] = inertia.x[g.x_face(x, y)] * .125;
    auto expected = source;
    for (std::size_t y = 0; y < g.height; ++y) for (std::size_t x = 0; x < g.width; ++x) {
        const auto destination = (x + 1) % g.width;
        expected.cells[destination + y * g.width] = source.cells[x + y * g.width];
        expected.momentum_x[g.x_face(destination, y)] = source.momentum_x[g.x_face(x, y)];
        expected.momentum_y[g.y_face(destination, y)] = source.momentum_y[g.y_face(x, y)];
    }
    const auto held = testing::fingerprint(source);
    MacMassFluxes flux{std::vector<double>(source.momentum_x.size()), std::vector<double>(source.momentum_y.size())};
    for (std::size_t y = 0; y < g.height; ++y) flux.x[g.x_face(2, y)] = source.cells[1 + y * g.width].mixture.mass();
    const std::vector<double> volumes(source.cells.size(), 1);
    const auto shifted = advance_mac_transport(set.registry, source, flux, *set.eos, volumes);
    check(testing::fingerprint(source) == held && testing::fingerprint(shifted.state) == testing::fingerprint(expected),
        "Complete depletion lost a trace, left background inventory or changed the exact shift");
    MacParcelPlan plan; plan.source_volumes_m3 = volumes; plan.retained_volumes_m3.assign(source.cells.size(), 0);
    plan.limiter.assign(source.cells.size(), 1);
    for (std::size_t i = 0; i < source.cells.size(); ++i) {
        const auto x = i % g.width, y = i / g.width, destination = (x + 1) % g.width;
        plan.retained.push_back({MixtureInventory(set.registry.size()), {}});
        plan.parcels.push_back({0, g.x_face(destination, y), i, destination + y * g.width, 1, source.cells[i]});
    }
    for (const auto mode : {MacMomentumReconstruction::swept_physical, MacMomentumReconstruction::swept_energy_flux,
        MacMomentumReconstruction::swept_nodal_1d})
        check(testing::fingerprint(advance_mac_parcel_transport(set.registry, source, *set.eos, plan, mode).state) ==
            testing::fingerprint(expected), "Physical parcel depletion changed the exact inventory/momentum translation");
    auto divergent = set.state(7, 2, true);
    for (std::size_t i = 0; i < divergent.cells.size(); ++i)
        if (i % 7 < 1 || i % 7 > 4) divergent.cells[i] = {MixtureInventory(set.registry.size()), {}};
    const auto inertia2 = mac_face_inertias(set.registry, divergent, *set.eos);
    MacMassFluxes outward{std::vector<double>(divergent.momentum_x.size()), std::vector<double>(divergent.momentum_y.size())};
    for (std::size_t y = 0; y < 2; ++y) {
        for (std::size_t x = 1; x <= 5; ++x) divergent.momentum_x[divergent.grid.x_face(x, y)] =
            inertia2.x[divergent.grid.x_face(x, y)] * .25 * (static_cast<double>(x) - 1);
        outward.x[divergent.grid.x_face(2, y)] = -divergent.cells[2 + y * 7].mixture.mass();
        outward.x[divergent.grid.x_face(4, y)] = divergent.cells[3 + y * 7].mixture.mass();
    }
    const MacClosedWorld original(set.registry, set.eos, divergent);
    const std::vector<double> volumes2(divergent.cells.size(), 1);
    for (const auto mode : {MacMomentumReconstruction::limited_linear, MacMomentumReconstruction::quadratic_limited_linear}) {
        const auto depleted = advance_mac_transport(set.registry, divergent, outward, *set.eos, volumes2, mode);
        check(!depleted.report.x.corrections.empty(), "Depletion control bypassed reconstruction everywhere");
        for (std::size_t y = 0; y < 2; ++y) for (const auto x : {2U, 3U}) {
            const auto& cell = depleted.state.cells[x + y * 7];
            check(cell.mixture.mass() == 0 && cell.mixture.energy == 0 && cell.passive_quantities.empty(), "Depletion left background inventory");
        }
        const MacClosedAdvance completed{MacClosedWorld(set.registry, set.eos, depleted.state), {}}; account(original, completed);
    }
    std::cout << "VDEPLETION 1 6\n";
}
}
int main() { try {
    std::cout << std::setprecision(17); const auto& set = testing::setup();
    for (const auto& m : set.eos->reference().materials())
        std::cout << "VMATERIAL " << set.registry.index(m.id) << ' ' << (m.phase == ReferencePhase::ideal_gas ? 0 : 1) << ' '
            << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' '
            << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
    int id = 0;
    for (int axis = 0; axis < 2; ++axis) for (int direction : {-1, 1}) for (int mode = 0; mode < 3; ++mode)
        expansion(id++, axis, direction, mode);
    empty_and_invalid(); closing_gap_rollback(); complete_transport_depletion(); std::cout << "VCOMPLETE " << id << '\n';
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }
