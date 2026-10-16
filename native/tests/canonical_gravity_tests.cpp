#include "mixed_atmospheres/experimental/canonical_material.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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
struct HydrostaticTargets {
    std::array<double, 7> pressure{}, density{}, alpha{}, beta{}, length{};
};
HydrostaticTargets hydrostatic_targets() {
    HydrostaticTargets r; const auto standard = SpeciesRegistry::standard();
    const double mixture_molar_mass = .79 * standard[standard.index("nitrogen")].molar_mass +
        .2099 * standard[standard.index("oxygen")].molar_mass + .0001 * standard[standard.index("carbon_dioxide")].molar_mass;
    const double rho0 = standard[standard.index("water")].density;
    r.length = {.25, .375, .5, .625, .75, .875, 1};
    for (std::size_t i = 0; i < 7; ++i) {
        r.alpha[i] = i < 3 ? rho0 / 2.2e9 : mixture_molar_mass / (reference_gas_constant * 315);
        r.beta[i] = i < 3 ? rho0 : 0;
    }
    r.pressure[0] = 250000;
    for (std::size_t i = 1; i < 7; ++i)
        r.pressure[i] = (r.pressure[i - 1] * (1 - .5 * 9.81 * r.length[i - 1] * r.alpha[i - 1]) -
            .5 * 9.81 * (r.length[i - 1] * r.beta[i - 1] + r.length[i] * r.beta[i])) /
            (1 + .5 * 9.81 * r.length[i] * r.alpha[i]);
    for (std::size_t i = 0; i < 7; ++i) r.density[i] = r.alpha[i] * r.pressure[i] + r.beta[i];
    return r;
}
CanonicalInitialization input(int case_id) {
    CanonicalInitialization r; const auto standard = SpeciesRegistry::standard();
    r.profile_version = "retained-gravity-controls-v4"; r.diagnostic_model_identity = "retained-gravity-first-controls";
    const std::vector<std::string> ids = case_id == 0 ? std::vector<std::string>{"nitrogen"} :
        case_id == 1 ? std::vector<std::string>{"nitrogen", "oxygen", "carbon_dioxide", "water"} :
        std::vector<std::string>{"nitrogen", "water"};
    for (std::size_t i = ids.size(); i-- > 0;) r.registry.add(standard[standard.index(ids[i])]);
    for (const auto& id : ids) {
        const auto& s = r.registry[r.registry.index(id)]; const bool gas = s.phase == Phase::gas;
        r.materials.push_back({id, gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid, s.molar_mass, s.cv(),
            s.internal_energy(0), 180, 900, gas ? 0 : s.density, gas ? 0 : 2.2e9, gas ? 1 : 1.08,
            "Original constant-gravity control using unchanged repository species and bounded reference EOS"});
    }
    r.expected_model_metadata = LiquidThermodynamicReference(r.profile_version, r.materials).metadata();
    r.area_m2 = .75; r.gravity_down_m_per_s2 = 9.81; r.energy = CanonicalInitialEnergy::temperature;
    r.completed_time_seconds = case_id == 2 ? std::ldexp(1.0, 40) : .125;
    if (case_id == 0) r.positions_m = {0, .25, .625, 1, 1.5, 2.125, 2.75, 3.5, 4.25};
    else if (case_id == 1) r.positions_m = {0, .25, .625, 1.125, 1.75, 2.5, 3.375, 4.375};
    else r.positions_m = {-1.75, -1.33, -.79, -.16, .41, 1.09, 1.87, 2.68, 3.57, 4.51, 5.53};
    r.reference_nodes_m = r.positions_m; const auto n = r.positions_m.size(); r.velocities_m_per_s.resize(n);
    for (std::size_t j = 0; j < n; ++j) r.velocities_m_per_s[j] = case_id == 0 ? .25 : case_id == 1 ? 0 : .03125 + static_cast<double>(j % 3) / 1024;
    if (case_id == 2) {
        r.left.kind = CanonicalBoundaryKind::fixed; r.right.kind = CanonicalBoundaryKind::prescribed_velocity;
        r.right.velocity_m_per_s = .03125; r.velocities_m_per_s.front() = 0; r.velocities_m_per_s.back() = .03125;
    }
    const auto hydro = hydrostatic_targets();
    const std::array<double, 3> mole_fractions{.79, .2099, .0001};
    double gas_molar_mass = 0;
    if (case_id == 1) for (std::size_t s = 0; s < 3; ++s) gas_molar_mass += mole_fractions[s] * r.materials[s].molar_mass_kg_per_mol;
    for (std::size_t e = 0; e + 1 < n; ++e) {
        const auto owner = e * 7 / (n - 1); CanonicalReferencePiece piece; piece.left = e; piece.right = e + 1; piece.owner = owner;
        if (case_id == 0) piece.density_left[0] = piece.density_right[0] = 1;
        else if (case_id == 1) {
            if (owner < 3) piece.density_left[3] = piece.density_right[3] = hydro.density[owner];
            else for (std::size_t s = 0; s < 3; ++s) piece.density_left[s] = piece.density_right[s] =
                hydro.density[owner] * mole_fractions[s] * r.materials[s].molar_mass_kg_per_mol / gas_molar_mass;
        } else {
            piece.density_left[0] = .8 + .0375 * static_cast<double>(owner); piece.density_right[0] = piece.density_left[0] + .01125;
            piece.density_left[1] = piece.density_right[1] = 625;
        }
        r.pieces.push_back(piece);
    }
    for (std::size_t i = 0; i < 7; ++i) { r.owner_ids[i] = 9301 + 17 * i; r.temperatures_k[i] = case_id == 2 ? 296 + 4 * static_cast<double>(i) : 315; }
    if (case_id == 0) r.left.exterior_pressure_pa = r.right.exterior_pressure_pa = reference_gas_constant * 315 / r.materials[0].molar_mass_kg_per_mol;
    if (case_id == 1) {
        r.left.exterior_pressure_pa = hydro.pressure.front() + .5 * r.gravity_down_m_per_s2 * hydro.length.front() * hydro.density.front();
        r.right.exterior_pressure_pa = hydro.pressure.back() - .5 * r.gravity_down_m_per_s2 * hydro.length.back() * hydro.density.back();
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
            state.last_step.reaction[j], state.last_step.actual_force[j], state.last_step.gravity_force[j]}) { std::cout << ','; hex(value); }
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
    auto in = input(case_id); const double h = std::ldexp(1.0, case_id == 0 ? -8 : case_id == 1 ? -10 : -14);
    std::cout << "CASE {\"case\":" << case_id << ",\"nodes\":" << in.positions_m.size() << ",\"species\":" << in.materials.size() << ",\"h\":"; hex(h);
    std::cout << ",\"area\":"; hex(in.area_m2); std::cout << ",\"g\":"; hex(in.gravity_down_m_per_s2);
    std::cout << ",\"left_kind\":" << static_cast<int>(in.left.kind) << ",\"right_kind\":" << static_cast<int>(in.right.kind);
    std::cout << ",\"left_velocity\":"; hex(in.left.velocity_m_per_s); std::cout << ",\"right_velocity\":"; hex(in.right.velocity_m_per_s); std::cout << "}\n";
    if (case_id == 1) {
        const auto hydro = hydrostatic_targets();
        for (std::size_t i = 0; i < 7; ++i) {
            std::cout << "HYDRO [" << case_id << ',' << i;
            for (double value : {hydro.pressure[i], hydro.density[i], hydro.alpha[i], hydro.beta[i], hydro.length[i]}) { std::cout << ','; hex(value); }
            std::cout << "]\n";
        }
    }
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
    save(directory, "case-" + std::to_string(case_id) + "-after.bin", bytes(output));
    check(!failure.failed() && bytes(state) == before && canonical_material_diagnostic_bytes(initial) == before, "Advance changed its immutable input or view");
    const auto view = *output.read_view(); const auto& after = view.snapshot(); const auto& report = after.last_step;
    check(after.completed_steps == 1 && after.consistent_mass == initial.snapshot().consistent_mass, "Completed step or reference matrix differs");
    for (std::size_t i = 0; i < 7; ++i) check(after.cells[i].masses_kg == initial.snapshot().cells[i].masses_kg, "Pressure step changed constituent bits");
    record_state(case_id, 1, after);
    std::cout << "SUMMARY {\"case\":" << case_id;
    const auto field = [&](const char* name, double value) { std::cout << ",\"" << name << "\":"; hex(value); };
    field("k0", report.old_kinetic_energy_j); field("k1", report.new_kinetic_energy_j); field("energy_error", report.energy_error_j);
    field("energy_budget", report.energy_budget_j); field("energy_remainder", report.accounting_remainder_j); field("energy_bound", report.accounting_bound_j);
    field("momentum_error", report.momentum_error); field("momentum_remainder", report.momentum_accounting_remainder); field("momentum_bound", report.momentum_accounting_bound);
    field("first_change", report.first_moment_change); field("first_error", report.first_moment_error); field("first_remainder", report.first_moment_accounting_remainder); field("first_bound", report.first_moment_bound);
    field("relative_bilinear", report.relative_position_velocity_change); field("bilinear_change", report.position_velocity_change);
    field("bilinear_remainder", report.position_velocity_accounting_remainder); field("bilinear_bound", report.position_velocity_bound);
    field("potential_change", report.gravitational_potential_change_j); field("gravity_work", report.gravity_work_j);
    field("gravity_defect", report.gravity_arithmetic_defect_j); field("gravity_impulse", report.gravity_impulse);
    field("force_ratio", report.force_residual_ratio); field("thermo_ratio", report.maximum_thermodynamic_ratio); field("work_ratio", report.maximum_work_oracle_ratio);
    std::cout << ",\"charges\":" << budget.counts().constitutive_charges << ",\"factorizations\":" << budget.counts().factorizations << "}\n";
    if (case_id == 0) {
        const double expected_velocity = .25 - in.gravity_down_m_per_s2 * h;
        const double expected_displacement = h * .25 - .5 * in.gravity_down_m_per_s2 * h * h;
        const double scale = std::max(.25, in.gravity_down_m_per_s2 * h);
        for (std::size_t j = 0; j < in.positions_m.size(); ++j) {
            check(std::abs(after.velocities_m_per_s[j] - expected_velocity) <= 2e-10 * scale, "Uniform gravity velocity differs from independent free fall");
            check(std::abs(report.retained.displacement[j].projection - expected_displacement) <= 2e-10 * h * scale,
                "Uniform gravity displacement differs from independent free fall");
        }
    }
    std::cout << "PASS " << (case_id == 0 ? "uniform_freefall" : case_id == 1 ? "supported_hydrostatic_column" : "mixed_boundary_gravity") << '\n';
    return output;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[2]) == "--diagnostic-freefall-only") {
            check(std::filesystem::is_directory(argv[1]), "Supply a fresh diagnostic directory");
            static_cast<void>(run(0, std::filesystem::path(argv[1])));
            std::cerr << "FAIL Diagnostic input unexpectedly passed the original free-fall check\n";
            return 2;
        }
        check(argc == 2 && std::filesystem::is_directory(argv[1]) && canonical_material_advance_ready(), "Supply a fresh output directory");
        const std::filesystem::path directory(argv[1]);
        static_cast<void>(run(0, directory)); static_cast<void>(run(1, directory)); auto state = run(2, directory);
        const auto before = bytes(state); auto output = state;
        const std::array<double, 3> invalid{-1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::denorm_min()};
        for (std::size_t control = 0; control < invalid.size(); ++control) {
            auto bad = input(2); bad.gravity_down_m_per_s2 = invalid[control];
            CanonicalBudgetContext budget; CanonicalFailureWitness failure; bool rejected = false;
            try { initialize_canonical_material_into(bad, output, budget, failure); }
            catch (const CanonicalFailure&) { rejected = true; }
            if (failure.failed()) witness(control == 0 ? "negative-gravity" : control == 1 ? "nonfinite-gravity" : "subnormal-gravity", failure);
            check(rejected && bytes(state) == before && bytes(output) == before && budget.counts().constitutive_charges == 0 &&
                budget.counts().factorizations == 0, "Invalid gravity changed the prior state or queried EOS");
        }
        std::cout << "PASS typed_atomic_gravity_rejections\n"; return 0;
    } catch (const CanonicalFailure& error) { std::cerr << "FAIL retained gravity: " << error.what() << '\n'; return 1; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
