#pragma once
#include "mac_fixture.hpp"
#include "mac_game_surface_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// A light ideal gas with the game's hydrogen data (molar mass 1.008 g/mol,
// cv 2400 J/(kg K), so gamma 4.44) cools below the catalogue's 1 K floor as it
// expands into vacuum. The game kernel admits an ideal gas at any positive
// temperature, so the flow continues instead of refusing its own isentrope;
// mass and U+K+G stay closed. Profiles 9 and 10 (Spaced Out! rail-gun fault,
// docs/material-catalogue.md).
namespace ma::testing::cold_gas_game {
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
inline void run() {
  SpeciesRegistry registry;
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
  material.provenance = "Explicit light-gas expansion control";
  auto eos = std::make_shared<const FluidThermodynamics>(
      registry, "light-gas-control-v1", std::vector<ReferenceMaterial>{material});
  for (const auto profile : {MacGameProfile::game_kernel_activity_tiers,
                             MacGameProfile::game_kernel_layered_remap}) {
    constexpr std::size_t W = 12, H = 3;
    MacTransportState state;
    state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
    for (std::size_t i = 0; i < W * H; ++i) {
      const auto x = i % W, y = i / W;
      MixtureInventory mixture(1);
      state.grid.walls[i] = x == 0 || y == 0 || x == W - 1 || y == H - 1;
      if (!state.grid.walls[i] && x <= 2) mixture.masses[0] = .002;
      state.cells.push_back({mixture, {}});
    }
    state.momentum_x.assign(state.grid.x_width() * H, 0);
    state.momentum_y.assign(W * state.grid.y_height(), 0);
    MacClosedSettings cfg;
    cfg.numerical_kernel = MacNumericalKernel::game; cfg.game_profile = profile;
    cfg.vacuum = MacVacuumRepresentation::void_phase; cfg.thermal_conduction = true;
    cfg.maximum_timestep_s = static_cast<double>(.2f); cfg.trace_sink_threshold_kg = 1e-11;
    cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
    cfg.game.pressure_maximum_iterations = 400; cfg.game.heat_maximum_iterations = 800;
    cfg.game.gas_heat_mixing = 8; cfg.game.phase_rule = MacPhaseRule::boil_only;
    auto world = MacClosedWorld::game_colony(registry, eos, state,
                                             std::vector<double>(W * H, .5), cfg);
    long double mass0 = 0;
    for (const auto &cell : world.state().cells) mass0 += cell.mixture.mass();
    const auto energy0 = surface_game::total_energy(world);
    double coldest = 1e9;
    for (unsigned tick = 0; tick < 5; ++tick) {
      world = world.advanced(static_cast<double>(.2f)).world;
      for (std::size_t i = 0; i < W * H; ++i)
        if (!world.state().grid.walls[i] && world.state().cells[i].mixture.mass() > 0)
          coldest = std::min(coldest, world.cell_reading(i).temperature_k);
    }
    long double mass1 = 0;
    for (const auto &cell : world.state().cells) mass1 += cell.mixture.mass();
    const auto energy1 = surface_game::total_energy(world);
    check(coldest > 0 && coldest < .5, "Light-gas control did not expand below its start");
    check(std::abs(mass1 - mass0) <= 1e-12L * mass0, "Light-gas control changed mass");
    check(std::abs(energy1 - energy0) <= 1e-12L * std::abs(energy0), "Light-gas control lost U+K+G");
  }
  std::cout << "PASS profiles 9/10 advance a gamma-4.44 light gas expanding into vacuum below "
               "the catalogue's 1 K floor with closed mass and U+K+G\n";
}
} // namespace ma::testing::cold_gas_game
