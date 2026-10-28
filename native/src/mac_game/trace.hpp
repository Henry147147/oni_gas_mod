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
// Cell and face scans and the inventory rebuild run in the grid's fixed row
// blocks; every booking of a cell or face that loses traces runs in one
// serial pass in the original cell and face order.
inline TraceResult
trace_sink(const ma::experimental::MacGrid &source,
           const std::vector<Species> &species, const State &initial,
           double threshold, double gravity, RegionAudit *regions = nullptr,
           const ma::experimental::mac_kernel::Grid *cached_grid = nullptr,
           ma::experimental::mac_kernel::Pool *executor = nullptr,
           std::span<const std::uint8_t> eligible = {}) {
  namespace k = ma::experimental::mac_kernel;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  if (!eligible.empty() && eligible.size() != g.N)
    throw std::invalid_argument("Trace activity coverage differs");
  const auto blocks = [&](const std::function<void(std::size_t)> &job) {
    if (executor)
      executor->run(g.blocks, job);
    else
      for (std::size_t block = 0; block < g.blocks; ++block)
        job(block);
  };
  const auto cells = [&](std::size_t block) {
    return std::pair{g.block_row_begin(block) * g.W,
                     g.block_row_end(block) * g.W};
  };
  TraceResult result;
  result.state.energy = initial.energy;
  result.state.jx = initial.jx;
  result.state.jy = initial.jy;
  result.state.passives = initial.passives;
  result.mass.resize(species.size());
  std::vector<DoubleDouble> sunk(species.size());
  std::vector<double> old_mass(g.N), new_mass(g.N);
  // Per block: kept constituents (then their first output index) and the
  // cells that lose a trace.
  std::vector<std::size_t> kept(g.blocks + 1);
  std::vector<std::vector<std::size_t>> changed(g.blocks);
  blocks([&](std::size_t block) {
    std::size_t count = 0;
    for (auto [i, end] = cells(block); i < end; ++i) {
      bool trace = false;
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a) {
        const double m = initial.masses.mass[a];
        old_mass[i] += m;
        if (m < threshold && (eligible.empty() || eligible[i])) {
          trace = true;
          continue;
        }
        new_mass[i] += m;
        ++count;
      }
      if (trace)
        changed[block].push_back(i);
    }
    kept[block + 1] = count;
  });
  for (std::size_t block = 0; block < g.blocks; ++block)
    kept[block + 1] += kept[block];
  auto &masses = result.state.masses;
  masses.offsets.resize(g.N + 1);
  masses.species.resize(kept.back());
  masses.mass.resize(kept.back());
  blocks([&](std::size_t block) {
    auto at = kept[block];
    for (auto [i, end] = cells(block); i < end; ++i) {
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a)
        if (initial.masses.mass[a] >= threshold || (!eligible.empty() && !eligible[i])) {
          masses.species[at] = initial.masses.species[a];
          masses.mass[at] = initial.masses.mass[a];
          ++at;
        }
      masses.offsets[i + 1] = at;
    }
  });
  masses.offsets[0] = 0;
  DoubleDouble internal, potential, kinetic, jx, jy, entropy;
  for (const auto &list : changed)
    for (const auto i : list) {
      double cv0 = 0, cv1 = 0, off0 = 0, off1 = 0;
      for (std::size_t a = initial.masses.offsets[i];
           a < initial.masses.offsets[i + 1]; ++a) {
        const auto key = initial.masses.species[a];
        const double m = initial.masses.mass[a];
        const auto &s = species[key];
        cv0 += m * s.cv;
        off0 += m * s.offset;
        if (m < threshold) {
          if (regions)
            regions->mass(i, key, -m);
          sunk[key].add(m);
          ++result.constituents;
          continue;
        }
        cv1 += m * s.cv;
        off1 += m * s.offset;
      }
      ++result.cells;
      result.state.energy[i] =
          new_mass[i] == 0 ? 0
                           : off1 + (initial.energy[i] - off0) * (cv1 / cv0);
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
        const double share =
            new_mass[i] == 0 ? 0 : quantity * (new_mass[i] / old_mass[i]);
        if (share > 0) {
          result.state.passives.species.push_back(identity);
          result.state.passives.mass.push_back(share);
        }
        passive_sink[identity].add(quantity);
        passive_sink[identity].add(-share);
        if (regions && share != quantity) {
          regions->passive(i, identity, -quantity);
          regions->passive(i, identity, share);
        }
      }
      result.state.passives.offsets.push_back(
          result.state.passives.mass.size());
    }
    for (const auto &[identity, value] : passive_sink)
      if (value.value() != 0)
        result.passives.emplace_back(identity, value.value());
  }
  // Face J follows its inertia only where an endpoint lost a trace.
  const auto m = [](const std::vector<double> &mass, k::Index i) {
    return i == k::none ? 0 : mass[i];
  };
  const auto inertias = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    return std::pair{.5 * (m(old_mass, l) + m(old_mass, r)),
                     .5 * (m(new_mass, l) + m(new_mass, r))};
  };
  std::vector<std::vector<std::size_t>> moved_x(g.blocks), moved_y(g.blocks);
  blocks([&](std::size_t block) {
    const auto y0 = g.block_row_begin(block);
    for (std::size_t f = y0 * g.XW; f < g.block_row_end(block) * g.XW; ++f)
      if (initial.jx[f] != 0) {
        const auto [I0, I1] = inertias(true, f);
        if (I0 != I1)
          moved_x[block].push_back(f);
      }
    for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f)
      if (initial.jy[f] != 0) {
        const auto [I0, I1] = inertias(false, f);
        if (I0 != I1)
          moved_y[block].push_back(f);
      }
  });
  const auto faces = [&](bool x, const auto &lists, const auto &old,
                         auto &next, auto &carried) {
    for (const auto &list : lists)
      for (const auto f : list) {
        const auto [I0, I1] = inertias(x, f);
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
  faces(true, moved_x, initial.jx, result.state.jx, jx);
  faces(false, moved_y, initial.jy, result.state.jy, jy);
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
