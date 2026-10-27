#pragma once
#include "../mac_kernel_detail.hpp"
#include "account.hpp"
#include "eos.hpp"
#include "state.hpp"
namespace ma::experimental::game_kernel {
template <class Key, class Mass>
inline double entropy_selected(std::span<const Species> species,
                               std::size_t count, Key key, Mass mass,
                               const Eos &eos) {
  if (eos.mass == 0)
    return 0;
  const double thermal_log = std::log(eos.temperature / 298.15);
  double total = 0;
  for (std::size_t a = 0; a < count; ++a)
    if (mass(a) > 0) {
      const auto &s = species[key(a)];
      double specific = s.cv * thermal_log + s.entropy_offset;
      if (s.gas) {
        const double partial =
            mass(a) * R / s.molar_mass * eos.temperature / eos.gas_volume;
        specific += R / s.molar_mass * std::log(R * eos.temperature / partial);
      }
      total += mass(a) * specific;
    }
  if (!std::isfinite(total)) {
    // A newly transported trace can be subnormal before the trace sink
    // removes it. R*T/partial may overflow even though m*s is finite.
    // Recompute only that exceptional diagnostic with logarithms of its
    // positive factors: R*T/partial = molar_mass*gas_volume/mass.
    // Keep the ordinary arithmetic (and physical evolution) unchanged.
    total = 0;
    const double thermal = std::isfinite(thermal_log)
                               ? thermal_log
                               : std::log(eos.temperature) - std::log(298.15);
    for (std::size_t a = 0; a < count; ++a)
      if (mass(a) > 0) {
        const auto &s = species[key(a)];
        double specific = s.cv * thermal + s.entropy_offset;
        if (s.gas)
          specific += R / s.molar_mass *
                      (std::log(s.molar_mass) + std::log(eos.gas_volume) -
                       std::log(mass(a)));
        total += mass(a) * specific;
      }
  }
  if (!std::isfinite(total))
    throw std::runtime_error("Private entropy diagnostic is nonfinite");
  return total;
}
inline double cell_entropy(std::span<const Species> species, const State &state,
                           std::size_t cell, double volume) {
  const auto begin = state.masses.offsets[cell],
             count = state.masses.offsets[cell + 1] - begin;
  const auto key = [&](std::size_t a) {
    return state.masses.species[begin + a];
  };
  const auto mass = [&](std::size_t a) { return state.masses.mass[begin + a]; };
  const auto eos =
      evaluate_selected(species, count, key, mass, state.energy[cell], volume);
  return entropy_selected(species, count, key, mass, eos);
}
inline DoubleDouble total_entropy(const ma::experimental::mac_kernel::Grid &g,
                                  std::span<const Species> species,
                                  const State &state,
                                  const std::vector<Eos> &eos,
                                  ma::experimental::mac_kernel::Pool &pool) {
  std::vector<DoubleDouble> parts(g.blocks);
  std::vector<std::exception_ptr> errors(g.blocks);
  pool.run(g.blocks, [&](std::size_t block) {
    try {
      for (std::size_t i = g.block_row_begin(block) * g.W;
           i < g.block_row_end(block) * g.W; ++i)
        if (!g.wall[i]) {
          const auto begin = state.masses.offsets[i],
                     count = state.masses.offsets[i + 1] - begin;
          parts[block].add(entropy_selected(
              species, count,
              [&](std::size_t a) { return state.masses.species[begin + a]; },
              [&](std::size_t a) { return state.masses.mass[begin + a]; },
              eos[i]));
        }
    } catch (...) {
      errors[block] = std::current_exception();
    }
  });
  DoubleDouble result;
  for (std::size_t block = 0; block < g.blocks; ++block) {
    if (errors[block])
      std::rethrow_exception(errors[block]);
    result.add(parts[block].hi);
    result.add(parts[block].lo);
  }
  return result;
}
} // namespace ma::experimental::game_kernel
