#include "mixed_atmospheres/species.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ma {
void require_finite(double value, const char* name) {
    if (!std::isfinite(value)) throw std::invalid_argument(std::string(name) + " must be finite");
}

double molecular_mass(const Composition& atoms) {
    static const std::map<std::string, double> weights{
        {"H", 0.00100794}, {"C", 0.0120107}, {"N", 0.0140067},
        {"O", 0.0159994}, {"Cl", 0.035453}, {"He", 0.004002602},
        {"Na", 0.02298976928}, {"S", 0.032065}, {"Si", 0.0280855}};
    double result = 0.0;
    if (atoms.empty()) throw std::invalid_argument("A species needs an elemental composition");
    for (const auto& [element, count] : atoms) {
        require_finite(count, "atom count");
        if (count <= 0) throw std::invalid_argument("Atom counts must be positive");
        const auto it = weights.find(element);
        if (it == weights.end()) throw std::invalid_argument("Unknown atomic mass: " + element);
        result += count * it->second;
    }
    require_finite(result, "molecular mass");
    if (result <= 0) throw std::invalid_argument("Molecular mass must be positive");
    return result;
}

double SpeciesDefinition::cv() const {
    return cp - (phase == Phase::gas ? gas_constant / molar_mass : 0.0);
}

double SpeciesDefinition::internal_energy(double temperature) const {
    return formation_enthalpy / molar_mass + cv() * (temperature - reference_temperature)
        - (phase == Phase::gas ? gas_constant * reference_temperature / molar_mass : 0.0);
}

double SpeciesDefinition::enthalpy(double temperature) const {
    return formation_enthalpy / molar_mass + cp * (temperature - reference_temperature);
}

std::size_t SpeciesRegistry::add(SpeciesDefinition definition) {
    if (definition.id.empty() || definition.id.size() > 256) throw std::invalid_argument("Invalid species ID");
    if (definition.phase != Phase::gas && definition.phase != Phase::liquid && definition.phase != Phase::solid)
        throw std::invalid_argument("Invalid species phase");
    for (const auto& item : species_) {
        if (item.id == definition.id) throw std::invalid_argument("Duplicate species ID: " + definition.id);
    }
    if (!definition.explicit_molar_mass) definition.molar_mass = molecular_mass(definition.atoms);
    else if (definition.atoms.empty() || !std::isfinite(definition.molar_mass) || definition.molar_mass <= 0)
        throw std::invalid_argument("An explicit molar mass needs an identity composition and a positive finite value");
    require_finite(definition.cp, "heat capacity");
    require_finite(definition.density, "density");
    require_finite(definition.conductivity, "conductivity");
    require_finite(definition.formation_enthalpy, "formation enthalpy");
    require_finite(definition.max_mole_fraction, "hazard limit");
    require_finite(definition.cv(), "constant-volume heat capacity");
    require_finite(definition.internal_energy(0), "reference internal energy");
    if (definition.cv() <= 0 || definition.conductivity < 0 ||
        (definition.phase != Phase::gas && definition.density <= 0) ||
        definition.max_mole_fraction < 0 || definition.max_mole_fraction > 1)
        throw std::invalid_argument("Invalid species properties: " + definition.id);
    species_.push_back(std::move(definition));
    return species_.size() - 1;
}

std::size_t SpeciesRegistry::index(std::string_view id) const {
    for (std::size_t i = 0; i < species_.size(); ++i) if (species_[i].id == id) return i;
    throw std::out_of_range("Unknown species: " + std::string(id));
}

const SpeciesDefinition& SpeciesRegistry::operator[](std::size_t i) const { return species_.at(i); }

SpeciesRegistry SpeciesRegistry::standard() {
    SpeciesRegistry r;
    const auto add = [&](std::string id, std::string name, Phase phase, Composition atoms,
                         double cp, double density, double k, double hf, double hazard = 1.0) {
        r.add({std::move(id), std::move(name), phase, std::move(atoms), 0, cp, density, k, hf, hazard});
    };
    add("oxygen", "Oxygen", Phase::gas, {{"O", 2}}, 918.0, 0, 0.0263, 0);
    add("nitrogen", "Nitrogen", Phase::gas, {{"N", 2}}, 1040.0, 0, 0.0258, 0);
    add("carbon_dioxide", "Carbon dioxide", Phase::gas, {{"C", 1}, {"O", 2}}, 844.0, 0, 0.0166, -393510.0);
    add("hydrogen", "Hydrogen", Phase::gas, {{"H", 2}}, 14304.0, 0, 0.1805, 0, 0.01);
    add("steam", "Water vapor", Phase::gas, {{"H", 2}, {"O", 1}}, 1864.0, 0, 0.025, -241826.0);
    add("methane", "Methane", Phase::gas, {{"C", 1}, {"H", 4}}, 2226.0, 0, 0.034, -74873.0, 0.01);
    add("chlorine", "Chlorine", Phase::gas, {{"Cl", 2}}, 479.0, 0, 0.0089, 0, 0.000001);
    add("helium", "Helium", Phase::gas, {{"He", 1}}, 5193.0, 0, 0.151, 0);
    add("water", "Water", Phase::liquid, {{"H", 2}, {"O", 1}}, 4184.0, 997.0, 0.6, -285830.0);
    add("liquid_nitrogen", "Liquid nitrogen", Phase::liquid, {{"N", 2}}, 2040.0, 808.0, 0.14, 0);
    add("liquid_oxygen", "Liquid oxygen", Phase::liquid, {{"O", 2}}, 1700.0, 1141.0, 0.15, 0);
    add("ice", "Ice", Phase::solid, {{"H", 2}, {"O", 1}}, 2050.0, 917.0, 2.2, 0);
    add("solid_nitrogen", "Solid nitrogen", Phase::solid, {{"N", 2}}, 1500.0, 1026.0, 0.2, 0);
    add("solid_oxygen", "Solid oxygen", Phase::solid, {{"O", 2}}, 1300.0, 1426.0, 0.2, 0);
    add("carbon", "Carbon", Phase::solid, {{"C", 1}}, 709.0, 2260.0, 1.0, 0);
    add("glucose", "Glucose surrogate", Phase::solid, {{"C", 6}, {"H", 12}, {"O", 6}}, 1250.0, 1540.0, 0.2, -1273300.0);

    // Match phase reference enthalpies at the measured transition point. The
    // constant-cp extrapolation is a reference model, not a cryogenic EOS table.
    const auto align = [&](const char* condensed, const char* warm, double t, double latent) {
        auto& c = r.species_.at(r.index(condensed));
        const auto& w = r[r.index(warm)];
        c.formation_enthalpy = (w.enthalpy(t) - latent - c.cp * (t - reference_temperature)) * c.molar_mass;
    };
    align("liquid_nitrogen", "nitrogen", 77.355, 199000.0);
    align("liquid_oxygen", "oxygen", 90.188, 213000.0);
    align("ice", "water", 273.15, 333550.0);
    align("solid_nitrogen", "liquid_nitrogen", 63.15, 25700.0);
    align("solid_oxygen", "liquid_oxygen", 54.36, 13900.0);
    return r;
}
}
