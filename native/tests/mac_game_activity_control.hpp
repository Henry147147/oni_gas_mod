#pragma once
#include "mac_fixture.hpp"
#include "../src/mac_game_bridge.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <bit>
#include <iostream>

namespace ma::testing {
inline void game_activity_control() {
  using namespace ma::experimental;
  const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
  const auto& f=setup();
  const auto physical=[](const MacClosedWorld& a,const MacClosedWorld& b) {
    const auto bits=[](double u,double v){return std::bit_cast<std::uint64_t>(u)==std::bit_cast<std::uint64_t>(v);};
    const auto field=[&](const auto& u,const auto& v){
      if(u.size()!=v.size())return false;
      for(std::size_t i=0;i<u.size();++i)if(!bits(u[i],v[i]))return false;
      return true;
    };
    const auto& x=a.state();const auto& y=b.state();
    if(x.grid.width!=y.grid.width || x.grid.height!=y.grid.height ||
        !bits(x.grid.spacing_m,y.grid.spacing_m) || !bits(x.grid.depth_m,y.grid.depth_m) ||
        x.grid.periodic_x!=y.grid.periodic_x || x.grid.periodic_y!=y.grid.periodic_y ||
        x.grid.closed_x!=y.grid.closed_x || x.grid.closed_y!=y.grid.closed_y ||
        x.grid.walls!=y.grid.walls || !field(x.momentum_x,y.momentum_x) || !field(x.momentum_y,y.momentum_y) ||
        !field(x.occupied_volumes_m3,y.occupied_volumes_m3) || x.cells.size()!=y.cells.size() ||
        !x.full_momentum_x.empty() || !y.full_momentum_x.empty() || !x.full_momentum_y.empty() || !y.full_momentum_y.empty())return false;
    for(std::size_t i=0;i<x.cells.size();++i) {
      if(!field(x.cells[i].mixture.masses,y.cells[i].mixture.masses) || !bits(x.cells[i].mixture.energy,y.cells[i].mixture.energy) ||
          !bits(x.cells[i].energy_remainder_j.projected,y.cells[i].energy_remainder_j.projected) ||
          !x.cells[i].energy_remainder_j.full.is_zero() || !y.cells[i].energy_remainder_j.full.is_zero() ||
          x.cells[i].passive_quantities.size()!=y.cells[i].passive_quantities.size())return false;
      for(std::size_t j=0;j<x.cells[i].passive_quantities.size();++j) {
        const auto& p=x.cells[i].passive_quantities[j];const auto& q=y.cells[i].passive_quantities[j];
        if(p.identity!=q.identity || !bits(p.quantity,q.quantity))return false;
      }
    }
    return true;
  };
  auto state=f.state(13,6,false);
  state.momentum_x.front()=-0.;
  for(std::size_t i=0;i<state.cells.size();++i) {
    const auto x=i%13,y=i/13;
    state.grid.walls[i]=x==0||x==4||x==8||x==12||y==0||y==5;
    if(state.grid.walls[i]){state.cells[i].mixture=MixtureInventory(f.registry.size());state.cells[i].passive_quantities.clear();}
  }
  std::vector<double> temperatures(state.cells.size());
  for(std::size_t i=0;i<temperatures.size();++i)temperatures[i]=290+static_cast<double>(i%13);
  MacClosedSettings cfg;cfg.numerical_kernel=MacNumericalKernel::game;
  cfg.game_profile=MacGameProfile::game_kernel_activity_tiers;cfg.vacuum=MacVacuumRepresentation::void_phase;
  cfg.maximum_timestep_s=static_cast<double>(.1f);cfg.game.liquid_law=MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations=400;cfg.game.heat_maximum_iterations=800;
  cfg.trace_sink_threshold_kg=1e-11;cfg.game.gas_heat_mixing=8;cfg.thermal_conduction=true;
  auto old_cfg=cfg;old_cfg.game_profile=MacGameProfile::game_kernel_carried_momentum;
  auto old=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,old_cfg);
  auto always=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,cfg);
  const double frame=static_cast<double>(.2f);
  for(unsigned n=0;n<3;++n) {
    old=old.advanced(frame).world;always=always.advanced(frame).world;
    check(physical(old,always),"Profile9 Always simulate changed profile8 physical bytes");
  }
  check(always.checkpoint()[8]==28,"Profile9 did not write checkpoint28");
  auto adaptive=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,cfg).with_game_activity_tiers(true);
  check(std::signbit(adaptive.state().momentum_x.front()),"Activity physical-byte control lost its signed-zero input");
  const auto still=adaptive.advanced(1);
  check(physical(adaptive,still.world) && still.report.game_stages.pressure_attempts==0 &&
        still.report.phase_change_stages==0 && still.report.trace_sink.constituents==0,
        "Pristine activity region evolved or entered a trace/phase stage");
  const std::size_t left=1+13,middle=5+13,right=9+13;
  std::vector<std::uint8_t> actors(state.cells.size()),touched(state.cells.size());actors[left]=1;
  auto active=adaptive.with_game_activity({},actors);
  const auto due=active.game_activity_steps(frame);
  check(due[left]==frame && due[middle]==0 && due[right]==0,"Actor activity did not select its complete region");
  const auto moved=active.advanced(frame).world;
  check(!physical(active,moved),"Active gas-gradient control did not evolve");
  check(moved.state().cells[middle].mixture.energy==active.state().cells[middle].mixture.energy,
        "Actor activity leaked into a solid-separated pristine region");
  // Short hold for this explicit cadence fixture; saved policy defaults remain
  // two seconds / sixty seconds, and every fixed physics gate is unchanged.
  auto cadence_cfg=cfg;cadence_cfg.game.activity_tiers=true;cadence_cfg.game.activity_hold_s=.2;
  auto cadence=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,cadence_cfg);
  touched[middle]=touched[right]=1;
  cadence=cadence.with_game_activity(touched,actors);
  cadence=cadence.advanced(frame).world;
  bool staggered=false,held=false;
  for(unsigned n=0;n<14;++n) {
    const auto scheduled=cadence.game_activity_steps(.1);
    staggered=staggered || ((scheduled[middle]>0)!=(scheduled[right]>0));
    held=held || cadence.game_activity()[middle].pending_s>0;
    const auto image=cadence.checkpoint();
    auto cold=MacClosedWorld::from_checkpoint(f.registry,f.eos,image);
    check(cold.checkpoint()==image && cold.advanced(.1).world.checkpoint()==cadence.advanced(.1).world.checkpoint(),
          "Activity pending clocks changed cold continuation");
    cadence=cadence.advanced(.1).world;
  }
  check(staggered && held,"Remote regions did not retain and stagger their fractional clocks");
  const auto saved=cadence.checkpoint();
  const auto records=saved.size()-8-25*state.cells.size();
  const auto corrupt=[&](std::size_t offset,std::uint64_t bits,unsigned bytes,std::size_t other=0,std::uint64_t other_bits=0) {
    auto image=saved;for(unsigned b=0;b<bytes;++b)image[offset+b]=static_cast<std::uint8_t>(bits>>(8*b));
    if(other)for(unsigned b=0;b<8;++b)image[other+b]=static_cast<std::uint8_t>(other_bits>>(8*b));
    std::uint64_t h=14695981039346656037ULL;
    for(std::size_t n=0;n+8<image.size();++n)h=(h^image[n])*1099511628211ULL;
    for(unsigned b=0;b<8;++b)image[image.size()-8+b]=static_cast<std::uint8_t>(h>>(8*b));
    bool refused=false;try{(void)MacClosedWorld::from_checkpoint(f.registry,f.eos,image);}catch(const std::exception&){refused=true;}
    check(refused,"Saved activity admitted an invalid actor flag or clock");
  };
  corrupt(records+25*middle+24,2,1);
  corrupt(records+25*middle,std::bit_cast<std::uint64_t>(cadence.time_s()+1),8);
  corrupt(records+25*middle+8,std::bit_cast<std::uint64_t>(2.),8);
  corrupt(records+25*middle+16,std::bit_cast<std::uint64_t>(cadence.time_s()+1000),8);
  corrupt(records+25*middle+8,std::bit_cast<std::uint64_t>(.7),8,records+25*middle+16,0);
  corrupt(records+25*middle+16,std::bit_cast<std::uint64_t>(cadence.time_s()-.1),8);
  corrupt(records+25*middle+24,1,1);
  const auto query=cadence.game_activity_steps(3);
  const double pending=cadence.game_activity()[middle].pending_s;
  const auto long_step=cadence.advanced(3).world;
  check(std::abs(query[middle]-(3+pending-long_step.game_activity()[middle].pending_s))<1e-12,
        "Activity query omitted remote work through maximum-timestep substeps");
  touched.assign(touched.size(),0);touched[middle]=1;
  const auto awake=cadence.with_game_activity(touched,actors);
  check(awake.game_activity_steps(frame)[middle]==frame &&
        awake.advanced(frame).world.game_activity()[middle].pending_s==0,
        "Activity wake caught up old remote pending time");
  // A saved short idle deadline checks the transition without a long gate.
  auto idle_cfg=cadence_cfg;idle_cfg.game.activity_pause_s=1;
  auto idle=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,idle_cfg);
  idle=idle.with_game_activity(touched,{}).advanced(2).world;
  check(idle.game_activity_steps(frame)[middle]==0 && idle.game_activity()[middle].pending_s==0,
        "Long-idle activity region did not pause without debt");
  idle=idle.with_game_activity(touched,{});
  check(idle.game_activity_steps(frame)[middle]==frame,"Paused region did not wake for input");
  // Empty connected chambers let a cell close without changing an inventory:
  // a split retains the original phase, and a touched merge wakes both sides.
  auto empty_state=state;
  for(auto& c:empty_state.cells){c.mixture=MixtureInventory(f.registry.size());c.passive_quantities.clear();}
  const std::size_t bridge=4+2*13;empty_state.grid.walls[bridge]=0;
  auto split=MacClosedWorld::game_colony(f.registry,f.eos,empty_state,temperatures,cadence_cfg);
  touched.assign(touched.size(),0);touched[left]=1;
  split=split.with_game_activity(touched,{}).advanced(.3).world;
  split=split.advanced(.1).world;
  const auto phase=split.game_activity()[middle];
  split=split.with_closed_cells(std::span(&bridge,1));
  check(split.game_activity()[middle]==phase && split.game_activity()[left]==phase,
        "Region split lost per-cell activity phase");
  split=split.with_opened_cells(std::span(&bridge,1));touched.assign(touched.size(),0);touched[bridge]=1;
  split=split.with_game_activity(touched,{});
  const auto joined=split.game_activity_steps(frame);
  check(joined[left]==frame && joined[middle]==frame && joined[right]==0,
        "Touched topology merge failed to wake the joined regions");
  const auto options=mac_kernel_default_options();auto workers=options;workers.threads=1;set_mac_kernel_default_options(workers);
  const auto serial=cadence.advanced(2).world.checkpoint();workers.threads=4;set_mac_kernel_default_options(workers);
  check(cadence.advanced(2).world.checkpoint()==serial,"Activity schedule depends on worker count");
  set_mac_kernel_default_options(options);
  const auto unchanged=cadence.checkpoint();bool refused=false;
  try{(void)cadence.advanced(.2,1);}catch(const std::exception&){refused=true;}
  check(refused && cadence.checkpoint()==unchanged,"Rejected activity advance committed clocks or physics");
  auto tiny_cfg=cadence_cfg;tiny_cfg.maximum_timestep_s=1e-12;
  const auto tiny=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,tiny_cfg);
  refused=false;try{(void)tiny.game_activity_steps(.2);}catch(const std::exception&){refused=true;}
  check(refused,"Paused activity query admitted an unbounded scheduling loop");
  refused=false;try{(void)tiny.advanced(.2);}catch(const std::exception&){refused=true;}
  check(refused,"Paused activity advance admitted an unbounded scheduling loop");
  auto enormous=adaptive.checkpoint();std::uint32_t model=0;
  for(unsigned b=0;b<4;++b)model|=std::uint32_t(enormous[12+b])<<(8*b);
  const auto time_at=16+static_cast<std::size_t>(model);
  const auto time_bits=std::bit_cast<std::uint64_t>(0x1p53);
  for(unsigned b=0;b<8;++b)enormous[time_at+b]=static_cast<std::uint8_t>(time_bits>>(8*b));
  std::uint64_t h=14695981039346656037ULL;
  for(std::size_t n=0;n+8<enormous.size();++n)h=(h^enormous[n])*1099511628211ULL;
  for(unsigned b=0;b<8;++b)enormous[enormous.size()-8+b]=static_cast<std::uint8_t>(h>>(8*b));
  const auto ancient=MacClosedWorld::from_checkpoint(f.registry,f.eos,enormous);
  refused=false;try{(void)ancient.game_activity_steps(.2);}catch(const std::exception&){refused=true;}
  check(refused,"Activity query admitted a non-progressing binary64 clock");
  refused=false;try{(void)ancient.advanced(.2);}catch(const std::exception&){refused=true;}
  check(refused,"Activity advance admitted a non-progressing binary64 clock");
  auto periodic=state;periodic.grid.periodic_y=true;
  refused=false;try{(void)MacClosedWorld::game_colony(f.registry,f.eos,periodic,temperatures,cadence_cfg);}catch(const std::exception&){refused=true;}
  check(refused,"Activity profile admitted an unsupported periodic grid");
  std::cout<<"PASS experimental profile9 pristine pause, actor/input wakes, staggered remote clocks, generic due query, split/merge, Always simulate profile8 equality, worker/cold continuation and refusal\n";
}
} // namespace ma::testing
