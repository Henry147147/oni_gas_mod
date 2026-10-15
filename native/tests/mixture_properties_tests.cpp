#include "mixed_atmospheres/mixture.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace {
// Deliberately dense scalar reference folds from the constant-property model.
// These exercise every species, including zero masses. Compatibility requires
// the same binary64 results and rejection behavior after sparse optimizations.
namespace reference {
double capacity(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry) {
    if (inventory.masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double value = 0;
    for (std::size_t i = 0; i < inventory.masses.size(); ++i) value += inventory.masses[i] * registry[i].cv();
    return value;
}
double temperature(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry) {
    const double heat_capacity = capacity(inventory, registry);
    if (heat_capacity == 0) return 0;
    double zero_energy = 0;
    for (std::size_t i = 0; i < inventory.masses.size(); ++i)
        zero_energy += inventory.masses[i] * registry[i].internal_energy(0);
    return (inventory.energy - zero_energy) / heat_capacity;
}
double condensed(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry) {
    if (inventory.masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double value = 0;
    for (std::size_t i = 0; i < inventory.masses.size(); ++i)
        if (registry[i].phase != ma::Phase::gas) value += inventory.masses[i] / registry[i].density;
    return value;
}
double moles(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry) {
    if (inventory.masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    double value = 0;
    for (std::size_t i = 0; i < inventory.masses.size(); ++i)
        if (registry[i].phase == ma::Phase::gas) value += inventory.masses[i] / registry[i].molar_mass;
    return value;
}
double pressure(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry, double volume) {
    ma::require_finite(volume, "volume");
    if (volume <= 0) throw std::invalid_argument("Volume must be positive");
    const double headspace = volume - condensed(inventory, registry);
    if (headspace <= 0) throw std::domain_error("No gas headspace: condensed-phase pressure requires the liquid solver");
    const double value = moles(inventory, registry) * ma::gas_constant * temperature(inventory, registry) / headspace;
    ma::require_finite(value, "pressure");
    return value;
}
double validate(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry) {
    if (inventory.masses.size() != registry.size()) throw std::invalid_argument("Registry/inventory size mismatch");
    ma::require_finite(inventory.energy, "inventory energy");
    for (double mass : inventory.masses) {
        ma::require_finite(mass, "component mass");
        if (mass < 0) throw std::invalid_argument("Negative component mass");
    }
    const double total = std::accumulate(inventory.masses.begin(), inventory.masses.end(), 0.0);
    if (!std::isfinite(total)) throw std::invalid_argument("Inventory mass overflow");
    if (total == 0) {
        if (inventory.energy != 0) throw std::invalid_argument("Empty inventory cannot hold thermal energy");
    } else {
        const double t = temperature(inventory, registry);
        if (!std::isfinite(t) || t <= 0) throw std::domain_error("Inventory has nonpositive temperature");
    }
    return 1;
}
}

struct Outcome { std::uint64_t bits; std::string type, message; };
template<class F> Outcome record(F&& operation) {
    try { return {std::bit_cast<std::uint64_t>(operation()), "", ""}; }
    catch (const std::exception& error) { return {0, typeid(error).name(), error.what()}; }
}
std::size_t comparisons = 0;
template<class F, class G> void same(const char* name, F&& expected, G&& actual) {
    const auto a = record(expected), b = record(actual);
    ++comparisons;
    if (a.bits != b.bits || a.type != b.type || a.message != b.message)
        throw std::runtime_error(std::string("Binary64/exception mismatch in ") + name +
            "; expected bits=" + std::to_string(a.bits) + ", actual=" + std::to_string(b.bits) +
            "; expected error=" + a.message + ", actual=" + b.message);
}
void compare(const ma::MixtureInventory& inventory, const ma::SpeciesRegistry& registry) {
    same("heat capacity", [&] { return reference::capacity(inventory, registry); }, [&] { return inventory.heat_capacity(registry); });
    same("temperature", [&] { return reference::temperature(inventory, registry); }, [&] { return inventory.temperature(registry); });
    same("condensed volume", [&] { return reference::condensed(inventory, registry); }, [&] { return inventory.condensed_volume(registry); });
    same("gas moles", [&] { return reference::moles(inventory, registry); }, [&] { return inventory.gas_moles(registry); });
    same("pressure", [&] { return reference::pressure(inventory, registry, 1); }, [&] { return inventory.pressure(registry, 1); });
    same("validation", [&] { return reference::validate(inventory, registry); }, [&] { inventory.validate(registry); return 1.0; });
}
void dense_sparse_and_reference_energy_cases() {
    auto registry = ma::SpeciesRegistry::standard();
    // Opposite-sign reference energies exercise cancellation as well as the
    // standard registry's cryogenic phase-reference alignment after add().
    registry.add({"positive-u0", "Positive reference-energy gas", ma::Phase::gas, {{"He", 1}}, 0, 6000, 0, 1, 1e6, 1});
    std::uint64_t state = 0x1e8eb64d9512;
    const auto next = [&] { state = state * 6364136223846793005ULL + 1442695040888963407ULL; return state; };
    for (std::size_t sample = 0; sample < 20000; ++sample) {
        ma::MixtureInventory inventory(registry.size());
        const double t = 1 + static_cast<double>(next() % 400000) / 100;
        for (std::size_t i = 0; i < registry.size(); ++i) {
            const auto random = next();
            if (sample % 3 == 0 || random % 4 == 0)
                inventory.masses[i] = std::ldexp(0.5 + static_cast<double>(random % 1000) / 2000,
                    static_cast<int>(random % 1700) - 1000);
            if (sample % 17 == 0 && i == sample % registry.size())
                inventory.masses[i] = std::numeric_limits<double>::denorm_min();
            inventory.energy += inventory.masses[i] * registry[i].internal_energy(t);
        }
        compare(inventory, registry);
    }
}
void invalid_inputs_and_signed_zeros_keep_their_contract() {
    const auto registry = ma::SpeciesRegistry::standard();
    for (double mass : {0.0, -0.0, -1.0, -std::numeric_limits<double>::denorm_min(),
            std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max()}) {
        for (double energy : {0.0, -0.0, 1.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            ma::MixtureInventory inventory(registry.size()); inventory.masses[0] = mass; inventory.energy = energy;
            compare(inventory, registry);
        }
    }
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, registry.size() + 1}) compare(ma::MixtureInventory(size), registry);
    for (double volume : {-1.0, 0.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        const ma::MixtureInventory inventory(registry.size());
        same("invalid volume", [&] { return reference::pressure(inventory, registry, volume); }, [&] { return inventory.pressure(registry, volume); });
    }
    compare(ma::MixtureInventory(), ma::SpeciesRegistry());
}
void public_mutations_are_observed_and_subnormal_traces_are_kept() {
    const auto registry = ma::SpeciesRegistry::standard();
    ma::MixtureInventory inventory(registry.size());
    inventory.masses[registry.index("helium")] = std::numeric_limits<double>::denorm_min();
    inventory.set_temperature(registry, 300);
    compare(inventory, registry);
    if (inventory.gas_moles(registry) <= 0 || inventory.heat_capacity(registry) <= 0)
        throw std::runtime_error("Minimum positive gas mass was treated as zero");
    inventory.masses[registry.index("nitrogen")] = 1;
    inventory.set_temperature(registry, 320);
    compare(inventory, registry);
    inventory.energy += 123.5;
    compare(inventory, registry);
    inventory.masses[registry.index("water")] = 0.125;
    compare(inventory, registry);
}
}

int main() {
    try {
        dense_sparse_and_reference_energy_cases();
        invalid_inputs_and_signed_zeros_keep_their_contract();
        public_mutations_are_observed_and_subnormal_traces_are_kept();
        std::cout << "PASS " << comparisons << " bit-exact property/exception comparisons: dense/sparse/reference energy, "
                  << "signed zeros, subnormal traces, public mutations, overflow and invalid domains\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << comparisons << " comparisons: " << error.what() << '\n';
        return 1;
    }
}
