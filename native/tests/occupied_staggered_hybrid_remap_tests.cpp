#include "mixed_atmospheres/occupied_staggered_hybrid.hpp"
#include <algorithm>
#include <bit>
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
    explicit Setup(double offset = 0, bool opposing = false) {
        for (const auto* id : {"gas", "gas_tag", "liquid"}) {
            SpeciesDefinition s; s.id = id; s.name = id;
            s.phase = s.id == "liquid" ? Phase::liquid : Phase::gas;
            s.atoms = {{"N", 2}}; s.cp = s.phase == Phase::gas ? 1040 : 4184; s.density = 1000;
            const double reference_offset = opposing && s.id == "gas_tag" ? -offset : offset;
            s.formation_enthalpy = (s.cp * reference_temperature + reference_offset) * molecular_mass(s.atoms); registry.add(s);
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
HybridRezonePlan material_plan(const HybridStaggeredState& source, std::vector<double> nodes) {
    return plan_hybrid_material_rezone(source,nodes,{0,0,.25,-64,64});
}
std::string fingerprint(const HybridStaggeredState& state) {
    std::ostringstream out; out << fingerprint(state.canonical()) << ':' << state.identity().lineage << ':' << state.identity().revision << ':' << state.identity().snapshot;
    out << state.has_coverage_grid() << ';' << std::hexfloat;
    const auto& g = state.coverage_grid(); out << g.origin_relative_high_m << ';' << g.origin_relative_low_m << ';' << g.spacing_m << ';' << g.first_cell << ';' << g.end_cell;
    for (const auto& c : state.source_cell_identities()) out << static_cast<int>(c.kind) << ':' << c.material.lineage << ':' << c.material.ordinal << ':' << c.cartesian_index;
    for (const auto& c : state.material_caps()) { out << c.initial_source_cell; for (double m : c.original_species_masses_kg) out << ';' << m; }
    for (const auto& c : state.authoritative_coverage()) out << static_cast<int>(c.partition) << ':' << c.cell_slot << ':' << c.cartesian_index << ':' << c.begin_relative_m << ':' << c.end_relative_m << ':' << c.width_m << ':' << c.volume_m3;
    for (const auto& c : state.authoritative_coverage_balances()) out << static_cast<int>(c.partition) << ':' << c.cell_slot << ':' << c.records << ':' << c.expected_width_m << ':' << c.covered_width_m << ':' << c.width_residual_m << ':' << c.width_arithmetic_bound_m << ':' << c.expected_volume_m3 << ':' << c.covered_volume_m3 << ':' << c.volume_residual_m3 << ':' << c.volume_arithmetic_bound_m3;
    return out.str();
}
void balance(const HybridStaggeredState& source, const HybridRezoneCandidate& result) {
    const auto& a = source.canonical(); const auto& b = result.state.canonical(); const auto& r = result.report.mechanics;
    check(a.model().get() == b.model().get() && a.model_metadata() == b.model_metadata(),"Hybrid rezone replaced immutable EOS");
    check(a.time_seconds() == b.time_seconds() && a.origin_high_m() == b.origin_high_m() && a.origin_low_m() == b.origin_low_m(),"Instantaneous rezone changed time/origin");
    check(result.state.identity().lineage == source.identity().lineage && result.state.identity().revision == source.identity().revision + 1 &&
        result.state.identity().snapshot != 0 && result.state.identity().snapshot != source.identity().snapshot,"Accepted rezone did not advance its authenticated immutable snapshot");
    const auto old = a.totals(), now = b.totals();
    for (std::size_t s = 0; s < old.masses_kg.size(); ++s) near(now.masses_kg[s],old.masses_kg[s],4e-14);
    near(now.momentum_kg_m_per_s-old.momentum_kg_m_per_s,0,0,r.momentum_arithmetic_bound_kg_m_per_s);
    near(r.energy_residual_j,0,0,r.energy_arithmetic_bound_j);
    const auto actual_energy = [](const OccupiedStaggeredState& state) {
        long double energy = 0;
        for (std::size_t i = 0; i < state.cells().size(); ++i) {
            const auto& c = state.cells()[i];
            energy += c.mixture.energy;
            energy += static_cast<long double>(c.mixture.mass())*state.gravity_m_per_s2()*std::midpoint(state.nodes()[i].relative_position_m,state.nodes()[i+1].relative_position_m);
        }
        for (const auto& n : state.nodes()) energy += .5L*n.momentum_kg_m_per_s*(static_cast<long double>(n.momentum_kg_m_per_s)/n.inertia_kg);
        return energy;
    };
    check(std::abs(actual_energy(b)-actual_energy(a)) <= r.energy_arithmetic_bound_j,"Independently summed actual U+J^2/(2I)+PE violates conservation");
    check(r.entropy_change_j_per_k >= -r.entropy_arithmetic_bound_j_per_k,"Hybrid rezone decreased actual EOS entropy");
    check(r.wall_heat_j == 0 && r.wall_impulse_kg_m_per_s == 0,"Protected walls received a rezone reset/impulse");
    check(r.target_cell_ids.empty(),"Unstructured mechanical slots were mislabeled Cartesian IDs");
    near(now.bulk_kinetic_energy_j+now.mechanical_variance_j,now.nodal_kinetic_energy_j,2e-13);
    for (const auto& c : result.report.caps) {
        check(c.outer_before.mass_kg == c.outer_after.mass_kg && c.outer_before.momentum_kg_m_per_s == c.outer_after.momentum_kg_m_per_s &&
            c.outer_before.kinetic_j == c.outer_after.kinetic_j,"Outer identity packet changed");
    }
    for (const auto& row : r.primal_records) for (std::size_t s = 0; s < row.masses_kg.size(); ++s)
        check(a.cells()[row.donor].mixture.masses[s] == 0 || row.masses_kg[s] > 0,"Positive donor constituent lost a positive overlap");
    for (const auto& row : r.dual_records) check(row.packet.mass_kg > 0 && row.packet.kinetic_j >= 0,"Nonpositive mechanical packet");
}
void exact_independent_protected_dyadic_oracle() {
    Setup setup; const auto source = bind_material_endcaps(initialize_occupied_staggered(setup.input({0,1,2,3,4},{1,3,1,1},{-2,2,4,-1,3})));
    const auto before = fingerprint(source.canonical()); const auto p = material_plan(source,{0,1,2.5,3,4});
    check(!p.diagnostics().preferred_cartesian_partition,"Explicit material plan claimed preferred-grid transition rules");
    check(p.planned_cells()[1].identity.kind == HybridCellKind::unstructured_interior,"Explicit target has a false Cartesian role");
    const auto result = rezone_hybrid_staggered(source,p); balance(source,result);
    const auto& r = result.report.mechanics; const auto& state = result.state.canonical();
    const double expected_masses[]{1,3.5,.5,1};
    for (std::size_t i = 0; i < 4; ++i) {
        near(state.cells()[i].mixture.mass(),expected_masses[i],0);
        near(state.cells()[i].mixture.masses[0],.75*expected_masses[i],0);
        near(state.cells()[i].mixture.masses[1],.25*expected_masses[i],0);
    }
    near(state.nodes()[0].inertia_kg,.5,0); near(state.nodes()[0].momentum_kg_m_per_s,-1,0);
    near(state.nodes()[4].inertia_kg,.5,0); near(state.nodes()[4].momentum_kg_m_per_s,1.5,0);
    near(state.nodes()[1].inertia_kg,9.0/4,0); near(state.nodes()[1].momentum_kg_m_per_s,17.0/3);
    near(state.nodes()[1].velocity_m_per_s(),68.0/27); near(r.nodes[1].mixing_heat_j,70.0/81);
    near(r.cells[0].mixing_heat_j,140.0/729); near(r.nodes[2].mixing_heat_j,1655.0/576);
    near(r.mixing_heat_j,19375.0/5184); near(source.canonical().totals().nodal_kinetic_energy_j,95.0/4,0);
    near(state.totals().nodal_kinetic_energy_j,103745.0/5184); near(state.totals().momentum_kg_m_per_s,23.0/2);
    check(state.cells()[0].mixture.energy > source.canonical().cells()[0].mixture.energy,"Shared inner cap node failed to heat its actual cap");
    check(r.cells[0].lambda == 1 && r.cells[3].lambda == 1 && r.cells[0].rebalance_transfer_mass_kg == 0 && r.cells[3].rebalance_transfer_mass_kg == 0,"Protected cap was normalized/rebalanced");
    check(r.primal_records.size() == 5 && r.dual_records.size() == 10,"Independent positive overlap counts differ");
    check(result.report.retired_interior_nodes_m == std::vector<double>{2} && result.report.inserted_interior_nodes_m == std::vector<double>{2.5},"Actual interior mesh events differ");
    check(fingerprint(source.canonical()) == before,"Candidate construction mutated its immutable source");
}
void reflected_reversed_and_translated_oracles() {
    Setup setup;
    for (bool reflected : {false,true}) for (double reversal : {-1.0,1.0}) {
        std::vector<double> masses{1,3,1,1}, velocities{-2,2,4,-1,3}, target{0,1,2.5,3,4};
        if (reflected) { std::reverse(masses.begin(),masses.end()); std::reverse(velocities.begin(),velocities.end()); for (auto& u : velocities) u = -u; target = {0,1,1.5,3,4}; }
        for (auto& u : velocities) u *= reversal;
        auto input = setup.input({0,1,2,3,4},masses,velocities); input.frame_origin_m = reflected ? -1e100 : 1e100;
        const auto source = bind_material_endcaps(initialize_occupied_staggered(input));
        const auto result = rezone_hybrid_staggered(source,material_plan(source,target)); balance(source,result);
        const std::size_t shared = reflected ? 3 : 1, cap = reflected ? 3 : 0;
        near(result.state.canonical().nodes()[shared].inertia_kg,9.0/4,0);
        near(result.state.canonical().nodes()[shared].momentum_kg_m_per_s,(reflected ? -1 : 1)*reversal*17.0/3);
        near(result.report.mechanics.nodes[shared].mixing_heat_j,70.0/81);
        near(result.report.mechanics.cells[cap].mixing_heat_j,140.0/729);
        near(result.report.mechanics.mixing_heat_j,19375.0/5184);
        near(result.state.canonical().totals().momentum_kg_m_per_s,(reflected ? -1 : 1)*reversal*23.0/2);
        for (const auto& record : result.report.caps) {
            const auto before = source.canonical().nodes()[record.right_outer ? 4 : 0];
            const auto after = result.state.canonical().nodes()[record.right_outer ? 4 : 0];
            check(std::bit_cast<std::uint64_t>(before.inertia_kg) == std::bit_cast<std::uint64_t>(after.inertia_kg) &&
                std::bit_cast<std::uint64_t>(before.momentum_kg_m_per_s) == std::bit_cast<std::uint64_t>(after.momentum_kg_m_per_s),"Physical endpoint I/J lost bit identity under symmetry/translation");
        }
    }
}
void real_revision_staleness_and_authoritative_coverage() {
    Setup setup; auto state = bind_material_endcaps(initialize_occupied_staggered(setup.input({0,1,2,3,4},{1,3,1,1},{-2,2,4,-1,3})));
    const auto old_snapshot = state; const auto p = material_plan(state,{0,1,1.5,2.5,3,4});
    const auto report = rezone_hybrid_staggered_into(state,p);
    check(state.identity().revision == 1 && state.has_coverage_grid() && report.before == old_snapshot.identity() && report.after == state.identity(),"Real accepted rezone did not publish its revision/coverage");
    const auto after = fingerprint(state);
    fatal(FluidFatalCode::unsupported_configuration,[&] { (void)rezone_hybrid_staggered_into(state,p); });
    check(fingerprint(state) == after,"Same-lineage stale-plan rejection changed actual accepted state/cache/coverage/ledger");
    validate_hybrid_rezone_plan(old_snapshot,p);
    const auto next = material_plan(state,{0,1,2.5,3,4});
    check(next.protected_caps().back().source_cell == 4 && next.protected_caps().back().planned_target_cell == 3,"Right cap mapping reused its original slot after a real cell-count change");
    const auto result = rezone_hybrid_staggered(state,next); balance(state,result);
    check(result.state.material_caps().back().initial_source_cell == 3,"Stable original cap identity was rekeyed to a transient slot");
    for (const auto& c : result.state.authoritative_coverage()) {
        check(c.partition == HybridCoveragePartition::authoritative_source && c.cell_slot < result.state.canonical().cells().size(),"Accepted target coverage remained a proposed inventory map");
        check(c.begin_relative_m >= result.state.canonical().nodes()[c.cell_slot].relative_position_m && c.end_relative_m <= result.state.canonical().nodes()[c.cell_slot+1].relative_position_m,"Coverage escapes its actual accepted material interval");
    }
    auto identity_state = result.state; std::vector<double> nodes;
    for (const auto& n : identity_state.canonical().nodes()) nodes.push_back(n.relative_position_m);
    const auto exact_before = fingerprint(identity_state.canonical());
    const auto identity = rezone_hybrid_staggered_into(identity_state,material_plan(identity_state,nodes));
    check(identity.mechanics.identity && fingerprint(identity_state.canonical()) == exact_before && identity_state.identity().revision == 3,"Accepted identity transaction changed mechanics or failed revision authentication");
}
void vacuum_whole_slab_and_adjacent_identity() {
    Setup setup; StaggeredInitialization empty; empty.thermodynamics = setup.model;
    const auto vacuum = bind_material_endcaps(initialize_occupied_staggered(empty),{0,0,0});
    const auto v = rezone_hybrid_staggered(vacuum,material_plan(vacuum,{})); balance(vacuum,v);
    check(v.report.mechanics.identity && v.state.canonical().cells().empty() && v.state.authoritative_coverage().empty(),"Vacuum acquired material or a packet");
    for (const auto& input : std::vector<StaggeredInitialization>{setup.input({0,1},{1},{-2,3}),setup.input({0,1,2},{1,3},{-2,2,4})}) {
        const auto source = bind_material_endcaps(initialize_occupied_staggered(input)); std::vector<double> nodes;
        for (const auto& n : source.canonical().nodes()) nodes.push_back(n.relative_position_m);
        const auto result = rezone_hybrid_staggered(source,material_plan(source,nodes)); balance(source,result);
        check(result.report.mechanics.identity && fingerprint(result.state.canonical()) == fingerprint(source.canonical()) && result.report.caps.size() == 2,"Whole/adjacent cap mechanics or outer identity evidence changed");
        fatal(FluidFatalCode::unsupported_topology,[&] { (void)material_plan(source,{0,.5,1}); });
    }
}
void alternate_same_revision_snapshots_cannot_reuse_plans() {
    Setup setup;
    const auto source = bind_material_endcaps(initialize_occupied_staggered(setup.input({0,1,2,3,4},{1,3,1,1},std::vector<double>(5,0))));
    const auto source_copy = source;
    const auto a = rezone_hybrid_staggered(source,material_plan(source,{0,1,2.5,3,4})); balance(source,a);
    const auto b = rezone_hybrid_staggered(source_copy,material_plan(source_copy,{0,1,1.5,3,4})); balance(source_copy,b);
    check(a.state.identity().lineage == b.state.identity().lineage && a.state.identity().revision == b.state.identity().revision &&
        a.state.identity().snapshot != b.state.identity().snapshot,"Different accepted branches reused a logical immutable snapshot");
    const auto a_plan = material_plan(a.state,{0,1,2,3,4}); const auto exact_a_copy = a.state;
    check(a_plan.source_identity() == exact_a_copy.identity(),"Exact copied snapshot changed authentication");
    validate_hybrid_rezone_plan(exact_a_copy,a_plan);
    const auto from_copy = rezone_hybrid_staggered(exact_a_copy,a_plan); balance(exact_a_copy,from_copy);
    auto output = b.state; const auto before = fingerprint(output);
    fatal(FluidFatalCode::unsupported_configuration,[&] { validate_hybrid_rezone_plan(output,a_plan); });
    fatal(FluidFatalCode::unsupported_configuration,[&] { (void)rezone_hybrid_staggered_into(output,a_plan); });
    check(fingerprint(output) == before,"Same-lineage/revision foreign snapshot changed canonical state or authentication");
    check(source_copy.identity() == source.identity(),"Candidate branches changed immutable source copies");
}
void uniform_gas_liquid_mixture_and_fixed_walls() {
    Setup setup;
    for (int phase : {0,1,2}) {
        auto input = setup.input({0,1,2,3,4,5,6},std::vector<double>(6,phase == 1 ? 1000 : 1),std::vector<double>(7,2),phase == 1);
        if (phase == 2) for (auto& c : input.cells) {
            c.mixture.masses = {.75,.25,500}; c.mixture = setup.model->initialize_at_temperature(c.mixture.masses,300,1).mixture;
            c.bulk_momentum_kg_m_per_s = c.mixture.mass()*2;
        }
        const auto source = bind_material_endcaps(initialize_occupied_staggered(input));
        const auto result = rezone_hybrid_staggered(source,material_plan(source,{0,1,2.25,3.5,5,6})); balance(source,result);
        for (const auto& n : result.state.canonical().nodes()) near(n.velocity_m_per_s(),2,8e-15);
        for (const auto& c : result.state.canonical().cells()) near(c.thermodynamics.temperature_k,300,3e-12);
        near(result.report.mechanics.mixing_heat_j,0,0,128*std::numeric_limits<double>::epsilon()*source.canonical().totals().nodal_kinetic_energy_j);
    }
    for (bool left : {false,true}) for (bool right : {false,true}) if (left || right) {
        auto input = setup.input({0,1,2,3,4},{1,3,1,1},{-2,2,4,-1,3});
        input.left = left ? StaggeredBoundary::rigid_wall : StaggeredBoundary::traction_free;
        input.right = right ? StaggeredBoundary::rigid_wall : StaggeredBoundary::traction_free;
        if (left) input.velocities_m_per_s.front() = 0;
        if (right) input.velocities_m_per_s.back() = 0;
        for (std::size_t i = 0; i < input.cells.size(); ++i) input.cells[i].bulk_momentum_kg_m_per_s = input.cells[i].mixture.mass()*std::midpoint(input.velocities_m_per_s[i],input.velocities_m_per_s[i+1]);
        const auto source = bind_material_endcaps(initialize_occupied_staggered(input));
        std::vector<double> target{0,1,2.5,3,4}; if (right && !left) for (auto& q : target) q -= 4;
        const auto result = rezone_hybrid_staggered(source,material_plan(source,target)); balance(source,result);
        check(result.state.canonical().nodes().front().fixed == left && result.state.canonical().nodes().back().fixed == right,"Rezone changed rigid boundary identity");
    }
}
void independent_gravity_and_typed_entropy_domain_rollback() {
    Setup setup;
    for (double gravity : {9.81,3e5,3e6}) {
        auto input = setup.input({0,1,2,3,4},{1,3,1,1},std::vector<double>(5,0),false,.25,gravity);
        auto state = bind_material_endcaps(initialize_occupied_staggered(input)); const auto before = fingerprint(state);
        const auto p = material_plan(state,{0,1,2.5,3,4});
        if (gravity > 100) {
            fatal(FluidFatalCode::unsupported_configuration,[&] { (void)rezone_hybrid_staggered_into(state,p); });
            check(fingerprint(state) == before,"Entropy/EOS rejection changed full immutable source/coverage/revision");
        } else {
            const auto result = rezone_hybrid_staggered(state,p); balance(state,result);
            near(result.report.mechanics.gravity_relocation_j,-.5*gravity);
            near(result.report.mechanics.cells[1].gravity_relocation_j,-.5*gravity);
            check(result.report.mechanics.cells.front().gravity_relocation_j == 0 && result.report.mechanics.cells.back().gravity_relocation_j == 0,"Identity caps acquired relocation energy");
            near(result.state.canonical().totals().internal_energy_j-state.canonical().totals().internal_energy_j,-.5*gravity,0,1e-8);
        }
    }
}
void positive_denormal_shares_and_strict_packet_representability() {
    Setup setup; const double d = std::numeric_limits<double>::denorm_min();
    for (double trace : {4*d,2*d}) {
        auto input = setup.input({0,1,5,6},{1,4,1},std::vector<double>(4,0),false,0);
        input.cells[1].mixture.masses[1] = trace; input.cells[1].mixture = setup.model->initialize_at_temperature(input.cells[1].mixture.masses,300,4).mixture;
        auto state = bind_material_endcaps(initialize_occupied_staggered(input)); const auto before = fingerprint(state);
        const auto p = material_plan(state,{0,1,2,3,4,5,6});
        if (trace == 4*d) {
            const auto result = rezone_hybrid_staggered(state,p); balance(state,result);
            for (std::size_t i = 1; i < 5; ++i) check(result.state.canonical().cells()[i].mixture.masses[1] == d,"Exactly representable positive trace was dropped");
        } else {
            fatal(FluidFatalCode::numerical_precision,[&] { (void)rezone_hybrid_staggered_into(state,p); });
            check(fingerprint(state) == before,"Unrepresentable trace changed source");
        }
    }
    auto input = setup.input({0,1,4,5},{1,3,1},std::vector<double>(4,0),false,0);
    input.cells[1].mixture.masses[1] = 2*d; input.cells[1].mixture = setup.model->initialize_at_temperature(input.cells[1].mixture.masses,300,3).mixture;
    auto final_zero = bind_material_endcaps(initialize_occupied_staggered(input)); const auto before = fingerprint(final_zero);
    fatal(FluidFatalCode::numerical_precision,[&] { (void)rezone_hybrid_staggered_into(final_zero,material_plan(final_zero,{0,1,2,3,4,5})); });
    check(fingerprint(final_zero) == before,"Final positive trace remainder changed source");
    const double odd_subnormal = std::nextafter(std::numeric_limits<double>::min(),0.0);
    auto half_input = setup.input({0,1,2,3},{1,odd_subnormal,1},std::vector<double>(4,0),false,0);
    auto half = bind_material_endcaps(initialize_occupied_staggered(half_input)); const auto half_before = fingerprint(half);
    fatal(FluidFatalCode::numerical_precision,[&] { (void)rezone_hybrid_staggered_into(half,material_plan(half,{0,1,1.5,2,3})); });
    check(fingerprint(half) == half_before,"Unrepresentable exact half mass changed source");
    const double tiny = std::ldexp(1.0,-100);
    auto kinetic_input = setup.input({0,tiny,1,2},{1,1,1},{0,1e-150,0,0},false,0);
    auto kinetic = bind_material_endcaps(initialize_occupied_staggered(kinetic_input)); const auto kinetic_before = fingerprint(kinetic);
    fatal(FluidFatalCode::numerical_precision,[&] { (void)rezone_hybrid_staggered_into(kinetic,material_plan(kinetic,{0,tiny,2*tiny,1,2})); });
    check(fingerprint(kinetic) == kinetic_before,"Positive kinetic share underflow changed source");
}
void explicit_geometry_capacities_and_foreign_atomicity() {
    Setup setup; const auto canonical = initialize_occupied_staggered(setup.input({0,1,2,3,4},{1,3,1,1},{-2,2,4,-1,3}));
    const auto source = bind_material_endcaps(canonical,{4,15,32});
    const auto p = plan_hybrid_material_rezone(source,std::vector<double>{0,1,2.5,3,4},{0,0,.25,0,16});
    const auto result = rezone_hybrid_staggered(source,p); balance(source,result);
    for (const auto limits : {HybridGeometryLimits{3,15,32},HybridGeometryLimits{4,14,32},HybridGeometryLimits{4,15,31}}) {
        if (limits.maximum_material_cells == 3) { fatal(FluidFatalCode::unsupported_configuration,[&] { (void)bind_material_endcaps(canonical,limits); }); continue; }
        const auto state = bind_material_endcaps(canonical,limits); const auto before = fingerprint(state);
        fatal(FluidFatalCode::unsupported_configuration,[&] { (void)plan_hybrid_material_rezone(state,std::vector<double>{0,1,2.5,3,4},{0,0,.25,0,16}); });
        check(fingerprint(state) == before,"Explicit plan capacity failure changed source");
    }
    auto state = result.state; const auto before = fingerprint(state);
    const auto foreign = bind_material_endcaps(canonical);
    fatal(FluidFatalCode::unsupported_configuration,[&] { (void)rezone_hybrid_staggered_into(state,material_plan(foreign,{0,1,2.5,3,4})); });
    fatal(FluidFatalCode::unsupported_configuration,[&] { (void)material_plan(state,{0,1,1.5,2.5,3,4}); });
    for (const auto& nodes : std::vector<std::vector<double>>{{0,.5,2.5,3,4},{0,1,2.5,3.5,4},{0,1,2.5,2.5,4},{0,1,4},{0,1,2.5,3,5}})
        fatal(FluidFatalCode::unsupported_topology,[&] { (void)material_plan(state,nodes); });
    fatal(FluidFatalCode::unsupported_configuration,[&] { (void)material_plan(state,{0,1,std::numeric_limits<double>::quiet_NaN(),3,4}); });
    check(fingerprint(state) == before,"Failure after accepted call changed canonical/coverage/identity/ledger/cache");
}
void signed_formation_offsets_preserve_cap_u_and_actual_energy() {
    for (bool opposing : {false,true}) {
        Setup setup(opposing ? std::ldexp(1.0,20) : -std::ldexp(1.0,20),opposing);
        auto input = setup.input({0,1,2,3,4},{1,3,1,1},std::vector<double>(5,2),false,opposing ? .5 : .25);
        const auto source = bind_material_endcaps(initialize_occupied_staggered(input));
        const auto result = rezone_hybrid_staggered(source,material_plan(source,{0,1,2.5,3,4})); balance(source,result);
        for (const std::size_t i : {std::size_t{0},std::size_t{3}})
            check(std::bit_cast<std::uint64_t>(source.canonical().cells()[i].mixture.energy) == std::bit_cast<std::uint64_t>(result.state.canonical().cells()[i].mixture.energy),"Zero-heat identity cap U was needlessly decomposed/readded");
    }
}
void non_dyadic_quadrature_signed_arithmetic_and_local_scales() {
    Setup setup; bool observed_normalization = false;
    for (int exponent : {-100,0,100}) for (double cut : {.19,.27,.31,.43,.53}) {
        const double unit = std::ldexp(1.0,exponent);
        std::vector<double> nodes{0,.1,.35,.6,1}, target{0,.1,cut,.6,1};
        for (auto& q : nodes) q *= unit;
        for (auto& q : target) q *= unit;
        auto input = setup.input(nodes,{.7,2.3,.2,1.1},{-.3,.7,-1.1,.4,2.2}); input.area_m2 = 1/unit; input.frame_origin_m = -1e100;
        const auto source = bind_material_endcaps(initialize_occupied_staggered(input));
        const auto p = plan_hybrid_material_rezone(source,target,{0,0,.125*unit,0,8});
        const auto result = rezone_hybrid_staggered(source,p); balance(source,result);
        double independently_summed_delta_h = 0, separately_reported_arithmetic = 0;
        for (std::size_t i = 0; i < result.report.mechanics.cells.size(); ++i) {
            const auto& c = result.report.mechanics.cells[i];
            const long double dm = static_cast<long double>(c.normalized_left.mass_kg)+c.normalized_right.mass_kg-c.raw_left.mass_kg-c.raw_right.mass_kg;
            const long double dp = static_cast<long double>(c.normalized_left.momentum_kg_m_per_s)+c.normalized_right.momentum_kg_m_per_s-c.raw_left.momentum_kg_m_per_s-c.raw_right.momentum_kg_m_per_s;
            const long double dh = static_cast<long double>(c.normalized_left.kinetic_j)+c.normalized_right.kinetic_j-c.raw_left.kinetic_j-c.raw_right.kinetic_j;
            check(std::abs(dm) <= c.quadrature_mass_bound_kg && std::abs(dp) <= c.normalization_momentum_bound_kg_m_per_s &&
                std::abs(dh) <= c.normalization_kinetic_bound_j,"Normalization exceeds measured packet arithmetic budget");
            independently_summed_delta_h += c.normalization_delta_kinetic_j; separately_reported_arithmetic += c.arithmetic_energy_j;
            observed_normalization = observed_normalization || c.normalization_delta_mass_kg != 0 || c.normalization_delta_momentum_kg_m_per_s != 0 || c.normalization_delta_kinetic_j != 0;
            if (i == 0 || i + 1 == result.report.mechanics.cells.size())
                check(c.lambda == 1 && c.normalization_delta_mass_kg == 0 && c.normalization_delta_momentum_kg_m_per_s == 0 && c.normalization_delta_kinetic_j == 0,"Cap identity took a quadrature normalization");
        }
        near(separately_reported_arithmetic,-independently_summed_delta_h,2e-13);
    }
    check(observed_normalization,"Non-dyadic packet cases did not exercise any certified normalization arithmetic");
}
void finite_large_kinetic_aggregate_has_finite_bounds() {
    // Every actual mass/U/K/P and EOS state is finite. Only H+K BEFORE
    // multiplying by 64 epsilon overflows: H = K = 2^1023 at node 1.
    // The distant material rezone is real, so an identity shortcut cannot
    // hide this range error. This is a numerical surrogate, not water data.
    SpeciesRegistry registry; SpeciesDefinition s;
    s.id = "dyadic_liquid"; s.name = "Arithmetic range liquid"; s.phase = Phase::liquid;
    s.atoms = {{"H",2},{"O",1}}; s.cp = std::ldexp(1.0,-10); s.density = 1024;
    s.formation_enthalpy = s.cp*reference_temperature*molecular_mass(s.atoms); registry.add(s);
    ReferenceMaterial m; m.id = s.id; m.phase = ReferencePhase::elastic_liquid;
    m.molar_mass_kg_per_mol = registry[0].molar_mass; m.cv_j_per_kg_k = registry[0].cv();
    m.energy_offset_j_per_kg = registry[0].internal_energy(0); m.minimum_temperature_k = .5; m.maximum_temperature_k = 2;
    m.zero_pressure_density_kg_per_m3 = 1024; m.reference_bulk_modulus_pa = 1024; m.maximum_density_ratio = 2;
    m.provenance = "Dyadic bound arithmetic test only";
    const auto model = std::make_shared<const FluidThermodynamics>(registry,"hybrid-bound-range-v1",std::vector{m});
    const double mass = std::ldexp(1.0,1016), area = std::ldexp(1.0,1006), expected_k = std::ldexp(1.0,1023);
    StaggeredInitialization input; input.thermodynamics = model; input.area_m2 = area;
    input.positions_m = {0,1,2,3,4,5}; input.velocities_m_per_s = {0,16,0,0,0,0};
    for (std::size_t i = 0; i < 5; ++i) input.cells.push_back({model->initialize_at_temperature(std::vector{mass},1,area).mixture,
        mass*std::midpoint(input.velocities_m_per_s[i],input.velocities_m_per_s[i+1])});
    const auto source = bind_material_endcaps(initialize_occupied_staggered(input));
    const auto result = rezone_hybrid_staggered(source,material_plan(source,{0,1,2,3.5,4,5})); balance(source,result);
    const auto before = source.canonical().totals(), after = result.state.canonical().totals();
    const auto& r = result.report.mechanics;
    check(!r.identity && std::isfinite(before.internal_energy_j+before.nodal_kinetic_energy_j),"Range fixture requires finite actual aggregate and real rezone");
    near(before.masses_kg[0],std::ldexp(5.0,1016),0); near(after.masses_kg[0],before.masses_kg[0],0);
    near(before.nodal_kinetic_energy_j,expected_k,0); near(after.nodal_kinetic_energy_j,expected_k,0);
    near(before.momentum_kg_m_per_s,std::ldexp(1.0,1020),0); near(after.momentum_kg_m_per_s,before.momentum_kg_m_per_s,0);
    near(after.internal_energy_j,before.internal_energy_j,0);
    near(r.nodes[1].transported_kinetic_j,expected_k,0); near(r.nodes[1].provisional_kinetic_j,expected_k,0);
    near(r.nodes[1].mixing_arithmetic_bound_j,std::ldexp(1.0,978),0);
    for (const auto& n : r.nodes) check(std::isfinite(n.mixing_arithmetic_bound_j),"Published infinite nodal convexity bound");
    for (const auto& c : r.cells) for (double b : {c.quadrature_mass_bound_kg,c.normalization_momentum_bound_kg_m_per_s,c.normalization_kinetic_bound_j})
        check(std::isfinite(b),"Published infinite packet bound");
    for (double b : {r.energy_arithmetic_bound_j,r.momentum_arithmetic_bound_kg_m_per_s,r.entropy_arithmetic_bound_j_per_k})
        check(std::isfinite(b),"Published infinite global arithmetic bound");
    check(r.energy_residual_j == 0 && r.mixing_heat_j == 0 && r.arithmetic_energy_j == 0,"Unchanged large mechanical packet created heat or energy");
    for (const auto& c : result.state.canonical().cells()) { near(c.thermodynamics.temperature_k,1,0); near(c.thermodynamics.mechanical_pressure_pa,0,0); }
    std::cout << std::setprecision(17) << "Finite range oracle K=" << expected_k << "; node bound=" << r.nodes[1].mixing_arithmetic_bound_j
        << "; global bound=" << r.energy_arithmetic_bound_j << "; actual dE=" << r.energy_residual_j << '\n';
}
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"independent protected dyadic packet/heat oracle",exact_independent_protected_dyadic_oracle},
        {"reflected/reversed fractions and huge frame translations",reflected_reversed_and_translated_oracles},
        {"real revision staleness, cap slots and authoritative coverage",real_revision_staleness_and_authoritative_coverage},
        {"same-revision branches reject plans; exact copies authenticate",alternate_same_revision_snapshots_cannot_reuse_plans},
        {"exact vacuum, whole slab and adjacent cap identities",vacuum_whole_slab_and_adjacent_identity},
        {"uniform gas/liquid/mixture and protected fixed walls",uniform_gas_liquid_mixture_and_fixed_walls},
        {"independent PE and typed entropy/domain rollback",independent_gravity_and_typed_entropy_domain_rollback},
        {"positive denormal shares and unrepresentable packet rejection",positive_denormal_shares_and_strict_packet_representability},
        {"explicit geometry/capacity/foreign failures after success",explicit_geometry_capacities_and_foreign_atomicity},
        {"signed/cancelling formation references and exact cap U",signed_formation_offsets_preserve_cap_u_and_actual_energy},
        {"non-dyadic normalization/signed arithmetic and local scales",non_dyadic_quadrature_signed_arithmetic_and_local_scales},
        {"finite large kinetic aggregate retains finite arithmetic bounds",finite_large_kinetic_aggregate_has_finite_bounds}
    };
    std::size_t passed = 0;
    for (const auto& [name,test] : tests) try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch(const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    std::cout << passed << '/' << tests.size() << " hybrid A1 material-rezone groups passed; no repeated-rate, pressure adapter or front gate claimed\n";
    return passed == tests.size() ? 0 : 1;
}
