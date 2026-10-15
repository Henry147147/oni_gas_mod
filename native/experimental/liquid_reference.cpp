#include "liquid_reference.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <utility>

namespace ma::experimental {
namespace {
using Real = long double;
constexpr Real R = reference_gas_constant;
[[noreturn]] void fail(ReferenceErrorCode code, const char* message) { throw ReferenceError(code, message); }
void check(bool condition, ReferenceErrorCode code, const char* message) { if (!condition) fail(code, message); }
bool positive(double value) { return std::isfinite(value) && value > 0; }
double checked(Real value) {
    const double result = static_cast<double>(value);
    check(std::isfinite(result), ReferenceErrorCode::numerical_range, "Thermodynamic result exceeds finite double range");
    check(value == 0 || result != 0, ReferenceErrorCode::numerical_range, "Positive thermodynamic quantity underflowed double range");
    return result;
}
struct Sum {
    Real value = 0, correction = 0;
    void add(Real item) {
        const Real next = value + item;
        correction += std::abs(value) >= std::abs(item) ? (value - next) + item : (item - next) + value;
        value = next;
    }
    Real get() const { return value + correction; }
};
Real boundary_roundoff(Real value, Real bound) {
    return 32 * std::numeric_limits<double>::epsilon() * std::max(std::abs(value), std::abs(bound));
}
Real compression_ratio(Real pressure, Real bulk) {
    const Real q = pressure / bulk;
    check(pressure == 0 || q > 0, ReferenceErrorCode::numerical_range, "Positive liquid compression underflowed");
    return q;
}
// scale*[log(1+q) - q/(1+q)]. Factor the small-q series before
// multiplying: q*q may underflow even when the scaled energy is representable.
Real elastic_energy(Real scale, Real q) {
    if (q == 0) return 0;
    Real result;
    if (q < 1e-3L) {
        Real power = q, factor = .5L;
        for (int n = 3; n <= 12; ++n) {
            factor += ((n % 2) == 0 ? 1 : -1) * (static_cast<Real>(n - 1) / n) * power;
            power *= q;
        }
        result = (scale * q) * (q * factor);
    } else result = scale * (std::log1p(q) - q / (1 + q));
    check(std::isfinite(result) && result > 0, ReferenceErrorCode::numerical_range,
        "Positive liquid elastic energy is outside the numerical range");
    return result;
}
struct Statistics {
    Real mass = 0, cv = 0, offset = 0, gas_moles = 0, nominal_liquid_volume = 0;
    Real minimum_temperature = 0, maximum_temperature = std::numeric_limits<Real>::max();
    Real maximum_pressure = std::numeric_limits<Real>::max();
    bool has_liquid = false;
};
Statistics statistics(const std::vector<ReferenceMaterial>& definitions, std::span<const double> masses, double volume) {
    check(masses.size() == definitions.size() && positive(volume), ReferenceErrorCode::invalid_inventory, "Mass vector or cell volume is invalid");
    Statistics result; Sum mass, cv, offset, moles, nominal;
    for (std::size_t i = 0; i < masses.size(); ++i) {
        const Real amount = masses[i]; const auto& material = definitions[i];
        check(std::isfinite(masses[i]) && amount >= 0, ReferenceErrorCode::invalid_inventory, "Species mass must be finite and nonnegative");
        if (amount == 0) continue;
        mass.add(amount); cv.add(amount * material.cv_j_per_kg_k); offset.add(amount * material.energy_offset_j_per_kg);
        result.minimum_temperature = std::max(result.minimum_temperature, static_cast<Real>(material.minimum_temperature_k));
        result.maximum_temperature = std::min(result.maximum_temperature, static_cast<Real>(material.maximum_temperature_k));
        if (material.phase == ReferencePhase::ideal_gas) moles.add(amount / material.molar_mass_kg_per_mol);
        else {
            result.has_liquid = true;
            nominal.add(amount / material.zero_pressure_density_kg_per_m3);
            result.maximum_pressure = std::min(result.maximum_pressure,
                static_cast<Real>(material.reference_bulk_modulus_pa) * (material.maximum_density_ratio - 1));
        }
    }
    result.mass = mass.get(); result.cv = cv.get(); result.offset = offset.get();
    result.gas_moles = moles.get(); result.nominal_liquid_volume = nominal.get();
    checked(result.mass); checked(result.cv); checked(result.offset); checked(result.gas_moles); checked(result.nominal_liquid_volume);
    if (result.mass > 0) check(result.minimum_temperature <= result.maximum_temperature && result.cv > 0,
        ReferenceErrorCode::temperature_outside_model, "Present materials have no common temperature range");
    return result;
}
struct Compression {
    Real energy = 0, contracted_volume = 0, compliance = 0;
};
Compression compression(const std::vector<ReferenceMaterial>& definitions, std::span<const double> masses, Real pressure) {
    Sum energy, contraction, compliance;
    for (std::size_t i = 0; i < masses.size(); ++i) {
        const auto& material = definitions[i];
        if (masses[i] == 0 || material.phase != ReferencePhase::elastic_liquid) continue;
        const Real nominal = static_cast<Real>(masses[i]) / material.zero_pressure_density_kg_per_m3;
        const Real bulk = material.reference_bulk_modulus_pa, q = compression_ratio(pressure, bulk);
        energy.add(elastic_energy(nominal * bulk, q));
        contraction.add(nominal * q / (1 + q));
        compliance.add(nominal / bulk / ((1 + q) * (1 + q)));
    }
    return {energy.get(), contraction.get(), compliance.get()};
}
void temperature_in_range(Real temperature, const Statistics& stats) {
    check(std::isfinite(temperature) && temperature > 0, ReferenceErrorCode::insufficient_thermal_energy, "Reconstructed temperature is not positive");
    // This is an acceptance tolerance for arithmetic at a declared boundary,
    // not a temperature floor: the returned temperature is never changed.
    check(stats.minimum_temperature - temperature <= boundary_roundoff(temperature, stats.minimum_temperature) &&
        temperature - stats.maximum_temperature <= boundary_roundoff(temperature, stats.maximum_temperature),
        ReferenceErrorCode::temperature_outside_model, "Reconstructed temperature is outside the explicit material range");
}
void validate_options(const ReferenceRootOptions& options) {
    check(positive(options.relative_pressure_tolerance) && options.relative_pressure_tolerance < 1 &&
        positive(options.relative_volume_tolerance) && options.relative_volume_tolerance < 1 && options.maximum_iterations > 0,
        ReferenceErrorCode::invalid_model, "Invalid reference root options");
}
template<class Function> Real pressure_root(Function residual, Real high, const ReferenceRootOptions& options, std::size_t& iterations) {
    check(std::isfinite(high) && high > 0, ReferenceErrorCode::compression_outside_model, "No positive supported pressure interval");
    const Real at_high = residual(high);
    check(std::isfinite(at_high), ReferenceErrorCode::numerical_range, "Pressure residual overflowed");
    check(at_high <= 0, ReferenceErrorCode::compression_outside_model, "Cell cannot fit inside its compression and thermal domain");
    if (at_high == 0) return high;
    Real low = 0;
    for (std::size_t n = 0; n < options.maximum_iterations; ++n) {
        ++iterations;
        const Real middle = low + (high - low) / 2;
        check(middle > 0, ReferenceErrorCode::numerical_range, "Pressure interval underflowed");
        const Real value = residual(middle);
        check(std::isfinite(value), ReferenceErrorCode::numerical_range, "Pressure residual overflowed");
        if (value == 0) return middle;
        if (value > 0) low = middle; else high = middle;
        if (high - low <= options.relative_pressure_tolerance * high || middle == low + (high - low) / 2)
            return low + (high - low) / 2;
    }
    fail(ReferenceErrorCode::root_not_converged, "Reference pressure root exceeded its explicit iteration budget");
}
Real energy_limited_pressure(const std::vector<ReferenceMaterial>& definitions, std::span<const double> masses,
    Real maximum, Real budget) {
    check(budget >= 0, ReferenceErrorCode::insufficient_thermal_energy, "No energy remains above the minimum temperature");
    if (compression(definitions, masses, maximum).energy <= budget) return maximum;
    Real low = 0, high = maximum;
    // Return the admissible side of the energy boundary. This bracket search
    // has a separate fixed budget and never introduces negative gas volume.
    for (int n = 0; n < 256; ++n) {
        const Real middle = low + (high - low) / 2;
        if (middle == low || middle == high) break;
        if (compression(definitions, masses, middle).energy <= budget) low = middle; else high = middle;
    }
    return low;
}
ReferenceEquilibriumState finish(const std::vector<ReferenceMaterial>& definitions, std::span<const double> masses,
    const Statistics& stats, Real volume, Real pressure, Real temperature, Real conserved_energy, std::size_t iterations,
    const ReferenceRootOptions& options) {
    ReferenceEquilibriumState result;
    result.partial_volumes_m3.resize(masses.size()); result.specific_internal_energies_j_per_kg.resize(masses.size());
    result.pressure_iterations = iterations;
    if (stats.mass == 0) { result.vacuum_volume_m3 = checked(volume); result.has_vacuum = true; return result; }
    temperature_in_range(temperature, stats);
    const auto elastic = compression(definitions, masses, pressure);
    const bool has_gas = stats.gas_moles > 0;
    const Real gas_volume = has_gas ? stats.gas_moles * R * temperature / pressure : 0;
    check(!has_gas || (pressure > 0 && gas_volume > 0), ReferenceErrorCode::numerical_range, "Actual gas lost its positive pressure or headspace");
    Sum liquid_volume, entropy, energy;
    for (std::size_t i = 0; i < masses.size(); ++i) {
        if (masses[i] == 0) continue;
        const auto& material = definitions[i]; const Real amount = masses[i];
        Real partial = 0, specific_elastic = 0;
        const Real specific_thermal = material.cv_j_per_kg_k * temperature;
        check(std::isfinite(specific_thermal) && specific_thermal > 0, ReferenceErrorCode::numerical_range,
            "Positive material thermal energy is outside the numerical range");
        entropy.add(amount * material.cv_j_per_kg_k * std::log(temperature / entropy_reference_temperature_k));
        if (material.phase == ReferencePhase::elastic_liquid) {
            const Real q = compression_ratio(pressure, material.reference_bulk_modulus_pa);
            partial = amount / material.zero_pressure_density_kg_per_m3 / (1 + q);
            specific_elastic = elastic_energy(material.reference_bulk_modulus_pa / static_cast<Real>(material.zero_pressure_density_kg_per_m3), q);
            liquid_volume.add(partial);
        } else {
            const Real moles = amount / material.molar_mass_kg_per_mol;
            partial = gas_volume * moles / stats.gas_moles;
            // Unit molar-volume reference is 1 m^3/mol; its arbitrary entropy
            // zero does not affect pressure, Cv or entropy differences.
            entropy.add(moles * R * std::log(gas_volume / moles));
        }
        Sum specific_sum; specific_sum.add(material.energy_offset_j_per_kg);
        specific_sum.add(specific_thermal); specific_sum.add(specific_elastic);
        result.partial_volumes_m3[i] = checked(partial);
        const double specific = checked(specific_sum.get());
        const Real sensible = specific_thermal + specific_elastic;
        const Real reconstructed_sensible = static_cast<Real>(specific) - material.energy_offset_j_per_kg;
        check(std::abs(reconstructed_sensible - sensible) <= boundary_roundoff(reconstructed_sensible, sensible),
            ReferenceErrorCode::numerical_range, "Material energy reference erases resolved thermal or elastic energy");
        result.specific_internal_energies_j_per_kg[i] = specific;
        energy.add(amount * specific);
    }
    const Real void_volume = !has_gas && pressure == 0 ? volume - stats.nominal_liquid_volume : 0;
    const Real residual = liquid_volume.get() + gas_volume + void_volume - volume;
    check(std::abs(residual) <= options.relative_volume_tolerance * volume,
        ReferenceErrorCode::root_not_converged, "Reference volume closure did not reach its declared tolerance");
    Sum thermodynamic_energy; thermodynamic_energy.add(stats.offset);
    thermodynamic_energy.add(stats.cv * temperature); thermodynamic_energy.add(elastic.energy);
    const Real thermodynamic_total = thermodynamic_energy.get();
    const Real energy_scale = std::max({std::abs(conserved_energy), std::abs(stats.offset),
        std::abs(stats.cv * temperature), std::abs(elastic.energy)});
    const Real energy_roundoff = 32 * std::numeric_limits<double>::epsilon() * energy_scale;
    check(std::abs(thermodynamic_total - conserved_energy) <= energy_roundoff &&
        std::abs(energy.get() - thermodynamic_total) <= energy_roundoff,
        ReferenceErrorCode::numerical_range, "Returned material energies do not close the conserved energy");
    Real effective_cv = stats.cv;
    if (has_gas && stats.has_liquid) {
        const Real compliance = elastic.compliance + gas_volume / pressure;
        effective_cv += pressure * elastic.compliance * gas_volume / (temperature * compliance);
    }
    result.temperature_k = checked(temperature); result.mechanical_pressure_pa = checked(pressure);
    result.gas_pressure_pa = has_gas ? result.mechanical_pressure_pa : 0;
    result.gas_volume_m3 = checked(gas_volume); result.liquid_volume_m3 = checked(liquid_volume.get());
    result.vacuum_volume_m3 = checked(void_volume);
    result.reference_energy_j = checked(stats.offset); result.thermal_energy_j = checked(stats.cv * temperature);
    result.elastic_energy_j = checked(elastic.energy); result.entropy_j_per_k = checked(entropy.get());
    result.helmholtz_energy_j = checked(thermodynamic_total - temperature * entropy.get());
    result.intrinsic_heat_capacity_j_per_k = checked(stats.cv);
    result.equilibrium_heat_capacity_at_cell_volume_j_per_k = checked(effective_cv);
    result.volume_residual_m3 = checked(residual); result.energy_residual_j = checked(energy.get() - conserved_energy);
    result.has_gas = has_gas; result.has_vacuum = void_volume > 0;
    return result;
}
}

LiquidThermodynamicReference::LiquidThermodynamicReference(std::string profile_version, std::vector<ReferenceMaterial> materials)
    : profile_version_(std::move(profile_version)), materials_(std::move(materials)) {
    check(!profile_version_.empty() && profile_version_.size() <= 256 && !materials_.empty(), ReferenceErrorCode::invalid_model, "Explicit profile version and materials are required");
    std::vector<std::string> ids;
    for (const auto& material : materials_) {
        check(!material.id.empty() && material.id.size() <= 256 && std::find(ids.begin(), ids.end(), material.id) == ids.end() &&
            !material.provenance.empty(), ReferenceErrorCode::invalid_model, "Materials need unique identities and explicit provenance");
        ids.push_back(material.id);
        check(positive(material.molar_mass_kg_per_mol) && positive(material.cv_j_per_kg_k) && std::isfinite(material.energy_offset_j_per_kg) &&
            positive(material.minimum_temperature_k) && positive(material.maximum_temperature_k) && material.maximum_temperature_k > material.minimum_temperature_k,
            ReferenceErrorCode::invalid_model, "Invalid caloric parameters or finite temperature range");
        if (material.phase == ReferencePhase::elastic_liquid) {
            check(positive(material.zero_pressure_density_kg_per_m3) && positive(material.reference_bulk_modulus_pa) &&
                std::isfinite(material.maximum_density_ratio) && material.maximum_density_ratio > 1 &&
                positive(material.reference_bulk_modulus_pa * (material.maximum_density_ratio - 1)),
                ReferenceErrorCode::invalid_model, "Invalid liquid reference stiffness, density or compression range");
        } else check(material.phase == ReferencePhase::ideal_gas && material.zero_pressure_density_kg_per_m3 == 0 &&
            material.reference_bulk_modulus_pa == 0 && material.maximum_density_ratio == 1,
            ReferenceErrorCode::invalid_model, "Ideal gases cannot carry liquid cold-curve parameters");
    }
}

std::string LiquidThermodynamicReference::metadata() const {
    std::ostringstream text; text.imbue(std::locale::classic()); text << std::setprecision(std::numeric_limits<double>::max_digits10) << std::scientific;
    text << liquid_reference_eos_version << '\n' << profile_version_.size() << ':' << profile_version_ << '\n';
    text << "R=" << reference_gas_constant << ";entropy_Tref=" << entropy_reference_temperature_k << ";zero_thermal_expansion;zero_tension;state_closure_only\n";
    for (const auto& material : materials_) text << material.id.size() << ':' << material.id << ';' << static_cast<int>(material.phase) << ';'
        << material.molar_mass_kg_per_mol << ';' << material.cv_j_per_kg_k << ';' << material.energy_offset_j_per_kg << ';'
        << material.minimum_temperature_k << ';' << material.maximum_temperature_k << ';' << material.zero_pressure_density_kg_per_m3 << ';'
        << material.reference_bulk_modulus_pa << ';' << material.maximum_density_ratio << ';' << material.provenance.size() << ':' << material.provenance << '\n';
    return text.str();
}

LiquidReferenceResponse LiquidThermodynamicReference::liquid_response(std::size_t index, double pressure) const {
    check(index < materials_.size() && materials_[index].phase == ReferencePhase::elastic_liquid && std::isfinite(pressure) && pressure >= 0,
        ReferenceErrorCode::invalid_inventory, "Liquid response requires a valid liquid and nonnegative finite pressure");
    const auto& material = materials_[index]; const Real q = compression_ratio(pressure, material.reference_bulk_modulus_pa);
    const Real maximum_q = material.maximum_density_ratio - 1;
    check(q - maximum_q <= boundary_roundoff(q, maximum_q), ReferenceErrorCode::compression_outside_model, "Liquid compression exceeds its declared ratio");
    const Real specific_volume = 1 / static_cast<Real>(material.zero_pressure_density_kg_per_m3) / (1 + q);
    return {checked(specific_volume), checked(elastic_energy(material.reference_bulk_modulus_pa / static_cast<Real>(material.zero_pressure_density_kg_per_m3), q)), pressure,
        checked(material.reference_bulk_modulus_pa + static_cast<Real>(pressure)),
        checked(material.reference_bulk_modulus_pa / static_cast<Real>(material.zero_pressure_density_kg_per_m3))};
}

ReferenceEquilibriumState LiquidThermodynamicReference::evaluate(const ReferenceConservedCell& cell, ReferenceRootOptions options) const {
    validate_options(options);
    const auto stats = statistics(materials_, cell.masses_kg, cell.volume_m3);
    check(std::isfinite(cell.internal_energy_j), ReferenceErrorCode::invalid_inventory, "Conserved internal energy must be finite");
    if (stats.mass == 0) {
        check(cell.internal_energy_j == 0, ReferenceErrorCode::invalid_inventory, "Vacuum cannot contain internal energy");
        return finish(materials_, cell.masses_kg, stats, cell.volume_m3, 0, 0, 0, 0, options);
    }
    const Real available = static_cast<Real>(cell.internal_energy_j) - stats.offset;
    check(available > 0, ReferenceErrorCode::insufficient_thermal_energy, "No positive thermal energy remains above the reference energy");
    const Real nominal_delta = stats.nominal_liquid_volume - cell.volume_m3;
    Real pressure = 0; std::size_t iterations = 0;
    if (!stats.has_liquid) pressure = stats.gas_moles * R * (available / stats.cv) / cell.volume_m3;
    else if (stats.gas_moles == 0) {
        if (nominal_delta > 0) pressure = pressure_root([&](Real p) { return nominal_delta - compression(materials_, cell.masses_kg, p).contracted_volume; }, stats.maximum_pressure, options, iterations);
    } else {
        const Real minimum_admissible = stats.minimum_temperature - boundary_roundoff(stats.minimum_temperature, stats.minimum_temperature);
        const Real budget = available - stats.cv * minimum_admissible;
        const Real upper = energy_limited_pressure(materials_, cell.masses_kg, stats.maximum_pressure, budget);
        pressure = pressure_root([&](Real p) {
            const auto elastic = compression(materials_, cell.masses_kg, p);
            const Real temperature = (available - elastic.energy) / stats.cv;
            return p * (nominal_delta - elastic.contracted_volume) + stats.gas_moles * R * temperature;
        }, upper, options, iterations);
    }
    const Real temperature = (available - compression(materials_, cell.masses_kg, pressure).energy) / stats.cv;
    return finish(materials_, cell.masses_kg, stats, cell.volume_m3, pressure, temperature, cell.internal_energy_j, iterations, options);
}

void LiquidThermodynamicReference::evaluate_into(const ReferenceConservedCell& cell, ReferenceEquilibriumState& output, ReferenceRootOptions options) const {
    auto completed = evaluate(cell, options); output = std::move(completed);
}

ReferenceTemperatureInitialization LiquidThermodynamicReference::initialize_at_temperature(std::span<const double> masses,
    double temperature, double volume, ReferenceRootOptions options) const {
    validate_options(options); const auto stats = statistics(materials_, masses, volume);
    check(positive(temperature), ReferenceErrorCode::invalid_inventory, "Initialization needs positive finite temperature");
    if (stats.mass > 0) temperature_in_range(temperature, stats);
    Real pressure = 0; std::size_t iterations = 0;
    const Real nominal_delta = stats.nominal_liquid_volume - volume;
    if (!stats.has_liquid && stats.gas_moles > 0) pressure = stats.gas_moles * R * temperature / volume;
    else if (stats.has_liquid && (stats.gas_moles > 0 || nominal_delta > 0)) {
        pressure = pressure_root([&](Real p) {
            const Real remaining = nominal_delta - compression(materials_, masses, p).contracted_volume;
            return stats.gas_moles > 0 ? p * remaining + stats.gas_moles * R * temperature : remaining;
        }, stats.maximum_pressure, options, iterations);
    }
    ReferenceTemperatureInitialization result;
    const Real caloric = stats.offset + stats.cv * temperature;
    const Real elastic = compression(materials_, masses, pressure).energy;
    result.cell = {std::vector<double>(masses.begin(), masses.end()), checked(caloric + elastic), volume};
    result.caloric_reference_and_thermal_energy_j = checked(caloric);
    result.initialized_elastic_energy_j = checked(elastic);
    result.state = finish(materials_, masses, stats, volume, pressure, stats.mass > 0 ? temperature : 0, result.cell.internal_energy_j, iterations, options);
    return result;
}
}
