#pragma once
#include "entropy.hpp"
#include "flow_local.hpp"
namespace ma::experimental::game_kernel {
struct TraceResult {
  State state;
  std::vector<double> mass;
  std::vector<std::pair<std::uint64_t, double>> passives;
  double entropy = 0, internal = 0, kinetic = 0, potential = 0, jx = 0, jy = 0;
  std::size_t constituents = 0, cells = 0;
};
inline TraceResult
trace_sink(const ma::experimental::MacGrid &source,
           const std::vector<Species> &species, const State &initial,
           double threshold, double gravity, RegionAudit *regions = nullptr,
           const ma::experimental::mac_kernel::Grid *cached_grid = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  TraceResult result;
  result.state = initial;
  result.state.masses = {};
  result.state.masses.offsets.push_back(0);
  result.mass.resize(species.size());
  std::vector<DoubleDouble> sunk(species.size());
  std::vector<double> old_mass(g.N), new_mass(g.N);
  DoubleDouble internal, potential, kinetic, jx, jy, entropy;
  for (std::size_t i = 0; i < g.N; ++i) {
    double cv0 = 0, cv1 = 0, off0 = 0, off1 = 0;
    bool changed = false;
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const double m = initial.masses.mass[a];
      const auto &s = species[key];
      old_mass[i] += m;
      cv0 += m * s.cv;
      off0 += m * s.offset;
      if (m < threshold) {
        if (regions)
          regions->mass(i, key, -m);
        sunk[key].add(m);
        changed = true;
        ++result.constituents;
        continue;
      }
      result.state.masses.species.push_back(key);
      result.state.masses.mass.push_back(m);
      new_mass[i] += m;
      cv1 += m * s.cv;
      off1 += m * s.offset;
    }
    result.state.masses.offsets.push_back(result.state.masses.mass.size());
    if (!changed)
      continue;
    ++result.cells;
    result.state.energy[i] =
        new_mass[i] == 0 ? 0 : off1 + (initial.energy[i] - off0) * (cv1 / cv0);
    internal.add(initial.energy[i]);
    internal.add(-result.state.energy[i]);
    entropy.add(cell_entropy(species, initial, i, g.V));
    entropy.add(-cell_entropy(species, result.state, i, g.V));
    const double factor =
        gravity * (static_cast<double>(i / g.W) + .5) * source.spacing_m;
    potential.add(old_mass[i] * factor);
    potential.add(-new_mass[i] * factor);
    if (regions) {
      regions->energy(i, -initial.energy[i]);
      regions->energy(i, result.state.energy[i]);
      regions->energy(i, -old_mass[i] * factor);
      regions->energy(i, new_mass[i] * factor);
    }
  }
  if (!initial.passives.mass.empty()) {
    initial.passives.validate(g.N);
    result.state.passives = {};
    result.state.passives.offsets.push_back(0);
    std::map<std::uint64_t, DoubleDouble> passive_sink;
    for (std::size_t i = 0; i < g.N; ++i) {
      for (std::size_t a = initial.passives.offsets[i];
           a < initial.passives.offsets[i + 1]; ++a) {
        const auto identity = initial.passives.species[a];
        const double quantity = initial.passives.mass[a];
        if (!(old_mass[i] > 0))
          throw std::runtime_error("Private trace passive without material");
        const double kept =
            new_mass[i] == 0 ? 0 : quantity * (new_mass[i] / old_mass[i]);
        if (kept > 0) {
          result.state.passives.species.push_back(identity);
          result.state.passives.mass.push_back(kept);
        }
        passive_sink[identity].add(quantity);
        passive_sink[identity].add(-kept);
        if (regions && kept != quantity) {
          regions->passive(i, identity, -quantity);
          regions->passive(i, identity, kept);
        }
      }
      result.state.passives.offsets.push_back(
          result.state.passives.mass.size());
    }
    for (const auto &[identity, value] : passive_sink)
      if (value.value() != 0)
        result.passives.emplace_back(identity, value.value());
  }
  const auto faces = [&](bool x, const auto &left, const auto &right,
                         const auto &old, auto &next, auto &carried) {
    const auto m = [](const auto &mass, k::Index i) {
      return i == k::none ? 0 : mass[i];
    };
    for (std::size_t f = 0; f < old.size(); ++f) {
      const double I0 = .5 * (m(old_mass, left[f]) + m(old_mass, right[f])),
                   I1 = .5 * (m(new_mass, left[f]) + m(new_mass, right[f]));
      if (old[f] == 0 || I0 == I1)
        continue;
      if (!(I0 > 0))
        throw std::runtime_error("Private trace J without inertia");
      next[f] = I1 == 0 ? 0 : old[f] * (I1 / I0);
      carried.add(old[f]);
      carried.add(-next[f]);
      kinetic.add(.5 * old[f] * (old[f] / I0));
      if (next[f] != 0)
        kinetic.add(-.5 * next[f] * (next[f] / I1));
      if (regions) {
        const auto i = regions->face_cell(x, f);
        regions->momentum(i, x, -old[f]);
        regions->momentum(i, x, next[f]);
        regions->energy(i, -.5 * old[f] * (old[f] / I0));
        if (next[f] != 0)
          regions->energy(i, .5 * next[f] * (next[f] / I1));
      }
    }
  };
  faces(true, g.xl, g.xr, initial.jx, result.state.jx, jx);
  faces(false, g.yl, g.yr, initial.jy, result.state.jy, jy);
  for (std::size_t key = 0; key < species.size(); ++key)
    result.mass[key] = sunk[key].value();
  result.entropy = entropy.value();
  result.internal = internal.value();
  result.kinetic = kinetic.value();
  result.potential = potential.value();
  result.jx = jx.value();
  result.jy = jy.value();
  return result;
}
} // namespace ma::experimental::game_kernel
