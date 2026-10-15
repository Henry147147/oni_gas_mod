#include "mixed_atmospheres/api.h"
#include "mixed_atmospheres/acoustics.hpp"
#include "mixed_atmospheres/grid.hpp"
#include "mixed_atmospheres/phase_change.hpp"
#include "mixed_atmospheres/process.hpp"
#include "mixed_atmospheres/pressure.hpp"
#include "mixed_atmospheres/reaction.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include "mixed_atmospheres/transport.hpp"
#include "mixed_atmospheres/volume_work.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Test = std::pair<const char*, std::function<void()>>;
std::vector<Test>& tests() { static std::vector<Test> result; return result; }
struct Register { Register(const char* name, std::function<void()> fn) { tests().emplace_back(name, std::move(fn)); } };
#define TEST(name) void name(); Register reg_##name(#name, name); void name()
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string("Check failed: ") + #condition + " at line " + std::to_string(__LINE__)); } while (false)
void near(double actual, double expected, double relative = 1e-10, double absolute = 1e-10) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > absolute + relative * std::abs(expected))
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}
template<class F> void rejects(F&& function) {
    bool caught = false;
    try { function(); } catch (const std::exception&) { caught = true; }
    CHECK(caught);
}
ma::SolverSettings quiet() { return {0, 0, false, false}; }

TEST(air_uses_mole_fractions_and_ideal_gas_pressure) {
    const auto r = ma::SpeciesRegistry::standard();
    const auto air = ma::make_air(r, 1);
    const auto sample = ma::assess_atmosphere(r, air, 1);
    CHECK(sample.breathable()); near(sample.oxygen_fraction, 0.21); near(sample.pressure, 101325);
    CHECK(air.masses[r.index("oxygen")] / air.mass() > 0.23);
    CHECK(air.masses[r.index("oxygen")] / air.mass() < 0.24);
    near(r[r.index("nitrogen")].molar_mass, 0.0280134);
}

void check_material_balance(const ma::SpeciesRegistry& r, const std::vector<ma::MaterialCell>& before,
                            const std::vector<ma::AcousticFace>& faces, double dt,
                            const ma::MaterialPressureStep& after) {
    long double energy = 0;
    double initial_volume = 0, final_volume = 0;
    std::vector<double> swept(before.size(), 0);
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const auto& face = faces[f];
        const double next = after.velocities_m_per_s[f];
        const double volume = dt * face.area_m2 * 0.5 * (face.velocity_m_per_s + next);
        swept[face.left] += volume; swept[face.right] -= volume;
        energy += 0.5L * face.inertia_kg * (static_cast<long double>(next) * next -
                                            static_cast<long double>(face.velocity_m_per_s) * face.velocity_m_per_s);
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        const auto& old = before[i]; const auto& next = after.cells[i];
        CHECK(next.mixture.masses == old.mixture.masses);
        initial_volume += old.volume_m3; final_volume += next.volume_m3;
        near(next.volume_m3 - old.volume_m3, swept[i], 1e-9, 1e-10);
        const double du = next.mixture.energy - old.mixture.energy;
        energy += du;
        near(du, -after.work_pressures_pa[i] * (next.volume_m3 - old.volume_m3), 1e-8, 1e-6);
        const double n_r = old.mixture.gas_moles(r) * ma::gas_constant;
        if (n_r != 0) {
            const double gas_before = old.volume_m3 - old.mixture.condensed_volume(r);
            const double gas_after = next.volume_m3 - next.mixture.condensed_volume(r);
            const double ds = old.mixture.heat_capacity(r) * std::log(next.mixture.temperature(r) / old.mixture.temperature(r)) +
                              n_r * std::log(gas_after / gas_before);
            near(ds, 0, 0, 1e-7);
            near(next.mixture.pressure(r, next.volume_m3), after.endpoint_pressures_pa[i], 1e-10);
        }
    }
    near(final_volume, initial_volume, 1e-10);
    near(static_cast<double>(energy), after.body_force_work_j, 1e-9, 3e-6);
    near(after.energy_balance_error_j, 0, 0, 3e-6);
}

TEST(nonlinear_material_pressure_couples_volume_temperature_and_energy) {
    const auto r = ma::SpeciesRegistry::standard();
    std::vector<ma::MaterialCell> cells{{ma::make_air(r, 1, 300, 150000)}, {ma::make_air(r, 1, 300, 50000)}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, 0.5 * (cells[0].mixture.mass() + cells[1].mixture.mass()), 0}};
    // More than sixty acoustic crossing times: a nonlinear energy/volume
    // check, not a claim of time-accurate acoustic resolution at this dt.
    const auto result = ma::advance_material_pressure(r, cells, faces, 0.2);
    CHECK(result.cells[0].volume_m3 > 1); CHECK(result.cells[1].volume_m3 < 1);
    CHECK(result.cells[0].mixture.temperature(r) < 300); CHECK(result.cells[1].mixture.temperature(r) > 300);
    CHECK(result.nonlinear_iterations > 0); check_material_balance(r, cells, faces, 0.2, result);
    CHECK(cells[0].volume_m3 == 1); near(cells[0].mixture.temperature(r), 300);
}

TEST(material_pressure_has_the_correct_small_amplitude_acoustic_limit) {
    const auto r = ma::SpeciesRegistry::standard();
    const std::vector<ma::MaterialCell> cells{{ma::make_air(r, 1, 300, 100000.05)}, {ma::make_air(r, 1, 300, 99999.95)}};
    const double inertia = 0.5 * (cells[0].mixture.mass() + cells[1].mixture.mass());
    const double gamma = 1 + cells[0].mixture.gas_moles(r) * ma::gas_constant / cells[0].mixture.heat_capacity(r);
    const double compliance = 1 / (gamma * 100000), dt = 0.02;
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, inertia, 0}};
    const auto result = ma::advance_material_pressure(r, cells, faces, dt);
    const double expected = dt * 0.1 / inertia / (1 + dt * dt / (2 * inertia * compliance));
    near(result.velocities_m_per_s[0], expected, 1e-6, 1e-10);
    check_material_balance(r, cells, faces, dt, result);
}

TEST(material_pressure_preserves_hydrostatic_gas_and_liquid_compartments) {
    const auto r = ma::SpeciesRegistry::standard();
    ma::MixtureInventory water(r.size()); water.add(r, r.index("water"), r[r.index("water")].density, 300);
    const double rho = water.mass();
    const std::vector<ma::MaterialCell> cells{
        {ma::make_air(r, 1, 300, 101325)}, {ma::make_air(r, 1, 300, 101313)},
        {water, 1, 101325 + rho * 9.81}, {water, 1, 101325}};
    const double gas_mass = (cells[0].mixture.pressure(r, 1) - cells[1].mixture.pressure(r, 1)) / 9.81;
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, gas_mass, 0, -9.81}, {2, 3, 1, rho, 0, -9.81}};
    const auto result = ma::advance_material_pressure(r, cells, faces, 2);
    for (const double velocity : result.velocities_m_per_s) near(velocity, 0, 0, 1e-10);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        near(result.cells[i].volume_m3, cells[i].volume_m3);
        near(result.cells[i].mixture.temperature(r), 300);
    }
    check_material_balance(r, cells, faces, 2, result);
}

TEST(material_pressure_transmits_force_through_incompressible_liquid) {
    const auto r = ma::SpeciesRegistry::standard();
    ma::MixtureInventory water(r.size()); water.add(r, r.index("water"), r[r.index("water")].density, 300);
    const std::vector<ma::MaterialCell> cells{
        {ma::make_air(r, 1, 300, 150000)}, {water}, {ma::make_air(r, 1, 300, 50000)}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, 500, 0}, {1, 2, 1, 500, 0}};
    const auto result = ma::advance_material_pressure(r, cells, faces, 0.1);
    CHECK(result.velocities_m_per_s[0] > 0);
    near(result.velocities_m_per_s[0], result.velocities_m_per_s[1]);
    CHECK(result.cells[1].volume_m3 == 1); CHECK(result.cells[1].mixture.energy == water.energy);
    CHECK(result.cells[0].volume_m3 > 1); CHECK(result.cells[2].volume_m3 < 1);
    check_material_balance(r, cells, faces, 0.1, result);
}

TEST(material_pressure_accounts_for_gravity_work_and_liquid_pressure_gauge) {
    const auto r = ma::SpeciesRegistry::standard();
    const std::vector<ma::MaterialCell> gas{{ma::make_air(r, 1)}, {ma::make_air(r, 1)}};
    const std::vector<ma::AcousticFace> gas_faces{{0, 1, 1, gas[0].mixture.mass(), 0, -9.81}};
    const auto falling = ma::advance_material_pressure(r, gas, gas_faces, 0.01);
    CHECK(falling.velocities_m_per_s[0] < 0); CHECK(falling.body_force_work_j > 0);
    check_material_balance(r, gas, gas_faces, 0.01, falling);
    ma::MixtureInventory water(r.size()); water.add(r, r.index("water"), r[r.index("water")].density, 300);
    const std::vector<ma::MaterialCell> liquid{{water}, {water}};
    const std::vector<ma::AcousticFace> liquid_faces{{0, 1, 1, water.mass(), 0, -9.81}};
    const auto supported = ma::advance_material_pressure(r, liquid, liquid_faces, 0.1);
    near(supported.velocities_m_per_s[0], 0, 0, 1e-10);
    near(supported.work_pressures_pa[0], 101325);
    near(supported.work_pressures_pa[0] - supported.work_pressures_pa[1], water.mass() * 9.81);
    check_material_balance(r, liquid, liquid_faces, 0.1, supported);
}

TEST(material_pressure_mixed_gas_liquid_heat_capacity_and_reversibility) {
    const auto r = ma::SpeciesRegistry::standard();
    auto mixed = ma::make_air(r, 0.5, 300, 140000);
    mixed.add(r, r.index("water"), 0.5 * r[r.index("water")].density, 300);
    mixed.add(r, r.index("chlorine"), 1e-20, 300);
    const std::vector<ma::MaterialCell> cells{{mixed}, {ma::make_air(r, 1, 300, 80000)}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, 250, 0}};
    const double dt = 0.01;
    const auto first = ma::advance_material_pressure(r, cells, faces, dt);
    check_material_balance(r, cells, faces, dt, first);
    auto reverse_faces = faces; reverse_faces[0].velocity_m_per_s = -first.velocities_m_per_s[0];
    const auto reverse = ma::advance_material_pressure(r, first.cells, reverse_faces, dt);
    check_material_balance(r, first.cells, reverse_faces, dt, reverse);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        near(reverse.cells[i].volume_m3, cells[i].volume_m3, 1e-10);
        near(reverse.cells[i].mixture.temperature(r), 300, 1e-10);
        CHECK(reverse.cells[i].mixture.masses == cells[i].mixture.masses);
    }
    near(reverse.velocities_m_per_s[0], 0, 0, 1e-9);
}

TEST(material_pressure_rejects_unsupported_states_without_input_changes) {
    const auto r = ma::SpeciesRegistry::standard();
    std::vector<ma::MaterialCell> cells{{ma::make_air(r, 1)}, {ma::make_air(r, 1)}};
    const auto original = cells;
    rejects([&] { (void)ma::advance_material_pressure(r, cells, {}, -1); });
    rejects([&] { (void)ma::advance_material_pressure(r, cells, {}, std::numeric_limits<double>::quiet_NaN()); });
    const std::vector<ma::AcousticFace> invalid{{0, 2, 1, 1, 0}};
    rejects([&] { (void)ma::advance_material_pressure(r, cells, invalid, 0.1); });
    for (std::size_t i = 0; i < cells.size(); ++i) {
        CHECK(cells[i].mixture.energy == original[i].mixture.energy);
        CHECK(cells[i].mixture.masses == original[i].mixture.masses);
    }
    cells[0].mixture = ma::MixtureInventory(r.size());
    rejects([&] { (void)ma::advance_material_pressure(r, cells, {}, 0.1); });
    cells[0].mixture.add(r, r.index("water"), 1, 300);
    rejects([&] { (void)ma::advance_material_pressure(r, cells, {}, 0.1); });
    cells[0].mixture.add(r, r.index("ice"), 1, 270);
    rejects([&] { (void)ma::advance_material_pressure(r, cells, {}, 0.1); });
    cells[0].mixture = ma::MixtureInventory(r.size());
    cells[0].mixture.add(r, r.index("water"), r[r.index("water")].density, 300);
    const std::vector<ma::AcousticFace> divergent{{0, 1, 1, 500, 1}};
    rejects([&] { (void)ma::advance_material_pressure(r, cells, divergent, 0.1); });
    const auto zero = ma::advance_material_pressure(r, cells, {}, 0);
    CHECK(zero.cells[0].mixture.energy == cells[0].mixture.energy);
    CHECK(zero.cells[0].volume_m3 == cells[0].volume_m3);
}

TEST(temperature_mixing_weights_energy_and_heat_capacity) {
    const auto r = ma::SpeciesRegistry::standard(); ma::MixtureInventory m(r.size());
    m.add(r, r.index("oxygen"), 1, 250); m.add(r, r.index("nitrogen"), 3, 350);
    const double cv_o = r[r.index("oxygen")].cv(), cv_n = r[r.index("nitrogen")].cv();
    near(m.temperature(r), (cv_o * 250 + 3 * cv_n * 350) / (cv_o + 3 * cv_n));
}

TEST(selective_filter_preserves_inert_and_trace_mass) {
    const auto r = ma::SpeciesRegistry::standard(); auto source = ma::make_air(r, 1); ma::MixtureInventory target(r.size());
    source.add(r, r.index("chlorine"), 1e-16, 293.15);
    const double mass = source.mass(), energy = source.energy, n2 = source.masses[r.index("nitrogen")];
    const auto moved = ma::transfer(r, source, target, 10, 10, r.index("oxygen"));
    CHECK(moved > 0); near(source.masses[r.index("nitrogen")], n2, 0, 0);
    CHECK(source.masses[r.index("chlorine")] == 1e-16);
    near(source.mass() + target.mass(), mass); near(source.energy + target.energy, energy);
}

TEST(full_output_does_not_consume_input) {
    const auto r = ma::SpeciesRegistry::standard(); auto source = ma::make_air(r, 1), target = ma::make_air(r, 1);
    const auto before = source;
    near(ma::transfer(r, source, target, 1, target.mass(), r.size()), 0);
    CHECK(source.masses == before.masses); CHECK(source.energy == before.energy);
    rejects([&] { ma::transfer(r, source, source, 1, 10, r.size()); });
}

TEST(compressor_work_heats_air_and_balances_stream_enthalpy) {
    const auto r = ma::SpeciesRegistry::standard();
    const ma::FlowPacket feed{ma::make_air(r, 1, 300, 100000), 100000};
    const auto reversible = ma::compress_gas(r, feed, 200000, 1);
    const auto actual = ma::compress_gas(r, feed, 200000, 0.7);
    CHECK(reversible.output.contents.temperature(r) > 360);
    CHECK(reversible.output.contents.temperature(r) < 370);
    CHECK(actual.output.contents.temperature(r) > reversible.output.contents.temperature(r));
    CHECK(actual.output.contents.masses == feed.contents.masses);
    near(actual.output.enthalpy(r) - feed.enthalpy(r), actual.work_in_j);
    near(actual.work_in_j * 0.7, reversible.work_in_j);
    near(reversible.entropy_produced_j_per_k, 0, 0, 1e-10);
    CHECK(actual.entropy_produced_j_per_k > 0);
    near(feed.contents.temperature(r), 300);
}

TEST(liquid_pump_accounts_for_pressure_energy_and_loss_heat) {
    const auto r = ma::SpeciesRegistry::standard();
    ma::FlowPacket feed{ma::MixtureInventory(r.size()), 100000};
    feed.contents.add(r, r.index("water"), 10, 300);
    const auto ideal = ma::pump_liquid(r, feed, 1100000, 1);
    const auto actual = ma::pump_liquid(r, feed, 1100000, 0.5);
    near(ideal.output.contents.temperature(r), 300);
    near(ideal.work_in_j, 10.0 / 997 * 1000000);
    near(actual.output.enthalpy(r) - feed.enthalpy(r), actual.work_in_j);
    near(actual.output.contents.energy - feed.contents.energy, ideal.work_in_j);
    CHECK(actual.entropy_produced_j_per_k > 0);
    CHECK(actual.output.contents.masses == feed.contents.masses);
}

TEST(separating_air_requires_demixing_work_and_heat_rejection) {
    const auto r = ma::SpeciesRegistry::standard();
    const ma::FlowPacket feed{ma::make_air(r, 1, 300, 100000), 100000};
    const auto result = ma::separate_gas(r, feed, r.index("oxygen"), 1, 0.5);
    const double n = feed.contents.gas_moles(r);
    const double reversible = -ma::gas_constant * 300 * n * (0.21 * std::log(0.21) + 0.79 * std::log(0.79));
    near(result.reversible_work_j, reversible);
    near(result.work_in_j, 2 * reversible);
    near(result.heat_rejected_j, result.work_in_j);
    near(result.selected.enthalpy(r) + result.remainder.enthalpy(r) + result.heat_rejected_j,
         feed.enthalpy(r) + result.work_in_j);
    CHECK(result.remainder.contents.masses[r.index("oxygen")] == 0);
    CHECK(result.selected.contents.masses[r.index("nitrogen")] == 0);
    CHECK(result.entropy_produced_j_per_k > 0);
    for (std::size_t i = 0; i < r.size(); ++i)
        near(result.selected.contents.masses[i] + result.remainder.contents.masses[i], feed.contents.masses[i]);
}

TEST(separator_partial_recovery_retains_trace_material_and_costs_work) {
    const auto r = ma::SpeciesRegistry::standard();
    ma::FlowPacket feed{ma::make_air(r, 1), 101325};
    feed.contents.add(r, r.index("chlorine"), 1e-16, 293.15);
    const auto partial = ma::separate_gas(r, feed, r.index("oxygen"), 0.5, 1);
    const auto complete = ma::separate_gas(r, feed, r.index("oxygen"), 1, 1);
    CHECK(partial.work_in_j > 0 && partial.work_in_j < complete.work_in_j);
    CHECK(partial.remainder.contents.masses[r.index("chlorine")] == 1e-16);
    const auto trace = ma::separate_gas(r, feed, r.index("chlorine"), 1, 1);
    CHECK(trace.work_in_j > 0);
    CHECK(trace.selected.contents.masses[r.index("chlorine")] == 1e-16);
    near(trace.entropy_produced_j_per_k, 0, 0, 0);
}

TEST(processes_reject_impossible_requests_and_leave_feed_unchanged) {
    const auto r = ma::SpeciesRegistry::standard();
    const ma::FlowPacket feed{ma::make_air(r, 1), 101325};
    const auto before = feed.contents;
    rejects([&] { (void)ma::compress_gas(r, feed, 1000, 1); });
    rejects([&] { (void)ma::compress_gas(r, feed, 200000, 1.1); });
    rejects([&] { (void)ma::pump_liquid(r, feed, 200000, 0.7); });
    rejects([&] { (void)ma::separate_gas(r, feed, r.index("water"), 1, 0.5); });
    rejects([&] { (void)ma::separate_gas(r, feed, 0, 1.1, 0.5); });
    rejects([&] { (void)ma::separate_gas(r, feed, 0, 1, 0); });
    CHECK(feed.contents.masses == before.masses && feed.contents.energy == before.energy);
    near(ma::compress_gas(r, feed, feed.pressure_pa, 0.7).work_in_j, 0, 0, 0);
    near(ma::separate_gas(r, feed, 0, 0, 1).work_in_j, 0, 0, 0);
}

TEST(implicit_pressure_pair_matches_analytic_solution) {
    const ma::PressureSystem system({1, 2}, {{0, 1, 3}});
    const auto result = system.solve(std::vector<double>{4, 5}, std::vector<double>{0, 0});
    near(result.pressure[0], 35.0 / 11); near(result.pressure[1], 32.0 / 11);
    CHECK(result.iterations <= 2);
    CHECK(result.residual_norm < 1e-10);
}

TEST(pressure_faces_cancel_and_barriers_isolate_compartments) {
    const ma::PressureSystem system({1, 0, 2, 0}, {{0, 1, 3}, {1, 2, 0}, {2, 3, 4}});
    const std::vector<double> pressure{10, 10, 20, 20};
    const auto rhs = system.apply(pressure);
    const auto solved = system.solve(rhs, std::vector<double>(4));
    for (std::size_t i = 0; i < pressure.size(); ++i) near(solved.pressure[i], pressure[i]);
    const auto arbitrary = system.apply(std::vector<double>{9, 24, -7, 34});
    near(arbitrary[0] + arbitrary[1] + arbitrary[2] + arbitrary[3], 9 - 14);
}

TEST(pressure_solve_recovers_variable_coefficient_field) {
    constexpr std::size_t width = 16, height = 12;
    std::vector<double> compliance(width * height), expected(width * height), initial(width * height, 100000);
    std::vector<ma::PressureFace> faces;
    for (std::size_t y = 0; y < height; ++y) for (std::size_t x = 0; x < width; ++x) {
        const auto i = y * width + x;
        compliance[i] = 0.01 + 0.001 * static_cast<double>(i % 7);
        expected[i] = 100000 + 100 * std::sin(static_cast<double>(x)) * std::cos(static_cast<double>(y));
        if (x + 1 < width) faces.push_back({i, i + 1, 1 + static_cast<double>(i % 5)});
        if (y + 1 < height) faces.push_back({i, i + width, 0.5 + static_cast<double>(i % 3)});
    }
    const ma::PressureSystem system(compliance, faces);
    const auto rhs = system.apply(expected);
    const auto solved = system.solve(rhs, initial, 1e-11, 1e-9);
    for (std::size_t i = 0; i < expected.size(); ++i) near(solved.pressure[i], expected[i], 1e-9);
    CHECK(solved.iterations < width * height);
}

TEST(pressure_solver_rejects_singular_components_and_does_not_publish_failure) {
    rejects([] { (void)ma::PressureSystem({0, 0}, {{0, 1, 1}}); });
    rejects([] { (void)ma::PressureSystem({1, 0}, {{0, 1, 0}}); });
    rejects([] { (void)ma::PressureSystem({1, 1}, {{0, 1, -1}}); });
    const ma::PressureSystem system({1, 2, 3}, {{0, 1, 2}, {1, 2, 5}});
    const std::vector<double> initial{4, 9, 15}, rhs{1, 2, 3};
    rejects([&] { (void)system.solve(rhs, initial, 1e-13, 1e-13, 1); });
    CHECK(initial == std::vector<double>({4, 9, 15}));
    rejects([&] { (void)system.solve(rhs, initial, 0, 0); });
}

TEST(implicit_heat_exchange_matches_pair_solution_and_increases_entropy) {
    const std::vector<ma::ThermalBody> bodies{{1000, 300}, {2000, 600}};
    const std::vector<ma::ThermalContact> contacts{{0, 1, 100}};
    const auto result = ma::conduct_heat(bodies, contacts, 10);
    // Backward Euler: difference = 300 / (1 + dt*G*(1/C1+1/C2)).
    near(result.temperatures_k[0], 420);
    near(result.temperatures_k[1], 540);
    near(result.energy_changes_j[0], 120000);
    near(result.energy_changes_j[0] + result.energy_changes_j[1], 0, 0, 1e-9);
    CHECK(result.entropy_produced_j_per_k > 0);
    near(bodies[0].temperature_k, 300, 0, 0);
}

TEST(implicit_acoustics_matches_damped_pair_mode_beyond_acoustic_cfl) {
    // Two unit cells containing air, connected by one inertial face. This is
    // the two-degree-of-freedom pressure/velocity oscillator with omega^2=2K/rho.
    const double bulk_modulus = 1.4 * 100000, density = 1.2;
    const double dt = 0.1; // ~34 sound-crossing times; no acoustic subcycling
    const std::vector<ma::AcousticCell> cells{{100100, 1 / bulk_modulus}, {99900, 1 / bulk_modulus}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, density, 0}};
    const auto step = ma::advance_acoustics(cells, faces, dt);
    const double factor = 1 / (1 + dt * dt * 2 * bulk_modulus / density);
    near(step.pressures_pa[0], 100000 + 100 * factor);
    near(step.pressures_pa[1], 100000 - 100 * factor);
    near(step.velocities_m_per_s[0], dt * 200 / density * factor);
    CHECK(step.pressure_iterations <= 2);
    CHECK(step.numerical_dissipation_j > 0);
    near(step.acoustic_energy_change_j + step.numerical_dissipation_j, 0, 0, 1e-10);
    near(step.body_force_work_j, 0, 0, 0);
    near(step.cell_volume_changes_m3[0] + step.cell_volume_changes_m3[1], 0, 0, 0);
    CHECK(cells[0].pressure_pa == 100100 && faces[0].velocity_m_per_s == 0);
}

TEST(acoustic_gravity_preserves_hydrostatic_gas_liquid_and_incompressible_states) {
    // Upward face normal: dp = rho*g*dy. Each bulk density is used exactly
    // once; individual constituents do not receive artificial sorting forces.
    const std::vector<ma::AcousticCell> cells{{200000, 1e-5}, {199988, 1e-8}, {189988, 0}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, 1.2, 0, -10}, {1, 2, 1, 1000, 0, -10}};
    const auto step = ma::advance_acoustics(cells, faces, 100);
    CHECK(step.pressures_pa == std::vector<double>({200000, 199988, 189988}));
    CHECK(step.velocities_m_per_s == std::vector<double>({0, 0}));
    CHECK(step.cell_volume_changes_m3 == std::vector<double>({0, 0, 0}));
    CHECK(step.pressure_iterations == 0);
    CHECK(step.acoustic_energy_change_j == 0 && step.body_force_work_j == 0);
}

TEST(incompressible_acoustics_removes_divergence_and_preserves_circulation) {
    const std::vector<ma::AcousticCell> cells(4, {100000, 0});
    const std::vector<ma::AcousticFace> faces{
        {0, 1, 1, 1000, 4}, {1, 2, 1, 1000, 0}, {2, 3, 1, 1000, 0}, {3, 0, 1, 1000, 0}};
    const auto step = ma::advance_acoustics(cells, faces, 0.1);
    // Orthogonal projection onto the divergence-free cyclic flow, which has
    // velocity equal to the initial circulation divided by the four edges.
    for (double velocity : step.velocities_m_per_s) near(velocity, 1, 0, 1e-11);
    for (double volume : step.cell_volume_changes_m3) near(volume, 0, 0, 1e-12);
    near(step.acoustic_energy_change_j, -6000);
    near(step.numerical_dissipation_j, 6000);
    near(step.acoustic_energy_change_j + step.numerical_dissipation_j, 0, 0, 1e-8);
    // Repeating a projected state must not dissipate its persistent circulation.
    auto projected = faces;
    for (auto& face : projected) face.velocity_m_per_s = 1;
    const auto next = ma::advance_acoustics(cells, projected, 100);
    for (double velocity : next.velocities_m_per_s) near(velocity, 1, 0, 0);
    CHECK(next.numerical_dissipation_j == 0);
}

TEST(acoustic_faces_balance_body_work_and_keep_separate_rooms_isolated) {
    const std::vector<ma::AcousticCell> cells{
        {100000, 1e-5}, {100000, 1e-9}, {200000, 1e-5}, {200000, 1e-5}, {300000, 0}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, 500, 0, -9.81}, {2, 3, 1, 1.2, 0}};
    const auto step = ma::advance_acoustics(cells, faces, 0.2);
    CHECK(step.velocities_m_per_s[0] < 0);
    CHECK(step.body_force_work_j > 0);
    near(step.acoustic_energy_change_j + step.numerical_dissipation_j, step.body_force_work_j, 1e-10, 1e-8);
    CHECK(step.pressures_pa[2] == 200000 && step.pressures_pa[3] == 200000 && step.pressures_pa[4] == 300000);
    CHECK(step.velocities_m_per_s[1] == 0);
    near(step.cell_volume_changes_m3[0], -step.cell_volume_changes_m3[1], 0, 0);
    CHECK(step.maximum_continuity_error_m3 < 1e-11);
}

TEST(acoustic_input_validation_rejects_undefined_states_before_publication) {
    const std::vector<ma::AcousticCell> cells{{100000, 1e-5}, {100000, 1e-5}};
    const std::vector<ma::AcousticFace> faces{{0, 1, 1, 1, 2}};
    const auto zero = ma::advance_acoustics(cells, faces, 0);
    CHECK(zero.velocities_m_per_s[0] == 2 && zero.pressures_pa[0] == 100000);
    rejects([&] { (void)ma::advance_acoustics(cells, faces, -1); });
    rejects([&] { (void)ma::advance_acoustics({}, {}, 1); });
    rejects([&] { (void)ma::advance_acoustics(std::vector<ma::AcousticCell>{{0, -1}}, {}, 1); });
    rejects([&] { (void)ma::advance_acoustics(cells, std::vector<ma::AcousticFace>{{0, 2, 1, 1, 0}}, 1); });
    rejects([&] { (void)ma::advance_acoustics(cells, std::vector<ma::AcousticFace>{{0, 1, 1, 0, 0}}, 1); });
    rejects([&] { (void)ma::advance_acoustics(cells, std::vector<ma::AcousticFace>{{0, 1, 1, 1, 0,
                  std::numeric_limits<double>::infinity()}}, 0); });
    CHECK(cells[0].pressure_pa == 100000 && faces[0].velocity_m_per_s == 2);
}

TEST(bulk_remap_moves_gas_liquid_and_trace_with_one_conservative_flux) {
    const auto r = ma::SpeciesRegistry::standard();
    auto mixture = ma::make_air(r, 1, 300);
    mixture.add(r, r.index("water"), 500, 300);
    mixture.add(r, r.index("chlorine"), 1e-16, 300);
    const double mass = mixture.mass();
    const std::vector<ma::TransportCell> cells{{mixture, 2, mass * 3, mass * -2}, {ma::MixtureInventory(r.size()), 2}};
    const auto next = ma::advect_mixtures(r, cells, std::vector<ma::SweptFace>{{0, 1, 0.5}});
    for (std::size_t s = 0; s < r.size(); ++s) {
        near(next[0].mixture.masses[s] + next[1].mixture.masses[s], mixture.masses[s], 1e-12, 0);
        near(next[1].mixture.masses[s], mixture.masses[s] / 4, 1e-12, 0);
    }
    CHECK(next[1].mixture.masses[r.index("chlorine")] > 0);
    for (const auto& c : next) {
        near(c.mixture.temperature(r), 300);
        near(c.momentum_x / c.mixture.mass(), 3);
        near(c.momentum_y / c.mixture.mass(), -2);
    }
    near(next[0].mixture.energy + next[0].kinetic_energy() + next[1].mixture.energy + next[1].kinetic_energy(),
         mixture.energy + cells[0].kinetic_energy());
    CHECK(cells[0].mixture.masses == mixture.masses && cells[1].mixture.mass() == 0);
}

TEST(liquid_volume_fraction_remains_bounded_under_solenoidal_remap) {
    const auto r = ma::SpeciesRegistry::standard();
    const double rho = r[r.index("water")].density;
    ma::MixtureInventory full(r.size()), half = ma::make_air(r, 0.5, 300), air = ma::make_air(r, 1, 300);
    full.add(r, r.index("water"), rho, 300);
    half.add(r, r.index("water"), rho * 0.5, 300);
    const std::vector<ma::TransportCell> cells{{full}, {half}, {air}};
    const std::vector<ma::SweptFace> faces{{0, 1, 0.25}, {1, 2, 0.25}, {2, 0, 0.25}};
    const auto next = ma::advect_mixtures(r, cells, faces);
    near(next[0].mixture.condensed_volume(r), 0.75);
    near(next[1].mixture.condensed_volume(r), 0.625);
    near(next[2].mixture.condensed_volume(r), 0.125);
    for (const auto& c : next) {
        CHECK(c.mixture.condensed_volume(r) <= 1);
        near(c.mixture.temperature(r), 300);
        near(c.mixture.pressure(r, 1), 101325);
    }
}

TEST(remap_converts_unresolved_velocity_variance_into_heat) {
    const auto r = ma::SpeciesRegistry::standard();
    ma::MixtureInventory gas(r.size()); gas.add(r, r.index("nitrogen"), 1, 300);
    const std::vector<ma::TransportCell> cells{{gas, 1, 0}, {gas, 1, 10}, {gas, 1, -10}};
    const std::vector<ma::SweptFace> faces{{0, 1, 0.5}, {1, 2, 0.5}, {2, 0, 0.5}};
    const auto next = ma::advect_mixtures(r, cells, faces);
    double kinetic = 0, thermal = 0, momentum = 0;
    for (const auto& c : next) {
        kinetic += c.kinetic_energy(); thermal += c.mixture.energy; momentum += c.momentum_x;
        CHECK(c.mixture.temperature(r) > 300);
    }
    near(kinetic, 25);
    near(thermal - 3 * gas.energy, 75, 1e-10, 1e-8);
    near(momentum, 0, 0, 1e-12);
}

TEST(remap_full_negative_face_transfer_leaves_no_residual_inventory) {
    const auto r = ma::SpeciesRegistry::standard();
    auto contents = ma::make_air(r, 1, 300);
    contents.add(r, r.index("water"), 1, 300);
    contents.add(r, r.index("chlorine"), 1e-16, 300);
    const std::vector<ma::TransportCell> cells{{ma::MixtureInventory(r.size()), 2}, {contents, 2, 7, -4}};
    const auto next = ma::advect_mixtures(r, cells, std::vector<ma::SweptFace>{{0, 1, -2}});
    CHECK(next[1].mixture.mass() == 0 && next[1].mixture.energy == 0);
    CHECK(next[1].momentum_x == 0 && next[1].momentum_y == 0);
    CHECK(next[0].mixture.masses == contents.masses);
    near(next[0].mixture.energy, contents.energy);
    CHECK(next[0].momentum_x == 7 && next[0].momentum_y == -4);
}

TEST(remap_rejects_overfill_cfl_and_solids_without_losing_input) {
    const auto r = ma::SpeciesRegistry::standard();
    ma::MixtureInventory liquid(r.size()), solid(r.size());
    liquid.add(r, r.index("water"), r[r.index("water")].density, 300);
    solid.add(r, r.index("ice"), 1, 250);
    const std::vector<ma::TransportCell> cells{{liquid}, {liquid}};
    rejects([&] { (void)ma::advect_mixtures(r, cells, std::vector<ma::SweptFace>{{0, 1, 0.1}}); });
    rejects([&] { (void)ma::advect_mixtures(r, cells, std::vector<ma::SweptFace>{{0, 1, 1.1}}); });
    rejects([&] { (void)ma::advect_mixtures(r, std::vector<ma::TransportCell>{{solid}}, {}); });
    rejects([&] { (void)ma::advect_mixtures(r, std::vector<ma::TransportCell>{{ma::MixtureInventory(r.size()), 1, 1}}, {}); });
    CHECK(cells[0].mixture.masses == liquid.masses && cells[1].mixture.energy == liquid.energy);
}

TEST(thermal_network_conserves_energy_and_handles_stiff_contacts) {
    const std::vector<ma::ThermalBody> bodies{{1000, 250}, {2000, 900}, {500, 400}, {1000, 300}};
    const std::vector<ma::ThermalContact> contacts{{0, 1, 200}, {1, 2, 300}, {2, 3, 0}};
    const auto result = ma::conduct_heat(bodies, contacts, 100000);
    double sum = 0;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        sum += result.energy_changes_j[i];
        CHECK(result.temperatures_k[i] >= 250 && result.temperatures_k[i] <= 900);
    }
    near(sum, 0, 0, 1e-8);
    near(result.temperatures_k[3], 300, 0, 0);
    const double equilibrium = (1000.0 * 250 + 2000.0 * 900 + 500.0 * 400) / 3500;
    for (std::size_t i = 0; i < 3; ++i) near(result.temperatures_k[i], equilibrium, 0, 0.04);
    near(ma::thermal_conductance(1, 10, 0.5, 0.5, 2), 2 / 0.55);
    near(ma::thermal_conductance(0, 10, 0.5, 0.5, 2), 0, 0, 0);
}

TEST(heat_exchange_has_no_equilibrium_leak_and_rejects_invalid_contacts) {
    const std::vector<ma::ThermalBody> bodies{{1000, 300}, {2000, 300}};
    const std::vector<ma::ThermalContact> contacts{{0, 1, 1e12}};
    const auto result = ma::conduct_heat(bodies, contacts, 1);
    CHECK(result.energy_changes_j == std::vector<double>({0, 0}));
    CHECK(result.entropy_produced_j_per_k == 0);
    rejects([&] { (void)ma::conduct_heat(bodies, contacts, -1); });
    rejects([&] { (void)ma::conduct_heat(bodies, std::vector<ma::ThermalContact>{{0, 2, 1}}, 1); });
    rejects([&] { (void)ma::conduct_heat(std::vector<ma::ThermalBody>{{0, 300}}, {}, 1); });
    rejects([&] { (void)ma::thermal_conductance(-1, 10, 0.5, 0.5, 1); });
}

TEST(bulk_pump_preserves_composition_and_temperature) {
    const auto r = ma::SpeciesRegistry::standard(); auto source = ma::make_air(r, 1); ma::MixtureInventory target(r.size());
    const double initial = source.mass();
    near(ma::transfer(r, source, target, initial / 3, initial, r.size()), initial / 3);
    near(target.temperature(r), 293.15); near(source.temperature(r), 293.15);
    near(target.masses[r.index("oxygen")] / target.mass(), source.masses[r.index("oxygen")] / source.mass());
}

TEST(invalid_inventory_mutations_are_atomic) {
    const auto r = ma::SpeciesRegistry::standard(); auto m = ma::make_air(r, 1); const auto before = m;
    rejects([&] { m.add(r, 0, -1, 300); });
    rejects([&] { m.add(r, 0, 1, std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { (void)m.extract(r, 1, r.size() + 1); });
    CHECK(m.masses == before.masses); CHECK(m.energy == before.energy);
}

TEST(invalid_material_properties_cannot_enter_a_registry) {
    ma::SpeciesRegistry r;
    auto species = ma::SpeciesRegistry::standard()[0];
    species.phase = static_cast<ma::Phase>(99);
    rejects([&] { r.add(species); });
    species.phase = ma::Phase::gas;
    species.atoms = {{"O", std::numeric_limits<double>::denorm_min()}};
    rejects([&] { r.add(species); });
    species.atoms = {{"O", 2}};
    species.formation_enthalpy = std::numeric_limits<double>::max();
    rejects([&] { r.add(species); });
    CHECK(r.size() == 0);
    const auto standard = ma::SpeciesRegistry::standard();
    const auto air = ma::make_air(standard, 1);
    rejects([&] { (void)air.pressure(standard, std::numeric_limits<double>::min()); });
    ma::ReactionDefinition invalid{"overflow", {{0, -std::numeric_limits<double>::max()}, {1, std::numeric_limits<double>::max()}}};
    rejects([&] { invalid.validate(standard); });
}

TEST(breathing_inclusive_limits_and_partial_pressure) {
    const auto r = ma::SpeciesRegistry::standard();
    CHECK(ma::assess_atmosphere(r, ma::make_air(r, 1, 300, 100000, 0.15), 1).breathable());
    CHECK(ma::assess_atmosphere(r, ma::make_air(r, 1, 300, 100000, 0.35), 1).breathable());
    CHECK(!ma::assess_atmosphere(r, ma::make_air(r, 1, 300, 100000, 0.149999), 1).breathable());
    CHECK(!ma::assess_atmosphere(r, ma::make_air(r, 1, 300, 100000, 0.350001), 1).breathable());
    ma::AtmosphereSettings easy; easy.hard_mode = false;
    CHECK(ma::assess_atmosphere(r, ma::make_air(r, 1, 300, 100000, 1), 1, easy).breathable());
    const auto thin = ma::assess_atmosphere(r, ma::make_air(r, 1, 300, 10000, 1), 1, easy);
    CHECK(thin.failures & static_cast<std::uint32_t>(ma::BreathingFailure::oxygen_partial_pressure_low));
    CHECK(ma::assess_atmosphere(r, ma::MixtureInventory(r.size()), 1).failures == 1);
}

TEST(co2_and_toxic_traces_are_independently_checked) {
    const auto r = ma::SpeciesRegistry::standard(); auto m = ma::make_air(r, 1, 300, 100000);
    m.add(r, r.index("carbon_dioxide"), 0.03, 300);
    auto result = ma::assess_atmosphere(r, m, 1);
    CHECK(result.failures & static_cast<std::uint32_t>(ma::BreathingFailure::carbon_dioxide_fraction_high));
    CHECK(result.failures & static_cast<std::uint32_t>(ma::BreathingFailure::carbon_dioxide_partial_pressure_high));
    auto contaminated = ma::make_air(r, 1); contaminated.add(r, r.index("chlorine"), 1e-4, 293.15);
    CHECK(ma::assess_atmosphere(r, contaminated, 1).failures & static_cast<std::uint32_t>(ma::BreathingFailure::contaminant));
    ma::AtmosphereSettings invalid; invalid.minimum_oxygen_fraction = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { (void)ma::assess_atmosphere(r, contaminated, 1, invalid); });
}

TEST(ultra_trace_and_zero_limits_preserve_hazard_warnings) {
    const auto standard = ma::SpeciesRegistry::standard();
    for (const double limit : {0.0, 1e-20}) {
        ma::SpeciesRegistry registry;
        for (auto species : standard.all()) {
            if (species.id == "chlorine") species.max_mole_fraction = limit;
            registry.add(std::move(species));
        }
        auto mixture = ma::make_air(registry, 1, 300, 101325);
        const auto chlorine = registry.index("chlorine");
        const double trace_mass = limit == 0 ? std::numeric_limits<double>::denorm_min() :
            mixture.gas_moles(registry) * 2e-20 * registry[chlorine].molar_mass;
        mixture.add(registry, chlorine, trace_mass, 300);
        CHECK(ma::assess_atmosphere(registry, mixture, 1).failures & static_cast<std::uint32_t>(ma::BreathingFailure::contaminant));
    }
    const auto registry = ma::SpeciesRegistry::standard();
    auto mixture = ma::make_air(registry, 1, 300, 101325);
    mixture.add(registry, registry.index("carbon_dioxide"), 1e-25, 300);
    ma::AtmosphereSettings settings; settings.maximum_co2_fraction = 0; settings.maximum_co2_partial_pressure = 0;
    const auto assessment = ma::assess_atmosphere(registry, mixture, 1, settings);
    CHECK(assessment.failures & static_cast<std::uint32_t>(ma::BreathingFailure::carbon_dioxide_fraction_high));
    CHECK(assessment.failures & static_cast<std::uint32_t>(ma::BreathingFailure::carbon_dioxide_partial_pressure_high));
}

TEST(all_standard_reactions_balance_atoms) {
    const auto r = ma::SpeciesRegistry::standard();
    for (const auto& reaction : ma::standard_reactions(r)) {
        reaction.validate(r); double mass = 0;
        for (const auto& term : reaction.terms) mass += term.moles * r[term.species].molar_mass;
        near(mass, 0, 0, 1e-15);
    }
    ma::ReactionDefinition broken{"carbon_deletion", {{r.index("methane"), -1}, {r.index("water"), 1}}};
    rejects([&] { broken.validate(r); });
}

TEST(electrolysis_requires_energy_and_roundtrip_cannot_create_it) {
    const auto r = ma::SpeciesRegistry::standard(); const auto electrolysis = ma::standard_reactions(r)[0];
    ma::MixtureInventory m(r.size()); m.add(r, r.index("water"), 2 * r[r.index("water")].molar_mass, 298.15);
    const auto before = m;
    rejects([&] { ma::react(r, m, electrolysis, 1); });
    CHECK(m.masses == before.masses); CHECK(m.energy == before.energy);
    double needed = 0;
    for (const auto& term : electrolysis.terms)
        needed += term.moles * r[term.species].molar_mass * r[term.species].internal_energy(298.15);
    ma::react(r, m, electrolysis, 1, needed, 0);
    near(m.temperature(r), 298.15); near(m.mass(), before.mass()); near(m.energy, before.energy + needed);
    ma::ReactionDefinition reverse = electrolysis; reverse.id = "hydrogen_to_water";
    for (auto& term : reverse.terms) term.moles *= -1;
    ma::react(r, m, reverse, 1, 0, needed);
    near(m.energy, before.energy); near(m.temperature(r), 298.15); near(m.mass(), before.mass());
}

TEST(metabolism_accounts_for_carbon_and_water) {
    const auto r = ma::SpeciesRegistry::standard(); const auto metabolism = ma::standard_reactions(r)[4];
    ma::MixtureInventory m(r.size());
    m.add(r, r.index("glucose"), r[r.index("glucose")].molar_mass, 310);
    m.add(r, r.index("oxygen"), 6 * r[r.index("oxygen")].molar_mass, 310);
    const double mass = m.mass(), energy = m.energy;
    ma::react(r, m, metabolism, 1);
    near(m.mass(), mass); near(m.energy, energy);
    near(m.masses[r.index("carbon_dioxide")], 6 * r[r.index("carbon_dioxide")].molar_mass);
    near(m.masses[r.index("water")], 6 * r[r.index("water")].molar_mass);
    CHECK(m.temperature(r) > 310);
}

TEST(evaporation_conserves_mass_energy_and_matches_partial_pressure) {
    const auto r = ma::SpeciesRegistry::standard(); auto m = ma::make_air(r, 1, 350);
    m.add(r, r.index("water"), 1, 350);
    const double energy = m.energy, mass = m.mass(); const auto pair = ma::water_vapor_pair(r);
    ma::equilibrate_vapor_liquid(r, m, pair, 1);
    near(m.mass(), mass); CHECK(m.energy == energy); CHECK(m.temperature(r) < 350);
    CHECK(m.masses[r.index("steam")] > 0); CHECK(m.masses[r.index("water")] > 0);
    const double t = m.temperature(r);
    const double actual = m.masses[pair.vapor] / r[pair.vapor].molar_mass * ma::gas_constant * t / (1 - m.condensed_volume(r));
    const double expected = pair.reference_pressure * std::exp(-pair.latent_heat * r[pair.vapor].molar_mass / ma::gas_constant * (1 / t - 1 / pair.reference_temperature));
    near(actual, expected, 1e-9);
    const auto before = m; ma::equilibrate_vapor_liquid(r, m, pair, 1);
    near(m.temperature(r), before.temperature(r)); near(m.masses[pair.vapor], before.masses[pair.vapor]);
}

TEST(melting_spends_latent_heat_without_changing_temperature) {
    const auto r = ma::SpeciesRegistry::standard(); const auto ice = r.index("ice"), water = r.index("water");
    ma::MixtureInventory m(r.size()); m.add(r, ice, 1, 273.15);
    m.energy += 333550.0 / 2;
    const double energy = m.energy;
    ma::equilibrate_solid_liquid(r, m, ice, water, 273.15);
    near(m.masses[water], 0.5); near(m.masses[ice], 0.5); near(m.temperature(r), 273.15);
    CHECK(m.energy == energy);
}

TEST(unsupported_phase_states_fail_without_mutation) {
    const auto r = ma::SpeciesRegistry::standard(); ma::MixtureInventory m(r.size()); m.add(r, r.index("water"), 1000, 300);
    const auto before = m;
    rejects([&] { ma::equilibrate_vapor_liquid(r, m, ma::water_vapor_pair(r), 1); });
    CHECK(m.masses == before.masses); CHECK(m.energy == before.energy);
}

TEST(uniform_air_remains_stationary) {
    ma::GasWorld w(4, 4, 1, 1, quiet());
    const auto air = ma::make_air(w.registry(), 1);
    for (std::size_t y = 0; y < 4; ++y) for (std::size_t x = 0; x < 4; ++x) w.set_cell(x, y, air);
    (void)w.step(0.02);
    for (std::size_t y = 0; y < 4; ++y) for (std::size_t x = 0; x < 4; ++x) {
        near(w.cell(x, y).momentum_x, 0); near(w.cell(x, y).momentum_y, 0);
        near(w.cell(x, y).mixture.mass(), air.mass());
    }
}

TEST(pressure_flows_into_lower_pressure_without_losing_material) {
    ma::GasWorld w(10, 2, 1, 1, quiet());
    for (std::size_t y = 0; y < 2; ++y) for (std::size_t x = 0; x < 10; ++x)
        w.set_cell(x, y, ma::make_air(w.registry(), 1, 300, x < 5 ? 150000 : 50000));
    const auto before = w.totals(); const double low_mass = w.cell(5, 0).mixture.mass();
    (void)w.step(0.01); CHECK(w.cell(5, 0).mixture.mass() > low_mass);
    const auto after = w.totals();
    for (std::size_t i = 0; i < before.masses.size(); ++i) near(after.masses[i], before.masses[i]);
    near(after.energy(), before.energy());
}

TEST(hydrostatic_air_does_not_develop_spurious_currents) {
    ma::GasWorld w(2, 16, 1, 1, {9.81, 0, false, false}); const auto& r = w.registry();
    const auto air = ma::make_air(r, 1, 300, 101325);
    const double specific_r = air.gas_moles(r) * ma::gas_constant / air.mass();
    for (std::size_t y = 0; y < 16; ++y) for (std::size_t x = 0; x < 2; ++x)
        w.set_cell(x, y, ma::make_air(r, 1, 300, 101325 * std::exp(-9.81 * (static_cast<double>(y) + 0.5) / (specific_r * 300))));
    const auto before = w.totals(); (void)w.step(0.03);
    for (std::size_t y = 0; y < 16; ++y) for (std::size_t x = 0; x < 2; ++x)
        near(w.cell(x, y).momentum_y, 0, 0, 1e-8);
    near(w.totals().energy(), before.energy());
}

TEST(hot_gas_parcel_accelerates_upward) {
    ma::GasWorld w(7, 9, 1, 1, {9.81, 0, false, false}); const auto& r = w.registry();
    const auto air = ma::make_air(r, 1, 300, 101325); const double rs = air.gas_moles(r) * ma::gas_constant / air.mass();
    for (std::size_t y = 0; y < 9; ++y) for (std::size_t x = 0; x < 7; ++x) {
        const double p = 101325 * std::exp(-9.81 * (static_cast<double>(y) + 0.5) / (rs * 300));
        w.set_cell(x, y, ma::make_air(r, 1, (x >= 2 && x <= 4 && y >= 3 && y <= 5) ? 400 : 300, p));
    }
    const auto before = w.totals(); (void)w.step(0.03);
    CHECK(w.cell(3, 4).momentum_y > 0); near(w.totals().energy(), before.energy());
}

TEST(diffusion_mixes_species_without_erasing_them) {
    ma::GasWorld w(2, 1, 1, 1, {0, 0.1, false, false});
    w.set_cell(0, 0, ma::make_air(w.registry(), 1, 300, 101325, 0.1));
    w.set_cell(1, 0, ma::make_air(w.registry(), 1, 300, 101325, 0.3));
    const auto before = w.totals(); const auto oxygen = w.registry().index("oxygen");
    const double old_o2 = w.cell(0, 0).mixture.masses[oxygen];
    (void)w.step(0.02); CHECK(w.cell(0, 0).mixture.masses[oxygen] > old_o2);
    const auto after = w.totals();
    for (std::size_t i = 0; i < before.masses.size(); ++i) near(after.masses[i], before.masses[i]);
    near(after.energy(), before.energy());
}

TEST(space_loss_is_included_in_conservation_ledger) {
    ma::GasWorld w(2, 3, 1, 1, {9.81, 0, false, true});
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 2; ++x) w.set_cell(x, y, ma::make_air(w.registry(), 1));
    const auto before = w.totals(); (void)w.step(0.003); const auto after = w.totals();
    CHECK(w.escaped_masses()[0] > 0);
    for (std::size_t i = 0; i < before.masses.size(); ++i) near(after.masses[i] + w.escaped_masses()[i], before.masses[i]);
    near(after.energy() + w.escaped_energy(), before.energy());
}

TEST(failed_timestep_rolls_back_world_and_escape_ledger) {
    ma::GasWorld w(2, 2, 1, 1, {0, 0, false, true}); w.set_cell(0, 0, ma::make_air(w.registry(), 1));
    const auto before = w.save(); rejects([&] { (void)w.step(1, 1); }); CHECK(w.save() == before);
}

TEST(walls_cannot_destroy_fluid_and_reference_grid_rejects_liquids) {
    ma::GasWorld w(2, 2); const auto& r = w.registry(); w.set_cell(0, 0, ma::make_air(r, 1));
    rejects([&] { w.set_wall(0, 0, true); });
    ma::MixtureInventory liquid(r.size()); liquid.add(r, r.index("water"), 1, 300);
    rejects([&] { w.set_cell(1, 1, liquid); }); CHECK(w.cell(1, 1).mixture.mass() == 0);
}

TEST(save_roundtrip_continues_identically) {
    ma::GasWorld w(3, 3, 1, 1, quiet());
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 3; ++x)
        w.set_cell(x, y, ma::make_air(w.registry(), 1, 300, 80000 + 10000 * static_cast<double>(x)));
    (void)w.step(0.002); const auto bytes = w.save(); auto restored = ma::GasWorld::load(bytes);
    CHECK(restored.save() == bytes); (void)w.step(0.003); (void)restored.step(0.003); CHECK(w.save() == restored.save());
    auto corrupt = bytes; corrupt[corrupt.size() / 2] ^= 1; rejects([&] { (void)ma::GasWorld::load(corrupt); });
    rejects([&] { (void)ma::GasWorld::load(std::span(bytes).first(bytes.size() - 1)); });
}

TEST(saved_species_are_identified_by_stable_names) {
    ma::GasWorld w(1, 1); const auto air = ma::make_air(w.registry(), 1); w.set_cell(0, 0, air);
    ma::SpeciesRegistry reordered;
    const auto& all = w.registry().all();
    for (auto i = all.rbegin(); i != all.rend(); ++i) reordered.add(*i);
    auto restored = ma::GasWorld::load(w.save(), reordered);
    near(restored.cell(0, 0).mixture.masses[reordered.index("oxygen")], air.masses[w.registry().index("oxygen")]);
    near(restored.cell(0, 0).mixture.temperature(reordered), 293.15);
}

TEST(ten_thousand_steps_conserve_mass_and_energy) {
    ma::GasWorld w(2, 2, 1, 1, {9.81, 2e-5, true, false});
    for (std::size_t y = 0; y < 2; ++y) for (std::size_t x = 0; x < 2; ++x)
        w.set_cell(x, y, ma::make_air(w.registry(), 1, 290 + 10 * static_cast<double>(y), 100000 + 1000 * static_cast<double>(x)), 0.01, 0);
    const auto before = w.totals();
    for (int i = 0; i < 10000; ++i) (void)w.step(0.00001);
    const auto after = w.totals();
    for (std::size_t i = 0; i < before.masses.size(); ++i) near(after.masses[i], before.masses[i], 1e-9);
    near(after.energy(), before.energy(), 1e-9);
}

TEST(c_abi_checks_buffers_and_contains_exceptions) {
    CHECK(ma_abi_version() == 1); ma_world* w = nullptr;
    ma_world_config config{2, 1, 1, 1, 0, 0, 0, 0}; CHECK(ma_create_world(&config, &w) == MA_OK);
    struct Cleanup { ma_world* w; ~Cleanup() { ma_destroy_world(w); } } cleanup{w};
    uint32_t count = 0; CHECK(ma_species_count(w, &count) == MA_OK);
    std::vector<double> masses(count, 0); masses[0] = 0.25; masses[1] = 0.75;
    CHECK(ma_set_cell(w, 0, 0, masses.data(), count, 300, 0, 0) == MA_OK);
    ma_sample sample{}; CHECK(ma_get_cell(w, 0, 0, &sample, masses.data(), count - 1) == MA_BUFFER_TOO_SMALL);
    CHECK(ma_get_cell(w, 0, 0, &sample, masses.data(), count) == MA_OK); near(sample.total_mass, 1);
    CHECK(ma_set_cell(w, 10, 0, masses.data(), count, 300, 0, 0) == MA_INVALID_ARGUMENT);
    CHECK(std::string(ma_last_error()).size() > 0);
    uint64_t size = 0; CHECK(ma_save(w, nullptr, 0, &size) == MA_OK);
    std::vector<uint8_t> save(static_cast<std::size_t>(size)); CHECK(ma_save(w, save.data(), size, &size) == MA_OK);
    ma_world* loaded = nullptr; CHECK(ma_load(save.data(), size, &loaded) == MA_OK); ma_destroy_world(loaded);
    CHECK(ma_load(nullptr, 0, &loaded) == MA_INVALID_ARGUMENT); CHECK(loaded == nullptr);
    CHECK(ma_get_config(nullptr, &config) == MA_INVALID_ARGUMENT);
}
}

int main() {
    int failures = 0;
    for (const auto& [name, test] : tests()) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    std::cout << (tests().size() - static_cast<std::size_t>(failures)) << '/' << tests().size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
