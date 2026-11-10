#pragma once
#include "mac_game_surface_control.hpp"
#include "../src/mac_game/surface.hpp"
#include "../src/mac_game/totals.hpp"
#include <array>

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
inline void run() {
  normal_controls();
  settling_control(false);
  settling_control(true);
}
} // namespace ma::testing::surface_stability_game
