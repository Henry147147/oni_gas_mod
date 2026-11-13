#pragma once
#include "mac_fixture.hpp"
#include "mac_game_convective_control.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 12 thin-gas drag (native/MAC_GAME_KERNEL.md). Faces lighter than
// 1 g feel an implicit drag, so thin gas seeps into vacuum instead of
// jetting. A row of thin oxygen beside vacuum spreads slower and needs fewer
// flow intervals than on profile 11, with mass and U+K+G closed; air of
// ordinary density has no face below 1 g, so there profile 12 differs from
// profile 11 only by its gas drag (20 s; exactly equal until 2026-11-13).
// Checkpoint 32 continues exactly cold and on 12 workers.
namespace ma::testing::thin_drag_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
// Thin oxygen (0.2 g a cell) in the left quarter of a 24x3 channel, vacuum beyond.
inline MacClosedWorld channel(MacGameProfile profile) {
  const auto &f = setup();
  constexpr std::size_t W = 26, H = 3;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto oxygen = f.registry.index("oxygen");
  std::vector<double> temperatures(W * H, 300);
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i] && x <= 6) cell.mixture.masses[oxygen] = 2e-4;
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
// Gas mass beyond column x.
inline long double beyond(const MacClosedWorld &w, std::size_t x0) {
  long double t = 0;
  const auto &s = w.state();
  for (std::size_t i = 0; i < s.cells.size(); ++i)
    if (i % s.grid.width > x0) t += s.cells[i].mixture.mass();
  return t;
}
inline void run() {
  using convective_game::total_mass;
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  auto fast = channel(MacGameProfile::game_kernel_convective);
  auto thin = channel(MacGameProfile::game_kernel_thin_drag);
  const auto mass0 = total_mass(thin), energy0 = surface_game::total_energy(thin);
  std::size_t fast_substeps = 0, thin_substeps = 0;
  long double sunk_mass = 0, sunk_energy = 0; // the trace sink's share
  for (unsigned tick = 0; tick < 5; ++tick) {
    auto a = fast.advanced(.2);
    auto b = thin.advanced(.2);
    fast_substeps += a.report.substeps;
    thin_substeps += b.report.substeps;
    const auto &t = b.report.trace_sink;
    for (const auto &m : t.mass_kg) sunk_mass += mac_energy_long_double(m);
    sunk_energy += mac_energy_long_double(t.internal_energy_j) + mac_energy_long_double(t.kinetic_energy_j) +
                   mac_energy_long_double(t.potential_energy_j);
    fast = std::move(a.world);
    thin = std::move(b.world);
  }
  const auto far_fast = beyond(fast, 12), far_thin = beyond(thin, 12);
  // 0.45 of profile 11's when adopted; 0.56 since the thin-gas heating
  // (2026-11-07), which keeps the front near 300 K instead of letting it heat
  // itself to 500 K and hold itself back.
  check(far_fast > 0 && far_thin < .6L * far_fast, "Profile 12 did not slow the thin front");
  check(thin_substeps < fast_substeps, "Profile 12 did not lengthen the flow intervals");
  check(std::abs(total_mass(thin) + sunk_mass - mass0) <= 1e-12L * mass0, "Profile 12 changed mass");
  check(std::abs(surface_game::total_energy(thin) + sunk_energy - energy0) <= 1e-12L * std::abs(energy0),
        "Profile 12 lost U+K+G");
  // Ordinary air: no face is below 1 g, so the thin-gas drag never acts,
  // and the gas drag (20 s) takes a hundredth of a face's momentum a frame.
  // This box overturns by the sub-grid adjustment, with little resolved
  // motion: its cell masses are within 1.3e-10 of profile 11's after 2 s
  // (equal to the bit before the gas drag).
  auto dense11 = convective_game::box(MacGameProfile::game_kernel_convective, true);
  auto dense12 = convective_game::box(MacGameProfile::game_kernel_thin_drag, true);
  for (unsigned tick = 0; tick < 10; ++tick) {
    dense11 = dense11.advanced(.2).world;
    dense12 = dense12.advanced(.2).world;
  }
  double dense_apart = 0;
  for (std::size_t i = 0; i < dense11.state().cells.size(); ++i) {
    const double a = dense11.state().cells[i].mixture.mass(), b = dense12.state().cells[i].mixture.mass();
    if (a > 0) dense_apart = std::max(dense_apart, std::abs(a - b) / a);
  }
  check(dense_apart < 1e-6, "Profile 12 moved air of ordinary density further from profile 11 than its gas drag does");
  const auto image = thin.checkpoint();
  check(image.at(8) == 32, "Profile 12 did not write checkpoint 32");
  check(fast.checkpoint().at(8) == 31, "Profile 11 changed its checkpoint version");
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image &&
            cold.settings().game_profile == MacGameProfile::game_kernel_thin_drag,
        "Checkpoint 32 round trip differs");
  const auto live = thin.advanced(.2).world.checkpoint();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  const auto restored = cold.advanced(.2).world.checkpoint();
  set_mac_kernel_default_options(saved);
  check(live == restored, "Profile 12 cold/12-worker continuation differs");
  std::cout << "PASS profile12 thin-gas drag: thin oxygen beyond column 12 after 1 s "
            << static_cast<double>(far_fast) << " -> " << static_cast<double>(far_thin) << " kg, flow intervals "
            << fast_substeps << " -> " << thin_substeps
            << "; ordinary air within " << dense_apart
            << " of profile11's cell masses after 2 s (the gas drag); mass and U+K+G closed; checkpoint 32 continues exactly cold and on 12 workers\n";
}
} // namespace ma::testing::thin_drag_game
