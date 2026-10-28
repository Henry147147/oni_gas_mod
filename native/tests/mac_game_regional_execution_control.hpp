#pragma once
#include "../src/mac_game/regional_tick.hpp"
#include <bit>
#include <iostream>

namespace ma::testing {
inline void regional_execution_control() {
  namespace k=ma::experimental::game_kernel;
  using ma::experimental::MacGrid;
  const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
  // Pool infrastructure errors escape a coordinator thread through the caller,
  // and the other work is joined even when both paths fail.
  for(const bool main_fails:{false,true}) {
    bool main_ran=false,residual_ran=false;std::string message;
    try{k::run_regional_pair([&]{main_ran=true;if(main_fails)throw std::runtime_error("main failure");},
      [&]{residual_ran=true;throw std::runtime_error("pool infrastructure failure");});}
    catch(const std::runtime_error& error){message=error.what();}
    check(main_ran && residual_ran && message=="pool infrastructure failure","Coordinator infrastructure failure terminated or escaped before join");
  }
  {bool joined=false;std::string message;
    try{k::run_regional_pair([]{throw std::runtime_error("main failure");},[&]{joined=true;});}
    catch(const std::runtime_error& error){message=error.what();}
    check(joined && message=="main failure","Main failure skipped the residual join");}
  const k::RegionExecutionKey left{1,8192,7},right{2,8192,11};
  std::vector<k::RegionExecutionWork> work{{left,.2,8},{right,.2,8}};
  const std::array<double,2> serial{100,10},parallel{40,10};
  k::RegionalExecutionHistory h;
  check(!h.plan(work,12).parallel_region,"Unknown execution costs selected inner workers");
  h.committed(work,serial,{});check(!h.plan(work,12).parallel_region,"One timing sample selected inner workers");
  h.committed(work,serial,{});
  for(const unsigned workers:{1U,2U})check(!h.plan(work,workers).parallel_region,"Small worker budget changed original dispatch");
  check(h.plan(work,4).inner_threads==2 && h.plan(work,12).inner_threads==4,"Regional inner budget is not bounded");
  auto decision=h.plan(work,12);check(decision.parallel_region==0,"Dominant expensive region did not select inner workers");
  h.committed(work,parallel,decision);
  h.costs[right].serial_ms_per_second=60/.2;
  check(h.plan(work,12).parallel_region==0,"Execution hysteresis dropped a still-dominant region");
  h.costs[right].serial_ms_per_second=70/.2;
  check(!h.plan(work,12).parallel_region,"Competing regions retained inner workers");
  h.costs[right].serial_ms_per_second=10/.2;
  while(h.selected_intervals<k::RegionalExecutionHistory::probe_interval)h.committed(work,parallel,h.plan(work,12));
  auto held_work=work;for(auto& item:held_work)item.seconds=0;
  const auto age=h.selected_intervals;h.committed(held_work,std::array<double,2>{0,0},h.plan(held_work,12));
  check(h.selected_intervals==age && !h.plan(held_work,12).probe_region,"Paused region aged or ran a serial probe");
  decision=h.plan(work,12);check(decision.probe_region==0 && !decision.parallel_region,"Selected region did not periodically refresh its serial cost");
  h.committed(work,serial,decision);check(h.selected_intervals==0 && h.plan(work,12).parallel_region==0,"Serial probe did not resume bounded dispatch");
  h.costs[left].serial_ms_per_second=19/.2;h.costs[right].serial_ms_per_second=1/.2;
  check(!h.plan(work,12).parallel_region,"Sub-20ms work entered inner parallelism");
  h.costs[left].serial_ms_per_second=100/.2;
  auto one_block=work;one_block[0].maximum_threads=1;check(!h.plan(one_block,12).parallel_region,"One-block region created idle inner workers");
  const auto before=h.costs[left];h.committed(work,std::array<double,2>{1,std::numeric_limits<double>::quiet_NaN()},{});
  check(h.costs[left].serial_ms_per_second==before.serial_ms_per_second && h.costs[left].samples==before.samples,"Invalid elapsed samples partially changed execution history");
  const std::array<k::RegionExecutionKey,1> retained{right};h.retain(retained);
  check(h.costs.size()==1 && !h.selected,"Split/merged membership retained an old execution selection");

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
    if(workers>2)check(history->selected==keys[0],"Controlled dominant region did not use the parallel Tick");
    const auto next=tick->advance(first,dt,0,1e-11).state;
    check(same(next,next_expected),"Warm regional execution changed continuation");
    auto cold=make_tick(grid,workers);check(same(cold->advance(first,dt,0,1e-11).state,next_expected),"Unsaved execution history changed cold continuation");
    const auto cost=history->costs.at(keys[0]);const auto selected=history->selected;const auto selected_age=history->selected_intervals;
    bool rejected=false;try{(void)tick->advance(initial,dt,0,1e-11,{},0);}catch(const std::exception&){rejected=true;}
    check(rejected && history->costs.at(keys[0]).samples==cost.samples && history->costs.at(keys[0]).serial_ms_per_second==cost.serial_ms_per_second &&
      history->selected==selected && history->selected_intervals==selected_age,"Failed regional work poisoned execution history");
    std::vector<double> due(grid.walls.size(),dt);
    for(std::size_t i=0;i<due.size();++i)if(i%grid.width<32)due[i]=0;
    auto dormant=make_tick(grid,1);const auto held=dormant->advance(first,dt,0,1e-11,{},65536,due).state;
    check(same(tick->advance(first,dt,0,1e-11,{},65536,due).state,held),"Scheduled sleep changed held or due physical fields");
    auto awake=make_tick(grid,1);const auto awake_expected=awake->advance(held,dt,0,1e-11).state;
    check(same(tick->advance(held,dt,0,1e-11).state,awake_expected),"Wake changed physical arithmetic or caught up time");
    auto split=grid;for(std::size_t y=0;y<split.height;++y)split.walls[16+y*split.width]=1;
    const auto unaffected=keys[1];auto split_tick=make_tick(split,workers,history);
    check(history->costs.contains(unaffected) && !history->costs.contains(keys[0]),"Topology rebuild lost unaffected cost or reused split cost");
    auto split_serial=make_tick(split,1);const auto split_state=state_of(split);
    check(same(split_tick->advance(split_state,dt,0,1e-11).state,split_serial->advance(split_state,dt,0,1e-11).state),"Topology remap changed bounded regional arithmetic");
  }
  std::cout<<"PASS bounded regional execution: measured-cost gates, hysteresis, serial probes, budgets, workers1/4/8/12, sleep/wake, errors, topology and cold physical bytes\n";
}
} // namespace ma::testing
