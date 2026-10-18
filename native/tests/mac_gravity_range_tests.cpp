#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
constexpr double dt = 0x1p-10;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void scaled(MacScaled value) { std::cout << ' ' << value.mantissa() << ' ' << value.exponent(); }

MacTransportState atmosphere(double mass, bool layered) {
    const auto& set = ma::testing::setup(); auto state = set.state(2, 2, false);
    state.grid.periodic_x = true; state.momentum_x.assign(4, 0);
    for (std::size_t i = 0; i < state.cells.size(); ++i) {
        const double amount = mass * (layered && i >= 2 ? 2 : 1);
        std::vector<double> masses(set.registry.size());
        masses[set.registry.index("nitrogen")] = amount;
        masses[set.registry.index("helium")] = amount * 1e-28;
        state.cells[i].mixture = set.eos->initialize_at_temperature(masses, layered && i >= 2 ? 150 : 300, 1).mixture;
        state.cells[i].passive_quantities = {{17, amount}, {UINT64_C(0x80000000), amount * 1e-30}};
    }
    return state;
}

void run(int id, double mass, double gravity, bool layered, MacScaled initial) {
    const auto& set = ma::testing::setup(); auto source = atmosphere(mass, layered);
    std::vector<MacScaled> momentum(source.momentum_y.size());
    for (std::size_t x = 0; x < source.grid.width; ++x) momentum[source.grid.y_face(x, 1)] = initial;
    store_mac_field(source.momentum_y, source.full_momentum_y, momentum);
    const auto held = ma::testing::fingerprint(source);
    MacClosedSettings settings; settings.gravity_m_per_s2 = gravity;
    const auto step = advance_mac_closed_substep(set.registry, *set.eos, source, dt, settings);
    const MacClosedWorld world(set.registry, set.eos, source, settings);
    const auto checkpoint = world.checkpoint();
    const auto restored = MacClosedWorld::from_checkpoint(set.registry, set.eos, checkpoint);
    check(restored.checkpoint() == checkpoint, "Range gravity checkpoint changed the source identity");
    const auto first = world.advanced(dt, 8);
    const auto resumed = restored.advanced(dt, 8);
    check(first.world.checkpoint() == resumed.world.checkpoint(), "Range gravity restored source changed continuation");
    const auto endpoint = MacClosedWorld::from_checkpoint(set.registry, set.eos, first.world.checkpoint());
    const auto uninterrupted = first.world.advanced(dt, 8), continued = endpoint.advanced(dt, 8);
    check(uninterrupted.world.checkpoint() == continued.world.checkpoint(), "Range gravity endpoint changed continuation");
    check(first.report.substeps == 1 && first.report.rejected_attempts == 0 &&
        uninterrupted.report.substeps == 1 && uninterrupted.report.rejected_attempts == 0,
        "Range gravity did not follow the declared nominal path");
    check(world.checkpoint() == checkpoint && ma::testing::fingerprint(source) == held,
        "Range gravity mutated the source or original owner");
    check(first.report.full_body_impulse_y.mantissa() < 0 && step.full_body_impulse_y.mantissa() < 0,
        "Range gravity lost the body impulse");
    if (id != 0) {
        const auto f = source.grid.y_face(0, 1);
        check(step.full_gravity_body_work_y[f].mantissa() > 0 && step.gravity_body_work_y[f] == 0 &&
            step.full_gravity_potential_flux_y[f].mantissa() < 0 && step.gravity_potential_flux_y[f] == 0 &&
            !step.mass_fluxes_kg.full_y.empty() && step.rounded_gravity_projections > 0,
            "Range gravity narrowed a nonzero work or actual mass flux");
        check(step.body_impulse_y == 0 && first.report.body_impulse_y == 0,
            "Range gravity fixture did not exercise a below-range body impulse");
        if (id == 1) check(step.unresolved_gravity_internal_updates > 0,
            "Layered range gravity did not report unresolved primary-U changes");
        if (id == 2) check(!step.volume_fluxes_m3.full_y.empty() && step.rounded_pressure_velocity_inputs > 0,
            "Range gravity fixture did not exercise the full sweep and incoming velocity paths");
    }
    for (const auto& cell : step.state.cells)
        check(cell.mixture.masses[set.registry.index("helium")] > 0 && cell.passive_quantities.back().quantity > 0,
            "Range gravity lost a positive constituent or passive trace");
    ma::testing::export_case(id, source, step, dt);
    std::cout << "GRAVITYRANGE " << id << ' ' << gravity << ' ' << layered << ' '
        << step.unresolved_gravity_internal_updates << ' ' << step.rounded_gravity_projections;
    for (const auto& value : {step.gravity_internal_residual_wide_j, step.gravity_internal_bound_wide_j,
        step.potential_transport_residual_wide_j, step.potential_transport_bound_wide_j,
        step.full_body_impulse_y, step.gravity_internal_change_wide_j,
        step.full_pressure_wall_impulse_x, step.full_pressure_wall_impulse_y}) scaled(value);
    std::cout << ' ' << step.entropy_change_j_per_k << ' ' << step.entropy_roundoff_j_per_k << '\n';
    for (std::size_t f = 0; f < step.gravity_body_work_y.size(); ++f) {
        std::cout << "GRAVITYFACE " << id << ' ' << f;
        for (const auto& value : {step.full_gravity_body_work_y[f], step.full_gravity_potential_flux_y[f],
            step.full_gravity_internal_work_y[f]}) scaled(value);
        std::cout << '\n';
    }
    for (std::size_t i = 0; i < step.state.cells.size(); ++i) {
        std::cout << "GRAVITYCELL " << id << ' ' << i << ' ' << step.pre_gravity_internal_energy_j[i] << ' '
            << step.state.cells[i].mixture.energy;
        scaled(step.full_gravity_internal_changes_j[i]); std::cout << '\n';
    }
    std::cout << "GRAVITYOWNER " << id << ' ' << first.report.substeps << ' ' << first.report.rejected_attempts << ' '
        << first.world.time_s() << ' ' << first.world.revision();
    scaled(first.report.full_body_impulse_y); scaled(first.report.gravity_internal_change_wide_j);
    std::cout << ' ' << first.report.unresolved_gravity_internal_updates << ' ' << first.report.rounded_gravity_projections << '\n';
}

void rollback() {
    const auto& set = ma::testing::setup(); auto source = atmosphere(0x1p-600, true);
    MacClosedSettings settings; settings.gravity_m_per_s2 = 0x1p-600;
    const MacClosedWorld world(set.registry, set.eos, source, settings); const auto held = world.checkpoint();
    bool rejected = false;
    try { static_cast<void>(world.advanced(.03, 1)); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected && world.checkpoint() == held, "Late range gravity rejection published a candidate");
    auto invalid = source; invalid.grid.periodic_y = true; invalid.momentum_y.assign(4, 0);
    rejected = false;
    try { static_cast<void>(MacClosedWorld(set.registry, set.eos, invalid, settings)); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Range gravity accepted a vertically periodic potential");
}

void long_continuation() {
    const auto& set = ma::testing::setup(); const auto source = atmosphere(0x1p-600, true);
    MacClosedSettings settings; settings.gravity_m_per_s2 = 0x1p-600;
    MacClosedWorld world(set.registry, set.eos, source, settings);
    const auto original = world.checkpoint(); constexpr std::size_t steps = 10000;
    const auto inertia = mac_face_inertias(set.registry, source, *set.eos);
    const auto face = source.grid.y_face(0, 1);
    for (std::size_t i = 0; i < steps; ++i) {
        auto next = world.advanced(dt, 2);
        check(next.report.substeps == 1 && next.report.rejected_attempts == 0,
            "Long range gravity continuation retried a nominal step");
        if (i == steps / 2) {
            const auto resumed = MacClosedWorld::from_checkpoint(set.registry, set.eos, world.checkpoint()).advanced(dt, 2);
            check(resumed.world.checkpoint() == next.world.checkpoint(), "Mid-run range gravity restore changed its next step");
            next = resumed;
        }
        world = std::move(next.world);
    }
    check(world.time_s() == steps * dt && world.revision() == steps + 1,
        "Long range gravity continuation lost its clock or revision");
    for (std::size_t i = 0; i < source.cells.size(); ++i) {
        const auto& a = world.state().cells[i].passive_quantities; const auto& b = source.cells[i].passive_quantities;
        check(world.state().cells[i].mixture.masses == source.cells[i].mixture.masses &&
            world.state().cells[i].mixture.energy == source.cells[i].mixture.energy && a.size() == b.size() &&
            std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
                return x.identity == y.identity && x.quantity == y.quantity;
            }),
            "Long range gravity continuation changed primary U/inventory or a positive trace");
    }
    const auto momentum = mac_field_values(world.state().momentum_y, world.state().full_momentum_y);
    const auto expected = MacScaled::from_double(-inertia.y[face]).multiplied(settings.gravity_m_per_s2).multiplied(steps * dt);
    MacScaledAccount impulse; impulse.add(momentum[face]); impulse.add(expected.negated());
    check(impulse.within_bound() && !momentum[face].is_zero() && momentum[face].project().value == 0,
        "Long range gravity continuation lost the accumulated canonical impulse");
    MacEnergyAccount energy;
    for (std::size_t i = 0; i < source.cells.size(); ++i) {
        energy.add(world.state().cells[i].mixture.energy); energy.add(-source.cells[i].mixture.energy);
    }
    for (std::size_t f = 0; f < momentum.size(); ++f) energy.add(MacEnergy::kinetic(inertia.y[f], momentum[f]));
    check(energy.within_bound(), "Long range gravity continuation exceeded the closed energy bound");
    check(MacClosedWorld(set.registry, set.eos, source, settings).checkpoint() == original,
        "Long range gravity continuation changed its original source");
    std::cout << "GRAVITYLONG " << steps << ' ' << world.time_s() << ' ' << world.revision();
    for (const auto& value : {momentum[face], expected, energy.residual(), energy.bound()}) scaled(value);
    std::cout << '\n';
}
}

int main() {
    try {
        require_mac_energy_environment(); std::cout << std::setprecision(17); const auto& set = ma::testing::setup();
        for (const auto& m : set.eos->reference().materials())
            std::cout << "MATERIAL " << set.registry.index(m.id) << ' ' << static_cast<int>(m.phase) << ' '
                << m.molar_mass_kg_per_mol << ' ' << m.cv_j_per_kg_k << ' ' << m.energy_offset_j_per_kg << ' '
                << m.zero_pressure_density_kg_per_m3 << ' ' << m.reference_bulk_modulus_pa << '\n';
        run(0, 1, 9.81, false, MacScaled::from_parts(.75, -1941));
        std::cout << "PASS incoming full vertical momentum with ordinary gravity\n";
        run(1, 0x1p-600, 0x1p-600, true, {});
        std::cout << "PASS below-range body impulse work and actual-mass potential transport\n";
        run(2, 1, std::numeric_limits<double>::denorm_min(), false, MacScaled::from_parts(.75, -1941));
        std::cout << "PASS below-range vertical velocity sweep and mass flux under minimum gravity\n";
        rollback(); std::cout << "PASS full gravity checkpoint continuation and atomic late rollback\n";
        long_continuation(); std::cout << "PASS ten thousand closed range-gravity steps and mid-run restore\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
