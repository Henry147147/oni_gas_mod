#pragma once
#include "mac_fixture.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 11 sub-grid convective adjustment (native/MAC_GAME_KERNEL.md). A
// hot oxygen row under a cold one in a box of identical columns is buoyantly
// unstable but cannot overturn on the discrete grid: profile 10 keeps it.
// Profile 11 exchanges gas across the unstable face and mixes the rows, with
// closed mass, U+K+G and passives; a stably stratified box (cold below hot)
// advances exactly as on profile 10. Checkpoint 31 continues exactly cold and
// on 12 workers.
namespace ma::testing::convective_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
inline MacClosedWorld box(MacGameProfile profile, bool unstable) {
  const auto &f = setup();
  constexpr std::size_t W = 6, H = 5;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto oxygen = f.registry.index("oxygen");
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i]) {
      const bool lower = y == 1;
      temperatures[i] = (lower == unstable) ? 360 : 300;
      // 1 atm at each row's temperature.
      cell.mixture.masses[oxygen] = 101325 * .032 / (8.31446261815324 * temperatures[i]);
      cell.passive_quantities = {{17, 1 + static_cast<double>(y)}};
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = profile;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.thermal_conduction = true;
  cfg.maximum_timestep_s = static_cast<double>(.2f);
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  cfg.game.gas_heat_mixing = 8;
  cfg.game.phase_rule = MacPhaseRule::boil_only;
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), temperatures, cfg);
}
// Temperature difference between the two interior rows (lower minus upper).
inline double row_difference(const MacClosedWorld &world) {
  const auto &s = world.state();
  double low = 0, high = 0;
  for (std::size_t x = 1; x + 1 < s.grid.width; ++x) {
    low += world.cell_reading(x + 1 * s.grid.width).temperature_k;
    high += world.cell_reading(x + 3 * s.grid.width).temperature_k;
  }
  return (low - high) / static_cast<double>(s.grid.width - 2);
}
inline long double total_mass(const MacClosedWorld &w) {
  long double t = 0;
  for (const auto &c : w.state().cells) t += c.mixture.mass();
  return t;
}
inline long double total_passive(const MacClosedWorld &w) {
  long double t = 0;
  for (const auto &c : w.state().cells)
    for (const auto &q : c.passive_quantities) t += q.quantity;
  return t;
}
inline void run() {
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  auto layered = box(MacGameProfile::game_kernel_layered_remap, true);
  auto convective = box(MacGameProfile::game_kernel_convective, true);
  const double start = row_difference(convective);
  const auto mass0 = total_mass(convective), energy0 = surface_game::total_energy(convective),
             passive0 = total_passive(convective);
  for (unsigned tick = 0; tick < 25; ++tick) {
    layered = layered.advanced(.2).world;
    convective = convective.advanced(.2).world;
  }
  check(row_difference(layered) > .5 * start, "Profile 10 control no longer holds the hot layer");
  check(row_difference(convective) < .25 * row_difference(layered),
        "Profile 11 did not overturn the unstable layer");
  check(std::abs(total_mass(convective) - mass0) <= 1e-12L * mass0, "Profile 11 changed mass");
  check(std::abs(surface_game::total_energy(convective) - energy0) <= 1e-12L * std::abs(energy0),
        "Profile 11 lost U+K+G");
  check(std::abs(total_passive(convective) - passive0) <= 1e-12L * passive0,
        "Profile 11 changed a passive total");
  // A stable box (cold below hot) never exchanges: identical to profile 10.
  auto stable10 = box(MacGameProfile::game_kernel_layered_remap, false);
  auto stable11 = box(MacGameProfile::game_kernel_convective, false);
  for (unsigned tick = 0; tick < 10; ++tick) {
    stable10 = stable10.advanced(.2).world;
    stable11 = stable11.advanced(.2).world;
  }
  for (std::size_t i = 0; i < stable10.state().cells.size(); ++i)
    check(stable10.state().cells[i].mixture.masses == stable11.state().cells[i].mixture.masses &&
              stable10.state().cells[i].mixture.energy == stable11.state().cells[i].mixture.energy,
          "Profile 11 exchanged gas in a stable layer");
  const auto image = convective.checkpoint();
  check(image.at(8) == 31, "Profile 11 did not write checkpoint 31");
  check(layered.checkpoint().at(8) == 30, "Profile 10 changed its checkpoint version");
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image &&
            cold.settings().game_profile == MacGameProfile::game_kernel_convective,
        "Checkpoint 31 round trip differs");
  const auto live = convective.advanced(.2).world.checkpoint();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  const auto restored = cold.advanced(.2).world.checkpoint();
  set_mac_kernel_default_options(saved);
  check(live == restored, "Profile 11 cold/12-worker continuation differs");
  std::cout << "PASS profile11 convective adjustment overturns an unstable hot gas layer (row difference "
            << start << " -> " << row_difference(convective) << " K, profile10 keeps "
            << row_difference(layered) << " K), leaves a stable layer exactly as profile10, closes mass, "
               "U+K+G and passives, and checkpoint 31 continues exactly cold and on 12 workers\n";
}
} // namespace ma::testing::convective_game
