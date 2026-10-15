#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma {
namespace {
bool compatible(double a, double b) {
    return std::abs(a - b) <= 32 * std::numeric_limits<double>::epsilon() * std::max(std::abs(a), std::abs(b));
}
}

FluidThermodynamics::FluidThermodynamics(const SpeciesRegistry& registry, std::string version,
    std::vector<experimental::ReferenceMaterial> materials)
    : registry_(registry), reference_(std::move(version), std::move(materials)),
      registry_to_profile_(registry.size(), registry.size()) {
    for (std::size_t i = 0; i < reference_.materials().size(); ++i) {
        const auto& material = reference_.materials()[i];
        const auto index = registry.index(material.id);
        const auto& species = registry[index];
        const bool gas = material.phase == experimental::ReferencePhase::ideal_gas;
        if (species.phase != (gas ? Phase::gas : Phase::liquid) ||
            !compatible(species.molar_mass, material.molar_mass_kg_per_mol) ||
            !compatible(species.cv(), material.cv_j_per_kg_k) ||
            !compatible(species.internal_energy(0), material.energy_offset_j_per_kg))
            throw std::invalid_argument("Fluid thermodynamic profile changes registry caloric identity: " + material.id);
        profile_to_registry_.push_back(index);
        registry_to_profile_[index] = i;
    }
}

void FluidThermodynamics::validate_registry(const SpeciesRegistry& registry) const {
    if (registry.size() != registry_.size()) throw std::invalid_argument("Fluid thermodynamic registry size differs");
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const auto& a = registry[i]; const auto& b = registry_[i];
        if (a.id != b.id || a.phase != b.phase || a.molar_mass != b.molar_mass ||
            a.cv() != b.cv() || a.internal_energy(0) != b.internal_energy(0))
            throw std::invalid_argument("Fluid thermodynamic registry identity differs");
    }
}

void FluidThermodynamics::validate_inventory(const MixtureInventory& mixture) const {
    if (mixture.masses.size() != registry_.size()) throw std::invalid_argument("Fluid thermodynamic inventory size differs");
    require_finite(mixture.energy, "fluid conserved internal energy");
    long double total = 0;
    for (std::size_t i = 0; i < mixture.masses.size(); ++i) {
        const auto mass = mixture.masses[i];
        require_finite(mass, "fluid constituent mass");
        if (mass < 0) throw std::invalid_argument("Negative fluid constituent mass");
        if (mass > 0 && registry_to_profile_[i] == registry_.size())
            throw std::invalid_argument("Fluid constituent has no supported EOS profile: " + registry_[i].id);
        total += mass;
    }
    if (!std::isfinite(static_cast<double>(total))) throw std::invalid_argument("Fluid mass overflow");
    if (total == 0 && mixture.energy != 0) throw std::invalid_argument("Vacuum cannot contain internal energy");
}

experimental::ReferenceConservedCell FluidThermodynamics::to_reference(const MixtureInventory& mixture,
    double occupied_volume) const {
    validate_inventory(mixture);
    experimental::ReferenceConservedCell result;
    result.masses_kg.reserve(profile_to_registry_.size());
    for (const auto index : profile_to_registry_) result.masses_kg.push_back(mixture.masses[index]);
    result.internal_energy_j = mixture.energy; result.volume_m3 = occupied_volume;
    return result;
}

MixtureInventory FluidThermodynamics::from_reference(const experimental::ReferenceConservedCell& cell) const {
    if (cell.masses_kg.size() != profile_to_registry_.size()) throw std::invalid_argument("Fluid reference size differs");
    MixtureInventory result(registry_.size());
    for (std::size_t i = 0; i < profile_to_registry_.size(); ++i) result.masses[profile_to_registry_[i]] = cell.masses_kg[i];
    result.energy = cell.internal_energy_j;
    validate_inventory(result);
    return result;
}

experimental::ReferenceEquilibriumState FluidThermodynamics::evaluate(const MixtureInventory& mixture,
    double occupied_volume) const {
    return reference_.evaluate(to_reference(mixture, occupied_volume));
}

double FluidThermodynamics::compression_compliance(const experimental::ReferenceIsentrope& anchor) const {
    if (anchor.model_metadata() != reference_.metadata())
        throw experimental::ReferenceError(experimental::ReferenceErrorCode::incompatible_entropy_reference,
            "Compression tangent belongs to another thermodynamic profile");
    const auto& state = anchor.reference_state();
    if (state.vacuum_volume_m3 > 32 * std::numeric_limits<double>::epsilon() * anchor.reference_cell().volume_m3)
        throw std::invalid_argument("Compression tangent requires an occupied EOS state without internal void");
    long double compliance = 0, gas_heat = 0;
    for (std::size_t i = 0; i < profile_to_registry_.size(); ++i) {
        const auto& material = reference_.materials()[i];
        if (material.phase == experimental::ReferencePhase::elastic_liquid)
            compliance += static_cast<long double>(state.partial_volumes_m3[i]) /
                (material.reference_bulk_modulus_pa + state.mechanical_pressure_pa);
        else gas_heat += static_cast<long double>(anchor.reference_cell().masses_kg[i]) /
            material.molar_mass_kg_per_mol * experimental::reference_gas_constant;
    }
    if (state.has_gas) {
        const long double capacity = state.intrinsic_heat_capacity_j_per_k;
        compliance += capacity / (capacity + gas_heat) * state.gas_volume_m3 / state.mechanical_pressure_pa;
    }
    const auto result = static_cast<double>(compliance);
    if (!std::isfinite(result) || result <= 0)
        throw experimental::ReferenceError(experimental::ReferenceErrorCode::numerical_range,
            "Occupied isentropic compression compliance is unrepresentable");
    return result;
}

LiquidFillResolution FluidThermodynamics::liquid_fill_resolution(const experimental::ReferenceConservedCell& cell) const {
    if (cell.masses_kg.size() != reference_.materials().size() || !std::isfinite(cell.volume_m3) || cell.volume_m3 <= 0)
        throw std::invalid_argument("Invalid liquid fill resolution query");
    double nominal = 0, correction = 0;
    bool material_present = false, gas_present = false;
    LiquidFillResolution result;
    for (std::size_t i = 0; i < cell.masses_kg.size(); ++i) {
        const double mass = cell.masses_kg[i]; require_finite(mass, "fill resolution mass");
        if (mass < 0) throw std::invalid_argument("Negative fill resolution mass");
        if (mass == 0) continue;
        material_present = true;
        const auto& material = reference_.materials()[i];
        // Exact positive-mass test: even an arbitrarily small actual gas
        // constituent disables this gasless representability approximation.
        if (material.phase == experimental::ReferencePhase::ideal_gas) { gas_present = true; continue; }
        const double part = mass / material.zero_pressure_density_kg_per_m3;
        if (!std::isfinite(part) || part <= 0)
            throw experimental::ReferenceError(experimental::ReferenceErrorCode::numerical_range,
                "Nominal liquid partial volume is unrepresentable");
        const double next = nominal + part;
        correction += std::abs(nominal) >= std::abs(part) ? (nominal - next) + part : (part - next) + nominal;
        nominal = next;
    }
    if (!std::isfinite(nominal) || !std::isfinite(correction))
        throw experimental::ReferenceError(experimental::ReferenceErrorCode::numerical_range,
            "Nominal liquid total volume is unrepresentable");
    result.gasless = material_present && !gas_present;
    result.nominal_minus_occupied_volume_m3 = (nominal - cell.volume_m3) + correction;
    result.volume_roundoff_bound_m3 = 32 * std::numeric_limits<double>::epsilon() * cell.volume_m3;
    result.within_roundoff = result.gasless && std::abs(result.nominal_minus_occupied_volume_m3) <= result.volume_roundoff_bound_m3;
    return result;
}

FluidThermodynamicInitialization FluidThermodynamics::initialize_at_temperature(std::span<const double> masses,
    double temperature, double occupied_volume) const {
    MixtureInventory requested(registry_.size());
    if (masses.size() != requested.masses.size()) throw std::invalid_argument("Fluid initialization mass vector size differs");
    requested.masses.assign(masses.begin(), masses.end());
    const auto reference_cell = to_reference(requested, occupied_volume);
    auto initialized = reference_.initialize_at_temperature(reference_cell.masses_kg, temperature, occupied_volume);
    return {from_reference(initialized.cell), std::move(initialized.state),
            initialized.caloric_reference_and_thermal_energy_j, initialized.initialized_elastic_energy_j};
}
}
