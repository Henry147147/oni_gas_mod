#pragma once
#include "../src/mac_game/regional_tick.hpp"
#include "../src/mac_kernel_detail.hpp"
#include <bit>
#include <array>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <set>
#include <iostream>

namespace ma::testing {
inline void regional_execution_control() {
  namespace k=ma::experimental::game_kernel;
  using ma::experimental::MacGrid;
  const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
  // The pool itself: a caller whose own blocks are done sleeps until the
  // last block of another thread is done, however long that takes, and wakes
  // for every one of many runs whose blocks end around the moment it falls
  // asleep. Pools nest as the coordinators and the regions' workers do.
  {using ma::experimental::mac_kernel::Pool;
    Pool pool(8);
    for(int round=0;round<3;++round) {
      std::vector<std::atomic<unsigned>> ran(24);std::atomic<bool> long_done{false};
      pool.run(ran.size(),[&](std::size_t block) {
        if(block==5){std::this_thread::sleep_for(std::chrono::milliseconds(30));long_done=true;}
        ++ran[block];
      });
      bool once=true;for(const auto& count:ran)once=once&&count.load()==1;
      check(once && long_done.load(),"A pool run returned before its longest block or ran a block twice");
    }
    std::atomic<std::uint64_t> total{0};std::uint64_t expected=0;
    for(unsigned round=0;round<4000;++round) {
      const std::size_t blocks=2+round%11;expected+=blocks;
      pool.run(blocks,[&](std::size_t block) {
        if((round+block)%13==0)std::this_thread::sleep_for(std::chrono::microseconds(50+(round*37)%400));
        total.fetch_add(1,std::memory_order_relaxed);
      });
      check(total.load()==expected,"A pool run returned with blocks still running");
    }
    std::string message;
    try{pool.run(16,[&](std::size_t block) {
      if(block==3){std::this_thread::sleep_for(std::chrono::milliseconds(5));throw std::runtime_error("late failure");}
      if(block==9)throw std::runtime_error("early failure");
    });}catch(const std::runtime_error& error){message=error.what();}
    check(message=="late failure","A sleeping caller lost the lowest failing block");
    Pool outer(3);std::array<std::unique_ptr<Pool>,3> inner;for(auto& p:inner)p=std::make_unique<Pool>(3);
    std::array<std::atomic<unsigned>,3> sums{};
    for(unsigned round=0;round<300;++round)
      outer.run(3,[&](std::size_t job){inner[job]->run(5+job,[&](std::size_t block){
        if(job==2 && block==0 && round%50==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        sums[job].fetch_add(1,std::memory_order_relaxed);});});
    check(sums[0]==300*5 && sums[1]==300*6 && sums[2]==300*7,"Nested pools lost or repeated blocks");}
  const k::RegionExecutionKey left{1,8192,7},right{2,8192,11},third{3,8192,13};
  std::vector<k::RegionExecutionWork> work{{left,.2,8},{right,.2,8}};
  const std::array<double,2> serial{100,10},parallel{40,10};
  using History=k::RegionalExecutionHistory;
  const auto sound=[&](const k::RegionExecutionDecision& d,std::span<const k::RegionExecutionWork> items,unsigned budget) {
    unsigned used=d.residual_threads;std::set<std::size_t> seen;
    for(const auto& share:d.parallel) {
      if(share.threads<2 || share.threads>History::inner_limit() || share.threads>items[share.region].maximum_threads || !seen.insert(share.region).second)return false;
      used+=share.threads;
    }
    return d.parallel.empty() || (used<=budget && (d.parallel.size()==items.size() || d.residual_threads>=1));
  };
  History h;
  check(h.plan(work,12).parallel.empty(),"Unknown execution costs selected inner workers");
  h.committed(work,serial,{});check(h.plan(work,12).parallel.empty(),"One timing sample selected inner workers");
  h.committed(work,serial,{});
  for(const unsigned workers:{1U,2U})check(h.plan(work,workers).parallel.empty(),"Small worker budget changed original dispatch");
  const unsigned alone=std::min(History::inner_limit(),11U);
  check(h.plan(work,3).threads_of(0)==2 && h.plan(work,3).residual_threads==1 && h.plan(work,4).threads_of(0)==3 &&
    h.plan(work,12).threads_of(0)==alone && h.plan(work,12).residual_threads==12-alone,"Regional inner budget is not bounded");
  auto decision=h.plan(work,12);check(decision.parallel.size()==1 && decision.parallel[0].region==0 && sound(decision,work,12),"Expensive region did not select inner workers");
  h.committed(work,parallel,decision);check(h.selected.contains(left) && !h.selected.contains(right),"Committed dispatch was not remembered");
  // A second expensive region no longer takes the first one's workers away:
  // both get a set of their own, and the budget is divided between them by
  // their work: the larger of the two works per worker is as small as any
  // division of the budget makes it, and the costlier region gets at least
  // as many workers.
  h.costs[right].serial_ms_per_second=70/.2;
  for(const unsigned budget:{4U,5U,6U,8U,12U,16U,32U}) {
    const auto both=h.plan(work,budget);
    const unsigned a=both.threads_of(0),b=both.threads_of(1),cap=std::min(History::inner_limit(),8U);
    double best=1e300;
    for(unsigned x=2;x<=cap;++x)for(unsigned y=2;y<=cap && x+y<=budget;++y)best=std::min(best,std::max(100./x,70./y));
    check(both.parallel.size()==2 && both.parallel[0].region==0 && both.parallel[1].region==1 && sound(both,work,budget) &&
      a>=b && a+b==std::min(budget,2*cap) && std::max(100./a,70./b)==best,"Two expensive regions did not divide the workers by their work");
  }
  check(h.plan(work,3).parallel.size()==1 && h.plan(work,3).parallel[0].region==0 && sound(h.plan(work,3),work,3),"A budget of three did not keep the residual worker");
  // Three expensive regions and one cheap one: the cheapest expensive region
  // waits when the budget cannot give each two workers and leave one over.
  {History many;std::vector<k::RegionExecutionWork> four{{left,.2,8},{right,.2,8},{third,.2,3},{k::RegionExecutionKey{4,64,17},.2,8}};
    many.costs[left]={90/.2,2};many.costs[right]={120/.2,2};many.costs[third]={60/.2,2};many.costs[four[3].key]={1/.2,2};
    for(unsigned budget=3;budget<=40;++budget) {
      const auto d=many.plan(four,budget);
      check(sound(d,four,budget) && !d.parallel.empty() && d.parallel[0].region==1 && d.threads_of(3)==0,"Several expensive regions broke the worker budget");
      check(d.parallel.size()==std::min<std::size_t>(3,(budget-1)/2),"Expensive regions were not admitted costliest first");
      if(d.parallel.size()==3)check(d.threads_of(2)>=2 && d.threads_of(2)<=3 && d.threads_of(1)>=d.threads_of(0),"A region received more workers than it has row blocks");
    }
    auto all=four;all.pop_back();const auto d=many.plan(all,6);
    check(d.parallel.size()==3 && d.residual_threads==0 && sound(d,all,6),"Regions without a residual did not use the whole budget");}
  h.costs[right].serial_ms_per_second=10/.2;
  while(h.selected.at(left)<History::probe_interval)h.committed(work,parallel,h.plan(work,12));
  auto held_work=work;for(auto& item:held_work)item.seconds=0;
  const auto age=h.selected.at(left);h.committed(held_work,std::array<double,2>{0,0},h.plan(held_work,12));
  check(h.selected.at(left)==age && !h.plan(held_work,12).probe_region,"Paused region aged or ran a serial probe");
  decision=h.plan(work,12);check(decision.probe_region==0 && decision.parallel.empty(),"Selected region did not periodically refresh its serial cost");
  h.committed(work,serial,decision);check(h.selected.at(left)==0 && h.plan(work,12).threads_of(0)==alone,"Serial probe did not resume bounded dispatch");
  // Two selected regions never probe in the same interval: the one that has
  // waited longer goes first and the other keeps its workers.
  {History two;two.costs[left]={100/.2,2};two.costs[right]={80/.2,2};
    two.selected[left]=History::probe_interval;two.selected[right]=History::probe_interval+3;
    auto d=two.plan(work,12);check(d.probe_region==1 && d.parallel.size()==1 && d.parallel[0].region==0,"Two regions probed together or out of turn");
    two.committed(work,std::array<double,2>{40,80},d);check(two.selected.at(right)==0 && two.selected.at(left)==History::probe_interval+1,"A probe did not reset only its own region");
    d=two.plan(work,12);check(d.probe_region==0 && d.threads_of(1)==alone,"The second region did not take its turn");}
  // Entry needs 20 ms of predicted serial work; a selected region stays down to 15 ms.
  h.costs[left].serial_ms_per_second=19/.2;h.costs[right].serial_ms_per_second=1/.2;
  check(h.plan(work,12).threads_of(0)==alone,"A selected region lost its workers just below the entry threshold");
  {History fresh;fresh.costs[left]={19/.2,2};fresh.costs[right]={1/.2,2};check(fresh.plan(work,12).parallel.empty(),"Sub-20ms work entered inner parallelism");}
  h.costs[left].serial_ms_per_second=14/.2;check(h.plan(work,12).parallel.empty(),"Sub-15ms work kept inner workers");
  h.committed(work,serial,h.plan(work,12));check(!h.selected.contains(left),"A region that ran serially stayed selected");
  h.costs[left].serial_ms_per_second=100/.2;
  auto one_block=work;one_block[0].maximum_threads=1;check(h.plan(one_block,12).parallel.empty(),"One-block region created idle inner workers");
  const auto before=h.costs[left];h.committed(work,std::array<double,2>{1,std::numeric_limits<double>::quiet_NaN()},{});
  check(h.costs[left].serial_ms_per_second==before.serial_ms_per_second && h.costs[left].samples==before.samples,"Invalid elapsed samples partially changed execution history");
  h.committed(work,parallel,h.plan(work,12));check(h.selected.contains(left),"Dispatch was not selected again");
  {History resized=h;
    const k::RegionExecutionKey dug{left.representative,left.cells+1,17};
    resized.retain(std::array{dug,right});
    check(!resized.costs.contains(left) && resized.costs.at(dug).samples==before.samples &&
      resized.selected.at(dug)==h.selected.at(left),"One dug tile lost the region's worker history");
    const k::RegionExecutionKey split{dug.representative,dug.cells/2,19};
    resized.retain(std::array{split,right});
    check(!resized.costs.contains(split) && !resized.selected.contains(split),"A large split inherited the former region's workers");}
  const std::array<k::RegionExecutionKey,1> retained{right};h.retain(retained);
  check(h.costs.size()==1 && h.selected.empty(),"Split/merged membership retained an old execution selection");

  // Two independent large systems exercise actual component-parallel pressure,
  // transport, passives and a concurrent serial residual system. History is
  // supplied explicitly so this control never relies on host timing.
  MacGrid grid;grid.width=65;grid.height=256;grid.walls.resize(grid.width*grid.height);
  for(std::size_t y=0;y<grid.height;++y)grid.walls[32+y*grid.width]=1;
  std::vector<k::Species> species(1);species[0].molar_mass=.032;species[0].cv=700;species[0].conductivity=.02;
  const auto state_of=[&](const MacGrid& g) {
    k::State state;state.energy.resize(g.walls.size());state.jx.resize(g.x_width()*g.height);state.jy.resize(g.width*g.y_height());
    for(std::size_t i=0;i<g.walls.size();++i) {
      state.masses.offsets.push_back(state.masses.mass.size());state.passives.offsets.push_back(state.passives.mass.size());
      if(g.walls[i])continue;
      const double mass=1+.0002*static_cast<double>(i%g.width);
      state.masses.species.push_back(0);state.masses.mass.push_back(mass);
      state.energy[i]=mass*species[0].cv*(300+.005*static_cast<double>(i/g.width));
      state.passives.species.push_back(17);state.passives.mass.push_back(.125+.0001*static_cast<double>(i%7));
    }
    state.masses.offsets.push_back(state.masses.mass.size());state.passives.offsets.push_back(state.passives.mass.size());
    state.jx[g.x_face(5,100)]=.01;state.jy[g.y_face(8,128)]=-.02;return state;
  };
  const auto same=[](const k::State& a,const k::State& b) {
    const auto bits=[](const auto& x,const auto& y) {
      if(x.size()!=y.size())return false;
      for(std::size_t i=0;i<x.size();++i)if(std::bit_cast<std::uint64_t>(x[i])!=std::bit_cast<std::uint64_t>(y[i]))return false;
      return true;
    };
    return bits(a.energy,b.energy)&&bits(a.jx,b.jx)&&bits(a.jy,b.jy)&&bits(a.masses.mass,b.masses.mass)&&
      bits(a.passives.mass,b.passives.mass)&&a.masses.species==b.masses.species&&a.masses.offsets==b.masses.offsets&&
      a.passives.species==b.passives.species&&a.passives.offsets==b.passives.offsets;
  };
  ma::experimental::MacGameSettings cfg;cfg.flow_parts=2;cfg.gas_heat_mixing=8;cfg.pressure_maximum_iterations=400;cfg.heat_maximum_iterations=800;
  const auto make_tick=[&](const MacGrid& g,unsigned workers,std::shared_ptr<k::RegionalExecutionHistory> history={}) {
    return std::make_unique<k::RegionalTick>(g,species,std::vector<k::PhasePair>{},workers,cfg,true,true,true,1e-6,std::move(history));
  };
  const double dt=.02;const auto initial=state_of(grid);auto serial_tick=make_tick(grid,1);
  const auto expected=serial_tick->advance(initial,dt,0,1e-11).state;
  const auto next_expected=serial_tick->advance(expected,dt,0,1e-11).state;
  for(const unsigned workers:{1U,4U,8U,12U}) {
    auto history=std::make_shared<k::RegionalExecutionHistory>();auto tick=make_tick(grid,workers,history);
    const auto keys=tick->execution_region_keys();check(keys.size()==2,"Regional execution control lost independent systems");
    history->costs[keys[0]]={100/dt,2};history->costs[keys[1]]={1/dt,2};
    const auto first=tick->advance(initial,dt,0,1e-11).state;
    check(same(first,expected),"Bounded regional workers changed physical binary64 fields");
    if(workers>2)check(history->selected.contains(keys[0]) && !history->selected.contains(keys[1]),"Controlled expensive region did not use the parallel Tick");
    const auto next=tick->advance(first,dt,0,1e-11).state;
    check(same(next,next_expected),"Warm regional execution changed continuation");
    auto cold=make_tick(grid,workers);check(same(cold->advance(first,dt,0,1e-11).state,next_expected),"Unsaved execution history changed cold continuation");
    const auto cost=history->costs.at(keys[0]);const auto selected=history->selected;
    bool rejected=false;try{(void)tick->advance(initial,dt,0,1e-11,{},0);}catch(const std::exception&){rejected=true;}
    check(rejected && history->costs.at(keys[0]).samples==cost.samples && history->costs.at(keys[0]).serial_ms_per_second==cost.serial_ms_per_second &&
      history->selected==selected,"Failed regional work poisoned execution history");
    std::vector<double> due(grid.walls.size(),dt);
    for(std::size_t i=0;i<due.size();++i)if(i%grid.width<32)due[i]=0;
    auto dormant=make_tick(grid,1);const auto held=dormant->advance(first,dt,0,1e-11,{},65536,due).state;
    check(same(tick->advance(first,dt,0,1e-11,{},65536,due).state,held),"Scheduled sleep changed held or due physical fields");
    auto awake=make_tick(grid,1);const auto awake_expected=awake->advance(held,dt,0,1e-11).state;
    check(same(tick->advance(held,dt,0,1e-11).state,awake_expected),"Wake changed physical arithmetic or caught up time");
    auto split=grid;for(std::size_t y=0;y<split.height;++y)split.walls[16+y*split.width]=1;
    const auto unaffected=keys[1];auto split_tick=make_tick(split,workers,history);
    check(history->costs.contains(unaffected) && !history->costs.contains(keys[0]),"Topology rebuild lost unaffected cost or reused split cost");
    split_tick->reuse_workspaces(*tick);
    auto split_serial=make_tick(split,1);const auto split_state=state_of(split);
    check(same(split_tick->advance(split_state,dt,0,1e-11).state,split_serial->advance(split_state,dt,0,1e-11).state),"Topology remap changed bounded regional arithmetic");
  }
  // Several regions with workers of their own at once: three systems, each
  // costly by supplied history, every budget, warm and cold, against the
  // single-worker result bit for bit. A fourth, cheap system (one column)
  // keeps the residual pool busy beside them.
  {auto wide=grid;wide.width=101;wide.walls.assign(wide.width*wide.height,0);
    for(std::size_t y=0;y<wide.height;++y)for(const std::size_t x:{32UL,65UL,98UL})wide.walls[x+y*wide.width]=1;
    const auto begin=state_of(wide);auto reference=make_tick(wide,1);
    const auto first_expected=reference->advance(begin,dt,0,1e-11).state;
    const auto second_expected=reference->advance(first_expected,dt,0,1e-11).state;
    const auto third_expected=reference->advance(second_expected,dt,0,1e-11).state;
    for(const unsigned workers:{3U,4U,5U,7U,8U,12U,16U}) {
      auto history=std::make_shared<k::RegionalExecutionHistory>();auto tick=make_tick(wide,workers,history);
      const auto keys=tick->execution_region_keys();check(keys.size()==4,"Several-region control lost its systems");
      const auto pin=[&]{for(std::size_t i=0;i<3;++i)history->costs[keys[i]]={(100.-10.*static_cast<double>(i))/dt,2};history->costs[keys[3]]={1/dt,2};};
      pin();const auto first=tick->advance(begin,dt,0,1e-11).state;
      check(same(first,first_expected),"Several parallel regions changed physical binary64 fields");
      const auto expected_regions=std::min<std::size_t>(3,(workers-1)/2);
      std::size_t chosen=0;for(std::size_t i=0;i<3;++i)chosen+=history->selected.contains(keys[i]);
      check(chosen==expected_regions && !history->selected.contains(keys[3]),"Several-region control did not give each expensive region workers");
      pin();const auto second=tick->advance(first,dt,0,1e-11).state;
      check(same(second,second_expected),"Warm several-region execution changed continuation");
      // One region loses its workers (its cost falls away), another is probed.
      pin();history->costs[keys[0]]={1/dt,2};history->selected[keys[1]]=k::RegionalExecutionHistory::probe_interval;
      check(same(tick->advance(second,dt,0,1e-11).state,third_expected),"Changing the set of parallel regions changed physical arithmetic");
      if(workers>=5)check(!history->selected.contains(keys[0]) && history->selected.at(keys[1])==0,"A dropped or probed region kept its dispatch state");
      // Half the systems asleep while the others keep their workers.
      std::vector<double> due(wide.walls.size(),dt);
      for(std::size_t i=0;i<due.size();++i)if(i%wide.width<32 || i%wide.width>65)due[i]=0;
      auto dormant=make_tick(wide,1);pin();
      check(same(tick->advance(first,dt,0,1e-11,{},65536,due).state,dormant->advance(first,dt,0,1e-11,{},65536,due).state),"Sleeping systems beside parallel regions changed physical fields");
      // A failure inside the interval leaves the history as it was.
      pin();const auto selected=history->selected;const auto costs=history->costs.at(keys[1]);
      bool rejected=false;try{(void)tick->advance(begin,dt,0,1e-11,{},0);}catch(const std::exception&){rejected=true;}
      check(rejected && history->selected==selected && history->costs.at(keys[1]).samples==costs.samples,"A failed several-region interval changed execution history");
      check(same(tick->advance(first,dt,0,1e-11).state,second_expected),"A failed interval changed the next one");
    }
    // Many intervals with the measured costs of this host deciding the
    // dispatch: whatever it chooses, the fields stay those of one worker.
    auto history=std::make_shared<k::RegionalExecutionHistory>();auto timed=make_tick(wide,8,history);auto serial_many=make_tick(wide,1);
    auto a=begin,b=begin;
    for(int interval=0;interval<40;++interval) {
      a=timed->advance(a,dt,0,1e-11).state;b=serial_many->advance(b,dt,0,1e-11).state;
      check(same(a,b),"Host-timed regional dispatch changed physical binary64 fields");
    }}
  std::cout<<"PASS bounded regional execution: sleeping pool callers and nested pools, measured-cost gates, a worker set for every expensive region, budgets divided by work, hysteresis, staggered serial probes, workers 1 to 16, sleep/wake, errors, topology and cold physical bytes\n";
}
} // namespace ma::testing
