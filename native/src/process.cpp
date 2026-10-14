#include "mixed_atmospheres/process.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma {
namespace {
void check_phase(const SpeciesRegistry& r, const FlowPacket& feed, Phase phase) {
    feed.validate(r);
    if (feed.contents.mass() == 0) throw std::invalid_argument("A process feed cannot be empty");
    for (std::size_t i = 0; i < r.size(); ++i)
        if (feed.contents.masses[i] != 0 && r[i].phase != phase)
            throw std::domain_error("Unsupported phase in process feed");
}
double cp_capacity(const SpeciesRegistry& r, const MixtureInventory& inventory) {
    double result = 0;
    for (std::size_t i = 0; i < r.size(); ++i) result += inventory.masses[i] * r[i].cp;
    require_finite(result, "stream heat capacity");
    if (result <= 0) throw std::domain_error("Invalid stream heat capacity");
    return result;
}
void check_efficiency(double efficiency) {
    require_finite(efficiency, "efficiency");
    if (efficiency <= 0 || efficiency > 1) throw std::invalid_argument("Efficiency must be in (0, 1]");
}
void check_outlet(const FlowPacket& feed, double pressure, double efficiency) {
    check_efficiency(efficiency);
    require_finite(pressure, "outlet pressure");
    if (pressure < feed.pressure_pa) throw std::invalid_argument("Pump outlet pressure must not be below its inlet");
}
long double binary_mixing_term(long double selected, long double other) {
    if (selected == 0 || other == 0) return 0;
    // log1p retains the majority constituent's contribution near pure streams.
    return -selected * std::log1p(other / selected) - other * std::log1p(selected / other);
}
}

void FlowPacket::validate(const SpeciesRegistry& r) const {
    contents.validate(r);
    require_finite(pressure_pa, "stream pressure");
    if (pressure_pa <= 0) throw std::invalid_argument("Stream pressure must be positive");
}
double FlowPacket::volume(const SpeciesRegistry& r) const {
    validate(r);
    const double result = contents.condensed_volume(r) + contents.gas_moles(r) * gas_constant * contents.temperature(r) / pressure_pa;
    require_finite(result, "stream volume");
    return result;
}
double FlowPacket::enthalpy(const SpeciesRegistry& r) const {
    const double result = contents.energy + pressure_pa * volume(r);
    require_finite(result, "stream enthalpy");
    return result;
}

PumpResult compress_gas(const SpeciesRegistry& r, const FlowPacket& feed, double outlet, double efficiency) {
    check_phase(r, feed, Phase::gas);
    check_outlet(feed, outlet, efficiency);
    const double capacity = cp_capacity(r, feed.contents);
    const double nr = feed.contents.gas_moles(r) * gas_constant;
    const double tin = feed.contents.temperature(r);
    // log(p2)-log(p1) avoids overflow of the pressure ratio.
    const double log_ratio = std::log(outlet) - std::log(feed.pressure_pa);
    const double work = capacity * tin * std::expm1(nr / capacity * log_ratio) / efficiency;
    require_finite(work, "compressor work");
    const double tout = tin + work / capacity;
    PumpResult result{feed, work, 0};
    result.output.pressure_pa = outlet;
    result.output.contents.set_temperature(r, tout);
    result.output.validate(r);
    result.entropy_produced_j_per_k = capacity * std::log1p((tout - tin) / tin) - nr * log_ratio;
    // The exact reversible expression is zero; tolerate its last-bit rounding.
    const double roundoff = 64 * std::numeric_limits<double>::epsilon() * capacity * std::max(1.0, std::abs(log_ratio));
    if (result.entropy_produced_j_per_k < -roundoff) throw std::domain_error("Compressor violated entropy bound");
    result.entropy_produced_j_per_k = std::max(0.0, result.entropy_produced_j_per_k);
    return result;
}

PumpResult pump_liquid(const SpeciesRegistry& r, const FlowPacket& feed, double outlet, double efficiency) {
    check_phase(r, feed, Phase::liquid);
    check_outlet(feed, outlet, efficiency);
    const double hydraulic_work = feed.contents.condensed_volume(r) * (outlet - feed.pressure_pa);
    const double work = hydraulic_work / efficiency;
    require_finite(work, "pump work");
    const double loss = work - hydraulic_work;
    PumpResult result{feed, work, 0};
    result.output.pressure_pa = outlet;
    result.output.contents.energy += loss;
    result.output.validate(r);
    const double tin = feed.contents.temperature(r), tout = result.output.contents.temperature(r);
    result.entropy_produced_j_per_k = cp_capacity(r, feed.contents) * std::log1p((tout - tin) / tin);
    return result;
}

SeparationResult separate_gas(const SpeciesRegistry& r, const FlowPacket& feed,
                             std::size_t species, double recovery, double efficiency) {
    check_phase(r, feed, Phase::gas);
    check_efficiency(efficiency);
    require_finite(recovery, "recovery fraction");
    if (species >= r.size() || r[species].phase != Phase::gas || recovery < 0 || recovery > 1)
        throw std::invalid_argument("Invalid separator selection or recovery");
    SeparationResult result{{MixtureInventory(r.size()), feed.pressure_pa}, feed};
    result.heat_rejection_temperature_k = feed.contents.temperature(r);
    result.selected.contents = result.remainder.contents.extract(r, feed.contents.masses[species] * recovery, species);
    // Other species retain their relative fractions. Their internal mixing
    // entropy cancels, leaving the selected-vs-other binary demixing term.
    const long double selected = static_cast<long double>(feed.contents.masses[species]) / r[species].molar_mass;
    const long double retained = static_cast<long double>(result.remainder.contents.masses[species]) / r[species].molar_mass;
    long double other = 0;
    for (std::size_t i = 0; i < r.size(); ++i)
        if (i != species) other += static_cast<long double>(feed.contents.masses[i]) / r[i].molar_mass;
    const long double dimensionless = binary_mixing_term(retained, other) - binary_mixing_term(selected, other);
    if (dimensionless < 0) throw std::domain_error("Separation work became negative");
    result.reversible_work_j = static_cast<double>(dimensionless * gas_constant * result.heat_rejection_temperature_k);
    result.work_in_j = result.reversible_work_j / efficiency;
    require_finite(result.work_in_j, "separation work");
    result.heat_rejected_j = result.work_in_j;
    result.entropy_produced_j_per_k = (result.work_in_j - result.reversible_work_j) / result.heat_rejection_temperature_k;
    return result;
}
}
