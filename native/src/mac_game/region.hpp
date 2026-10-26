#pragma once
#include "../mac_kernel_detail.hpp"
#include "account.hpp"
#include "state.hpp"
#include <map>
namespace ma::experimental::game_kernel {
struct Topology {
  std::vector<std::size_t> labels;
  std::size_t count = 0;
  Topology() = default;
  explicit Topology(const ma::experimental::mac_kernel::Grid &g)
      : labels(g.N, g.N) {
    namespace k = ma::experimental::mac_kernel;
    std::vector<std::size_t> pending;
    for (std::size_t seed = 0; seed < g.N; ++seed)
      if (!g.wall[seed] && labels[seed] == g.N) {
        pending.clear();
        pending.push_back(seed);
        labels[seed] = count;
        for (std::size_t at = 0; at < pending.size(); ++at) {
          const auto i = pending[at];
          const auto visit = [&](bool x, std::size_t f) {
            const auto &l = x ? g.xl : g.yl, &r = x ? g.xr : g.yr;
            if (g.closed(!x, f) || l[f] == k::none || r[f] == k::none)
              return;
            const auto other = l[f] == i ? r[f] : l[f];
            if (labels[other] == g.N) {
              labels[other] = count;
              pending.push_back(other);
            }
          };
          visit(true, g.left_face(i));
          visit(true, g.right_face(i));
          visit(false, g.bottom_face(i));
          visit(false, g.top_face(i));
        }
        ++count;
      }
  }
};
// The region accounts of one row block of a parallel pass: few regions and
// few species each, merged into the audit in block order.
class RegionPartial {
  struct Entry {
    std::size_t region = 0;
    std::vector<std::pair<std::uint32_t, Balance>> species;
    Balance energy, jx, jy;
    std::vector<std::pair<std::uint64_t, Balance>> passives;
  };
  const Topology *topology = nullptr;
  std::vector<Entry> entries;
  std::size_t last = 0;
  Entry &of(std::size_t cell) {
    const auto region = topology->labels.at(cell);
    if (last < entries.size() && entries[last].region == region)
      return entries[last];
    for (last = 0; last < entries.size(); ++last)
      if (entries[last].region == region)
        return entries[last];
    auto &entry = entries.emplace_back();
    entry.region = region;
    return entry;
  }
  template <class Key>
  static Balance &find(std::vector<std::pair<Key, Balance>> &list, Key key) {
    for (auto &[k, b] : list)
      if (k == key)
        return b;
    return list.emplace_back(key, Balance{}).second;
  }
  friend class RegionAudit;

public:
  explicit RegionPartial(const Topology &t) : topology(&t) {}
  void mass(std::size_t cell, std::uint32_t key, double value) {
    find(of(cell).species, key).add(value);
  }
  void energy(std::size_t cell, double value) { of(cell).energy.add(value); }
  void passive(std::size_t cell, std::uint64_t key, double value) {
    find(of(cell).passives, key).add(value);
  }
  void momentum(std::size_t cell, bool x, double value) {
    auto &e = of(cell);
    (x ? e.jx : e.jy).add(value);
  }
};
// One double-double audit per connected fluid region and tick. It records
// physical sources/sinks, never a rounding-error or per-write repair ledger.
// Serial bookings add directly; the whole-state passes (captures and phase
// conversion) sum fixed row blocks in parallel and merge them in block order.
class RegionAudit {
  const ma::experimental::mac_kernel::Grid &grid;
  const Topology &topology;
  std::size_t species_count;
  std::vector<Balance> species_, energy_, jx_, jy_; // species_: region * K + key
  std::vector<std::map<std::uint64_t, Balance>> passives_;
  static void merge(Balance &into, const Balance &part) {
    into.residual.add(part.residual.hi);
    into.residual.add(part.residual.lo);
    into.operands.add(part.operands.hi);
    into.operands.add(part.operands.lo);
  }
  static void merge(DoubleDouble &into, const DoubleDouble &part) {
    into.add(part.hi);
    into.add(part.lo);
  }
  std::size_t region(std::size_t cell) const {
    return topology.labels.at(cell);
  }
  Balance &species(std::size_t cell, std::size_t key) {
    if (key >= species_count)
      throw std::out_of_range("Private region species index");
    return species_[region(cell) * species_count + key];
  }
  void merge(const RegionPartial &part) {
    for (const auto &e : part.entries) {
      for (const auto &[key, b] : e.species) {
        if (key >= species_count)
          throw std::out_of_range("Private region species index");
        merge(species_[e.region * species_count + key], b);
      }
      merge(energy_[e.region], e.energy);
      merge(jx_[e.region], e.jx);
      merge(jy_[e.region], e.jy);
      for (const auto &[key, b] : e.passives)
        merge(passives_[e.region][key], b);
    }
  }
  // Runs job(block) over the grid's fixed row blocks, on the pool if any.
  void blocks(ma::experimental::mac_kernel::Pool *pool,
              const std::function<void(std::size_t)> &job) const {
    if (pool)
      pool->run(grid.blocks, job);
    else
      for (std::size_t block = 0; block < grid.blocks; ++block)
        job(block);
  }

public:
  RegionAudit(const ma::experimental::mac_kernel::Grid &g, const Topology &t,
              std::size_t species)
      : grid(g), topology(t), species_count(species),
        species_(t.count * species), energy_(t.count), jx_(t.count),
        jy_(t.count), passives_(t.count) {}
  std::size_t count() const { return energy_.size(); }
  void mass(std::size_t cell, std::size_t key, double value) {
    species(cell, key).add(value);
  }
  void energy(std::size_t cell, double value) {
    energy_[region(cell)].add(value);
  }
  void passive(std::size_t cell, std::uint64_t key, double value) {
    passives_[region(cell)][key].add(value);
  }
  void momentum(std::size_t cell, bool x, double value) {
    (x ? jx_ : jy_)[region(cell)].add(value);
  }
  void momentum_receipt(std::size_t cell, bool x, double value,
                        double operands) {
    auto &b = (x ? jx_ : jy_)[region(cell)];
    b.residual.add(value);
    b.operands.add(operands);
  }
  std::size_t face_cell(bool x, std::size_t face) const {
    namespace k = ma::experimental::mac_kernel;
    const auto &l = x ? grid.xl : grid.yl, &r = x ? grid.xr : grid.yr;
    if (l[face] == k::none && r[face] == k::none)
      throw std::runtime_error("Private region nonzero impulse without fluid");
    if (l[face] != k::none && r[face] != k::none &&
        topology.labels[l[face]] != topology.labels[r[face]])
      throw std::runtime_error("Private region closed face retains impulse");
    return l[face] != k::none ? l[face] : r[face];
  }
  // Books sign * (masses, U, G, K, J, passives) of every region, and, when
  // `totals` is given, sums the whole state's audit totals in the same pass.
  void capture(const State &state, double gravity, double sign,
               Totals *totals = nullptr,
               ma::experimental::mac_kernel::Pool *pool = nullptr) {
    namespace k = ma::experimental::mac_kernel;
    std::vector<RegionPartial> parts(grid.blocks, RegionPartial(topology));
    std::vector<Totals> sums(totals ? grid.blocks : 0);
    const auto inventory = [&](k::Index i) {
      double total = 0;
      if (i != k::none)
        for (std::size_t a = state.masses.offsets[i];
             a < state.masses.offsets[i + 1]; ++a)
          total += state.masses.mass[a];
      return total;
    };
    blocks(pool, [&](std::size_t block) {
      auto &part = parts[block];
      Totals *sum = totals ? &sums[block] : nullptr;
      if (sum)
        sum->species.resize(species_count);
      const auto y0 = grid.block_row_begin(block), y1 = grid.block_row_end(block);
      for (std::size_t i = y0 * grid.W; i < y1 * grid.W; ++i) {
        double mass = 0;
        for (std::size_t a = state.masses.offsets[i];
             a < state.masses.offsets[i + 1]; ++a) {
          const double m = state.masses.mass[a];
          mass += m;
          if (!grid.wall[i])
            part.mass(i, state.masses.species[a], sign * m);
          if (sum)
            sum->species[state.masses.species[a]].add(m);
        }
        const double potential =
            mass * gravity * (static_cast<double>(i / grid.W) + .5) * grid.h;
        if (!grid.wall[i]) {
          part.energy(i, sign * state.energy[i]);
          part.energy(i, sign * potential);
        }
        if (sum) {
          sum->energy.add(state.energy[i]);
          sum->energy.add(potential);
          sum->energy_operands.add(std::abs(state.energy[i]));
          sum->energy_operands.add(std::abs(potential));
        }
        if (!state.passives.mass.empty())
          for (std::size_t a = state.passives.offsets[i];
               a < state.passives.offsets[i + 1]; ++a) {
            if (!grid.wall[i])
              part.passive(i, state.passives.species[a],
                           sign * state.passives.mass[a]);
            if (sum)
              sum->passives[state.passives.species[a]].add(state.passives.mass[a]);
          }
      }
      const auto faces = [&](bool x, std::size_t first, std::size_t end) {
        const auto &l = x ? grid.xl : grid.yl, &r = x ? grid.xr : grid.yr;
        const auto &j = x ? state.jx : state.jy;
        for (std::size_t f = first; f < end; ++f)
          if (j[f] != 0) {
            const auto i = face_cell(x, f);
            const double inertia = .5 * (inventory(l[f]) + inventory(r[f]));
            if (!(inertia > 0))
              throw std::runtime_error("Private region J without inertia");
            const double kinetic = .5 * j[f] * (j[f] / inertia);
            part.energy(i, sign * kinetic);
            part.momentum(i, x, sign * j[f]);
            if (sum) {
              sum->energy.add(kinetic);
              sum->energy_operands.add(kinetic);
              (x ? sum->jx : sum->jy).add(j[f]);
              (x ? sum->jx_operands : sum->jy_operands).add(std::abs(j[f]));
            }
          }
      };
      faces(true, y0 * grid.XW, y1 * grid.XW);
      faces(false, y0 * grid.W, grid.block_yrow_end(block) * grid.W);
    });
    for (const auto &part : parts)
      merge(part);
    if (!totals)
      return;
    *totals = {};
    totals->species.resize(species_count);
    for (const auto &sum : sums) {
      for (std::size_t key = 0; key < species_count; ++key)
        merge(totals->species[key], sum.species[key]);
      for (auto [into, part] :
           {std::pair{&totals->energy, &sum.energy},
            std::pair{&totals->jx, &sum.jx}, std::pair{&totals->jy, &sum.jy},
            std::pair{&totals->energy_operands, &sum.energy_operands},
            std::pair{&totals->jx_operands, &sum.jx_operands},
            std::pair{&totals->jy_operands, &sum.jy_operands}})
        merge(*into, *part);
      for (const auto &[key, value] : sum.passives)
        merge(totals->passives[key], value);
    }
  }
  // Phase conversion of every cell: each region loses its constituents
  // before and gains those after; `conversion` sums the same per species.
  void convert(const SparseMass &before, const SparseMass &after,
               std::vector<DoubleDouble> &conversion,
               ma::experimental::mac_kernel::Pool *pool = nullptr) {
    std::vector<RegionPartial> parts(grid.blocks, RegionPartial(topology));
    std::vector<std::vector<DoubleDouble>> sums(
        grid.blocks, std::vector<DoubleDouble>(conversion.size()));
    blocks(pool, [&](std::size_t block) {
      for (std::size_t i = grid.block_row_begin(block) * grid.W;
           i < grid.block_row_end(block) * grid.W; ++i)
        for (const auto [masses, sign] :
             {std::pair{&before, -1.}, std::pair{&after, 1.}})
          for (std::size_t a = masses->offsets[i]; a < masses->offsets[i + 1];
               ++a) {
            sums[block].at(masses->species[a]).add(sign * masses->mass[a]);
            parts[block].mass(i, masses->species[a], sign * masses->mass[a]);
          }
    });
    for (std::size_t block = 0; block < grid.blocks; ++block) {
      merge(parts[block]);
      for (std::size_t key = 0; key < conversion.size(); ++key)
        merge(conversion[key], sums[block][key]);
    }
  }
  double finish() const {
    double worst = 0;
    const auto check = [&](const Balance &b, std::size_t region,
                           const char *kind) {
      // An account nothing was booked to is closed and adds nothing.
      if (b.operands.hi == 0 && b.operands.lo == 0 && b.residual.hi == 0 &&
          b.residual.lo == 0)
        return;
      if (!b.closed())
        throw std::runtime_error(
            "Private connected-region " + std::to_string(region) + " " + kind +
            " audit failed: residual=" + std::to_string(b.residual.value()) +
            " scale=" + std::to_string(b.operands.value()));
      if (b.operands.value())
        worst =
            std::max(worst, std::abs(b.residual.value()) / b.operands.value());
    };
    for (std::size_t i = 0; i < count(); ++i) {
      for (std::size_t key = 0; key < species_count; ++key)
        check(species_[i * species_count + key], i, "species");
      check(energy_[i], i, "energy");
      check(jx_[i], i, "Jx");
      check(jy_[i], i, "Jy");
      for (const auto &[key, b] : passives_[i])
        check(b, i, "passive");
    }
    return worst;
  }
};
} // namespace ma::experimental::game_kernel
