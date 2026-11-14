#pragma once
#include "mac_fixture.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <array>
#include <cmath>
#include <iostream>

// Profile 12 heat across layers (native/MAC_GAME_KERNEL.md, "Gas heat mixing
// across a stable layer" and "Liquid convection").
//
// Gas: the eddy term of the heat solve mixed heat across any face in
// proportion to the speed through it. Across a stable layer that mixing is
// not free, and the term now falls with the bulk Richardson number. A column
// of oxygen at rest, 350 K over 290 K, grew its own motion (4 cm/s after two
// minutes) and was 302 K under 334 K after ten; it now stays as it is. The
// same column the other way up still overturns.
//
// Liquid: liquid density does not depend on temperature here, so no flow
// carried heat up from a warm floor. The heat solve now exchanges heat
// between liquid cells as convection would: a cold layer of water over a hot
// one mixes within a minute or two (it kept 60 K of difference for good),
// and a hot layer over a cold one stays.
namespace ma::testing::heat_layer_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
inline MacClosedSettings settings() {
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_thin_drag;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.thermal_conduction = true;
  cfg.maximum_timestep_s = .2;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  cfg.game.gas_heat_mixing = 8;
  return cfg;
}
struct Halves {
  double lower = 0, upper = 0, speed = 0;
};
// Advances `world` and returns the mass-mean temperatures of rows
// [1, split) and [split, top] of its fluid of `species` and the fastest
// vertical face of the last 20 s, with mass and U+K+G closed.
inline Halves run(MacClosedWorld world, std::size_t species, std::size_t split, std::size_t top, unsigned ticks) {
  const auto energy0 = surface_game::total_energy(world);
  long double mass0 = 0, sunk_mass = 0, sunk_energy = 0;
  for (const auto &cell : world.state().cells) mass0 += cell.mixture.mass();
  Halves out;
  for (unsigned tick = 1; tick <= ticks; ++tick) {
    auto next = std::move(world).advanced(.2);
    for (const auto &m : next.report.trace_sink.mass_kg) sunk_mass += mac_energy_long_double(m);
    const auto &sink = next.report.trace_sink;
    sunk_energy += mac_energy_long_double(sink.internal_energy_j) + mac_energy_long_double(sink.kinetic_energy_j) +
                   mac_energy_long_double(sink.potential_energy_j);
    world = std::move(next.world);
    const auto &s = world.state();
    for (std::size_t f = 0; tick + 100 > ticks && f < s.momentum_y.size(); ++f) {
      const auto x = f % s.grid.width, y = f / s.grid.width;
      if (y == 0 || y >= s.grid.height) continue;
      const double inertia = .5 * (s.cells[x + (y - 1) * s.grid.width].mixture.mass() + s.cells[x + y * s.grid.width].mixture.mass());
      if (inertia > 1e-3) out.speed = std::max(out.speed, std::abs(s.momentum_y[f] / inertia));
    }
  }
  long double mass1 = 0;
  for (const auto &cell : world.state().cells) mass1 += cell.mixture.mass();
  check(std::abs(mass1 + sunk_mass - mass0) < 1e-12L * mass0, "The layered fluid changed mass");
  check(std::abs(surface_game::total_energy(world) + sunk_energy - energy0) < 1e-12L * std::abs(energy0),
        "The layered fluid lost U+K+G");
  std::array<double, 2> heat{}, weight{};
  const auto W = world.state().grid.width;
  for (std::size_t i = 0; i < world.state().cells.size(); ++i) {
    const auto y = i / W;
    const double m = world.state().cells[i].mixture.masses[species];
    if (world.state().grid.walls[i] || y > top || !(m > 0)) continue;
    heat[y >= split] += m * world.cell_reading(i).temperature_k;
    weight[y >= split] += m;
  }
  out.lower = heat[0] / weight[0];
  out.upper = heat[1] / weight[1];
  return out;
}
// A column of oxygen, 6 by 30 cells at 100 kPa, its halves at `low` and `high`.
inline MacClosedWorld column(double low, double high) {
  const auto &f = setup();
  constexpr std::size_t W = 8, H = 32;
  const auto oxygen = f.registry.index("oxygen");
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    state.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!state.grid.walls[i]) {
      temperatures[i] = y <= 15 ? low : high;
      cell.mixture.masses[oxygen] = 100000 * .032 / (gas_constant * temperatures[i]);
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), temperatures, settings());
}
// A pool of water, 6 by 4 cells under two rows of oxygen at 100 kPa, its
// lower two rows at `low` and its upper two at `high`.
inline MacClosedWorld pool(double low, double high) {
  const auto &f = setup();
  constexpr std::size_t W = 8, H = 8;
  const auto water = f.registry.index("water"), oxygen = f.registry.index("oxygen");
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    state.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!state.grid.walls[i]) {
      if (y <= 4) {
        temperatures[i] = y <= 2 ? low : high;
        cell.mixture.masses[water] = 1000 + 1000 * (100000 + 9810 * (4.5 - static_cast<double>(y))) / (2.2e9 * .04);
      } else cell.mixture.masses[oxygen] = 100000 * .032 / (gas_constant * 300);
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), temperatures, settings());
}
inline void run() {
  const auto saved = mac_kernel_default_options();
  auto options = saved;
  options.threads = 1;
  set_mac_kernel_default_options(options);
  const auto &f = setup();
  const auto oxygen = f.registry.index("oxygen"), water = f.registry.index("water");
  const auto stable = run(column(290, 350), oxygen, 16, 30, 3000);
  const auto unstable = run(column(350, 290), oxygen, 16, 30, 1500);
  const auto warm_top = run(pool(290, 350), water, 3, 4, 750);
  const auto cold_top = run(pool(350, 290), water, 3, 4, 750);
  set_mac_kernel_default_options(saved);
  check(stable.lower < 291 && stable.upper > 349 && stable.speed < 1e-3,
        "Hot gas over cold gas did not stay at rest");
  check(std::abs(unstable.upper - unstable.lower) < 5, "Hot gas under cold gas did not overturn");
  check(warm_top.lower < 291 && warm_top.upper > 349, "Hot water over cold water did not stay");
  check(std::abs(cold_top.upper - cold_top.lower) < 1, "Cold water over hot water did not mix");
  std::cout << "PASS profile12 heat across layers: oxygen at 350 K over 290 K reads " << stable.upper << " K over "
            << stable.lower << " K after 600 s (fastest vertical gas " << stable.speed << " m/s) and the other way up "
            << unstable.upper << " K over " << unstable.lower << " K after 300 s; water at 350 K over 290 K reads "
            << warm_top.upper << " K over " << warm_top.lower << " K after 150 s and the other way up " << cold_top.upper
            << " K over " << cold_top.lower << " K; mass and U+K+G closed\n";
}
} // namespace ma::testing::heat_layer_game
