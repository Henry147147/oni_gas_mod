#include "mixed_atmospheres/experimental/canonical_material.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace ma;
using namespace ma::experimental;
namespace {
void check(bool value, const char* text) { if (!value) throw std::logic_error(text); }
void near(double value, double expected, double relative, const char* text) {
    check(std::isfinite(value) && std::abs(value - expected) <= relative * std::max(std::abs(value), std::abs(expected)), text);
}
bool bits(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }
void print_witness(const CanonicalFailureWitness& witness) {
    std::array<char, canonical_witness_json_capacity> buffer{};
    const auto status = encode_canonical_witness_json(witness, buffer);
    if (status.result != CanonicalEncodingResult::success) throw std::logic_error("Failure witness encoding failed");
    std::cout << "WITNESS "; std::cout.write(buffer.data(), static_cast<std::streamsize>(status.written)); std::cout << '\n';
}
CanonicalInitialization input(std::size_t count = 4, bool fine = true) {
    CanonicalInitialization r;
    r.profile_version = "canonical-owner-new-cases-2026-10-v1";
    r.diagnostic_model_identity = "canonical-owner-new-cases-v1";
    const auto standard = SpeciesRegistry::standard();
    // Registry order deliberately differs from profile order.
    const std::array<std::string, 4> ids{"nitrogen", "oxygen", "carbon_dioxide", "water"};
    for (std::size_t i = count; i-- > 0;) r.registry.add(standard[standard.index(ids[i])]);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& s = r.registry[r.registry.index(ids[i])];
        const bool gas = s.phase == Phase::gas;
        r.materials.push_back({s.id, gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid,
            s.molar_mass, s.cv(), s.internal_energy(0), 180, 900,
            gas ? 0 : s.density, gas ? 0 : 2.2e9, gas ? 1 : 1.08,
            "Repository standard species identity; bounded constant-Cv/elastic model validation fixture"});
    }
    r.expected_model_metadata = LiquidThermodynamicReference(r.profile_version, r.materials).metadata();
    r.area_m2 = .625; r.energy = CanonicalInitialEnergy::temperature;
    const std::size_t n = fine ? 16 : 8;
    r.reference_nodes_m.resize(n); r.positions_m.resize(n); r.velocities_m_per_s.resize(n);
    for (std::size_t j = 1; j < n; ++j) {
        r.reference_nodes_m[j] = r.reference_nodes_m[j - 1] + (fine ? .125 + .0625 * static_cast<double>((j - 1) % 3) : .375);
        r.positions_m[j] = r.reference_nodes_m[j];
    }
    for (std::size_t e = 0; e + 1 < n; ++e) {
        const auto owner = e * 7 / (n - 1);
        CanonicalReferencePiece piece; piece.left = e; piece.right = e + 1; piece.owner = owner;
        piece.density_left[0] = .8 + .03125 * static_cast<double>(owner);
        piece.density_right[0] = piece.density_left[0] + (fine ? .015625 : 0);
        if (count >= 2) piece.density_left[1] = piece.density_right[1] = .25;
        if (count >= 3) piece.density_left[2] = piece.density_right[2] = 1e-8;
        if (count == 4) piece.density_left[3] = piece.density_right[3] = 128 + 4 * static_cast<double>(owner);
        r.pieces.push_back(piece);
    }
    for (std::size_t i = 0; i < 7; ++i) { r.owner_ids[i] = 1201 + i * 19; r.temperatures_k[i] = 305 + 2 * static_cast<double>(i); }
    r.left.kind = r.right.kind = CanonicalBoundaryKind::fixed;
    return r;
}
CanonicalMaterialState initialized(const CanonicalInitialization& in) {
    CanonicalMaterialState r; CanonicalBudgetContext budget; CanonicalFailureWitness witness;
    try { initialize_canonical_material_into(in, r, budget, witness); }
    catch (const CanonicalFailure&) { print_witness(witness); throw; }
    check(r.initialized() && !witness.failed(), "Initialization did not publish a complete state"); return r;
}
std::vector<std::byte> bytes(const CanonicalMaterialState& r) {
    check(r.initialized(), "Cannot inspect an uninitialized test state");
    return canonical_material_diagnostic_bytes(*r.read_view());
}
void check_reference(const CanonicalSnapshot& r) {
    check(r.cells.size() == 7 && r.pieces.size() == r.reference_pieces.size(), "Snapshot shape differs");
    for (std::size_t i = 0; i < 7; ++i) {
        const auto& owner = r.owners[i]; const auto& cell = r.cells[i]; const auto& eos = r.thermodynamics_readings[i];
        check(eos.has_gas && !eos.has_vacuum && eos.mechanical_pressure_pa > 0, "Actual EOS phase state differs");
        for (std::size_t s = 0; s < cell.masses_kg.size(); ++s) {
            long double expected = 0;
            for (const auto& piece : r.reference_pieces) if (piece.owner == i)
                expected += static_cast<long double>(r.area_m2) *
                    (static_cast<long double>(r.reference_nodes_m[piece.right]) - r.reference_nodes_m[piece.left]) *
                    (static_cast<long double>(piece.density_left[s]) + piece.density_right[s]) / 2;
            near(cell.masses_kg[s], static_cast<double>(expected), 3e-14, "Independent species measure differs");
            check(bits(cell.masses_kg[s], owner.masses_kg[s]), "Reading changed a retained species bit");
            check(cell.masses_kg[s] == 0 || eos.partial_volumes_m3[s] > 0, "Positive trace lost its actual partial volume");
        }
        near(owner.resolved_kinetic_energy_j, owner.bulk_kinetic_energy_j + owner.mechanical_variance_j, 3e-14, "K partition differs");
    }
    for (std::size_t j = 0; j < r.momentum.size(); ++j) {
        long double p = 0, scale = std::abs(static_cast<long double>(r.momentum[j]));
        for (std::size_t k = 0; k < r.momentum.size(); ++k) {
            const long double term = static_cast<long double>(r.consistent_mass[j][k]) * r.velocities_m_per_s[k];
            p += term; scale += std::abs(term);
        }
        check(std::abs(p - r.momentum[j]) <= 6e-14L * scale, "Independent full consistent row differs");
    }
}
void owner_lifetime() {
    auto in = input();
    for (std::size_t j = 1; j + 1 < in.velocities_m_per_s.size(); ++j)
        in.velocities_m_per_s[j] = .0625 * static_cast<double>(static_cast<int>(j % 5) - 2);
    auto state = initialized(in); auto retained = *state.read_view(); const auto original = bytes(state);
    check_reference(retained.snapshot());
    check(retained.snapshot().resolved_kinetic_energy_j > 0, "Resolved K missing");
    check(retained.snapshot().thermodynamics->reference().materials()[0].id == "nitrogen", "Profile order lost");
    check(retained.snapshot().registry->all()[0].id == "water", "Actual registry order lost");
    in.materials.clear(); in.registry = SpeciesRegistry{}; in.pieces.clear(); in.positions_m.clear();
    state = CanonicalMaterialState{};
    check(canonical_material_diagnostic_bytes(retained) == original, "Read view borrowed retired caller/state storage");
    auto moved = std::move(retained);
    check(canonical_material_diagnostic_bytes(retained) == canonical_material_diagnostic_bytes(moved), "Move invalidated source view");
    check(original.size() < 262144, "Snapshot fixture exceeds per-file resource cap");
}
void supplied_u() {
    auto in = input(); auto first = initialized(in); const auto& a = first.read_view()->snapshot();
    in.energy = CanonicalInitialEnergy::stored_internal_energy; in.temperatures_k.fill(0);
    for (std::size_t i = 0; i < 7; ++i) in.stored_internal_energy_j[i] = a.cells[i].internal_energy_j;
    auto second = initialized(in); const auto& b = second.read_view()->snapshot();
    for (std::size_t i = 0; i < 7; ++i) {
        check(bits(a.cells[i].internal_energy_j, b.cells[i].internal_energy_j), "Supplied stored U was rewritten");
        check(b.initialization.explicitly_added_elastic_energy_j[i] == 0, "Supplied U acquired initialization heat");
        check(bits(a.thermodynamics_readings[i].temperature_k, b.thermodynamics_readings[i].temperature_k), "Same stored facts gave another T");
    }
    check_reference(b);
}
void check_step(const CanonicalMaterialState& old, const CanonicalMaterialState& next, double h, const std::vector<std::byte>& before) {
    check(bytes(old) == before, "Advance changed its input");
    const auto& a = old.read_view()->snapshot(); const auto& b = next.read_view()->snapshot(); const auto& report = b.last_step;
    check(b.completed_steps == a.completed_steps + 1 && bits(report.seconds, h), "Fixed-h completion differs");
    check(b.reference_pieces == a.reference_pieces && b.consistent_mass == a.consistent_mass, "Advance changed retained inertia/reference");
    check(report.force_residual_ratio <= 2e-12 && report.prescribed_displacements_exact, "Pressure/geometry gate differs");
    check(std::abs(report.energy_error_j) <= report.energy_budget_j, "Physical energy gate failed");
    check(std::abs(report.accounting_remainder_j) <= report.accounting_bound_j, "Finite energy ledger failed");
    check(std::abs(report.momentum_accounting_remainder) <= report.momentum_accounting_bound, "Finite momentum ledger failed");
    check(std::abs(report.first_moment_accounting_remainder) <= report.first_moment_bound, "First-moment ledger failed");
    check(std::abs(report.position_velocity_accounting_remainder) <= report.position_velocity_bound, "Bilinear ledger failed");
    for (std::size_t i = 0; i < 7; ++i) check(a.cells[i].masses_kg == b.cells[i].masses_kg, "Advance changed species");
    std::cout << "METRICS residual=" << report.force_residual_ratio << " energy=" << report.energy_error_j
        << " accounting=" << report.accounting_remainder_j << " newton=" << report.newton_updates
        << " charges=" << report.constitutive_charges << '\n';
}
void step_case(const std::string& name) {
    auto in = input(name == "free_gas" || name == "translation" ? 2 : 4, name == "translation");
    double h = 1.0 / 4096;
    if (name == "moving_port") {
        in.right.kind = CanonicalBoundaryKind::prescribed_velocity; in.right.velocity_m_per_s = .125;
        in.velocities_m_per_s.back() = .125;
    } else if (name == "free_gas") {
        in.left.kind = in.right.kind = CanonicalBoundaryKind::free;
    } else if (name == "translation") {
        in.left.kind = in.right.kind = CanonicalBoundaryKind::free; in.temperatures_k.fill(312);
        for (auto& piece : in.pieces) {
            piece.density_left = {.75, .25, 0, 0}; piece.density_right = piece.density_left;
        }
        std::fill(in.velocities_m_per_s.begin(), in.velocities_m_per_s.end(), .375);
        const auto& n = in.materials[0]; const auto& o = in.materials[1];
        in.left.exterior_pressure_pa = in.right.exterior_pressure_pa =
            reference_gas_constant * 312 * (.75 / n.molar_mass_kg_per_mol + .25 / o.molar_mass_kg_per_mol);
        h = 1.0 / 2048;
    }
    auto old = initialized(in); auto next = initialized(input(2, false));
    const auto before = bytes(old), previous = bytes(next);
    CanonicalBudgetContext budget; CanonicalFailureWitness witness;
    try { advance_canonical_material_into(old, h, next, budget, witness); }
    catch (const CanonicalFailure&) {
        print_witness(witness);
        check(bytes(old) == before && bytes(next) == previous, "Failed advance changed input/previous output");
        std::cout << "PRESERVED input_and_previous_output_bytes\n"; throw;
    }
    check_step(old, next, h, before); check_reference(next.read_view()->snapshot());
    if (name == "translation") for (double u : next.read_view()->snapshot().velocities_m_per_s) near(u, .375, 2e-11, "Uniform translation changed velocity");
}
void failed_init_preserves() {
    auto valid = input(); auto output = initialized(valid); const auto before = bytes(output);
    const std::array<std::function<void(CanonicalInitialization&)>, 5> mutations{
        [](auto& r) { r.expected_model_metadata += "mismatch"; },
        [](auto& r) { r.positions_m[3] = r.positions_m[2]; },
        [](auto& r) { r.pieces[0].density_left[2] = std::numeric_limits<double>::denorm_min(); },
        [](auto& r) { r.temperatures_k[0] = 0; },
        [](auto& r) { r.owner_ids[1] = r.owner_ids[0]; }};
    for (const auto& mutate : mutations) {
        auto bad = valid; mutate(bad); CanonicalBudgetContext budget; CanonicalFailureWitness witness; bool rejected = false;
        try { initialize_canonical_material_into(bad, output, budget, witness); }
        catch (const CanonicalFailure&) { rejected = true; }
        check(rejected && witness.failed() && witness.capture_valid(), "Invalid init lacked a typed complete witness");
        check(bytes(output) == before, "Invalid init changed previous output");
    }
}
void failed_step_preserves() {
    const auto old = initialized(input(2, false)); auto out = initialized(input()); const auto a = bytes(old), b = bytes(out);
    for (double h : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::denorm_min()}) {
        CanonicalBudgetContext budget; CanonicalFailureWitness witness; bool rejected = false;
        try { advance_canonical_material_into(old, h, out, budget, witness); }
        catch (const CanonicalFailure&) { rejected = true; }
        check(rejected && witness.failed() && witness.capture_valid(), "Invalid timestep lacked a typed witness");
        check(bytes(old) == a && bytes(out) == b, "Rejected timestep changed state");
    }
    CanonicalBudgetContext budget; CanonicalFailureWitness witness; bool rejected = false;
    try { advance_canonical_material_into(out, .001, out, budget, witness); }
    catch (const CanonicalFailure&) { rejected = true; }
    check(rejected && bytes(out) == b, "Aliased advance was admitted or changed state");
}
}
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "One predeclared case name is required\n"; return 2; }
    std::string name = argv[1];
    const std::vector<std::string> cases = name == "suite" ? std::vector<std::string>{
        "owner_lifetime", "supplied_u", "failed_init", "failed_step", "moving_port", "free_gas", "translation"} : std::vector<std::string>{name};
    try {
        for (const auto& selected : cases) {
            name = selected;
            if (name == "owner_lifetime") owner_lifetime();
            else if (name == "supplied_u") supplied_u();
            else if (name == "moving_port" || name == "free_gas" || name == "translation") step_case(name);
            else if (name == "failed_init") failed_init_preserves();
            else if (name == "failed_step") failed_step_preserves();
            else throw std::logic_error("Unknown case");
            std::cout << "PASS " << name << '\n';
        }
        return 0;
    } catch (const CanonicalFailure& error) {
        std::cerr << "FAIL " << name << " code=" << static_cast<int>(error.code()) << " stage=" << static_cast<int>(error.stage()) << " " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) { std::cerr << "FAIL " << name << " " << error.what() << '\n'; return 1; }
}
