#include "mac_fixture.hpp"
#include "../src/mac_game_bridge.hpp"
#include <array>
#include <cfenv>
#include <cstdlib>
#include <cstring>
#include <new>
#include <xmmintrin.h>

namespace allocation {
thread_local bool enabled=false;
thread_local std::size_t calls=0,fail_at=std::numeric_limits<std::size_t>::max();
void* get(std::size_t size) {
  if(enabled && calls++==fail_at){enabled=false;throw std::bad_alloc();}
  if(void* result=std::malloc(size?size:1))return result;
  throw std::bad_alloc();
}
void begin(std::size_t failure=std::numeric_limits<std::size_t>::max()){calls=0;fail_at=failure;enabled=true;}
std::size_t end(){enabled=false;return calls;}
}
void* operator new(std::size_t n){return allocation::get(n);}
void* operator new[](std::size_t n){return allocation::get(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

using namespace ma;using namespace ma::experimental;
void require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
struct Environment {
  int rounding=std::fegetround(),flags=std::fetestexcept(FE_ALL_EXCEPT);
  unsigned csr=_mm_getcsr();
  void same() const {require(rounding==std::fegetround() && flags==std::fetestexcept(FE_ALL_EXCEPT) && csr==_mm_getcsr(),"Detach changed FPU state");}
};
struct Metadata {
  const SpeciesRegistry* registry;
  const FluidThermodynamics* eos;
  const GasTransportProfile* transport;
  const MacClosedSettings* settings;
  const void *totals,*activity;
  std::uint64_t revision;
  double seconds;
  explicit Metadata(const MacClosedWorld& w):registry(&w.registry()),eos(&w.thermodynamics()),transport(&w.transport_profile()),settings(&w.settings()),totals(MacGameKernelAccess::heat_totals_identity(w)),activity(MacGameKernelAccess::heat_activity_identity(w)),revision(w.revision()),seconds(w.time_s()){}
  void same(const MacClosedWorld& w)const {
    require(registry==&w.registry() && eos==&w.thermodynamics() && transport==&w.transport_profile() && settings==&w.settings() && totals==MacGameKernelAccess::heat_totals_identity(w) && activity==MacGameKernelAccess::heat_activity_identity(w) && revision==w.revision() && std::bit_cast<std::uint64_t>(seconds)==std::bit_cast<std::uint64_t>(w.time_s()),"Detach changed world metadata/cache identity");
  }
};
MacClosedWorld make(bool wet,bool activity) {
  const auto& f=ma::testing::setup();MacClosedSettings cfg;cfg.numerical_kernel=MacNumericalKernel::game;cfg.game_profile=activity?MacGameProfile::game_kernel_activity_tiers:MacGameProfile::game_kernel_carried_momentum;cfg.vacuum=MacVacuumRepresentation::void_phase;cfg.game.liquid_law=MacGameLiquidLaw::positive_segments;cfg.game.activity_tiers=activity;cfg.gravity_m_per_s2=9.81;
  auto world=MacClosedWorld::game_colony(f.registry,f.eos,f.state(4,3,false,wet),std::vector<double>(12,300),cfg);
  if(activity){const std::vector<std::uint8_t> all(12,1);world=world.with_game_activity(all,all);}
  return world.advanced(.02).world;
}
int main(){try {
  std::size_t detach_failure_points=0,heat_failure_points=0;
  for(bool wet:{false,true})for(bool activity:{false,true}) {
    auto unique=make(wet,activity);const auto unique_image=unique.checkpoint();const auto* unique_state=&unique.state();const Metadata unique_metadata(unique);const Environment unique_environment;
    allocation::begin(0);const bool detached_unique=MacGameKernelAccess::detach_heat_state(unique);const auto unique_allocations=allocation::end();unique_environment.same();
    require(!detached_unique && unique_allocations==0 && &unique.state()==unique_state,"Unique preparation allocated or replaced state");unique_metadata.same(unique);require(unique.checkpoint()==unique_image,"Unique preparation changed physical bytes");

    const auto published=make(wet,activity);const auto retained=published;const auto image=published.checkpoint();const auto* published_state=&published.state();
    auto copy=published;const Metadata metadata(copy);const Environment environment;
    allocation::begin();const bool detached=MacGameKernelAccess::detach_heat_state(copy);const auto detach_allocations=allocation::end();environment.same();
    require(detached && detach_allocations>0 && &copy.state()!=published_state && &published.state()==published_state && &retained.state()==published_state,"Shared preparation failed to isolate immutable state");metadata.same(copy);
    require(copy.checkpoint()==image && published.checkpoint()==image && retained.checkpoint()==image,"Preparation changed source or immutable published bytes");
    for(std::size_t failure=0;failure<detach_allocations;++failure) {
      auto victim=published;const Metadata original(victim);const Environment fp;bool refused=false;
      allocation::begin(failure);try{(void)MacGameKernelAccess::detach_heat_state(victim);}catch(const std::bad_alloc&){refused=true;}(void)allocation::end();fp.same();
      require(refused && &victim.state()==published_state && &published.state()==published_state && &retained.state()==published_state,"Failed detach exposed a partial replacement");original.same(victim);
      require(victim.checkpoint()==image && published.checkpoint()==image && retained.checkpoint()==image,"Failed detach changed source/published bytes");++detach_failure_points;
    }

    const std::array<MacCellHeat,4> accepted{{{0,10,100000},{1,-.25,100000},{0,.5,100000},{2,0,1}}};
    const auto reference=published.with_cell_heat(accepted);auto target=published;(void)MacGameKernelAccess::detach_heat_state(target);const auto* isolated=&target.state();
    auto result=std::move(target).with_cell_heat(accepted);require(&result.state()==isolated && result.checkpoint()==reference.checkpoint() && published.checkpoint()==image && retained.checkpoint()==image,"Prepared heat differs from original const heat");

    auto failed=accepted;failed.back()={2,1,1};
    std::string original_error;try{(void)published.with_cell_heat(failed);}catch(const std::invalid_argument& e){original_error=e.what();}require(!original_error.empty(),"Invalid control accepted");
    auto counted=published;(void)MacGameKernelAccess::detach_heat_state(counted);std::string counted_error;
    allocation::begin();try{(void)std::move(counted).with_cell_heat(failed);}catch(const std::invalid_argument& e){(void)allocation::end();counted_error=e.what();}const auto heat_allocations=allocation::end();
    require(counted_error==original_error && counted.checkpoint()==image,"Prepared rejection changed error or physical bytes");
    for(std::size_t failure=0;failure<heat_allocations;++failure) {
      auto victim=published;(void)MacGameKernelAccess::detach_heat_state(victim);const auto* state=&victim.state();const Metadata original(victim);bool refused=false;
      allocation::begin(failure);try{(void)std::move(victim).with_cell_heat(failed);}catch(const std::exception&){refused=true;}(void)allocation::end();
      require(refused && &victim.state()==state,"Injected heat failure lost reusable isolated state");original.same(victim);
      require(victim.checkpoint()==image && published.checkpoint()==image && retained.checkpoint()==image,"Injected heat failure exposed partial source/published bytes");
      auto retried=std::move(victim).with_cell_heat(accepted);require(retried.checkpoint()==reference.checkpoint(),"Injected failure changed successful retry");++heat_failure_points;
    }
  }
  std::cout<<"PASS owner heat retry ownership: unique path zero allocations; "<<detach_failure_points<<" copy allocation failures and "<<heat_failure_points<<" heat allocation failures preserve complete bytes/metadata/cache/published originals; retry exact\n";return 0;
}catch(const std::exception& e){allocation::enabled=false;std::cerr<<"OWNER HEAT OWNERSHIP FAIL: "<<e.what()<<'\n';return 1;}}
