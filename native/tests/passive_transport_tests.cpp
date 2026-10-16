#include "mixed_atmospheres/coupled_api.h"
#include "mixed_atmospheres/fluid_world.hpp"
#include "mixed_atmospheres/transport.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void ok(ma_status status) { if (status != MA_OK) throw std::runtime_error(ma_coupled_last_error()); }
void near(double a, double b) { check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 1e-11 * std::max(std::abs(a), std::abs(b)), "Passive analytic comparison differs"); }
double quantity(const ma::PassiveQuantities& values, uint64_t identity) {
    for (const auto& value : values) if (value.identity == identity) return value.quantity;
    return 0;
}
void same(const ma::PassiveQuantities& a, const ma::PassiveQuantities& b) {
    check(a.size() == b.size(), "Passive identity set differs");
    for (std::size_t i = 0; i < a.size(); ++i) check(a[i].identity == b[i].identity && a[i].quantity == b[i].quantity, "Passive value differs");
}
ma::MixtureInventory gas(const ma::SpeciesRegistry& r) {
    ma::MixtureInventory m(r.size()); m.masses[r.index("nitrogen")] = 1; m.set_temperature(r, 300); return m;
}
void donor_weights_and_identity() {
    const auto r = ma::SpeciesRegistry::standard(); const auto m = gas(r); const auto rad = ma::radiation_passive_identity;
    const std::vector<ma::TransportCell> cells{{m, 1, 0, 0, {{17, 24}, {rad, 2}}}, {m, 1, 0, 0, {{0x80000000ULL, 1e-30}, {rad, .125}}}};
    const auto before = cells; const auto out = ma::advect_mixtures(r, cells, std::vector<ma::SweptFace>{{0, 1, .25}});
    near(quantity(out[0].passive_quantities, 17), 18); near(quantity(out[1].passive_quantities, 17), 6);
    near(quantity(out[1].passive_quantities, 0x80000000ULL), 1e-30); near(quantity(out[0].passive_quantities, rad), 1.5);
    near(quantity(out[1].passive_quantities, rad), .625); same(cells[0].passive_quantities, before[0].passive_quantities);
    const auto identity = ma::advect_mixtures(r, cells, {});
    for (std::size_t i = 0; i < cells.size(); ++i) same(cells[i].passive_quantities, identity[i].passive_quantities);
}
void multiple_outgoing_and_signed_faces() {
    const auto r = ma::SpeciesRegistry::standard(); const auto m = gas(r);
    const std::vector<ma::TransportCell> cells{{m, 1, 0, 0, {}}, {m, 1, 0, 0, {{17, 1e-60}}}, {m, 1, 0, 0, {}}};
    const auto out = ma::advect_mixtures(r, cells, std::vector<ma::SweptFace>{{0, 1, -.2}, {1, 2, .3}});
    near(quantity(out[0].passive_quantities, 17), 2e-61); near(quantity(out[1].passive_quantities, 17), 5e-61); near(quantity(out[2].passive_quantities, 17), 3e-61);
}
void diffusion_and_physical_noninterference() {
    const auto r = ma::SpeciesRegistry::standard(); auto mixture = gas(r); mixture.add(r, r.index("water"), .25 * r[r.index("water")].density, 300);
    ma::FluidSolverSettings settings; settings.gravity = 0; settings.conduction = false; settings.diffusivity_m2_per_s = 1.5;
    settings.viscosity_m2_per_s = 0; const auto rad = ma::radiation_passive_identity;
    ma::FluidWorld baseline(2, 1, 1, 1, settings, r), traced(2, 1, 1, 1, settings, r);
    for (std::size_t i = 0; i < 2; ++i) baseline.set_conserved_cell(i, 0, mixture, 0, 0);
    traced.set_conserved_cell(0, 0, mixture, 0, 0, 101325, ma::PassiveQuantities{{17, 1000}, {rad, .5}});
    traced.set_conserved_cell(1, 0, mixture, 0, 0, 101325, ma::PassiveQuantities{{0x80000000ULL, 1e-30}, {rad, 1.5}});
    const double fraction = .015 / mixture.mass();
    const auto a = baseline.step(.01); const auto b = traced.step(.01);
    check(a.substeps == b.substeps && a.pressure_iterations == b.pressure_iterations, "Metadata altered pressure work");
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& x = baseline.cell(i, 0); const auto& y = traced.cell(i, 0);
        check(x.mixture.masses == y.mixture.masses && x.mixture.energy == y.mixture.energy &&
            x.momentum_x == y.momentum_x && x.momentum_y == y.momentum_y, "Passive metadata changed physical state");
    }
    near(quantity(traced.cell(0, 0).passive_quantities, 17), 1000 * (1 - fraction));
    near(quantity(traced.cell(1, 0).passive_quantities, 17), 1000 * fraction);
    near(quantity(traced.cell(0, 0).passive_quantities, 0x80000000ULL), 1e-30 * fraction);
    near(quantity(traced.cell(0, 0).passive_quantities, rad), .5 + fraction);
}
void whole_call_metadata_rollback() {
    const auto r = ma::SpeciesRegistry::standard(); ma::FluidSolverSettings settings;
    settings.gravity = 0; settings.diffusivity_m2_per_s = 0; settings.viscosity_m2_per_s = 0; settings.conduction = false; settings.maximum_timestep_s = .001;
    ma::FluidWorld world(2, 1, 1, 1, settings, r);
    for (std::size_t i = 0; i < 2; ++i) world.set_conserved_cell(i, 0, gas(r), 0, 0, 101325, ma::PassiveQuantities{{17 + i, 1e-30}, {ma::radiation_passive_identity, .25}});
    const auto before = world; bool rejected = false;
    try { (void)world.step(.003, 1); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected && world.time() == before.time(), "Failed metadata timestep published");
    for (std::size_t i = 0; i < 2; ++i) { same(world.cell(i, 0).passive_quantities, before.cell(i, 0).passive_quantities);
        check(world.cell(i, 0).mixture.energy == before.cell(i, 0).mixture.energy, "Failed metadata timestep changed U"); }
}
void full_abi_and_buffer_atomicity() {
    const auto r = ma::SpeciesRegistry::standard(); const auto mixture = gas(r);
    ma_coupled_config config{sizeof(ma_coupled_config), 1, 2, 1, 1, 1, 0, 0, 0, .001, .45, 0, 0, 1, 0, 0};
    std::vector<ma_coupled_cell_input> input(2, {mixture.energy, 0, 0, 101325, 0, 0}); std::vector<double> masses;
    for (int i = 0; i < 2; ++i) masses.insert(masses.end(), mixture.masses.begin(), mixture.masses.end());
    const std::vector<uint64_t> offsets{0, 2, 4};
    const std::vector<ma_coupled_passive> entries{{17, 1e-30}, {ma::radiation_passive_identity, .25}, {0x80000000ULL, 1000}, {ma::radiation_passive_identity, .5}};
    ma_coupled_world* pointer = nullptr; check(ma_coupled_passive_abi_version() == 1, "Passive extension differs");
    ok(ma_coupled_create_full(&config, input.data(), 2, masses.data(), masses.size(), offsets.data(), offsets.size(), entries.data(), entries.size(), &pointer));
    std::unique_ptr<ma_coupled_world, decltype(&ma_coupled_destroy)> owner(pointer, ma_coupled_destroy);
    uint64_t count = 0; ok(ma_coupled_passive_count(owner.get(), &count)); check(count == entries.size(), "Sparse shape differs");
    ma_coupled_snapshot header{}; std::vector<ma_coupled_cell> cells(2); auto copied_masses = masses; auto copied_offsets = offsets; auto copied_entries = entries;
    check(ma_coupled_copy_snapshot(owner.get(), &header, cells.data(), 2, copied_masses.data(), copied_masses.size()) == MA_INVALID_ARGUMENT,
        "Base snapshot silently omitted metadata");
    std::memset(&header, 0xa5, sizeof(header)); const auto sentinel = header; copied_entries[0].quantity = 42;
    check(ma_coupled_copy_full_snapshot(owner.get(), &header, cells.data(), 2, copied_masses.data(), copied_masses.size(), copied_offsets.data(),
        copied_offsets.size(), copied_entries.data(), copied_entries.size() - 1) == MA_BUFFER_TOO_SMALL &&
        std::memcmp(&header, &sentinel, sizeof(header)) == 0 && copied_entries[0].quantity == 42, "Passive buffer failure wrote outputs");
    ok(ma_coupled_copy_full_snapshot(owner.get(), &header, cells.data(), 2, copied_masses.data(), copied_masses.size(), copied_offsets.data(), copied_offsets.size(), copied_entries.data(), copied_entries.size()));
    check(copied_offsets == offsets && copied_entries[0].identity == 17 && copied_entries[0].quantity == 1e-30 && header.revision == 0, "Full metadata export differs");
    auto malformed = entries; malformed[1].identity = 17;
    check(ma_coupled_create_full(&config, input.data(), 2, masses.data(), masses.size(), offsets.data(), offsets.size(), malformed.data(), malformed.size(), &pointer) == MA_INVALID_ARGUMENT && !pointer,
        "Duplicate sparse identities admitted");
}
void rejects_invalid_and_unrepresentable_passives() {
    for (const auto& values : std::vector<ma::PassiveQuantities>{{{0, 1}}, {{17, -1}}, {{17, std::numeric_limits<double>::infinity()}}, {{ma::radiation_passive_identity + 1, 1}}}) {
        bool rejected = false; try { ma::validate_passive_quantities(values); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid passive value accepted");
    }
    bool rejected = false;
    try { (void)ma::scale_passive_quantities(ma::PassiveQuantities{{17, std::numeric_limits<double>::denorm_min()}}, .25); }
    catch (const std::domain_error&) { rejected = true; }
    check(rejected, "Positive underflowed passive transfer disappeared");
}
}
int main() {
    try {
        donor_weights_and_identity(); std::cout << "PASS shared donor weights and identity\n";
        multiple_outgoing_and_signed_faces(); std::cout << "PASS split outgoing and reversed faces preserve tiny identities\n";
        diffusion_and_physical_noninterference(); std::cout << "PASS bulk gas-exchange metadata and physical noninterference\n";
        whole_call_metadata_rollback(); std::cout << "PASS whole-call passive rollback\n";
        full_abi_and_buffer_atomicity(); std::cout << "PASS sparse ABI, complete export and buffer atomicity\n";
        rejects_invalid_and_unrepresentable_passives(); std::cout << "PASS malformed and unrepresentable passive rejection\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
