#pragma once
#include "mac_fixture.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 12 thin-gas heat exchange (native/MAC_GAME_KERNEL.md). A light gas
// with the game's hydrogen data (1.008 g/mol, cv 2400 J/(kg K), gamma 4.44)
// leaks from a pressurized pocket into an empty cave. The drag holds the thin
// front back, so pressure work and drag heat build on traces; without the
// exchange each front cell filled from a hotter one (the Spaced Out!
// acceptance fault, 1e9 K on 1e-12 kg). With it no occupied cell is ever
// read far above the pocket's temperature, and mass and U+K+G stay closed;
// air of ordinary density is still exactly profile 11.
namespace ma::testing::thin_heat_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
struct LightGas {
  SpeciesRegistry registry;
  std::shared_ptr<const FluidThermodynamics> eos;
  LightGas() {
    SpeciesDefinition gas;
    gas.id = "light_game_gas"; gas.name = "Light game gas"; gas.phase = Phase::gas;
    gas.atoms = {{"H", 1}}; gas.explicit_molar_mass = true; gas.molar_mass = .00100794;
    gas.cp = 2400 + gas_constant / gas.molar_mass; gas.conductivity = .168;
    registry.add(gas);
    ReferenceMaterial material;
    material.id = gas.id; material.phase = ReferencePhase::ideal_gas;
    material.molar_mass_kg_per_mol = gas.molar_mass; material.cv_j_per_kg_k = gas.cv();
    material.energy_offset_j_per_kg = gas.internal_energy(0);
    material.minimum_temperature_k = 1; material.maximum_temperature_k = 1e9;
    material.provenance = "Explicit thin-gas heating control";
    eos = std::make_shared<const FluidThermodynamics>(
        registry, "thin-heat-control-v1", std::vector<ReferenceMaterial>{material});
  }
};
inline const LightGas &light() { static const LightGas value; return value; }
// A W x H box (walls included): the pocket fills columns 1..pocket at
// `density` kg a cell, 300 K; the rest is empty.
inline MacClosedWorld leak(MacGameProfile profile, std::size_t W, std::size_t H,
                           std::size_t pocket, double density) {
  const auto &g = light();
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    MixtureInventory mixture(1);
    state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
    if (!state.grid.walls[i] && x <= pocket) mixture.masses[0] = density;
    state.cells.push_back({mixture, {}});
  }
  state.momentum_x.assign(state.grid.x_width() * H, 0);
  state.momentum_y.assign(W * state.grid.y_height(), 0);
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game; cfg.game_profile = profile;
  cfg.vacuum = MacVacuumRepresentation::void_phase; cfg.thermal_conduction = true;
  cfg.maximum_timestep_s = static_cast<double>(.2f); cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400; cfg.game.heat_maximum_iterations = 800;
  cfg.game.gas_heat_mixing = 8; cfg.game.phase_rule = MacPhaseRule::boil_only;
  return MacClosedWorld::game_colony(g.registry, g.eos, std::move(state),
                                     std::vector<double>(W * H, 300), cfg);
}
struct Outcome {
  double hottest = 0;
  long double mass_error = 0, energy_error = 0;
  std::string fault;
};
inline Outcome run_leak(MacGameProfile profile, std::size_t W, std::size_t H,
                        std::size_t pocket, double density, unsigned ticks) {
  auto world = leak(profile, W, H, pocket, density);
  Outcome out;
  long double mass0 = 0;
  for (const auto &cell : world.state().cells) mass0 += cell.mixture.mass();
  const auto energy0 = surface_game::total_energy(world);
  long double sunk_mass = 0, sunk_energy = 0;
  try {
    for (unsigned tick = 0; tick < ticks; ++tick) {
      auto next = world.advanced(static_cast<double>(.2f));
      const auto &t = next.report.trace_sink;
      for (const auto &m : t.mass_kg) sunk_mass += mac_energy_long_double(m);
      sunk_energy += mac_energy_long_double(t.internal_energy_j) + mac_energy_long_double(t.kinetic_energy_j) +
                     mac_energy_long_double(t.potential_energy_j);
      world = std::move(next.world);
      for (std::size_t i = 0; i < W * H; ++i)
        if (!world.state().grid.walls[i] && world.state().cells[i].mixture.mass() > 0)
          out.hottest = std::max(out.hottest, world.cell_reading(i).temperature_k);
    }
  } catch (const std::exception &e) {
    out.fault = e.what();
    return out;
  }
  long double mass1 = 0;
  for (const auto &cell : world.state().cells) mass1 += cell.mixture.mass();
  out.mass_error = std::abs(mass1 + sunk_mass - mass0) / mass0;
  out.energy_error = std::abs(surface_game::total_energy(world) + sunk_energy - energy0) / std::abs(energy0);
  return out;
}
inline void run() {
  const auto saved = mac_kernel_default_options();
  auto one = saved;
  one.threads = 1;
  set_mac_kernel_default_options(one);
  // A 0.156 kg/cell (386 kPa) pocket in columns 1-3 of a 30x3 box. Before
  // the exchange profile 12 read 1.06e6 K here; profile 11 reads 5.1e4 K.
  const auto plain = run_leak(MacGameProfile::game_kernel_convective, 30, 3, 3, .156, 5);
  const auto thin = run_leak(MacGameProfile::game_kernel_thin_drag, 30, 3, 3, .156, 5);
  check(plain.fault.empty() && thin.fault.empty(), "Thin-gas heating control faulted");
  check(thin.hottest < 1e4, "Profile 12 heated a thin leak front above 1e4 K");
  check(thin.hottest < plain.hottest, "Profile 12 leak front is not cooler than profile 11's");
  check(thin.mass_error <= 1e-12L && thin.energy_error <= 1e-12L, "Profile 12 thin-gas heating lost mass or U+K+G");
  // Checkpoint 32 continues the leak exactly cold and on 12 workers.
  auto world = leak(MacGameProfile::game_kernel_thin_drag, 30, 3, 3, .156);
  for (unsigned tick = 0; tick < 2; ++tick) world = world.advanced(static_cast<double>(.2f)).world;
  const auto image = world.checkpoint();
  const auto live = world.advanced(static_cast<double>(.2f)).world.checkpoint();
  auto twelve = saved;
  twelve.threads = 12;
  set_mac_kernel_default_options(twelve);
  const auto cold = MacClosedWorld::from_checkpoint(light().registry, light().eos, image);
  const auto restored = cold.advanced(static_cast<double>(.2f)).world.checkpoint();
  set_mac_kernel_default_options(saved);
  check(image.at(8) == 32 && live == restored, "Profile 12 leak cold/12-worker continuation differs");
  std::cout << "PASS profile12 thin-gas heating: a 386 kPa light-gas leak into an empty box reads at most "
            << thin.hottest << " K (profile 11 " << plain.hottest
            << " K) with mass and U+K+G closed; checkpoint 32 continues exactly cold and on 12 workers\n";
}
} // namespace ma::testing::thin_heat_game
