#pragma once
#include "species.hpp"
#include <vector>

namespace ma {
struct MixtureInventory {
    std::vector<double> masses; // kg, one entry per stable registry species
    double energy = 0.0;       // J, including formation energy, excluding bulk kinetic energy

    explicit MixtureInventory(std::size_t count = 0) : masses(count, 0.0) {}
    [[nodiscard]] double mass() const;
    [[nodiscard]] double heat_capacity(const SpeciesRegistry& registry) const;
    [[nodiscard]] double temperature(const SpeciesRegistry& registry) const;
    [[nodiscard]] double condensed_volume(const SpeciesRegistry& registry) const;
    [[nodiscard]] double gas_moles(const SpeciesRegistry& registry) const;
    [[nodiscard]] double pressure(const SpeciesRegistry& registry, double volume) const;
    void validate(const SpeciesRegistry& registry) const;
    void set_temperature(const SpeciesRegistry& registry, double temperature);
    void add(const SpeciesRegistry& registry, std::size_t species, double kg, double temperature);
    void merge(const SpeciesRegistry& registry, const MixtureInventory& parcel);
    // Returns the actual parcel. Selection = registry.size() means bulk extraction.
    [[nodiscard]] MixtureInventory extract(const SpeciesRegistry& registry, double kg, std::size_t selection);
};

// Atomic, mass-limited transfer with a target mass capacity; a full target leaves
// the source unchanged. Same-inventory transfers are rejected.
double transfer(const SpeciesRegistry& registry, MixtureInventory& source,
                MixtureInventory& target, double requested_kg, double target_capacity,
                std::size_t selection);
}
