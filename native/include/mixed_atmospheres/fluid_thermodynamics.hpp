#pragma once
#include "mixture.hpp"
#include "liquid_reference.hpp"
#include <span>

namespace ma {
enum class FluidFatalCode { unsupported_topology, unsupported_configuration, numerical_precision };
class FluidFatalError final : public std::runtime_error {
public:
    FluidFatalError(FluidFatalCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] FluidFatalCode code() const noexcept { return code_; }
private:
    FluidFatalCode code_;
};
enum class LiquidFillRoundoffPolicy { strict_eos, balanced_local_roundoff };
struct LiquidFillResolution {
    bool gasless = false, within_roundoff = false;
    double nominal_minus_occupied_volume_m3 = 0, volume_roundoff_bound_m3 = 0;
};

struct FluidThermodynamicInitialization {
    MixtureInventory mixture;
    experimental::ReferenceEquilibriumState state;
    double caloric_reference_and_thermal_energy_j = 0;
    double initialized_elastic_energy_j = 0;
};

// Immutable registry/profile bridge used by MacClosedWorld and the opt-in
// experimental FluidWorld geometry mode. Inventory U includes elastic energy. Legacy inventory caloric
// temperature and nominal condensed-volume helpers are not this mode's EOS.
class FluidThermodynamics {
public:
    FluidThermodynamics(const SpeciesRegistry& registry, std::string profile_version,
                        std::vector<experimental::ReferenceMaterial> materials);
    void validate_registry(const SpeciesRegistry& registry) const;
    void validate_inventory(const MixtureInventory& mixture) const;
    [[nodiscard]] std::size_t registry_size() const noexcept { return registry_.size(); }
    [[nodiscard]] const experimental::LiquidThermodynamicReference& reference() const noexcept { return reference_; }
    [[nodiscard]] experimental::ReferenceConservedCell to_reference(const MixtureInventory& mixture,
                                                                   double occupied_volume_m3) const;
    [[nodiscard]] MixtureInventory from_reference(const experimental::ReferenceConservedCell& cell) const;
    [[nodiscard]] experimental::ReferenceEquilibriumState evaluate(const MixtureInventory& mixture,
                                                                  double occupied_volume_m3) const;
    // Positive -dV/dp at fixed entropy, taking the explicitly compressive side
    // at a zero-pressure liquid fill. Not a two-sided derivative at that kink.
    [[nodiscard]] double compression_compliance(const experimental::ReferenceIsentrope& reference) const;
    [[nodiscard]] LiquidFillResolution liquid_fill_resolution(const experimental::ReferenceConservedCell& cell) const;
    [[nodiscard]] FluidThermodynamicInitialization initialize_at_temperature(std::span<const double> masses_kg,
        double temperature_k, double occupied_volume_m3) const;
private:
    SpeciesRegistry registry_;
    experimental::LiquidThermodynamicReference reference_;
    std::vector<std::size_t> profile_to_registry_, registry_to_profile_;
};
}
