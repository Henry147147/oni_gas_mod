#pragma once
#include "../src/mac_game/patch.hpp"
#include "mac_fixture.hpp"
#include "mac_game_convective_control.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 12 liquid volume consistency (native/MAC_GAME_KERNEL.md, "Flooded
// cells"). Two mechanisms overfilled liquid cells after a flood:
// - the room bound: the pressure solve sees a partly filled gas-free cell as a
//   zero-pressure boundary and a gas bubble by its tangent compliance, so one
//   interval could send a cell more liquid than its void or gas could take;
// - the volume-consistent remap: on local dyadic clocks a cell can receive a
//   coarse face's whole sweep before its finer faces send theirs, and a donor
//   swept as if its material filled the cell then ships the excess density on.
// A water column over a cavitated (95% full) cell and a thin-gas bubble falls
// into it: profile 11 leaves megapascals in the column, profile 12 stays near
// hydrostatic, with closed mass and U+K+G and exact checkpoint 32 continuation.
// The remap alone moves a uniform liquid round two loops at clock levels 0 and
// 2: cells sharing both keep their density on profile 12.
namespace ma::testing::flood_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
// 3 x 8 interior inside a wall ring: row 0 water with 5% void (column 0),
// a thin nitrogen bubble (column 1) or full (column 2); rows 1-5 full water;
// rows 6-7 air.
inline MacClosedWorld column(MacGameProfile profile) {
  const auto &f = setup();
  constexpr std::size_t W = 5, H = 10;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto water = f.registry.index("water"), nitrogen = f.registry.index("nitrogen"),
             oxygen = f.registry.index("oxygen");
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i]) {
      if (y == 1) {
        cell.mixture.masses[water] = x == 3 ? 1000 : 950;
        if (x == 2) cell.mixture.masses[nitrogen] = 1e-4;
      } else if (y <= 6)
        cell.mixture.masses[water] = 1000;
      else {
        cell.mixture.masses[nitrogen] = .9;
        cell.mixture.masses[oxygen] = .28;
      }
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
inline double peak(const MacClosedWorld &world) {
  double highest = 0;
  const auto &s = world.state();
  for (std::size_t i = 0; i < s.cells.size(); ++i)
    if (!s.grid.walls[i] && s.cells[i].mixture.mass() > 0)
      highest = std::max(highest, world.cell_reading(i).mechanical_pressure_pa);
  return highest;
}
// Two loops of uniform water on a 3 x 2 grid: the left loop (columns 0-1)
// sweeps 0.25 cell a face, the right loop (columns 1-2) 1.2 cells (clock
// level 2). Every cell's net volume is zero, so every cell must keep 1000 kg.
inline double loop_drift(bool swept_volume) {
  namespace gk = ma::experimental::game_kernel;
  namespace k = ma::experimental::mac_kernel;
  const MacGrid grid{3, 2, 1, 1, false, false, std::vector<std::uint8_t>(6)};
  const k::Grid g(grid);
  gk::SparseMass masses;
  std::vector<double> energy(g.N, 1000 * 4.18e6 * .3);
  for (std::size_t i = 0; i < g.N; ++i) {
    masses.offsets.push_back(masses.mass.size());
    masses.species.push_back(0);
    masses.mass.push_back(1000);
  }
  masses.offsets.push_back(masses.mass.size());
  std::vector<double> jx(g.NX), jy(g.NY), qx(g.NX), qy(g.NY);
  // Left loop 0 -> 1 -> 4 -> 3 -> 0, right loop 2 -> 1 -> 4 -> 5 -> 2; the
  // shared face 1 -> 4 carries both.
  const double a = .25, b = 1.2;
  qx[g.xface(1, 0)] = a;     // 0 -> 1
  qy[g.yface(1, 1)] = a + b; // 1 -> 4
  qx[g.xface(1, 1)] = -a;    // 4 -> 3
  qy[g.yface(0, 1)] = -a;    // 3 -> 0
  qx[g.xface(2, 0)] = -b;    // 2 -> 1
  qx[g.xface(2, 1)] = b;     // 4 -> 5
  qy[g.yface(2, 1)] = -b;    // 5 -> 2
  gk::PatchTransport patch(grid);
  const auto out = patch.advance(masses, energy, jx, jy, qx, qy, 0, true, nullptr, nullptr, nullptr, nullptr,
                                 swept_volume);
  check(out.microticks > 1, "The loop control no longer runs local clocks");
  double drift = 0, total = 0;
  for (std::size_t i = 0; i < g.N; ++i) {
    double m = 0;
    for (auto e = out.masses.offsets[i]; e < out.masses.offsets[i + 1]; ++e) m += out.masses.mass[e];
    drift = std::max(drift, std::abs(m - 1000));
    total += m;
  }
  check(std::abs(total - 6000) <= 1e-9, "The loop control changed the total mass");
  return drift;
}
inline void run() {
  using convective_game::total_mass;
  const auto old_drift = loop_drift(false), new_drift = loop_drift(true);
  check(old_drift > 10, "Profile 11 sweeps no longer drift on mixed clock levels");
  check(new_drift < 1e-9, "The volume-consistent remap changed a uniform liquid's density");
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  auto before = column(MacGameProfile::game_kernel_convective);
  auto after = column(MacGameProfile::game_kernel_thin_drag);
  const auto mass0 = total_mass(after), energy0 = surface_game::total_energy(after);
  double old_peak = 0, new_peak = 0;
  long double sunk_mass = 0, sunk_energy = 0;
  for (unsigned tick = 0; tick < 15; ++tick) {
    auto a = before.advanced(.2);
    auto b = after.advanced(.2);
    const auto &t = b.report.trace_sink;
    for (const auto &m : t.mass_kg) sunk_mass += mac_energy_long_double(m);
    sunk_energy += mac_energy_long_double(t.internal_energy_j) + mac_energy_long_double(t.kinetic_energy_j) +
                   mac_energy_long_double(t.potential_energy_j);
    before = std::move(a.world);
    after = std::move(b.world);
    old_peak = std::max(old_peak, peak(before));
    new_peak = std::max(new_peak, peak(after));
  }
  // Seven metres of water under one atmosphere: about 170 kPa at the floor.
  check(old_peak > 1e6, "Profile 11 no longer overfills the cavitated column");
  check(new_peak < 3e5, "Profile 12 left a flooded cell far above its column's pressure");
  check(std::abs(total_mass(after) + sunk_mass - mass0) <= 1e-12L * mass0, "Profile 12 changed the column's mass");
  check(std::abs(surface_game::total_energy(after) + sunk_energy - energy0) <= 1e-12L * std::abs(energy0),
        "Profile 12 lost U+K+G in the column");
  const auto image = after.checkpoint();
  check(image.at(8) == 32, "Profile 12 did not write checkpoint 32");
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  check(cold.checkpoint() == image, "Checkpoint 32 round trip differs");
  const auto live = after.advanced(.2).world.checkpoint();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  const auto restored = cold.advanced(.2).world.checkpoint();
  set_mac_kernel_default_options(saved);
  check(live == restored, "Profile 12 flooded-column cold/12-worker continuation differs");
  std::cout << "PASS profile12 flooded cells: a uniform liquid round level-0/2 clock loops drifts " << old_drift << " -> "
            << new_drift << " kg; a water column falling into a cavitated cell and a thin-gas bubble peaks at "
            << old_peak / 1e3 << " kPa on profile 11 and " << new_peak / 1e3
            << " kPa on profile 12; mass and U+K+G closed; checkpoint 32 continues exactly cold and on 12 workers\n";
}
} // namespace ma::testing::flood_game
