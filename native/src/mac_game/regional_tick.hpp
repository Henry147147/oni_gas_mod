#pragma once
#include "tick.hpp"
#include <exception>
#include <memory>

namespace ma::experimental::game_kernel {
// Solid walls make these systems independent. Keep explicit face barriers
// inside their common system so a closed face with two fluid neighbours is
// never represented twice. Each system advances the full requested interval.
class RegionalTick {
  using Grid = ma::experimental::mac_kernel::Grid;
  MacGrid source;
  Grid grid;
  ma::experimental::mac_kernel::Pool pool;
  Topology topology;
  std::size_t species_count;
  std::size_t attempt_depth=0,refinement_depth=0;
  struct Region {
    std::size_t x=0,y=0,representative=0;
    MacGrid box;
    std::unique_ptr<Tick> tick;
  };
  std::vector<Region> regions;
  static Topology wall_topology(MacGrid source) {
    source.closed_x.clear();source.closed_y.clear();
    return Topology(Grid(source));
  }
  std::size_t global_cell(const Region& r,std::size_t i) const {
    return r.x+i%r.box.width+(r.y+i/r.box.width)*source.width;
  }
  std::size_t global_face(const Region& r,bool x,std::size_t f) const {
    return x?source.x_face(r.x+f%r.box.x_width(),r.y+f/r.box.x_width()):
             source.y_face(r.x+f%r.box.width,r.y+f/r.box.width);
  }
  bool belongs(std::size_t cell,std::size_t region) const {
    return cell!=ma::experimental::mac_kernel::none && topology.labels[cell]==region;
  }
  State extract(const State& initial,std::size_t id) const {
    const auto& r=regions[id];State local;
    local.energy.resize(r.box.walls.size());
    for(std::size_t i=0;i<r.box.walls.size();++i) {
      local.masses.offsets.push_back(local.masses.mass.size());
      local.passives.offsets.push_back(local.passives.mass.size());
      if(r.box.walls[i])continue;
      const auto g=global_cell(r,i);local.energy[i]=initial.energy[g];
      for(auto a=initial.masses.offsets[g];a<initial.masses.offsets[g+1];++a) {
        local.masses.species.push_back(initial.masses.species[a]);local.masses.mass.push_back(initial.masses.mass[a]);
      }
      if(!initial.passives.offsets.empty())
        for(auto a=initial.passives.offsets[g];a<initial.passives.offsets[g+1];++a) {
          local.passives.species.push_back(initial.passives.species[a]);local.passives.mass.push_back(initial.passives.mass[a]);
        }
    }
    local.masses.offsets.push_back(local.masses.mass.size());
    local.passives.offsets.push_back(local.passives.mass.size());
    local.jx.resize(r.box.x_width()*r.box.height);local.jy.resize(r.box.width*r.box.y_height());
    for(const bool x:{true,false}) {
      auto& field=x?local.jx:local.jy;const auto& original=x?initial.jx:initial.jy;
      const auto& left=x?grid.xl:grid.yl;const auto& right=x?grid.xr:grid.yr;
      for(std::size_t f=0;f<field.size();++f) {
        const auto gf=global_face(r,x,f);
        if(belongs(left[gf],id)||belongs(right[gf],id))field[f]=original[gf];
      }
    }
    return local;
  }
public:
  RegionalTick(const MacGrid& g,std::vector<Species> species,
               std::vector<PhasePair> pairs,unsigned threads,
               const MacGameSettings& cfg,bool conduction)
    :source(g),grid(g),pool(static_cast<unsigned>(std::min<std::size_t>(threads,grid.blocks))),
     topology(wall_topology(g)),species_count(species.size()),regions(topology.count) {
    std::vector<std::size_t> right(regions.size()),top(regions.size());
    for(auto& r:regions){r.x=g.width;r.y=g.height;}
    for(std::size_t i=0;i<grid.N;++i)if(!grid.wall[i]) {
      const auto id=topology.labels[i];auto& r=regions[id];
      const auto x=i%g.width,y=i/g.width;
      if(r.x==g.width)r.representative=i;
      r.x=std::min(r.x,x);r.y=std::min(r.y,y);right[id]=std::max(right[id],x);top[id]=std::max(top[id],y);
    }
    for(std::size_t id=0;id<regions.size();++id) {
      auto& r=regions[id];auto& b=r.box;b.width=right[id]-r.x+1;b.height=top[id]-r.y+1;
      b.spacing_m=g.spacing_m;b.depth_m=g.depth_m;b.walls.resize(b.width*b.height,1);
      for(std::size_t i=0;i<b.walls.size();++i)b.walls[i]=topology.labels[global_cell(r,i)]!=id;
      if(!g.closed_x.empty()) {b.closed_x.resize(b.x_width()*b.height);for(std::size_t f=0;f<b.closed_x.size();++f)b.closed_x[f]=g.closed_x[global_face(r,true,f)];}
      if(!g.closed_y.empty()) {b.closed_y.resize(b.width*b.y_height());for(std::size_t f=0;f<b.closed_y.size();++f)b.closed_y[f]=g.closed_y[global_face(r,false,f)];}
      r.tick=std::make_unique<Tick>(b,species,pairs,regions.size()==1?threads:1,cfg.flow_parts,
        cfg.gas_heat_mixing,cfg.sharpen_liquid_interfaces,cfg.interface_detail_fraction,
        cfg.pressure_relative_tolerance,0,false,0,2,cfg.phase_relaxation_time_s,
        conduction,cfg.pressure_maximum_iterations,cfg.heat_maximum_iterations,true,true,true);
    }
  }
  unsigned threads_used() const noexcept {
    return regions.size()==1?regions.front().tick->threads_used():
      static_cast<unsigned>(std::max<std::size_t>(1,std::min<std::size_t>(pool.size(),regions.size())));
  }
  std::size_t pressure_attempt_depth() const {return attempt_depth;}
  std::size_t pressure_refinement_depth() const {return refinement_depth;}
  ma::experimental::mac_kernel::Pool& worker_pool() {return pool;}
  TickResult advance(const State& initial,double dt,double gravity,double threshold,
                     std::span<const std::uint8_t> blocked={},std::size_t budget=65536) {
    attempt_depth=refinement_depth=0;
    RegionAudit audit(grid,topology,species_count);Totals before;
    audit.capture(initial,gravity,1,&before,&pool);
    std::vector<TickResult> results(regions.size());std::vector<std::exception_ptr> errors(regions.size());
    pool.run(regions.size(),[&](std::size_t id) {
      try {
        auto& r=regions[id];auto state=extract(initial,id);
        std::vector<std::uint8_t> local_blocked;
        if(!blocked.empty()) {local_blocked.resize(r.box.walls.size());for(std::size_t i=0;i<local_blocked.size();++i)local_blocked[i]=blocked[global_cell(r,i)];}
        try {results[id]=r.tick->advance(state,dt,gravity,threshold,local_blocked,budget);}
        catch(const LiquidBoundaryConflict& e) {
          auto translated=e;
          for(auto& face:translated.faces) {
            face.face=global_face(r,face.x,face.face);
            face.donor=global_cell(r,face.donor);face.target=global_cell(r,face.target);
          }
          throw translated;
        }
      }catch(...){errors[id]=std::current_exception();}
    });
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    TickResult out;out.state.energy=initial.energy;out.state.jx=initial.jx;out.state.jy=initial.jy;out.eos.resize(grid.N);
    std::vector<DoubleDouble> sinks(species_count),conversion(species_count);
    std::map<std::uint64_t,DoubleDouble> passive_sinks;
    DoubleDouble sink_u,sink_k,sink_g,sink_x,sink_y,wall_x,wall_y,body_y;
    out.regions=topology.count;
    for(std::size_t id=0;id<regions.size();++id) {
      const auto& r=regions[id];const auto& next=results[id];const auto cell=r.representative;
      attempt_depth=std::max(attempt_depth,next.pressure_attempts);
      refinement_depth=std::max(refinement_depth,next.pressure_refinements);
      const double height=gravity*static_cast<double>(r.y)*source.spacing_m;
      DoubleDouble removed_mass;
      for(std::size_t key=0;key<species_count;++key) {
        sinks[key].add(next.sink_species[key]);conversion[key].add(next.conversion_species[key]);removed_mass.add(next.sink_species[key]);
        audit.mass(cell,key,next.conversion_species[key]);audit.mass(cell,key,-next.sink_species[key]);
      }
      DoubleDouble potential;potential.add(next.sink_potential);potential.add(removed_mass.hi*height);potential.add(removed_mass.lo*height);
      sink_u.add(next.sink_internal);sink_k.add(next.sink_kinetic);sink_g.add(potential.hi);sink_g.add(potential.lo);
      sink_x.add(next.sink_px);sink_y.add(next.sink_py);
      audit.energy(cell,-next.sink_internal);audit.energy(cell,-next.sink_kinetic);audit.energy(cell,-potential.hi);audit.energy(cell,-potential.lo);
      audit.momentum(cell,true,-next.sink_px);audit.momentum(cell,false,-next.sink_py);
      audit.momentum_receipt(cell,true,next.wall_px,next.wall_operands_x);
      audit.momentum_receipt(cell,false,next.wall_py,next.wall_operands_y);
      audit.momentum_receipt(cell,false,next.body_py,next.body_operands_y);
      wall_x.add(next.wall_px);wall_y.add(next.wall_py);body_y.add(next.body_py);
      for(const auto& [identity,value]:next.sink_passive_quantities) {passive_sinks[identity].add(value);audit.passive(cell,identity,-value);}
#define MA_REGIONAL_SUM(field) out.field+=next.field
      MA_REGIONAL_SUM(flow_ms);MA_REGIONAL_SUM(heat_ms);MA_REGIONAL_SUM(phase_ms);MA_REGIONAL_SUM(trace_audit_ms);
      MA_REGIONAL_SUM(interface_ms);MA_REGIONAL_SUM(layering_ms);MA_REGIONAL_SUM(pressure_ms);MA_REGIONAL_SUM(transport_ms);MA_REGIONAL_SUM(eos_ms);
      MA_REGIONAL_SUM(pressure_iterations);MA_REGIONAL_SUM(pressure_corrections);
      MA_REGIONAL_SUM(pressure_attempts);MA_REGIONAL_SUM(pressure_refinements);
      MA_REGIONAL_SUM(trace_constituents);MA_REGIONAL_SUM(trace_cells);MA_REGIONAL_SUM(microevents);
      MA_REGIONAL_SUM(interface_faces);MA_REGIONAL_SUM(limited_faces);MA_REGIONAL_SUM(interface_clusters);
      MA_REGIONAL_SUM(evaporated);MA_REGIONAL_SUM(condensed);MA_REGIONAL_SUM(entropy_change);MA_REGIONAL_SUM(trace_entropy);MA_REGIONAL_SUM(phase_entropy_change);MA_REGIONAL_SUM(entropy_ms);
      MA_REGIONAL_SUM(wall_operands_x);MA_REGIONAL_SUM(wall_operands_y);MA_REGIONAL_SUM(body_operands_y);
#undef MA_REGIONAL_SUM
      out.flow_substeps=std::max(out.flow_substeps,next.flow_substeps);
      out.largest_interface_cluster=std::max(out.largest_interface_cluster,next.largest_interface_cluster);
      out.mass_error=std::max(out.mass_error,next.mass_error);out.passive_error=std::max(out.passive_error,next.passive_error);
      for(std::size_t i=0;i<r.box.walls.size();++i)if(!r.box.walls[i]) {
        const auto g=global_cell(r,i);out.state.energy[g]=next.state.energy[i];out.eos[g]=next.eos[i];
      }
      for(const bool x:{true,false}) {
        const auto& field=x?next.state.jx:next.state.jy;auto& global=x?out.state.jx:out.state.jy;
        const auto& left=x?grid.xl:grid.yl;const auto& right=x?grid.xr:grid.yr;
        for(std::size_t f=0;f<field.size();++f) {const auto g=global_face(r,x,f);if(belongs(left[g],id)||belongs(right[g],id))global[g]=field[f];}
      }
    }
    for(std::size_t i=0;i<grid.N;++i) {
      out.state.masses.offsets.push_back(out.state.masses.mass.size());out.state.passives.offsets.push_back(out.state.passives.mass.size());
      if(grid.wall[i])continue;
      const auto id=topology.labels[i];const auto& r=regions[id];const auto local=i%source.width-r.x+(i/source.width-r.y)*r.box.width;
      const auto& state=results[id].state;
      for(auto a=state.masses.offsets[local];a<state.masses.offsets[local+1];++a) {out.state.masses.species.push_back(state.masses.species[a]);out.state.masses.mass.push_back(state.masses.mass[a]);}
      if(!state.passives.offsets.empty())for(auto a=state.passives.offsets[local];a<state.passives.offsets[local+1];++a) {out.state.passives.species.push_back(state.passives.species[a]);out.state.passives.mass.push_back(state.passives.mass[a]);}
    }
    out.state.masses.offsets.push_back(out.state.masses.mass.size());out.state.passives.offsets.push_back(out.state.passives.mass.size());
    Totals after;audit.capture(out.state,gravity,-1,&after,&pool);out.region_error=audit.finish();
    out.sink_species.resize(species_count);out.conversion_species.resize(species_count);
    for(std::size_t key=0;key<species_count;++key) {out.sink_species[key]=sinks[key].value();out.conversion_species[key]=conversion[key].value();out.sink_kg+=out.sink_species[key];}
    for(const auto& [identity,value]:passive_sinks)out.sink_passive_quantities.emplace_back(identity,value.value());
    out.sink_internal=sink_u.value();out.sink_kinetic=sink_k.value();out.sink_potential=sink_g.value();out.sink_px=sink_x.value();out.sink_py=sink_y.value();
    out.wall_px=wall_x.value();out.wall_py=wall_y.value();out.body_py=body_y.value();
    DoubleDouble removed_energy;removed_energy.add(sink_u.hi);removed_energy.add(sink_u.lo);removed_energy.add(sink_k.hi);removed_energy.add(sink_k.lo);removed_energy.add(sink_g.hi);removed_energy.add(sink_g.lo);
    Balance energy,mx,my;const auto add=[](Balance& b,const DoubleDouble& v,double sign=1){b.residual.add(sign*v.hi);b.residual.add(sign*v.lo);};
    add(energy,before.energy);add(energy,after.energy,-1);add(energy,removed_energy,-1);
    energy.operands.add(before.energy_operands.value());energy.operands.add(after.energy_operands.value());energy.operands.add(std::abs(removed_energy.value()));
    add(mx,before.jx);add(mx,after.jx,-1);add(mx,wall_x);add(mx,sink_x,-1);
    mx.operands.add(before.jx_operands.value());mx.operands.add(after.jx_operands.value());mx.operands.add(out.wall_operands_x);mx.operands.add(std::abs(sink_x.value()));
    add(my,before.jy);add(my,after.jy,-1);add(my,wall_y);add(my,body_y);add(my,sink_y,-1);
    my.operands.add(before.jy_operands.value());my.operands.add(after.jy_operands.value());my.operands.add(out.wall_operands_y);my.operands.add(out.body_operands_y);my.operands.add(std::abs(sink_y.value()));
    if(!energy.closed()||!mx.closed()||!my.closed())throw std::runtime_error("Regional pressure energy/momentum audit failed");
    out.energy_residual=energy.residual;out.energy_operands=energy.operands;
    out.energy_error=energy.operands.value()?std::abs(energy.residual.value())/energy.operands.value():0;
    out.momentum_error=std::max(mx.operands.value()?std::abs(mx.residual.value())/mx.operands.value():0,my.operands.value()?std::abs(my.residual.value())/my.operands.value():0);
    out.totals=std::move(after);out.sink_j=removed_energy.value();return out;
  }
};
} // namespace ma::experimental::game_kernel
