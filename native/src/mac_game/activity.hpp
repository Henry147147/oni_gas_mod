#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace ma::experimental::game_kernel {
// Activity uses solid-separated regions, like RegionalTick; temporary face
// closures never split clocks or duplicate shared face momentum.
struct ActivityRegions {
  std::vector<std::vector<std::size_t>> cells;
  explicit ActivityRegions(const MacGrid& g) {
    const auto N = g.width * g.height;
    std::vector<std::uint8_t> seen(N);
    for (std::size_t seed = 0; seed < N; ++seed) if (!g.walls[seed] && !seen[seed]) {
      auto& region = cells.emplace_back(); region.push_back(seed); seen[seed] = 1;
      for (std::size_t at = 0; at < region.size(); ++at) {
        const auto i = region[at], x = i % g.width, y = i / g.width;
        const auto visit = [&](std::size_t j) {
          if (!g.walls[j] && !seen[j]) { seen[j] = 1; region.push_back(j); }
        };
        if (x) visit(i-1);
        if (x+1 < g.width) visit(i+1);
        if (y) visit(i-g.width);
        if (y+1 < g.height) visit(i+g.width);
      }
      std::sort(region.begin(), region.end());
    }
  }
};
// Activity connectivity depends only on solid walls. Actor snapshots and due
// queries reuse it until geometry changes; face closures do not split regions.
inline const ActivityRegions& cached_activity_regions(const MacGrid& g) {
  struct Cache {
    std::size_t width=0,height=0;
    std::vector<std::uint8_t> walls;
    std::unique_ptr<ActivityRegions> regions;
  };
  thread_local Cache cache;
  if(!cache.regions || cache.width!=g.width || cache.height!=g.height || cache.walls!=g.walls) {
    auto regions=std::make_unique<ActivityRegions>(g);
    cache.walls=g.walls;cache.width=g.width;cache.height=g.height;cache.regions=std::move(regions);
  }
  return *cache.regions;
}
inline void validate_activity(std::span<const MacGameActivityCell> state,
                              std::size_t cells, double time,
                              std::span<const std::uint8_t> walls = {}) {
  if (state.size() != cells) throw std::invalid_argument("Game activity coverage differs");
  for (std::size_t i=0;i<state.size();++i) {
    const auto& c=state[i];
    if (!std::isfinite(c.last_activity_s) || !std::isfinite(c.pending_s) ||
        !std::isfinite(c.next_remote_s) ||
        !(c.last_activity_s == -1 || (c.last_activity_s >= 0 && c.last_activity_s <= time)) ||
        c.pending_s < 0 || c.pending_s > 1.2 || c.next_remote_s < 0 || c.next_remote_s > time+1.2 ||
        (c.last_activity_s == 0 && std::signbit(c.last_activity_s)) ||
        (c.pending_s == 0 && std::signbit(c.pending_s)) ||
        (c.next_remote_s == 0 && std::signbit(c.next_remote_s)) ||
        (c.actors && (c.pending_s != 0 || c.next_remote_s != 0)) ||
        ((walls.empty() || !walls[i]) &&
          ((c.pending_s > 0 && !(c.next_remote_s > time)) ||
           (c.next_remote_s > 0 && !(c.next_remote_s > time)))) ||
        (c.last_activity_s == -1 && (c.actors || c.pending_s != 0 || c.next_remote_s != 0)))
      throw std::invalid_argument("Invalid saved game activity clock");
  }
}
inline void validate_activity_interval(double seconds,double maximum,std::size_t budget=65536) {
  const double intervals=std::ceil(seconds/maximum);
  if(!std::isfinite(intervals)||intervals<1||intervals>static_cast<double>(budget))
    throw std::invalid_argument("Game activity interval exceeds its scheduling budget");
}
// Profile 12 (MAC_GAME_KERNEL.md, "Levelling keeps a region remote"): a remote
// region reaching its pause deadline keeps its remote steps while its liquid
// still moves this fast on some face. Pools at rest move below 1e-6 m/s, and
// below 2.6e-4 m/s at the deadline with 1-1.5 m/s of convecting gas above; a
// levelling pool moves about 0.016 m/s times its column range in kg over its
// width in columns, so it pauses within about 2.6 kg of level at 140 columns.
inline constexpr double levelling_speed_m_per_s = 3e-4;
// The extension ends this long after the region's last activity whatever its
// liquid does, so motion that never stops (a pool boiling on hot rock) cannot
// keep an untouched region stepping for good.
inline constexpr double levelling_limit_s = 600;
// Whether a region's liquid is still moving, read from one owner state (the
// state at the start of the query or advance, so both agree). Each open face
// between two of its cells counts |J|/I (I half the two cells' mass) times the
// smaller of the two cells' liquid volume fractions (nominal volume, at most
// 1, as the liquid drag): gas weighs nothing and mist next to nothing. Saved
// clocks and the checkpoint are unchanged; regions are evaluated once each.
class Levelling {
  const MacTransportState& state_;
  std::vector<double> specific_volume_;
  std::vector<std::int8_t> known_;
  std::vector<double> mass_, liquid_;
  // One pass in cell order: a cell's left and lower neighbours in the region
  // come before it. Stops at the first fast face.
  bool moving(const std::vector<std::size_t>& cells) {
    const auto& g = state_.grid;
    const auto W = g.width;
    const double V = g.volume_m3();
    const auto fast = [&](std::size_t a, std::size_t b, double J) {
      const double share = std::min(liquid_[a], liquid_[b]);
      return share > 0 && std::abs(J) * share > levelling_speed_m_per_s * .5 * (mass_[a] + mass_[b]);
    };
    for (const auto i : cells) {
      double m = 0, v = 0;
      const auto& masses = state_.cells[i].mixture.masses;
      if (!masses.implicit_zero())
        for (std::size_t k = 0; k < masses.size(); ++k) {
          m += masses[k];
          if (specific_volume_[k] > 0) v += masses[k] * specific_volume_[k];
        }
      mass_[i] = m; liquid_[i] = std::min(1., v / V);
      const auto x = i % W, y = i / W;
      if (x > 0 && !g.walls[i - 1] && fast(i - 1, i, state_.momentum_x[x + y * (W + 1)])) return true;
      if (y > 0 && !g.walls[i - W] && fast(i - W, i, state_.momentum_y[x + y * W])) return true;
    }
    return false;
  }
public:
  Levelling(const MacTransportState& state, const SpeciesRegistry& registry,
            const FluidThermodynamics& eos, std::size_t regions)
      : state_(state), specific_volume_(registry.size()), known_(regions, -1) {
    for (const auto& m : eos.reference().materials())
      if (m.phase != ReferencePhase::ideal_gas)
        specific_volume_[registry.index(m.id)] = 1 / m.zero_pressure_density_kg_per_m3;
  }
  bool operator()(std::size_t region, const std::vector<std::size_t>& cells) {
    if (known_[region] < 0) {
      mass_.resize(state_.cells.size()); liquid_.resize(state_.cells.size());
      known_[region] = moving(cells) ? 1 : 0;
    }
    return known_[region] != 0;
  }
};
// Pure policy, shared by the owner query and physical advance. Only the caller's
// private candidate is updated; publication commits it after all physics passes.
// `levelling` (profile 12) keeps a remote region past its pause deadline in the
// remote tier while its liquid still moves; a paused region stays paused.
inline std::vector<double> activity_step(const MacGrid& g, const ActivityRegions& regions,
    std::vector<MacGameActivityCell>& state, const MacGameSettings& cfg,
    double now, double dt, Levelling* levelling = nullptr) {
  if(!std::isfinite(now+dt) || !(now+dt>now))
    throw std::invalid_argument("Game activity clock cannot advance");
  std::vector<double> due(g.walls.size());
  for (std::size_t region = 0; region < regions.cells.size(); ++region) {
    const auto& cells = regions.cells[region];
    const auto& first = state[cells.front()];
    double last = first.last_activity_s, pending = first.pending_s,
           next = first.next_remote_s;
    bool actors = false, same_clock = true;
    for (const auto i : cells) {
      const auto& c = state[i]; last = std::max(last,c.last_activity_s);
      actors = actors || c.actors;
      same_clock = same_clock && c.pending_s == pending && c.next_remote_s == next;
    }
    // A merge with different held clocks is a new disturbance. Never add
    // clocks from different histories or catch up their elapsed idle time.
    if (!same_clock) last = now;
    if (actors) last = now;
    double step = 0;
    if (!cfg.activity_tiers || actors || (last >= 0 && now-last < cfg.activity_hold_s)) {
      step = dt; pending = next = 0;
    } else if (last < 0 || (now-last >= cfg.activity_pause_s &&
                            !(next > 0 && levelling && now-last < levelling_limit_s &&
                              (*levelling)(region, cells)))) {
      pending = next = 0;
    } else {
      if (!(next > now)) {
        pending = 0;
        // Five deterministic game-time slots distribute remote work. The
        // representative is the region's lowest global cell, never a worker.
        const double phase = .2 * static_cast<double>(cells.front()%5);
        next = std::floor(now) + phase;
        if (!(next > now)) next += 1;
        if(!(next>now))throw std::invalid_argument("Game remote deadline cannot advance");
      }
      pending += dt;
      if (now+dt >= next) {
        step = pending; pending = 0;
        do {
          const double previous=next;next+=1;
          if(!(next>previous))throw std::invalid_argument("Game remote deadline cannot advance");
        } while (!(next > now+dt));
      }
    }
    for (const auto i : cells) {
      due[i] = step;
      state[i].last_activity_s = last;
      state[i].pending_s = pending; state[i].next_remote_s = next;
    }
  }
  return due;
}
} // namespace ma::experimental::game_kernel
