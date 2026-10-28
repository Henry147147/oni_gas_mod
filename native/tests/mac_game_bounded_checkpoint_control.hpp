#pragma once
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "../src/mac_game/regional_tick.hpp"

namespace ma::testing {
inline void regional_pressure_controls() {
  using namespace ma::experimental;
  namespace k=ma::experimental::game_kernel;
  const auto check=[](bool v,const char* message){if(!v)throw std::runtime_error(message);};
  const auto& f=setup();
  MacClosedSettings cfg;cfg.numerical_kernel=MacNumericalKernel::game;
  cfg.game_profile=MacGameProfile::game_kernel_bounded_pressure;
  cfg.vacuum=MacVacuumRepresentation::void_phase;cfg.game.liquid_law=MacGameLiquidLaw::positive_segments;
  cfg.game.pressure_maximum_iterations=400;cfg.game.heat_maximum_iterations=800;
  cfg.game.gas_heat_mixing=8;cfg.maximum_timestep_s=.2;cfg.trace_sink_threshold_kg=1e-6;cfg.gravity_m_per_s2=9.81;
  auto isolated=f.state(5,100,false);
  for(std::size_t i=0;i<isolated.cells.size();++i) {
    isolated.grid.walls[i]=1;isolated.cells[i].mixture=MixtureInventory(f.registry.size());isolated.cells[i].passive_quantities.clear();
  }
  const std::size_t at=2+80*5;
  isolated.grid.walls[at]=0;
  auto masses=std::vector<double>(f.registry.size());masses[f.registry.index("oxygen")]=1;masses[f.registry.index("helium")]=1e-7;
  isolated.cells[at].mixture=f.eos->initialize_at_temperature(masses,300,1).mixture;
  isolated.cells[at].passive_quantities={{77,.2}};
  auto world=MacClosedWorld::game_colony(f.registry,f.eos,isolated,std::vector<double>(500,300),cfg);
  const auto next=world.advanced(.2);
  check(next.report.trace_sink.constituents==1 && next.report.trace_sink.potential_energy_j.mantissa()>0,
        "Regional trace sink lost its global gravitational height");
  check(std::abs(next.report.energy_residual_j)<=next.report.energy_bound_j,
        "Regional shifted-origin energy account failed");
  check(MacClosedWorld::from_checkpoint(f.registry,f.eos,next.world.checkpoint()).advanced(.2).world.checkpoint()==next.world.advanced(.2).world.checkpoint(),
        "Shifted-origin regional trace cold continuation differs");

  auto caves=f.state(65,65,false);std::size_t count=0;
  for(std::size_t i=0;i<caves.cells.size();++i) {
    const bool open=(i%65)%2==1 && (i/65)%2==1;
    caves.grid.walls[i]=!open;
    if(!open){caves.cells[i].mixture=MixtureInventory(f.registry.size());caves.cells[i].passive_quantities.clear();}
    else ++count;
  }
  auto calm=MacClosedWorld::game_colony(f.registry,f.eos,caves,std::vector<double>(65*65,300),cfg);
  const auto step=calm.advanced(.2,2);
  check(count==1024 && step.report.attempts==2 && step.report.game_stages.pressure_attempts>=2*count,
        "Independent regions incorrectly multiply the temporal attempt budget");

  auto chambers=f.state(16,48,false);
  for(std::size_t x=0;x<16;++x) {
    const auto i=x+23*16;chambers.grid.walls[i]=1;
    chambers.cells[i].mixture=MixtureInventory(f.registry.size());chambers.cells[i].passive_quantities.clear();
  }
  for(const auto i:{4+8*16,12+32*16}) {
    chambers.cells[i].mixture=MixtureInventory(f.registry.size());chambers.cells[i].passive_quantities.clear();
  }
  chambers.grid.closed_x.assign(chambers.grid.x_width()*chambers.grid.height,0);
  chambers.grid.closed_x[chambers.grid.x_face(6,5)]=1;
  std::vector<double> temperatures(chambers.cells.size());
  for(std::size_t i=0;i<temperatures.size();++i)temperatures[i]=290+.3*static_cast<double>(i%16)+.2*static_cast<double>(i/16);
  cfg.thermal_conduction=true;
  const auto dynamic=MacClosedWorld::game_colony(f.registry,f.eos,chambers,temperatures,cfg);
  const auto original_options=mac_kernel_default_options();auto worker=original_options;worker.threads=1;set_mac_kernel_default_options(worker);
  const auto one=dynamic.advanced(.2).world;
  const auto two=one.advanced(.2).world.checkpoint();
  for(const unsigned threads:{2U,12U}) {
    worker.threads=threads;set_mac_kernel_default_options(worker);
    check(dynamic.advanced(.2).world.checkpoint()==one.checkpoint(),
          "Parallel independent moving regions changed checkpoint bytes");
    check(MacClosedWorld::from_checkpoint(f.registry,f.eos,one.checkpoint()).advanced(.2).world.checkpoint()==two,
          "Parallel independent moving regions changed cold continuation");
  }
  set_mac_kernel_default_options(original_options);

  MacGrid grid;grid.width=12;grid.height=28;grid.walls.assign(12*28,1);
  for(const auto origin:{std::pair{2U,1U},std::pair{7U,20U}})
    for(unsigned y=origin.second;y<origin.second+2;++y)for(unsigned x=origin.first;x<origin.first+2;++x)grid.walls[x+y*12]=0;
  std::vector<k::Species> species(1);species[0].gas=false;species[0].positive_liquid=true;
  species[0].rho0=1000;species[0].stiffness=8e7;species[0].cv=4180;
  k::State fluid;fluid.energy.resize(grid.walls.size());fluid.jx.resize(grid.x_width()*grid.height);fluid.jy.resize(grid.width*grid.y_height());
  for(std::size_t i=0;i<grid.walls.size();++i) {
    fluid.masses.offsets.push_back(fluid.masses.mass.size());
    if(!grid.walls[i]){fluid.masses.species.push_back(0);fluid.masses.mass.push_back(1);fluid.energy[i]=4180*300;}
  }
  fluid.masses.offsets.push_back(fluid.masses.mass.size());
  std::vector<std::uint8_t> blocked(grid.walls.size());const auto membrane=7+20*12;blocked[membrane]=1;
  k::RegionalTick tick(grid,species,{},2,cfg.game,false);bool translated=false;
  try{(void)tick.advance(fluid,.2,0,0,blocked);}
  catch(const k::LiquidBoundaryConflict& conflict) {
    translated=!conflict.faces.empty();
    for(const auto& face:conflict.faces) {
      translated=translated && face.donor==membrane && (face.target==membrane+1 || face.target==membrane+12);
      translated=translated && face.face==(face.x?grid.x_face(8,20):grid.y_face(7,21));
    }
  }
  check(translated,"Regional liquid boundary conflict retained local cells or faces");
  std::cout<<"PASS regional shifted gravity/sinks, 1024 regions/budget2, moving regions/workers/cold continuation and global boundary coordinates\n";
}
inline void bounded_pressure_checkpoint_control() {
  using namespace ma::experimental;
  const auto check=[](bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
  };
  const auto& f=setup();
  auto state=f.state(32,32,false);
  const auto cell=16+16*32;
  state.cells[cell].mixture=MixtureInventory(f.registry.size());
  state.cells[cell].passive_quantities.clear();
  const std::vector<double> temperatures(state.cells.size(),300);
  const auto saved_options=mac_kernel_default_options();
  auto options=saved_options;options.threads=1;set_mac_kernel_default_options(options);
  struct Reset {
    MacKernelOptions options;
    ~Reset(){set_mac_kernel_default_options(options);}
  } reset{saved_options};
  auto config=[](MacGameProfile profile) {
    MacClosedSettings cfg;cfg.numerical_kernel=MacNumericalKernel::game;
    cfg.game_profile=profile;cfg.vacuum=MacVacuumRepresentation::void_phase;
    cfg.maximum_timestep_s=.2;cfg.gravity_m_per_s2=0;
    cfg.trace_sink_threshold_kg=1e-11;cfg.game.gas_heat_mixing=8;
    if(profile!=MacGameProfile::game_kernel) {
      cfg.game.liquid_law=MacGameLiquidLaw::positive_segments;
      cfg.game.pressure_maximum_iterations=400;cfg.game.heat_maximum_iterations=800;
    }
    return cfg;
  };
  for(const auto profile:{MacGameProfile::game_kernel,
      MacGameProfile::game_kernel_positive_liquid,
      MacGameProfile::game_kernel_free_surface,MacGameProfile::game_kernel_trace_heat,
      MacGameProfile::game_kernel_bounded_pressure,MacGameProfile::game_kernel_sized_intervals}) {
    const auto source=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,config(profile));
    const auto image=source.checkpoint();
    const bool sized=profile==MacGameProfile::game_kernel_sized_intervals;
    check(image.at(8)==(sized?26U:18+static_cast<unsigned>(profile)),"Saved game pressure policy version differs");
    const auto cold=MacClosedWorld::from_checkpoint(f.registry,f.eos,image);
    check(cold.checkpoint()==image && cold.settings().game_profile==profile,
          "Saved game pressure policy changed on restore");
    const auto first=source.advanced(.2);
    check(first.world.checkpoint()==cold.advanced(.2).world.checkpoint(),
          "Saved game pressure policy changed cold continuation");
    if(sized) {
      // Version 26 saves version 25's phase-rule selector: 0 equilibrium,
      // 1 boil-only; any other value is refused.
      std::uint32_t model=0;for(unsigned b=0;b<4;++b)model|=std::uint32_t(image.at(12+b))<<(8*b);
      // Magic, version, model identity; clock, geometry and settings through
      // the heat iteration cap; then the selector.
      const auto at=std::size_t(8+4+4)+model+8+8+4+4+8+8+8+8+1+8+8+4+8+8+1+8+8+4+4+4;
      check(image.at(at)==0&&image.at(at+1)==0&&image.at(at+2)==0&&image.at(at+3)==0,"Profile 7 phase rule is not saved as equilibrium");
      const auto relabel=[&](std::uint8_t rule) {
        auto other=image;other.at(at)=rule;std::uint64_t h=14695981039346656037ULL;
        for(std::size_t n=0;n+8<other.size();++n)h=(h^other[n])*1099511628211ULL;
        for(unsigned b=0;b<8;++b)other[other.size()-8+b]=static_cast<std::uint8_t>(h>>(8*b));
        return other;
      };
      auto boil_cfg=config(profile);boil_cfg.game.phase_rule=MacPhaseRule::boil_only;
      const auto boiling=MacClosedWorld::game_colony(f.registry,f.eos,state,temperatures,boil_cfg).checkpoint();
      check(boiling==relabel(1),"Profile 7 boil-only checkpoint differs from its selector");
      const auto restored=MacClosedWorld::from_checkpoint(f.registry,f.eos,boiling);
      check(restored.settings().game_profile==profile&&restored.settings().game.phase_rule==MacPhaseRule::boil_only&&
            restored.checkpoint()==boiling,"Profile 7 boil-only rule changed on restore");
      bool refused=false;
      try{(void)MacClosedWorld::from_checkpoint(f.registry,f.eos,relabel(2));}catch(const std::exception& error){refused=std::string(error.what()).find("phase rule")!=std::string::npos;}
      check(refused,"Profile 7 restored a noncanonical saved phase rule");
    }
    if(profile!=MacGameProfile::game_kernel_bounded_pressure && !sized) {
      check(first.report.game_stages.pressure_refinements==0,
            "Legacy saved profile acquired pressure refinement");
      continue;
    }
    check(first.report.game_stages.pressure_refinements>0 &&
          first.report.attempts==first.report.game_stages.pressure_attempts &&
          first.report.rejected_attempts==first.report.game_stages.pressure_refinements &&
          first.report.substeps+first.report.rejected_attempts==first.report.attempts,
          "Bounded pressure work omitted accepted/rejected attempts");
    bool refused=false;
    try{(void)source.advanced(.2,1);}catch(const std::exception& error){refused=std::string(error.what()).find("budget")!=std::string::npos;}
    check(refused && source.checkpoint()==image,"Pressure budget changed or advanced the source");
    check(source.advanced(.2).world.checkpoint()==first.world.checkpoint(),
          "Rejected pressure trial changed subsequent cache continuation");
    options.threads=2;set_mac_kernel_default_options(options);
    check(source.advanced(.2).world.checkpoint()==first.world.checkpoint(),
          "Bounded pressure worker partition changed physical bytes");
    const auto second=first.world.advanced(.2).world.checkpoint();
    check(MacClosedWorld::from_checkpoint(f.registry,f.eos,first.world.checkpoint()).advanced(.2).world.checkpoint()==second,
          "Bounded pressure worker cold continuation differs");
  }
  regional_pressure_controls();
  std::cout<<"PASS saved pressure profiles20-24 and 26 (both phase rules), bounded/sized attempt budget, cache retry and worker/cold continuation\n";
}
} // namespace ma::testing
