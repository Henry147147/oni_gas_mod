#pragma once
#include "mixture.hpp"
#include <string>
#include <vector>

namespace ma {
struct ReactionTerm { std::size_t species; double moles; }; // negative = reagent
struct ReactionDefinition {
    std::string id;
    std::vector<ReactionTerm> terms;
    void validate(const SpeciesRegistry& registry) const;
    [[nodiscard]] double enthalpy_change(const SpeciesRegistry& registry, double temperature) const;
    [[nodiscard]] double available_extent(const SpeciesRegistry& registry, const MixtureInventory& inventory) const;
};

struct ReactionResult {
    double extent_mol = 0;
    double work_in_j = 0;
    double work_out_j = 0;
};

// Adds/subtracts explicit electrical or boundary work. No implicit thermostat,
// deleted products, or free reaction energy. Insufficient thermal energy rejects
// the whole operation, including its requested work exchange.
ReactionResult react(const SpeciesRegistry& registry, MixtureInventory& inventory,
                     const ReactionDefinition& reaction, double extent_mol,
                     double work_in_j = 0, double work_out_j = 0);
[[nodiscard]] std::vector<ReactionDefinition> standard_reactions(const SpeciesRegistry& registry);
}
