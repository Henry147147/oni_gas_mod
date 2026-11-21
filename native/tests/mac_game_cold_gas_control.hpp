#pragma once
#include "mac_fixture.hpp"
#include "../src/mac_game/tick.hpp"
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
// A moving drop touches a 0.5 K gas film. Even an infinitesimal parcel
// would leave the liquid below its 1 K floor. The warmer liquid across
// the room can supply the energy by ordinary conduction.
inline void cold_liquid_contact() {
  namespace k = ma::experimental::game_kernel;
  MacGrid grid{3, 1, 1, 1, false, false, std::vector<std::uint8_t>(3)};
  std::vector<k::Species> species(2);
  species[0].minimum_temperature = 0;
  species[0].cv = 1000;
  species[0].conductivity = .1;
  species[1].gas = false;
  species[1].positive_liquid = true;
  species[1].cv = 4000;
  species[1].conductivity = .6;
  k::State state;
  state.masses.offsets = {0, 1, 2, 3};
  state.masses.species = {1, 0, 1};
  state.masses.mass = {1e-10, 1.6e-7, .001};
  state.energy = {1e-10 * 4000 * 280, 1.6e-7 * 1000 * .5, .001 * 4000 * 280};
  state.jx.resize(4);
  state.jy.resize(6);
  state.jx[1] = 1e-10;
  const auto advance = [&](bool conduction) {
    k::Tick tick(grid, species, {}, 1, 2, 8, false, 0, 1e-8, 0, false, 0, 2, 1,
                 conduction, 400, 800, true, true, true, true, true, 1e-6, true, true, 1e-6, .2, .002);
    return tick.advance(state, .2, 0, 1e-11);
  };
  bool refused = false;
  try { (void)advance(false); }
  catch (const std::runtime_error& error) {
    refused = std::string_view(error.what()) == "Game pressure refinement cannot advance time";
  }
  check(refused, "The cold liquid contact accepted an inadmissible transfer with conduction disabled");
  const auto result = advance(true);
  check(result.eos[1].temperature >= species[1].minimum_temperature &&
        result.mass_error < 1e-12 && result.energy_error < 1e-12 && result.pressure_refinements > 0,
        "The cold liquid contact failed to advance within the thermal, mass or energy bounds");
}
inline void run() {
  cold_liquid_contact();
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
