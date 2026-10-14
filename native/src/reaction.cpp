#include "mixed_atmospheres/reaction.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace ma {
void ReactionDefinition::validate(const SpeciesRegistry& r) const {
    if (id.empty() || terms.size() < 2) throw std::invalid_argument("Incomplete reaction");
    std::map<std::string, double> balance;
    std::set<std::size_t> seen;
    bool input = false, output = false;
    for (const auto& term : terms) {
        require_finite(term.moles, "stoichiometric coefficient");
        if (term.moles == 0 || !seen.insert(term.species).second)
            throw std::invalid_argument("Zero or duplicate reaction term");
        const auto& s = r[term.species];
        input |= term.moles < 0;
        output |= term.moles > 0;
        for (const auto& [atom, count] : s.atoms) balance[atom] += term.moles * count;
    }
    if (!input || !output) throw std::invalid_argument("Reaction requires inputs and outputs");
    for (const auto& [atom, net] : balance) {
        require_finite(net, "atomic balance");
        if (std::abs(net) > 1e-12) throw std::invalid_argument("Unbalanced reaction " + id + ": " + atom);
    }
}

double ReactionDefinition::enthalpy_change(const SpeciesRegistry& r, double t) const {
    validate(r);
    require_finite(t, "temperature");
    if (t <= 0) throw std::invalid_argument("Temperature must be positive");
    double result = 0;
    for (const auto& term : terms)
        result += term.moles * r[term.species].molar_mass * r[term.species].enthalpy(t);
    return result;
}

double ReactionDefinition::available_extent(const SpeciesRegistry& r, const MixtureInventory& inventory) const {
    validate(r);
    inventory.validate(r);
    double extent = std::numeric_limits<double>::infinity();
    for (const auto& term : terms)
        if (term.moles < 0) extent = std::min(extent, inventory.masses[term.species] /
            (-term.moles * r[term.species].molar_mass));
    return extent;
}

ReactionResult react(const SpeciesRegistry& r, MixtureInventory& inventory,
                     const ReactionDefinition& reaction, double extent, double work_in, double work_out) {
    reaction.validate(r);
    inventory.validate(r);
    require_finite(extent, "reaction extent");
    require_finite(work_in, "input work");
    require_finite(work_out, "output work");
    if (extent < 0 || work_in < 0 || work_out < 0) throw std::invalid_argument("Negative reaction request");
    if (extent > reaction.available_extent(r, inventory))
        throw std::domain_error("Insufficient reagents; no reaction or work exchange committed");
    if (extent == 0 && (work_in != 0 || work_out != 0))
        throw std::invalid_argument("Zero-extent reaction cannot exchange work");
    auto next = inventory;
    for (const auto& term : reaction.terms) {
        const double delta = term.moles * extent * r[term.species].molar_mass;
        // Multiplication order can differ from available_extent; bound only a
        // roundoff-sized reagent decrement, never a physical negative inventory.
        if (delta < 0 && -delta > next.masses[term.species]) {
            if (-delta - next.masses[term.species] > 8 * std::numeric_limits<double>::epsilon() * -delta)
                throw std::domain_error("Insufficient reagent");
            next.masses[term.species] = 0;
        } else next.masses[term.species] += delta;
    }
    next.energy += work_in - work_out;
    next.validate(r);
    inventory = std::move(next);
    return {extent, work_in, work_out};
}

std::vector<ReactionDefinition> standard_reactions(const SpeciesRegistry& r) {
    const auto term = [&](const char* id, double n) { return ReactionTerm{r.index(id), n}; };
    std::vector<ReactionDefinition> result{
        {"electrolysis", {term("water", -2), term("hydrogen", 2), term("oxygen", 1)}},
        {"hydrogen_combustion", {term("hydrogen", -2), term("oxygen", -1), term("steam", 2)}},
        {"methane_combustion", {term("methane", -1), term("oxygen", -2), term("carbon_dioxide", 1), term("steam", 2)}},
        {"carbon_combustion", {term("carbon", -1), term("oxygen", -1), term("carbon_dioxide", 1)}},
        {"glucose_metabolism", {term("glucose", -1), term("oxygen", -6), term("carbon_dioxide", 6), term("water", 6)}},
        {"photosynthesis", {term("carbon_dioxide", -6), term("water", -6), term("glucose", 1), term("oxygen", 6)}}
    };
    for (const auto& reaction : result) reaction.validate(r);
    return result;
}
}
