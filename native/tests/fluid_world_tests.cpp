#include "mixed_atmospheres/fluid_world.hpp"
#include "mixed_atmospheres/transport.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double a, double b, double relative = 1e-10, double absolute = 0) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > absolute + relative * std::abs(b))
        throw std::runtime_error("Expected " + std::to_string(b) + ", got " + std::to_string(a));
}
template<class F> void rejects(F&& f) {
    bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid operation was accepted");
}
ma::FluidSolverSettings quiet() {
    ma::FluidSolverSettings s; s.gravity = 0; s.conduction = false;
    s.diffusivity_m2_per_s = 0; s.viscosity_m2_per_s = 0; return s;
}
ma::MixtureInventory wet_air(const ma::SpeciesRegistry& r, double alpha, double t, double p, double trace = 0) {
    auto m = ma::make_air(r, 1 - alpha, t, p);
    m.add(r, r.index("water"), alpha * r[r.index("water")].density, t);
    m.add(r, r.index("helium"), trace, t);
    return m;
}
void conserved(const ma::ConservationTotals& before, const ma::ConservationTotals& after,
               double sensible_scale, double tolerance = 1e-8) {
    for (std::size_t s = 0; s < before.masses.size(); ++s) near(after.masses[s], before.masses[s], tolerance);
    near(after.energy(), before.energy(), 0, sensible_scale * tolerance);
}

void geometric_remap_preserves_full_liquid_and_traces() {
    const auto r = ma::SpeciesRegistry::standard();
    auto first = wet_air(r, 0.75, 300, 100000, 1e-28);
    auto second = wet_air(r, 0.25, 300, 100000, 1e-32);
    // Left Lagrangian volume expanded by .2; remap that same .2 rightward.
    const std::vector<ma::TransportCell> moving{{first, 1.2, 1, 2}, {second, 0.8, -1, -2}};
    const std::vector<ma::SweptFace> faces{{0, 1, 0.2}};
    const std::vector<double> targets{1, 1}, potentials{0, 9.81};
    const auto next = ma::remap_material_mixtures(r, moving, faces, targets, potentials);
    near(next[0].volume_m3, 1); near(next[1].volume_m3, 1);
    for (std::size_t s = 0; s < r.size(); ++s)
        near(next[0].mixture.masses[s] + next[1].mixture.masses[s], first.masses[s] + second.masses[s]);
    near(next[0].momentum_x + next[1].momentum_x, 0, 0, 1e-14);
    near(next[0].momentum_y + next[1].momentum_y, 0, 0, 1e-14);
    const double olde = first.energy + second.energy + moving[0].kinetic_energy() + moving[1].kinetic_energy() + second.mass() * 9.81;
    const double newe = next[0].mixture.energy + next[1].mixture.energy + next[0].kinetic_energy() + next[1].kinetic_energy() + next[1].mixture.mass() * 9.81;
    near(newe, olde, 1e-14);
    rejects([&] { (void)ma::remap_material_mixtures(r, moving, faces, std::vector<double>{1.01, 0.99}); });
    ma::MixtureInventory wa(r.size()), wb(r.size());
    wa.add(r, r.index("water"), 1.2 * r[r.index("water")].density, 300);
    wb.add(r, r.index("water"), 0.8 * r[r.index("water")].density, 300);
    const std::vector<ma::TransportCell> liquid{{wa, 1.2}, {wb, 0.8}};
    const auto filled = ma::remap_material_mixtures(r, liquid, faces, targets);
    near(filled[0].mixture.condensed_volume(r), 1); near(filled[1].mixture.condensed_volume(r), 1);
}

void pressure_remap_moves_both_phases_and_preserves_inventory() {
    ma::FluidWorld w(2, 1, 1, 1, quiet()); const auto& r = w.registry();
    w.set_cell(0, 0, wet_air(r, 0.2, 300, 150000, 1e-28));
    w.set_cell(1, 0, wet_air(r, 0.6, 310, 50000, 1e-32));
    const auto before = w.totals(); const double water = w.cell(0, 0).mixture.masses[r.index("water")];
    const auto step = w.step(0.1);
    check(step.nonlinear_iterations > 0 && step.pressure_iterations > 0, "Integrated pressure solve was bypassed");
    check(w.cell(0, 0).mixture.masses[r.index("water")] < water, "Liquid did not follow pressure-driven mixture motion");
    check(w.cell(1, 0).mixture.masses[r.index("helium")] > 1e-32, "Trace constituent was lost in remap");
    check(w.pressure(0, 0) < 150000 && w.pressure(1, 0) > 50000, "Pressure failed to relax");
    check(w.liquid_fraction(0, 0) <= 1 && w.liquid_fraction(1, 0) <= 1, "Condensed phase overfilled mesh");
    conserved(before, w.totals(), 1e9, 1e-11);
    check(step.substeps < 100, "Integrated solver still requires acoustic CFL steps");
}

void gas_pressure_moves_a_full_liquid_slug_without_overfilling() {
    ma::FluidWorld w(3, 1, 1, 1, quiet()); const auto& r = w.registry();
    ma::MixtureInventory water(r.size()); water.add(r, r.index("water"), r[r.index("water")].density, 300);
    auto gas = ma::make_air(r, 1, 300, 150000); gas.add(r, r.index("helium"), 1e-28, 300);
    w.set_cell(0, 0, gas); w.set_cell(1, 0, water); w.set_cell(2, 0, ma::make_air(r, 1, 300, 50000));
    const auto before = w.totals(); (void)w.step(0.005);
    check(w.liquid_fraction(1, 0) < 1 && w.liquid_fraction(2, 0) > 0,
          "Gas pressure failed to displace an incompressible liquid slug");
    check(w.cell(1, 0).mixture.masses[r.index("helium")] > 0, "Gas trace failed to cross displaced liquid interface");
    for (std::size_t x = 0; x < 3; ++x) check(w.liquid_fraction(x, 0) <= 1, "Liquid slug remap overfilled cell");
    conserved(before, w.totals(), 1e9, 1e-11);
}

void pure_gas_takes_an_implicit_step_above_acoustic_cfl() {
    ma::FluidWorld w(2, 1, 1, 1, quiet()); const auto& r = w.registry();
    w.set_cell(0, 0, ma::make_air(r, 1, 300, 100010));
    w.set_cell(1, 0, ma::make_air(r, 1, 300, 99990));
    const auto before = w.totals(); const auto result = w.step(0.1);
    check(result.substeps == 1 && result.rejected_substeps == 0, "Low-amplitude gas case fell back to acoustic CFL");
    check(w.pressure(0, 0) < 100010 && w.pressure(1, 0) > 99990, "Implicit gas response did not reduce pressure jump");
    conserved(before, w.totals(), 1e6, 1e-10);
}

void equilibrium_contacts_remain_stationary_without_numerical_sorting() {
    ma::FluidWorld w(4, 2, 1, 1, quiet()); const auto& r = w.registry();
    for (std::size_t y = 0; y < 2; ++y) for (std::size_t x = 0; x < 4; ++x)
        w.set_cell(x, y, wet_air(r, 0.1 + 0.2 * static_cast<double>(x), 300, 100000, 1e-25));
    const auto before = w.totals(); const auto mass = w.cell(0, 0).mixture.masses;
    const auto step = w.step(0.1);
    check(step.substeps == 1, "Equilibrium required acoustic subcycling");
    conserved(before, w.totals(), 1e9, 1e-12);
    for (std::size_t s = 0; s < mass.size(); ++s) near(w.cell(0, 0).mixture.masses[s], mass[s], 1e-12);
    near(w.cell(0, 0).momentum_x, 0, 0, 1e-10);
}

void hydrostatic_liquid_and_gas_remain_balanced() {
    auto settings = quiet(); settings.gravity = 9.81;
    ma::FluidWorld w(1, 4, 1, 1, settings); const auto& r = w.registry();
    ma::MixtureInventory water(r.size()); water.add(r, r.index("water"), r[r.index("water")].density, 300);
    const auto top = ma::make_air(r, 1, 300, 100000);
    const double beta = top.mass() / 100000;
    const double pg = 100000 * (1 + 0.5 * beta * settings.gravity) / (1 - 0.5 * beta * settings.gravity);
    const auto lower_gas = ma::make_air(r, 1, 300, pg);
    const double pw = pg + 0.5 * (water.mass() + lower_gas.mass()) * settings.gravity;
    w.set_cell(0, 0, water, 0, 0, pw + water.mass() * settings.gravity);
    w.set_cell(0, 1, water, 0, 0, pw); w.set_cell(0, 2, lower_gas); w.set_cell(0, 3, top);
    const auto before = w.totals(); (void)w.step(0.1, 500);
    conserved(before, w.totals(), 1e9, 1e-12);
    for (std::size_t y = 0; y < 4; ++y) near(w.cell(0, y).momentum_y, 0, 0, 1e-8);
    near(w.liquid_fraction(0, 0), 1); near(w.liquid_fraction(0, 1), 1);
}

void long_closed_multiphase_run_preserves_each_trace_and_total_energy() {
    ma::FluidSolverSettings settings; settings.gravity = 9.81;
    settings.diffusivity_m2_per_s = 0.005; settings.viscosity_m2_per_s = 0.01;
    ma::FluidWorld w(3, 3, 1, 1, settings); const auto& r = w.registry();
    double sensible = 0;
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 3; ++x) {
        auto m = wet_air(r, 0.1 + 0.1 * static_cast<double>(y), 290 + 5 * static_cast<double>(x),
                         100000 + 100 * static_cast<double>(x), x == 0 && y == 0 ? 1e-28 : 0);
        sensible += m.heat_capacity(r) * m.temperature(r);
        w.set_cell(x, y, m, 0.01 * static_cast<double>(x), 0);
    }
    const auto before = w.totals();
    for (int i = 0; i < 10000; ++i) (void)w.step(0.00001);
    conserved(before, w.totals(), sensible, 1e-8);
    const auto after = w.totals(); double drift = 0;
    for (std::size_t s = 0; s < before.masses.size(); ++s) if (before.masses[s] > 0)
        drift = std::max(drift, std::abs(after.masses[s] - before.masses[s]) / before.masses[s]);
    std::cout << std::setprecision(12) << "  10000-step maximum constituent drift=" << drift
              << "; energy drift / initial sensible energy=" << std::abs(after.energy() - before.energy()) / sensible << '\n';
    for (std::size_t y = 0; y < 3; ++y) for (std::size_t x = 0; x < 3; ++x) {
        check(w.liquid_fraction(x, y) > 0 && w.liquid_fraction(x, y) < 1, "Multiphase volume bounds were lost");
        check(w.cell(x, y).mixture.temperature(r) > 0, "Negative thermal state published");
    }
    check(w.cell(1, 0).mixture.masses[r.index("helium")] > 0, "Trace gas did not diffuse");
}

void tangential_viscosity_dissipates_into_heat_without_momentum_loss() {
    auto settings = quiet(); settings.viscosity_m2_per_s = 0.1;
    ma::FluidWorld w(2, 1, 1, 1, settings); const auto& r = w.registry();
    const auto m = wet_air(r, 0.3, 300, 100000);
    w.set_cell(0, 0, m, 0, 10); w.set_cell(1, 0, m, 0, -10);
    const auto before = w.totals(); (void)w.step(0.1);
    const auto after = w.totals();
    check(after.kinetic_energy < before.kinetic_energy, "Viscosity failed to dissipate shear");
    check(after.internal_energy > before.internal_energy, "Viscous kinetic energy failed to become heat");
    near(w.cell(0, 0).momentum_y + w.cell(1, 0).momentum_y, 0, 0, 1e-12);
    conserved(before, after, 1e9, 1e-12);
}

void smaller_step_retry_completes_without_losing_inventory() {
    auto settings = quiet(); settings.maximum_outgoing_fraction = 0.05;
    ma::FluidWorld w(2, 1, 1, 1, settings); const auto& r = w.registry();
    w.set_cell(0, 0, wet_air(r, 0.2, 300, 1000000, 1e-28));
    w.set_cell(1, 0, wet_air(r, 0.2, 300, 10000));
    const auto before = w.totals(); const auto step = w.step(0.1, 10000);
    check(step.rejected_substeps > 0 && step.substeps > 1, "CFL failure did not retry with smaller steps");
    near(step.seconds, 0.1); near(w.time(), 0.1);
    conserved(before, w.totals(), 1e9, 1e-10);
}

void invalid_states_and_failed_step_leave_every_cell_unchanged() {
    ma::FluidWorld w(2, 1, 1, 1, quiet()); const auto& r = w.registry();
    w.set_cell(0, 0, wet_air(r, 0.1, 300, 1e6));
    w.set_cell(1, 0, wet_air(r, 0.9, 300, 1e4));
    const auto before = w.totals(); const auto m = w.cell(0, 0).mixture.masses;
    rejects([&] { (void)w.step(0.2, 1); });
    check(w.cell(0, 0).mixture.masses == m && w.time() == 0, "Failed multi-step call partially committed");
    conserved(before, w.totals(), 1e9, 0);
    rejects([&] { (void)w.step(-1); });
    rejects([&] { (void)w.step(std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { w.set_cell(0, 0, wet_air(r, 1.1, 300, 100000)); });
    ma::MixtureInventory partial(r.size()); partial.add(r, r.index("water"), 1, 300);
    rejects([&] { w.set_cell(0, 0, partial); });
    ma::FluidWorld empty(1, 1); rejects([&] { (void)empty.step(0.1); });
}
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"geometric remap", geometric_remap_preserves_full_liquid_and_traces},
        {"pressure driven multiphase transport", pressure_remap_moves_both_phases_and_preserves_inventory},
        {"gas pressure displaces full liquid slug", gas_pressure_moves_a_full_liquid_slug_without_overfilling},
        {"implicit pure gas timestep", pure_gas_takes_an_implicit_step_above_acoustic_cfl},
        {"equilibrium contacts", equilibrium_contacts_remain_stationary_without_numerical_sorting},
        {"gas liquid hydrostatics", hydrostatic_liquid_and_gas_remain_balanced},
        {"10000 step multiphase conservation", long_closed_multiphase_run_preserves_each_trace_and_total_energy},
        {"viscous shear energy", tangential_viscosity_dissipates_into_heat_without_momentum_loss},
        {"adaptive smaller-step retry", smaller_step_retry_completes_without_losing_inventory},
        {"atomic failure and unsupported states", invalid_states_and_failed_step_leave_every_cell_unchanged}};
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
