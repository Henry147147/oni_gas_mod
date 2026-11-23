#pragma once
#include "mixed_atmospheres/mac_closed_world.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
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
//
// When a few cells open or close, only the regions holding or beside them
// change: those are flood-filled again (their open cells and the cells that
// opened) and the others kept. The result is ActivityRegions(g) exactly: the
// same partition (a kept region neither holds nor touches a changed cell, so
// it is still whole and joined to nothing new), each region's cells in order,
// and the regions in the order of their first cells, which is the order in
// which a full pass meets them. A full pass over a 256,000-cell map took about
// 3 ms on every frame that built or dug a tile.
inline const ActivityRegions& cached_activity_regions(const MacGrid& g) {
  constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
  struct Cache {
    std::size_t width=0,height=0;
    std::vector<std::uint8_t> walls;
    std::unique_ptr<ActivityRegions> regions;
    std::vector<std::uint32_t> first; // per cell: its region's first cell (none for a wall)
    std::vector<std::uint8_t> refill; // scratch, all zero between calls
  };
  thread_local Cache cache;
  const auto N = g.width * g.height;
  const auto full = [&] {
    auto regions=std::make_unique<ActivityRegions>(g);
    cache.walls=g.walls;cache.width=g.width;cache.height=g.height;cache.regions=std::move(regions);
    cache.first.assign(N, none);
    if (N <= none) for (const auto& region : cache.regions->cells) for (const auto i : region) cache.first[i] = static_cast<std::uint32_t>(region.front());
  };
  if(!cache.regions || cache.width!=g.width || cache.height!=g.height || cache.walls.size()!=g.walls.size() ||
     g.walls.size()!=N || cache.first.size()!=N || N>none) { full(); return *cache.regions; }
  if(cache.walls==g.walls) return *cache.regions;
  std::vector<std::size_t> changed;
  for (std::size_t i = 0; i < N; ++i) if ((cache.walls[i] != 0) != (g.walls[i] != 0)) changed.push_back(i);
  if (changed.size() > N / 64) { full(); return *cache.regions; }
  // The old regions holding or beside a changed cell, by first cell.
  std::vector<std::uint32_t> touched;
  const auto around = [&](std::size_t i, const auto& visit) {
    const auto x = i % g.width, y = i / g.width;
    visit(i);
    if (x) visit(i - 1);
    if (x + 1 < g.width) visit(i + 1);
    if (y) visit(i - g.width);
    if (y + 1 < g.height) visit(i + g.width);
  };
  for (const auto c : changed) around(c, [&](std::size_t j) { if (cache.first[j] != none) touched.push_back(cache.first[j]); });
  std::sort(touched.begin(), touched.end());
  touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
  auto& old = cache.regions->cells;
  const auto index_of = [&](std::uint32_t first) {
    return static_cast<std::size_t>(std::lower_bound(old.begin(), old.end(), first,
        [](const std::vector<std::size_t>& r, std::uint32_t f) { return r.front() < f; }) - old.begin());
  };
  // Their cells still open and the cells that opened, filled again.
  std::vector<std::uint8_t> dropped(old.size());
  std::vector<std::size_t> open;
  for (const auto f : touched) {
    const auto r = index_of(f); dropped[r] = 1;
    for (const auto i : old[r]) if (!g.walls[i]) open.push_back(i);
  }
  for (const auto c : changed) if (!g.walls[c]) open.push_back(c);
  std::sort(open.begin(), open.end());
  auto& mark = cache.refill;
  if (mark.size() != N) mark.assign(N, 0);
  struct Clear { std::vector<std::uint8_t>& mark; const std::vector<std::size_t>& open; ~Clear() { for (const auto i : open) mark[i] = 0; } } clear{mark, open};
  for (const auto i : open) mark[i] = 1;
  std::vector<std::vector<std::size_t>> refilled;
  for (const auto seed : open) if (mark[seed] == 1) {
    auto& region = refilled.emplace_back(); region.push_back(seed); mark[seed] = 2;
    for (std::size_t at = 0; at < region.size(); ++at)
      around(region[at], [&](std::size_t j) { if (mark[j] == 1) { mark[j] = 2; region.push_back(j); } });
    std::sort(region.begin(), region.end());
  }
  for (const auto c : changed) if (g.walls[c]) cache.first[c] = none;
  for (const auto& region : refilled) for (const auto i : region) cache.first[i] = static_cast<std::uint32_t>(region.front());
  std::vector<std::vector<std::size_t>> next;
  next.reserve(old.size() + refilled.size());
  for (std::size_t r = 0; r < old.size(); ++r) if (!dropped[r]) next.push_back(std::move(old[r]));
  for (auto& region : refilled) next.push_back(std::move(region));
  std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.front() < b.front(); });
  old = std::move(next);
  cache.walls = g.walls;
  return *cache.regions;
}
// Whether one cell's saved clock is valid (`open`: not a solid cell).
inline bool valid_activity_clock(const MacGameActivityCell& c, double time, bool open) {
  return !(!std::isfinite(c.last_activity_s) || !std::isfinite(c.pending_s) ||
        !std::isfinite(c.next_remote_s) ||
        !(c.last_activity_s == -1 || (c.last_activity_s >= 0 && c.last_activity_s <= time)) ||
        c.pending_s < 0 || c.pending_s > 1.2 || c.next_remote_s < 0 || c.next_remote_s > time+1.2 ||
        (c.last_activity_s == 0 && std::signbit(c.last_activity_s)) ||
        (c.pending_s == 0 && std::signbit(c.pending_s)) ||
        (c.next_remote_s == 0 && std::signbit(c.next_remote_s)) ||
        (c.actors && (c.pending_s != 0 || c.next_remote_s != 0)) ||
        (open &&
          ((c.pending_s > 0 && !(c.next_remote_s > time)) ||
           (c.next_remote_s > 0 && !(c.next_remote_s > time)))) ||
        (c.last_activity_s == -1 && (c.actors || c.pending_s != 0 || c.next_remote_s != 0)));
}
inline void validate_activity(std::span<const MacGameActivityCell> state,
                              std::size_t cells, double time,
                              std::span<const std::uint8_t> walls = {}) {
  if (state.size() != cells) throw std::invalid_argument("Game activity coverage differs");
  for (std::size_t i=0;i<state.size();++i)
    if (!valid_activity_clock(state[i], time, walls.empty() || !walls[i]))
      throw std::invalid_argument("Invalid saved game activity clock");
}
// validate_activity with the cells checked in fixed blocks by `run(blocks,
// job)` (a worker pool's run): the same verdict and the same exception.
template <class Run>
inline void validate_activity_in_blocks(Run&& run, std::span<const MacGameActivityCell> state,
                                        std::size_t cells, double time,
                                        std::span<const std::uint8_t> walls = {}) {
  if (state.size() != cells) throw std::invalid_argument("Game activity coverage differs");
  constexpr std::size_t block = 16384;
  const auto blocks = (state.size() + block - 1) / block;
  std::vector<std::uint8_t> invalid(blocks);
  run(blocks, [&](std::size_t b) {
    for (std::size_t i = b * block, end = std::min(state.size(), i + block); i < end; ++i)
      if (!valid_activity_clock(state[i], time, walls.empty() || !walls[i])) { invalid[b] = 1; return; }
  });
  if (std::find(invalid.begin(), invalid.end(), std::uint8_t{1}) != invalid.end())
    throw std::invalid_argument("Invalid saved game activity clock");
}
// Copies of the clocks for a world to keep (8 MB on a 256,000-cell map, a
// few a frame). A vector a world held comes back here when the last world
// drops it, and the next copy overwrites it, `run(blocks, job)` (a worker
// pool's run) copying fixed blocks of cells: the values are the copy's.
struct ClockRecycler {
  std::mutex mutex;
  std::vector<std::vector<MacGameActivityCell>> spare;
};
inline ClockRecycler& clock_recycler() {
  static auto* recycler = new ClockRecycler; // never destroyed: worlds may outlive statics
  return *recycler;
}
// `to` made a copy of `from`, in fixed blocks by `run`.
template <class Run>
inline void copy_clocks(Run&& run, std::span<const MacGameActivityCell> from, std::vector<MacGameActivityCell>& to) {
  if (to.size() != from.size()) to.resize(from.size());
  constexpr std::size_t block = 16384;
  run((from.size() + block - 1) / block, [&](std::size_t b) {
    const auto first = b * block, end = std::min(from.size(), first + block);
    std::copy(from.begin() + static_cast<std::ptrdiff_t>(first), from.begin() + static_cast<std::ptrdiff_t>(end),
              to.begin() + static_cast<std::ptrdiff_t>(first));
  });
}
template <class Run>
inline std::vector<MacGameActivityCell> copied_clocks(Run&& run, std::span<const MacGameActivityCell> from) {
  std::vector<MacGameActivityCell> to;
  {
    auto& recycler = clock_recycler();
    std::lock_guard lock(recycler.mutex);
    for (auto at = recycler.spare.begin(); at != recycler.spare.end(); ++at)
      if (at->size() == from.size()) { to = std::move(*at); recycler.spare.erase(at); break; }
  }
  copy_clocks(run, from, to);
  return to;
}
inline std::shared_ptr<const std::vector<MacGameActivityCell>> shared_clocks(std::vector<MacGameActivityCell>&& clocks) {
  auto* held = new std::vector<MacGameActivityCell>(std::move(clocks));
  return std::shared_ptr<const std::vector<MacGameActivityCell>>(held, [](const std::vector<MacGameActivityCell>* done) {
    auto* owned = const_cast<std::vector<MacGameActivityCell>*>(done);
    try {
      auto& recycler = clock_recycler();
      std::lock_guard lock(recycler.mutex);
      if (recycler.spare.size() < 4) recycler.spare.push_back(std::move(*owned));
    } catch (...) {}
    delete owned;
  });
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
  // Safe from several threads at once for different regions (a region's
  // cells, and the left and lower neighbours it reads, are its own).
  bool operator()(std::size_t region, const std::vector<std::size_t>& cells) {
    if (known_[region] < 0) {
      std::call_once(sized_, [&] { mass_.resize(state_.cells.size()); liquid_.resize(state_.cells.size()); });
      known_[region] = moving(cells) ? 1 : 0;
    }
    return known_[region] != 0;
  }
private:
  std::once_flag sized_;
};
// Pure policy, shared by the owner query and physical advance. Only the caller's
// private candidate is updated; publication commits it after all physics passes.
// `levelling` (profile 12) keeps a remote region past its pause deadline in the
// remote tier while its liquid still moves; a paused region stays paused.
// `run(count, job)` runs job(r) for every region r, in any order or at once:
// each region reads and writes only its own cells (a worker pool's run; a
// region that throws makes it throw the lowest such region's exception).
template <class Run>
inline std::vector<double> activity_step_in(Run&& run, const MacGrid& g, const ActivityRegions& regions,
    std::vector<MacGameActivityCell>& state, const MacGameSettings& cfg,
    double now, double dt, Levelling* levelling = nullptr) {
  if(!std::isfinite(now+dt) || !(now+dt>now))
    throw std::invalid_argument("Game activity clock cannot advance");
  std::vector<double> due(g.walls.size());
  run(regions.cells.size(), [&](std::size_t region) {
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
  });
  return due;
}
inline std::vector<double> activity_step(const MacGrid& g, const ActivityRegions& regions,
    std::vector<MacGameActivityCell>& state, const MacGameSettings& cfg,
    double now, double dt, Levelling* levelling = nullptr) {
  return activity_step_in([](std::size_t count, const auto& job) { for (std::size_t r = 0; r < count; ++r) job(r); },
                          g, regions, state, cfg, now, dt, levelling);
}
} // namespace ma::experimental::game_kernel
