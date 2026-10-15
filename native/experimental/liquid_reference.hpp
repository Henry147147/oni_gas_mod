#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Standalone thermodynamic prototype. No production inventory, world or ABI
// includes this header. All energies below exclude bulk kinetic energy.
namespace ma::experimental {
inline constexpr double reference_gas_constant = 8.31446261815324;
inline constexpr double entropy_reference_temperature_k = 298.15;
inline constexpr const char* liquid_reference_eos_version = "ma-zero-expansion-elastic-liquid-v1";

enum class ReferencePhase { ideal_gas, elastic_liquid };
enum class ReferenceErrorCode {
    invalid_model, invalid_inventory, temperature_outside_model,
    compression_outside_model, insufficient_thermal_energy, root_not_converged, numerical_range
};

class ReferenceError final : public std::runtime_error {
public:
    ReferenceError(ReferenceErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] ReferenceErrorCode code() const noexcept { return code_; }
private:
    ReferenceErrorCode code_;
};

struct ReferenceMaterial {
    std::string id;
    ReferencePhase phase = ReferencePhase::ideal_gas;
    double molar_mass_kg_per_mol = 0;
    double cv_j_per_kg_k = 0;
    double energy_offset_j_per_kg = 0; // e(T,p=0) = offset + Cv*T
    double minimum_temperature_k = 0;
    double maximum_temperature_k = 0;
    double zero_pressure_density_kg_per_m3 = 0;
    double reference_bulk_modulus_pa = 0;
    double maximum_density_ratio = 1; // liquids: 1 <= rho/rho0 <= this bound
    std::string provenance;
};

struct ReferenceConservedCell {
    std::vector<double> masses_kg;
    double internal_energy_j = 0; // caloric reference + thermal + elastic
    double volume_m3 = 1;
};

struct ReferenceRootOptions {
    double relative_pressure_tolerance = 2e-13;
    double relative_volume_tolerance = 2e-12;
    std::size_t maximum_iterations = 256;
};

struct LiquidReferenceResponse {
    double specific_volume_m3_per_kg = 0;
    double specific_elastic_energy_j_per_kg = 0;
    double pressure_pa = 0;
    double isothermal_bulk_modulus_pa = 0;
    double sound_speed_squared_m2_per_s2 = 0;
};

struct ReferenceEquilibriumState {
    double temperature_k = 0;
    double mechanical_pressure_pa = 0;
    double gas_pressure_pa = 0; // zero if there is no actual gas
    double gas_volume_m3 = 0;
    double liquid_volume_m3 = 0;
    double vacuum_volume_m3 = 0; // state closure only; no interface dynamics
    double reference_energy_j = 0;
    double thermal_energy_j = 0;
    double elastic_energy_j = 0;
    double entropy_j_per_k = 0;
    double helmholtz_energy_j = 0;
    double intrinsic_heat_capacity_j_per_k = 0;
    double equilibrium_heat_capacity_at_cell_volume_j_per_k = 0;
    double volume_residual_m3 = 0;
    double energy_residual_j = 0;
    // Ideal-gas entries are thermodynamic partial volumes, not separate
    // spatial phases. Liquid entries are their actual compressed volumes.
    std::vector<double> partial_volumes_m3;
    std::vector<double> specific_internal_energies_j_per_kg;
    std::size_t pressure_iterations = 0;
    bool has_gas = false;
    bool has_vacuum = false;
};

struct ReferenceTemperatureInitialization {
    ReferenceConservedCell cell;
    ReferenceEquilibriumState state;
    double caloric_reference_and_thermal_energy_j = 0;
    // Explicit conversion ledger: native temperature-preserving import would
    // need this additional stored elastic energy. It is never added by evaluate.
    double initialized_elastic_energy_j = 0;
};

class LiquidThermodynamicReference {
public:
    LiquidThermodynamicReference(std::string profile_version, std::vector<ReferenceMaterial> materials);
    [[nodiscard]] const std::vector<ReferenceMaterial>& materials() const noexcept { return materials_; }
    [[nodiscard]] const std::string& profile_version() const noexcept { return profile_version_; }
    [[nodiscard]] std::string metadata() const;
    [[nodiscard]] LiquidReferenceResponse liquid_response(std::size_t material, double pressure_pa) const;
    [[nodiscard]] ReferenceEquilibriumState evaluate(const ReferenceConservedCell& cell,
        ReferenceRootOptions options = {}) const;
    // Strong output guarantee: an invalid state or failed root leaves both the
    // conserved input and the previous output untouched.
    void evaluate_into(const ReferenceConservedCell& cell, ReferenceEquilibriumState& output,
        ReferenceRootOptions options = {}) const;
    [[nodiscard]] ReferenceTemperatureInitialization initialize_at_temperature(std::span<const double> masses_kg,
        double temperature_k, double volume_m3, ReferenceRootOptions options = {}) const;
private:
    std::string profile_version_;
    std::vector<ReferenceMaterial> materials_;
};
}
