#pragma once
#include "mac_game_surface_control.hpp"
#include "../src/mac_game/surface.hpp"
#include "../src/mac_game/totals.hpp"
#include <algorithm>
#include <array>
#include <tuple>

namespace ma::testing::surface_stability_game {
using namespace ma::experimental;
namespace gk = ma::experimental::game_kernel;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
struct Interface {
  MacGrid grid;
  std::vector<gk::Species> species;
  gk::State state;
  std::vector<gk::Eos> eos;
};
// The same interface and wall contacts in both orientations. A single
// partially filled layer has real height variation along its tangent;
// two partial layers deliberately smear the interface along its normal.
inline Interface interface(bool transpose, bool diffuse) {
  Interface out;
  const std::size_t W = transpose ? 5 : 9, H = transpose ? 9 : 5;
  out.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  out.species.resize(2);
  out.species[0].gas = false;
  out.species[0].positive_liquid = true;
  out.species[0].cv = 4180;
  out.species[0].stiffness = 8.8e7;
  out.species[1].molar_mass = .032;
  auto &s = out.state;
  s.energy.resize(W * H);
  s.jx.resize((W + 1) * H);
  s.jy.resize(W * (H + 1));
  constexpr double p = 100000, T = 300;
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    out.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1;
    s.masses.offsets.push_back(s.masses.mass.size());
    s.passives.offsets.push_back(s.passives.mass.size());
    if (out.grid.walls[i]) continue;
    const auto n = transpose ? x : y, tangent = transpose ? y : x;
    const double alpha = diffuse ? (n == 1 ? .8 : n == 2 ? .2 : 0.)
                                 : (n == 1 ? 1. : n == 2 ? .3 + .03 * static_cast<double>(tangent) : 0.);
    double total = 0;
    for (std::uint32_t key = 0; key < 2; ++key) {
      const auto &material = out.species[key];
      const double m = key == 0 ? alpha * material.rho0 / gk::liquid_volume_fraction(material, p)
                                 : (1 - alpha) * p * material.molar_mass / (gk::R * T);
      if (m == 0) continue;
      s.masses.species.push_back(key);
      s.masses.mass.push_back(m);
      s.energy[i] += m * (material.cv * T + (key == 0 ? gk::liquid_specific_elastic(material, p) : 0.));
      total += m;
    }
    s.passives.species.push_back(17);
    s.passives.mass.push_back(total * static_cast<double>(i + 1));
  }
  s.masses.offsets.push_back(s.masses.mass.size());
  s.passives.offsets.push_back(s.passives.mass.size());
  out.eos.resize(W * H);
  for (std::size_t i = 0; i < W * H; ++i) {
    if (out.grid.walls[i]) continue;
    const auto begin = s.masses.offsets[i];
    out.eos[i] = gk::evaluate_selected(out.species, s.masses.offsets[i + 1] - begin,
        [&](std::size_t a) { return s.masses.species[begin + a]; },
        [&](std::size_t a) { return s.masses.mass[begin + a]; }, s.energy[i], 1);
  }
  return out;
}
inline void normal_controls() {
  for (const bool transpose : {false, true}) {
    const auto source = interface(transpose, false);
    const auto legacy = gk::sharpen(source.grid, source.species, source.state, source.eos, .2, 0);
    check(legacy.faces > 0, "Tangential control no longer exposes legacy surface gathering");
    const auto fixed = gk::sharpen(source.grid, source.species, source.state, source.eos, .2, 0,
                                 0, nullptr, nullptr, nullptr, true, nullptr, true);
    check(fixed.faces == 0, "Sharpening changed a one-layer surface's physical height variation");
    const auto smear = interface(transpose, true);
    const auto sharpened = gk::sharpen(smear.grid, smear.species, smear.state, smear.eos, .2, 9.81,
                                     0, nullptr, nullptr, nullptr, true, nullptr, true);
    check(sharpened.faces > 0, "Normal sharpening was disabled with tangential gathering");
    const auto before = gk::totals(smear.grid, smear.state, 2, 9.81);
    const auto after = gk::totals(smear.grid, sharpened.state, 2, 9.81);
    for (std::size_t q = 0; q < 2; ++q)
      check(std::abs(after.species[q].value() - before.species[q].value()) < 1e-12 * before.species[q].value(),
            "Normal sharpening changed a species total");
    check(std::abs(after.energy.value() - before.energy.value()) < 1e-12 * before.energy_operands.value(),
          "Normal sharpening lost U+K+G");
    check(std::abs(after.passives.at(17).value() - before.passives.at(17).value()) < 1e-12 * before.passives.at(17).value(),
          "Normal sharpening lost passive identity");
  }
}
inline MacClosedWorld basin() {
  const auto &f = setup();
  constexpr std::size_t W = 18, H = 9;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto water = f.registry.index("water"), oxygen = f.registry.index("oxygen");
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    state.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!state.grid.walls[i]) {
      if (y < 4)
        cell.mixture.masses[water] = 1000 + 1000 * (100000 + 9810 * (4.5 - static_cast<double>(y))) / (2.2e9 * .04);
      else if (y == 4) {
        const double alpha = x % 2 ? .3 : .7;
        cell.mixture.masses[water] = 1000 * alpha;
        cell.mixture.masses[oxygen] = (1 - alpha) * 100000 * .032 / (gas_constant * 300);
      } else cell.mixture.masses[oxygen] = 100000 * .032 / (gas_constant * 300);
      cell.passive_quantities = {{17, cell.mixture.mass() * static_cast<double>(i + 1)}};
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_thin_drag;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.maximum_timestep_s = .2;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state),
                                    std::vector<double>(W * H, 300), cfg);
}
inline std::pair<double, double> wave(const MacClosedWorld &world) {
  const auto &s = world.state();
  const auto water = world.registry().index("water");
  double low = 1e30, high = 0, speed = 0;
  for (std::size_t x = 1; x + 1 < s.grid.width; ++x) {
    double mass = 0;
    for (std::size_t y = 1; y + 1 < s.grid.height; ++y)
      mass += s.cells[x + y * s.grid.width].mixture.masses[water];
    low = std::min(low, mass);
    high = std::max(high, mass);
  }
  const auto [ix, iy] = surface_game::inertias(s);
  for (std::size_t f = 0; f < ix.size(); ++f)
    if (ix[f] > 0) speed = std::max(speed, std::abs(s.momentum_x[f] / ix[f]));
  for (std::size_t f = 0; f < iy.size(); ++f)
    if (iy[f] > 0) speed = std::max(speed, std::abs(s.momentum_y[f] / iy[f]));
  return {high - low, speed};
}
inline void settling_control(bool wake) {
  const auto saved = mac_kernel_default_options();
  auto options = saved;
  options.threads = 1;
  set_mac_kernel_default_options(options);
  auto world = basin();
  if (wake) world = world.with_game_activity_tiers(true);
  std::vector<std::uint8_t> touched(world.state().cells.size());
  touched[19] = 1;
  const auto energy0 = surface_game::total_energy(world);
  const double initial_height = wave(world).first;
  long double mass0 = 0, passive0 = 0, sunk_mass = 0, sunk_energy = 0, sunk_passive = 0;
  for (const auto &cell : world.state().cells) {
    mass0 += cell.mixture.mass();
    for (const auto &q : cell.passive_quantities) passive0 += q.quantity;
  }
  double late_height = 0, late_speed = 0;
  // The initial pressure is approximate hydrostatic balance: ordinary
  // settling/reflected waves are expected. Bound a whole late window,
  // rather than demanding exact rest or selecting a favorable endpoint.
  for (unsigned tick = 1; tick <= 1000; ++tick) {
    // A synthetic disturbance touches the region every three game seconds.
    // No heat, mass or momentum is added. Between touches the two-second
    // hold expires, so this also exercises accumulated remote flow and wakes.
    if (wake && (tick - 1) % 15 == 0)
      world = world.with_game_activity(touched, {});
    auto next = std::move(world).advanced(.2);
    for (const auto &m : next.report.trace_sink.mass_kg) sunk_mass += mac_energy_long_double(m);
    const auto &sink = next.report.trace_sink;
    sunk_energy += mac_energy_long_double(sink.internal_energy_j) + mac_energy_long_double(sink.kinetic_energy_j) +
                   mac_energy_long_double(sink.potential_energy_j);
    for (const auto &q : sink.passives) sunk_passive += q.quantity;
    world = std::move(next.world);
    if (tick >= 500) {
      const auto [height, speed] = wave(world);
      late_height = std::max(late_height, height);
      late_speed = std::max(late_speed, speed);
    }
  }
  check(late_height < .2 * initial_height && late_speed < .1,
        "Unforced water surface maintained cell-scale waves instead of settling");
  long double mass1 = 0, passive1 = 0;
  for (const auto &cell : world.state().cells) {
    mass1 += cell.mixture.mass();
    for (const auto &q : cell.passive_quantities) passive1 += q.quantity;
  }
  check(std::abs(mass1 + sunk_mass - mass0) < 1e-12L * mass0, "Settling basin changed mass");
  check(std::abs(surface_game::total_energy(world) + sunk_energy - energy0) < 1e-12L * std::abs(energy0),
        "Settling basin lost U+K+G");
  check(std::abs(passive1 + sunk_passive - passive0) < 1e-12L * passive0, "Settling basin lost passive identity");
  const auto image = world.checkpoint();
  const auto &f = setup();
  const auto cold = MacClosedWorld::from_checkpoint(f.registry, f.eos, image);
  const auto live = world.advanced(wake ? 1. : .2).world.checkpoint();
  options.threads = 12;
  set_mac_kernel_default_options(options);
  check(cold.checkpoint() == image && cold.advanced(wake ? 1. : .2).world.checkpoint() == live,
        "Settling basin cold/requested-worker continuation differs");
  set_mac_kernel_default_options(saved);
  std::cout << (wake ? "PASS profile12 repeated activity wakes: " : "PASS profile12 surface normal/tangent controls: ")
            << "unforced basin late height/speed "
            << late_height << " kg/" << late_speed << " m/s; closed accounts and exact cold/requested-worker continuation\n";
}
// A wide pool with a step (2026-11-12 playtest: a 35-tile pool kept a
// half-tile step sloshing from side to side for over a minute after a shaft
// was dug under it). 35 columns, three cells deep, the surface layer a
// quarter full on the left half and three quarters on the right. With the
// liquid drag the two halves level without changing sides and the surface is
// flat within twenty seconds; without it the step sloshed at full height.
inline void pool_step_control() {
  const auto saved = mac_kernel_default_options();
  auto options = saved;
  options.threads = 1;
  set_mac_kernel_default_options(options);
  const auto &f = setup();
  constexpr std::size_t open = 35, W = open + 2, H = 8;
  constexpr double gas = 138000;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto water = f.registry.index("water"), oxygen = f.registry.index("oxygen");
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    state.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1;
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!state.grid.walls[i]) {
      if (y < 3)
        cell.mixture.masses[water] = 1000 + 1000 * (gas + 9810 * (3.5 - static_cast<double>(y))) / (2.2e9 * .04);
      else if (y == 3) {
        const double alpha = x <= open / 2 ? .25 : .75;
        cell.mixture.masses[water] = 1000 * alpha;
        cell.mixture.masses[oxygen] = (1 - alpha) * gas * .032 / (gas_constant * 300);
      } else cell.mixture.masses[oxygen] = gas * .032 / (gas_constant * 300);
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_thin_drag;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.maximum_timestep_s = .2;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  auto world = MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), std::vector<double>(W * H, 300), cfg);
  const auto energy0 = surface_game::total_energy(world);
  long double mass0 = 0, sunk_mass = 0, sunk_energy = 0;
  for (const auto &cell : world.state().cells) mass0 += cell.mixture.mass();
  // Mean column mass of the right half less the left half, and the range.
  const auto step = [&](const MacClosedWorld &w) {
    double left = 0, right = 0, low = 1e30, high = 0;
    for (std::size_t x = 1; x + 1 < W; ++x) {
      double column = 0;
      for (std::size_t y = 1; y + 1 < H; ++y) column += w.state().cells[x + y * W].mixture.masses[water];
      (x <= open / 2 ? left : right) += column;
      low = std::min(low, column);
      high = std::max(high, column);
    }
    return std::pair{right / (open - open / 2) - left / (open / 2), high - low};
  };
  const double initial = step(world).first;
  double reversed = 0, late = 0;
  for (unsigned tick = 1; tick <= 150; ++tick) {
    auto next = std::move(world).advanced(.2);
    for (const auto &m : next.report.trace_sink.mass_kg) sunk_mass += mac_energy_long_double(m);
    const auto &sink = next.report.trace_sink;
    sunk_energy += mac_energy_long_double(sink.internal_energy_j) + mac_energy_long_double(sink.kinetic_energy_j) +
                   mac_energy_long_double(sink.potential_energy_j);
    world = std::move(next.world);
    const auto [difference, range] = step(world);
    reversed = std::min(reversed, difference);
    if (tick > 100) late = std::max(late, range);
  }
  check(initial > 499 && reversed > -.05 * initial, "A stepped pool sloshed to the other side instead of levelling");
  check(late < .02 * initial, "A stepped pool was not flat within twenty seconds");
  long double mass1 = 0;
  for (const auto &cell : world.state().cells) mass1 += cell.mixture.mass();
  check(std::abs(mass1 + sunk_mass - mass0) < 1e-12L * mass0, "The stepped pool changed mass");
  check(std::abs(surface_game::total_energy(world) + sunk_energy - energy0) < 1e-12L * std::abs(energy0),
        "The stepped pool lost U+K+G");
  set_mac_kernel_default_options(saved);
  std::cout << "PASS profile12 liquid drag: a 35-column pool with a " << initial << " kg step levels without changing sides (lowest "
            << reversed << " kg) and holds " << late << " kg of range from 20 to 30 s; mass and U+K+G closed\n";
}
// Profile 12, water above a sealed room of air, joined by a one-cell hole in
// the floor between them. The hole's face carries one velocity, so the flow
// alone compresses the room's air until it bears the water, which then stands
// on it for good (31.7 t of 40 t in the probe of 2026-11-12). With the
// liquid-over-gas exchange the water falls through while the air rises past
// it, and the room fills.
inline void sealed_drain_control() {
  const auto saved = mac_kernel_default_options();
  auto options = saved;
  options.threads = 1;
  set_mac_kernel_default_options(options);
  const auto &f = setup();
  // Rows 1-2: the sealed room; row 3: its ceiling with the hole; rows 4-6:
  // water; rows 7-8: air.
  constexpr std::size_t W = 6, H = 10, floor = 3, hole = 2 + floor * W;
  constexpr double gas = 138000;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  const auto water = f.registry.index("water"), oxygen = f.registry.index("oxygen");
  for (std::size_t i = 0; i < W * H; ++i) {
    const auto x = i % W, y = i / W;
    state.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1 || (y == floor && i != hole);
    MacMaterialCell cell{MixtureInventory(f.registry.size()), {}};
    if (!state.grid.walls[i]) {
      if (y > floor && y <= floor + 3)
        cell.mixture.masses[water] = 1000 + 1000 * (gas + 9810 * (floor + 3.5 - static_cast<double>(y))) / (2.2e9 * .04);
      else cell.mixture.masses[oxygen] = gas * .032 / (gas_constant * 300);
    }
    state.cells.push_back(std::move(cell));
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_thin_drag;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.maximum_timestep_s = .2;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  auto world = MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), std::vector<double>(W * H, 300), cfg);
  const auto energy0 = surface_game::total_energy(world);
  long double mass0 = 0, sunk_mass = 0, sunk_energy = 0;
  for (const auto &cell : world.state().cells) mass0 += cell.mixture.mass();
  // The room and its hole: the water and the air they hold.
  const auto room = [&](const MacClosedWorld &w) {
    double wet = 0, air = 0;
    for (std::size_t i = 0; i < (floor + 1) * W; ++i) {
      wet += w.state().cells[i].mixture.masses[water];
      air += w.state().cells[i].mixture.masses[oxygen];
    }
    return std::pair{wet, air};
  };
  const double air0 = room(world).second;
  double half_at = 0;
  for (unsigned tick = 1; tick <= 600; ++tick) {
    auto next = std::move(world).advanced(.2);
    for (const auto &m : next.report.trace_sink.mass_kg) sunk_mass += mac_energy_long_double(m);
    const auto &sink = next.report.trace_sink;
    sunk_energy += mac_energy_long_double(sink.internal_energy_j) + mac_energy_long_double(sink.kinetic_energy_j) +
                   mac_energy_long_double(sink.potential_energy_j);
    world = std::move(next.world);
    if (half_at == 0 && room(world).first > 4500) half_at = .2 * tick;
  }
  const auto [wet1, air1] = room(world);
  // Nine cells: the room fills and its air has gone up through the water,
  // but for bubbles under the flat ceiling away from the hole (0.32 kg).
  check(wet1 > 8500, "Water above a sealed room did not drain through a one-cell hole");
  check(air0 > 10 && air1 < .05 * air0, "The sealed room's air did not rise past the draining water");
  long double mass1 = 0;
  for (const auto &cell : world.state().cells) mass1 += cell.mixture.mass();
  check(std::abs(mass1 + sunk_mass - mass0) < 1e-12L * mass0, "The draining pool changed mass");
  check(std::abs(surface_game::total_energy(world) + sunk_energy - energy0) < 1e-12L * std::abs(energy0),
        "The draining pool lost U+K+G");
  set_mac_kernel_default_options(saved);
  std::cout << "PASS profile12 liquid over gas: a sealed nine-cell room under 12 t of water fills through a one-cell hole (half by "
            << half_at << " s; " << wet1 << " kg of water and " << air1 << " of " << air0 << " kg of air in it after 120 s)\n";
}
// Water under a lighter immiscible liquid (870 kg/m3, like crude oil) under
// 130 kPa of oxygen in a closed 10 x 7 box. Each half of the box has its own
// water and oil tops (in cells); the liquid cells start compressed only by
// the liquid above them, so the air settles onto the pool as it starts.
struct LiquidPair {
  SpeciesRegistry registry;
  std::shared_ptr<const FluidThermodynamics> eos;
  std::size_t oxygen = 0, water = 0, oil = 0;
};
inline const LiquidPair &liquid_pair() {
  static const LiquidPair value = [] {
    LiquidPair out;
    const auto standard = SpeciesRegistry::standard();
    auto oil = standard[standard.index("water")];
    oil.id = "interface_oil";
    oil.name = "Interface control oil";
    oil.density = 870;
    oil.cp = 1690;
    std::vector<ReferenceMaterial> materials;
    for (const auto &[sp, density, modulus] :
         {std::tuple{standard[standard.index("oxygen")], 0., 0.},
          std::tuple{standard[standard.index("water")], 1000., 2.2e9}, std::tuple{oil, 870., 1.5e9}}) {
      out.registry.add(sp);
      ReferenceMaterial m;
      m.id = sp.id;
      m.phase = sp.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
      m.molar_mass_kg_per_mol = sp.molar_mass;
      m.cv_j_per_kg_k = sp.cv();
      m.energy_offset_j_per_kg = sp.internal_energy(0);
      m.minimum_temperature_k = 1;
      m.maximum_temperature_k = 1e6;
      m.zero_pressure_density_kg_per_m3 = density;
      m.reference_bulk_modulus_pa = modulus;
      m.maximum_density_ratio = sp.phase == Phase::gas ? 1 : 2;
      m.provenance = "Two-liquid surface control surrogate";
      materials.push_back(std::move(m));
    }
    out.eos = std::make_shared<const FluidThermodynamics>(out.registry, "liquid-interface-control-v1",
                                                          std::move(materials));
    out.oxygen = out.registry.index("oxygen");
    out.water = out.registry.index("water");
    out.oil = out.registry.index("interface_oil");
    return out;
  }();
  return value;
}
inline MacClosedWorld liquid_pair_box(std::array<double, 2> water_top, std::array<double, 2> oil_top) {
  const auto &f = liquid_pair();
  constexpr std::size_t W = 12, H = 9;
  constexpr double gas = 130000, T = 295;
  MacTransportState state;
  state.grid = {W, H, 1, 1, false, false, std::vector<std::uint8_t>(W * H)};
  state.cells.resize(W * H, MacMaterialCell{MixtureInventory(f.registry.size()), {}});
  for (std::size_t x = 0; x < W; ++x) {
    double above = 0; // liquid weight above the cell, Pa
    for (std::size_t y = H; y-- > 0;) {
      const auto i = x + y * W;
      state.grid.walls[i] = x == 0 || x == W - 1 || y == 0 || y == H - 1;
      if (state.grid.walls[i]) continue;
      const double low = static_cast<double>(y - 1), high = low + 1;
      const double wet = water_top[x <= 5 ? 0 : 1], top = oil_top[x <= 5 ? 0 : 1];
      const double water = std::clamp(wet - low, 0., 1.), oil = std::clamp(std::min(top, high) - std::max(wet, low), 0., 1.);
      const double weight = 9.81 * (1000 * water + 870 * oil);
      const double p = above + .5 * weight;
      auto &m = state.cells[i].mixture.masses;
      m[f.water] = water * 1000 * (1 + p / (.04 * 2.2e9));
      m[f.oil] = oil * 870 * (1 + p / (.04 * 1.5e9));
      if (water + oil < 1) m[f.oxygen] = (1 - water - oil) * gas * .032 / (gas_constant * T);
      above += weight;
    }
  }
  state.momentum_x.resize((W + 1) * H);
  state.momentum_y.resize(W * (H + 1));
  MacClosedSettings cfg;
  cfg.numerical_kernel = MacNumericalKernel::game;
  cfg.game_profile = MacGameProfile::game_kernel_thin_drag;
  cfg.vacuum = MacVacuumRepresentation::void_phase;
  cfg.maximum_timestep_s = .2;
  cfg.gravity_m_per_s2 = 9.81;
  cfg.trace_sink_threshold_kg = 1e-11;
  cfg.game.liquid_law = MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations = 400;
  cfg.game.heat_maximum_iterations = 800;
  return MacClosedWorld::game_colony(f.registry, f.eos, std::move(state), std::vector<double>(W * H, T), cfg);
}
struct LiquidPairRun {
  double range = 0, gas_under = 0, water_lifted = 0, flat_after = -1, mass_error = 0, energy_error = 0;
};
// Advances `seconds`; range is the final water column range, gas_under the
// largest gas volume in rows 1-3 (all liquid at rest), water_lifted the
// largest water mass in rows 4 and up, flat_after the first time from which
// the water column range stays under 25 kg.
inline LiquidPairRun run_liquid_pair(MacClosedWorld world, double seconds) {
  const auto &f = liquid_pair();
  const auto W = world.state().grid.width;
  const auto energy0 = surface_game::total_energy(world);
  long double mass0 = 0, sunk_mass = 0, sunk_energy = 0;
  for (const auto &cell : world.state().cells) mass0 += cell.mixture.mass();
  LiquidPairRun out;
  const auto ticks = static_cast<unsigned>(std::lround(seconds / .2));
  for (unsigned tick = 1; tick <= ticks; ++tick) {
    auto next = std::move(world).advanced(.2);
    for (const auto &m : next.report.trace_sink.mass_kg) sunk_mass += mac_energy_long_double(m);
    const auto &sink = next.report.trace_sink;
    sunk_energy += mac_energy_long_double(sink.internal_energy_j) + mac_energy_long_double(sink.kinetic_energy_j) +
                   mac_energy_long_double(sink.potential_energy_j);
    world = std::move(next.world);
    double low = 1e30, high = 0, gas = 0, lifted = 0;
    for (std::size_t x = 1; x + 1 < W; ++x) {
      double column = 0;
      for (std::size_t y = 1; y + 1 < world.state().grid.height; ++y) {
        const auto i = x + y * W;
        const double water = world.state().cells[i].mixture.masses[f.water];
        column += water;
        if (y <= 3 && world.state().cells[i].mixture.masses[f.oxygen] > 0) gas += world.cell_reading(i).gas_volume_m3;
        if (y >= 4) lifted += water;
      }
      low = std::min(low, column);
      high = std::max(high, column);
    }
    out.range = high - low;
    out.gas_under = std::max(out.gas_under, gas);
    out.water_lifted = std::max(out.water_lifted, lifted);
    if (out.range >= 25) out.flat_after = -1;
    else if (out.flat_after < 0) out.flat_after = .2 * tick;
  }
  long double mass1 = 0;
  for (const auto &cell : world.state().cells) mass1 += cell.mixture.mass();
  out.mass_error = static_cast<double>(std::abs(mass1 + sunk_mass - mass0) / mass0);
  out.energy_error = static_cast<double>(std::abs(surface_game::total_energy(world) + sunk_energy - energy0) / std::abs(energy0));
  return out;
}
// Sharpening exchanges a liquid for everything else its target cell holds.
// Beside a second liquid, that is not the phase across the interface, so
// profile 12 refuses a vertical exchange that lifts net mass (surface.hpp).
// Without that refusal the resting pool below (water 2.5 cells, oil 1 cell,
// oil top inside a cell) trades its water up for oil: within 60 s it lifts
// 4425 kg of water into the oil row, draws 0.572 m3 of air under the liquid
// and ends 639 kg out of level. The tilted interface (water 2.5/1.5 cells
// under oil to a flat top at 4 cells) holds 0.18 m3 of air under its oil and
// is still 120 kg out of level at 90 s; with the refusal it is under 25 kg
// from 51.2 s, the pool keeps 1.8e-7 kg of range, 0.12 kg of lifted water
// and no gas under liquid.
inline void liquid_interface_control() {
  const auto saved = mac_kernel_default_options();
  auto options = saved;
  options.threads = 1;
  set_mac_kernel_default_options(options);
  const auto rest = run_liquid_pair(liquid_pair_box({2.5, 2.5}, {3.5, 3.5}), 60);
  const auto tilt = run_liquid_pair(liquid_pair_box({2.5, 1.5}, {4, 4}), 90);
  set_mac_kernel_default_options(saved);
  check(rest.range < 1 && rest.water_lifted < 10 && rest.gas_under < 1e-3,
        "A resting water/oil pool mixed or drew gas under its liquid");
  check(tilt.flat_after > 0, "A tilted water/oil interface did not level within 90 s");
  for (const auto &measured : {rest, tilt}) {
    check(measured.mass_error < 1e-12, "The water/oil pool changed mass");
    check(measured.energy_error < 1e-12, "The water/oil pool lost U+K+G");
  }
  std::cout << "PASS profile12 liquid interface: a resting water/oil pool keeps " << rest.range
            << " kg of column range, " << rest.water_lifted << " kg of water above its interface and "
            << rest.gas_under << " m3 of gas under liquid for 60 s; a tilted interface is under 25 kg from "
            << tilt.flat_after << " s (" << tilt.range << " kg at 90 s); mass and U+K+G closed\n";
}
inline void run() {
  normal_controls();
  settling_control(false);
  settling_control(true);
  pool_step_control();
  sealed_drain_control();
  liquid_interface_control();
}
} // namespace ma::testing::surface_stability_game
