#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Reference thermodynamics, tested standalone and used through FluidThermodynamics
// by MacClosedWorld and the experimental FluidWorld bridge. GasWorld and the C
// ABI do not select it.
// All energies below exclude bulk kinetic energy.
namespace ma::experimental {
inline constexpr double reference_gas_constant = 8.31446261815324;
inline constexpr double entropy_reference_temperature_k = 298.15;
inline constexpr const char* liquid_reference_eos_version = "ma-zero-expansion-elastic-liquid-v1";

enum class ReferencePhase { ideal_gas, elastic_liquid };
enum class ReferenceErrorCode {
    invalid_model, invalid_inventory, temperature_outside_model,
    compression_outside_model, insufficient_thermal_energy, root_not_converged, numerical_range,
    unsupported_derivative, incompatible_entropy_reference
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

// An immutable, validated entropy reference, bound to the complete model
// metadata. Relative thermodynamic changes retain precision that would be lost
// by subtracting two large absolute entropy values or formation energies.
class ReferenceIsentrope {
public:
    [[nodiscard]] const ReferenceConservedCell& reference_cell() const noexcept { return cell_; }
    [[nodiscard]] const ReferenceEquilibriumState& reference_state() const noexcept { return state_; }
    [[nodiscard]] const std::string& model_metadata() const noexcept { return metadata_; }
private:
    friend class LiquidThermodynamicReference;
    ReferenceIsentrope(std::string metadata, ReferenceConservedCell cell, ReferenceEquilibriumState state);
    std::string metadata_;
    ReferenceConservedCell cell_;
    ReferenceEquilibriumState state_;
};

enum class ReferenceIsentropicBranch { vacuum, gas, compressed_liquid, liquid_with_void, zero_pressure_fill };

struct ReferenceIsentropicResponse {
    ReferenceConservedCell endpoint;
    ReferenceEquilibriumState state; // endpoint pressure, distinct from work pressure
    double volume_change_m3 = 0;
    // Explicit balanced change, retained even if adding it to a much larger
    // stored absolute U rounds away the increment. No caller state is mutated.
    double internal_energy_change_j = 0;
    double work_pressure_pa = 0; // -deltaU/deltaV, continuous limit at deltaV=0
    double represented_volume_change_m3 = 0;
    double represented_internal_energy_change_j = 0;
    // Compare represented increments with the mathematical changes using the
    // same 32-epsilon relative policy. A false flag explicitly reports that a
    // rounded endpoint is NOT an exact representation of the pressure work.
    bool endpoint_volume_resolves_change = true;
    bool endpoint_energy_resolves_change = true;
    ReferenceIsentropicBranch branch = ReferenceIsentropicBranch::vacuum;
};

struct ReferenceIsentropicDerivatives {
    double pressure_volume_derivative_pa_per_m3 = 0;
    double temperature_volume_derivative_k_per_m3 = 0;
    double energy_volume_derivative_pa = 0; // dU/dV = -p at constant entropy
    double work_pressure_volume_derivative_pa_per_m3 = 0;
};

struct ReferenceIsentropicWork {
    ReferenceIsentropicResponse response;
    double work_pressure_volume_derivative_pa_per_m3 = 0;
    // Continuous work-pressure change from the anchor, evaluated without
    // subtracting two large rounded pressures. Reconstructing the work pressure
    // must agree with the absolute secant within its existing reference bound.
    // Absent if that check fails or the increment cannot be stored accurately;
    // the ordinary query remains valid. Zero is available at zero change and
    // on a pressure-free branch.
    std::optional<double> work_pressure_change_pa;
};

class LiquidThermodynamicReference {
public:
    LiquidThermodynamicReference(std::string profile_version, std::vector<ReferenceMaterial> materials);
    [[nodiscard]] const std::vector<ReferenceMaterial>& materials() const noexcept { return materials_; }
    [[nodiscard]] const std::string& profile_version() const noexcept { return profile_version_; }
    // Immutable identity is serialized once at construction. Borrow from an
    // lvalue; a temporary returns an owned copy so its metadata cannot dangle.
    [[nodiscard]] const std::string& metadata() const & noexcept { return metadata_; }
    [[nodiscard]] std::string metadata() const && { return metadata_; }
    [[nodiscard]] LiquidReferenceResponse liquid_response(std::size_t material, double pressure_pa) const;
    [[nodiscard]] ReferenceEquilibriumState evaluate(const ReferenceConservedCell& cell,
        ReferenceRootOptions options = {}) const;
    // Strong output guarantee: an invalid state or failed root leaves both the
    // conserved input and the previous output untouched.
    void evaluate_into(const ReferenceConservedCell& cell, ReferenceEquilibriumState& output,
        ReferenceRootOptions options = {}) const;
    [[nodiscard]] ReferenceTemperatureInitialization initialize_at_temperature(std::span<const double> masses_kg,
        double temperature_k, double volume_m3, ReferenceRootOptions options = {}) const;
    [[nodiscard]] ReferenceIsentrope make_isentrope(const ReferenceConservedCell& cell,
        ReferenceRootOptions options = {}) const;
    [[nodiscard]] ReferenceIsentropicResponse isentropic_at_volume(const ReferenceIsentrope& entropy_reference,
        double volume_m3, ReferenceRootOptions options = {}) const;
    // This overload preserves deltaV below the ULP of the reference volume.
    // The rounded endpoint.volume_m3 may then equal the reference volume;
    // volume_change_m3 and internal_energy_change_j retain the actual change.
    [[nodiscard]] ReferenceIsentropicResponse isentropic_response(const ReferenceIsentrope& entropy_reference,
        double volume_change_m3, ReferenceRootOptions options = {}) const;
    void isentropic_response_into(const ReferenceIsentrope& entropy_reference, double volume_change_m3,
        ReferenceIsentropicResponse& output, ReferenceRootOptions options = {}) const;
    // A zero-pressure liquid fill has a pressure-stiffness kink. Vacuum has
    // no temperature derivative. These queries fail with a typed error there,
    // while state and pressure-work queries remain valid.
    [[nodiscard]] ReferenceIsentropicDerivatives isentropic_derivatives(const ReferenceIsentrope& entropy_reference,
        double volume_change_m3, ReferenceRootOptions options = {}) const;
    // This derivative still exists when a nonzero step ends at the fill kink.
    // At a zero-length step starting at that kink it is two-sided undefined.
    [[nodiscard]] double isentropic_work_pressure_derivative(const ReferenceIsentrope& entropy_reference,
        double volume_change_m3, ReferenceRootOptions options = {}) const;
    // The same response and tangent, evaluated from one root. This retains
    // the standalone queries' domains, rounding and liquid-fill kink errors.
    [[nodiscard]] ReferenceIsentropicWork isentropic_work(const ReferenceIsentrope& entropy_reference,
        double volume_change_m3, ReferenceRootOptions options = {}) const;
private:
    std::string profile_version_;
    std::vector<ReferenceMaterial> materials_;
    std::string metadata_;
    [[nodiscard]] std::string build_metadata() const;
};
}
