#pragma once
#include "mac_fixture.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 10 layered vertical remap (native/MAC_GAME_KERNEL.md). Freshly
// placed water cells hold 1% vacuum under air, as ONI's element replacement
// leaves them. Their collapse moves the column. Profile 9 sweeps each gas/
// liquid donor's phases in proportion, so the air rising out of the surface
// cell carries water into the dry rows above and it stays there; profile 10
// draws gas first through a top face and liquid first through a bottom face,
// so no water reaches the air. Both close mass and U+K+G.
namespace ma::testing::layered_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
// 4 x 6 interior inside a wall ring: rows 0-2 water (990 kg, no gas), rows
// 3-5 oxygen at 1.2 kg.
inline MacClosedWorld basin(MacGameProfile profile) {
  const auto &f = setup();
  constexpr std::size_t W = 6, H = 8;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto water = f.registry.index("water"), oxygen = f.registry.index("oxygen");
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i]) {
      if (y <= 3) cell.mixture.masses[water] = 990;
      else cell.mixture.masses[oxygen] = 1.2;
      cell.passive_quantities = {{17, 1 + static_cast<double>(i)}};
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
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state),
                                     std::vector<double>(W * H, 300), cfg);
}
// Liquid in the rows above the initial surface cell (interior rows 4-5,
// grid rows 5-6).
inline double lifted(const MacClosedWorld &world) {
  const auto &f = setup();
  const auto water = f.registry.index("water");
  const auto &s = world.state();
  double total = 0;
  for (std::size_t i = 0; i < s.cells.size(); ++i)
    if (!s.grid.walls[i] && i / s.grid.width >= 5)
      total += s.cells[i].mixture.masses[water];
  return total;
}
inline long double total_mass(const MacClosedWorld &world) {
  long double total = 0;
  for (const auto &cell : world.state().cells) total += cell.mixture.mass();
  return total;
}
inline long double total_passive(const MacClosedWorld &world) {
  long double total = 0;
  for (const auto &cell : world.state().cells)
    for (const auto &q : cell.passive_quantities) total += q.quantity;
  return total;
}
inline void run() {
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  auto legacy = basin(MacGameProfile::game_kernel_activity_tiers);
  auto layered = basin(MacGameProfile::game_kernel_layered_remap);
  const auto mass0 = total_mass(layered), energy0 = surface_game::total_energy(layered),
             passive0 = total_passive(layered);
  for (unsigned tick = 0; tick < 10; ++tick) {
    legacy = legacy.advanced(.2).world;
    layered = layered.advanced(.2).world;
    check(lifted(layered) == 0, "Profile 10 carried water into the air above its surface");
  }
  check(lifted(legacy) > 1, "Profile 9 control no longer lifts water into the air");
  const auto mass1 = total_mass(layered), energy1 = surface_game::total_energy(layered),
             passive1 = total_passive(layered);
  // Bounds at the region audits' relative 1e-12 (trace sinks are far smaller).
  check(std::abs(mass1 - mass0) <= 1e-12L * mass0, "Profile 10 changed the basin's mass");
  check(std::abs(energy1 - energy0) <= 1e-12L * std::abs(energy0), "Profile 10 lost U+K+G");
  check(std::abs(passive1 - passive0) <= 1e-12L * passive0, "Profile 10 changed a passive total");
  // Checkpoint 30: exact round trip, cold continuation and worker count.
  const auto image = layered.checkpoint();
  check(image.at(8) == 30, "Profile 10 did not write checkpoint 30");
  check(legacy.checkpoint().at(8) == 28, "Profile 9 changed its checkpoint version");
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image &&
            cold.settings().game_profile == MacGameProfile::game_kernel_layered_remap,
        "Checkpoint 30 round trip differs");
  const auto live = layered.advanced(.2).world.checkpoint();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  const auto restored = cold.advanced(.2).world.checkpoint();
  set_mac_kernel_default_options(saved);
  check(live == restored, "Profile 10 cold/12-worker continuation differs");
  std::cout << "PASS profile10 layered vertical remap keeps freshly placed water out of the air "
               "(profile9 lifts " << lifted(legacy) << " kg), closes mass, U+K+G and passives, "
               "and checkpoint 30 continues exactly cold and on 12 workers\n";
}
} // namespace ma::testing::layered_game
