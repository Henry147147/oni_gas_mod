#include "mixed_atmospheres/mixture.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ma {
double MixtureInventory::mass() const { return std::accumulate(masses.begin(), masses.end(), 0.0); }

double MixtureInventory::heat_capacity(const SpeciesRegistry& registry) const {
    if (masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double result = 0;
    for (std::size_t i = 0; i < masses.size(); ++i) result += masses[i] * registry[i].cv();
    return result;
}

double MixtureInventory::temperature(const SpeciesRegistry& registry) const {
    const double capacity = heat_capacity(registry);
    if (capacity == 0) return 0;
    double zero_energy = 0;
    for (std::size_t i = 0; i < masses.size(); ++i) zero_energy += masses[i] * registry[i].internal_energy(0);
    return (energy - zero_energy) / capacity;
}

double MixtureInventory::condensed_volume(const SpeciesRegistry& registry) const {
    if (masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double result = 0;
    for (std::size_t i = 0; i < masses.size(); ++i)
        if (registry[i].phase != Phase::gas) result += masses[i] / registry[i].density;
    return result;
}

double MixtureInventory::gas_moles(const SpeciesRegistry& registry) const {
    if (masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double result = 0;
    for (std::size_t i = 0; i < masses.size(); ++i)
        if (registry[i].phase == Phase::gas) result += masses[i] / registry[i].molar_mass;
    return result;
}

double MixtureInventory::pressure(const SpeciesRegistry& registry, double volume) const {
    require_finite(volume, "volume");
    if (volume <= 0) throw std::invalid_argument("Volume must be positive");
    const double gas_volume = volume - condensed_volume(registry);
    if (gas_volume <= 0) throw std::domain_error("No gas headspace: condensed-phase pressure requires the liquid solver");
    const double result = gas_moles(registry) * gas_constant * temperature(registry) / gas_volume;
    require_finite(result, "pressure");
    return result;
}

void MixtureInventory::validate(const SpeciesRegistry& registry) const {
    if (masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    require_finite(energy, "inventory energy");
    for (double m : masses) {
        require_finite(m, "component mass");
        if (m < 0) throw std::invalid_argument("Negative component mass");
    }
    const double total = mass();
    if (!std::isfinite(total)) throw std::invalid_argument("Inventory mass overflow");
    if (total == 0) {
        if (energy != 0) throw std::invalid_argument("Empty inventory cannot hold thermal energy");
    } else {
        const double t = temperature(registry);
        if (!std::isfinite(t) || t <= 0) throw std::domain_error("Inventory has nonpositive temperature");
    }
}

void MixtureInventory::set_temperature(const SpeciesRegistry& registry, double t) {
    require_finite(t, "temperature");
    if (t <= 0) throw std::invalid_argument("Temperature must be positive");
    if (masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double next = 0;
    for (std::size_t i = 0; i < masses.size(); ++i) next += masses[i] * registry[i].internal_energy(t);
    require_finite(next, "inventory energy");
    energy = next;
}

void MixtureInventory::add(const SpeciesRegistry& registry, std::size_t species, double kg, double t) {
    validate(registry);
    require_finite(kg, "added mass");
    require_finite(t, "temperature");
    if (kg < 0 || t <= 0) throw std::invalid_argument("Invalid addition");
    if (species >= masses.size()) throw std::out_of_range("Species index");
    auto next = *this;
    next.masses[species] += kg;
    next.energy += kg * registry[species].internal_energy(t);
    next.validate(registry);
    *this = std::move(next);
}

void MixtureInventory::merge(const SpeciesRegistry& registry, const MixtureInventory& parcel) {
    validate(registry);
    parcel.validate(registry);
    auto next = *this;
    for (std::size_t i = 0; i < masses.size(); ++i) next.masses[i] += parcel.masses[i];
    next.energy += parcel.energy;
    next.validate(registry);
    *this = std::move(next);
}

MixtureInventory MixtureInventory::extract(const SpeciesRegistry& registry, double kg, std::size_t selection) {
    validate(registry);
    require_finite(kg, "requested mass");
    if (kg < 0 || selection > registry.size()) throw std::invalid_argument("Invalid extraction");
    MixtureInventory parcel(registry.size());
    const double available = selection == registry.size() ? mass() : masses[selection];
    if (available == 0 || kg == 0) return parcel;
    const double amount = std::min(kg, available);
    const double fraction = amount / available;
    const double t = temperature(registry);
    auto next = *this;
    for (std::size_t i = 0; i < masses.size(); ++i) {
        if (selection == registry.size() || selection == i) {
            parcel.masses[i] = amount == available ? masses[i] : masses[i] * fraction;
            next.masses[i] -= parcel.masses[i];
            parcel.energy += parcel.masses[i] * registry[i].internal_energy(t);
        }
    }
    next.energy -= parcel.energy;
    if (next.mass() == 0) { parcel.energy += next.energy; next.energy = 0; }
    next.validate(registry);
    parcel.validate(registry);
    *this = std::move(next);
    return parcel;
}

double transfer(const SpeciesRegistry& r, MixtureInventory& source, MixtureInventory& target,
                double requested, double capacity, std::size_t selection) {
    if (&source == &target) throw std::invalid_argument("Source and target are identical");
    require_finite(requested, "requested mass");
    require_finite(capacity, "capacity");
    if (requested < 0 || capacity < 0) throw std::invalid_argument("Invalid transfer limit");
    source.validate(r);
    target.validate(r);
    auto next_source = source;
    auto next_target = target;
    auto parcel = next_source.extract(r, std::min(requested, std::max(0.0, capacity - target.mass())), selection);
    const double moved = parcel.mass();
    next_target.merge(r, parcel);
    source = std::move(next_source);
    target = std::move(next_target);
    return moved;
}
}
