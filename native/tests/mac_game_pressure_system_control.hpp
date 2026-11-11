#pragma once
#include "mac_fixture.hpp"
#include "mac_game_convective_control.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 12 pressure systems (native/MAC_GAME_KERNEL.md, "Room in liquid
// cells" and "Large connected regions"; 2026-11-11).
// - Mist at a vent front. The room bound enters a liquid cell whose solve
//   overfills it on its liquid's compliance alone. Only liquid uses up room:
//   a thin-gas cell holding a mist droplet was entered the same way, with
//   the droplet's compliance (almost none), which held the gas front back
//   and made the region's pressure system nearly incompressible. Air
//   expanding into thin oxygen costs the same pressure work with and
//   without ten micrograms of water in every thin cell, and its front
//   stays within 3 %.
// - A large connected room. One connected pressure system of at least 2,000
//   cells is solved by the geometric multigrid PCG, checked against the
//   component solve's residual bound. A disturbed 66 x 34 room uses it,
//   closes mass and U+K+G, and continues exactly cold and on 12 workers.
namespace ma::testing::pressure_system_game {
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
  cfg.maximum_timestep_s = static_cast<double>(.2f);
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  cfg.game.gas_heat_mixing = 8;
  cfg.game.phase_rule = MacPhaseRule::boil_only;
  return cfg;
}
// Air at about 1 atm in columns 1-10 of a 40 x 3 channel, thin oxygen
// (0.01 g a cell) beyond, with or without 10 ug of water in each thin cell.
inline MacClosedWorld front(bool mist) {
  const auto &f = setup();
  constexpr std::size_t W = 42, H = 5;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto water = f.registry.index("water"), nitrogen = f.registry.index("nitrogen"),
             oxygen = f.registry.index("oxygen");
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i]) {
      if (x <= 10) {
        cell.mixture.masses[nitrogen] = .9;
        cell.mixture.masses[oxygen] = .28;
      } else {
        cell.mixture.masses[oxygen] = 1e-5;
        if (mist) cell.mixture.masses[water] = 1e-8;
      }
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state),
                                     std::vector<double>(W * H, 300), settings());
}
inline long double gas_beyond(const MacClosedWorld &w, std::size_t x0) {
  const auto &f = setup();
  const auto water = f.registry.index("water");
  long double t = 0;
  const auto &s = w.state();
  for (std::size_t i = 0; i < s.cells.size(); ++i)
    if (i % s.grid.width > x0) t += s.cells[i].mixture.mass() - s.cells[i].mixture.masses[water];
  return t;
}
// A 64 x 32 room of air with a hot, dense patch in one corner.
inline MacClosedWorld room() {
  const auto &f = setup();
  constexpr std::size_t W = 66, H = 34;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto nitrogen = f.registry.index("nitrogen"), oxygen = f.registry.index("oxygen");
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i]) {
      const bool patch = x <= 6 && y <= 6;
      cell.mixture.masses[nitrogen] = patch ? 1.4 : .9;
      cell.mixture.masses[oxygen] = patch ? .5 : .28;
      if (patch) temperatures[i] = 420;
      cell.passive_quantities = {{17, 1 + static_cast<double>(x % 5)}};
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), temperatures, settings());
}
inline void run() {
  using convective_game::total_mass;
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  // Mist at a vent front.
  auto dry = front(false), wet = front(true);
  const auto wet_mass0 = total_mass(wet), wet_energy0 = surface_game::total_energy(wet);
  std::size_t dry_iterations = 0, wet_iterations = 0;
  long double sunk_mass = 0, sunk_energy = 0;
  for (unsigned tick = 0; tick < 5; ++tick) {
    auto a = dry.advanced(.2);
    auto b = wet.advanced(.2);
    dry_iterations += a.report.game_stages.pressure_iterations;
    wet_iterations += b.report.game_stages.pressure_iterations;
    const auto &t = b.report.trace_sink;
    for (const auto &m : t.mass_kg) sunk_mass += mac_energy_long_double(m);
    sunk_energy += mac_energy_long_double(t.internal_energy_j) + mac_energy_long_double(t.kinetic_energy_j) +
                   mac_energy_long_double(t.potential_energy_j);
    dry = std::move(a.world);
    wet = std::move(b.world);
  }
  const auto far_dry = gas_beyond(dry, 20), far_wet = gas_beyond(wet, 20);
  check(far_dry > 1e-3L, "The front control's air did not reach the thin gas");
  // The pressure cost separates the two rules: 1,657 iterations with the
  // droplet cells entered on their droplets' compliance, 1,191 without
  // (1,194 dry). The front itself moves little either way (1.5 % here: a
  // droplet still changes the intervals the transport takes).
  check(std::abs(far_wet - far_dry) <= .03L * far_dry, "A mist droplet in thin gas held the gas front back");
  check(wet_iterations <= dry_iterations + dry_iterations / 10, "A mist droplet in thin gas made the pressure solves expensive");
  check(std::abs(total_mass(wet) + sunk_mass - wet_mass0) <= 1e-12L * wet_mass0, "The mist front changed mass");
  check(std::abs(surface_game::total_energy(wet) + sunk_energy - wet_energy0) <= 1e-12L * std::abs(wet_energy0),
        "The mist front lost U+K+G");
  // A large connected room.
  auto large = room();
  const auto mass0 = total_mass(large), energy0 = surface_game::total_energy(large);
  std::size_t geometric = 0, iterations = 0, attempts = 0;
  for (unsigned tick = 0; tick < 4; ++tick) {
    auto next = large.advanced(.2);
    geometric += next.report.game_stages.pressure_geometric;
    iterations += next.report.game_stages.pressure_iterations;
    attempts += next.report.game_stages.pressure_attempts;
    large = std::move(next.world);
  }
  check(geometric > 0, "A 2,048-cell connected room did not use the geometric pressure solve");
  check(std::abs(total_mass(large) - mass0) <= 1e-12L * mass0, "The large room changed mass");
  check(std::abs(surface_game::total_energy(large) - energy0) <= 1e-12L * std::abs(energy0), "The large room lost U+K+G");
  const auto image = large.checkpoint();
  check(image.at(8) == 32, "The large room did not write checkpoint 32");
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image, "The large room's checkpoint round trip differs");
  const auto live = large.advanced(.2).world.checkpoint();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  auto restored = cold.advanced(.2);
  set_mac_kernel_default_options(saved);
  check(restored.report.game_stages.pressure_geometric > 0, "The 12-worker large room did not use the geometric pressure solve");
  check(live == restored.world.checkpoint(), "The large room's cold/12-worker continuation differs");
  // A small room keeps the component solve.
  auto small = convective_game::box(MacGameProfile::game_kernel_thin_drag, true);
  check(small.advanced(.2).report.game_stages.pressure_geometric == 0, "A small room used the geometric pressure solve");
  std::cout << "PASS profile12 pressure systems: gas beyond column 20 after 1 s dry/mist "
            << static_cast<double>(far_dry) << "/" << static_cast<double>(far_wet) << " kg, pressure iterations "
            << dry_iterations << "/" << wet_iterations << "; 2,048-cell room " << geometric << " geometric solves of "
            << attempts << " attempts, " << iterations
            << " iterations; mass and U+K+G closed; exact cold and on 12 workers\n";
}
} // namespace ma::testing::pressure_system_game
