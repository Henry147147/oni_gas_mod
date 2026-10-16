#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string_view>

// Experimental failure evidence only. No simulator, EOS dispatch, game hook or
// state serialization is provided by this component.
namespace ma::experimental {
inline constexpr std::size_t canonical_witness_scalar_capacity = 248;
inline constexpr std::size_t canonical_witness_row_capacity = 8;
inline constexpr std::size_t canonical_witness_row_length = 33;
inline constexpr std::size_t canonical_witness_numeric_capacity = 512;
inline constexpr std::size_t canonical_witness_json_capacity = 32768;
inline constexpr std::uint32_t canonical_absent_index = UINT32_MAX;

enum class CanonicalFatalCode : std::uint8_t {
    unsupported_topology, unsupported_configuration, numerical_precision
};
enum class CanonicalStage : std::uint8_t {
    none, preflight, model_validation, reference_integration, initialization,
    canonical_decode, pressure_trial, free_impulse, constrained_impulse,
    reaction, geometry, thermodynamics, conservation, publication, diagnostics,
    count
};
enum class CanonicalWitnessField : std::uint16_t {
    timestep, pi0, pi1, velocity0, velocity1, compatibility0, compatibility1,
    delta_velocity, mass_delta_velocity, requested_impulse, represented_impulse,
    pressure_force, exterior_force, reaction_force, assembled_force,
    assembled_impulse, momentum_residual, requested_displacement,
    represented_displacement, prescribed_displacement, kinematic_defect,
    linearity_defect, join_defect, impulse_defect, row_scale, arithmetic_bound,
    sum_head, sum_correction, matrix_coefficients, matrix_operands,
    matrix_products, mass, stored_internal_energy, occupied_volume,
    temperature, gas_pressure, mechanical_pressure, resolved_kinetic_energy,
    thermal_energy, elastic_energy, entropy_change, energy_error,
    rounding_mode, mxcsr, binary64_supported, x86_supported,
    // Append-only producer fields; existing serialized IDs remain stable.
    operand_left, operand_right, arithmetic_result, completed_time, constitutive_charge,
    constitutive_error_code, temperature_minimum, temperature_maximum, molar_mass,
    heat_capacity, energy_offset, reference_density, bulk_modulus, density_ratio_limit,
    reference_position, position, jacobian, species_mass, partial_volume,
    work_pressure, work_stiffness, balanced_energy_change, represented_energy_change,
    requested_volume_change, represented_volume_change, incidence_volume_change,
    free_residual, linear_residual, caloric_reference, mechanical_variance,
    entropy_cache_difference, old_kinetic_energy, new_kinetic_energy,
    basis_index, node_indices,
    thermodynamic_reading,
    residual_ratio, backtracking_fraction,
    retained_position_low, retained_momentum_low, retained_internal_energy_low, retained_time_low,
    count
};

// Classification uses integer bits, including under FTZ/DAZ. A decimal is only
// a readable supplement to these bits, never the authoritative failure value.
struct CanonicalDiagnosticNumber {
    enum Flag : std::uint8_t { present = 1, finite = 2, negative = 4, zero = 8, subnormal = 16 };
    std::uint64_t bits = 0;
    std::uint8_t flags = 0;
    [[nodiscard]] static CanonicalDiagnosticNumber capture(double value) noexcept;
    [[nodiscard]] bool has(Flag flag) const noexcept { return (flags & flag) != 0; }
};
struct CanonicalWitnessScalar {
    CanonicalWitnessField field = CanonicalWitnessField::timestep;
    std::uint32_t index = canonical_absent_index;
    CanonicalDiagnosticNumber number;
};
struct CanonicalWitnessRow {
    CanonicalWitnessField field = CanonicalWitnessField::matrix_coefficients;
    std::uint32_t index = canonical_absent_index;
    std::size_t length = 0;
    std::array<CanonicalDiagnosticNumber, canonical_witness_row_length> numbers{};
};
struct CanonicalWitnessContext {
    std::uint32_t node = canonical_absent_index, owner = canonical_absent_index;
    std::uint32_t newton_updates = 0, backtracks = 0, factorizations = 0;
    std::uint64_t top_level_queries = 0, constitutive_charges = 0, oracle_iterations = 0;
    std::uint64_t predicate_sequence = 0; // Producer's operation-local sequence; zero means unavailable.
};

class CanonicalFailureWitness {
public:
    // Reset is an explicit caller action between operations. A producer must
    // check every returned bool and throw if capture cannot continue.
    void reset() noexcept;
    [[nodiscard]] bool begin_stage(CanonicalStage stage) noexcept;
    [[nodiscard]] bool set_identity(std::string_view printable_model_identity, std::uint64_t completed_step) noexcept;
    [[nodiscard]] bool set_context(const CanonicalWitnessContext& context) noexcept;
    [[nodiscard]] bool add_scalar(CanonicalWitnessField field, double value,
        std::uint32_t index = canonical_absent_index) noexcept;
    [[nodiscard]] bool add_row(CanonicalWitnessField field, std::span<const double> values,
        std::uint32_t index = canonical_absent_index) noexcept;
    // The first seal wins. Bad/capacity-exceeding diagnostic input also seals
    // explicitly, with capture_valid()==false, rather than truncating evidence.
    void seal(CanonicalFatalCode code, std::string_view printable_reason) noexcept;
    // Producer-side capture/association failure, distinct from physical rejection.
    void reject_capture(std::string_view printable_reason) noexcept;

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool capture_valid() const noexcept { return capture_valid_; }
    [[nodiscard]] CanonicalFatalCode code() const noexcept { return code_; }
    [[nodiscard]] CanonicalStage stage() const noexcept { return stage_; }
    [[nodiscard]] std::string_view reason() const noexcept { return reason_.data(); }
    [[nodiscard]] std::string_view model_identity() const noexcept { return identity_.data(); }
    [[nodiscard]] std::uint64_t completed_step() const noexcept { return completed_step_; }
    [[nodiscard]] const CanonicalWitnessContext& context() const noexcept { return context_; }
    [[nodiscard]] std::span<const CanonicalWitnessScalar> scalars() const noexcept { return {scalars_.data(), scalar_count_}; }
    [[nodiscard]] std::span<const CanonicalWitnessRow> rows() const noexcept { return {rows_.data(), row_count_}; }
    [[nodiscard]] std::size_t numeric_slots() const noexcept;
private:
    void invalid_capture(std::string_view reason) noexcept;
    bool failed_ = false, capture_valid_ = true;
    CanonicalFatalCode code_ = CanonicalFatalCode::unsupported_configuration;
    CanonicalStage stage_ = CanonicalStage::none;
    std::array<char, 257> reason_{}, identity_{};
    std::uint64_t completed_step_ = 0;
    CanonicalWitnessContext context_;
    std::size_t scalar_count_ = 0, row_count_ = 0;
    std::array<CanonicalWitnessScalar, canonical_witness_scalar_capacity> scalars_{};
    std::array<CanonicalWitnessRow, canonical_witness_row_capacity> rows_{};
};

[[noreturn]] void canonical_fail(CanonicalFailureWitness& witness, CanonicalFatalCode code, std::string_view reason);

// Direct std::exception ancestry deliberately bypasses legacy runtime_error
// catches which select a smaller timestep. Its text needs no dynamic string.
class CanonicalFailure final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override { return reason_.data(); }
    [[nodiscard]] CanonicalFatalCode code() const noexcept { return code_; }
    [[nodiscard]] CanonicalStage stage() const noexcept { return stage_; }
private:
    explicit CanonicalFailure(const CanonicalFailureWitness& witness) noexcept;
    friend void canonical_fail(CanonicalFailureWitness&, CanonicalFatalCode, std::string_view);
    CanonicalFatalCode code_;
    CanonicalStage stage_;
    std::array<char, 257> reason_{};
};
enum class CanonicalEncodingResult { success, buffer_too_small, invalid_output, encoding_failure };
struct CanonicalEncodingStatus {
    CanonicalEncodingResult result = CanonicalEncodingResult::success;
    std::size_t written = 0;
};
// Pure bounded encoding; no allocation, I/O, EOS query, mutation or callback.
// Finite decimals use integer arithmetic on binary64 bits, independent of FENV.
// On failure written==0; discard any partial output bytes. Output overlapping
// the witness is rejected before writing, preserving the sealed evidence.
[[nodiscard]] CanonicalEncodingStatus encode_canonical_witness_json(const CanonicalFailureWitness& witness,
    std::span<char> output) noexcept;

struct CanonicalArithmeticEnvironment {
    int rounding_mode = 0;
    std::uint32_t mxcsr = 0;
    bool binary64_supported = false, x86_supported = false;
    bool nearest = false, ftz_disabled = false, daz_disabled = false, exceptions_masked = false;
    [[nodiscard]] bool supported() const noexcept;
};
[[nodiscard]] CanonicalArithmeticEnvironment read_canonical_arithmetic_environment() noexcept;
// Records the observed environment before rejecting. It never sets a control
// register, rounds an input, or treats a flushed trace as absent.
void require_canonical_arithmetic_environment(CanonicalFailureWitness& witness);
}
