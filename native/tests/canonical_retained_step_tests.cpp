#include "mixed_atmospheres/experimental/canonical_material.hpp"
#include <bit>
#include <cfenv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ma;
using namespace ma::experimental;
namespace {
void check(bool value, const char* reason) { if (!value) throw std::logic_error(reason); }
void hex(double value) {
    std::cout << '"' << std::hex << std::setw(16) << std::setfill('0') << std::bit_cast<std::uint64_t>(value) << std::dec << '"';
}
void witness(const char* name, const CanonicalFailureWitness& value) {
    check(value.failed(), "Failure lost its owned witness");
    std::array<char, canonical_witness_json_capacity> buffer{};
    const auto status = encode_canonical_witness_json(value, buffer);
    check(status.result == CanonicalEncodingResult::success, "Failure witness encoding failed");
    std::cout << "WITNESS [\"" << name << "\",";
    std::cout.write(buffer.data(), static_cast<std::streamsize>(status.written)); std::cout << "]\n";
    check(value.capture_valid(), "Failure witness reports incomplete capture");
}
CanonicalInitialization input(int case_id) {
    CanonicalInitialization r; const auto standard = SpeciesRegistry::standard();
    r.profile_version = "retained-pressure-new-controls-v3";
    r.diagnostic_model_identity = "retained-pressure-first-controls";
    const std::vector<std::string> ids = case_id == 0 ? std::vector<std::string>{"nitrogen", "oxygen", "carbon_dioxide", "chlorine"} :
        std::vector<std::string>{"nitrogen", "water"};
    for (std::size_t i = ids.size(); i-- > 0;) r.registry.add(standard[standard.index(ids[i])]);
    for (const auto& id : ids) {
        const auto& species = r.registry[r.registry.index(id)]; const bool gas = species.phase == Phase::gas;
        r.materials.push_back({id, gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid,
            species.molar_mass, species.cv(), species.internal_energy(0), 180, 900,
            gas ? 0 : species.density, gas ? 0 : 2.2e9, gas ? 1 : 1.08,
            "Original retained-pressure validation input using unchanged repository species and bounded reference profiles"});
    }
    r.expected_model_metadata = LiquidThermodynamicReference(r.profile_version, r.materials).metadata();
    r.area_m2 = case_id == 0 ? .875 : .75;
    r.energy = case_id == 0 ? CanonicalInitialEnergy::stored_internal_energy : CanonicalInitialEnergy::temperature;
    r.completed_time_seconds = case_id == 1 ? 1.5 : std::ldexp(1.0, 40);
    if (case_id == 0) r.positions_m = {0, .18, .41, .7, 1.2, 1.9, 2.5, 3.3, 4};
    else if (case_id == 1) r.positions_m = {0, .2, .5, .875, 1.5, 2.125, 3, 3.625, 4.4, 5.3};
    else r.positions_m = {-2.25, -1.93, -1.37, -.71, .13, .86, 1.59, 2.47, 3.19, 4.08, 5.11};
    r.reference_nodes_m = r.positions_m; const auto n = r.positions_m.size();
    r.velocities_m_per_s.resize(n);
    for (std::size_t j = 0; j < n; ++j) {
        if (case_id == 0) r.velocities_m_per_s[j] = (static_cast<double>(j) - 4) / 256;
        else if (case_id == 1) r.velocities_m_per_s[j] = static_cast<double>(j) / (64 * static_cast<double>(n - 1));
        else r.velocities_m_per_s[j] = .0625 + static_cast<double>(j % 3) / 2048;
    }
    if (case_id != 0) {
        r.left.kind = CanonicalBoundaryKind::fixed;
        r.right.kind = CanonicalBoundaryKind::prescribed_velocity;
        r.left.velocity_m_per_s = 0;
        r.right.velocity_m_per_s = case_id == 1 ? 1.0 / 64 : .0625;
        r.velocities_m_per_s.front() = r.left.velocity_m_per_s; r.velocities_m_per_s.back() = r.right.velocity_m_per_s;
    }
    std::array<std::array<double, 4>, 7> masses{};
    for (std::size_t e = 0; e + 1 < n; ++e) {
        const auto owner = e * 7 / (n - 1); CanonicalReferencePiece piece;
        piece.left = e; piece.right = e + 1; piece.owner = owner;
        piece.density_left[0] = .8 + .0375 * static_cast<double>(owner);
        piece.density_right[0] = piece.density_left[0] + .01125;
        if (case_id == 0) {
            piece.density_left[1] = piece.density_right[1] = .27;
            piece.density_left[2] = piece.density_right[2] = .0006;
            piece.density_left[3] = piece.density_right[3] = .000003;
        } else piece.density_left[1] = piece.density_right[1] = 625;
        const double volume = r.area_m2 * (r.reference_nodes_m[e + 1] - r.reference_nodes_m[e]);
        for (std::size_t s = 0; s < ids.size(); ++s)
            masses[owner][s] += volume * (.5 * piece.density_left[s] + .5 * piece.density_right[s]);
        r.pieces.push_back(piece);
    }
    for (std::size_t i = 0; i < 7; ++i) {
        r.owner_ids[i] = 8107 + 23 * i;
        const double temperature = 296 + 4 * static_cast<double>(i);
        if (case_id == 0) for (std::size_t s = 0; s < ids.size(); ++s)
            r.stored_internal_energy_j[i] += masses[i][s] * (r.materials[s].energy_offset_j_per_kg + r.materials[s].cv_j_per_kg_k * temperature);
        else r.temperatures_k[i] = temperature;
    }
    return r;
}
std::vector<std::byte> bytes(const CanonicalMaterialState& state) { return canonical_material_diagnostic_bytes(*state.read_view()); }
void save(const std::filesystem::path& directory, const std::string& name, const std::vector<std::byte>& value) {
    const auto path = directory / name; check(!std::filesystem::exists(path), "Output already exists");
    check(!value.empty() && value.size() <= 2 * 1024 * 1024, "Diagnostic snapshot exceeds bound");
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
    stream.flush(); check(stream.good(), "Snapshot write failed"); stream.close(); check(stream.good(), "Snapshot close failed");
}
CanonicalMaterialState initialize(const CanonicalInitialization& in) {
    CanonicalMaterialState result; CanonicalBudgetContext budget; CanonicalFailureWitness failure;
    try { initialize_canonical_material_into(in, result, budget, failure); }
    catch (const CanonicalFailure&) { witness("unexpected-initialization", failure); throw; }
    check(result.initialized() && !failure.failed(), "Initialization did not publish"); return result;
}
void record_state(int case_id, int endpoint, const CanonicalSnapshot& state) {
    for (std::size_t j = 0; j < state.positions_m.size(); ++j) {
        std::cout << "NODE [" << case_id << ',' << endpoint << ',' << j << ','; hex(state.velocities_m_per_s[j]);
        if (endpoint == 1) for (double value : {state.last_step.requested_displacement[j], state.last_step.exterior_force[j],
            state.last_step.reaction[j], state.last_step.actual_force[j]}) { std::cout << ','; hex(value); }
        std::cout << "]\n";
    }
    for (std::size_t i = 0; i < 7; ++i) {
        const auto& cell = state.cells[i]; const auto& eos = state.thermodynamics_readings[i];
        std::cout << "CELL [" << case_id << ',' << endpoint << ',' << i;
        for (double value : {cell.volume_m3, eos.temperature_k, eos.mechanical_pressure_pa, eos.thermal_energy_j, eos.elastic_energy_j}) {
            std::cout << ','; hex(value);
        }
        for (std::size_t s = 0; s < 4; ++s) { std::cout << ','; hex(s < cell.masses_kg.size() ? cell.masses_kg[s] : 0); }
        if (endpoint == 1) for (double value : {state.last_step.work_pressure[i], state.last_step.balanced_energy_change[i],
            state.last_step.requested_volume_change[i], state.last_step.entropy_change[i], state.last_step.entropy_cache_difference[i]}) {
            std::cout << ','; hex(value);
        }
        std::cout << "]\n";
    }
}
CanonicalMaterialState run(int case_id, const std::filesystem::path& directory) {
    auto in = input(case_id); const double h = std::ldexp(1.0, case_id == 2 ? -56 : -17);
    std::cout << "CASE {\"case\":" << case_id << ",\"nodes\":" << in.positions_m.size() << ",\"species\":" << in.materials.size() << ",\"h\":"; hex(h);
    std::cout << ",\"area\":"; hex(in.area_m2); std::cout << ",\"left_kind\":" << static_cast<int>(in.left.kind) << ",\"right_kind\":" << static_cast<int>(in.right.kind);
    std::cout << ",\"left_velocity\":"; hex(in.left.velocity_m_per_s); std::cout << ",\"right_velocity\":"; hex(in.right.velocity_m_per_s); std::cout << "}\n";
    auto state = initialize(in); auto output = state; const auto before = bytes(state);
    save(directory, "case-" + std::to_string(case_id) + "-before.bin", before);
    const auto initial = *state.read_view(); record_state(case_id, 0, initial.snapshot());
    for (std::size_t j = 0; j < in.positions_m.size(); ++j) {
        std::cout << "INPUT [" << case_id << ',' << j << ','; hex(in.positions_m[j]); std::cout << "]\n";
        for (std::size_t p = 0; p < in.positions_m.size(); ++p) {
            std::cout << "ROW [" << case_id << ',' << j << ',' << p << ','; hex(initial.snapshot().consistent_mass[j][p]); std::cout << "]\n";
        }
    }
    CanonicalBudgetContext budget; CanonicalFailureWitness failure;
    try { advance_canonical_material_into(state, h, output, budget, failure); }
    catch (const CanonicalFailure&) {
        witness("unexpected-advance", failure);
        const bool input_preserved = bytes(state) == before, output_preserved = bytes(output) == before;
        std::cout << "ATOMIC [" << case_id << ',' << (input_preserved ? "true" : "false") << ',' << (output_preserved ? "true" : "false") << "]\n";
        save(directory, "case-" + std::to_string(case_id) + "-rejected-output.bin", bytes(output)); throw;
    }
    check(!failure.failed() && bytes(state) == before && canonical_material_diagnostic_bytes(initial) == before, "Advance changed its immutable input or view");
    const auto view = *output.read_view(); const auto& after = view.snapshot(); const auto& report = after.last_step;
    check(after.completed_steps == 1 && after.consistent_mass == initial.snapshot().consistent_mass, "Completed step or reference matrix differs");
    for (std::size_t i = 0; i < 7; ++i) check(after.cells[i].masses_kg == initial.snapshot().cells[i].masses_kg, "Pressure step changed constituent bits");
    if (case_id != 1) check(after.primary.completed_time_seconds.high() == initial.snapshot().primary.completed_time_seconds.high() &&
        after.primary.completed_time_seconds.low() != 0, "Tiny completed-time change was not retained");
    if (case_id == 2) {
        bool found = false;
        for (std::size_t j = 0; j < in.positions_m.size(); ++j) found = found ||
            (after.positions_m[j] == initial.snapshot().positions_m[j] && report.retained.relative_position_change[j].expansion_count != 0);
        check(found, "Small-change control does not expose a retained relative update behind an unchanged scalar projection");
    }
    save(directory, "case-" + std::to_string(case_id) + "-after.bin", bytes(output)); record_state(case_id, 1, after);
    std::cout << "SUMMARY {\"case\":" << case_id;
    const auto field = [&](const char* name, double value) { std::cout << ",\"" << name << "\":"; hex(value); };
    field("k0", report.old_kinetic_energy_j); field("k1", report.new_kinetic_energy_j); field("energy_error", report.energy_error_j);
    field("energy_budget", report.energy_budget_j); field("energy_remainder", report.accounting_remainder_j); field("energy_bound", report.accounting_bound_j);
    field("momentum_error", report.momentum_error); field("momentum_remainder", report.momentum_accounting_remainder); field("momentum_bound", report.momentum_accounting_bound);
    field("first_change", report.first_moment_change); field("first_error", report.first_moment_error); field("first_remainder", report.first_moment_accounting_remainder); field("first_bound", report.first_moment_bound);
    field("relative_bilinear", report.relative_position_velocity_change); field("bilinear_change", report.position_velocity_change);
    field("bilinear_remainder", report.position_velocity_accounting_remainder); field("bilinear_bound", report.position_velocity_bound);
    field("force_ratio", report.force_residual_ratio); field("thermo_ratio", report.maximum_thermodynamic_ratio); field("work_ratio", report.maximum_work_oracle_ratio);
    std::cout << ",\"charges\":" << budget.counts().constitutive_charges << ",\"factorizations\":" << budget.counts().factorizations << "}\n";
    std::cout << "PASS " << (case_id == 0 ? "four_species_retained_pressure" : case_id == 1 ? "gas_liquid_prescribed_port" : "small_retained_geometry_and_time") << '\n';
    return output;
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2 && std::filesystem::is_directory(argv[1]) && canonical_material_advance_ready(), "Supply a fresh output directory and reviewed retained step");
        const std::filesystem::path directory(argv[1]);
        static_cast<void>(run(0, directory)); static_cast<void>(run(1, directory)); auto state = run(2, directory);
        const auto before = bytes(state); auto output = state;
        for (int control = 0; control < 3; ++control) {
            CanonicalBudgetContext budget; CanonicalFailureWitness failure; bool rejected = false;
            const int previous = std::fegetround();
            if (control == 2) check(std::fesetround(FE_UPWARD) == 0, "Cannot select unsupported-rounding control");
            try { advance_canonical_material_into(state, control == 0 ? 0 : .001, control == 1 ? state : output, budget, failure); }
            catch (const CanonicalFailure&) { rejected = true; }
            if (control == 2) check(std::fesetround(previous) == 0, "Cannot restore arithmetic controls");
            if (failure.failed()) witness(control == 0 ? "zero-step" : control == 1 ? "alias" : "unsupported-rounding", failure);
            check(rejected && bytes(state) == before && bytes(output) == before && budget.counts().constitutive_charges == 0 &&
                budget.counts().factorizations == 0, "Rejected operation changed state or entered constitutive physics");
        }
        std::cout << "PASS typed_atomic_step_rejections\n"; return 0;
    } catch (const CanonicalFailure& error) { std::cerr << "FAIL retained pressure: " << error.what() << '\n'; return 1; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
