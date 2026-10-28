#pragma once
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <exception>
#include <thread>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

namespace ma::experimental::game_kernel {
// Execution history only. It never enters a physical checkpoint or a timestep,
// pressure cap, tolerance or activity decision. Membership keys survive changes
// to temporary face barriers and discard split/merged systems' old costs.
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
  serial_budget, held, unknown, too_small, competing, parallel, serial_probe
};
struct RegionExecutionDecision {
  std::optional<std::size_t> parallel_region,probe_region;
  unsigned inner_threads=1;
  RegionExecutionReason reason=RegionExecutionReason::serial_budget;
};
// The coordinator must report failures outside a region job as well (pool
// allocation and worker arithmetic-environment checks). Always join before a
// failure escapes the calling thread. Infrastructure failures retain the pool's
// precedence; numerical region errors are ordered by the caller afterward.
template<class Main,class Residual>
void run_regional_pair(Main&& main,Residual&& residual) {
  std::exception_ptr residual_error,main_error;
  std::thread coordinator([&]{try{residual();}catch(...){residual_error=std::current_exception();}});
  try{main();}catch(...){main_error=std::current_exception();}
  coordinator.join();
  if(residual_error)std::rethrow_exception(residual_error);
  if(main_error)std::rethrow_exception(main_error);
}
class RegionalExecutionHistory {
public:
  static constexpr double minimum_work_ms=20;
  static constexpr unsigned probe_interval=16;
  std::map<RegionExecutionKey,RegionExecutionCost> costs;
  std::optional<RegionExecutionKey> selected;
  unsigned selected_intervals=0;

  void retain(std::span<const RegionExecutionKey> keys) {
    const std::set<RegionExecutionKey> live(keys.begin(),keys.end());
    std::erase_if(costs,[&](const auto& entry){return !live.contains(entry.first);});
    if(selected && !live.contains(*selected)){selected.reset();selected_intervals=0;}
  }
  [[nodiscard]] RegionExecutionDecision plan(std::span<const RegionExecutionWork> work,unsigned budget) const {
    RegionExecutionDecision result;
    if(budget<=2 || work.size()<=1)return result;
    result.reason=RegionExecutionReason::held;
    std::optional<std::size_t> largest;
    double first=0,second=0;
    for(std::size_t i=0;i<work.size();++i) {
      const auto& item=work[i];if(item.seconds==0)continue;
      const auto known=costs.find(item.key);
      if(known==costs.end() || known->second.samples==0){result.reason=RegionExecutionReason::unknown;return result;}
      const double predicted=known->second.serial_ms_per_second*item.seconds;
      if(!std::isfinite(predicted) || predicted<0){result.reason=RegionExecutionReason::unknown;return result;}
      if(!largest || predicted>first){second=first;first=predicted;largest=i;}
      else second=std::max(second,predicted);
    }
    if(!largest)return result;
    const auto& candidate=work[*largest];const auto& cost=costs.at(candidate.key);
    const unsigned inner=std::min({4U,budget/2,candidate.maximum_threads});
    if(cost.samples<2){result.reason=RegionExecutionReason::unknown;return result;}
    if(first<minimum_work_ms || inner<2){result.reason=RegionExecutionReason::too_small;return result;}
    const bool same=selected && *selected==candidate.key;
    if(first<(same?1.5:2.)*second){result.reason=RegionExecutionReason::competing;return result;}
    if(same && selected_intervals>=probe_interval) {
      result.reason=RegionExecutionReason::serial_probe;result.probe_region=largest;return result;
    }
    result.parallel_region=largest;result.inner_threads=inner;result.reason=RegionExecutionReason::parallel;return result;
  }
  // Called only after the complete regional result and its audits succeed.
  // Parallel observations do not pretend to be serial costs; an occasional
  // original serial pass refreshes that sample. Paused cells never age a probe.
  void committed(std::span<const RegionExecutionWork> work,std::span<const double> elapsed_ms,
                 const RegionExecutionDecision& decision) {
    if(work.size()!=elapsed_ms.size())throw std::invalid_argument("Regional execution timing coverage differs");
    for(std::size_t i=0;i<work.size();++i)
      if(work[i].seconds>0 && (!std::isfinite(elapsed_ms[i]) || elapsed_ms[i]<0))return;
    for(std::size_t i=0;i<work.size();++i) {
      if(work[i].seconds==0 || decision.parallel_region==i)continue;
      const double rate=elapsed_ms[i]/work[i].seconds;
      if(!std::isfinite(rate) || rate<0)continue;
      auto& cost=costs[work[i].key];
      cost.serial_ms_per_second=cost.samples?.5*cost.serial_ms_per_second+.5*rate:rate;
      cost.samples=std::min(32U,cost.samples+1);
    }
    if(decision.parallel_region) {
      const auto& key=work[*decision.parallel_region].key;
      selected_intervals=selected && *selected==key?selected_intervals+1:1;selected=key;
    } else if(decision.probe_region) {
      selected=work[*decision.probe_region].key;selected_intervals=0;
    } else if(selected) {
      for(const auto& item:work)if(item.key==*selected && item.seconds>0){selected.reset();selected_intervals=0;break;}
    }
  }
};
} // namespace ma::experimental::game_kernel
