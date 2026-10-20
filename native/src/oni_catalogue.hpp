#pragma once
// Base-game fluid catalogue (docs/material-catalogue.md). Values the game has
// (molar mass, specific heat, conductivity, liquid density, transitions) are
// read from the installed game at runtime and passed in; they are never
// committed. Values it lacks (latent heat, compressibility, viscosity,
// miscibility) come from the committed reference table or a documented
// surrogate rule.
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "mixed_atmospheres/species.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ma::oni {
// One gas or liquid element as the running game defines it.
struct GameFluidElement {
    std::string id;                 // element id (SimHashes name)
    std::int32_t hash = 0;          // SimHashes value
    std::uint32_t native_index = 0; // element table index
    bool gas = true;
    double molar_mass_kg_per_mol = 0;
    double specific_heat_j_per_kg_k = 0;
    double conductivity_w_per_m_k = 0;
    double density_kg_per_m3 = 0;   // liquids: the game's full-cell mass in one cubic metre
    double low_temperature_k = 0, high_temperature_k = 0;
    std::int32_t low_target = 0, high_target = 0;       // transition element hashes, 0 if none
    std::int32_t low_byproduct = 0, high_byproduct = 0; // transition ore hashes, 0 if none
    double low_byproduct_fraction = 0, high_byproduct_fraction = 0;
};
// Committed reference data or a surrogate (surrogate == true names the rule).
struct ReferenceProperties {
    double latent_heat_j_per_kg = 0;  // liquids: vaporization at the game's boiling point
    double bulk_modulus_pa = 0;       // liquids
    double viscosity_pa_s = 0;        // informational until per-material viscosity exists
    std::uint32_t miscibility_group = 0; // liquids sharing a nonzero group form one phase
    std::string source;
    bool surrogate_latent = false, surrogate_bulk = false;
};
enum class MiscibilityGroup : std::uint32_t { none = 0, aqueous = 1, hydrocarbon = 2, light_hydrocarbon = 3 };
// A liquid and the vapour it boils to and condenses from (the game's
// transitions point at each other). The vapour's energy offset makes the
// latent heat at the game's boiling point equal the catalogue value, and its
// entropy offset puts the pair at saturation there at one atmosphere.
struct CatalogueSaturationPair { std::size_t liquid = 0, vapour = 0; double boiling_k = 0, latent_heat_j_per_kg = 0; };
struct CatalogueMaterial { GameFluidElement game; ReferenceProperties reference; };
inline constexpr double catalogue_reference_pressure_pa = 101325;
inline constexpr char catalogue_profile[] = "oni-colony-catalogue-v1";
struct Catalogue {
    std::vector<CatalogueMaterial> materials; // registry order
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> eos;
    std::vector<CatalogueSaturationPair> pairs;
    [[nodiscard]] std::size_t species_of_native(std::uint32_t native_index) const; // throws if absent
    [[nodiscard]] bool has_native(std::uint32_t native_index) const noexcept;
    [[nodiscard]] std::size_t species_of_hash(std::int32_t hash) const;
    std::vector<std::uint32_t> native_to_species; // UINT32_MAX where the element is not a catalogue fluid
};
// The committed reference row for an element id, or the surrogate rule.
[[nodiscard]] ReferenceProperties reference_properties(const GameFluidElement&);
// Validates every record (finite positive properties, unique ids, indices
// and hashes) and builds the registry and EOS. Throws std::invalid_argument.
[[nodiscard]] Catalogue build_catalogue(std::vector<GameFluidElement> elements);
}
