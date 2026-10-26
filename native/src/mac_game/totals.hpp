#pragma once
#include "flow_local.hpp"
namespace ma::experimental::game_kernel {
inline Totals
totals(const ma::experimental::MacGrid &source, const State &state,
       std::size_t count, double gravity,
       const ma::experimental::mac_kernel::Grid *cached_grid = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  Totals result;
  result.species.resize(count);
  std::vector<double> mass(g.N);
  for (std::size_t i = 0; i < g.N; ++i) {
    for (std::size_t a = state.masses.offsets[i];
         a < state.masses.offsets[i + 1]; ++a) {
      mass[i] += state.masses.mass[a];
      result.species[state.masses.species[a]].add(state.masses.mass[a]);
    }
    const double potential =
        mass[i] * gravity * (static_cast<double>(i / g.W) + .5) * source.spacing_m;
    result.energy.add(state.energy[i]);
    result.energy.add(potential);
    result.energy_operands.add(std::abs(state.energy[i]));
    result.energy_operands.add(std::abs(potential));
  }
  const auto faces = [&](const auto &l, const auto &r, const auto &j,
                         DoubleDouble &momentum, DoubleDouble &operands) {
    for (std::size_t f = 0; f < j.size(); ++f)
      if (j[f] != 0) {
        const double inertia = .5 * ((l[f] == k::none ? 0 : mass[l[f]]) +
                                     (r[f] == k::none ? 0 : mass[r[f]]));
        if (!(inertia > 0))
          throw std::runtime_error("Private totals J without inertia");
        const double kinetic = .5 * j[f] * (j[f] / inertia);
        result.energy.add(kinetic);
        result.energy_operands.add(kinetic);
        momentum.add(j[f]);
        operands.add(std::abs(j[f]));
      }
  };
  for (std::size_t a = 0; a < state.passives.mass.size(); ++a)
    result.passives[state.passives.species[a]].add(state.passives.mass[a]);
  faces(g.xl, g.xr, state.jx, result.jx, result.jx_operands);
  faces(g.yl, g.yr, state.jy, result.jy, result.jy_operands);
  return result;
}
inline void add(Balance &balance, const DoubleDouble &value, double sign = 1) {
  balance.add(sign * value.hi);
  balance.add(sign * value.lo);
}
} // namespace ma::experimental::game_kernel
