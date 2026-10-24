#pragma once
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 4 free-surface closure: a gas-free liquid surface facing vacuum
// stays at rest instead of accumulating free-fall momentum on a face nothing
// can cross, and momentum stranded on such a face becomes local heat with
// closed U+K+G and impulse accounts. Profile 3 keeps its original behaviour.
namespace ma::testing::surface_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
// Face inertias from cell masses (the generic validator refuses the gas-free
// partial cells this control is about).
inline std::pair<std::vector<double>, std::vector<double>>
inertias(const MacTransportState &s) {
  const auto W = s.grid.width, H = s.grid.height;
  std::vector<double> x((W + 1) * H), y(W * (H + 1));
  const auto m = [&](std::size_t i) { return s.cells[i].mixture.mass(); };
  for (std::size_t r = 0; r < H; ++r)
    for (std::size_t c = 0; c <= W; ++c)
      x[c + r * (W + 1)] = .5 * ((c > 0 ? m(c - 1 + r * W) : 0) + (c < W ? m(c + r * W) : 0));
  for (std::size_t r = 0; r <= H; ++r)
    for (std::size_t c = 0; c < W; ++c)
      y[c + r * W] = .5 * ((r > 0 ? m(c + (r - 1) * W) : 0) + (r < H ? m(c + r * W) : 0));
  return {x, y};
}
inline long double total_energy(const MacClosedWorld &world) {
  const auto &s = world.state();
  long double total = 0;
  for (std::size_t i = 0; i < s.cells.size(); ++i)
    total += s.cells[i].mixture.energy +
             static_cast<long double>(s.cells[i].mixture.mass()) *
                 world.settings().gravity_m_per_s2 *
                 (static_cast<long double>(i / s.grid.width) + .5L) * s.grid.spacing_m;
  const auto [ix, iy] = inertias(s);
  for (std::size_t f = 0; f < s.momentum_x.size(); ++f)
    if (s.momentum_x[f] != 0) total += .5L * s.momentum_x[f] * s.momentum_x[f] / ix[f];
  for (std::size_t f = 0; f < s.momentum_y.size(); ++f)
    if (s.momentum_y[f] != 0) total += .5L * s.momentum_y[f] * s.momentum_y[f] / iy[f];
  return total;
}
inline double vertical_kinetic(const MacClosedWorld &world) {
  const auto &s = world.state();
  const auto iy = inertias(s).second;
  double total = 0;
  for (std::size_t f = 0; f < s.momentum_y.size(); ++f)
    if (s.momentum_y[f] != 0) total += .5 * s.momentum_y[f] * s.momentum_y[f] / iy[f];
  return total;
}
// Two water columns in a 4 x 6 box: two full cells, a gas-free half cell,
// vacuum above. The top face of the half cell is (column, y=3 | y=4).
inline MacClosedWorld basin(MacGameProfile profile, double top_momentum = 0) {
  const auto &f = setup();
  auto state = f.state(4, 6, false);
  const auto water = f.registry.index("water");
  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    const auto x = i % 4, y = i / 4;
    state.grid.walls[i] = x == 0 || x == 3 || y == 0 || y == 5;
    state.cells[i].mixture = MixtureInventory(f.registry.size());
    state.cells[i].passive_quantities.clear();
    if (state.grid.walls[i]) continue;
    if (y <= 2) state.cells[i].mixture.masses[water] = 1000.05;
    else if (y == 3) state.cells[i].mixture.masses[water] = 500;
  }
  for (const std::size_t x : {1U, 2U}) state.momentum_y.at(x + 4 * 4) = top_momentum;
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = profile;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.thermal_conduction = true;
  cfg.maximum_timestep_s = .2;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state),
                                     std::vector<double>(24, 300), cfg);
}
inline void run() {
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  auto legacy = basin(MacGameProfile::game_kernel_positive_liquid);
  auto surface = basin(MacGameProfile::game_kernel_free_surface);
  for (unsigned tick = 0; tick < 10; ++tick) {
    legacy = legacy.advanced(.2).world;
    surface = surface.advanced(.2).world;
  }
  // The half cell's top face (y=4) is the one nothing can cross.
  const auto &legacy_j = legacy.state().momentum_y, &surface_j = surface.state().momentum_y;
  check(legacy_j.at(1 + 4 * 4) < -1e3 && legacy_j.at(2 + 4 * 4) < -1e3,
        "Profile 3 control no longer shows free-surface momentum");
  check(surface_j.at(1 + 4 * 4) == 0 && surface_j.at(2 + 4 * 4) == 0 &&
            vertical_kinetic(surface) < .01 * vertical_kinetic(legacy),
        "Profile 4 resting surface gained free-fall momentum");
  const auto image = surface.checkpoint();
  check(image.at(8) == 22, "Profile 4 did not write checkpoint 22");
  check(legacy.checkpoint().at(8) == 21, "Profile 3 changed its checkpoint version");
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image &&
            cold.settings().game_profile == MacGameProfile::game_kernel_free_surface,
        "Checkpoint 22 cold round trip differs");
  auto many = saved;
  many.threads = 12;
  const auto next_one = surface.advanced(.2).world;
  set_mac_kernel_default_options(many);
  const auto next_many = cold.advanced(.2).world;
  check(next_one.checkpoint() == next_many.checkpoint(),
        "Profile 4 cold/worker continuation differs");
  // Downward momentum stranded on the half cell's top face (it points out of
  // the empty cell): the boundary takes it and its kinetic energy is heat.
  const auto stranded = basin(MacGameProfile::game_kernel_free_surface, -50);
  const auto before = total_energy(stranded);
  const auto after = stranded.advanced(.2);
  const auto now = total_energy(after.world);
  check(std::abs(now - before) <= 1e-12L * (std::abs(before) + std::abs(now)),
        "Stranded surface momentum did not close U+K+G");
  check(after.world.state().momentum_y.at(1 + 4 * 4) == 0 &&
            after.world.state().momentum_y.at(2 + 4 * 4) == 0,
        "Stranded surface momentum remained on its face");
  set_mac_kernel_default_options(saved);
  std::cout << "PASS profile4 free-surface closure: resting gas-free surface "
               "stays at rest (profile3 control unchanged), stranded momentum "
               "becomes heat with closed U+K+G, checkpoint22 and workers1/12 "
               "cold continuation\n";
}
} // namespace ma::testing::surface_game
