#pragma once
#include "mac_fixture.hpp"
#include "mac_game_liquid_control.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <cmath>
#include <iostream>

// Profile 4 free-surface closure: a gas-free liquid surface facing vacuum
// stays at rest instead of accumulating free-fall momentum on a face nothing
// can cross, and momentum stranded on such a face becomes local heat with
// closed U+K+G and impulse accounts. Profile 3 keeps its original behaviour.
// Profile 5 adds only a trace sink before conduction.
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

// A hot synthetic gas expands down a long evacuated channel. At tick46 the
// last flow half creates tiny cold cells: carrying their conduction heat into
// the later sink differs from removing them before heat. No licensed fields.
inline MacClosedWorld trace_front() {
  SpeciesRegistry registry;
  SpeciesDefinition gas;
  gas.id = "trace_heat_gas"; gas.name = "Trace heat control gas";
  gas.phase = Phase::gas; gas.atoms = {{"O", 2}};
  gas.explicit_molar_mass = true; gas.molar_mass = .032;
  gas.cp = 700 + gas_constant / gas.molar_mass; gas.conductivity = 20;
  gas.formation_enthalpy = (-3e6 + gas.cp * reference_temperature) * gas.molar_mass;
  registry.add(gas);
  ReferenceMaterial material;
  material.id = gas.id; material.phase = ReferencePhase::ideal_gas;
  material.molar_mass_kg_per_mol = gas.molar_mass; material.cv_j_per_kg_k = gas.cv();
  material.energy_offset_j_per_kg = gas.internal_energy(0);
  material.minimum_temperature_k = 1; material.maximum_temperature_k = 1e6;
  material.provenance = "Explicit synthetic trace conduction control";
  auto eos = std::make_shared<const FluidThermodynamics>(registry,
      "trace-heat-control-v1", std::vector<ReferenceMaterial>{material});
  constexpr unsigned W = 129, H = 3;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  state.cells.resize(W * H, MacMaterialCell{MixtureInventory(1), {}});
  for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x)
    state.grid.walls[x + y * W] = y == 0 || y == H - 1 || x == 0 || x == W - 1;
  state.cells[W + 1].mixture.masses[0] = 1;
  state.cells[W + 1].passive_quantities = {{17, 37}, {UINT64_C(0x80000000), 1e-30}};
  state.momentum_x.resize((W + 1) * H); state.momentum_y.resize(W * (H + 1));
  std::vector<double> temperatures(W * H, 300); temperatures[W + 1] = 10000;
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_free_surface;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.thermal_conduction = true; cfg.maximum_timestep_s = .2;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400; cfg.game.heat_maximum_iterations = 800;
  cfg.game.sharpen_liquid_interfaces = false;
  return MacClosedWorld::game_colony(std::move(registry), std::move(eos),
      std::move(state), temperatures, cfg);
}
inline void trace_conduction() {
  auto source = trace_front();
  for (unsigned tick = 0; tick < 45; ++tick) source = source.advanced(.2).world;
  for (const auto &cell : source.state().cells)
    for (double mass : cell.mixture.masses)
      check(mass == 0 || mass >= 1e-11, "Trace control already contains a removable trace");
  // Construct the new policy from exactly these synthetic physical fields.
  // This test does not add a saved-world migration path.
  auto cfg = source.settings(); cfg.game_profile = MacGameProfile::game_kernel_trace_heat;
  const auto candidate = MacClosedWorld(source.registry(),
      std::make_shared<const FluidThermodynamics>(source.thermodynamics()), source.state(), cfg);
  const auto legacy = source.advanced(.2), fixed = candidate.advanced(.2);
  const auto &sink = fixed.report.trace_sink;
  check(sink.constituents > 0 && sink.cells > 0 &&
            sink.mass_kg[0].project().value == legacy.report.trace_sink.mass_kg[0].project().value,
        "Pre-heat trace control did not remove the same transported material");
  check(std::abs(mac_energy_long_double(sink.internal_energy_j) -
                 mac_energy_long_double(legacy.report.trace_sink.internal_energy_j)) > 1e-9L,
        "Trace control did not exercise the changed heat/sink ordering");
  const auto removed_energy = mac_energy_long_double(sink.internal_energy_j) +
      mac_energy_long_double(sink.kinetic_energy_j) + mac_energy_long_double(sink.potential_energy_j);
  const auto before = total_energy(candidate), after = total_energy(fixed.world);
  check(std::abs(before - after - removed_energy) <=
            1e-12L * (std::abs(before) + std::abs(after) + std::abs(removed_energy)),
        "Pre-heat trace sink lost U+K+G");
  const auto passive = [](const MacClosedWorld &world, std::uint64_t identity) {
    long double sum = 0;
    for (const auto &cell : world.state().cells)
      for (const auto &row : cell.passive_quantities)
        if (row.identity == identity) sum += row.quantity;
    return sum;
  };
  for (auto identity : {UINT64_C(17), UINT64_C(0x80000000)}) {
    long double removed = 0;
    for (const auto &row : sink.passives) if (row.identity == identity) removed += row.quantity;
    const auto initial = passive(candidate, identity), remaining = passive(fixed.world, identity);
    check(removed > 0 && std::abs(initial - remaining - removed) <=
              1e-12L * (std::abs(initial) + std::abs(remaining) + std::abs(removed)),
          "Pre-heat trace sink lost a passive identity");
  }
  const auto image = candidate.checkpoint();
  const auto cold = MacClosedWorld::from_checkpoint(candidate.registry(),
      std::make_shared<const FluidThermodynamics>(candidate.thermodynamics()), image);
  check(cold.advanced(.2).world.checkpoint() == fixed.world.checkpoint(),
        "Pre-heat trace cold continuation differs");
  std::cout << "PASS profile5 transported trace removal before conduction with "
               "closed U+K+G, both passive identities and exact cold continuation\n";
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
  trace_conduction();
  // Family 5 only adds the trace sink before conduction. With no trace it is
  // the identity, so the basin matches family 4 byte for byte apart from the
  // version word; checkpoint 23 restores cold and continues at 1/12 workers.
  set_mac_kernel_default_options(one);
  auto five = basin(MacGameProfile::game_kernel_trace_heat);
  auto four = basin(MacGameProfile::game_kernel_free_surface);
  for (unsigned tick = 0; tick < 10; ++tick) {
    five = five.advanced(.2).world;
    four = four.advanced(.2).world;
  }
  auto five_image = five.checkpoint();
  check(five_image.at(8) == 23, "Profile 5 did not write checkpoint 23");
  const auto five_cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, five_image);
  check(five_cold.checkpoint() == five_image &&
            five_cold.settings().game_profile == MacGameProfile::game_kernel_trace_heat,
        "Checkpoint 23 cold round trip differs");
  const auto five_one = five.advanced(.2).world;
  set_mac_kernel_default_options(many);
  check(five_one.checkpoint() == five_cold.advanced(.2).world.checkpoint(),
        "Profile 5 cold/worker continuation differs");
  five_image[8] = 22;
  ma::testing::positive_game::rewrite_checksum(five_image);
  check(five_image == four.checkpoint(), "Profile 5 trace sink was not inert without traces");
  set_mac_kernel_default_options(saved);
  std::cout << "PASS profile5 trace sink before conduction inert without traces "
               "(basin equals profile4 apart from version), checkpoint23 and "
               "workers1/12 cold continuation\n";
  set_mac_kernel_default_options(saved);
  std::cout << "PASS profile4 free-surface closure: resting gas-free surface "
               "stays at rest (profile3 control unchanged), stranded momentum "
               "becomes heat with closed U+K+G, checkpoint22 and workers1/12 "
               "cold continuation\n";
}
} // namespace ma::testing::surface_game
