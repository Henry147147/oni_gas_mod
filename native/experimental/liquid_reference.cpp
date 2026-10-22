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
Real log_ratio(Real value, Real reference) {
    const Real difference = value - reference;
    return std::abs(difference) < reference / 2 ? std::log1p(difference / reference) : std::log(value) - std::log(reference);
}
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
double projected_volume(Real value, Real& projection_error) {
    if (value > 0 && static_cast<double>(value) == 0) {
        const double projected = std::numeric_limits<double>::denorm_min();
        projection_error += static_cast<Real>(projected) - value;
        return projected;
    }
    return checked(value);
}
double projected_elastic_energy(Real value, Real scale) {
    if (value != 0 && static_cast<double>(value) == 0) {
        check(std::abs(value) <= boundary_roundoff(value, scale), ReferenceErrorCode::numerical_range,
            "Unresolved elastic-energy readout exceeds the conserved-energy precision");
        return 0;
    }
    return checked(value);
}
Real compression_ratio(Real pressure, Real bulk) {
    const Real q = pressure / bulk;
    check(pressure == 0 || q > 0, ReferenceErrorCode::numerical_range, "Positive liquid compression underflowed");
    return q;
}
// scale*[log(1+q) - q/(1+q)]. Factor the small-q series before
// multiplying: q*q may underflow even when the scaled energy is representable.
Real scaled_elastic_shape(Real scale, Real q) {
    if (q == 0) return 0;
    Real result;
    check(q > -1, ReferenceErrorCode::numerical_range, "Elastic divided difference left its logarithmic domain");
    if (std::abs(q) < 1e-3L) {
        Real power = q, factor = .5L;
        for (int n = 3; n <= 12; ++n) {
            factor += ((n % 2) == 0 ? 1 : -1) * (static_cast<Real>(n - 1) / n) * power;
            power *= q;
        }
        result = (scale * q) * (q * factor);
    } else result = scale * (std::log1p(q) - q / (1 + q));
    check(std::isfinite(result) && result >= 0, ReferenceErrorCode::numerical_range,
        "Elastic divided difference is outside the numerical range");
    return result;
}
Real elastic_energy(Real scale, Real q) {
    const Real result = scaled_elastic_shape(scale, q);
    check(q == 0 || result > 0, ReferenceErrorCode::numerical_range,
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
    checked(result.mass); checked(result.cv); checked(result.offset); checked(result.gas_moles);
    // This private volume remains wide throughout the pressure/energy solve.
    check(std::isfinite(result.nominal_liquid_volume) &&
        result.nominal_liquid_volume <= std::numeric_limits<double>::max(),
        ReferenceErrorCode::numerical_range, "Nominal liquid volume exceeds finite double range");
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
Real mixed_pressure_upper_bound(const std::vector<ReferenceMaterial>& definitions, std::span<const double> masses,
    const Statistics& stats, Real delta, Real temperature, Real high) {
    const Real gas_energy = stats.gas_moles * R * temperature;
    const Real compliance = compression(definitions, masses, high).compliance;
    // Liquid compliance decreases with pressure, so contraction(p) >= C(high)*p.
    // The positive root of C*p*p - delta*p - nRT therefore bounds the root
    // even at exact nominal fill, where a headspace-only bound is unavailable.
    if (!(compliance > 0)) return delta < 0 ? gas_energy / (-delta) : 0;
    const Real radical = std::sqrt(delta * delta + 4 * compliance * gas_energy);
    return delta <= 0 ? 2 * gas_energy / (radical - delta) : (delta + radical) / (2 * compliance);
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
template<class Function> Real pressure_root(Function residual, Real high, const ReferenceRootOptions& options,
    std::size_t& iterations, Real physical_upper = 0) {
    check(std::isfinite(high) && high > 0, ReferenceErrorCode::compression_outside_model, "No positive supported pressure interval");
    const Real at_high = residual(high);
    check(std::isfinite(at_high), ReferenceErrorCode::numerical_range, "Pressure residual overflowed");
    check(at_high <= 0, ReferenceErrorCode::compression_outside_model, "Cell cannot fit inside its compression and thermal domain");
    if (at_high == 0) return high;
    // Keep ordinary roots' original arithmetic. Extreme trace roots can spend
    // the entire budget crossing empty pressure decades before resolving them.
    if (physical_upper > 0 && std::isfinite(physical_upper) &&
        options.maximum_iterations < static_cast<std::size_t>(std::numeric_limits<Real>::max_exponent)) {
        const Real candidate = physical_upper * (1 + 32 * std::numeric_limits<double>::epsilon());
        const Real unresolved = std::ldexp(high, -static_cast<int>(options.maximum_iterations)) /
            options.relative_pressure_tolerance;
        if (candidate < std::min(high, unresolved)) {
            const Real value = residual(candidate);
            if (std::isfinite(value) && value <= 0) high = candidate;
        }
    }
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
    Real volume_projection = 0;
    for (std::size_t i = 0; i < masses.size(); ++i) {
        if (masses[i] == 0) continue;
        const auto& material = definitions[i]; const Real amount = masses[i];
        Real partial = 0, specific_elastic = 0;
        const Real specific_thermal = material.cv_j_per_kg_k * temperature;
        check(std::isfinite(specific_thermal) && specific_thermal > 0, ReferenceErrorCode::numerical_range,
            "Positive material thermal energy is outside the numerical range");
        entropy.add(amount * material.cv_j_per_kg_k * log_ratio(temperature, entropy_reference_temperature_k));
        if (material.entropy_offset_j_per_kg_k != 0) entropy.add(amount * material.entropy_offset_j_per_kg_k);
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
            // V/n can overflow/underflow although both logarithms, their
            // difference and n*R*log(V/n) are finite (including a denormal
            // trace beside bulk gas). This is the same entropy reference.
            entropy.add(moles * R * log_ratio(gas_volume, moles));
        }
        Sum specific_sum; specific_sum.add(material.energy_offset_j_per_kg);
        specific_sum.add(specific_thermal); specific_sum.add(specific_elastic);
        result.partial_volumes_m3[i] = projected_volume(partial, volume_projection);
        const double specific = checked(specific_sum.get());
        const Real sensible = specific_thermal + specific_elastic;
        const Real reconstructed_sensible = static_cast<Real>(specific) - material.energy_offset_j_per_kg;
        // The stored binary64 specific energy resolves sensible energy only to
        // its own precision, about eps * |offset| when a large chemical offset
        // dominates (cold gas: N2 below ~5 K, CO2 below ~215 K). Stored U has
        // the same absolute precision, so judge the round trip against the
        // representation, not against the sensible part alone.
        const Real allowance = std::max(boundary_roundoff(reconstructed_sensible, sensible),
            boundary_roundoff(static_cast<Real>(specific), material.energy_offset_j_per_kg));
        check(std::abs(reconstructed_sensible - sensible) <= allowance,
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
    result.gas_volume_m3 = projected_volume(gas_volume, volume_projection);
    result.liquid_volume_m3 = projected_volume(liquid_volume.get(), volume_projection);
    result.vacuum_volume_m3 = projected_volume(void_volume, volume_projection);
    check(volume_projection <= boundary_roundoff(volume, volume), ReferenceErrorCode::numerical_range,
        "Unresolved volume readouts exceed the cell-volume precision");
    result.reference_energy_j = checked(stats.offset); result.thermal_energy_j = checked(stats.cv * temperature);
    result.elastic_energy_j = projected_elastic_energy(elastic.energy, energy_scale); result.entropy_j_per_k = checked(entropy.get());
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
            std::isfinite(material.entropy_offset_j_per_kg_k) &&
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
    metadata_ = build_metadata();
}

std::string LiquidThermodynamicReference::build_metadata() const {
    std::ostringstream text; text.imbue(std::locale::classic()); text << std::setprecision(std::numeric_limits<double>::max_digits10) << std::scientific;
    text << liquid_reference_eos_version << '\n' << profile_version_.size() << ':' << profile_version_ << '\n';
    text << "R=" << reference_gas_constant << ";entropy_Tref=" << entropy_reference_temperature_k << ";zero_thermal_expansion;zero_tension;state_closure_only\n";
    for (const auto& material : materials_) {
        text << material.id.size() << ':' << material.id << ';' << static_cast<int>(material.phase) << ';'
            << material.molar_mass_kg_per_mol << ';' << material.cv_j_per_kg_k << ';' << material.energy_offset_j_per_kg << ';'
            << material.minimum_temperature_k << ';' << material.maximum_temperature_k << ';' << material.zero_pressure_density_kg_per_m3 << ';'
            << material.reference_bulk_modulus_pa << ';' << material.maximum_density_ratio << ';' << material.provenance.size() << ':' << material.provenance;
        // Calibrated saturation pairs only; uncalibrated metadata is unchanged.
        if (material.entropy_offset_j_per_kg_k != 0) text << ";entropy_offset=" << material.entropy_offset_j_per_kg_k;
        text << '\n';
    }
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
        }, upper, options, iterations,
            mixed_pressure_upper_bound(materials_, cell.masses_kg, stats, nominal_delta, available / stats.cv, upper));
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
        }, stats.maximum_pressure, options, iterations,
            mixed_pressure_upper_bound(materials_, masses, stats, nominal_delta, temperature, stats.maximum_pressure));
    }
    ReferenceTemperatureInitialization result;
    const Real caloric = stats.offset + stats.cv * temperature;
    const Real elastic = compression(materials_, masses, pressure).energy;
    result.cell = {std::vector<double>(masses.begin(), masses.end()), checked(caloric + elastic), volume};
    result.caloric_reference_and_thermal_energy_j = checked(caloric);
    result.initialized_elastic_energy_j = projected_elastic_energy(elastic,
        std::max({std::abs(caloric), std::abs(stats.offset), std::abs(stats.cv * temperature), std::abs(elastic)}));
    result.state = finish(materials_, masses, stats, volume, pressure, stats.mass > 0 ? temperature : 0, result.cell.internal_energy_j, iterations, options);
    return result;
}

ReferenceIsentrope::ReferenceIsentrope(std::string metadata, ReferenceConservedCell cell, ReferenceEquilibriumState state)
    : metadata_(std::move(metadata)), cell_(std::move(cell)), state_(std::move(state)) {}

namespace {
Real scaled_exp(Real scale, Real exponent) {
    const Real result = std::abs(exponent) < .5L ? scale * std::exp(exponent) : std::exp(std::log(scale) + exponent);
    check(std::isfinite(result) && result > 0, ReferenceErrorCode::numerical_range, "Isentropic exponential is outside the numerical range");
    return result;
}
Real scaled_expm1(Real scale, Real exponent) {
    return std::abs(exponent) < .5L ? scale * std::expm1(exponent) : scaled_exp(scale, exponent) - scale;
}
struct IsentropicPoint {
    Real pressure = 0, temperature = 0, gas_volume = 0;
    Real delta_pressure = 0, delta_volume = 0, delta_energy = 0;
};
struct IsentropicCurve {
    const std::vector<ReferenceMaterial>& definitions;
    const ReferenceIsentrope& anchor;
    Statistics stats;
    Real p0, t0, vg0, gas_heat, a, b;
    IsentropicCurve(const std::vector<ReferenceMaterial>& materials, const ReferenceIsentrope& reference)
        : definitions(materials), anchor(reference),
          stats(statistics(materials, reference.reference_cell().masses_kg, reference.reference_cell().volume_m3)),
          p0(reference.reference_state().mechanical_pressure_pa), t0(reference.reference_state().temperature_k),
          vg0(reference.reference_state().gas_volume_m3), gas_heat(stats.gas_moles * R),
          a(stats.gas_moles > 0 ? gas_heat / (stats.cv + gas_heat) : 0),
          b(stats.gas_moles > 0 ? stats.cv / (stats.cv + gas_heat) : 1) {
        if (stats.gas_moles > 0) check(a > 0 && a < 1 && b > 0,
            ReferenceErrorCode::numerical_range, "Isentropic gas exponent is not numerically resolved");
    }
    IsentropicPoint point(Real parameter) const {
        IsentropicPoint result;
        result.temperature = t0;
        if (stats.gas_moles > 0) {
            result.pressure = scaled_exp(p0, parameter);
            result.delta_pressure = scaled_expm1(p0, parameter);
            result.temperature = scaled_exp(t0, a * parameter);
            result.gas_volume = scaled_exp(vg0, -b * parameter);
        } else {
            result.delta_pressure = parameter;
            result.pressure = p0 + parameter;
            check(result.pressure >= 0 && std::isfinite(result.pressure), ReferenceErrorCode::compression_outside_model,
                "Isentropic liquid requires nonnegative finite pressure");
        }
        Sum volume, energy;
        if (stats.gas_moles > 0) {
            volume.add(scaled_expm1(vg0, -b * parameter));
            energy.add(scaled_expm1(stats.cv * t0, a * parameter));
        }
        for (std::size_t i = 0; i < definitions.size(); ++i) {
            const auto& material = definitions[i];
            if (anchor.reference_cell().masses_kg[i] == 0 || material.phase != ReferencePhase::elastic_liquid) continue;
            const Real nominal = static_cast<Real>(anchor.reference_cell().masses_kg[i]) / material.zero_pressure_density_kg_per_m3;
            const Real bulk = material.reference_bulk_modulus_pa;
            const Real previous_volume = nominal / (1 + p0 / bulk);
            const Real relative = result.delta_pressure / (bulk + p0);
            const Real fraction = result.delta_pressure / (bulk + result.pressure);
            volume.add(-previous_volume * fraction);
            // Difference of the integrated cold curve, with neither absolute
            // formation energy nor two nearly equal elastic energies subtracted.
            energy.add(scaled_elastic_shape(nominal * bulk, relative));
            energy.add((nominal * bulk) * (p0 / (bulk + p0)) * fraction);
        }
        result.delta_volume = volume.get(); result.delta_energy = energy.get();
        check(std::isfinite(result.delta_volume) && std::isfinite(result.delta_energy), ReferenceErrorCode::numerical_range,
            "Isentropic change exceeds the numerical range");
        return result;
    }
    Real compliance(const IsentropicPoint& point) const {
        const Real liquid = compression(definitions, anchor.reference_cell().masses_kg, point.pressure).compliance;
        const Real result = liquid + (stats.gas_moles > 0 ? b * point.gas_volume / point.pressure : 0);
        check(std::isfinite(result) && result > 0, ReferenceErrorCode::numerical_range, "Isentropic compliance is not finite and positive");
        return result;
    }
};

template<class Residual> Real shifted_root(Residual residual, Real step, Real bound, int direction,
    ReferenceErrorCode bound_error, const ReferenceRootOptions& options, bool logarithmic, std::size_t& iterations) {
    check(std::isfinite(bound) && (direction > 0 ? bound > 0 : bound < 0), bound_error, "No supported isentropic volume interval remains");
    const Real extent = std::abs(bound);
    if (!std::isfinite(step)) step = logarithmic ? 1 : extent;
    step = std::min(step, extent);
    if (logarithmic) step = std::min(step, static_cast<Real>(1));
    check(step > 0, ReferenceErrorCode::numerical_range, "Isentropic pressure increment underflowed");
    Real near = 0, far = direction * step;
    const auto sample = [&](Real parameter) {
        if (iterations >= options.maximum_iterations) fail(ReferenceErrorCode::root_not_converged, "Isentropic pressure root exceeded its explicit iteration budget");
        ++iterations;
        const Real value = residual(parameter);
        check(std::isfinite(value), ReferenceErrorCode::numerical_range, "Isentropic volume residual overflowed");
        return value;
    };
    for (;;) {
        const Real value = sample(far);
        if (value == 0) return far;
        if (direction * value < 0) break;
        check(far != bound, bound_error, "Requested volume exceeds the isentrope's declared domain");
        near = far;
        step = std::min(extent, step * 2);
        far = direction * step;
    }
    for (;;) {
        const Real middle = near + (far - near) / 2;
        if (middle == near || middle == far) return middle;
        const Real value = sample(middle);
        if (value == 0) return middle;
        if (direction * value > 0) near = middle; else far = middle;
        Real scale = std::max(std::abs(near), std::abs(far));
        if (logarithmic) scale = std::min(scale, static_cast<Real>(1));
        if (std::abs(far - near) <= options.relative_pressure_tolerance * scale) return near + (far - near) / 2;
    }
}
struct IsentropicSolution {
    ReferenceIsentropicResponse response;
    IsentropicPoint point;
    Real parameter = 0, initial_void = 0, target_fill_deficit = 0;
};

IsentropicSolution solve_isentrope(const std::vector<ReferenceMaterial>& definitions, const ReferenceIsentrope& anchor,
    Real target_volume, Real delta, bool explicit_delta, const ReferenceRootOptions& options) {
    validate_options(options);
    check(std::isfinite(target_volume) && target_volume > 0 && std::isfinite(delta), ReferenceErrorCode::invalid_inventory,
        "Isentropic target volume and increment must be finite with positive target volume");
    const IsentropicCurve curve(definitions, anchor);
    const auto& cell = anchor.reference_cell(); const auto& previous = anchor.reference_state();
    IsentropicSolution solution;
    auto& result = solution.response;
    result.volume_change_m3 = checked(delta);
    result.endpoint = {cell.masses_kg, cell.internal_energy_j, checked(target_volume)};
    solution.initial_void = previous.vacuum_volume_m3;
    solution.target_fill_deficit = explicit_delta ? (curve.stats.nominal_liquid_volume - cell.volume_m3) - delta
        : curve.stats.nominal_liquid_volume - target_volume;
    if (curve.stats.mass == 0) {
        result.state = finish(definitions, cell.masses_kg, curve.stats, target_volume, 0, 0, 0, 0, options);
        result.branch = ReferenceIsentropicBranch::vacuum;
    } else if (delta == 0) {
        result.state = previous;
        result.work_pressure_pa = previous.mechanical_pressure_pa;
        solution.point = {curve.p0, curve.t0, curve.vg0, 0, 0, 0};
    } else {
        std::size_t iterations = 0;
        if (curve.stats.gas_moles == 0 && solution.target_fill_deficit <= 0) {
            solution.parameter = -curve.p0;
        } else if (curve.stats.gas_moles > 0 && !curve.stats.has_liquid) {
            // Invert the gas-only volume curve exactly. A relative bisection
            // error in this small logarithmic shift becomes a relative error
            // in deltaU/deltaV, even as deltaV tends to zero. It can therefore
            // introduce a finite work-pressure perturbation into uniform flow.
            const bool incremental = std::abs(delta) <= static_cast<Real>(cell.volume_m3) / 8 && solution.initial_void == 0;
            const Real logarithm = incremental ? std::log1p(delta / curve.vg0) : log_ratio(target_volume, curve.vg0);
            solution.parameter = -logarithm / curve.b;
            check(std::isfinite(solution.parameter) && solution.parameter != 0, ReferenceErrorCode::numerical_range,
                "Nonzero gas volume change has an unrepresentable logarithmic pressure shift");
            // Keep the same thermal/pressure domain and error priority used
            // by the iterative gas/mixed branch below. No boundary is clamped.
            const int direction = solution.parameter > 0 ? 1 : -1;
            const Real double_min = std::numeric_limits<double>::denorm_min(), double_max = std::numeric_limits<double>::max();
            const Real minimum = curve.stats.minimum_temperature - boundary_roundoff(curve.stats.minimum_temperature, curve.stats.minimum_temperature);
            const Real maximum = std::min(curve.stats.maximum_temperature + boundary_roundoff(curve.stats.maximum_temperature, curve.stats.maximum_temperature), double_max);
            const Real thermal_bound = log_ratio(direction > 0 ? maximum : minimum, curve.t0) / curve.a;
            const Real pressure_bound = log_ratio(direction > 0 ? std::min(curve.stats.maximum_pressure, double_max) : double_min, curve.p0);
            const bool thermal_first = direction > 0 ? thermal_bound < pressure_bound : thermal_bound > pressure_bound;
            const Real bound = thermal_first ? thermal_bound : pressure_bound;
            const auto code = thermal_first ? ReferenceErrorCode::temperature_outside_model : ReferenceErrorCode::numerical_range;
            check(direction > 0 ? solution.parameter <= bound : solution.parameter >= bound, code,
                "Requested volume exceeds the gas isentrope's declared domain");
        } else {
            const bool gas = curve.stats.gas_moles > 0;
            const bool incremental = std::abs(delta) <= static_cast<Real>(cell.volume_m3) / 8 && solution.initial_void == 0;
            const auto residual = [&](Real parameter) {
                const auto point = curve.point(parameter);
                if (incremental) return point.delta_volume - delta;
                Sum sum; sum.add(solution.target_fill_deficit);
                sum.add(-compression(definitions, cell.masses_kg, point.pressure).contracted_volume);
                sum.add(point.gas_volume);
                return sum.get();
            };
            const auto initial = curve.point(0);
            Real scale = curve.compliance(initial);
            Real bound;
            ReferenceErrorCode bound_error = ReferenceErrorCode::compression_outside_model;
            const int direction = gas || incremental ? (delta < 0 ? 1 : -1)
                : (solution.target_fill_deficit > compression(definitions, cell.masses_kg, curve.p0).contracted_volume ? 1 : -1);
            Real requested = delta;
            if (gas) {
                scale = curve.p0 * compression(definitions, cell.masses_kg, curve.p0).compliance + curve.b * curve.vg0;
                const Real double_min = std::numeric_limits<double>::denorm_min(), double_max = std::numeric_limits<double>::max();
                const Real minimum = curve.stats.minimum_temperature - boundary_roundoff(curve.stats.minimum_temperature, curve.stats.minimum_temperature);
                const Real maximum = std::min(curve.stats.maximum_temperature + boundary_roundoff(curve.stats.maximum_temperature, curve.stats.maximum_temperature), double_max);
                const Real thermal_bound = log_ratio(direction > 0 ? maximum : minimum, curve.t0) / curve.a;
                const Real pressure_bound = log_ratio(direction > 0 ? std::min(curve.stats.maximum_pressure, double_max) : double_min, curve.p0);
                if (direction > 0 ? thermal_bound < pressure_bound : thermal_bound > pressure_bound) {
                    bound = thermal_bound; bound_error = ReferenceErrorCode::temperature_outside_model;
                } else {
                    bound = pressure_bound;
                    if (direction < 0 || !curve.stats.has_liquid) bound_error = ReferenceErrorCode::numerical_range;
                }
            } else {
                bound = direction > 0 ? curve.stats.maximum_pressure - curve.p0 : -curve.p0;
                if (!incremental) requested = compression(definitions, cell.masses_kg, curve.p0).contracted_volume - solution.target_fill_deficit;
            }
            solution.parameter = shifted_root(residual, std::abs(requested) / scale, bound, direction, bound_error, options, gas, iterations);
        }
        solution.point = curve.point(solution.parameter);
        check(solution.point.delta_energy == 0 || (solution.point.delta_energy > 0) == (delta < 0), ReferenceErrorCode::numerical_range,
            "Isentropic work has an inconsistent sign");
        check((solution.point.pressure == 0 && curve.p0 == 0) || solution.point.delta_energy != 0,
            ReferenceErrorCode::numerical_range, "Nonzero isentropic work underflowed");
        result.internal_energy_change_j = checked(solution.point.delta_energy);
        result.work_pressure_pa = checked(-solution.point.delta_energy / delta);
        // Form the endpoint independently, so expansion toward vacuum does
        // not erase its small remaining heat through U0 + (-almost U0).
        // The represented-increment fields below expose any mismatch with
        // the separately retained work; no conservation equality is assumed.
        Sum energy; energy.add(curve.stats.offset); energy.add(curve.stats.cv * solution.point.temperature);
        energy.add(compression(definitions, cell.masses_kg, solution.point.pressure).energy);
        result.endpoint.internal_energy_j = checked(energy.get());
        result.state = finish(definitions, cell.masses_kg, curve.stats, target_volume, solution.point.pressure,
            solution.point.temperature, result.endpoint.internal_energy_j, iterations, options);
    }
    if (curve.stats.mass > 0) result.branch = curve.stats.gas_moles > 0 ? ReferenceIsentropicBranch::gas
        : solution.target_fill_deficit > 0 ? ReferenceIsentropicBranch::compressed_liquid
        : solution.target_fill_deficit < 0 ? ReferenceIsentropicBranch::liquid_with_void : ReferenceIsentropicBranch::zero_pressure_fill;
    const Real represented_volume = static_cast<Real>(result.endpoint.volume_m3) - cell.volume_m3;
    const Real represented_energy = static_cast<Real>(result.endpoint.internal_energy_j) - cell.internal_energy_j;
    result.represented_volume_change_m3 = checked(represented_volume);
    result.represented_internal_energy_change_j = checked(represented_energy);
    result.endpoint_volume_resolves_change = std::abs(represented_volume - delta) <= boundary_roundoff(represented_volume, delta);
    result.endpoint_energy_resolves_change = std::abs(represented_energy - result.internal_energy_change_j) <=
        boundary_roundoff(represented_energy, result.internal_energy_change_j);
    return solution;
}

// (x-log(1+x))/x^2, retaining the finite limit without x*x.
Real logarithmic_second_difference(Real x) {
    if (std::abs(x) >= 1e-3L) return (1 - std::log1p(x) / x) / x;
    Real power = 1, sum = .5L;
    for (int n = 3; n <= 14; ++n) { power *= x; sum += (n % 2 == 0 ? 1 : -1) * power / n; }
    return sum;
}
Real exprel(Real x) { return x == 0 ? 1 : std::expm1(x) / x; }
// [expm1(l)-expm1(a*l)/a]/[(1-a)*l^2]. This divided
// difference stays positive and finite at l=0 and when a is a tiny trace.
Real gas_second_difference(Real logarithm, Real a, Real b) {
    if (std::abs(logarithm) >= 1e-3L)
        return (std::exp(a * logarithm) * exprel(b * logarithm) - exprel(a * logarithm)) / logarithm;
    Real geometric = 1, power = 1, factorial = 2, sum = .5L;
    for (int n = 3; n <= 16; ++n) {
        geometric = 1 + a * geometric; power *= logarithm; factorial *= n;
        sum += geometric * power / factorial;
    }
    return sum;
}
Real work_derivative(const IsentropicCurve& curve, const IsentropicSolution& solution) {
    const Real delta = solution.response.volume_change_m3;
    if (curve.stats.mass == 0) return 0;
    if (delta == 0) {
        check(solution.response.branch != ReferenceIsentropicBranch::zero_pressure_fill, ReferenceErrorCode::unsupported_derivative,
            "Zero-pressure liquid fill has different one-sided work-pressure derivatives");
        if (solution.response.branch == ReferenceIsentropicBranch::liquid_with_void) return 0;
        return -.5L / curve.compliance(solution.point);
    }
    Sum derivative;
    if (curve.stats.gas_moles > 0) {
        const Real ratio = solution.parameter / delta;
        derivative.add(-curve.gas_heat * curve.t0 * curve.b * ratio * ratio * gas_second_difference(solution.parameter, curve.a, curve.b));
    }
    for (std::size_t i = 0; i < curve.definitions.size(); ++i) {
        const auto& material = curve.definitions[i];
        if (curve.anchor.reference_cell().masses_kg[i] == 0 || material.phase != ReferencePhase::elastic_liquid) continue;
        const Real nominal = static_cast<Real>(curve.anchor.reference_cell().masses_kg[i]) / material.zero_pressure_density_kg_per_m3;
        const Real r = solution.point.delta_pressure / (material.reference_bulk_modulus_pa + curve.p0);
        const Real ratio = r / delta;
        derivative.add(-nominal * material.reference_bulk_modulus_pa * ratio * ratio * logarithmic_second_difference(r));
    }
    if (solution.initial_void > 0 && solution.point.pressure > 0)
        derivative.add(-solution.point.pressure * (solution.initial_void / delta) / delta);
    const Real result = derivative.get();
    check(std::isfinite(result) && result <= 0, ReferenceErrorCode::numerical_range, "Isentropic work-pressure derivative is not finite and nonpositive");
    return result;
}
std::optional<double> work_pressure_change(const IsentropicCurve& curve,
    const IsentropicSolution& solution, Real derivative) {
    const Real delta = solution.response.volume_change_m3;
    if (delta == 0 || curve.stats.mass == 0) return 0.;
    // W'=(p_endpoint-W)/dV, hence W-p0=(p_endpoint-p0)-dV*W'.
    // Both terms on the right are first order near dV=0. The curve retains
    // endpoint pressure changes using expm1, including mixed/liquid states.
    // Far from the anchor, the direct work difference is well conditioned.
    const Real change = std::abs(solution.point.delta_pressure) < std::abs(curve.p0) / 8
        ? solution.point.delta_pressure - delta * derivative
        : -solution.point.delta_energy / delta - curve.p0;
    const double stored = static_cast<double>(change);
    if (!std::isfinite(stored) || (stored == 0 && (change != 0 || derivative != 0)) ||
        std::abs(static_cast<Real>(stored) - change) > boundary_roundoff(change, change)) return std::nullopt;
    // Anchor readings, the volume inversion and the absolute secant each have
    // their documented rounding. Admit the continuous increment only while
    // its reconstructed work pressure agrees within that same reference bound.
    const Real reconstructed = curve.p0 + change;
    if (std::abs(reconstructed - solution.response.work_pressure_pa) >
        boundary_roundoff(reconstructed, solution.response.work_pressure_pa)) return std::nullopt;
    return stored;
}
}

ReferenceIsentrope LiquidThermodynamicReference::make_isentrope(const ReferenceConservedCell& cell, ReferenceRootOptions options) const {
    return {metadata(), cell, evaluate(cell, options)};
}
ReferenceIsentropicResponse LiquidThermodynamicReference::isentropic_at_volume(const ReferenceIsentrope& anchor, double volume, ReferenceRootOptions options) const {
    check(anchor.model_metadata() == metadata(), ReferenceErrorCode::incompatible_entropy_reference, "Entropy reference belongs to a different model profile");
    return solve_isentrope(materials_, anchor, volume, static_cast<Real>(volume) - anchor.reference_cell().volume_m3, false, options).response;
}
ReferenceIsentropicResponse LiquidThermodynamicReference::isentropic_response(const ReferenceIsentrope& anchor, double delta, ReferenceRootOptions options) const {
    check(anchor.model_metadata() == metadata(), ReferenceErrorCode::incompatible_entropy_reference, "Entropy reference belongs to a different model profile");
    return solve_isentrope(materials_, anchor, static_cast<Real>(anchor.reference_cell().volume_m3) + delta, delta, true, options).response;
}
void LiquidThermodynamicReference::isentropic_response_into(const ReferenceIsentrope& anchor, double delta, ReferenceIsentropicResponse& output, ReferenceRootOptions options) const {
    auto next = isentropic_response(anchor, delta, options); output = std::move(next);
}
ReferenceIsentropicDerivatives LiquidThermodynamicReference::isentropic_derivatives(const ReferenceIsentrope& anchor, double delta, ReferenceRootOptions options) const {
    check(anchor.model_metadata() == metadata(), ReferenceErrorCode::incompatible_entropy_reference, "Entropy reference belongs to a different model profile");
    const auto solution = solve_isentrope(materials_, anchor, static_cast<Real>(anchor.reference_cell().volume_m3) + delta, delta, true, options);
    check(solution.response.branch != ReferenceIsentropicBranch::vacuum && solution.response.branch != ReferenceIsentropicBranch::zero_pressure_fill,
        ReferenceErrorCode::unsupported_derivative, "Vacuum temperature or zero-pressure fill stiffness has no two-sided derivative");
    const IsentropicCurve curve(materials_, anchor);
    if (solution.response.branch == ReferenceIsentropicBranch::liquid_with_void) return {0, 0, 0, checked(work_derivative(curve, solution))};
    const Real pressure_derivative = -1 / curve.compliance(solution.point);
    const Real temperature_derivative = curve.stats.gas_moles > 0 ? curve.a * solution.point.temperature / solution.point.pressure * pressure_derivative : 0;
    return {checked(pressure_derivative), checked(temperature_derivative), checked(-solution.point.pressure), checked(work_derivative(curve, solution))};
}
double LiquidThermodynamicReference::isentropic_work_pressure_derivative(const ReferenceIsentrope& anchor, double delta, ReferenceRootOptions options) const {
    check(anchor.model_metadata() == metadata(), ReferenceErrorCode::incompatible_entropy_reference, "Entropy reference belongs to a different model profile");
    const auto solution = solve_isentrope(materials_, anchor, static_cast<Real>(anchor.reference_cell().volume_m3) + delta, delta, true, options);
    return checked(work_derivative(IsentropicCurve(materials_, anchor), solution));
}
ReferenceIsentropicWork LiquidThermodynamicReference::isentropic_work(const ReferenceIsentrope& anchor, double delta, ReferenceRootOptions options) const {
    check(anchor.model_metadata() == metadata(), ReferenceErrorCode::incompatible_entropy_reference, "Entropy reference belongs to a different model profile");
    auto solution = solve_isentrope(materials_, anchor, static_cast<Real>(anchor.reference_cell().volume_m3) + delta, delta, true, options);
    const IsentropicCurve curve(materials_, anchor);
    const Real derivative = work_derivative(curve, solution);
    const auto change = work_pressure_change(curve, solution, derivative);
    return {std::move(solution.response), checked(derivative), change};
}
}
