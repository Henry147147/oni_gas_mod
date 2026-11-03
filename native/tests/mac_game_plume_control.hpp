#pragma once
#include "../src/mac_game/layering.hpp"
#include <iostream>

// Profile 11 convective adjustment beside a launch plume (Spaced Out! rocket
// flight, docs/dlc-compatibility.md). Two near-vacuum cells sit under two
// denser ones, so the gas is buoyantly unstable, while the plume crosses
// the upper pair at 1500 m/s. A swap there would carry the plume's momentum
// into the near-vacuum cells, and the face kinetic energy it books exceeds
// their whole thermal energy. The resolved flow is far faster than the
// free-convection speed, so the adjustment leaves these faces alone; with
// the plume at rest the same column exchanges, exactly conserving species
// and energy.
namespace ma::testing {
inline void convective_plume_control() {
  namespace k = ma::experimental::game_kernel;
  using ma::experimental::MacGrid;
  const auto check = [](bool ok, const char *why) {
    if (!ok) throw std::runtime_error(why);
  };
  std::vector<k::Species> species(1);
  species[0].gas = true; species[0].molar_mass = .032; species[0].cv = 650;
  for (const bool plume : {true, false}) {
    MacGrid g;
    g.width = 2; g.height = 2; g.walls.resize(4);
    k::State s;
    s.energy.resize(4);
    s.jx.resize(g.x_width() * g.height);
    s.jy.resize(g.width * g.y_height());
    for (std::size_t i = 0; i < 4; ++i) {
      const bool upper = i >= 2;
      const double mass = upper ? 4.3e-9 : 4e-11, temperature = upper ? 119 : 95;
      s.masses.offsets.push_back(s.masses.mass.size());
      s.masses.species.push_back(0);
      s.masses.mass.push_back(mass);
      s.energy[i] = mass * species[0].cv * temperature;
      s.passives.offsets.push_back(0);
    }
    s.masses.offsets.push_back(s.masses.mass.size());
    s.passives.offsets.push_back(0);
    if (plume)
      s.jx[g.x_face(1, 1)] = 4.3e-9 * 1500;
    std::vector<k::Eos> eos(4);
    for (std::size_t i = 0; i < 4; ++i)
      eos[i] = k::evaluate_selected(species, 1, [](std::size_t) { return 0U; },
                                    [&](std::size_t) { return s.masses.mass[i]; },
                                    s.energy[i], 1);
    const ma::experimental::mac_kernel::Grid grid(g);
    const auto next = k::layer(g, species, s, eos, .2, 9.81, &grid, nullptr, true,
                               nullptr, true);
    if (plume) {
      check(next.faces == 0, "Convective adjustment swapped gas beside a 1500 m/s plume");
      continue;
    }
    check(next.faces > 0, "Convective adjustment left a quiescent unstable column");
    double mass = 0, energy = 0, before_energy = 0;
    for (double value : next.state.masses.mass) mass += value;
    for (std::size_t i = 0; i < 4; ++i) {
      energy += next.state.energy[i]; before_energy += s.energy[i];
      check(next.state.energy[i] > 0, "Convective adjustment left a cell without thermal energy");
    }
    check(std::abs(mass - 2 * (4.3e-9 + 4e-11)) <= 1e-15 * mass, "Convective adjustment changed mass");
    // The potential energy released stays as heat: U rises by exactly the drop.
    check(energy > before_energy, "Convective adjustment released no potential energy");
  }
  std::cout << "PASS profile11 convective adjustment leaves near-vacuum cells beside a 1500 m/s plume "
               "alone and still overturns the same column at rest\n";
}
} // namespace ma::testing
