#include "mixed_atmospheres/occupied_staggered.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, double relative = 1e-10, double absolute = 0) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > relative * std::abs(expected) + absolute) {
        std::ostringstream out; out << std::setprecision(17) << "Expected " << expected << ", got " << actual;
        throw std::runtime_error(out.str());
    }
}
template<class F> void fatal(FluidFatalCode code, F&& action) {
    bool caught = false; try { action(); } catch (const FluidFatalError& e) { caught = e.code() == code; }
    check(caught, "Expected typed retained-state failure");
}
template<class F> void rejects(F&& action) {
    bool caught = false; try { action(); } catch (const std::exception&) { caught = true; }
    check(caught, "Invalid retained operation was accepted");
}
struct Setup {
    SpeciesRegistry registry = SpeciesRegistry::standard();
    std::shared_ptr<const FluidThermodynamics> model;
    std::size_t gas = registry.index("nitrogen"), water = registry.index("water"), trace = registry.index("helium");
    double r = gas_constant / registry[gas].molar_mass, cv = registry[gas].cv(), temperature = 300;
    Setup() {
        std::vector<ReferenceMaterial> materials;
        for (const auto* id : {"nitrogen", "helium", "water"}) {
            const auto& s = registry[registry.index(id)]; ReferenceMaterial m;
            m.id = id; m.phase = s.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            m.molar_mass_kg_per_mol = s.molar_mass; m.cv_j_per_kg_k = s.cv(); m.energy_offset_j_per_kg = s.internal_energy(0);
            m.minimum_temperature_k = 1e-20; m.maximum_temperature_k = 1e9;
            m.zero_pressure_density_kg_per_m3 = s.phase == Phase::gas ? 0 : 1000;
            m.reference_bulk_modulus_pa = s.phase == Phase::gas ? 0 : 2.2e9;
            m.maximum_density_ratio = s.phase == Phase::gas ? 1 : 2;
            m.provenance = "Explicit retained material-state test surrogate; not real-fluid calibration";
            materials.push_back(m);
        }
        model = std::make_shared<const FluidThermodynamics>(registry, "retained-staggered-gate1-v1", std::move(materials));
    }
    [[nodiscard]] double sound_speed() const { return std::sqrt((1 + r / cv) * r * temperature); }
    [[nodiscard]] MixtureInventory gas_cell(double mass, double volume, double t = 300, double trace_mass = 0) const {
        std::vector<double> masses(registry.size()); masses[gas] = mass; masses[trace] = trace_mass;
        return model->initialize_at_temperature(masses, t, volume).mixture;
    }
    [[nodiscard]] MixtureInventory liquid_cell(double volume, double pressure = 0) const {
        std::vector<double> masses(registry.size()); masses[water] = 1000 * (1 + pressure / 2.2e9) * volume;
        return model->initialize_at_temperature(masses, 300, volume).mixture;
    }
    [[nodiscard]] StaggeredInitialization uniform(std::size_t count, bool liquid = false, double speed = 0) const {
        StaggeredInitialization input; input.thermodynamics = model;
        for (std::size_t i = 0; i < count; ++i) {
            const auto mix = liquid ? liquid_cell(1) : gas_cell(1, 1);
            input.cells.push_back({mix, mix.mass() * speed}); input.positions_m.push_back(static_cast<double>(i));
            input.velocities_m_per_s.push_back(speed);
        }
        input.positions_m.push_back(static_cast<double>(count)); input.velocities_m_per_s.push_back(speed); return input;
    }
};
void compatible_momenta(StaggeredInitialization& input) {
    for (std::size_t i = 0; i < input.cells.size(); ++i)
        input.cells[i].bulk_momentum_kg_m_per_s = input.cells[i].mixture.mass() *
            std::midpoint(input.velocities_m_per_s[i], input.velocities_m_per_s[i + 1]);
}
std::string fingerprint(const OccupiedStaggeredState& state) {
    std::ostringstream out; out << std::hexfloat << state.initialized() << state.model().get() << state.model_metadata();
    for (double x : {state.origin_high_m(), state.origin_low_m(), state.time_seconds(), state.area_m2(), state.gravity_m_per_s2(),
                    state.cumulative_energy_balance_error_j(), state.cumulative_geometry_volume_residual_m3()}) out << x << ';';
    const auto& l = state.initialization_ledger();
    for (double x : {l.prior_bulk_kinetic_energy_j, l.retained_kinetic_energy_j, l.additional_mechanical_energy_j,
        l.requested_internal_energy_change_j, l.represented_internal_energy_change_j, l.declared_added_energy_j,
        l.represented_total_energy_change_j, l.kinetic_partition_roundoff_j, l.momentum_reconciliation_kg_m_per_s,
        l.maximum_momentum_arithmetic_bound_kg_m_per_s}) out << x << ';';
    out << l.unresolved_energy_conversions;
    for (const auto& n : state.nodes()) out << n.relative_position_m << n.inertia_kg << n.momentum_kg_m_per_s << n.fixed;
    for (const auto& c : state.cells()) {
        out << c.mixture.energy << c.occupied_volume_m3 << c.bulk_momentum_kg_m_per_s << c.mechanical_variance_j;
        for (double x : c.mixture.masses) out << x << ';';
        const auto& e = c.thermodynamics;
        for (double x : {e.temperature_k, e.mechanical_pressure_pa, e.gas_pressure_pa, e.gas_volume_m3, e.liquid_volume_m3,
            e.vacuum_volume_m3, e.reference_energy_j, e.thermal_energy_j, e.elastic_energy_j, e.entropy_j_per_k,
            e.helmholtz_energy_j, e.intrinsic_heat_capacity_j_per_k, e.equilibrium_heat_capacity_at_cell_volume_j_per_k,
            e.volume_residual_m3, e.energy_residual_j}) out << x << ';';
        for (double x : e.partial_volumes_m3) out << x << ';';
        for (double x : e.specific_internal_energies_j_per_kg) out << x << ';';
        out << e.has_gas << e.has_vacuum << e.pressure_iterations;
    }
    return out.str();
}
void mass_and_identity(const OccupiedStaggeredState& state) {
    const auto totals = state.totals(); double cell_p = 0;
    for (std::size_t i = 0; i < state.cells().size(); ++i) {
        const auto& c = state.cells()[i]; const double mass = c.mixture.mass();
        const double ul = state.nodes()[i].velocity_m_per_s(), ur = state.nodes()[i + 1].velocity_m_per_s();
        near(c.bulk_momentum_kg_m_per_s, mass * (ul + ur) / 2);
        near(c.mechanical_variance_j, mass * (ur - ul) * (ur - ul) / 8);
        cell_p += c.bulk_momentum_kg_m_per_s;
    }
    near(totals.nodal_kinetic_energy_j, totals.bulk_kinetic_energy_j + totals.mechanical_variance_j);
    near(totals.momentum_kg_m_per_s, cell_p, 1e-12, 1e-13 * std::sqrt(totals.nodal_kinetic_energy_j));
}
void initialization_and_identities() {
    Setup s; auto in = s.uniform(3); in.velocities_m_per_s = {-2, 1, 4, -3};
    in.cells[1].mixture = s.gas_cell(2, 1); in.cells[2].mixture = s.gas_cell(3, 1); compatible_momenta(in);
    auto state = initialize_occupied_staggered(in); mass_and_identity(state);
    const auto t = state.totals(); const auto& l = state.initialization_ledger();
    near(l.retained_kinetic_energy_j, t.nodal_kinetic_energy_j);
    near(l.additional_mechanical_energy_j, t.mechanical_variance_j);
    near(l.declared_added_energy_j, t.mechanical_variance_j);
    near(l.represented_total_energy_change_j, l.declared_added_energy_j);
    for (std::size_t i = 0; i < in.cells.size(); ++i) {
        check(state.cells()[i].mixture.energy == in.cells[i].mixture.energy, "Temperature policy altered U");
        near(state.cells()[i].thermodynamics.temperature_k, 300);
    }
    const auto model = state.model(); in.thermodynamics.reset(); s.model.reset();
    check(model && state.model_metadata() == model->reference().metadata(), "EOS lifetime or identity is borrowed");
}
void mutable_model_alias_cannot_change_retained_state() {
    Setup s; auto mutable_alias = std::make_shared<FluidThermodynamics>(*s.model);
    auto input = s.uniform(3); input.thermodynamics = mutable_alias;
    auto state = initialize_occupied_staggered(input), control = state;
    StaggeredInitialization empty; empty.thermodynamics = mutable_alias;
    const auto vacuum = initialize_occupied_staggered(empty);
    // This check also makes the old implementation fail safely, before any
    // larger-registry assignment could expose its stale cell vectors to OOB.
    check(state.model().get() != mutable_alias.get() && vacuum.model().get() != mutable_alias.get(),
        "Initializer retained a const view of a mutable input allocation");
    const auto original_metadata = s.model->reference().metadata();
    const auto before = fingerprint(state), empty_before = fingerprint(vacuum);
    SpeciesRegistry changed;
    for (auto species : s.registry.all()) {
        if (species.id == "nitrogen") species.cp *= 1.1;
        changed.add(std::move(species));
    }
    auto changed_profiles = s.model->reference().materials();
    for (auto& material : changed_profiles) {
        const auto& species = changed[changed.index(material.id)];
        material.cv_j_per_kg_k = species.cv(); material.energy_offset_j_per_kg = species.internal_energy(0);
        if (material.phase == ReferencePhase::elastic_liquid) material.reference_bulk_modulus_pa *= 2;
        material.provenance = "Deliberately replaced mutable caller profile";
    }
    *mutable_alias = FluidThermodynamics(changed, "mutated-same-size-v2", changed_profiles);
    check(mutable_alias->registry_size() == s.registry.size() && mutable_alias->reference().metadata() != original_metadata,
        "Same-size model mutation fixture did not change the supplied EOS");
    check(state.model_metadata() == original_metadata && state.model()->reference().metadata() == original_metadata &&
        vacuum.model_metadata() == original_metadata && vacuum.model()->reference().metadata() == original_metadata,
        "Mutable alias changed owned EOS profiles/metadata");
    state.model()->validate_registry(s.registry);
    check(fingerprint(state) == before && fingerprint(vacuum) == empty_before, "Mutable alias invalidated retained caches/state");
    (void)advance_occupied_staggered(state, 1e-5); (void)advance_occupied_staggered(control, 1e-5);
    check(fingerprint(state) == fingerprint(control), "Mutable same-size model changed the next material step");
    SpeciesRegistry larger = s.registry; auto extra = larger[s.gas]; extra.id = "alias_extra_nitrogen"; larger.add(std::move(extra));
    *mutable_alias = FluidThermodynamics(larger, "mutated-larger-registry-v3", s.model->reference().materials());
    check(mutable_alias->registry_size() > s.registry.size(), "Larger-registry mutation fixture did not grow");
    // Verify dimensions BEFORE invoking totals or a step. Even a regression
    // must never knowingly execute the old stale-vector out-of-bounds path.
    check(state.model()->registry_size() == s.registry.size() && vacuum.model()->registry_size() == s.registry.size(),
        "Caller registry replacement changed retained inventory dimensions");
    state.model()->validate_registry(s.registry);
    check(state.model()->reference().metadata() == original_metadata && fingerprint(vacuum) == empty_before,
        "Caller registry replacement changed retained profiles or vacuum state");
    const auto actual = state.totals(), expected = control.totals();
    check(actual.masses_kg == expected.masses_kg, "Caller registry replacement changed totals");
    near(actual.internal_energy_j, expected.internal_energy_j); near(actual.nodal_kinetic_energy_j, expected.nodal_kinetic_energy_j);
    check(vacuum.totals().masses_kg == std::vector<double>(s.registry.size()), "Vacuum registry changed after alias replacement");
    (void)advance_occupied_staggered(state, 1e-5); (void)advance_occupied_staggered(control, 1e-5);
    check(fingerprint(state) == fingerprint(control), "Larger caller registry changed next step or cached EOS");
}
StaggeredInitialization finite_per_cell_mass_range_input() {
    // This dyadic, zero-pressure surrogate makes every individual EOS state,
    // volume, heat capacity, U, nodal inertia and their energy sums finite.
    // Only the positive constituent mass sum exceeds double's finite range.
    SpeciesRegistry registry; SpeciesDefinition species;
    species.id = "dyadic_liquid"; species.name = "Mass range test liquid"; species.phase = Phase::liquid;
    species.atoms = {{"H", 2}, {"O", 1}}; species.cp = std::ldexp(1.0, -10); species.density = 1024;
    species.formation_enthalpy = species.cp * reference_temperature * molecular_mass(species.atoms);
    registry.add(species); const auto& registered = registry[0];
    ReferenceMaterial material;
    material.id = registered.id; material.phase = ReferencePhase::elastic_liquid;
    material.molar_mass_kg_per_mol = registered.molar_mass; material.cv_j_per_kg_k = registered.cv();
    material.energy_offset_j_per_kg = registered.internal_energy(0);
    material.minimum_temperature_k = .5; material.maximum_temperature_k = 2;
    material.zero_pressure_density_kg_per_m3 = 1024; material.reference_bulk_modulus_pa = 1024;
    material.maximum_density_ratio = 2; material.provenance = "Dyadic arithmetic range test only, not a physical material";
    const auto model = std::make_shared<const FluidThermodynamics>(registry, "finite-cell-mass-overflow-v1", std::vector{material});
    const double mass = std::ldexp(1.0, 1016), occupied = std::ldexp(1.0, 1006);
    const auto initialized = model->initialize_at_temperature(std::vector{mass}, 1, occupied);
    const auto independently_evaluated = model->evaluate(initialized.mixture, occupied);
    near(independently_evaluated.temperature_k, 1); near(independently_evaluated.mechanical_pressure_pa, 0);
    check(std::isfinite(initialized.mixture.energy) && std::isfinite(independently_evaluated.intrinsic_heat_capacity_j_per_k),
        "Mass range fixture violates a per-cell EOS range first");
    StaggeredInitialization input; input.thermodynamics = model; input.area_m2 = occupied;
    for (std::size_t i = 0; i < 512; ++i) {
        input.cells.push_back({initialized.mixture, 0}); input.positions_m.push_back(static_cast<double>(i));
        input.velocities_m_per_s.push_back(0);
    }
    input.positions_m.push_back(512); input.velocities_m_per_s.push_back(0);
    return input;
}
void finite_per_cell_liquid_mass_total_overflow_is_atomic() {
    const auto input = finite_per_cell_mass_range_input();
    auto finite_input = input; finite_input.cells.resize(255); finite_input.positions_m.resize(256); finite_input.velocities_m_per_s.resize(256);
    const auto finite_state = initialize_occupied_staggered(finite_input); const auto finite_totals = finite_state.totals();
    near(finite_totals.masses_kg[0], std::ldexp(255.0, 1016), 0);
    check(std::isfinite(finite_totals.internal_energy_j), "Near-limit finite mass control has invalid energy total");
    Setup s; auto output = initialize_occupied_staggered(s.uniform(1)); (void)advance_occupied_staggered(output, 1e-5);
    const auto before = fingerprint(output);
    fatal(FluidFatalCode::numerical_precision, [&] { initialize_occupied_staggered_into(input, output); });
    check(fingerprint(output) == before, "Overflowing constituent sum published a partial initializer/model/cache");
    std::cout << "Mass range finite cells=255 total=" << std::setprecision(17) << finite_totals.masses_kg[0] <<
        "; 512 finite-EOS cells rejected before publication\n";
}
void conserved_energy_initialization_and_conflicts() {
    Setup s; auto in = s.uniform(2); in.velocities_m_per_s = {-4, 2, 6}; compatible_momenta(in);
    in.energy = StaggeredInitializationEnergy::preserve_total_energy;
    auto state = initialize_occupied_staggered(in); const auto& ledger = state.initialization_ledger();
    near(ledger.requested_internal_energy_change_j, -ledger.additional_mechanical_energy_j);
    near(ledger.declared_added_energy_j, 0);
    near(ledger.represented_total_energy_change_j, 0, 0, 1e-9);
    check(state.cells()[0].thermodynamics.temperature_k < 300, "Conserved energy conversion created temperature");
    mass_and_identity(state); const auto before = fingerprint(state);
    in.cells[1].bulk_momentum_kg_m_per_s += .1;
    fatal(FluidFatalCode::unsupported_configuration, [&] { initialize_occupied_staggered_into(in, state); });
    check(fingerprint(state) == before, "Conflicting momentum initialization mutated output");
    in.velocities_m_per_s = {-1e6, 0, 1e6}; compatible_momenta(in);
    rejects([&] { initialize_occupied_staggered_into(in, state); });
    check(fingerprint(state) == before, "Failed thermal conversion mutated output");
}
void initialization_arithmetic_ledgers() {
    Setup s; auto in = s.uniform(1, false, 2);
    in.cells[0].bulk_momentum_kg_m_per_s = std::nextafter(2.0, 3.0);
    const auto reconciled = initialize_occupied_staggered(in); const auto& l = reconciled.initialization_ledger();
    check(l.momentum_reconciliation_kg_m_per_s < 0 &&
        std::abs(l.momentum_reconciliation_kg_m_per_s) <= l.maximum_momentum_arithmetic_bound_kg_m_per_s,
        "Accepted arithmetic momentum difference was hidden");
    in.cells[0].bulk_momentum_kg_m_per_s = 2 * (1 + 1e-10);
    fatal(FluidFatalCode::unsupported_configuration, [&] { (void)initialize_occupied_staggered(in); });
    in = s.uniform(1); in.velocities_m_per_s = {-1e-6, 1e-6}; compatible_momenta(in);
    in.energy = StaggeredInitializationEnergy::preserve_total_energy;
    const auto small = initialize_occupied_staggered(in); const auto& tiny = small.initialization_ledger();
    check(tiny.unresolved_energy_conversions == 1 && tiny.additional_mechanical_energy_j > 0 &&
        tiny.represented_total_energy_change_j != 0, "Unresolved initialization energy conversion was called exact");
    near(tiny.represented_total_energy_change_j, tiny.additional_mechanical_energy_j);
    check(small.cells()[0].mixture.energy == in.cells[0].mixture.energy, "Sub-ULP energy fixture unexpectedly resolved");
    in = s.uniform(1, false, 1e8); auto fast = initialize_occupied_staggered(in); const auto prior = fast;
    const auto step = advance_occupied_staggered(fast, 1e-14);
    check(step.unresolved_momentum_updates == 2, "Sub-ULP nodal impulses were not reported");
    check(fast.nodes()[0].momentum_kg_m_per_s == prior.nodes()[0].momentum_kg_m_per_s &&
        fast.nodes()[1].momentum_kg_m_per_s == prior.nodes()[1].momentum_kg_m_per_s,
        "Sub-ULP momentum fixture unexpectedly resolved");
}
void exact_vacuum_and_stationary_liquid() {
    Setup s; StaggeredInitialization empty; empty.thermodynamics = s.model;
    auto vacuum = initialize_occupied_staggered(empty); const auto r = advance_occupied_staggered(vacuum, .25);
    check(vacuum.cells().empty() && vacuum.nodes().empty() && r.pressure_iterations == 0, "Vacuum acquired unknowns");
    near(vacuum.time_seconds(), .25); near(vacuum.totals().internal_energy_j, 0);
    auto in = s.uniform(3, true); auto state = initialize_occupied_staggered(in); const auto old = state;
    const auto step = advance_occupied_staggered(state, .1);
    near(step.represented_internal_energy_change_j, 0); near(step.represented_kinetic_energy_change_j, 0);
    for (std::size_t i = 0; i < state.nodes().size(); ++i) {
        check(state.nodes()[i].relative_position_m == old.nodes()[i].relative_position_m &&
            state.nodes()[i].momentum_kg_m_per_s == 0, "Stationary liquid moved");
    }
}
void uniform_translation_and_coordinates() {
    Setup s;
    for (double speed : {-3.25, 3.25}) for (double origin : {-1e100, 0.0, 1e100}) {
        auto in = s.uniform(3, true, speed); in.frame_origin_m = origin;
        in.positions_m = {-2, -.5, .25, 2};
        for (std::size_t i = 0; i < in.cells.size(); ++i) in.cells[i].mixture = s.liquid_cell(in.positions_m[i + 1] - in.positions_m[i]);
        compatible_momenta(in); auto state = initialize_occupied_staggered(in); const auto before = state;
        for (int i = 0; i < 20; ++i) {
            const auto r = advance_occupied_staggered(state, .125);
            near(r.represented_internal_energy_change_j, 0); near(r.represented_kinetic_energy_change_j, 0);
        }
        for (std::size_t j = 0; j < state.nodes().size(); ++j) {
            check(state.nodes()[j].relative_position_m == before.nodes()[j].relative_position_m, "Uniform translation distorted material geometry");
            near(state.nodes()[j].velocity_m_per_s(), speed);
        }
        for (std::size_t i = 0; i < state.cells().size(); ++i) {
            check(state.cells()[i].mixture.energy == before.cells()[i].mixture.energy, "Translation heated liquid");
            check(state.cells()[i].mechanical_variance_j == 0, "Translation created variance");
        }
        // The low component retains translations below the ULP of a large origin.
        const long double delta = (static_cast<long double>(state.origin_high_m()) - before.origin_high_m()) +
            (static_cast<long double>(state.origin_low_m()) - before.origin_low_m());
        near(static_cast<double>(delta), speed * 2.5);
    }
}
void local_geometry_scale_extremes() {
    Setup s;
    for (int exponent : {-300, 300}) for (double speed : {-2.0, 2.0}) {
        const double dx = std::ldexp(1.0, exponent); auto input = s.uniform(2, true, speed);
        input.positions_m = {0, dx, 2 * dx}; input.frame_origin_m = 1e100;
        for (auto& cell : input.cells) cell.mixture = s.liquid_cell(dx);
        compatible_momenta(input); auto state = initialize_occupied_staggered(input); const auto old = state;
        const auto report = advance_occupied_staggered(state, dx / 1e4);
        check(state.cells()[0].occupied_volume_m3 == dx && state.cells()[1].occupied_volume_m3 == dx,
            "Local geometry scale changed through translation");
        check(state.totals().masses_kg == old.totals().masses_kg, "Scale-extreme material changed");
        near(report.absolute_geometry_volume_residual_m3, 0); near(report.origin_displacement_roundoff_m, 0);
        for (const auto& node : state.nodes()) near(node.velocity_m_per_s(), speed);
    }
}
void isolated_symmetric_expansion_retains_mechanics() {
    Setup s;
    for (bool liquid : {false, true}) {
        auto in = s.uniform(1, liquid);
        if (liquid) in.cells[0].mixture = s.liquid_cell(1, 2.2e8);
        auto state = initialize_occupied_staggered(in); const auto old = state;
        const auto r = advance_occupied_staggered(state, liquid ? 1e-5 : 1e-4);
        const auto totals = state.totals(); check(totals.nodal_kinetic_energy_j > 0, "Expansion discarded nodal K");
        near(state.cells()[0].bulk_momentum_kg_m_per_s, 0, 0, 1e-12);
        near(totals.nodal_kinetic_energy_j, state.cells()[0].mechanical_variance_j);
        near(r.energy_balance_error_j, r.nonlinear_pressure_residual_work_j + r.pressure_work_roundoff_j +
            r.gravity_geometry_work_residual_j, 0, r.energy_accounting_arithmetic_bound_j);
        // The root's finite nonlinear tolerance is an actual energy residual,
        // not heat. Independently constrain it relative to actual expansion K.
        near(r.energy_balance_error_j, 0, 0, 1e-8 * r.represented_kinetic_energy_change_j + 1e-7);
        std::cout << "Expansion liquid=" << liquid << " E_residual=" << r.energy_balance_error_j <<
            " nonlinear_work=" << r.nonlinear_pressure_residual_work_j << " U_roundoff=" << r.pressure_work_roundoff_j <<
            " accounting_remainder=" << r.energy_accounting_remainder_j << '\n';
        check(state.cells()[0].mixture.energy < old.cells()[0].mixture.energy, "Expansion variance became heat");
        near(state.cells()[0].thermodynamics.entropy_j_per_k, old.cells()[0].thermodynamics.entropy_j_per_k, 0, 2e-7);
        check(state.cells()[0].mixture.masses == old.cells()[0].mixture.masses, "Expansion changed species");
        if (!liquid) {
            const double k = totals.nodal_kinetic_energy_j;
            for (int j = 0; j < 8; ++j) (void)advance_occupied_staggered(state, 1e-4);
            check(state.totals().nodal_kinetic_energy_j > k, "Retained expansion moment was reset on subsequent steps");
        }
        mass_and_identity(state);
    }
}
void independent_single_cell_adiabatic_solution() {
    Setup s;
    for (bool liquid : {false, true}) {
        auto input = s.uniform(1, liquid); if (liquid) input.cells[0].mixture = s.liquid_cell(1, 2.2e8);
        const double dt = liquid ? 1e-5 : 1e-4, mass = input.cells[0].mixture.mass();
        // Independent closed-form U(V): ideal-gas adiabat or integrated
        // zero-expansion liquid p(V). This does not query the reference's
        // isentrope, work derivative, pressure root or returned residuals.
        const auto independent_work = [&](double speed) {
            const double dv = dt * speed;
            if (speed == 0) return liquid ? 2.2e8 : s.r * 300;
            if (!liquid) return -mass * s.cv * 300 * std::expm1(-(s.r / s.cv) * std::log1p(dv)) / dv;
            const double nominal = mass / 1000;
            const auto elastic = [&](double v) {
                const double q = nominal / v - 1;
                return mass * 2.2e6 * (std::log1p(q) - q / (1 + q));
            };
            return (elastic(1) - elastic(1 + dv)) / dv;
        };
        double a = 0, b = liquid ? .05 / dt : std::sqrt(2 * s.cv * 300);
        for (int k = 0; k < 100; ++k) {
            const double speed = std::midpoint(a, b);
            if (speed - 2 * dt * independent_work(speed) / mass > 0) b = speed; else a = speed;
        }
        const double exact_speed = std::midpoint(a, b), exact_volume = 1 + dt * exact_speed;
        auto state = initialize_occupied_staggered(input); const auto old = state;
        const auto result = advance_occupied_staggered(state, dt);
        near(state.nodes().back().velocity_m_per_s(), exact_speed, liquid ? 2e-8 : 1e-10);
        near(state.cells()[0].occupied_volume_m3, exact_volume, 1e-12);
        const double exact_temperature = liquid ? 300 : 300 * std::pow(exact_volume, -s.r / s.cv);
        near(state.cells()[0].thermodynamics.temperature_k, exact_temperature, 1e-11);
        const double exact_pressure = liquid ? 2.2e9 * (mass / (1000 * exact_volume) - 1) : mass * s.r * exact_temperature / exact_volume;
        near(state.cells()[0].thermodynamics.mechanical_pressure_pa, exact_pressure, 1e-10);
        const double entropy_change = state.cells()[0].thermodynamics.entropy_j_per_k - old.cells()[0].thermodynamics.entropy_j_per_k;
        const double energy_scale = old.cells()[0].thermodynamics.thermal_energy_j + old.cells()[0].thermodynamics.elastic_energy_j;
        near(entropy_change, 0, 0, 2e-13 * energy_scale / 300);
        near(result.energy_balance_error_j, 0, 0, 2e-11 * energy_scale);
        std::cout << "Independent adiabat liquid=" << liquid << " speed=" << state.nodes().back().velocity_m_per_s() <<
            " analytic=" << exact_speed << " entropy_change=" << entropy_change << '\n';
    }
}
void fourier_dt_to_zero() {
    Setup s; constexpr std::size_t n = 16; auto input = s.uniform(n);
    input.left = input.right = StaggeredBoundary::rigid_wall;
    for (std::size_t j = 0; j <= n; ++j) input.velocities_m_per_s[j] =
        j == 0 || j == n ? 0 : std::sin(4 * std::numbers::pi * static_cast<double>(j) / n);
    compatible_momenta(input);
    double prior_error = 1;
    for (double dt : {1e-6, 1e-8, 1e-10}) {
        auto state = initialize_occupied_staggered(input); const auto old = state;
        const auto r = advance_occupied_staggered(state, dt); double numerator = 0, denominator = 0;
        for (std::size_t j = 0; j <= n; ++j) {
            numerator += state.nodes()[j].velocity_m_per_s() * input.velocities_m_per_s[j];
            denominator += input.velocities_m_per_s[j] * input.velocities_m_per_s[j];
        }
        const double amplitude = numerator / denominator, error = std::abs(amplitude - 1);
        check(error < prior_error, "Retained Fourier velocity has a nonvanishing dt-to-zero defect"); prior_error = error;
        near(amplitude, 1, 0, 2e-7);
        for (std::size_t i = 0; i < n; ++i) check(state.cells()[i].mixture.masses == old.cells()[i].mixture.masses, "Fourier species changed");
        near(r.energy_balance_error_j, 0, 0, 2e-8);
        std::cout << "Fourier dt=" << dt << " amplitude=" << std::setprecision(17) << amplitude << '\n';
    }
}
void alternating_pressure_is_not_a_null_mode() {
    Setup s; auto input = s.uniform(8); input.left = input.right = StaggeredBoundary::rigid_wall;
    for (std::size_t i = 0; i < input.cells.size(); ++i) input.cells[i].mixture = s.gas_cell(1, 1, 300 * (i % 2 == 0 ? 1.001 : .999));
    auto state = initialize_occupied_staggered(input); const auto old = state;
    constexpr double dt = 1e-8; (void)advance_occupied_staggered(state, dt);
    for (std::size_t j = 1; j < state.nodes().size() - 1; ++j) {
        const double analytic_impulse = dt * (old.cells()[j - 1].thermodynamics.mechanical_pressure_pa - old.cells()[j].thermodynamics.mechanical_pressure_pa);
        near(state.nodes()[j].momentum_kg_m_per_s, analytic_impulse, 1e-7);
        check(state.nodes()[j].momentum_kg_m_per_s != 0, "Pressure checkerboard survived retained nodal gradient");
    }
}
struct AcousticError { double velocity = 0, pressure = 0, phase = 0; };
AcousticError acoustic(std::size_t count, double courant, bool print = true) {
    Setup s; const double dx = 1 / static_cast<double>(count), c = s.sound_speed(), amplitude = c * 1e-7;
    auto input = s.uniform(count); input.left = input.right = StaggeredBoundary::rigid_wall;
    for (std::size_t j = 0; j <= count; ++j) {
        input.positions_m[j] = static_cast<double>(j) * dx;
        input.velocities_m_per_s[j] = j == 0 || j == count ? 0 : amplitude * std::sin(std::numbers::pi * input.positions_m[j]);
    }
    for (auto& cell : input.cells) cell.mixture = s.gas_cell(dx, dx);
    compatible_momenta(input); auto state = initialize_occupied_staggered(input);
    const double target = .17 / c, step = courant * dx / c;
    const auto steps = static_cast<std::size_t>(std::ceil(target / step)); const double dt = target / static_cast<double>(steps);
    for (std::size_t j = 0; j < steps; ++j) (void)advance_occupied_staggered(state, dt);
    const double angle = std::numbers::pi * c * target, p0 = s.r * 300;
    double mode = 0, norm = 0; AcousticError error;
    for (std::size_t j = 0; j <= count; ++j) {
        const double shape = std::sin(std::numbers::pi * static_cast<double>(j) * dx);
        const double normalized = state.nodes()[j].velocity_m_per_s() / amplitude;
        error.velocity += dx * std::abs(normalized - shape * std::cos(angle)); mode += normalized * shape; norm += shape * shape;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const double x = (static_cast<double>(i) + .5) * dx;
        const double perturbation = (state.cells()[i].thermodynamics.mechanical_pressure_pa - p0) / (c * amplitude);
        error.pressure += dx * std::abs(perturbation + std::cos(std::numbers::pi * x) * std::sin(angle));
    }
    const double inferred = std::acos(mode / norm) / (std::numbers::pi * target);
    error.phase = std::abs(inferred / c - 1);
    if (print) std::cout << "Acoustic N=" << count << " C=" << courant << " velocity_L1=" << error.velocity <<
        " pressure_L1=" << error.pressure << " speed_relative=" << error.phase << '\n';
    mass_and_identity(state); return error;
}
void independent_acoustic_grid_and_time_refinement() {
    const auto coarse = acoustic(16, .1), medium = acoustic(32, .1), fine = acoustic(64, .1);
    check(medium.velocity < coarse.velocity * .35 && fine.velocity < medium.velocity * .4, "Acoustic velocity lacks independent grid convergence");
    check(medium.pressure < coarse.pressure * .4 && fine.pressure < medium.pressure * .4, "Acoustic pressure lacks independent grid convergence");
    check(fine.phase < 2e-4, "Acoustic phase gives incorrect sound speed");
    // At fixed grid, compare to a much finer temporal solution. This separates
    // temporal error from the spatial dispersion of the same discrete mode.
    const auto t0 = acoustic(16, 1.6), t1 = acoustic(16, .8), t2 = acoustic(16, .4), reference = acoustic(16, .025, false);
    const double e0 = std::abs(t0.phase - reference.phase), e1 = std::abs(t1.phase - reference.phase), e2 = std::abs(t2.phase - reference.phase);
    check(e1 < e0 * .4 && e2 < e1 * .4, "Acoustic fixed-grid time refinement failed");
}
void gravity_freefall_and_wall_reaction() {
    Setup s; auto input = s.uniform(3, true, .3); input.gravity_m_per_s2 = 9.81; input.frame_origin_m = 1e80;
    auto state = initialize_occupied_staggered(input); const auto old = state; const double dt = .01;
    const auto r = advance_occupied_staggered(state, dt);
    for (const auto& node : state.nodes()) near(node.velocity_m_per_s(), .3 - 9.81 * dt);
    near(r.represented_potential_energy_change_j, 3000 * 9.81 * (.3 * dt - .5 * 9.81 * dt * dt), 1e-12);
    near(r.represented_kinetic_energy_change_j, -r.represented_potential_energy_change_j, 1e-12);
    near(r.wall_reaction_impulse_kg_m_per_s, 0); check(state.totals().masses_kg == old.totals().masses_kg, "Freefall changed constituents");
    input = s.uniform(3); input.gravity_m_per_s2 = 9.81; input.left = input.right = StaggeredBoundary::rigid_wall;
    input.cells[1].mixture = s.gas_cell(2, 1); input.cells[2].mixture = s.gas_cell(3, 1);
    input.velocities_m_per_s = {0, .2, -.1, 0}; compatible_momenta(input);
    state = initialize_occupied_staggered(input); const auto prior = state;
    const auto wall = advance_occupied_staggered(state, 1e-8);
    const double left_p = prior.cells().front().thermodynamics.mechanical_pressure_pa, right_p = prior.cells().back().thermodynamics.mechanical_pressure_pa;
    const double expected = 1e-8 * (left_p - right_p + 9.81 * (prior.nodes().front().inertia_kg + prior.nodes().back().inertia_kg));
    near(wall.wall_reaction_impulse_kg_m_per_s, expected, 2e-8);
    check(state.nodes().front().relative_position_m == prior.nodes().front().relative_position_m &&
        state.nodes().back().relative_position_m == prior.nodes().back().relative_position_m, "Wall coordinates changed");
    near(wall.energy_balance_error_j, 0, 0, 2e-8); near(wall.momentum_balance_error_kg_m_per_s, 0, 0, 1e-13);
    mass_and_identity(state);
}
void reflected_single_wall_material_motion() {
    Setup s; auto left = s.uniform(2), right = left;
    left.left = StaggeredBoundary::rigid_wall; right.right = StaggeredBoundary::rigid_wall;
    left.frame_origin_m = right.frame_origin_m = 1e100;
    auto a = initialize_occupied_staggered(left), b = initialize_occupied_staggered(right);
    const auto old_a = a, old_b = b;
    for (int j = 0; j < 8; ++j) {
        const auto ra = advance_occupied_staggered(a, 1e-5), rb = advance_occupied_staggered(b, 1e-5);
        near(ra.wall_reaction_impulse_kg_m_per_s, -rb.wall_reaction_impulse_kg_m_per_s, 1e-11);
    }
    for (std::size_t j = 0; j < a.nodes().size(); ++j) {
        near(a.nodes()[j].velocity_m_per_s(), -b.nodes()[b.nodes().size() - 1 - j].velocity_m_per_s(), 1e-11);
    }
    near(a.totals().nodal_kinetic_energy_j, b.totals().nodal_kinetic_energy_j, 1e-11);
    check(a.origin_high_m() == old_a.origin_high_m() && a.origin_low_m() == old_a.origin_low_m() &&
        b.origin_high_m() == old_b.origin_high_m() && b.origin_low_m() == old_b.origin_low_m(), "Reflected rigid wall changed actual coordinate");
    check(a.nodes().front().relative_position_m == 0 && b.nodes().back().relative_position_m == 0,
        "Single wall anchor was not retained");
}
void hydrostatic_column() {
    Setup s; auto input = s.uniform(3, true); input.gravity_m_per_s2 = 9.81; input.left = StaggeredBoundary::rigid_wall;
    const double a = 1000 * 9.81 / (2 * 2.2e9); double p = 1000 * 9.81 / (2 * (1 - a));
    for (std::size_t j = 3; j-- > 0;) {
        input.cells[j].mixture = s.liquid_cell(1, p);
        p = (p * (1 + a) + 1000 * 9.81) / (1 - a);
    }
    auto state = initialize_occupied_staggered(input); const auto totals = state.totals();
    const double mass = std::accumulate(totals.masses_kg.begin(), totals.masses_kg.end(), 0.0);
    const auto r = advance_occupied_staggered(state, .01);
    for (const auto& node : state.nodes()) near(node.velocity_m_per_s(), 0, 0, 1e-9);
    near(r.wall_reaction_impulse_kg_m_per_s, mass * 9.81 * .01, 1e-9);
}
void atomic_failures_and_trace_preservation() {
    Setup s; auto input = s.uniform(2); input.cells[1].mixture = s.gas_cell(1, 1, 300, std::numeric_limits<double>::denorm_min());
    auto state = initialize_occupied_staggered(input); (void)advance_occupied_staggered(state, 1e-5);
    check(state.cells()[1].mixture.masses[s.trace] == std::numeric_limits<double>::denorm_min(), "Denormal trace was dropped");
    const auto before = fingerprint(state);
    for (double dt : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        fatal(FluidFatalCode::unsupported_configuration, [&] { (void)advance_occupied_staggered(state, dt); });
        check(fingerprint(state) == before, "Failed step mutated state/time/cache/ledger");
    }
    fatal(FluidFatalCode::numerical_precision, [&] { (void)advance_occupied_staggered(state, 1e200); });
    check(fingerprint(state) == before, "Late pressure failure mutated accepted state/time/cache/ledger");
    input.cells[0].mixture = MixtureInventory(s.registry.size());
    fatal(FluidFatalCode::unsupported_topology, [&] { initialize_occupied_staggered_into(input, state); });
    check(fingerprint(state) == before, "Failed topology initialization mutated output");
    input = s.uniform(1, true); input.velocities_m_per_s = {-1, 1}; compatible_momenta(input);
    auto cavitating = initialize_occupied_staggered(input); const auto cavitation_before = fingerprint(cavitating);
    fatal(FluidFatalCode::unsupported_topology, [&] { (void)advance_occupied_staggered(cavitating, .1); });
    check(fingerprint(cavitating) == cavitation_before, "Cavitation failure partially published state");
    input = s.uniform(2); input.left = StaggeredBoundary::rigid_wall; input.velocities_m_per_s[0] = 1;
    fatal(FluidFatalCode::unsupported_configuration, [&] { initialize_occupied_staggered_into(input, state); });
    input = s.uniform(1); input.positions_m[1] = input.positions_m[0];
    fatal(FluidFatalCode::numerical_precision, [&] { initialize_occupied_staggered_into(input, state); });
    input = s.uniform(2); input.positions_m = {-1e100, 0, 1e-100};
    fatal(FluidFatalCode::numerical_precision, [&] { initialize_occupied_staggered_into(input, state); });
    input = s.uniform(1, true, 1); input.frame_origin_m = 1e100;
    input.positions_m = {1e50, std::nextafter(1e50, 2e50)};
    input.cells[0].mixture = s.liquid_cell(input.positions_m[1] - input.positions_m[0]); compatible_momenta(input);
    auto displaced = initialize_occupied_staggered(input); const auto displaced_before = fingerprint(displaced);
    fatal(FluidFatalCode::numerical_precision, [&] { (void)advance_occupied_staggered(displaced, .01); });
    check(fingerprint(displaced) == displaced_before, "Unresolved compensated translation was published");
    input = s.uniform(1); input.cells[0].mixture = MixtureInventory(s.registry.size());
    input.cells[0].mixture.masses[s.gas] = std::numeric_limits<double>::denorm_min();
    fatal(FluidFatalCode::numerical_precision, [&] { initialize_occupied_staggered_into(input, state); });
    check(fingerprint(state) == before, "Failed representability initialization mutated output");
    OccupiedStaggeredState uninitialized;
    fatal(FluidFatalCode::unsupported_configuration, [&] { (void)advance_occupied_staggered(uninitialized, 1); });
    (void)advance_occupied_staggered(state, 0); check(fingerprint(state) == before, "Zero timestep changed state");
    StaggeredInitialization empty; empty.thermodynamics = s.model; auto elapsed = initialize_occupied_staggered(empty);
    (void)advance_occupied_staggered(elapsed, 1e16); const auto elapsed_before = fingerprint(elapsed);
    fatal(FluidFatalCode::numerical_precision, [&] { (void)advance_occupied_staggered(elapsed, 1); });
    check(fingerprint(elapsed) == elapsed_before, "Unresolved time partially published");
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"explicit initialization and retained identities", initialization_and_identities},
        {"mutable model aliases cannot change owned EOS/registry", mutable_model_alias_cannot_change_retained_state},
        {"finite-cell constituent mass overflow rejects atomically", finite_per_cell_liquid_mass_total_overflow_is_atomic},
        {"conserved energy conversion and conflict rejection", conserved_energy_initialization_and_conflicts},
        {"explicit initialization arithmetic ledgers", initialization_arithmetic_ledgers},
        {"exact vacuum and stationary liquid", exact_vacuum_and_stationary_liquid},
        {"uniform translation and compensated coordinates", uniform_translation_and_coordinates},
        {"local geometry scale extremes", local_geometry_scale_extremes},
        {"symmetric expansion retains mechanical variance", isolated_symmetric_expansion_retains_mechanics},
        {"independent single-cell adiabatic solution", independent_single_cell_adiabatic_solution},
        {"retained Fourier dt-to-zero consistency", fourier_dt_to_zero},
        {"alternating pressure force response", alternating_pressure_is_not_a_null_mode},
        {"independent acoustic grid/time/phase", independent_acoustic_grid_and_time_refinement},
        {"occupied-centroid gravity and wall impulse", gravity_freefall_and_wall_reaction},
        {"reflected single-wall material motion", reflected_single_wall_material_motion},
        {"one-sided hydrostatic column", hydrostatic_column},
        {"typed atomic failures and denormal trace", atomic_failures_and_trace_preservation}
    };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << passed << '/' << tests.size() << " retained material-state groups passed; no Eulerian remap/front gate claimed\n";
    return passed == tests.size() ? 0 : 1;
}
