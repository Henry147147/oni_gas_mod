#include "oni_catalogue.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace ma::oni {
namespace {
using namespace experimental;
void require(bool ok, const std::string& why) { if (!ok) throw std::invalid_argument(why); }
bool positive(double v) { return std::isfinite(v) && v > 0; }

// Liquid classes for surrogate compressibility (docs/material-catalogue.md).
enum class LiquidClass { aqueous, organic, cryogenic, molten_nonmetal, molten_metal };
constexpr double class_bulk_modulus(LiquidClass c) {
    switch (c) {
    case LiquidClass::aqueous: return 2.2e9;
    case LiquidClass::organic: return 1.3e9;
    case LiquidClass::cryogenic: return 0.5e9;
    case LiquidClass::molten_nonmetal: return 15e9;
    case LiquidClass::molten_metal: return 100e9;
    }
    return 2.2e9;
}
// Committed public reference data. Latent heat (kJ/kg) is vaporization at the
// normal boiling point; bulk modulus (GPa) is the isentropic modulus rho c^2
// from published sound speeds or a tabulated modulus near the liquid's usual
// temperature. Zero means "use the surrogate rule". Viscosity is Pa s.
struct Row {
    std::string_view id; double latent_kj_per_kg, bulk_gpa, viscosity_pa_s; MiscibilityGroup group; LiquidClass type; std::string_view source;
    double critical_k = 0; // critical temperature; zero: Guldberg's rule
};
constexpr std::string_view nist = "NIST Chemistry WebBook, SRD 69 (enthalpy of vaporization; fluid sound speed and density)";
constexpr std::string_view toolbox = "Engineering ToolBox, Bulk Modulus and Fluid Elasticity; Liquids - Dynamic Viscosities";
constexpr std::string_view crc = "CRC Handbook of Chemistry and Physics, 97th ed. (enthalpies of vaporization of the elements)";
constexpr std::string_view melts = "Iida & Guthrie, The Thermophysical Properties of Metallic Liquids (2015) (sound speed and density at melting)";
constexpr std::string_view silicate = "Lange & Carmichael, Rev. Mineral. 24 (1990) (silicate melt compressibility, about 15 GPa)";
constexpr std::string_view fictional = "fictional game material: documented surrogate";
// Critical temperatures: NIST Chemistry WebBook and CRC Handbook (fluid data).
// Without a row, Guldberg's rule: about 1.6 times the normal boiling point.
constexpr double guldberg_ratio = 1.6;
constexpr Row rows[] = {
    {"Water", 2257, 2.2, 1.0e-3, MiscibilityGroup::aqueous, LiquidClass::aqueous, nist, 647.1},
    {"DirtyWater", 2257, 2.2, 1.0e-3, MiscibilityGroup::aqueous, LiquidClass::aqueous, nist, 647.1},
    {"SaltWater", 2257, 2.34, 1.1e-3, MiscibilityGroup::aqueous, LiquidClass::aqueous, toolbox, 647.1},
    {"Brine", 2257, 2.6, 1.5e-3, MiscibilityGroup::aqueous, LiquidClass::aqueous, toolbox, 647.1},
    {"Ethanol", 841, 1.06, 1.1e-3, MiscibilityGroup::aqueous, LiquidClass::organic, nist, 513.9},
    {"CrudeOil", 0, 1.5, 5e-2, MiscibilityGroup::hydrocarbon, LiquidClass::organic, toolbox},
    {"Petroleum", 0, 1.3, 1.6e-3, MiscibilityGroup::hydrocarbon, LiquidClass::organic, toolbox},
    {"Naphtha", 0, 1.0, 5e-4, MiscibilityGroup::hydrocarbon, LiquidClass::organic, toolbox},
    {"LiquidCarbonDioxide", 347, 1.0, 2.4e-4, MiscibilityGroup::none, LiquidClass::cryogenic, nist, 304.1},
    {"LiquidOxygen", 213, 0.92, 1.9e-4, MiscibilityGroup::none, LiquidClass::cryogenic, nist, 154.6},
    {"LiquidHydrogen", 446, 0.086, 1.3e-5, MiscibilityGroup::none, LiquidClass::cryogenic, nist, 33.2},
    {"LiquidMethane", 510, 0.76, 1.2e-4, MiscibilityGroup::light_hydrocarbon, LiquidClass::cryogenic, nist, 190.6},
    {"LiquidPropane", 428, 0.77, 2e-4, MiscibilityGroup::light_hydrocarbon, LiquidClass::cryogenic, nist, 369.8},
    {"LiquidHelium", 20.7, 0.004, 3.6e-6, MiscibilityGroup::none, LiquidClass::cryogenic, nist, 5.2},
    {"Chlorine", 288, 1.1, 3.4e-4, MiscibilityGroup::none, LiquidClass::cryogenic, nist, 417.0},
    {"LiquidSulfur", 0, 0, 1e-2, MiscibilityGroup::none, LiquidClass::molten_nonmetal, crc, 1314},
    {"LiquidPhosphorus", 0, 0, 2e-3, MiscibilityGroup::none, LiquidClass::molten_nonmetal, crc, 994},
    {"Magma", 0, 15, 100, MiscibilityGroup::none, LiquidClass::molten_nonmetal, silicate},
    {"MoltenGlass", 0, 15, 10, MiscibilityGroup::none, LiquidClass::molten_nonmetal, silicate},
    {"MoltenSalt", 2910, 4.5, 1e-3, MiscibilityGroup::none, LiquidClass::molten_nonmetal, crc},
    {"MoltenCarbon", 29600, 0, 5e-3, MiscibilityGroup::none, LiquidClass::molten_nonmetal, crc},
    {"MoltenIron", 6090, 101, 5.5e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenSteel", 6090, 101, 5.5e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenCopper", 4730, 95, 4e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenGold", 1700, 114, 5e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenAluminum", 10900, 52, 1.3e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenTungsten", 4480, 0, 7e-3, MiscibilityGroup::none, LiquidClass::molten_metal, crc},
    {"MoltenNiobium", 7430, 0, 4.5e-3, MiscibilityGroup::none, LiquidClass::molten_metal, crc},
    {"MoltenLead", 858, 34, 2.1e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenCobalt", 6400, 0, 4.5e-3, MiscibilityGroup::none, LiquidClass::molten_metal, crc},
    {"MoltenNickel", 6440, 0, 4.9e-3, MiscibilityGroup::none, LiquidClass::molten_metal, crc},
    {"MoltenZinc", 1760, 51, 3.9e-3, MiscibilityGroup::none, LiquidClass::molten_metal, melts},
    {"MoltenIridium", 2930, 0, 7e-3, MiscibilityGroup::none, LiquidClass::molten_metal, crc},
    {"Mercury", 295, 28.5, 1.5e-3, MiscibilityGroup::none, LiquidClass::molten_metal, toolbox, 1750},
    {"SuperCoolant", 0, 0, 1e-3, MiscibilityGroup::none, LiquidClass::aqueous, fictional},
    {"ViscoGel", 0, 0, 10, MiscibilityGroup::none, LiquidClass::organic, fictional},
};
const Row* row_of(std::string_view id) {
    for (const auto& row : rows) if (row.id == id) return &row;
    return nullptr;
}
// Surrogate class for a liquid with no reference row: by name, else organic.
LiquidClass guess_class(std::string_view id) {
    if (id.find("Molten") == 0) return LiquidClass::molten_metal;
    if (id.find("Liquid") == 0) return LiquidClass::cryogenic;
    if (id.find("Water") != std::string_view::npos || id.find("Brine") != std::string_view::npos || id.find("Milk") != std::string_view::npos)
        return LiquidClass::aqueous;
    return LiquidClass::organic;
}
constexpr double trouton_j_per_mol_k = 88; // Trouton's rule, molar entropy of vaporization
}

ReferenceProperties reference_properties(const GameFluidElement& element) {
    ReferenceProperties r;
    if (element.gas) { r.source = "ideal gas from game values"; return r; }
    const auto* row = row_of(element.id);
    const auto type = row ? row->type : guess_class(element.id);
    r.miscibility_group = row ? static_cast<std::uint32_t>(row->group) : 0;
    r.viscosity_pa_s = row ? row->viscosity_pa_s : 1e-3;
    r.source = row ? std::string(row->source) : "no reference row: class surrogates";
    if (row && row->bulk_gpa > 0) r.bulk_modulus_pa = row->bulk_gpa * 1e9;
    else { r.bulk_modulus_pa = class_bulk_modulus(type); r.surrogate_bulk = true; }
    r.critical_temperature_k = row && row->critical_k > 0 ? row->critical_k : guldberg_ratio * element.high_temperature_k;
    if (row && row->latent_kj_per_kg > 0) r.latent_heat_j_per_kg = row->latent_kj_per_kg * 1e3;
    else {
        // Trouton's rule at the game's boiling point and molar mass.
        r.latent_heat_j_per_kg = positive(element.high_temperature_k) && positive(element.molar_mass_kg_per_mol) ?
            trouton_j_per_mol_k * element.high_temperature_k / element.molar_mass_kg_per_mol : 0;
        r.surrogate_latent = true;
    }
    return r;
}

std::size_t Catalogue::species_of_native(std::uint32_t native_index) const {
    if (!has_native(native_index)) throw std::out_of_range("Element is not a catalogue fluid");
    return native_to_species[native_index];
}
bool Catalogue::has_native(std::uint32_t native_index) const noexcept {
    return native_index < native_to_species.size() && native_to_species[native_index] != std::numeric_limits<std::uint32_t>::max();
}
std::size_t Catalogue::species_of_hash(std::int32_t hash) const {
    for (std::size_t i = 0; i < materials.size(); ++i) if (materials[i].game.hash == hash) return i;
    throw std::out_of_range("Element hash is not a catalogue fluid");
}

Catalogue build_catalogue(std::vector<GameFluidElement> elements) {
    require(!elements.empty() && elements.size() <= 4096, "A catalogue needs 1..4096 fluid elements");
    std::sort(elements.begin(), elements.end(), [](const auto& a, const auto& b) { return a.native_index < b.native_index; });
    std::set<std::string> ids; std::set<std::int32_t> hashes; std::set<std::uint32_t> indices;
    for (const auto& e : elements) {
        require(!e.id.empty() && e.id.size() <= 64, "Invalid catalogue element id");
        require(ids.insert(e.id).second && hashes.insert(e.hash).second && indices.insert(e.native_index).second,
            "Duplicate catalogue element id, hash or index: " + e.id);
        require(e.native_index < 65535, "Catalogue element index out of range: " + e.id);
        require(positive(e.molar_mass_kg_per_mol) && positive(e.specific_heat_j_per_kg_k) && std::isfinite(e.conductivity_w_per_m_k) &&
            e.conductivity_w_per_m_k >= 0 && (e.gas || positive(e.density_kg_per_m3)), "Invalid game properties for " + e.id);
        require(std::isfinite(e.low_temperature_k) && std::isfinite(e.high_temperature_k), "Invalid transition temperatures for " + e.id);
    }
    Catalogue c;
    std::map<std::int32_t, std::size_t> by_hash;
    for (std::size_t i = 0; i < elements.size(); ++i) by_hash[elements[i].hash] = i;
    // Saturation pairs: a liquid that boils to a gas that condenses back to it.
    std::map<std::size_t, std::size_t> vapour_of, liquid_of;
    for (std::size_t l = 0; l < elements.size(); ++l) {
        const auto& liquid = elements[l];
        if (liquid.gas || !liquid.high_target) continue;
        const auto it = by_hash.find(liquid.high_target);
        if (it == by_hash.end()) continue;
        const auto& vapour = elements[it->second];
        if (!vapour.gas || vapour.low_target != liquid.hash || liquid_of.count(it->second)) continue;
        if (!positive(liquid.high_temperature_k)) continue;
        vapour_of[l] = it->second; liquid_of[it->second] = l;
    }
    for (auto& e : elements) c.materials.push_back({e, reference_properties(e)});
    // Registry: atoms label identity only; a pair shares one label and the
    // vapour's molar mass, so the saturation machinery recognizes it.
    const auto label = [&](std::size_t i) {
        if (liquid_of.count(i)) return "oni:" + elements[liquid_of[i]].id;
        return "oni:" + elements[i].id;
    };
    const auto molar = [&](std::size_t i) {
        return vapour_of.count(i) ? elements[vapour_of[i]].molar_mass_kg_per_mol : elements[i].molar_mass_kg_per_mol;
    };
    std::vector<double> formation(elements.size()); // J/mol; nonzero only for calibrated vapours
    const auto registry_of = [&] {
        SpeciesRegistry r;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const auto& e = elements[i];
            SpeciesDefinition d; d.id = e.id; d.name = e.id; d.phase = e.gas ? Phase::gas : Phase::liquid;
            d.atoms = {{label(i), 1}}; d.molar_mass = molar(i); d.explicit_molar_mass = true;
            // The game's specific heat is the heat content of a fixed-volume
            // cell: it is the constant-volume capacity here (cp = cv + R/M).
            d.cp = e.specific_heat_j_per_kg_k + (e.gas ? gas_constant / d.molar_mass : 0);
            d.density = e.gas ? 0 : e.density_kg_per_m3; d.conductivity = e.conductivity_w_per_m_k;
            d.formation_enthalpy = formation[i];
            d.liquid_phase_group = e.gas ? 0 : c.materials[i].reference.miscibility_group;
            r.add(std::move(d));
        }
        return r;
    };
    const auto materials_of = [&](const SpeciesRegistry& r) {
        std::vector<ReferenceMaterial> out;
        for (std::size_t i = 0; i < r.size(); ++i) {
            const auto& s = r[i]; const auto& ref = c.materials[i].reference;
            ReferenceMaterial m; m.id = s.id;
            m.phase = s.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
            m.molar_mass_kg_per_mol = s.molar_mass; m.cv_j_per_kg_k = s.cv(); m.energy_offset_j_per_kg = s.internal_energy(0);
            m.minimum_temperature_k = 1; m.maximum_temperature_k = 1e9;
            m.zero_pressure_density_kg_per_m3 = s.phase == Phase::gas ? 0 : s.density;
            m.reference_bulk_modulus_pa = s.phase == Phase::gas ? 0 : ref.bulk_modulus_pa;
            m.maximum_density_ratio = s.phase == Phase::gas ? 1 : 2;
            m.provenance = std::string("ONI game values read at runtime; ") + (s.phase == Phase::gas ? "ideal gas" : ref.source);
            out.push_back(std::move(m));
        }
        return out;
    };
    // Calibrate each pair: the vapour's formation enthalpy makes the EOS latent
    // heat at the game's boiling point (one atmosphere) the catalogue value.
    for (const auto& [l, v] : vapour_of)
        require(positive(c.materials[l].reference.latent_heat_j_per_kg), "A saturation pair needs a positive latent heat: " + elements[l].id);
    for (int pass = 0; pass < 3 && !vapour_of.empty(); ++pass) {
        const auto r = registry_of();
        const FluidThermodynamics probe(r, catalogue_profile, materials_of(r));
        for (const auto& [l, v] : vapour_of) {
            const double actual = mac_latent_heat(probe, l, v, elements[l].high_temperature_k, catalogue_reference_pressure_pa);
            formation[v] += r[v].molar_mass * (c.materials[l].reference.latent_heat_j_per_kg - actual);
        }
    }
    c.registry = registry_of();
    auto materials = materials_of(c.registry);
    // The entropy offset places each pair at saturation at that point.
    for (const auto& [l, v] : vapour_of) {
        const double tb = elements[l].high_temperature_k;
        materials[v].entropy_offset_j_per_kg_k = mac_saturation_entropy_offset(materials[l], materials[v], tb, catalogue_reference_pressure_pa);
        require(std::isfinite(materials[v].entropy_offset_j_per_kg_k) && materials[v].entropy_offset_j_per_kg_k != 0,
            "Saturation calibration failed for " + elements[l].id);
        c.pairs.push_back({l, v, tb, c.materials[l].reference.latent_heat_j_per_kg, c.materials[l].reference.critical_temperature_k});
    }
    c.eos = std::make_shared<const FluidThermodynamics>(c.registry, catalogue_profile, std::move(materials));
    const auto detected = mac_saturation_pairs(c.registry, *c.eos);
    require(detected.size() == c.pairs.size(), "Catalogue saturation pairs differ from the EOS pairing");
    std::uint32_t maximum_index = 0;
    for (const auto& e : elements) maximum_index = std::max(maximum_index, e.native_index);
    c.native_to_species.assign(maximum_index + 1, std::numeric_limits<std::uint32_t>::max());
    for (std::size_t i = 0; i < elements.size(); ++i) c.native_to_species[elements[i].native_index] = static_cast<std::uint32_t>(i);
    return c;
}
}
