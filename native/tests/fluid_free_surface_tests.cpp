#include "mixed_atmospheres/fluid_world.hpp"
#include "mixed_atmospheres/volume_work.hpp"
#include "mixed_atmospheres/transport.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void near(double actual, double expected, double relative = 1e-11, double absolute = 0) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > absolute + relative * std::abs(expected))
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}
template<class F> void rejects(F&& operation) {
    bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid operation was accepted");
}
template<class F> void fatal(ma::FluidFatalCode code, F&& operation) {
    bool rejected = false;
    try { operation(); } catch (const ma::FluidFatalError& error) { rejected = error.code() == code; }
    check(rejected, "Expected typed fatal failure");
}
ma::FluidSolverSettings quiet(double gravity = 0) {
    ma::FluidSolverSettings s; s.gravity = gravity; s.diffusivity_m2_per_s = 0;
    s.viscosity_m2_per_s = 0; s.conduction = false; return s;
}
std::shared_ptr<const ma::FluidThermodynamics> model(const ma::SpeciesRegistry& registry,
    double bulk_modulus = 2.2e9, std::string version = "free-surface-tests-v1") {
    std::vector<ma::experimental::ReferenceMaterial> materials;
    for (const std::string id : {"water", "liquid_nitrogen", "oxygen", "nitrogen", "helium"}) {
        const auto& species = registry[registry.index(id)];
        ma::experimental::ReferenceMaterial material;
        material.id = id;
        material.phase = species.phase == ma::Phase::gas ? ma::experimental::ReferencePhase::ideal_gas :
            ma::experimental::ReferencePhase::elastic_liquid;
        material.molar_mass_kg_per_mol = species.molar_mass; material.cv_j_per_kg_k = species.cv();
        material.energy_offset_j_per_kg = species.internal_energy(0);
        material.minimum_temperature_k = 1e-20; material.maximum_temperature_k = 1e9;
        material.zero_pressure_density_kg_per_m3 = species.phase == ma::Phase::gas ? 0 : (id == "water" ? 1000 : species.density);
        material.reference_bulk_modulus_pa = species.phase == ma::Phase::gas ? 0 : bulk_modulus;
        material.maximum_density_ratio = species.phase == ma::Phase::gas ? 1 : 2;
        material.provenance = "Explicit zero-expansion test surrogate; not calibrated real water";
        materials.push_back(std::move(material));
    }
    return std::make_shared<const ma::FluidThermodynamics>(registry, std::move(version), std::move(materials));
}
ma::FluidWorld world(std::size_t count, ma::FluidAxis axis, double gravity = 0, double dx = 1) {
    auto registry = ma::SpeciesRegistry::standard(); auto eos = model(registry);
    return ma::FluidWorld(axis == ma::FluidAxis::x ? count : 1, axis == ma::FluidAxis::y ? count : 1,
        dx, 1, quiet(gravity), std::move(registry), {axis, std::move(eos), ma::LiquidFillRoundoffPolicy::balanced_local_roundoff});
}
bool same_cell(const ma::FluidCell& a, const ma::FluidCell& b) {
    return a.mixture.masses == b.mixture.masses && a.mixture.energy == b.mixture.energy &&
        a.momentum_x == b.momentum_x && a.momentum_y == b.momentum_y && a.wall == b.wall &&
        a.liquid_pressure_pa == b.liquid_pressure_pa && a.occupied_begin_m == b.occupied_begin_m &&
        a.occupied_end_m == b.occupied_end_m;
}
ma::MixtureInventory water(const ma::SpeciesRegistry& registry, const ma::FluidThermodynamics& eos,
    double occupied_volume, double pressure = 0, double temperature = 300) {
    std::vector<double> masses(registry.size());
    masses[registry.index("water")] = 1000 * (1 + pressure / 2.2e9) * occupied_volume;
    return eos.initialize_at_temperature(masses, temperature, occupied_volume).mixture;
}
double momentum(const ma::FluidWorld& w, ma::FluidAxis axis) {
    double total = 0;
    for (std::size_t y = 0; y < w.height(); ++y) for (std::size_t x = 0; x < w.width(); ++x)
        total += axis == ma::FluidAxis::x ? w.cell(x, y).momentum_x : w.cell(x, y).momentum_y;
    return total;
}
void conserve(const ma::ConservationTotals& before, const ma::ConservationTotals& after, double energy_scale,
    double tolerance = 2e-10) {
    for (std::size_t s = 0; s < before.masses.size(); ++s) near(after.masses[s], before.masses[s], tolerance);
    near(after.energy(), before.energy(), 0, tolerance * energy_scale);
}
void add_plug(ma::FluidWorld& w, ma::FluidAxis axis, double begin, double end, double speed, double dx = 1) {
    const auto eos = model(w.registry());
    const auto count = axis == ma::FluidAxis::x ? w.width() : w.height();
    for (std::size_t i = 0; i < count; ++i) {
        const double a = std::max(begin, static_cast<double>(i) * dx);
        const double b = std::min(end, static_cast<double>(i + 1) * dx);
        if (b <= a) continue;
        std::vector<double> masses(w.registry().size()); masses[w.registry().index("water")] = 1000 * (b - a);
        masses[w.registry().index("liquid_nitrogen")] = 1e-28 * (b - a);
        const auto mixture = eos->initialize_at_temperature(masses, 300, b - a).mixture;
        w.set_occupied_cell(axis == ma::FluidAxis::x ? i : 0, axis == ma::FluidAxis::y ? i : 0,
            mixture, a - static_cast<double>(i) * dx, b - static_cast<double>(i) * dx, speed);
    }
}

void initialization_preserves_temperature_and_accounts_for_elastic_energy() {
    const auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    std::vector<double> masses(registry.size()); masses[registry.index("water")] = 1250;
    const auto initialized = eos->initialize_at_temperature(masses, 300, 1);
    near(initialized.state.temperature_k, 300);
    near(initialized.state.mechanical_pressure_pa, 0.25 * 2.2e9);
    const double analytic_elastic = 1250 * 2.2e6 * (std::log(1.25) - 0.25 / 1.25);
    near(initialized.initialized_elastic_energy_j, analytic_elastic);
    near(initialized.mixture.energy - initialized.caloric_reference_and_thermal_energy_j, analytic_elastic, 1e-12);
    check(initialized.mixture.temperature(registry) > 300, "Test did not expose legacy caloric-temperature mismatch");
    const auto anchor = eos->reference().make_isentrope(eos->to_reference(initialized.mixture, 1));
    near(eos->compression_compliance(anchor), 1 / (2.2e9 * 1.25));
}

void true_vacuum_steps_without_unknowns_or_material() {
    auto w = world(5, ma::FluidAxis::y, 9.81); const auto before = w.totals();
    const auto result = w.step(0.3); near(result.seconds, 0.3); near(w.time(), 0.3);
    check(result.pressure_iterations == 0, "Vacuum entered pressure system");
    for (std::size_t y = 0; y < 5; ++y) {
        const auto& c = w.cell(0, y); check(c.mixture.mass() == 0 && c.mixture.energy == 0 &&
            c.momentum_x == 0 && c.momentum_y == 0 && w.occupied_volume(0, y) == 0 && w.pressure(0, y) == 0,
            "Vacuum acquired material, work, pressure or occupied volume");
        const auto state = w.thermodynamic_state(0, y);
        check(state.temperature_k == 0 && state.intrinsic_heat_capacity_j_per_k == 0 && state.has_vacuum,
            "Vacuum gained a thermal unknown");
    }
    check(w.totals().masses == before.masses && w.totals().energy() == before.energy(), "Vacuum changed conserved totals");
}

void zero_pressure_cut_liquid_uses_occupied_geometry() {
    auto w = world(4, ma::FluidAxis::x); const auto eos = model(w.registry());
    const auto mixture = water(w.registry(), *eos, 0.25);
    w.set_occupied_cell(1, 0, mixture, 0.3, 0.55);
    const auto original = w.cell(1, 0);
    near(w.occupied_volume(1, 0), 0.25); near(w.liquid_fraction(1, 0), 0.25); near(w.pressure(1, 0), 0, 0, 2e-6);
    near(w.thermodynamic_state(1, 0).temperature_k, 300);
    const auto result = w.step(0.5); check(result.substeps > 0, "Occupied state did not use step path");
    check(same_cell(w.cell(1, 0), original), "Stationary cut liquid drifted");
    check(w.cell(0, 0).mixture.mass() == 0 && w.cell(2, 0).mixture.mass() == 0, "Stationary liquid wetted vacuum");
}

void occupied_centroid_defines_gravitational_energy() {
    auto w = world(3, ma::FluidAxis::y, 9.81); const auto eos = model(w.registry());
    const auto mixture = water(w.registry(), *eos, 0.2);
    w.set_occupied_cell(0, 1, mixture, 0.1, 0.3);
    near(w.totals().potential_energy, mixture.mass() * 9.81 * 1.2);
    check(std::abs(w.totals().potential_energy - mixture.mass() * 9.81 * 1.5) > 1,
        "Potential energy still used fixed geometric cell centre");
}

void hydrostatic_cut_column_balances_one_sided_surface_inertia() {
    auto w = world(5, ma::FluidAxis::y, 9.81); const auto eos = model(w.registry());
    // Independent discrete half-cell weight recurrence. rho(p)=rho0*(1+p/K).
    // The upper cut cell has height .4, full cross-section A=1, surface p_ext=0.
    constexpr double g = 9.81, rho0 = 1000, bulk = 2.2e9;
    double upper_pressure = 0, upper_mass = 0;
    for (std::size_t reverse = 0; reverse < 3; ++reverse) {
        const auto y = 2 - reverse; const double height = y == 2 ? 0.4 : 1;
        const double pressure = (upper_pressure + g * (rho0 * height + upper_mass) / 2) /
            (1 - g * rho0 * height / (2 * bulk));
        const double mass = rho0 * height * (1 + pressure / bulk);
        std::vector<double> masses(w.registry().size()); masses[w.registry().index("water")] = mass;
        const auto initialized = eos->initialize_at_temperature(masses, 300, height);
        w.set_occupied_cell(0, y, initialized.mixture, 0, height);
        near(w.pressure(0, y), pressure, 0, 2e-6);
        upper_pressure = pressure; upper_mass = mass;
    }
    const auto before = w.totals(); std::vector<ma::FluidCell> original;
    for (std::size_t y = 0; y < 5; ++y) original.push_back(w.cell(0, y));
    (void)w.step(0.4);
    for (std::size_t y = 0; y < 5; ++y) {
        const auto& c = w.cell(0, y);
        near(c.momentum_y, 0, 0, 2e-7);
        near(c.occupied_begin_m, original[y].occupied_begin_m, 0, 2e-10);
        near(c.occupied_end_m, original[y].occupied_end_m, 0, 2e-10);
    }
    near(w.totals().energy(), before.energy(), 0, 1e-4);
    for (std::size_t s = 0; s < before.masses.size(); ++s) near(w.totals().masses[s], before.masses[s]);
}

void unsupported_geometry_and_internal_void_fail_atomically() {
    auto w = world(5, ma::FluidAxis::x); const auto eos = model(w.registry());
    const auto mixture = water(w.registry(), *eos, 0.25);
    w.set_occupied_cell(0, 0, mixture, 0, 0.25); w.set_occupied_cell(2, 0, mixture, 0, 0.25);
    const auto first = w.cell(0, 0), second = w.cell(2, 0);
    fatal(ma::FluidFatalCode::unsupported_topology, [&] { (void)w.step(0.1, 1); });
    check(w.time() == 0 && same_cell(w.cell(0, 0), first) && same_cell(w.cell(2, 0), second), "Topology rejection changed world");
    fatal(ma::FluidFatalCode::unsupported_topology, [&] { w.set_occupied_cell(0, 0, mixture, 0, 0.5); });
    check(same_cell(w.cell(0, 0), first), "Invalid occupied setter changed previous state");
    rejects([&] { w.set_occupied_cell(0, 0, mixture, 0.8, 0.2); });
    rejects([&] { w.set_cell(0, 0, mixture); });
}

void explicit_profile_and_mode_constraints_are_validated() {
    auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    rejects([&] { ma::FluidWorld w(2, 2, 1, 1, quiet(), registry, {ma::FluidAxis::x, eos}); });
    rejects([&] { ma::FluidWorld w(2, 1, 1, 1, quiet(9.81), registry, {ma::FluidAxis::x, eos}); });
    rejects([&] { ma::FluidWorld w(1, 2, 1, 1, {}, registry, {ma::FluidAxis::y, eos}); });
    auto wrong_registry = registry;
    wrong_registry.add({"other", "Other", ma::Phase::gas, {{"He", 1}}, 0, 6000, 0, 0, 0, 1});
    rejects([&] { ma::FluidWorld w(1, 2, 1, 1, quiet(), wrong_registry, {ma::FluidAxis::y, eos}); });
    ma::MixtureInventory unsupported(registry.size()); unsupported.masses[registry.index("chlorine")] = 1;
    rejects([&] { (void)eos->evaluate(unsupported, 1); });
    auto bad_material = eos->reference().materials(); bad_material[0].cv_j_per_kg_k *= 1.01;
    rejects([&] { ma::FluidThermodynamics bad(registry, "bad-caloric-v1", bad_material); });
    const auto other = model(registry, 1.1e9, "other-v1");
    const auto mixture = water(registry, *eos, 1, 1e5);
    const auto anchor = eos->reference().make_isentrope(eos->to_reference(mixture, 1));
    rejects([&] { (void)other->compression_compliance(anchor); });
}

void translating_liquid_plug_crosses_wet_dry_cells_without_drag_or_trace_loss() {
    auto w = world(16, ma::FluidAxis::x); add_plug(w, ma::FluidAxis::x, 2.2, 5.7, 1.25);
    const auto before = w.totals(); const double po = momentum(w, ma::FluidAxis::x);
    const auto result = w.step(2); conserve(before, w.totals(), 4e9);
    near(momentum(w, ma::FluidAxis::x), po, 2e-11);
    double left = 16, right = 0, volume = 0;
    for (std::size_t x = 0; x < 16; ++x) {
        const auto& c = w.cell(x, 0); volume += w.occupied_volume(x, 0);
        if (c.mixture.mass() == 0) {
            check(c.mixture.energy == 0 && c.momentum_x == 0 && w.pressure(x, 0) == 0, "Dry cell retained material fields");
            continue;
        }
        left = std::min(left, static_cast<double>(x) + c.occupied_begin_m);
        right = std::max(right, static_cast<double>(x) + c.occupied_end_m);
        near(c.momentum_x / c.mixture.mass(), 1.25, 0, 2e-8);
        check(c.mixture.masses[w.registry().index("liquid_nitrogen")] > 0, "Occupied receiver lost positive liquid trace");
        check(w.pressure(x, 0) < 1e-3, "Translation created material pressure");
    }
    near(left, 4.7, 0, 2e-9); near(right, 8.2, 0, 2e-9); near(volume, 3.5, 0, 2e-9);
    check(w.cell(2, 0).mixture.mass() == 0 && w.cell(3, 0).mixture.mass() == 0 && w.cell(8, 0).mixture.mass() > 0,
        "Interface failed to deactivate/activate fixed cells");
    check(result.substeps >= 20, "Plug did not run through repeated world candidate/remap path");
}

void incremental_heat_and_free_surface_momentum_match_independent_lumped_mechanics() {
    auto w = world(8, ma::FluidAxis::x); const auto eos = model(w.registry());
    std::vector<double> m1(w.registry().size()), m2(w.registry().size());
    m1[w.registry().index("oxygen")] = 1; m2[w.registry().index("oxygen")] = 2;
    auto a = eos->initialize_at_temperature(m1, 300, 1).mixture;
    auto b = eos->initialize_at_temperature(m2, 300, 1).mixture;
    w.set_occupied_cell(2, 0, a, 0, 1, 1); w.set_occupied_cell(3, 0, b, 0, 1, -2);
    const auto before = w.totals();
    // Initial nodes u=(1,-1,-2), inertias=(.5,1.5,1). Their lower initial
    // kinetic energy is not converted to heat: only increments are transferred.
    const std::vector<ma::MaterialCell> cells{{a, 1}, {b, 1}};
    const auto none = ma::no_material_cell;
    const std::vector<ma::OccupiedMaterialNode> nodes{
        {none, 0, 1, 0.5, 1, 0, false}, {0, 1, 1, 1.5, -1, 0, false}, {1, none, 1, 1, -2, 0, false}};
    const auto reference = ma::advance_occupied_material_pressure(*eos, cells, nodes, 1e-6);
    const auto& p = reference.work_pressures_pa;
    const double du0 = -1e-6 * p[0] / 0.5, du1 = 1e-6 * (p[0] - p[1]) / 1.5, du2 = 1e-6 * p[1];
    near(reference.velocities_m_per_s[0], 1 + du0);
    near(reference.velocities_m_per_s[1], -1 + du1);
    near(reference.velocities_m_per_s[2], -2 + du2);
    const double expected_final_heat = (du1 - du0) * (du1 - du0) / 8 +
        2 * (du2 - du1) * (du2 - du1) / 8;
    const double old_node_ke = 0.25 * 1 + 0.75 * 1 + 0.5 * 4;
    const double new_node_ke = 0.25 * (1 + du0) * (1 + du0) + 0.75 * (-1 + du1) * (-1 + du1) +
        0.5 * (-2 + du2) * (-2 + du2);
    const double va = 1 + (du0 + du1) / 2, vb = -2 + (du1 + du2) / 2;
    near((new_node_ke - old_node_ke) - (0.5 * va * va + vb * vb - 4.5), expected_final_heat, 1e-10);
    const auto step = w.step(1e-6); check(step.initial_projection_heat_j == 0, "Initial velocity was projected to heat");
    near(step.final_projection_heat_j, expected_final_heat, 1e-11);
    near(momentum(w, ma::FluidAxis::x), -3, 0, 2e-13);
    check(step.wall_reaction_impulse_kg_m_per_s == 0 && step.body_impulse_kg_m_per_s == 0,
        "Free surface generated exterior impulse");
    conserve(before, w.totals(), 1e6, 1e-11);

    // Combine unequal masses and nonuniform canonical initial velocities with
    // a fixed wall AND gravity. This exercises the adjoint linear work terms
    // when the wall node's reconstructed velocity differs from its cell mean.
    constexpr double gravity = 3.5, dt = 1e-6;
    auto vertical = world(8, ma::FluidAxis::y, gravity);
    vertical.set_occupied_cell(0, 0, a, 0, 1, 1); vertical.set_occupied_cell(0, 1, b, 0, 1, -2);
    const auto vertical_before = vertical.totals();
    const std::vector<ma::OccupiedMaterialNode> supported_nodes{
        {none, 0, 1, .5, 0, -gravity, true}, {0, 1, 1, 1.5, -1, -gravity, false},
        {1, none, 1, 1, -2, -gravity, false}};
    const auto supported = ma::advance_occupied_material_pressure(*eos, cells, supported_nodes, dt);
    const double middle_increment = dt * ((supported.work_pressures_pa[0] - supported.work_pressures_pa[1]) / 1.5 - gravity);
    const double top_increment = dt * (supported.work_pressures_pa[1] - gravity);
    const double supported_heat = middle_increment * middle_increment / 8 +
        2 * (top_increment - middle_increment) * (top_increment - middle_increment) / 8;
    const auto vertical_step = vertical.step(dt);
    check(vertical_step.initial_projection_heat_j == 0, "Wall projected an initial cell velocity instead of applying a finite force");
    near(vertical_step.final_projection_heat_j, supported_heat, 2e-10);
    near(vertical_step.wall_reaction_impulse_kg_m_per_s, dt * (supported.work_pressures_pa[0] + .5 * gravity), 2e-11);
    near(vertical_step.body_impulse_kg_m_per_s, -3 * gravity * dt, 2e-11);
    near(momentum(vertical, ma::FluidAxis::y), -3 + vertical_step.wall_reaction_impulse_kg_m_per_s +
        vertical_step.body_impulse_kg_m_per_s, 0, 2e-12);
    conserve(vertical_before, vertical.totals(), 1e6, 1e-11);
}

void fourier_velocity_has_an_identity_limit_as_timestep_vanishes() {
    // For equal masses the old full PIC projection maps this quarter-grid
    // Fourier mode to exactly half its amplitude independently of dt. The
    // actual world step must instead approach the original velocity field.
    double previous_error = 1;
    for (const double dt : {1e-7, 1e-9, 1e-11}) {
        auto w = world(32, ma::FluidAxis::x); const auto eos = model(w.registry());
        std::vector<double> masses(w.registry().size()); masses[w.registry().index("oxygen")] = 1;
        const auto gas = eos->initialize_at_temperature(masses, 300, 1).mixture;
        const auto wave = [](std::size_t i) { return i % 4 == 1 ? 1.0 : i % 4 == 3 ? -1.0 : 0.0; };
        for (std::size_t i = 0; i < 32; ++i) w.set_occupied_cell(i, 0, gas, 0, 1, wave(i));
        const auto before = w.totals(); const auto result = w.step(dt);
        double numerator = 0, denominator = 0, error = 0;
        for (std::size_t i = 4; i < 28; ++i) {
            const double velocity = w.cell(i, 0).momentum_x / w.cell(i, 0).mixture.mass();
            numerator += wave(i) * velocity; denominator += wave(i) * wave(i);
            error = std::max(error, std::abs(velocity - wave(i)));
        }
        const double amplitude = numerator / denominator;
        check(std::abs(amplitude - 1) <= 2 * dt && error <= 2 * dt,
            "Fourier velocity has nonvanishing drag as dt -> 0");
        check(error < 0.02 * previous_error + 32 * std::numeric_limits<double>::epsilon(),
            "Fixed-grid Fourier velocity error did not vanish with timestep");
        check(result.initial_projection_heat_j == 0, "Fourier velocity suffered an initial PIC projection");
        conserve(before, w.totals(), 1e7, 1e-12);
        previous_error = error;
        std::cout << std::setprecision(15) << "  Fourier dt=" << dt << " amplitude=" << amplitude <<
            " maximum velocity error=" << error << '\n';
    }
}

void gravity_falls_at_occupied_centroids_and_wall_reaction_balances_weight() {
    auto falling = world(14, ma::FluidAxis::y, 9.81); add_plug(falling, ma::FluidAxis::y, 6.1, 8.6, 0);
    const auto before = falling.totals(); const double mass = falling.cell(0, 6).mixture.mass() +
        falling.cell(0, 7).mixture.mass() + falling.cell(0, 8).mixture.mass();
    const auto step = falling.step(0.2);
    near(momentum(falling, ma::FluidAxis::y), -mass * 9.81 * 0.2, 1e-11);
    near(step.body_impulse_kg_m_per_s, -mass * 9.81 * 0.2, 1e-11);
    check(step.wall_reaction_impulse_kg_m_per_s == 0, "Falling free plug acquired wall impulse");
    conserve(before, falling.totals(), 3e9);
    double top = 0;
    for (std::size_t y = 0; y < 14; ++y) if (falling.cell(0, y).mixture.mass() > 0)
        top = static_cast<double>(y) + falling.cell(0, y).occupied_end_m;
    near(top, 8.6 - 0.5 * 9.81 * 0.2 * 0.2, 0, 2e-9);

    auto supported = world(3, ma::FluidAxis::y, 9.81); const auto eos = model(supported.registry());
    constexpr double height = 0.4;
    const double p = 1000 * 9.81 * height / 2 / (1 - 1000 * 9.81 * height / (2 * 2.2e9));
    const auto mixture = water(supported.registry(), *eos, height, p);
    supported.set_occupied_cell(0, 0, mixture, 0, height);
    const auto reaction = supported.step(0.01);
    near(reaction.wall_reaction_impulse_kg_m_per_s, mixture.mass() * 9.81 * 0.01, 0, 2e-9);
    near(momentum(supported, ma::FluidAxis::y), reaction.wall_reaction_impulse_kg_m_per_s + reaction.body_impulse_kg_m_per_s, 0, 2e-13);
}

void compressed_release_conserves_energy_without_gas_and_cavitation_is_fatal() {
    auto w = world(5, ma::FluidAxis::x); const auto eos = model(w.registry());
    const auto mixture = water(w.registry(), *eos, 0.5, 1e6);
    w.set_occupied_cell(2, 0, mixture, 0.25, 0.75);
    const auto before = w.totals(); const auto state = w.thermodynamic_state(2, 0);
    const auto step = w.step(1e-6);
    check(w.occupied_volume(2, 0) > 0.5 && w.pressure(2, 0) < state.mechanical_pressure_pa,
        "Compressed liquid did not release at free surfaces");
    check(w.thermodynamic_state(2, 0).elastic_energy_j < state.elastic_energy_j && step.final_projection_heat_j > 0,
        "Released elastic work was not accounted in compatible mechanics");
    check(w.cell(2, 0).mixture.gas_moles(w.registry()) == 0, "Release invented seed gas");
    conserve(before, w.totals(), 1e9, 1e-11);
    auto overstretched = world(8, ma::FluidAxis::x); add_plug(overstretched, ma::FluidAxis::x, 2, 4, 0);
    const auto a = overstretched.cell(2, 0).mixture, b = overstretched.cell(3, 0).mixture;
    overstretched.set_occupied_cell(2, 0, a, 0, 1, -1);
    overstretched.set_occupied_cell(3, 0, b, 0, 1, 1);
    const auto original_a = overstretched.cell(2, 0), original_b = overstretched.cell(3, 0);
    fatal(ma::FluidFatalCode::unsupported_topology, [&] { (void)overstretched.step(0.001, 1000); });
    check(overstretched.time() == 0 && same_cell(overstretched.cell(2, 0), original_a) &&
        same_cell(overstretched.cell(3, 0), original_b), "Cavitation failure did not restore world");
}

void failed_call_restores_geometry_after_an_accepted_moving_substep() {
    auto w = world(12, ma::FluidAxis::x); add_plug(w, ma::FluidAxis::x, 2.2, 5.7, 1.25);
    auto control = w; const auto advanced = control.step(0.1, 1);
    check(advanced.substeps == 1 && !same_cell(control.cell(2, 0), w.cell(2, 0)), "Control did not accept a moving substep");
    std::vector<ma::FluidCell> original; for (std::size_t x = 0; x < 12; ++x) original.push_back(w.cell(x, 0));
    rejects([&] { (void)w.step(0.3, 1); });
    check(w.time() == 0, "Failed call changed world time");
    for (std::size_t x = 0; x < 12; ++x) check(same_cell(w.cell(x, 0), original[x]), "Failed call lost occupied geometry/inventory/momentum");
}

void sub_ulp_motion_is_reported_and_tiny_volume_bounds_are_local() {
    auto w = world(5, ma::FluidAxis::x); const auto eos = model(w.registry());
    std::vector<double> masses(w.registry().size()); masses[w.registry().index("oxygen")] = 1;
    const auto mixture = eos->initialize_at_temperature(masses, 300, 0.5).mixture;
    w.set_occupied_cell(2, 0, mixture, 0.25, 0.75);
    const auto before = w.totals(); const auto result = w.step(1e-12);
    check(result.unresolved_position_updates > 0 && result.maximum_position_roundoff_m > 0,
        "Sub-ULP material motion was falsely reported as exact");
    check(result.maximum_position_roundoff_m <= 32 * std::numeric_limits<double>::epsilon(),
        "Accepted position rounding exceeded local geometric policy");
    conserve(before, w.totals(), 1e6, 1e-12);

    constexpr double dx = 1e-100, occupied = 1e-200;
    auto tiny = world(3, ma::FluidAxis::x, 0, dx); const auto tiny_eos = model(tiny.registry());
    const auto liquid = water(tiny.registry(), *tiny_eos, occupied);
    tiny.set_occupied_cell(1, 0, liquid, 0, dx);
    const auto original = tiny.cell(1, 0);
    rejects([&] { tiny.set_occupied_cell(1, 0, liquid, 0, dx * 1.01); });
    check(same_cell(tiny.cell(1, 0), original), "Tiny-volume violation changed state");
    // A 1% expansion inside the allowed cell is still many orders above its
    // relative roundoff budget. No 1 m3/unit floor can excuse the void.
    const auto smaller = water(tiny.registry(), *tiny_eos, occupied / 2);
    tiny.set_occupied_cell(1, 0, smaller, 0, dx / 2);
    fatal(ma::FluidFatalCode::unsupported_topology, [&] { tiny.set_occupied_cell(1, 0, smaller, 0, dx * 0.505); });
}

void explicit_fill_policy_is_gasless_bounded_and_keeps_true_eos_readings() {
    const auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    constexpr double epsilon = std::numeric_limits<double>::epsilon();
    std::vector<double> masses(registry.size()); masses[registry.index("water")] = 1000 * (1 + 4 * epsilon);
    const auto initialized = eos->initialize_at_temperature(masses, 300, 1);
    check(initialized.state.mechanical_pressure_pa > 0, "Test must retain true positive EOS pressure");
    const auto inside = eos->liquid_fill_resolution(eos->to_reference(initialized.mixture, 1));
    check(inside.gasless && inside.within_roundoff, "Representable fill mismatch was not in explicit band");
    const auto none = ma::no_material_cell;
    const std::vector<ma::MaterialCell> material{{initialized.mixture, 1}};
    const std::vector<ma::OccupiedMaterialNode> nodes{{none, 0, 1, 500, 0, 0, false}, {0, none, 1, 500, 0, 0, false}};
    const auto approximate = ma::advance_occupied_material_pressure(*eos, material, nodes, 1e-6,
        ma::PressurePreconditioner::automatic, ma::LiquidFillRoundoffPolicy::balanced_local_roundoff);
    const auto strict = ma::advance_occupied_material_pressure(*eos, material, nodes, 1e-6);
    check(approximate.zero_work_substitutions == 1 && approximate.work_pressures_pa[0] == 0 &&
        approximate.endpoint_pressures_pa[0] > 0 && approximate.maximum_omitted_pressure_pa > 0,
        "Representability substitution changed true EOS reading or was not reported");
    check(approximate.cells[0].mixture.masses == initialized.mixture.masses &&
        approximate.cells[0].mixture.energy == initialized.mixture.energy && approximate.cells[0].volume_m3 == 1,
        "Zero-work substitution modified conserved state/geometry");
    check(strict.zero_work_substitutions == 0 && strict.work_pressures_pa[0] > 0, "Strict EOS policy silently substituted work pressure");
    masses[registry.index("water")] = 1000 * (1 + 64 * epsilon);
    const auto compressed = eos->initialize_at_temperature(masses, 300, 1);
    check(!eos->liquid_fill_resolution(eos->to_reference(compressed.mixture, 1)).within_roundoff,
        "Compression outside local band was hidden");
    const auto outside = ma::advance_occupied_material_pressure(*eos, std::vector<ma::MaterialCell>{{compressed.mixture, 1}}, nodes, 1e-6,
        ma::PressurePreconditioner::automatic, ma::LiquidFillRoundoffPolicy::balanced_local_roundoff);
    check(outside.zero_work_substitutions == 0 && outside.work_pressures_pa[0] > 0, "Outside-band compression failed to restore full EOS work");
    auto underfilled = eos->to_reference(initialized.mixture, 1 + 64 * epsilon);
    check(!eos->liquid_fill_resolution(underfilled).within_roundoff, "Outside-band underfill was hidden");
    masses[registry.index("water")] = 1000; masses[registry.index("helium")] = 1e-30;
    const auto with_gas = eos->initialize_at_temperature(masses, 300, 1);
    check(!eos->liquid_fill_resolution(eos->to_reference(with_gas.mixture, 1)).gasless, "Positive gas trace did not disable gasless policy");
    const auto gas = ma::advance_occupied_material_pressure(*eos, std::vector<ma::MaterialCell>{{with_gas.mixture, 1}}, nodes, 1e-8,
        ma::PressurePreconditioner::automatic, ma::LiquidFillRoundoffPolicy::balanced_local_roundoff);
    check(gas.zero_work_substitutions == 0 && gas.work_pressures_pa[0] > 0, "Positive trace gas EOS was replaced by zero work");
    auto exact_trace = eos->to_reference(initialized.mixture, 1);
    const auto helium = std::find_if(eos->reference().materials().begin(), eos->reference().materials().end(),
        [](const auto& m) { return m.id == "helium"; });
    exact_trace.masses_kg[static_cast<std::size_t>(helium - eos->reference().materials().begin())] = std::numeric_limits<double>::denorm_min();
    check(!eos->liquid_fill_resolution(exact_trace).gasless, "Minimum positive gas mass was thresholded away");
    const auto oxygen = std::find_if(eos->reference().materials().begin(), eos->reference().materials().end(),
        [](const auto& m) { return m.id == "oxygen"; });
    const auto oxygen_index = static_cast<std::size_t>(oxygen - eos->reference().materials().begin());
    const auto helium_index = static_cast<std::size_t>(helium - eos->reference().materials().begin());
    check(helium_index > oxygen_index, "Invalid-after-gas test order is incorrect");
    for (const double invalid : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto malformed = eos->to_reference(initialized.mixture, 1);
        malformed.masses_kg[oxygen_index] = 1;
        malformed.masses_kg[helium_index] = invalid;
        rejects([&] { (void)eos->liquid_fill_resolution(malformed); });
    }
    masses.assign(registry.size(), 0); masses[registry.index("water")] = 1e-197 * (1 + 4 * epsilon);
    auto tiny = eos->to_reference(eos->initialize_at_temperature(masses, 300, 1e-200).mixture, 1e-200);
    check(eos->liquid_fill_resolution(tiny).within_roundoff, "Tiny local fill roundoff was rejected");
    tiny.volume_m3 *= 1 + 64 * epsilon;
    check(!eos->liquid_fill_resolution(tiny).within_roundoff, "Unit floor hid a tiny-volume outside-band change");
}

void fill_policy_rejects_material_elastic_work_and_restores_world() {
    ma::SpeciesRegistry registry;
    const ma::Composition atoms{{"H", 2}, {"O", 1}};
    // Choose an exactly zero caloric offset so extremely small positive
    // thermal energy is representable. This is a numerical test surrogate.
    registry.add({"water", "Surrogate", ma::Phase::liquid, atoms, 0,
        1 / ma::reference_temperature, 1000, 0, ma::molecular_mass(atoms), 1});
    check(registry[0].internal_energy(0) == 0, "Cold test requires exact zero caloric offset");
    ma::experimental::ReferenceMaterial material;
    material.id = "water"; material.phase = ma::experimental::ReferencePhase::elastic_liquid;
    material.molar_mass_kg_per_mol = registry[0].molar_mass; material.cv_j_per_kg_k = registry[0].cv();
    material.energy_offset_j_per_kg = 0; material.minimum_temperature_k = 1e-21; material.maximum_temperature_k = 1;
    material.zero_pressure_density_kg_per_m3 = 1000; material.reference_bulk_modulus_pa = 2.2e9;
    material.maximum_density_ratio = 2; material.provenance = "Explicit numerical-domain test surrogate";
    const auto eos = std::make_shared<const ma::FluidThermodynamics>(registry, "cold-bound-v1", std::vector{material});
    ma::FluidWorld w(6, 1, 1, 1, quiet(), registry,
        {ma::FluidAxis::x, eos, ma::LiquidFillRoundoffPolicy::balanced_local_roundoff});
    const std::vector<double> masses{1000 * (1 + 4 * std::numeric_limits<double>::epsilon())};
    const auto initialized = eos->initialize_at_temperature(masses, 1e-20, 1);
    w.set_occupied_cell(2, 0, initialized.mixture, 0, 1, -1e-14);
    w.set_occupied_cell(3, 0, initialized.mixture, 0, 1, 1e-14);
    const auto a = w.cell(2, 0), b = w.cell(3, 0);
    fatal(ma::FluidFatalCode::numerical_precision, [&] { (void)w.step(0.1); });
    check(w.time() == 0 && w.cumulative_omitted_elastic_work_bound() == 0 && same_cell(a, w.cell(2, 0)) && same_cell(b, w.cell(3, 0)),
        "Material omitted-work rejection did not restore conserved state/geometry/ledger");
}

void translating_plug_refines_grid_and_timestep_with_reported_error_bounds() {
    for (const double dx : {1.0, 0.5, 0.25}) {
        auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
        auto settings = quiet(); settings.maximum_timestep_s = 0.1 * dx;
        const auto count = static_cast<std::size_t>(16 / dx);
        ma::FluidWorld w(count, 1, dx, 1 / dx, settings, registry,
            {ma::FluidAxis::x, eos, ma::LiquidFillRoundoffPolicy::balanced_local_roundoff});
        add_plug(w, ma::FluidAxis::x, 2.2, 5.7, 1.25, dx);
        const auto before = w.totals();
        const auto first = w.step(1); const auto second = w.step(1);
        const double time_before_zero = w.time(), bound_before_zero = w.cumulative_omitted_elastic_work_bound();
        const auto zero = w.step(0);
        check(zero.seconds == 0 && zero.substeps == 0 && zero.omitted_elastic_work_bound_j == 0 &&
            zero.cumulative_omitted_elastic_work_bound_j == bound_before_zero && w.time() == time_before_zero &&
            w.cumulative_omitted_elastic_work_bound() == bound_before_zero,
            "Zero-duration call did not preserve/report existing cumulative work bound");
        check(second.cumulative_omitted_elastic_work_bound_j >= first.cumulative_omitted_elastic_work_bound_j,
            "Cumulative omitted-work bound decreased");
        near(w.cumulative_omitted_elastic_work_bound(), first.omitted_elastic_work_bound_j + second.omitted_elastic_work_bound_j, 1e-14);
        check(first.zero_work_substitutions + second.zero_work_substitutions > 0, "Explicit approximation was not reported");
        check(w.cumulative_omitted_elastic_work_bound() < 64 * std::numeric_limits<double>::epsilon() * 4e9,
            "Accumulated omitted work became material at nonformation energy scale");
        double left = 16, right = 0, occupied = 0;
        for (std::size_t x = 0; x < count; ++x) if (w.cell(x, 0).mixture.mass() > 0) {
            const auto& c = w.cell(x, 0);
            left = std::min(left, static_cast<double>(x) * dx + c.occupied_begin_m);
            right = std::max(right, static_cast<double>(x) * dx + c.occupied_end_m);
            occupied += w.occupied_volume(x, 0);
            near(c.momentum_x / c.mixture.mass(), 1.25, 0, 2e-8);
        }
        near(left, 4.7, 0, 2e-9); near(right, 8.2, 0, 2e-9); near(occupied, 3.5, 0, 2e-9);
        conserve(before, w.totals(), 4e9, 2e-10);
        std::cout << std::setprecision(12) << "  plug dx=" << dx << " dt_max=" << settings.maximum_timestep_s <<
            " omitted-work bound=" << w.cumulative_omitted_elastic_work_bound() << " J; max omitted pressure=" <<
            std::max(first.maximum_omitted_pressure_pa, second.maximum_omitted_pressure_pa) << " Pa\n";
    }
}

void zero_duration_reports_an_explicit_nonzero_omitted_work_ledger() {
    auto w = world(3, ma::FluidAxis::x); const auto eos = model(w.registry());
    std::vector<double> masses(w.registry().size());
    masses[w.registry().index("water")] = 1000 * (1 + 4 * std::numeric_limits<double>::epsilon());
    const auto compressed = eos->initialize_at_temperature(masses, 300, 1).mixture;
    // An explicit sub-ULP fill-band expansion releases a positive (bounded)
    // elastic energy. Do not depend on incidental remap/PIC rounding to make
    // the prior cumulative ledger nonzero.
    w.set_occupied_cell(0, 0, compressed, 0, 1, 0);
    w.set_occupied_cell(1, 0, compressed, 0, 1, -2e-14);
    w.set_occupied_cell(2, 0, compressed, 0, 1, 0);
    const auto step = w.step(0.1);
    check(step.omitted_elastic_work_bound_j > 0 && w.cumulative_omitted_elastic_work_bound() > 0,
        "Zero-duration ledger fixture did not explicitly release bounded elastic work");
    const double before_time = w.time(), before_bound = w.cumulative_omitted_elastic_work_bound();
    const auto zero = w.step(0);
    check(zero.seconds == 0 && zero.substeps == 0 && zero.omitted_elastic_work_bound_j == 0 &&
        zero.cumulative_omitted_elastic_work_bound_j == before_bound && w.time() == before_time &&
        w.cumulative_omitted_elastic_work_bound() == before_bound,
        "Zero-duration call lost a positive prior omitted-work ledger");
}

void endpoint_volume_accounting_handles_expansion_and_translation_without_a_floor() {
    const auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    // A symmetric one-cell pressure kick has an analytic swept-volume root.
    // The unresolved expansion kinetic moment becomes heat in this bounded
    // method, so its stored cell returns to T0; it is not a retained-slope test.
    for (const double ratio : {1 + 1e-6, 68.0, 1e8}) for (const std::size_t origin : {2U, 10U}) {
        const double volume0 = ratio < 2 ? 0.5 : 0x1p-30;
        const double temperature = ratio < 2 ? 300 : 1e8; // Keep the declared caloric reference numerically resolved during a huge isentropic kick.
        std::vector<double> masses(registry.size()); masses[registry.index("oxygen")] = volume0;
        const auto initial = eos->initialize_at_temperature(masses, temperature, volume0);
        const auto& species = registry[registry.index("oxygen")];
        const double k = ma::gas_constant / (species.molar_mass * species.cv());
        const double dv = volume0 * (ratio - 1);
        const double work = -initial.state.thermal_energy_j * std::expm1(-k * std::log(ratio)) / dv;
        const double dt = std::sqrt(dv * initial.mixture.mass() / (2 * work));
        auto settings = quiet(); settings.maximum_timestep_s = dt;
        ma::FluidWorld w(16, 1, 1, 1, settings, registry, {ma::FluidAxis::x, eos});
        w.set_occupied_cell(origin, 0, initial.mixture, 0.25, 0.25 + volume0);
        const auto before = w.totals(); const auto step = w.step(dt);
        check(step.substeps == 1 && step.rejected_substeps == 0, "Expansion accounting unexpectedly changed its analytic single kick");
        double volume1 = 0;
        for (std::size_t x = 0; x < w.width(); ++x) if (w.cell(x, 0).mixture.mass() > 0) {
            volume1 += w.occupied_volume(x, 0);
            const auto state = w.thermodynamic_state(x, 0);
            near(state.temperature_k, temperature, 2e-10);
            near(state.mechanical_pressure_pa, initial.state.mechanical_pressure_pa / ratio, 2e-10);
        }
        near(volume1, volume0 * ratio, 2e-10);
        check(step.maximum_volume_addition_roundoff_m3 <= 32 * std::numeric_limits<double>::epsilon() * volume0 * ratio &&
            step.maximum_root_arithmetic_roundoff_m3 <= 32 * std::numeric_limits<double>::epsilon() * volume0 * ratio &&
            step.maximum_endpoint_relative_volume_residual <= 5e-13,
            "Scalar root/addition diagnostics exceeded endpoint-local volume budgets");
        check(step.absolute_geometry_volume_residual_m3 <= 32 * std::numeric_limits<double>::epsilon() * volume0 * ratio,
            "Expansion geometry used an initial-volume or unit floor");
        conserve(before, w.totals(), initial.state.thermal_energy_j, 2e-10);
    }
}

void positive_denormal_trace_is_never_deleted_by_endpoint_accounting() {
    auto w = world(8, ma::FluidAxis::x); const auto eos = model(w.registry());
    std::vector<double> masses(w.registry().size()); masses[w.registry().index("oxygen")] = 1;
    masses[w.registry().index("helium")] = std::numeric_limits<double>::denorm_min();
    const auto gas = eos->initialize_at_temperature(masses, 300, 0.5).mixture;
    w.set_occupied_cell(3, 0, gas, 0, 0.5);
    const auto original = w.cell(3, 0);
    // Expansion into several overlaps cannot split a minimum representable
    // trace into positive binary64 shares. Fail atomically, never make it dry.
    fatal(ma::FluidFatalCode::numerical_precision, [&] { (void)w.step(0.001); });
    check(w.time() == 0 && same_cell(original, w.cell(3, 0)), "Denormal trace split was silently lost or partially published");

    // Two directly rounded shares can consume a two-ULP subnormal trace,
    // leaving a zero final remainder in a still-wet third receiver. Global
    // species conservation alone would miss this lost local constituent.
    masses[w.registry().index("oxygen")] = 3;
    masses[w.registry().index("helium")] = 2 * std::numeric_limits<double>::denorm_min();
    const auto three = eos->initialize_at_temperature(masses, 300, 3).mixture;
    const std::vector<ma::OccupiedTransportCell> moving{{three, {2, 0}, {5, 0}, 0}};
    const std::vector<std::uint8_t> walls(8, 0);
    fatal(ma::FluidFatalCode::numerical_precision, [&] { (void)ma::remap_occupied_mixtures(*eos, moving, walls, 1, 1); });
    check(moving[0].mixture.masses == three.masses && moving[0].mixture.energy == three.energy,
        "Final-zero trace failure changed the original donor");
}

void contracting_pressure_root_requires_endpoint_relative_accuracy() {
    const auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    std::vector<double> masses(registry.size()); masses[registry.index("oxygen")] = 1;
    const auto initial = eos->initialize_at_temperature(masses, 300, 1);
    const auto& species = registry[registry.index("oxygen")];
    const double k = ma::gas_constant / (species.molar_mass * species.cv());
    const std::vector<ma::MaterialCell> material{{initial.mixture, 1}};
    const auto none = ma::no_material_cell;
    for (const double ratio : {0.5, 0.01, 1e-4, 1e-8}) {
        constexpr double dt = 1e-6;
        const double dv = ratio - 1;
        const double work = -initial.state.thermal_energy_j * std::expm1(-k * std::log(ratio)) / dv;
        const double inward_speed = dt * work - dv / (2 * dt);
        const std::vector<ma::OccupiedMaterialNode> nodes{
            {none, 0, 1, .5, inward_speed, 0, false}, {0, none, 1, .5, -inward_speed, 0, false}};
        if (ratio == 1e-8) {
            // At this compression the encoded input/node sweeps cannot resolve
            // the root to the requested relative endpoint accuracy. Reject;
            // a tolerance scaled by the original V0 must not excuse it.
            rejects([&] { (void)ma::advance_occupied_material_pressure(*eos, material, nodes, dt); });
        } else {
            const auto step = ma::advance_occupied_material_pressure(*eos, material, nodes, dt);
            near(step.cells[0].volume_m3, ratio, 2e-11);
            check(step.maximum_endpoint_relative_volume_residual <= 5e-13 &&
                step.maximum_volume_addition_roundoff_m3 <= 32 * std::numeric_limits<double>::epsilon() * step.cells[0].volume_m3 &&
                step.maximum_root_arithmetic_roundoff_m3 <= 32 * std::numeric_limits<double>::epsilon() * step.cells[0].volume_m3,
                "Contraction used its initial volume as an endpoint accuracy floor");
        }
        check(material[0].mixture.masses == initial.mixture.masses && material[0].mixture.energy == initial.mixture.energy &&
            material[0].volume_m3 == 1, "Pressure contraction/failure changed input state");
    }
}

void reflected_translated_overlap_geometry_is_accurate_or_fails_atomically() {
    const auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    const std::vector<std::uint8_t> walls(16, 0);
    for (const std::size_t origin : {2U, 10U}) for (const double length : {0x1p-40, 1e-16}) for (const bool reflected : {false, true}) {
        const auto mixture = water(registry, *eos, length);
        const ma::OccupiedMeshPosition a = reflected ? ma::OccupiedMeshPosition{origin + 1, -length} : ma::OccupiedMeshPosition{origin, 0};
        const ma::OccupiedMeshPosition b = reflected ? ma::OccupiedMeshPosition{origin + 1, 0} : ma::OccupiedMeshPosition{origin, length};
        const std::vector<ma::OccupiedTransportCell> moving{{mixture, a, b, 0}};
        if (reflected && length == 1e-16) {
            // The present local begin/end storage cannot encode dx-1e-16
            // with relative precision for this tiny target. It must fail,
            // whereas the reflected dyadic width has an exact representation.
            fatal(ma::FluidFatalCode::numerical_precision, [&] { (void)ma::remap_occupied_mixtures(*eos, moving, walls, 1, 1); });
        } else {
            const auto target = ma::remap_occupied_mixtures(*eos, moving, walls, 1, 1);
            near(target[origin].end.offset_m - target[origin].begin.offset_m, length, 4e-15);
            near(target[origin].mixture.mass(), mixture.mass(), 4e-15);
        }
        check(moving[0].mixture.masses == mixture.masses && moving[0].mixture.energy == mixture.energy,
            "Overlap geometry check mutated donor input");
    }
    auto w = world(8, ma::FluidAxis::x);
    w.set_occupied_cell(3, 0, water(w.registry(), *eos, 1), 0, 1, -1e-15);
    const auto original = w.cell(3, 0);
    fatal(ma::FluidFatalCode::numerical_precision, [&] { (void)w.step(0.1); });
    check(w.time() == 0 && same_cell(original, w.cell(3, 0)), "Unrepresentable left target partially published a world step");
}

void tiny_cut_receiver_gets_direct_share_and_large_overlap_gets_remainder() {
    const auto registry = ma::SpeciesRegistry::standard(); const auto eos = model(registry);
    constexpr double tiny_length = 1e-16;
    const double length = 0.875 + tiny_length;
    std::vector<double> masses(registry.size()); masses[registry.index("water")] = 1000 * length;
    masses[registry.index("liquid_nitrogen")] = 1e-28 * length;
    const auto mixture = eos->initialize_at_temperature(masses, 300, length).mixture;
    const std::vector<ma::OccupiedTransportCell> moving{{mixture, {2, 0.125}, {3, tiny_length}, mixture.mass() * 1.25}};
    const std::vector<std::uint8_t> walls(6, 0);
    const auto result = ma::remap_occupied_mixtures(*eos, moving, walls, 1, 1);
    near(result[3].mixture.masses[registry.index("water")], 1000 * tiny_length, 2e-14);
    near(result[3].mixture.masses[registry.index("liquid_nitrogen")], 1e-28 * tiny_length, 2e-14);
    check(result[3].begin.offset_m == 0 && result[3].end.offset_m == tiny_length,
        "Tiny positive wet receiver geometry was rounded to dry");
    for (std::size_t s = 0; s < registry.size(); ++s)
        near(result[2].mixture.masses[s] + result[3].mixture.masses[s], mixture.masses[s], 2e-14);
}

void materially_unresolved_thin_geometry_fails_with_whole_call_rollback() {
    auto w = world(8, ma::FluidAxis::x); const auto eos = model(w.registry());
    const double begin = 0.5, end = std::nextafter(begin, 1.0), length = end - begin;
    const auto mixture = water(w.registry(), *eos, length);
    w.set_occupied_cell(2, 0, mixture, begin, end, 1);
    const auto original = w.cell(2, 0);
    auto control = w; (void)control.step(0.1); check(control.time() == 0.1, "Thin geometry control must accept initial translation");
    fatal(ma::FluidFatalCode::numerical_precision, [&] { (void)w.step(0.6); });
    check(w.time() == 0 && w.cumulative_omitted_elastic_work_bound() == 0 && same_cell(w.cell(2, 0), original),
        "Unresolvable thin interval lost original conserved state/geometry/ledger");
}
}

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"explicit elastic initialization and caloric identity", initialization_preserves_temperature_and_accounts_for_elastic_energy},
        {"exact vacuum through FluidWorld step", true_vacuum_steps_without_unknowns_or_material},
        {"stationary cut liquid with occupied EOS volume", zero_pressure_cut_liquid_uses_occupied_geometry},
        {"occupied centroid gravitational energy", occupied_centroid_defines_gravitational_energy},
        {"hydrostatic cut column and one-sided free surface", hydrostatic_cut_column_balances_one_sided_surface_inertia},
        {"typed topology failure and atomic setters", unsupported_geometry_and_internal_void_fail_atomically},
        {"profile identity and explicit mode constraints", explicit_profile_and_mode_constraints_are_validated},
        {"translating liquid plug wet/dry and trace conservation", translating_liquid_plug_crosses_wet_dry_cells_without_drag_or_trace_loss},
        {"independent incremental kinetic heat and free momentum", incremental_heat_and_free_surface_momentum_match_independent_lumped_mechanics},
        {"fixed-grid Fourier velocity dt-to-zero consistency", fourier_velocity_has_an_identity_limit_as_timestep_vanishes},
        {"falling centroid gravity and hydrostatic wall reaction", gravity_falls_at_occupied_centroids_and_wall_reaction_balances_weight},
        {"gasless compressed release and fatal cavitation rollback", compressed_release_conserves_energy_without_gas_and_cavitation_is_fatal},
        {"whole-call rollback after moving geometry commit", failed_call_restores_geometry_after_an_accepted_moving_substep},
        {"reported sub-ULP motion and relative tiny-volume bounds", sub_ulp_motion_is_reported_and_tiny_volume_bounds_are_local},
        {"explicit fill policy bounds, traces and true EOS", explicit_fill_policy_is_gasless_bounded_and_keeps_true_eos_readings},
        {"material omitted elastic work is fatal and atomic", fill_policy_rejects_material_elastic_work_and_restores_world},
        {"liquid plug grid/timestep refinement and cumulative bounds", translating_plug_refines_grid_and_timestep_with_reported_error_bounds},
        {"zero-duration reports explicit nonzero omitted-work ledger", zero_duration_reports_an_explicit_nonzero_omitted_work_ledger},
        {"endpoint-local expansion accounting and translated coordinates", endpoint_volume_accounting_handles_expansion_and_translation_without_a_floor},
        {"denormal gas trace split fails atomically without deletion", positive_denormal_trace_is_never_deleted_by_endpoint_accounting},
        {"contracting pressure roots require endpoint-relative accuracy", contracting_pressure_root_requires_endpoint_relative_accuracy},
        {"reflected/translated target geometry accurate or atomic failure", reflected_translated_overlap_geometry_is_accurate_or_fails_atomically},
        {"tiny wet receiver direct share with conservative remainder", tiny_cut_receiver_gets_direct_share_and_large_overlap_gets_remainder},
        {"materially unresolvable thin geometry full rollback", materially_unresolved_thin_geometry_fails_with_whole_call_rollback}
    };
    std::size_t passed = 0, failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << passed << " integrated free-surface groups passed\n";
    return failed == 0 ? 0 : 1;
}
