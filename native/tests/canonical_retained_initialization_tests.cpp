#include "mixed_atmospheres/experimental/canonical_material.hpp"
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
void witness(const char* prefix, const CanonicalFailureWitness& value);
bool bits(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }
void hex(double value) {
    std::cout << '"' << std::hex << std::setw(16) << std::setfill('0') << std::bit_cast<std::uint64_t>(value) << std::dec << '"';
}
CanonicalInitialization fixture(int case_id) {
    CanonicalInitialization r;
    const auto standard = SpeciesRegistry::standard();
    const auto species = standard[standard.index("nitrogen")];
    r.registry.add(species);
    r.profile_version = "retained-owner-initialization-v2";
    r.diagnostic_model_identity = "retained-owner-init-first";
    r.materials.push_back({species.id, ReferencePhase::ideal_gas, species.molar_mass, species.cv(),
        species.internal_energy(0), 180, 900, 0, 0, 1,
        "Original bounded initialization control, not a production material catalogue"});
    r.expected_model_metadata = LiquidThermodynamicReference(r.profile_version, r.materials).metadata();
    r.positions_m = {-1, -std::ldexp(1.0, -60), 0, .25, 1, 1.25, 2, 3};
    r.reference_nodes_m = r.positions_m;
    r.velocities_m_per_s.resize(8);
    r.area_m2 = case_id == 0 ? 1.3 : .625;
    r.energy = case_id == 0 ? CanonicalInitialEnergy::temperature : CanonicalInitialEnergy::stored_internal_energy;
    r.completed_time_seconds = .125;
    for (std::size_t i = 0; i < 7; ++i) {
        CanonicalReferencePiece piece; piece.left = i; piece.right = i + 1; piece.owner = i;
        piece.density_left[0] = piece.density_right[0] = 1;
        r.pieces.push_back(piece); r.owner_ids[i] = 4100 + i;
        if (case_id == 0) r.temperatures_k[i] = 305;
        else {
            const double represented_mass = r.area_m2 * (r.reference_nodes_m[i + 1] - r.reference_nodes_m[i]);
            r.stored_internal_energy_j[i] = represented_mass * species.internal_energy(305);
        }
    }
    if (case_id == 1) for (std::size_t j = 0; j < 8; ++j)
        r.velocities_m_per_s[j] = std::ldexp(3.0 + static_cast<double>(j), -5);
    return r;
}
CanonicalMaterialState initialize(const CanonicalInitialization& input) {
    CanonicalMaterialState result; CanonicalBudgetContext budget; CanonicalFailureWitness failure;
    try { initialize_canonical_material_into(input, result, budget, failure); }
    catch (const CanonicalFailure&) { witness("UNEXPECTED", failure); throw; }
    check(result.initialized() && !failure.failed() && result.read_view().has_value(), "Incomplete initialization publication");
    return result;
}
void save(const std::filesystem::path& output, const char* name, const CanonicalReadView& view) {
    const auto path = output / name;
    check(!std::filesystem::exists(path), "Snapshot output already exists");
    const auto bytes = canonical_material_diagnostic_bytes(view);
    check(!bytes.empty() && bytes.size() <= 2 * 1024 * 1024, "Snapshot exceeds its diagnostic bound");
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.flush(); check(stream.good(), "Incomplete snapshot write");
    stream.close(); check(stream.good(), "Snapshot stream did not close cleanly");
}
void record(int case_id, const CanonicalInitialization& input, const CanonicalReadView& view) {
    const auto& d = view.snapshot(); const auto& material = input.materials[0];
    std::cout << "META {\"case\":" << case_id << ",\"snapshot\":\"case-" << case_id << ".snapshot.bin\",\"area\":";
    hex(input.area_m2); std::cout << ",\"cv\":"; hex(material.cv_j_per_kg_k);
    std::cout << ",\"molar_mass\":"; hex(material.molar_mass_kg_per_mol);
    std::cout << ",\"offset\":"; hex(material.energy_offset_j_per_kg);
    std::cout << ",\"gas_constant\":"; hex(reference_gas_constant); std::cout << "}\n";
    for (std::size_t j = 0; j < 8; ++j) {
        std::cout << "NODE [" << case_id << ',' << j;
        for (double value : {input.positions_m[j], d.positions_m[j], d.primary.relative_positions_m[j].high(),
            d.primary.relative_positions_m[j].low(), d.primary.momentum[j].high(), d.primary.momentum[j].low(),
            d.momentum[j], input.velocities_m_per_s[j], d.velocities_m_per_s[j]}) { std::cout << ','; hex(value); }
        std::cout << "]\n";
    }
    for (std::size_t i = 0; i < 7; ++i) {
        std::cout << "CELL [" << case_id << ',' << i;
        for (double value : {input.stored_internal_energy_j[i], d.primary.internal_energy_j[i].high(),
            d.primary.internal_energy_j[i].low(), d.cells[i].internal_energy_j, d.cells[i].volume_m3,
            d.thermodynamics_readings[i].temperature_k, d.thermodynamics_readings[i].mechanical_pressure_pa,
            d.cells[i].masses_kg[0]}) { std::cout << ','; hex(value); }
        std::cout << "]\n";
    }
    for (std::size_t j = 0; j < 8; ++j) for (std::size_t p = 0; p < 8; ++p) {
        const auto index = j * 8 + p;
        std::cout << "ROW [" << case_id << ',' << j << ',' << p;
        for (double value : {d.consistent_mass[j][p], input.velocities_m_per_s[p],
            d.initialization.momentum_product_terms[index][0], d.initialization.momentum_product_terms[index][1],
            d.initialization.momentum_sum_remainders[index][0], d.initialization.momentum_sum_remainders[index][1]}) {
            std::cout << ','; hex(value);
        }
        std::cout << "]\n";
    }
}
void witness(const char* prefix, const CanonicalFailureWitness& value) {
    check(value.failed() && value.capture_valid(), "Rejected operation lost its complete witness");
    std::array<char, canonical_witness_json_capacity> bytes{};
    const auto encoded = encode_canonical_witness_json(value, bytes);
    check(encoded.result == CanonicalEncodingResult::success, "Rejected operation witness did not encode");
    std::cout << prefix << ' '; std::cout.write(bytes.data(), static_cast<std::streamsize>(encoded.written)); std::cout << '\n';
}
struct RestoreRounding {
    int previous = std::fegetround();
    ~RestoreRounding() { std::fesetround(previous); }
};
}
int main(int argc, char** argv) {
    try {
        check(argc == 2 && std::filesystem::is_directory(argv[1]), "Supply a fresh existing diagnostic output directory");
        const std::filesystem::path output(argv[1]);
        auto input0 = fixture(0); auto state0 = initialize(input0); auto view0 = *state0.read_view();
        const auto& d0 = view0.snapshot();
        check(bits(d0.positions_m[1], d0.positions_m[2]), "Control does not expose collapsed scalar coordinates");
        check(d0.primary.relative_positions_m[1].low() != 0 && d0.owner_geometry[1].projected_volume_m3 > 0,
            "Retained geometry did not preserve the positive tiny interval");
        save(output, "case-0.snapshot.bin", view0); record(0, input0, view0);
        std::cout << "PASS retained_geometry_initialization\n";
        auto input1 = fixture(1); auto state1 = initialize(input1); auto view1 = *state1.read_view();
        bool has_low_pi = false, has_variance = false;
        for (std::size_t i = 0; i < 8; ++i) has_low_pi = has_low_pi || view1.snapshot().primary.momentum[i].low() != 0;
        for (std::size_t i = 0; i < 7; ++i) {
            check(bits(view1.snapshot().primary.internal_energy_j[i].high(), input1.stored_internal_energy_j[i]) &&
                view1.snapshot().primary.internal_energy_j[i].low() == 0, "Supplied U changed during retained initialization");
            has_variance = has_variance || view1.snapshot().owners[i].mechanical_variance_j > 0;
        }
        check(has_low_pi && has_variance, "Control did not retain low momentum and separate kinetic variance");
        save(output, "case-1.snapshot.bin", view1); record(1, input1, view1);
        std::cout << "PASS retained_initial_momentum_and_supplied_u\n";
        const auto before0 = canonical_material_diagnostic_bytes(view0), before1 = canonical_material_diagnostic_bytes(view1);
        input0.positions_m.assign(8, 999); input1.velocities_m_per_s.assign(8, 999);
        state0 = CanonicalMaterialState{};
        check(before0 == canonical_material_diagnostic_bytes(view0), "Retired owner or changed input altered the retained view");
        {
            RestoreRounding restore;
            check(std::fesetround(FE_UPWARD) == 0, "Cannot set diagnostic-only upward rounding");
            check(before0 == canonical_material_diagnostic_bytes(view0) && before1 == canonical_material_diagnostic_bytes(view1),
                "Diagnostic serialization performed control-dependent arithmetic");
        }
        check(std::fegetround() == FE_TONEAREST, "Diagnostic control did not restore nearest rounding");
        std::cout << "PASS owned_view_and_control_independent_diagnostics\n";
        check(!canonical_material_advance_ready(), "Unmigrated pressure path became selectable");
        CanonicalMaterialState previous = state1; CanonicalBudgetContext guard_budget; CanonicalFailureWitness guard; bool rejected = false;
        try { advance_canonical_material_into(state1, .001, previous, guard_budget, guard); }
        catch (const CanonicalFailure&) { rejected = true; }
        if (guard.failed()) witness("GUARD", guard);
        check(rejected && guard.code() == CanonicalFatalCode::unsupported_configuration && guard.stage() == CanonicalStage::publication &&
            guard_budget.counts().constitutive_charges == 0 && guard_budget.counts().factorizations == 0 &&
            before1 == canonical_material_diagnostic_bytes(*previous.read_view()) && before1 == canonical_material_diagnostic_bytes(view1),
            "Closed step boundary ran physics or changed complete state");
        auto bad = fixture(0); bad.area_m2 = std::numeric_limits<double>::min();
        CanonicalBudgetContext relay_budget; CanonicalFailureWitness relay; rejected = false;
        try { initialize_canonical_material_into(bad, previous, relay_budget, relay); }
        catch (const CanonicalFailure&) { rejected = true; }
        if (relay.failed()) witness("RELAY", relay);
        check(rejected && relay.code() == CanonicalFatalCode::numerical_precision && relay.stage() == CanonicalStage::reference_integration &&
            relay.model_identity() == bad.diagnostic_model_identity && relay.context().node == 0 && relay.context().owner == 0 &&
            relay_budget.counts().constitutive_charges == 0 && before1 == canonical_material_diagnostic_bytes(*previous.read_view()),
            "Arithmetic failure did not preserve owner context and previous publication");
        std::cout << "PASS closed_step_boundary_and_relay_atomicity\n";
        return 0;
    } catch (const CanonicalFailure& error) { std::cerr << "FAIL canonical initialization: " << error.what() << '\n'; return 1; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
