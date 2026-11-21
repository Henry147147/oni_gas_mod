#pragma once
#include "../mac_kernel_detail.hpp"
#include "eos.hpp"
#include "sparse.hpp"
#include <map>
namespace ma::experimental::game_kernel {
inline constexpr std::uint64_t gas_group =
    std::numeric_limits<std::uint64_t>::max();
inline std::uint64_t phase_group(const std::vector<Species> &species,
                                 std::uint32_t key) {
  const auto &s = species[key];
  return s.gas            ? gas_group
         : s.liquid_group ? std::uint64_t(s.liquid_group)
                          : (std::uint64_t(1) << 32) + key;
}
struct GeometricPhase {
  std::uint64_t identity = 0;
  double volume = 0, mass = 0, energy = 0;
};
struct Geometry {
  bool liquid = false;
  double alpha = 0;
  std::vector<GeometricPhase> phases;
};
// Writes the geometry of an inventory into `result`, reusing the capacity of
// its phase list.
template <class Inventory>
inline void geometry_into(Geometry &result, const std::vector<Species> &species,
                          const Inventory &inventory, double energy,
                          double volume, double gravity,
                          const Eos *known = nullptr) {
  result.liquid = false;
  result.alpha = 0;
  result.phases.clear();
  for (const auto &[key, mass] : inventory)
    result.liquid = result.liquid || !species[key].gas;
  if (!result.liquid)
    return;
  // Interface correction already has the EOS for this exact inventory/U.
  const auto eos = known ? *known : evaluate_selected(
      species, inventory.size(),
      [&](std::size_t a) { return inventory[a].first; },
      [&](std::size_t a) { return inventory[a].second; }, energy, volume);
  result.alpha = eos.liquid_volume / volume;
  // Phases in ascending identity, each accumulated in inventory order: the
  // order of a map keyed by identity, without its node allocations.
  auto &phases = result.phases;
  for (const auto &[key, mass] : inventory) {
    const auto &s = species[key];
    const auto group = phase_group(species, key);
    auto at = std::lower_bound(
        phases.begin(), phases.end(), group,
        [](const GeometricPhase &p, std::uint64_t id) { return p.identity < id; });
    if (at == phases.end() || at->identity != group)
      at = phases.insert(at, GeometricPhase{group});
    auto &phase = *at;
    phase.mass += mass;
    const double partial_volume =
        s.gas ? (eos.nR > 0 && eos.pressure > 0
                     ? mass * R / s.molar_mass * eos.temperature / eos.pressure
                     : 0)
              : mass / s.rho0 * liquid_volume_fraction(s,eos.pressure);
    phase.volume += partial_volume;
    phase.energy +=
        mass *
        (s.offset + s.cv * eos.temperature +
         (s.gas ? 0
                : liquid_specific_elastic(s,eos.pressure)));
  }
  std::stable_sort(result.phases.begin(), result.phases.end(),
                   [&](const auto &a, const auto &b) {
                     if (a.identity == gas_group)
                       return false;
                     if (b.identity == gas_group)
                       return true;
                     return gravity >= 0
                                ? a.mass / a.volume > b.mass / b.volume
                                : a.mass / a.volume < b.mass / b.volume;
                   });
}
template <class Inventory>
inline Geometry geometry(const std::vector<Species> &species,
                         const Inventory &inventory, double energy,
                         double volume, double gravity, const Eos *known = nullptr) {
  Geometry result;
  geometry_into(result, species, inventory, energy, volume, gravity, known);
  return result;
}
// Height differences over three-cell columns choose the dominant interface
// normal. The corresponding height slab is integrated analytically; tangent
// sweeps use the phase area fractions. This is a private HF prototype, not
// PLIC.
inline std::pair<bool, bool>
height_normal(const ma::experimental::mac_kernel::Grid &g,
              const std::vector<Geometry> &geom, std::size_t i) {
  const auto x = static_cast<long>(i % g.W), y = static_cast<long>(i / g.W);
  const double center = geom[i].alpha;
  const auto alpha = [&](long cx, long cy) {
    if (cx < 0 || cy < 0 || cx >= static_cast<long>(g.W) ||
        cy >= static_cast<long>(g.H))
      return center;
    const auto index =
        static_cast<std::size_t>(cx + cy * static_cast<long>(g.W));
    return g.wall[index] ? center : geom[index].alpha;
  };
  double gx = 0, gy = 0;
  for (long a = -1; a <= 1; ++a) {
    gx += .5 * (alpha(x + 1, y + a) - alpha(x - 1, y + a));
    gy += .5 * (alpha(x + a, y + 1) - alpha(x + a, y - 1));
  }
  const bool x_normal = std::abs(gx) > std::abs(gy);
  const bool liquid_low = (x_normal ? gx : gy) <= 0;
  return {x_normal, liquid_low};
}
inline std::vector<double> height_fractions(const Geometry &geometry,
                                            double volume, bool x_face,
                                            bool positive, double courant,
                                            std::pair<bool, bool> normal) {
  std::vector<double> fractions(geometry.phases.size());
  if (!(courant >= 0 && courant <= 1))
    throw std::runtime_error("Private HF face sweep exceeds one cell");
  const bool normal_sweep = x_face == normal.first;
  if (!normal_sweep || geometry.alpha >= 1) {
    for (std::size_t phase = 0; phase < fractions.size(); ++phase)
      fractions[phase] = courant;
    return fractions;
  }
  const double slab_begin = positive ? 1 - courant : 0,
               slab_end = positive ? 1 : courant;
  const auto overlap = [&](double begin, double end) {
    return std::max(0., std::min(end, slab_end) - std::max(begin, slab_begin));
  };
  const double liquid_begin = normal.second ? 0 : 1 - geometry.alpha,
               liquid_end = liquid_begin + geometry.alpha;
  if (x_face) {
    // A vertical interface has the same phase proportions across its width.
    const double liquid_sweep = overlap(liquid_begin, liquid_end) * volume;
    for (std::size_t p = 0; p < fractions.size(); ++p)
      if (geometry.phases[p].identity != gas_group)
        fractions[p] =
            geometry.alpha > 0 ? liquid_sweep / (geometry.alpha * volume) : 0;
  } else {
    double position = liquid_begin;
    for (std::size_t p = 0; p < fractions.size(); ++p)
      if (geometry.phases[p].identity != gas_group) {
        const double end = position + geometry.phases[p].volume / volume;
        fractions[p] =
            geometry.phases[p].volume > 0
                ? overlap(position, end) * volume / geometry.phases[p].volume
                : 0;
        position = end;
      }
  }
  for (std::size_t p = 0; p < fractions.size(); ++p)
    if (geometry.phases[p].identity == gas_group) {
      const double begin = normal.second ? liquid_end : 0,
                   end = normal.second ? 1 : liquid_begin;
      fractions[p] =
          geometry.phases[p].volume > 0
              ? overlap(begin, end) * volume / geometry.phases[p].volume
              : 0;
    }
  return fractions;
}
} // namespace ma::experimental::game_kernel
