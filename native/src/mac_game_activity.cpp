#include "mac_game/activity.hpp"
#include <limits>

namespace ma::experimental {
std::span<const MacGameActivityCell> MacClosedWorld::game_activity() const noexcept {
  return game_activity_ ? std::span<const MacGameActivityCell>(*game_activity_) :
                          std::span<const MacGameActivityCell>{};
}
MacClosedWorld MacClosedWorld::with_game_activity(std::span<const std::uint8_t> touched,
    std::span<const std::uint8_t> actors) const {
  if (settings_.game_profile != MacGameProfile::game_kernel_activity_tiers)
    throw std::invalid_argument("Game activity needs experimental profile9");
  const auto N = state().cells.size();
  const auto valid = [&](auto mask) {
    return (mask.empty() || mask.size() == N) &&
        std::all_of(mask.begin(),mask.end(),[](auto v){return v<=1;});
  };
  if (!valid(touched) || !valid(actors)) throw std::invalid_argument("Invalid game activity input masks");
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
  for(std::size_t i=0;i<N;++i) {
    cells[i].actors=present[i]!=0;
    if(active[i] || present[i])cells[i].last_activity_s=time_s_;
  }
  const auto& regions=game_kernel::cached_activity_regions(g);
  for(const auto& region:regions.cells) {
    double last=-1;bool present_actor=false;
    for(const auto i:region){last=std::max(last,cells[i].last_activity_s);present_actor=present_actor||cells[i].actors;}
    if(present_actor || (last>=0 && time_s_-last<settings_.game.activity_hold_s))
      for(const auto i:region){cells[i].last_activity_s=last;cells[i].pending_s=cells[i].next_remote_s=0;}
  }
  if(std::equal(cells.begin(),cells.end(),game_activity().begin()))return *this;
  auto out=MacClosedWorld(Derived{},*this,state_snapshot_);out.game_totals_=game_totals_;
  out.game_activity_=std::make_shared<const std::vector<MacGameActivityCell>>(std::move(cells));
  return out;
}
MacClosedWorld MacClosedWorld::with_game_activity_tiers(bool enabled) const {
  if(settings_.game_profile!=MacGameProfile::game_kernel_activity_tiers)
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
  if(settings_.game_profile!=MacGameProfile::game_kernel_activity_tiers) {
    for(std::size_t i=0;i<total.size();++i)if(!g.walls[i])total[i]=seconds;
    return total;
  }
  game_kernel::validate_activity(game_activity(),g.walls.size(),time_s_,g.walls);
  game_kernel::validate_activity_interval(seconds,settings_.maximum_timestep_s);
  const auto& regions=game_kernel::cached_activity_regions(g);
  auto cells=std::vector<MacGameActivityCell>(game_activity().begin(),game_activity().end());
  double elapsed=0;std::size_t intervals=0;
  while(elapsed<seconds) {
    if(++intervals>65536)throw std::invalid_argument("Game activity query exceeds its scheduling budget");
    const double dt=std::min(settings_.maximum_timestep_s,seconds-elapsed);
    const auto due=game_kernel::activity_step(g,regions,cells,settings_.game,time_s_+elapsed,dt);
    for(std::size_t i=0;i<total.size();++i)total[i]+=due[i];
    const double next=elapsed+dt;
    if(!(next>elapsed))throw std::invalid_argument("Game activity interval cannot advance");
    elapsed=next;
  }
  return total;
}
} // namespace ma::experimental
