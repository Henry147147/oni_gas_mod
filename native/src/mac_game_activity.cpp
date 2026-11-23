#include "mac_game/activity.hpp"
#include "mac_kernel_detail.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <functional>
#include <limits>
#include <optional>

namespace ma::experimental {
std::span<const MacGameActivityCell> MacClosedWorld::game_activity() const noexcept {
  return game_activity_ ? std::span<const MacGameActivityCell>(*game_activity_) :
                          std::span<const MacGameActivityCell>{};
}
namespace {
// Per-cell and per-region passes below run in fixed blocks (or one region a
// job) on the worker pool: every cell's or region's arithmetic, and its
// order within the cell or region, is the serial pass's.
mac_kernel::Pool& activity_pool() { return mac_kernel::pool_for(mac_kernel_default_options().threads); }
constexpr std::size_t activity_block = 16384;
}
MacClosedWorld MacClosedWorld::with_game_activity(std::span<const std::uint8_t> touched,
    std::span<const std::uint8_t> actors) const {
  if (!mac_game_activity_profile(settings_.game_profile))
    throw std::invalid_argument("Game activity needs experimental profile9");
  const auto N = state().cells.size();
  auto& pool = activity_pool();
  const auto blocks_of = [](std::size_t count) { return (count + activity_block - 1) / activity_block; };
  {
    // Each mask empty or one per cell, and every mark 0 or 1.
    if (!(touched.empty() || touched.size() == N) || !(actors.empty() || actors.size() == N))
      throw std::invalid_argument("Invalid game activity input masks");
    std::vector<std::uint8_t> invalid(blocks_of(N));
    pool.run(invalid.size(), [&](std::size_t b) {
      for (std::size_t i = b * activity_block, end = std::min(N, i + activity_block); i < end; ++i)
        if ((!touched.empty() && touched[i] > 1) || (!actors.empty() && actors[i] > 1)) { invalid[b] = 1; return; }
    });
    if (std::find(invalid.begin(), invalid.end(), std::uint8_t{1}) != invalid.end())
      throw std::invalid_argument("Invalid game activity input masks");
  }
  auto cells = std::vector<MacGameActivityCell>(game_activity().begin(),game_activity().end());
  std::vector<std::uint8_t> active(N), present(N);
  const auto& g = state().grid;
  const auto mark = [&](std::size_t i, auto& mask) {
    if (!g.walls[i]) { mask[i] = 1; return; }
    const auto x=i%g.width,y=i/g.width;
    const auto add=[&](std::size_t j){if(!g.walls[j])mask[j]=1;};
    if(x)add(i-1);
    if(x+1<g.width)add(i+1);
    if(y)add(i-g.width);
    if(y+1<g.height)add(i+g.width);
  };
  for(std::size_t i=0;i<N;++i) {
    if(!touched.empty() && touched[i])mark(i,active);
    if(!actors.empty() && actors[i])mark(i,present);
  }
  pool.run(blocks_of(N), [&](std::size_t b) {
    for(std::size_t i=b*activity_block,end=std::min(N,i+activity_block);i<end;++i) {
      cells[i].actors=present[i]!=0;
      if(active[i] || present[i])cells[i].last_activity_s=time_s_;
    }
  });
  const auto& regions=game_kernel::cached_activity_regions(g);
  pool.run(regions.cells.size(), [&](std::size_t r) {
    const auto& region=regions.cells[r];
    double last=-1;bool present_actor=false;
    for(const auto i:region){last=std::max(last,cells[i].last_activity_s);present_actor=present_actor||cells[i].actors;}
    if(present_actor || (last>=0 && time_s_-last<settings_.game.activity_hold_s))
      for(const auto i:region){cells[i].last_activity_s=last;cells[i].pending_s=cells[i].next_remote_s=0;}
  });
  // Unchanged clocks (`cells` is a copy of them, the same length).
  const auto before = game_activity();
  std::vector<std::uint8_t> differs(blocks_of(cells.size()));
  pool.run(differs.size(), [&](std::size_t b) {
    const auto first = b * activity_block, end = std::min(cells.size(), first + activity_block);
    differs[b] = !std::equal(cells.begin() + static_cast<std::ptrdiff_t>(first), cells.begin() + static_cast<std::ptrdiff_t>(end),
                             before.begin() + static_cast<std::ptrdiff_t>(first));
  });
  if(std::find(differs.begin(), differs.end(), std::uint8_t{1}) == differs.end())return *this;
  auto out=MacClosedWorld(Derived{},*this,state_snapshot_);out.game_totals_=game_totals_;
  out.game_activity_=std::make_shared<const std::vector<MacGameActivityCell>>(std::move(cells));
  return out;
}
MacClosedWorld MacClosedWorld::with_game_activity_tiers(bool enabled) const {
  if(!mac_game_activity_profile(settings_.game_profile))
    throw std::invalid_argument("Game activity needs experimental profile9");
  if(settings_.game.activity_tiers==enabled)return *this;
  auto cells=std::vector<MacGameActivityCell>(game_activity().begin(),game_activity().end());
  for(auto& c:cells)c.pending_s=c.next_remote_s=0;
  auto out=MacClosedWorld(Derived{},*this,state_snapshot_);out.game_totals_=game_totals_;
  out.settings_.game.activity_tiers=enabled;
  out.game_activity_=std::make_shared<const std::vector<MacGameActivityCell>>(std::move(cells));
  return out;
}
std::vector<double> MacClosedWorld::game_activity_steps(double seconds) const {
  if(!std::isfinite(seconds)||seconds<=0||seconds>10)
    throw std::invalid_argument("Invalid game activity interval");
  const auto& g=state().grid;
  std::vector<double> total(g.walls.size());
  if(!mac_game_activity_profile(settings_.game_profile)) {
    for(std::size_t i=0;i<total.size();++i)if(!g.walls[i])total[i]=seconds;
    return total;
  }
  // The saved clocks' checks (fixed blocks) and the clock steps (one region
  // a job) on the pool: the same verdicts and values.
  auto& pool=activity_pool();
  const auto on_pool=[&](std::size_t blocks,const std::function<void(std::size_t)>& job){pool.run(blocks,job);};
  game_kernel::validate_activity_in_blocks(on_pool,game_activity(),g.walls.size(),time_s_,g.walls);
  game_kernel::validate_activity_interval(seconds,settings_.maximum_timestep_s);
  const auto& regions=game_kernel::cached_activity_regions(g);
  // The clocks this query steps and then drops: a scratch copy kept between
  // queries (a fresh 8 MB vector a frame on a 256,000-cell map).
  thread_local std::vector<MacGameActivityCell> cells;
  cells.assign(game_activity().begin(),game_activity().end());
  std::optional<game_kernel::Levelling> levelling;
  if(settings_.game_profile==MacGameProfile::game_kernel_thin_drag && settings_.game.activity_tiers)
    levelling.emplace(state(),registry_,*thermodynamics_,regions.cells.size());
  double elapsed=0;std::size_t intervals=0;
  while(elapsed<seconds) {
    if(++intervals>65536)throw std::invalid_argument("Game activity query exceeds its scheduling budget");
    const double dt=std::min(settings_.maximum_timestep_s,seconds-elapsed);
    const auto due=game_kernel::activity_step_in(on_pool,g,regions,cells,settings_.game,time_s_+elapsed,dt,
                                              levelling?&*levelling:nullptr);
    pool.run((total.size()+activity_block-1)/activity_block,[&](std::size_t b) {
      for(std::size_t i=b*activity_block,end=std::min(total.size(),i+activity_block);i<end;++i)total[i]+=due[i];
    });
    const double next=elapsed+dt;
    if(!(next>elapsed))throw std::invalid_argument("Game activity interval cannot advance");
    elapsed=next;
  }
  return total;
}
} // namespace ma::experimental
