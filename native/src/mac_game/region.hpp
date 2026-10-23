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
// One double-double audit per connected fluid region and tick. It records
// physical sources/sinks, never a rounding-error or per-write repair ledger.
class RegionAudit {
  struct Accounts {
    std::vector<Balance> species;
    Balance energy, jx, jy;
    std::map<std::uint64_t, Balance> passives;
  };
  const ma::experimental::mac_kernel::Grid &grid;
  const Topology &topology;
  std::vector<Accounts> accounts;

public:
  RegionAudit(const ma::experimental::mac_kernel::Grid &g, const Topology &t,
              std::size_t species)
      : grid(g), topology(t), accounts(t.count) {
    for (auto &a : accounts)
      a.species.resize(species);
  }
  std::size_t count() const { return accounts.size(); }
  void mass(std::size_t cell, std::size_t key, double value) {
    accounts.at(topology.labels.at(cell)).species.at(key).add(value);
  }
  void energy(std::size_t cell, double value) {
    accounts.at(topology.labels.at(cell)).energy.add(value);
  }
  void passive(std::size_t cell, std::uint64_t key, double value) {
    accounts.at(topology.labels.at(cell)).passives[key].add(value);
  }
  void momentum(std::size_t cell, bool x, double value) {
    auto &a = accounts.at(topology.labels.at(cell));
    (x ? a.jx : a.jy).add(value);
  }
  void momentum_receipt(std::size_t cell, bool x, double value,
                        double operands) {
    auto &a = accounts.at(topology.labels.at(cell));
    auto &b = x ? a.jx : a.jy;
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
  void capture(const State &state, double gravity, double sign) {
    std::vector<double> inventory(grid.N);
    for (std::size_t i = 0; i < grid.N; ++i) {
      if (grid.wall[i])
        continue;
      for (std::size_t a = state.masses.offsets[i];
           a < state.masses.offsets[i + 1]; ++a) {
        inventory[i] += state.masses.mass[a];
        mass(i, state.masses.species[a], sign * state.masses.mass[a]);
      }
      energy(i, sign * state.energy[i]);
      energy(i, sign * inventory[i] * gravity * (static_cast<double>(i / grid.W) + .5) * grid.h);
      if (!state.passives.mass.empty())
        for (std::size_t a = state.passives.offsets[i];
             a < state.passives.offsets[i + 1]; ++a)
          passive(i, state.passives.species[a], sign * state.passives.mass[a]);
    }
    const auto faces = [&](bool x, const auto &j) {
      namespace k = ma::experimental::mac_kernel;
      const auto &l = x ? grid.xl : grid.yl, &r = x ? grid.xr : grid.yr;
      for (std::size_t f = 0; f < j.size(); ++f)
        if (j[f] != 0) {
          const auto i = face_cell(x, f);
          const double inertia = .5 * ((l[f] == k::none ? 0 : inventory[l[f]]) +
                                       (r[f] == k::none ? 0 : inventory[r[f]]));
          if (!(inertia > 0))
            throw std::runtime_error("Private region J without inertia");
          energy(i, sign * .5 * j[f] * (j[f] / inertia));
          momentum(i, x, sign * j[f]);
        }
    };
    faces(true, state.jx);
    faces(false, state.jy);
  }
  double finish() const {
    double worst = 0;
    const auto check = [&](const Balance &b, std::size_t region,
                           const char *kind) {
      if (!b.closed())
        throw std::runtime_error(
            "Private connected-region " + std::to_string(region) + " " + kind +
            " audit failed: residual=" + std::to_string(b.residual.value()) +
            " scale=" + std::to_string(b.operands.value()));
      if (b.operands.value())
        worst =
            std::max(worst, std::abs(b.residual.value()) / b.operands.value());
    };
    for (std::size_t i = 0; i < accounts.size(); ++i) {
      const auto &a = accounts[i];
      for (const auto &b : a.species)
        check(b, i, "species");
      check(a.energy, i, "energy");
      check(a.jx, i, "Jx");
      check(a.jy, i, "Jy");
      for (const auto &[key, b] : a.passives)
        check(b, i, "passive");
    }
    return worst;
  }
};
} // namespace ma::experimental::game_kernel
