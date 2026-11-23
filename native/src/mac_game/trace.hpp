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
// Capacity trace_sink reuses across calls (Tick keeps one); every entry it
// reads is written first. `spare` lends the buffers of a state the caller
// no longer needs to the next result.
struct TraceScratch {
  std::vector<double> old_mass, new_mass;
  std::vector<std::size_t> kept, passive_kept;
  std::vector<std::vector<std::size_t>> changed, moved_x, moved_y;
  // The changed cells in cell order, and what each cell's booking needs:
  // its new energy and its entropy before and after (or the error reading
  // them threw).
  std::vector<std::size_t> cells;
  std::vector<double> energy, entropy_before, entropy_after;
  std::vector<std::exception_ptr> errors;
  State spare;
};
// Cell and face scans, the inventory rebuild and each changed cell's new
// energy and entropies run in the grid's fixed row blocks (or fixed chunks
// of the changed cells); every booking of a cell or face that loses traces
// runs in one serial pass in the original cell and face order, and a cell
// whose entropy could not be read throws there, after its trace bookings.
inline TraceResult
trace_sink(const ma::experimental::MacGrid &source,
           const std::vector<Species> &species, const State &initial,
           double threshold, double gravity, RegionAudit *regions = nullptr,
           const ma::experimental::mac_kernel::Grid *cached_grid = nullptr,
           ma::experimental::mac_kernel::Pool *executor = nullptr,
           std::span<const std::uint8_t> eligible = {},
           TraceScratch *reuse = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  if (!eligible.empty() && eligible.size() != g.N)
    throw std::invalid_argument("Trace activity coverage differs");
  TraceScratch local_scratch;
  auto &scratch = reuse ? *reuse : local_scratch;
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
  result.state = std::move(scratch.spare);
  scratch.spare = {};
  // Energy and face momenta are copied by row block below when they cover
  // the grid.
  const bool rows = initial.energy.size() == g.N && initial.jx.size() == g.NX &&
                    initial.jy.size() == g.NY;
  if (rows) {
    result.state.energy.resize(g.N);
    result.state.jx.resize(g.NX);
    result.state.jy.resize(g.NY);
  } else {
    result.state.energy = initial.energy;
    result.state.jx = initial.jx;
    result.state.jy = initial.jy;
  }
  if (initial.passives.mass.empty())
    result.state.passives = initial.passives;
  result.mass.resize(species.size());
  std::vector<DoubleDouble> sunk(species.size());
  auto &old_mass = scratch.old_mass, &new_mass = scratch.new_mass;
  old_mass.resize(g.N);
  new_mass.resize(g.N);
  // Per block: kept constituents (then their first output index) and the
  // cells that lose a trace.
  auto &kept = scratch.kept;
  kept.assign(g.blocks + 1, 0);
  auto &changed = scratch.changed;
  changed.resize(g.blocks);
  blocks([&](std::size_t block) {
    std::size_t count = 0;
    changed[block].clear();
    for (auto [i, end] = cells(block); i < end; ++i) {
      bool trace = false;
      old_mass[i] = 0;
      new_mass[i] = 0;
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
    if (!rows)
      return;
    const auto [first, last] = cells(block);
    std::copy(initial.energy.begin() + static_cast<std::ptrdiff_t>(first),
              initial.energy.begin() + static_cast<std::ptrdiff_t>(last),
              result.state.energy.begin() + static_cast<std::ptrdiff_t>(first));
    const auto y0 = g.block_row_begin(block);
    std::copy(initial.jx.begin() + static_cast<std::ptrdiff_t>(y0 * g.XW),
              initial.jx.begin() + static_cast<std::ptrdiff_t>(g.block_row_end(block) * g.XW),
              result.state.jx.begin() + static_cast<std::ptrdiff_t>(y0 * g.XW));
    std::copy(initial.jy.begin() + static_cast<std::ptrdiff_t>(y0 * g.W),
              initial.jy.begin() + static_cast<std::ptrdiff_t>(g.block_yrow_end(block) * g.W),
              result.state.jy.begin() + static_cast<std::ptrdiff_t>(y0 * g.W));
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
  // Each changed cell's new energy and entropies, in fixed chunks.
  auto &list = scratch.cells;
  list.clear();
  for (const auto &block : changed)
    list.insert(list.end(), block.begin(), block.end());
  scratch.energy.resize(list.size());
  scratch.entropy_before.resize(list.size());
  scratch.entropy_after.resize(list.size());
  scratch.errors.assign(list.size(), nullptr);
  {
    constexpr std::size_t chunk = 64;
    const auto run = [&](std::size_t c) {
      for (auto n = c * chunk; n < std::min(list.size(), (c + 1) * chunk); ++n) {
        const auto i = list[n];
        double cv0 = 0, cv1 = 0, off0 = 0, off1 = 0;
        for (std::size_t a = initial.masses.offsets[i];
             a < initial.masses.offsets[i + 1]; ++a) {
          const double m = initial.masses.mass[a];
          const auto &s = species[initial.masses.species[a]];
          cv0 += m * s.cv;
          off0 += m * s.offset;
          if (m < threshold)
            continue;
          cv1 += m * s.cv;
          off1 += m * s.offset;
        }
        scratch.energy[n] = result.state.energy[i] =
            new_mass[i] == 0 ? 0
                             : off1 + (initial.energy[i] - off0) * (cv1 / cv0);
        try {
          scratch.entropy_before[n] = cell_entropy(species, initial, i, g.V);
          scratch.entropy_after[n] = cell_entropy(species, result.state, i, g.V);
        } catch (...) {
          scratch.errors[n] = std::current_exception();
        }
      }
    };
    const auto count = (list.size() + chunk - 1) / chunk;
    if (executor && count > 1)
      executor->run(count, run);
    else
      for (std::size_t c = 0; c < count; ++c)
        run(c);
  }
  DoubleDouble internal, potential, kinetic, jx, jy, entropy;
  for (std::size_t n = 0; n < list.size(); ++n) {
    const auto i = list[n];
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const double m = initial.masses.mass[a];
      if (m < threshold) {
        if (regions)
          regions->mass(i, key, -m);
        sunk[key].add(m);
        ++result.constituents;
      }
    }
    if (scratch.errors[n])
      std::rethrow_exception(scratch.errors[n]);
    ++result.cells;
    internal.add(initial.energy[i]);
    internal.add(-scratch.energy[n]);
    entropy.add(scratch.entropy_before[n]);
    entropy.add(-scratch.entropy_after[n]);
    const double factor =
        gravity * (static_cast<double>(i / g.W) + .5) * source.spacing_m;
    potential.add(old_mass[i] * factor);
    potential.add(-new_mass[i] * factor);
    if (regions) {
      regions->energy(i, -initial.energy[i]);
      regions->energy(i, scratch.energy[n]);
      regions->energy(i, -old_mass[i] * factor);
      regions->energy(i, new_mass[i] * factor);
    }
  }
  if (!initial.passives.mass.empty()) {
    validate_parts(initial.passives, g.N, g.blocks,
                   [&](std::size_t count, const std::function<void(std::size_t)> &job) {
                     if (executor)
                       executor->run(count, job);
                     else
                       for (std::size_t part = 0; part < count; ++part)
                         job(part);
                   });
    // Each kept passive's share, in row blocks; then the sink account and
    // the region bookings in cell order.
    const auto share_of = [&](std::size_t i, double quantity) {
      return new_mass[i] == 0 ? 0 : quantity * (new_mass[i] / old_mass[i]);
    };
    auto &passive_kept = scratch.passive_kept;
    passive_kept.assign(g.blocks + 1, 0);
    blocks([&](std::size_t block) {
      std::size_t count = 0;
      for (auto [i, end] = cells(block); i < end; ++i)
        for (std::size_t a = initial.passives.offsets[i];
             a < initial.passives.offsets[i + 1]; ++a)
          count += share_of(i, initial.passives.mass[a]) > 0;
      passive_kept[block + 1] = count;
    });
    for (std::size_t block = 0; block < g.blocks; ++block)
      passive_kept[block + 1] += passive_kept[block];
    auto &passives = result.state.passives;
    passives.offsets.resize(g.N + 1);
    passives.species.resize(passive_kept.back());
    passives.mass.resize(passive_kept.back());
    passives.offsets[0] = 0;
    blocks([&](std::size_t block) {
      auto at = passive_kept[block];
      for (auto [i, end] = cells(block); i < end; ++i) {
        for (std::size_t a = initial.passives.offsets[i];
             a < initial.passives.offsets[i + 1]; ++a) {
          const double share = share_of(i, initial.passives.mass[a]);
          if (share > 0) {
            passives.species[at] = initial.passives.species[a];
            passives.mass[at] = share;
            ++at;
          }
        }
        passives.offsets[i + 1] = at;
      }
    });
    std::map<std::uint64_t, DoubleDouble> passive_sink;
    for (std::size_t i = 0; i < g.N; ++i) {
      for (std::size_t a = initial.passives.offsets[i];
           a < initial.passives.offsets[i + 1]; ++a) {
        const auto identity = initial.passives.species[a];
        const double quantity = initial.passives.mass[a];
        if (!(old_mass[i] > 0))
          throw std::runtime_error("Private trace passive without material");
        const double share = share_of(i, quantity);
        passive_sink[identity].add(quantity);
        passive_sink[identity].add(-share);
        if (regions && share != quantity) {
          regions->passive(i, identity, -quantity);
          regions->passive(i, identity, share);
        }
      }
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
  auto &moved_x = scratch.moved_x, &moved_y = scratch.moved_y;
  moved_x.resize(g.blocks);
  moved_y.resize(g.blocks);
  blocks([&](std::size_t block) {
    moved_x[block].clear();
    moved_y[block].clear();
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
    for (const auto &face_list : lists)
      for (const auto f : face_list) {
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
