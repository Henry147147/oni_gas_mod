#pragma once
#include "mixed_atmospheres/mac_energy.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <exception>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace ma::experimental::game_kernel {
// Execution history only. It never enters a physical checkpoint or a timestep,
// pressure cap, tolerance or activity decision. Membership keys identify exact
// regions. A small change in the outline can
// inherit a scheduling estimate.
struct RegionExecutionKey {
  std::size_t representative=0,cells=0;
  std::uint64_t membership=1469598103934665603ULL;
  auto operator<=>(const RegionExecutionKey&) const = default;
};
struct RegionExecutionWork {
  RegionExecutionKey key;
  double seconds=0;
  unsigned maximum_threads=1;
};
struct RegionExecutionCost {
  double serial_ms_per_second=0;
  unsigned samples=0;
};
enum class RegionExecutionReason {
  serial_budget, held, unknown, too_small, parallel, serial_probe
};
// Which regions run on workers of their own this interval. Every other
// stepped region shares the residual workers, one region to a worker.
struct RegionExecutionShare {
  std::size_t region=0;
  unsigned threads=1;
};
struct RegionExecutionDecision {
  // Costliest first. Their thread counts and `residual_threads` add up to at
  // most the budget.
  std::vector<RegionExecutionShare> parallel;
  // A selected region that runs on one residual worker this interval, so that
  // its serial cost is measured again.
  std::optional<std::size_t> probe_region;
  unsigned residual_threads=0;
  RegionExecutionReason reason=RegionExecutionReason::serial_budget;
  [[nodiscard]] unsigned threads_of(std::size_t region) const noexcept {
    for(const auto& share:parallel)if(share.region==region)return share.threads;
    return 0;
  }
};
// Diagnostic for the performance tools, process-wide: committed regional
// intervals by the number of regions that had workers of their own (the last
// entry counts four or more) and the inner workers handed out. Nothing reads
// it back into a decision.
namespace execution_counts {
inline std::array<std::atomic<std::uint64_t>,5>& intervals() noexcept {static std::array<std::atomic<std::uint64_t>,5> counts{};return counts;}
inline std::atomic<std::uint64_t>& inner_threads() noexcept {static std::atomic<std::uint64_t> count{0};return count;}
}
class RegionalExecutionHistory {
public:
  // A region enters with this much predicted serial work an interval and
  // stays while it has three quarters of it.
  static constexpr double minimum_work_ms=20;
  static constexpr double retained_work_ms=15;
  static constexpr unsigned probe_interval=16;
  // More inner workers than this bought nothing on the measured colonies:
  // most of a region's interval is serial (MAC_GAME_KERNEL.md, regional
  // execution).
  static constexpr unsigned maximum_inner_threads=4;
  // MA_REGION_WORKERS overrides the limit for measurements (2 to 64).
  [[nodiscard]] static unsigned inner_limit() noexcept {
    static const unsigned limit=[] {
      const char* value=std::getenv("MA_REGION_WORKERS");
      const long parsed=value && *value?std::strtol(value,nullptr,10):0;
      return parsed>=2 && parsed<=64?static_cast<unsigned>(parsed):maximum_inner_threads;
    }();
    return limit;
  }
  std::map<RegionExecutionKey,RegionExecutionCost> costs;
  // Regions that ran on their own workers in the last interval that stepped
  // them, with the number of such intervals since their last serial sample.
  std::map<RegionExecutionKey,unsigned> selected;

  void retain(std::span<const RegionExecutionKey> keys) {
    const std::set<RegionExecutionKey> live(keys.begin(),keys.end());
    // Digging or adding one tile used to make the large colony region run
    // serially for two ticks after every edit. Carry its cost estimate and
    // probe age across a small resize at the same anchor. A new or substantially
    // resized region still earns its own samples. Only thread assignment uses
    // these hints. Geometry, pressure data and clocks always use the new grid.
    for (const auto& key : keys) {
      if (costs.contains(key)) continue;
      auto prior = costs.lower_bound({key.representative, 0, 0});
      if (prior == costs.end() || prior->first.representative != key.representative) continue;
      const auto old_size = prior->first.cells;
      if (std::max(old_size, key.cells) - std::min(old_size, key.cells) > old_size / 256) continue;
      costs.emplace(key, prior->second);
      if (const auto age = selected.find(prior->first); age != selected.end())
        selected.emplace(key, age->second);
    }
    std::erase_if(costs,[&](const auto& entry){return !live.contains(entry.first);});
    std::erase_if(selected,[&](const auto& entry){return !live.contains(entry.first);});
  }
  // Every region with enough measured serial work gets workers of its own:
  // the budget, less one worker for the other regions when there are any, is
  // divided evenly among them, at most `maximum_inner_threads` each and never
  // more than a region has row blocks. When the budget cannot give each two
  // workers, the cheapest wait their turn on the residual workers.
  [[nodiscard]] RegionExecutionDecision plan(std::span<const RegionExecutionWork> work,unsigned budget) const {
    RegionExecutionDecision result;
    if(budget<=2 || work.size()<=1)return result;
    result.reason=RegionExecutionReason::held;
    struct Candidate {std::size_t region;double predicted;};
    std::vector<Candidate> candidates;std::size_t stepped=0;bool unknown=false;
    for(std::size_t i=0;i<work.size();++i) {
      const auto& item=work[i];if(item.seconds==0)continue;
      ++stepped;
      // A region without two serial samples runs on a residual worker
      // until it has them; it does not keep
      // the measured regions from their workers.
      const auto known=costs.find(item.key);
      if(known==costs.end() || known->second.samples<2){unknown=true;continue;}
      const double predicted=known->second.serial_ms_per_second*item.seconds;
      if(!std::isfinite(predicted) || predicted<0){unknown=true;continue;}
      if(item.maximum_threads<2)continue;
      if(predicted>=(selected.contains(item.key)?retained_work_ms:minimum_work_ms))candidates.push_back({i,predicted});
    }
    if(stepped==0)return result;
    if(candidates.empty()){result.reason=unknown?RegionExecutionReason::unknown:RegionExecutionReason::too_small;return result;}
    std::stable_sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.predicted>b.predicted;});
    // One selected region at a time gives up its workers for an interval to
    // have its serial cost measured again: the one that has waited longest.
    std::optional<std::size_t> probe;unsigned waited=0;
    for(const auto& candidate:candidates) {
      const auto age=selected.find(work[candidate.region].key);
      if(age!=selected.end() && age->second>=probe_interval && age->second>waited){waited=age->second;probe=candidate.region;}
    }
    if(probe) {
      std::erase_if(candidates,[&](const Candidate& c){return c.region==*probe;});
      result.probe_region=probe;
    }
    // Two workers a region at least; the residual regions keep one worker.
    std::size_t count=candidates.size();
    const auto residual_needed=[&](std::size_t parallel){return stepped>parallel?1U:0U;};
    while(count>0 && budget<residual_needed(count)+2*count)--count;
    if(count==0) {
      result.reason=probe?RegionExecutionReason::serial_probe:RegionExecutionReason::serial_budget;return result;
    }
    const unsigned share=std::min<unsigned>(inner_limit(),(budget-residual_needed(count))/static_cast<unsigned>(count));
    unsigned used=0;
    for(std::size_t c=0;c<count;++c) {
      const auto region=candidates[c].region;
      const unsigned threads=std::min(share,work[region].maximum_threads);
      result.parallel.push_back({region,threads});used+=threads;
    }
    result.residual_threads=budget-used;
    result.reason=RegionExecutionReason::parallel;return result;
  }
  // Called only after the complete regional result and its audits succeed.
  // Parallel observations do not pretend to be serial costs; an occasional
  // original serial pass refreshes that sample. Paused cells never age a probe.
  void committed(std::span<const RegionExecutionWork> work,std::span<const double> elapsed_ms,
                 const RegionExecutionDecision& decision) {
    if(work.size()!=elapsed_ms.size())throw std::invalid_argument("Regional execution timing coverage differs");
    for(std::size_t i=0;i<work.size();++i)
      if(work[i].seconds>0 && (!std::isfinite(elapsed_ms[i]) || elapsed_ms[i]<0))return;
    execution_counts::intervals()[std::min<std::size_t>(decision.parallel.size(),execution_counts::intervals().size()-1)].fetch_add(1,std::memory_order_relaxed);
    for(const auto& share:decision.parallel)execution_counts::inner_threads().fetch_add(share.threads,std::memory_order_relaxed);
    for(std::size_t i=0;i<work.size();++i) {
      if(work[i].seconds==0)continue;
      const auto& key=work[i].key;
      if(decision.threads_of(i)!=0) {++selected[key];continue;}
      const double rate=elapsed_ms[i]/work[i].seconds;
      if(std::isfinite(rate) && rate>=0) {
        auto& cost=costs[key];
        cost.serial_ms_per_second=cost.samples?.5*cost.serial_ms_per_second+.5*rate:rate;
        cost.samples=std::min(32U,cost.samples+1);
      }
      // A probed region stays selected with a fresh sample; any other stepped
      // region that ran serially is no longer selected.
      if(decision.probe_region==i)selected[key]=0;else selected.erase(key);
    }
  }
};
} // namespace ma::experimental::game_kernel
