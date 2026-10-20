// Catalogue control (docs/material-catalogue.md). The element records below
// are made-up round numbers, not game data.
#include "../src/oni_catalogue.hpp"
#include "mixed_atmospheres/mac_phase_change.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ma;
using namespace ma::oni;
using namespace ma::experimental;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
template<class F> void rejects(F&& f, const char* text) {
    try { f(); } catch (const std::invalid_argument& e) { if (std::strstr(e.what(), text)) return; throw std::runtime_error(std::string("wrong rejection: ") + e.what()); }
    throw std::runtime_error(std::string("not rejected: ") + text);
}
GameFluidElement gas(const char* id, std::int32_t hash, std::uint32_t index, double molar, double cv, std::int32_t low = 0, double low_t = 10) {
    GameFluidElement e; e.id = id; e.hash = hash; e.native_index = index; e.gas = true; e.molar_mass_kg_per_mol = molar;
    e.specific_heat_j_per_kg_k = cv; e.conductivity_w_per_m_k = .02; e.low_temperature_k = low_t; e.high_temperature_k = 1e4; e.low_target = low;
    return e;
}
GameFluidElement liquid(const char* id, std::int32_t hash, std::uint32_t index, double molar, double c, double density, double high_t, std::int32_t high = 0) {
    GameFluidElement e; e.id = id; e.hash = hash; e.native_index = index; e.gas = false; e.molar_mass_kg_per_mol = molar;
    e.specific_heat_j_per_kg_k = c; e.conductivity_w_per_m_k = .5; e.density_kg_per_m3 = density; e.low_temperature_k = 200;
    e.high_temperature_k = high_t; e.high_target = high;
    return e;
}
std::vector<GameFluidElement> sample() {
    return {
        liquid("Water", 11, 7, .018, 4000, 1000, 375, 12),
        gas("Steam", 12, 3, .018, 2000, 11, 375),
        gas("Oxygen", 13, 9, .016, 1000),
        liquid("DirtyWater", 14, 4, .02, 4000, 1000, 390, 12), // boils to steam, which condenses to water: no pair
        liquid("CrudeOil", 15, 5, .3, 1700, 900, 650),
        liquid("MoltenUnobtainium", 16, 20, .1, 800, 5000, 3000),
    };
}
}

int main() {
    try {
        const auto c = build_catalogue(sample());
        require(c.registry.size() == 6, "catalogue size");
        // Registry follows native element order.
        require(c.registry[0].id == "Steam" && c.registry[5].id == "MoltenUnobtainium", "registry order");
        const auto water = c.species_of_native(7), steam = c.species_of_native(3), oxygen = c.species_of_hash(13);
        require(c.pairs.size() == 1 && c.pairs[0].liquid == water && c.pairs[0].vapour == steam, "saturation pair");
        require(!c.has_native(8) && c.has_native(20), "native index map");
        // Game specific heat is the constant-volume capacity; molar mass is explicit.
        require(std::abs(c.registry[oxygen].cv() - 1000) <= 1e-12 * 1000 && c.registry[oxygen].molar_mass == .016, "gas cv and molar mass");
        require(c.registry[water].molar_mass == c.registry[steam].molar_mass, "pair molar mass");
        // Latent heat at the game's boiling point is the reference value, and
        // the pair is at saturation there at one atmosphere.
        const double latent = mac_latent_heat(*c.eos, water, steam, 375, catalogue_reference_pressure_pa);
        const double saturation = mac_saturation_pressure(*c.eos, water, steam, 375, catalogue_reference_pressure_pa);
        std::cout << "latent " << latent << " J/kg, saturation at 375 K " << saturation << " Pa\n";
        require(std::abs(latent - 2257e3) <= 1e-9 * 2257e3, "calibrated latent heat");
        require(std::abs(saturation - catalogue_reference_pressure_pa) <= 1e-6 * catalogue_reference_pressure_pa, "calibrated saturation");
        require(mac_saturation_pressure(*c.eos, water, steam, 350, catalogue_reference_pressure_pa) < .6 * saturation, "pressure-dependent boiling");
        // Reference rows and surrogates.
        const auto& oil = c.materials[c.species_of_hash(15)].reference;
        const auto& unknown = c.materials[c.species_of_hash(16)].reference;
        require(c.materials[water].reference.bulk_modulus_pa == 2.2e9 && c.materials[water].reference.miscibility_group == 1, "water row");
        require(oil.bulk_modulus_pa == 1.5e9 && oil.surrogate_latent && std::abs(oil.latent_heat_j_per_kg - 88 * 650 / .3) < 1e-6, "oil row and Trouton");
        require(unknown.surrogate_bulk && unknown.bulk_modulus_pa == 100e9 && unknown.miscibility_group == 0, "molten surrogate");
        require(c.materials[c.species_of_hash(14)].reference.miscibility_group == 1, "aqueous group");
        // A cell of each phase evaluates.
        std::vector<double> masses(c.registry.size()); masses[water] = 400; masses[oxygen] = .5;
        const auto state = c.eos->evaluate(c.eos->initialize_at_temperature(masses, 300, 1).mixture, 1);
        require(state.has_gas && state.temperature_k > 299.999 && state.temperature_k < 300.001, "mixed cell");
        // Rejections.
        rejects([] { auto e = sample(); e[1].native_index = 7; (void)build_catalogue(e); }, "Duplicate");
        rejects([] { auto e = sample(); e[2].specific_heat_j_per_kg_k = 0; (void)build_catalogue(e); }, "Invalid game properties");
        rejects([] { auto e = sample(); e[0].density_kg_per_m3 = -1; (void)build_catalogue(e); }, "Invalid game properties");
        rejects([] { (void)build_catalogue({}); }, "1..4096");
        std::cout << "CATALOGUE PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "CATALOGUE FAIL: " << e.what() << '\n';
        return 1;
    }
}
