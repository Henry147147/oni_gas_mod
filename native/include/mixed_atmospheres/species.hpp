#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ma {
inline constexpr double gas_constant = 8.31446261815324;
inline constexpr double reference_temperature = 298.15;

enum class Phase { gas, liquid, solid };
using Composition = std::map<std::string, double>;

struct SpeciesDefinition {
    std::string id;
    std::string name;
    Phase phase = Phase::gas;
    Composition atoms;
    double molar_mass = 0.0;         // kg/mol
    double cp = 0.0;                 // J/(kg K), constant-property reference model
    double density = 0.0;            // kg/m^3 for condensed phases
    double conductivity = 0.0;       // W/(m K)
    double formation_enthalpy = 0.0; // J/mol at 298.15 K
    double max_mole_fraction = 1.0;  // configured gameplay contamination limit
    // Keep molar_mass as given instead of deriving it from atoms. Catalogue
    // materials read from the game use this; atoms then only label identity.
    bool explicit_molar_mass = false;

    [[nodiscard]] double cv() const;
    [[nodiscard]] double internal_energy(double temperature) const; // J/kg
    [[nodiscard]] double enthalpy(double temperature) const;        // J/kg
};

class SpeciesRegistry {
public:
    std::size_t add(SpeciesDefinition definition);
    [[nodiscard]] std::size_t index(std::string_view id) const;
    [[nodiscard]] const SpeciesDefinition& operator[](std::size_t index) const;
    [[nodiscard]] std::size_t size() const noexcept { return species_.size(); }
    [[nodiscard]] const std::vector<SpeciesDefinition>& all() const noexcept { return species_; }
    [[nodiscard]] static SpeciesRegistry standard();
private:
    std::vector<SpeciesDefinition> species_;
};

[[nodiscard]] double molecular_mass(const Composition& atoms);
void require_finite(double value, const char* name);
}
