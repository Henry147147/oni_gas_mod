#include "mixed_atmospheres/coupled_api.h"
#include "mixed_atmospheres/mixture.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void ok(ma_status status) { if (status != MA_OK) throw std::runtime_error(ma_coupled_last_error()); }
void near(double a, double b, double scale, double tolerance = 1e-10) {
    check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance * scale, "Independent coupled comparison failed");
}
using Owner = std::unique_ptr<ma_coupled_world, decltype(&ma_coupled_destroy)>;
struct Fixture {
    ma::SpeciesRegistry registry = ma::SpeciesRegistry::standard();
    ma_coupled_config config{sizeof(ma_coupled_config), 1, 2, 2, 1, 1, 0, 0, 0, .001, .45, 0, 0, 1, 0, 0};
    std::vector<ma_coupled_cell_input> cells = std::vector<ma_coupled_cell_input>(4);
    std::vector<double> masses = std::vector<double>(4 * registry.size());
    explicit Fixture(bool uniform = false) {
        for (std::size_t i = 0; i < 4; ++i) {
            const double alpha = uniform ? .25 : .1 + .05 * static_cast<double>(i);
            const double t = uniform ? 300 : 298 + 2 * static_cast<double>(i);
            const double n = 100000 * (1 - alpha) / (ma::gas_constant * t);
            ma::MixtureInventory inventory(registry.size());
            inventory.masses[registry.index("nitrogen")] = .79 * n * registry[registry.index("nitrogen")].molar_mass;
            inventory.masses[registry.index("oxygen")] = .21 * n * registry[registry.index("oxygen")].molar_mass;
            inventory.masses[registry.index("water")] = alpha * registry[registry.index("water")].density;
            inventory.masses[registry.index("helium")] = 1e-30;
            inventory.set_temperature(registry, t);
            cells[i] = {inventory.energy, 0, 0, 100000, 0, 0};
            std::copy(inventory.masses.begin(), inventory.masses.end(), masses.begin() + static_cast<std::ptrdiff_t>(i * registry.size()));
        }
    }
    Owner create() const {
        ma_coupled_world* pointer = nullptr;
        ok(ma_coupled_create(&config, cells.data(), cells.size(), masses.data(), masses.size(), &pointer));
        return {pointer, ma_coupled_destroy};
    }
};
struct Capture {
    ma_coupled_snapshot header{};
    std::vector<ma_coupled_cell> cells = std::vector<ma_coupled_cell>(4);
    std::vector<double> masses;
    explicit Capture(const ma_coupled_world* world, std::size_t count) : masses(4 * count) {
        ok(ma_coupled_copy_snapshot(world, &header, cells.data(), cells.size(), masses.data(), masses.size()));
    }
    bool same(const Capture& other) const {
        return std::memcmp(&header, &other.header, sizeof(header)) == 0 && cells.size() == other.cells.size() && masses.size() == other.masses.size() &&
            std::memcmp(masses.data(), other.masses.data(), masses.size() * sizeof(double)) == 0 &&
            std::memcmp(cells.data(), other.cells.data(), cells.size() * sizeof(cells[0])) == 0;
    }
};
Owner advance(const Owner& source, double seconds, ma_coupled_step_result& report) {
    ma_coupled_world* pointer = nullptr; ok(ma_coupled_advance(source.get(), seconds, 10000, &pointer, &report));
    return {pointer, ma_coupled_destroy};
}
void definitions_and_layout() {
    check(ma_coupled_abi_version() == 1, "Coupled ABI differs"); uint32_t count = 0; ok(ma_coupled_species_count(&count));
    const auto r = ma::SpeciesRegistry::standard(); check(count == r.size(), "Coupled registry count differs");
    for (uint32_t s = 0; s < count; ++s) {
        uint64_t size = 0; ok(ma_coupled_species_id(s, nullptr, 0, &size)); std::vector<char> name(static_cast<std::size_t>(size));
        ok(ma_coupled_species_id(s, name.data(), size, &size)); ma_coupled_species definition{}; ok(ma_coupled_species_definition(s, &definition));
        check(std::string(name.data()) == r[s].id && definition.molar_mass == r[s].molar_mass && definition.cv == r[s].cv() &&
            definition.energy_offset == r[s].internal_energy(0) && definition.condensed_density == r[s].density &&
            definition.phase == static_cast<uint32_t>(r[s].phase) && definition.reserved == 0, "Coupled material model differs");
    }
}
void exact_import_and_owned_capture() {
    Fixture f; auto retained = f.cells; const double momentum = std::bit_cast<double>(0x3fd999999999999cULL); const double mass = [&] {
        double result = 0; for (std::size_t s = 0; s < f.registry.size(); ++s) result += f.masses[s]; return result;
    }();
    // Independently calculated binary64 counterexample, not a runtime search.
    std::cout << "CONTROL " << std::hex << std::bit_cast<uint64_t>(mass) << ' ' << std::bit_cast<uint64_t>(momentum) << ' '
        << std::bit_cast<uint64_t>(momentum / mass * mass) << std::dec << '\n';
    check(std::bit_cast<uint64_t>(mass) == 0x40592fde816581afULL &&
        std::bit_cast<uint64_t>(momentum / mass * mass) == 0x3fd999999999999bULL, "Momentum control operands changed");
    check(momentum / mass * mass != momentum, "Missing momentum round-trip control"); f.cells[0].momentum_x = momentum;
    retained = f.cells; const auto input_masses = f.masses; auto owner = f.create(); const Capture c(owner.get(), f.registry.size());
    check(f.masses == input_masses && std::memcmp(f.cells.data(), retained.data(), sizeof(retained[0]) * retained.size()) == 0, "Import changed caller data");
    check(c.header.revision == 0 && c.header.time == 0 && c.header.cells == 4 && c.header.species == f.registry.size() &&
        c.masses == input_masses && c.cells[0].momentum_x == momentum, "Conserved import changed primary bits");
    f.masses[0] = 1234; f.cells[0].internal_energy = 0; check(Capture(owner.get(), f.registry.size()).same(c), "Caller mutation affected owner");
}
void immutable_advance_and_conservation() {
    Fixture f; auto source = f.create(); const Capture before(source.get(), f.registry.size()); ma_coupled_step_result result{};
    auto candidate = advance(source, 1. / 1024, result); const Capture after(candidate.get(), f.registry.size());
    check(Capture(source.get(), f.registry.size()).same(before), "Advance changed source");
    check(after.header.revision == 1 && after.header.time == 1. / 1024 && result.revision == 1 && result.substeps > 0, "Completed revision differs");
    double thermal_scale = 0;
    for (std::size_t s = 0; s < f.registry.size(); ++s) {
        double a = 0, b = 0;
        for (std::size_t i = 0; i < 4; ++i) { a += before.masses[i * f.registry.size() + s]; b += after.masses[i * f.registry.size() + s];
            thermal_scale += before.masses[i * f.registry.size() + s] * f.registry[s].cv() * before.cells[i].temperature; }
        near(a, b, std::max(a, b));
    }
    near(before.header.internal_energy + before.header.kinetic_energy, after.header.internal_energy + after.header.kinetic_energy, thermal_scale, 1e-8);
    const auto trace = f.registry.index("helium"); for (std::size_t i = 0; i < 4; ++i) check(after.masses[i * f.registry.size() + trace] > 0, "Trace erased");
    candidate.reset(); check(Capture(source.get(), f.registry.size()).same(before), "Child lifetime damaged source");
}
void independent_readings() {
    Fixture f; auto source = f.create(); const Capture c(source.get(), f.registry.size());
    for (std::size_t i = 0; i < 4; ++i) {
        double n = 0, liquid = 0;
        for (std::size_t s = 0; s < f.registry.size(); ++s) {
            const auto mass = c.masses[i * f.registry.size() + s];
            if (f.registry[s].phase == ma::Phase::gas) n += mass / f.registry[s].molar_mass;
            else if (f.registry[s].phase == ma::Phase::liquid) liquid += mass / f.registry[s].density;
        }
        const auto p = n * ma::gas_constant * c.cells[i].temperature / (1 - liquid);
        near(c.cells[i].pressure, p, p); near(c.cells[i].liquid_fraction, liquid, liquid);
    }
}
void failed_fork_and_outputs() {
    Fixture f(true); auto source = f.create(); const Capture before(source.get(), f.registry.size()); ma_coupled_step_result valid{};
    auto prefix = advance(source, .001, valid); check(valid.substeps == 1, "Budget control prefix was not one completed substep");
    ma_coupled_step_result result; std::memset(&result, 0xa5, sizeof(result)); const auto preserved = result;
    ma_coupled_world* rejected = nullptr;
    check(ma_coupled_advance(source.get(), .003, 1, &rejected, &result) == MA_NUMERICAL_ERROR && rejected == nullptr &&
        std::memcmp(&result, &preserved, sizeof(result)) == 0, "Failed fork published output or lost diagnostics sentinel");
    check(Capture(source.get(), f.registry.size()).same(before), "Failed fork changed complete source");
    auto header = before.header; auto cells = before.cells; auto masses = before.masses;
    check(ma_coupled_copy_snapshot(source.get(), &header, cells.data(), cells.size() - 1, masses.data(), masses.size()) == MA_BUFFER_TOO_SMALL,
        "Undersized cell output accepted");
    check(std::memcmp(&header, &before.header, sizeof(header)) == 0 && std::memcmp(cells.data(), before.cells.data(), cells.size() * sizeof(cells[0])) == 0 &&
        masses == before.masses, "Buffer preflight changed output");
}
void rejected_initialization_and_walls() {
    Fixture f; ma_coupled_world* rejected = nullptr;
    auto bad = f.config; bad.model = 0;
    check(ma_coupled_create(&bad, f.cells.data(), 4, f.masses.data(), f.masses.size(), &rejected) == MA_INVALID_ARGUMENT && !rejected, "Unknown model accepted");
    bad = f.config; bad.reserved2 = 1;
    check(ma_coupled_create(&bad, f.cells.data(), 4, f.masses.data(), f.masses.size(), &rejected) == MA_INVALID_ARGUMENT && !rejected, "Unknown flags accepted");
    check(ma_coupled_create(&f.config, f.cells.data(), 4, f.masses.data(), f.masses.size() - 1, &rejected) == MA_INVALID_ARGUMENT && !rejected, "Wrong mass shape accepted");
    std::fill(f.masses.begin() + static_cast<std::ptrdiff_t>(3 * f.registry.size()), f.masses.end(), 0.); f.cells[3] = {};
    check(ma_coupled_create(&f.config, f.cells.data(), 4, f.masses.data(), f.masses.size(), &rejected) == MA_INVALID_ARGUMENT && !rejected, "Vacuum silently admitted");
    f.cells[3].wall = 1; auto owner = f.create(); const Capture c(owner.get(), f.registry.size());
    check(c.cells[3].wall == 1 && c.cells[3].total_mass == 0 && c.cells[3].temperature == 0 && c.cells[3].pressure == 0, "Empty solid ownership differs");
    f.masses[3 * f.registry.size()] = 1;
    check(ma_coupled_create(&f.config, f.cells.data(), 4, f.masses.data(), f.masses.size(), &rejected) == MA_INVALID_ARGUMENT && !rejected, "Fluid wall accepted");
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"coupled ABI and material definitions", definitions_and_layout}, {"conserved import and owned snapshot", exact_import_and_owned_capture},
        {"immutable coupled advance, traces and conservation", immutable_advance_and_conservation}, {"independent pressure and liquid readings", independent_readings},
        {"failed fork and output-buffer atomicity", failed_fork_and_outputs}, {"whole-world admission and empty walls", rejected_initialization_and_walls}};
    try { for (const auto& [name, run] : tests) { run(); std::cout << "PASS " << name << '\n'; } return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
