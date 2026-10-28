#pragma once
#include "flow_local.hpp"
#include "totals.hpp"
namespace ma::experimental::game_kernel {
struct LayerResult {
  State state;
  std::size_t faces = 0;
  double released = 0;
  // Profile 8: momentum the parcels carried into closed faces.
  double wall_x = 0, wall_y = 0, wall_ops_x = 0, wall_ops_y = 0;
};
// Jacobi equal-volume buoyant exchanges. Miscible constituents share one phase.
// Donor-specific U and mass-carried passives use shared parcels. Profiles 2-7
// retain face J; profile 8 carries the parcels' donor face velocities. Actual
// face-K change and gravity release go to local U.
inline LayerResult
layer(const ma::experimental::MacGrid &source,
      const std::vector<Species> &species, const State &initial,
      const std::vector<Eos> &eos, double dt, double gravity,
      const ma::experimental::mac_kernel::Grid *cached_grid = nullptr,
      ma::experimental::mac_kernel::Pool *executor = nullptr,
      bool carry_momentum = false, RegionAudit *audit = nullptr) {
  namespace k = ma::experimental::mac_kernel;
  k::Grid built;
  if (!cached_grid)
    built = k::Grid(source);
  const auto &g = cached_grid ? *cached_grid : built;
  LayerResult out;
  if (gravity == 0)
    return out;
  // Cell passes run in the grid's fixed row blocks; parcels and face work
  // keep their serial face order.
  const auto blocks = [&](const std::function<void(std::size_t)> &job) {
    if (executor)
      executor->run(g.blocks, job);
    else
      for (std::size_t block = 0; block < g.blocks; ++block)
        job(block);
  };
  const auto block_cells = [&](std::size_t block) {
    return std::pair{g.block_row_begin(block) * g.W,
                     g.block_row_end(block) * g.W};
  };
  const double fraction = std::min(.25, -std::expm1(-dt / 2.));
  struct Pick {
    std::uint64_t phase = 0;
    double mass = 0, volume = 0;
    bool valid = false;
    double density() const { return mass / volume; }
  };
  std::vector<Pick> light(g.N), heavy(g.N);
  std::vector<double> old_mass(g.N), new_mass(g.N);
  const auto phase = [&](std::uint32_t key) {
    return species[key].liquid_group ? std::uint64_t(species[key].liquid_group)
                                     : (std::uint64_t(1) << 32) + key;
  };
  // Keep map key/accumulation order while reusing one small phase buffer.
  blocks([&](std::size_t block) {
  std::vector<Pick> phases;
  phases.reserve(species.size());
  for (auto [i, end] = block_cells(block); i < end; ++i) {
    phases.clear();
    for (std::size_t a = initial.masses.offsets[i];
         a < initial.masses.offsets[i + 1]; ++a) {
      const auto key = initial.masses.species[a];
      const auto m = initial.masses.mass[a];
      old_mass[i] += m;
      if (species[key].gas)
        continue;
      const auto identity = phase(key);
      auto at = std::lower_bound(phases.begin(), phases.end(), identity,
          [](const Pick &p, std::uint64_t id) { return p.phase < id; });
      if (at == phases.end() || at->phase != identity)
        at = phases.insert(at, Pick{identity});
      auto &p = *at;
      p.phase = phase(key);
      p.mass += m;
      p.volume += m / species[key].rho0;
      p.valid = true;
    }
    for (const auto &p : phases) {
      if (!light[i].valid || p.density() < light[i].density())
        light[i] = p;
      if (!heavy[i].valid || p.density() > heavy[i].density())
        heavy[i] = p;
    }
  }
  });
  struct Parcel {
    std::size_t from, to;
    std::vector<std::pair<std::uint32_t, double>> species;
    double mass = 0, energy = 0;
  };
  std::vector<Parcel> parcels;
  for (std::size_t f = 0; f < g.NY; ++f) {
    const auto lower = gravity > 0 ? g.yl[f] : g.yr[f],
               upper = gravity > 0 ? g.yr[f] : g.yl[f];
    if (lower == k::none || upper == k::none || g.closed(true, f))
      continue;
    const auto &l = light[lower];
    const auto &h = heavy[upper];
    if (!l.valid || !h.valid || l.phase == h.phase ||
        !(l.density() < h.density()))
      continue;
    const double exchanged = fraction * std::min(l.volume, h.volume);
    const auto add_parcel = [&](std::size_t from, std::size_t to,
                                const Pick &chosen) {
      Parcel parcel{from, to, {}};
      const double share = exchanged / chosen.volume;
      const auto &state = eos[from];
      for (std::size_t a = initial.masses.offsets[from];
           a < initial.masses.offsets[from + 1]; ++a) {
        const auto key = initial.masses.species[a];
        const auto &s = species[key];
        if (s.gas || phase(key) != chosen.phase)
          continue;
        const double moved = initial.masses.mass[a] * share;
        parcel.species.emplace_back(key, moved);
        parcel.mass += moved;
        const double specific =
            s.offset + s.cv * state.temperature +
            liquid_specific_elastic(s,state.pressure);
        parcel.energy += moved * specific;
      }
      parcels.push_back(std::move(parcel));
    };
    add_parcel(lower, upper, l);
    add_parcel(upper, lower, h);
    ++out.faces;
  }
  if (parcels.empty())
    return out;
  // Each cell's outgoing and then incoming parcels, in parcel order.
  std::vector<std::size_t> out_start(g.N + 1), in_start(g.N + 1), outgoing,
      incoming;
  for (const auto &parcel : parcels) {
    ++out_start[parcel.from + 1];
    ++in_start[parcel.to + 1];
  }
  for (std::size_t i = 0; i < g.N; ++i) {
    out_start[i + 1] += out_start[i];
    in_start[i + 1] += in_start[i];
  }
  outgoing.resize(parcels.size());
  incoming.resize(parcels.size());
  {
    auto next_out = out_start, next_in = in_start;
    for (std::size_t n = 0; n < parcels.size(); ++n) {
      outgoing[next_out[parcels[n].from]++] = n;
      incoming[next_in[parcels[n].to]++] = n;
    }
  }
  const bool has_passives = !initial.passives.mass.empty();
  out.state.energy = initial.energy;
  out.state.jx = initial.jx;
  out.state.jy = initial.jy;
  if (!has_passives)
    out.state.passives = initial.passives;
  // New inventories of each block's cells, packed below in block order.
  struct Rows {
    std::vector<std::uint32_t> species;
    std::vector<double> mass;
    std::vector<std::uint64_t> passive_species;
    std::vector<double> passive_mass;
    std::vector<std::size_t> counts, passive_counts;
  };
  std::vector<Rows> rows(g.blocks);
  blocks([&](std::size_t block) {
    auto &row = rows[block];
    std::map<std::uint32_t, double> mass;
    std::map<std::uint64_t, double> passive;
    for (auto [i, end] = block_cells(block); i < end; ++i) {
      const auto species_before = row.species.size(),
                 passives_before = row.passive_species.size();
      if (out_start[i] == out_start[i + 1] && in_start[i] == in_start[i + 1]) {
        new_mass[i] = old_mass[i];
        row.species.insert(row.species.end(),
                           initial.masses.species.begin() + initial.masses.offsets[i],
                           initial.masses.species.begin() + initial.masses.offsets[i + 1]);
        row.mass.insert(row.mass.end(),
                        initial.masses.mass.begin() + initial.masses.offsets[i],
                        initial.masses.mass.begin() + initial.masses.offsets[i + 1]);
        if (has_passives) {
          row.passive_species.insert(
              row.passive_species.end(),
              initial.passives.species.begin() + initial.passives.offsets[i],
              initial.passives.species.begin() + initial.passives.offsets[i + 1]);
          row.passive_mass.insert(
              row.passive_mass.end(),
              initial.passives.mass.begin() + initial.passives.offsets[i],
              initial.passives.mass.begin() + initial.passives.offsets[i + 1]);
        }
      } else {
        mass.clear();
        passive.clear();
        for (std::size_t a = initial.masses.offsets[i];
             a < initial.masses.offsets[i + 1]; ++a)
          mass[initial.masses.species[a]] = initial.masses.mass[a];
        if (has_passives)
          for (std::size_t a = initial.passives.offsets[i];
               a < initial.passives.offsets[i + 1]; ++a)
            passive[initial.passives.species[a]] = initial.passives.mass[a];
        const auto apply = [&](const Parcel &parcel, double sign) {
          for (const auto &[key, value] : parcel.species)
            mass[key] += sign * value;
          out.state.energy[i] += sign * parcel.energy;
          if (has_passives)
            for (std::size_t a = initial.passives.offsets[parcel.from];
                 a < initial.passives.offsets[parcel.from + 1]; ++a)
              passive[initial.passives.species[a]] +=
                  sign * initial.passives.mass[a] *
                  (parcel.mass / old_mass[parcel.from]);
        };
        for (auto n = out_start[i]; n < out_start[i + 1]; ++n)
          apply(parcels[outgoing[n]], -1);
        for (auto n = in_start[i]; n < in_start[i + 1]; ++n)
          apply(parcels[incoming[n]], 1);
        double total = 0;
        for (const auto &[key, value] : mass) {
          if (value < 0)
            throw std::runtime_error("Private layering mass budget failed");
          if (value > 0) {
            row.species.push_back(key);
            row.mass.push_back(value);
            total += value;
          }
        }
        new_mass[i] = total;
        if (has_passives)
          for (const auto &[key, value] : passive) {
            if (value < 0)
              throw std::runtime_error("Private layering passive budget failed");
            if (value > 0) {
              row.passive_species.push_back(key);
              row.passive_mass.push_back(value);
            }
          }
      }
      row.counts.push_back(row.species.size() - species_before);
      row.passive_counts.push_back(row.passive_species.size() - passives_before);
    }
  });
  const auto pack = [&](auto &into, auto species_of, auto mass_of, auto counts_of) {
    into.offsets.assign(1, 0);
    into.species.clear();
    into.mass.clear();
    for (const auto &row : rows) {
      const auto &keys = species_of(row);
      const auto &values = mass_of(row);
      into.species.insert(into.species.end(), keys.begin(), keys.end());
      into.mass.insert(into.mass.end(), values.begin(), values.end());
      for (const auto count : counts_of(row))
        into.offsets.push_back(into.offsets.back() + count);
    }
  };
  pack(out.state.masses, [](const Rows &r) -> const auto & { return r.species; },
       [](const Rows &r) -> const auto & { return r.mass; },
       [](const Rows &r) -> const auto & { return r.counts; });
  if (has_passives)
    pack(out.state.passives,
         [](const Rows &r) -> const auto & { return r.passive_species; },
         [](const Rows &r) -> const auto & { return r.passive_mass; },
         [](const Rows &r) -> const auto & { return r.passive_counts; });
  DoubleDouble released;
  for (const auto &parcel : parcels) {
    const double drop = parcel.mass * gravity * source.spacing_m *
                        (double(parcel.from / g.W) - double(parcel.to / g.W));
    out.state.energy[parcel.from] += .5 * drop;
    out.state.energy[parcel.to] += .5 * drop;
    released.add(drop);
  }
  out.released = released.value();
  const auto m = [](const std::vector<double> &values, k::Index i) {
    return i == k::none ? 0 : values[i];
  };
  if (carry_momentum) {
    // Profile 8: each parcel keeps the velocities of the faces it leaves.
    // Its half on each face of its source cell moves to the face on the same
    // side of its destination (velocities from the initial momentum and
    // masses); momentum reaching a closed face is a wall impulse. Each
    // face's kinetic energy change, in face order, is local heat.
    auto &jx = out.state.jx;
    auto &jy = out.state.jy;
    const auto fluid = [&](bool x, std::size_t f) {
      const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
      return l != k::none && r != k::none && !g.wall[l] && !g.wall[r] &&
             !g.closed(!x, f);
    };
    const auto velocity = [&](bool x, std::size_t f) {
      const double j = (x ? initial.jx : initial.jy)[f];
      if (j == 0)
        return 0.;
      const double I0 = .5 * (m(old_mass, (x ? g.xl : g.yl)[f]) +
                              m(old_mass, (x ? g.xr : g.yr)[f]));
      if (!(I0 > 0))
        throw std::runtime_error("Private layering J without inertia");
      return j / I0;
    };
    const auto side = [&](std::size_t cell, unsigned which) {
      return which == 0   ? std::pair{true, g.left_face(cell)}
             : which == 1 ? std::pair{true, g.right_face(cell)}
             : which == 2 ? std::pair{false, g.bottom_face(cell)}
                          : std::pair{false, g.top_face(cell)};
    };
    DoubleDouble wall_x, wall_y, ops_x, ops_y;
    std::vector<std::pair<bool, std::size_t>> touched;
    for (const auto &parcel : parcels)
      for (unsigned which = 0; which < 4; ++which) {
        const auto from = side(parcel.from, which),
                   to = side(parcel.to, which);
        touched.push_back(from);
        touched.push_back(to);
        const double amount = .5 * parcel.mass * velocity(from.first, from.second);
        if (amount == 0)
          continue;
        (from.first ? jx : jy)[from.second] -= amount;
        if (fluid(to.first, to.second))
          (to.first ? jx : jy)[to.second] += amount;
        else {
          (to.first ? wall_x : wall_y).add(-amount);
          (to.first ? ops_x : ops_y).add(std::abs(amount));
          if (audit)
            audit->momentum_receipt(parcel.to, to.first, -amount, std::abs(amount));
        }
      }
    std::sort(touched.begin(), touched.end(), [](const auto &a, const auto &b) {
      return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    for (const auto &[x, f] : touched) {
      const double j0 = (x ? initial.jx : initial.jy)[f], j1 = (x ? jx : jy)[f];
      if (j0 == 0 && j1 == 0)
        continue;
      const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
      const double I0 = .5 * (m(old_mass, l) + m(old_mass, r)),
                   I1 = .5 * (m(new_mass, l) + m(new_mass, r));
      if ((j0 != 0 && !(I0 > 0)) || !(I1 > 0))
        throw std::runtime_error("Private layering removed face inertia");
      const double heat = (j0 == 0 ? 0. : .5 * j0 * (j0 / I0)) -
                          (j1 == 0 ? 0. : .5 * j1 * (j1 / I1)),
                   sum = m(new_mass, l) + m(new_mass, r);
      if (l != k::none)
        out.state.energy[l] += heat * m(new_mass, l) / sum;
      if (r != k::none)
        out.state.energy[r] += heat * m(new_mass, r) / sum;
    }
    out.wall_x = wall_x.value();
    out.wall_y = wall_y.value();
    out.wall_ops_x = ops_x.value();
    out.wall_ops_y = ops_y.value();
    return out;
  }
  // Face J keeps its value; where an endpoint's mass changed, the K change
  // goes to U. Such faces are found in row blocks, then booked in face order.
  const auto inertias = [&](bool x, std::size_t f) {
    const auto l = (x ? g.xl : g.yl)[f], r = (x ? g.xr : g.yr)[f];
    return std::pair{.5 * (m(old_mass, l) + m(old_mass, r)),
                     .5 * (m(new_mass, l) + m(new_mass, r))};
  };
  std::vector<std::vector<std::size_t>> moved_x(g.blocks), moved_y(g.blocks);
  blocks([&](std::size_t block) {
    const auto y0 = g.block_row_begin(block);
    for (std::size_t f = y0 * g.XW; f < g.block_row_end(block) * g.XW; ++f)
      if (initial.jx[f] != 0 && inertias(true, f).first != inertias(true, f).second)
        moved_x[block].push_back(f);
    for (std::size_t f = y0 * g.W; f < g.block_yrow_end(block) * g.W; ++f)
      if (initial.jy[f] != 0 && inertias(false, f).first != inertias(false, f).second)
        moved_y[block].push_back(f);
  });
  const auto faces = [&](bool x, const auto &lists) {
    const auto &left = x ? g.xl : g.yl, &right = x ? g.xr : g.yr;
    const auto &j = x ? initial.jx : initial.jy;
    for (const auto &list : lists)
      for (const auto f : list) {
        const auto l = left[f], r = right[f];
        const auto [I0, I1] = inertias(x, f);
        if (!(I1 > 0))
          throw std::runtime_error("Private layering removed face inertia");
        const double heat = .5 * j[f] * (j[f] / I0) - .5 * j[f] * (j[f] / I1),
                     sum = m(new_mass, l) + m(new_mass, r);
        if (l != k::none)
          out.state.energy[l] += heat * m(new_mass, l) / sum;
        if (r != k::none)
          out.state.energy[r] += heat * m(new_mass, r) / sum;
      }
  };
  faces(true, moved_x);
  faces(false, moved_y);
  return out;
}
} // namespace ma::experimental::game_kernel
