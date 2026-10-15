#include "mixed_atmospheres/occupied_staggered_remap.hpp"
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
void check(bool c, const char* text) { if (!c) throw std::runtime_error(text); }
void near(double a, double b, double r = 1e-11, double absolute = 0) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > r * std::abs(b) + absolute) {
        std::ostringstream s; s << std::setprecision(17) << "Expected " << b << ", got " << a; throw std::runtime_error(s.str());
    }
}
template<class F> void fatal(FluidFatalCode code, F&& action) {
    bool caught = false; try { action(); } catch (const FluidFatalError& e) { caught = e.code() == code; }
    check(caught, "Expected typed remap failure");
}
template<class F> void rejects(F&& action) {
    bool caught = false; try { action(); } catch (const std::exception&) { caught = true; }
    check(caught, "Invalid rezone was accepted");
}
struct Setup {
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> model;
    Setup() {
        for (const auto* id : {"gas", "gas_tag", "liquid"}) {
            SpeciesDefinition s; s.id = id; s.name = id;
            s.phase = s.id == "liquid" ? Phase::liquid : Phase::gas;
            s.atoms = {{"N", 2}}; s.cp = s.phase == Phase::gas ? 1040 : 4184; s.density = 1000;
            s.formation_enthalpy = s.cp * reference_temperature * molecular_mass(s.atoms); registry.add(s);
        }
        std::vector<ReferenceMaterial> materials;
        for (const auto& s : registry.all()) {
            ReferenceMaterial m; m.id = s.id; m.phase = s.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            m.molar_mass_kg_per_mol = s.molar_mass; m.cv_j_per_kg_k = s.cv(); m.energy_offset_j_per_kg = s.internal_energy(0);
            m.minimum_temperature_k = 1e-20; m.maximum_temperature_k = 1e9;
            m.zero_pressure_density_kg_per_m3 = s.phase == Phase::gas ? 0 : 1000;
            m.reference_bulk_modulus_pa = s.phase == Phase::gas ? 0 : 2.2e9; m.maximum_density_ratio = s.phase == Phase::gas ? 1 : 2;
            m.provenance = "Explicit remap surrogate; thermally identical gas/tag, not chemical calibration"; materials.push_back(m);
        }
        model = std::make_shared<const FluidThermodynamics>(registry, "positive-rezone-tests-v1", materials);
    }
    StaggeredInitialization input(std::vector<double> positions, std::vector<double> masses, std::vector<double> velocities,
                                 bool liquid = false, double fraction = .25, double gravity = 0) const {
        StaggeredInitialization in; in.thermodynamics = model; in.positions_m = std::move(positions);
        in.velocities_m_per_s = std::move(velocities); in.gravity_m_per_s2 = gravity;
        for (std::size_t i = 0; i < masses.size(); ++i) {
            std::vector<double> m(registry.size());
            if (liquid) m[2] = masses[i]; else { m[0] = masses[i] * (1 - fraction); m[1] = masses[i] * fraction; }
            auto mix = model->initialize_at_temperature(m, 300, in.positions_m[i + 1] - in.positions_m[i]).mixture;
            in.cells.push_back({mix, mix.mass() * std::midpoint(in.velocities_m_per_s[i], in.velocities_m_per_s[i + 1])});
        }
        return in;
    }
};
StaggeredRezonePlan plan(std::vector<double> positions) {
    StaggeredRezonePlan p; p.node_relative_positions_m = std::move(positions);
    for (std::size_t i = 1; i < p.node_relative_positions_m.size(); ++i) p.target_cell_ids.push_back(static_cast<std::int64_t>(i - 1));
    return p;
}
std::string fingerprint(const OccupiedStaggeredState& state) {
    std::ostringstream out; out << std::hexfloat << state.model().get() << state.model_metadata();
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
        for (double x : c.mixture.masses) out << x << ';';
        out << c.mixture.energy << c.occupied_volume_m3 << c.bulk_momentum_kg_m_per_s << c.mechanical_variance_j;
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
void balance(const OccupiedStaggeredState& a, const StaggeredRemapCandidate& result) {
    const auto& b = result.state; const auto& r = result.report;
    check(a.model().get() == b.model().get() && a.model_metadata() == b.model_metadata(), "Remap replaced owned model");
    check(a.time_seconds() == b.time_seconds() && a.origin_high_m() == b.origin_high_m() && a.origin_low_m() == b.origin_low_m(), "Remap changed time/origin");
    const auto old = a.totals(), now = b.totals();
    for (std::size_t s = 0; s < old.masses_kg.size(); ++s) near(now.masses_kg[s], old.masses_kg[s], 4e-14);
    near(now.momentum_kg_m_per_s - old.momentum_kg_m_per_s, r.wall_impulse_kg_m_per_s, 0, r.momentum_arithmetic_bound_kg_m_per_s);
    near(r.energy_residual_j, 0, 0, r.energy_arithmetic_bound_j);
    check(r.entropy_change_j_per_k >= -r.entropy_arithmetic_bound_j_per_k, "Material entropy decrease");
    near(now.bulk_kinetic_energy_j + now.mechanical_variance_j, now.nodal_kinetic_energy_j, 2e-13);
    near(a.initialization_ledger().declared_added_energy_j, b.initialization_ledger().declared_added_energy_j, 0);
}
void independent_dyadic_mechanical_and_primal_oracle() {
    Setup s; const auto old = initialize_occupied_staggered(s.input({0, 1, 2}, {1, 3}, {-2, 2, 4}));
    const auto result = rezone_occupied_staggered(old, plan({0, .5, 1.5, 2})); balance(old, result);
    const auto& state = result.state; const auto& r = result.report;
    const std::vector<double> masses{.5, 2, 1.5}, momenta{-.5, 1.5, 5, 3}, velocities{-2, 1.2, 20.0 / 7, 4};
    for (std::size_t i = 0; i < masses.size(); ++i) {
        near(state.cells()[i].mixture.mass(), masses[i]); near(state.cells()[i].mixture.masses[0], .75 * masses[i]);
        near(state.cells()[i].mixture.masses[1], .25 * masses[i]);
    }
    for (std::size_t j = 0; j < momenta.size(); ++j) { near(state.nodes()[j].momentum_kg_m_per_s, momenta[j]); near(state.nodes()[j].velocity_m_per_s(), velocities[j]); }
    near(r.cells[1].raw_left.mass_kg, .5); near(r.cells[1].raw_right.mass_kg, 1.5);
    near(r.cells[1].rebalance_transfer_mass_kg, .5); near(r.cells[1].left.mass_kg, 1); near(r.cells[1].right.mass_kg, 1);
    near(r.nodes[1].mixing_heat_j, 8.0 / 5); near(r.nodes[2].mixing_heat_j, 6.0 / 7);
    near(r.cells[0].mixing_heat_j, 8.0 / 25); near(r.cells[1].mixing_heat_j, 32.0 / 25 + 24.0 / 49);
    near(r.cells[2].mixing_heat_j, 18.0 / 49); near(r.mixing_heat_j, 86.0 / 35);
    near(old.totals().nodal_kinetic_energy_j, 17); near(state.totals().nodal_kinetic_energy_j, 509.0 / 35);
    check(r.primal_records.size() == 4 && r.dual_records.size() == 6, "Unexpected positive intersection partition");
    for (const auto& packet : r.dual_records) check(packet.packet.mass_kg > 0 && packet.packet.kinetic_j >= 0, "Nonpositive mechanical overlap");
}
void exact_identity_vacuum_and_uniform_translation() {
    Setup s;
    for (bool liquid : {false, true}) for (double velocity : {-2.0, 2.0}) {
        auto input = s.input({-.25, .25, 1.25, 1.75}, liquid ? std::vector<double>{500, 1000, 500} : std::vector<double>{.5, 1, .5},
            std::vector<double>(4, velocity), liquid); input.frame_origin_m = 1e100;
        const auto state = initialize_occupied_staggered(input); const auto before = fingerprint(state);
        const auto identity = rezone_occupied_staggered(state, plan({0, .5, 1.5, 2}));
        check(identity.report.identity && fingerprint(identity.state) == before, "Identity rezone changed canonical state/ledger/cache");
        const auto result = rezone_occupied_staggered(state, plan({0, .25, .75, 1.75, 2})); balance(state, result);
        for (const auto& node : result.state.nodes()) near(node.velocity_m_per_s(), velocity, 8e-15);
        near(result.report.mixing_heat_j, 0, 0, 1e-10);
        for (const auto& cell : result.state.cells()) near(cell.thermodynamics.temperature_k, 300);
    }
    StaggeredInitialization empty; empty.thermodynamics = s.model; const auto vacuum = initialize_occupied_staggered(empty);
    const auto result = rezone_occupied_staggered(vacuum, {});
    check(result.report.identity && fingerprint(result.state) == fingerprint(vacuum), "Vacuum acquired material");
}
void planner_and_free_boundary_cut_transitions() {
    Setup s;
    for (double offset : {-.25, 0.0, .25}) {
        auto input = s.input({0, 1, 2}, {1, 1}, {3, 3, 3}); input.frame_origin_m = -1e100;
        const auto state = initialize_occupied_staggered(input);
        const StaggeredCartesianGrid grid{offset, 0, .5, -2, 6};
        const auto p = plan_staggered_cartesian_rezone(state, grid); const auto result = rezone_occupied_staggered(state, p);
        if (!result.report.identity) balance(state, result);
        check(result.state.nodes().front().relative_position_m == 0 && result.state.nodes().back().relative_position_m == 2, "Planner moved free surface");
        for (const auto& node : result.state.nodes()) near(node.velocity_m_per_s(), 3);
        check(p.target_cell_ids.front() >= -2 && p.target_cell_ids.back() < 6, "Planner mapping out of bounds");
    }
    const auto state = initialize_occupied_staggered(s.input({0, 1}, {1}, {0, 0}));
    for (double cut : {std::ldexp(1.0, -30), std::ldexp(1.0, -100)}) {
        const auto result = rezone_occupied_staggered(state, plan({0, cut, .5, 1})); balance(state, result);
        check(result.state.cells()[0].mixture.mass() > 0, "Tiny positive cut became dry");
    }
    fatal(FluidFatalCode::unsupported_topology, [&] { (void)plan_staggered_cartesian_rezone(state, {0, 0, .25, 0, 2}); });
}
void independent_wall_impulse_and_heat() {
    Setup s; auto input = s.input({0, 1, 2}, {1, 1}, {0, 2, 0}); input.left = input.right = StaggeredBoundary::rigid_wall;
    const auto state = initialize_occupied_staggered(input); const auto result = rezone_occupied_staggered(state, plan({0, 2})); balance(state, result);
    near(result.report.wall_impulse_kg_m_per_s, -2); near(result.report.wall_heat_j, 1); near(result.report.mixing_heat_j, 1);
    for (const auto& node : result.state.nodes()) check(node.fixed && node.momentum_kg_m_per_s == 0, "Wall retained nonzero J");
    near(result.state.cells()[0].mixture.energy - 2 * state.cells()[0].mixture.energy, 2, 0, 1e-9);
    const auto identity = rezone_occupied_staggered(state, plan({0, 1, 2}));
    check(identity.report.identity && identity.report.wall_impulse_kg_m_per_s == 0 && identity.report.wall_heat_j == 0, "Identity heated walls");
}
void independent_gravitational_first_moment_and_entropy_rejection() {
    Setup s;
    const auto old = initialize_occupied_staggered(s.input({0, 1, 2}, {1, 3}, {0, 0, 0}, false, .25, 9.81));
    const auto result = rezone_occupied_staggered(old, plan({0, .5, 1.5, 2})); balance(old, result);
    near(result.report.gravity_relocation_j, .25 * 9.81);
    near(result.report.cells[1].gravity_relocation_j, .25 * 9.81);
    near(result.state.totals().internal_energy_j - old.totals().internal_energy_j, .25 * 9.81, 0, 1e-8);
    auto rejecting = initialize_occupied_staggered(s.input({0, 1, 2}, {3, 1}, {0, 0, 0}, false, .25, 3e5)); const auto before = fingerprint(rejecting);
    fatal(FluidFatalCode::unsupported_configuration, [&] { (void)rezone_occupied_staggered_into(rejecting, plan({0, .5, 1.5, 2})); });
    check(fingerprint(rejecting) == before, "Entropy rejection published a candidate");
    auto insufficient = initialize_occupied_staggered(s.input({0, 1, 2}, {3, 1}, {0, 0, 0}, false, .25, 3e6)); const auto old_insufficient = fingerprint(insufficient);
    rejects([&] { (void)rezone_occupied_staggered_into(insufficient, plan({0, .5, 1.5, 2})); });
    check(fingerprint(insufficient) == old_insufficient, "EOS domain failure published a candidate");
}
void positive_trace_shares_and_atomic_failures() {
    Setup s; auto input = s.input({0, 4}, {4}, {0, 0}, false, 0);
    input.cells[0].mixture.masses[1] = 4 * std::numeric_limits<double>::denorm_min();
    input.cells[0].mixture = s.model->initialize_at_temperature(input.cells[0].mixture.masses, 300, 4).mixture;
    auto state = initialize_occupied_staggered(input);
    const auto accepted = rezone_occupied_staggered(state, plan({0, 1, 2, 3, 4})); balance(state, accepted);
    for (const auto& cell : accepted.state.cells()) check(cell.mixture.masses[1] == std::numeric_limits<double>::denorm_min(), "Exact positive denormal trace was dropped");
    input.cells[0].mixture.masses[1] = 2 * std::numeric_limits<double>::denorm_min();
    input.cells[0].mixture = s.model->initialize_at_temperature(input.cells[0].mixture.masses, 300, 4).mixture;
    state = initialize_occupied_staggered(input); const auto before = fingerprint(state);
    fatal(FluidFatalCode::numerical_precision, [&] { (void)rezone_occupied_staggered_into(state, plan({0, 1, 2, 4})); });
    check(fingerprint(state) == before, "Unrepresentable trace split changed state");
    state = accepted.state; const auto after_success = fingerprint(state);
    fatal(FluidFatalCode::unsupported_topology, [&] { (void)rezone_occupied_staggered_into(state, plan({0, 1, 1, 4})); });
    check(fingerprint(state) == after_success, "Failure after successful rezone changed state/cache/ledger");
}
struct Manufactured {
    double concentration_l1 = 0, velocity_l1 = 0, left_trace = 0, right_trace = 0;
    double maximum_boundary_rate = 0, total_boundary_variation_per_time = 0;
    double mass_residual = 0, energy_residual = 0, heat = 0;
};
Manufactured manufactured(std::size_t n, double courant) {
    Setup s; const double dx = 1 / static_cast<double>(n), duration = .125;
    const auto concentration_primitive = [](double q) { return .25 * q - .1 * std::cos(2 * std::numbers::pi * q) / (2 * std::numbers::pi); };
    std::vector<double> positions, masses(n, dx), velocities;
    for (std::size_t j = 0; j <= n; ++j) { positions.push_back(static_cast<double>(j) * dx); velocities.push_back(1 + .1 * positions.back()); }
    auto input = s.input(positions, masses, velocities);
    for (std::size_t i = 0; i < n; ++i) {
        auto& cell = input.cells[i]; const double tag = concentration_primitive(positions[i + 1]) - concentration_primitive(positions[i]);
        cell.mixture.masses[1] = tag; cell.mixture.masses[0] = dx - tag;
        cell.mixture = s.model->initialize_at_temperature(cell.mixture.masses, 300, dx).mixture;
    }
    auto state = initialize_occupied_staggered(input); const auto before = state.totals();
    const auto steps = static_cast<std::size_t>(std::ceil(duration / (courant * dx)));
    const double dt = duration / static_cast<double>(steps); Manufactured result;
    for (std::size_t step = 1; step <= steps; ++step) {
        const double clock = static_cast<double>(step) * dt;
        // Operator-only manufactured carrier a=1: in a co-moving material
        // frame the slab and exact profiles are stationary and the Cartesian
        // grid moves left. No pressure/body solve or production time wrapper.
        const auto p = plan_staggered_cartesian_rezone(state, {-clock, 0, dx, -static_cast<std::int64_t>(n), 2 * static_cast<std::int64_t>(n)});
        const double old_left = state.nodes().front().velocity_m_per_s(), old_right = state.nodes().back().velocity_m_per_s();
        const auto report = rezone_occupied_staggered_into(state, p); result.heat += report.mixing_heat_j;
        const double change = std::max(std::abs(state.nodes().front().velocity_m_per_s() - old_left),
            std::abs(state.nodes().back().velocity_m_per_s() - old_right));
        result.maximum_boundary_rate = std::max(result.maximum_boundary_rate, change / dt);
        result.total_boundary_variation_per_time += (std::abs(state.nodes().front().velocity_m_per_s() - old_left) +
            std::abs(state.nodes().back().velocity_m_per_s() - old_right)) / duration;
    }
    for (std::size_t i = 0; i < state.cells().size(); ++i) {
        const double a = state.nodes()[i].relative_position_m, b = state.nodes()[i + 1].relative_position_m;
        result.concentration_l1 += std::abs(state.cells()[i].mixture.masses[1] - (concentration_primitive(b) - concentration_primitive(a)));
    }
    for (const auto& node : state.nodes()) result.velocity_l1 += node.inertia_kg * std::abs(node.velocity_m_per_s() - (1 + .1 * node.relative_position_m));
    result.left_trace = state.nodes().front().velocity_m_per_s() - 1;
    result.right_trace = state.nodes().back().velocity_m_per_s() - 1.1;
    const auto after = state.totals(); result.mass_residual = after.masses_kg[1] - before.masses_kg[1];
    result.energy_residual = (after.internal_energy_j - before.internal_energy_j) + (after.nodal_kinetic_energy_j - before.nodal_kinetic_energy_j);
    near(after.masses_kg[0], before.masses_kg[0], 2e-13); near(after.masses_kg[1], before.masses_kg[1], 2e-13);
    near(result.energy_residual, 0, 0, 2e-8);
    std::cout << std::setprecision(17) << "Manufactured N=" << n << " C=" << courant << " steps=" << steps <<
        " concentration_L1=" << result.concentration_l1 << " velocity_L1=" << result.velocity_l1 <<
        " left_trace=" << result.left_trace << " right_trace=" << result.right_trace <<
        " max_boundary_du_dt=" << result.maximum_boundary_rate << " boundary_variation_per_time=" << result.total_boundary_variation_per_time <<
        " tag_mass_residual=" << result.mass_residual << " energy_residual=" << result.energy_residual << " mixing_heat=" << result.heat << '\n';
    return result;
}
void manufactured_advection_and_free_trace_refinement() {
    const auto a = manufactured(16, .25), b = manufactured(32, .25), c = manufactured(64, .25);
    const auto t0 = manufactured(32, .5), t1 = manufactured(32, .25), t2 = manufactured(32, .125), t3 = manufactured(32, .0625);
    check(b.concentration_l1 < .8 * a.concentration_l1 && c.concentration_l1 < .8 * b.concentration_l1,
        "Manufactured concentration lacks grid refinement");
    check(b.velocity_l1 < .8 * a.velocity_l1 && c.velocity_l1 < .8 * b.velocity_l1,
        "Manufactured momentum field lacks grid refinement");
    const auto trace = [](Manufactured x) { return std::max(std::abs(x.left_trace), std::abs(x.right_trace)); };
    check(trace(b) < .8 * trace(a) && trace(c) < .8 * trace(b), "Nonuniform free-boundary trace lacks grid refinement");
    check(t2.maximum_boundary_rate <= 1.5 * t1.maximum_boundary_rate && t3.maximum_boundary_rate <= 1.5 * t2.maximum_boundary_rate &&
        t3.total_boundary_variation_per_time <= 1.5 * t2.total_boundary_variation_per_time,
        "Nonuniform free boundary has an unbounded per-call smoothing defect as dt tends to zero");
    check(std::abs(trace(t2) - trace(t3)) < std::abs(trace(t0) - trace(t1)), "Fixed-grid trace timestep changes do not settle");
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"independent dyadic primal/corner/node oracle", independent_dyadic_mechanical_and_primal_oracle},
        {"bitwise identity, vacuum and uniform translation", exact_identity_vacuum_and_uniform_translation},
        {"Cartesian planner and positive free cuts", planner_and_free_boundary_cut_transitions},
        {"independent fixed-wall impulse and K-to-U", independent_wall_impulse_and_heat},
        {"independent gravitational first moment and entropy/domain", independent_gravitational_first_moment_and_entropy_rejection},
        {"positive denormal shares and atomic failures", positive_trace_shares_and_atomic_failures},
        {"manufactured carrier and free-boundary refinement", manufactured_advection_and_free_trace_refinement}
    };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << passed << '/' << tests.size() << " remap-only groups passed; no composed timestep/front gate claimed\n";
    return passed == tests.size() ? 0 : 1;
}
