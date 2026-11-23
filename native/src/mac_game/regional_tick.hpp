#pragma once
#include "tick.hpp"
#include "regional_execution.hpp"
#include "region_samples.hpp"
#include <exception>
#include <memory>
#include <thread>

namespace ma::experimental::game_kernel {
inline bool same_grid(const MacGrid &a, const MacGrid &b) {
  return a.width == b.width && a.height == b.height &&
         a.spacing_m == b.spacing_m && a.depth_m == b.depth_m &&
         a.periodic_x == b.periodic_x && a.periodic_y == b.periodic_y &&
         a.walls == b.walls && a.closed_x == b.closed_x &&
         a.closed_y == b.closed_y;
}

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
  std::vector<std::uint8_t> sink_cells;double sink_speed=0;
  std::vector<CellFaceForce> face_forces;
  struct Region {
    std::size_t x=0,y=0,representative=0;
    MacGrid box;
    std::unique_ptr<Tick> tick;
  };
  std::vector<Region> regions;
  std::vector<RegionExecutionKey> execution_keys;
  std::vector<Species> execution_species;
  std::vector<PhasePair> execution_pairs;
  MacGameSettings execution_settings;
  bool execution_conduction=false,execution_sized=false,execution_carried=false;
  double execution_floor=0;
  bool execution_layered=false,execution_convective=false;
  double execution_thin_mass=0,execution_thin_time=0;
  bool execution_room=false;
  std::shared_ptr<RegionalExecutionHistory> execution_history;
  // Ticks with inner workers, by region, for the regions the execution plan
  // gives workers of their own. A region keeps its single-worker Tick too.
  std::map<std::size_t,std::unique_ptr<Tick>> parallel_ticks;
  // The other regions' workers, and one coordinator for each region with
  // workers of its own and for the residual pool. Both are kept between
  // intervals: no thread is created or ended while the plan stays the same.
  std::unique_ptr<ma::experimental::mac_kernel::Pool> residual_pool,coordinator_pool;
  std::unique_ptr<Tick> make_tick(const Region& r,unsigned threads) const {
    const auto& cfg=execution_settings;
    return std::make_unique<Tick>(r.box,execution_species,execution_pairs,threads,cfg.flow_parts,
      cfg.gas_heat_mixing,cfg.sharpen_liquid_interfaces,cfg.interface_detail_fraction,
      cfg.pressure_relative_tolerance,0,false,0,2,cfg.phase_relaxation_time_s,
      execution_conduction,cfg.pressure_maximum_iterations,cfg.heat_maximum_iterations,true,true,true,
      execution_sized,execution_carried,execution_floor,execution_layered,execution_convective,
      execution_thin_mass,execution_thin_time,execution_room);
  }
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
               const MacGameSettings& cfg,bool conduction,bool sized_intervals=false,
               bool carry_momentum=false,double bound_mass_floor=0,
               std::shared_ptr<RegionalExecutionHistory> history={},bool layered_remap=false,
               bool convective=false,double thin_gas_mass=0,double thin_gas_time=0,bool room_bound=false)
    :source(g),grid(g),pool(static_cast<unsigned>(std::min<std::size_t>(threads,grid.blocks))),
     topology(wall_topology(g)),species_count(species.size()),regions(topology.count),
     execution_keys(regions.size()),execution_species(std::move(species)),execution_pairs(std::move(pairs)),
     execution_settings(cfg),execution_conduction(conduction),execution_sized(sized_intervals),
     execution_carried(carry_momentum),execution_floor(bound_mass_floor),execution_layered(layered_remap),execution_convective(convective),
     execution_thin_mass(thin_gas_mass),execution_thin_time(thin_gas_time),execution_room(room_bound),
     execution_history(history?std::move(history):std::make_shared<RegionalExecutionHistory>()) {
    std::vector<std::size_t> right(regions.size()),top(regions.size());
    for(auto& r:regions){r.x=g.width;r.y=g.height;}
    for(std::size_t i=0;i<grid.N;++i)if(!grid.wall[i]) {
      const auto id=topology.labels[i];auto& r=regions[id];
      const auto x=i%g.width,y=i/g.width;
      if(r.x==g.width){r.representative=i;execution_keys[id].representative=i;}
      ++execution_keys[id].cells;execution_keys[id].membership^=static_cast<std::uint64_t>(i);
      execution_keys[id].membership*=1099511628211ULL;
      r.x=std::min(r.x,x);r.y=std::min(r.y,y);right[id]=std::max(right[id],x);top[id]=std::max(top[id],y);
    }
    for(std::size_t id=0;id<regions.size();++id) {
      auto& r=regions[id];auto& b=r.box;b.width=right[id]-r.x+1;b.height=top[id]-r.y+1;
      b.spacing_m=g.spacing_m;b.depth_m=g.depth_m;b.walls.resize(b.width*b.height,1);
      for(std::size_t i=0;i<b.walls.size();++i)b.walls[i]=topology.labels[global_cell(r,i)]!=id;
      if(!g.closed_x.empty()) {b.closed_x.resize(b.x_width()*b.height);for(std::size_t f=0;f<b.closed_x.size();++f)b.closed_x[f]=g.closed_x[global_face(r,true,f)];}
      if(!g.closed_y.empty()) {b.closed_y.resize(b.width*b.y_height());for(std::size_t f=0;f<b.closed_y.size();++f)b.closed_y[f]=g.closed_y[global_face(r,false,f)];}
      if (regions.size()==1) r.tick=make_tick(r,threads);
    }
    execution_history->retain(execution_keys);
  }
  // Only geometry changed. Reuse scratch and worker pools for identical
  // local grids. Extract still reads every stepped region from the new state.
  void reuse_workspaces(RegionalTick& previous) {
    if (source.width != previous.source.width || source.height != previous.source.height) return;
    residual_pool=std::move(previous.residual_pool);
    coordinator_pool=std::move(previous.coordinator_pool);
    for (std::size_t id=0; id<regions.size(); ++id) {
      auto& region=regions[id];
      const auto old_id=previous.topology.labels[region.representative];
      if (old_id>=previous.regions.size()) continue;
      auto& old=previous.regions[old_id];
      if (region.x!=old.x || region.y!=old.y || !same_grid(region.box,old.box)) continue;
      if (regions.size()>1 && previous.regions.size()>1) region.tick=std::move(old.tick);
      if (auto found=previous.parallel_ticks.find(old_id); found!=previous.parallel_ticks.end())
        parallel_ticks.emplace(id,std::move(found->second));
    }
  }
  std::span<const RegionExecutionKey> execution_region_keys() const noexcept {return execution_keys;}
  unsigned threads_used() const noexcept {
    return regions.size()==1?regions.front().tick->threads_used():
      static_cast<unsigned>(std::max<std::size_t>(1,std::min<std::size_t>(pool.size(),regions.size())));
  }
  std::size_t pressure_attempt_depth() const {return attempt_depth;}
  std::size_t pressure_refinement_depth() const {return refinement_depth;}
  ma::experimental::mac_kernel::Pool& worker_pool() {return pool;}
  // Space venting (Flow::set_sinks): owner-grid cells the owner empties every
  // frame, and their faces' speed cap; an empty mask or zero switches it off.
  void set_sinks(std::span<const std::uint8_t> cells,double speed) {
    if(!cells.empty() && cells.size()!=grid.N)throw std::invalid_argument("Game sink coverage differs");
    sink_cells.assign(cells.begin(),cells.end());sink_speed=cells.empty()?0:speed;
  }
  // Face forces (docs/face-forces.md) on owner-grid cells, for the following
  // advances; empty: none. A stepped region books each force's work as an
  // external energy input (TickResult::force_work, in this order); a region
  // an interval leaves unstepped is not driven in it.
  void set_face_forces(std::span<const CellFaceForce> forces) {face_forces.assign(forces.begin(),forces.end());}
  TickResult advance(const State& initial,double dt,double gravity,double threshold,
                     std::span<const std::uint8_t> blocked={},std::size_t budget=65536,
                     std::span<const double> due={},double maximum_active_interval=0) {
    return advance_impl(initial,nullptr,nullptr,dt,gravity,threshold,blocked,budget,due,maximum_active_interval);
  }
  // advance() that takes `initial` (its energy and face momenta become the
  // result's) and writes the readings of the cells it steps into `readings`
  // (one per cell) instead of returning a whole-grid eos. Other cells'
  // readings are left as they are. Values are advance()'s.
  // `continues`: `initial` is the state this tick's previous advance_into
  // returned, unchanged (the next interval of one advance); its audit totals
  // are then that call's, not summed again.
  TickResult advance_into(State&& initial,std::vector<Eos>& readings,double dt,double gravity,double threshold,
                          std::span<const std::uint8_t> blocked={},std::size_t budget=65536,
                          std::span<const double> due={},bool continues=false,double maximum_active_interval=0) {
    if(readings.size()!=grid.N)throw std::invalid_argument("Game readings coverage differs");
    if(!continues || !carried_valid || carried_gravity!=gravity)carried_valid=false;
    return advance_impl(initial,&initial,&readings,dt,gravity,threshold,blocked,budget,due,maximum_active_interval);
  }
private:
  // The last advance_into's audit totals of its result, by row block.
  bool carried_valid=false;double carried_gravity=0;
  Totals carried_totals;std::vector<Totals> carried_block_sums;
  TickResult advance_impl(const State& initial,State* consumed,std::vector<Eos>* readings,double dt,double gravity,double threshold,
                          std::span<const std::uint8_t> blocked,std::size_t budget,std::span<const double> due,double maximum_active_interval) {
    if(!due.empty() && due.size()!=grid.N)throw std::invalid_argument("Game activity step coverage differs");
    auto mark=std::chrono::steady_clock::now();
    const auto lap=[&mark]{const auto now=std::chrono::steady_clock::now();const double ms=std::chrono::duration<double,std::milli>(now-mark).count();mark=now;return ms;};
    double setup_ms=0,capture_ms=0,run_ms=0,merge_ms=0,rebuild_ms=0;
    std::vector<double> steps(regions.size(),dt);
    // Each region's step and its clock check, on the pool; the first region
    // (in order) that fails throws what the serial checks threw.
    std::vector<std::uint8_t> invalid(regions.size());
    pool.run(regions.size(),[&](std::size_t id) {
      const auto& r=regions[id];
      const double step=due.empty()?dt:due[r.representative];
      if(!std::isfinite(step) || step<0){invalid[id]=1;return;}
      if(!due.empty())for(std::size_t i=0;i<r.box.walls.size();++i)
        if(!r.box.walls[i] && due[global_cell(r,i)]!=step){invalid[id]=2;return;}
      steps[id]=step;
    });
    for(const auto verdict:invalid) {
      if(verdict==1)throw std::invalid_argument("Invalid game activity step");
      if(verdict==2)throw std::invalid_argument("Game activity region clocks differ");
    }
    std::vector<RegionExecutionWork> execution_work;execution_work.reserve(regions.size());
    for(std::size_t id=0;id<regions.size();++id) {
      const auto& box=regions[id].box;const auto rows=Grid::block_rows(box.width);
      execution_work.push_back({execution_keys[id],steps[id],static_cast<unsigned>((box.height+rows-1)/rows)});
    }
    const auto execution=execution_history->plan(execution_work,pool.size());
    if(!execution.parallel.empty()) {
      // Build what is missing before anything is replaced: a failed
      // allocation leaves the previous Ticks and pool in place.
      std::map<std::size_t,std::unique_ptr<Tick>> next;
      for(const auto& share:execution.parallel) {
        const auto held=parallel_ticks.find(share.region);
        if(held==parallel_ticks.end() || held->second->threads_used()!=share.threads)
          next[share.region]=make_tick(regions[share.region],share.threads);
      }
      const auto remaining=std::max(1U,execution.residual_threads);
      std::unique_ptr<ma::experimental::mac_kernel::Pool> residual;
      if(!residual_pool || residual_pool->size()!=remaining)
        residual=std::make_unique<ma::experimental::mac_kernel::Pool>(remaining);
      const auto jobs=static_cast<unsigned>(execution.parallel.size()+1);
      std::unique_ptr<ma::experimental::mac_kernel::Pool> coordinators;
      if(!coordinator_pool || coordinator_pool->size()!=jobs)
        coordinators=std::make_unique<ma::experimental::mac_kernel::Pool>(jobs);
      // A region that lost its workers gives its Tick up; the others keep theirs.
      std::erase_if(parallel_ticks,[&](const auto& entry){return execution.threads_of(entry.first)==0 && execution.probe_region!=entry.first;});
      for(auto& [id,tick]:next)parallel_ticks[id]=std::move(tick);
      if(residual)residual_pool=std::move(residual);
      if(coordinators)coordinator_pool=std::move(coordinators);
    }
    attempt_depth=refinement_depth=0;
    // Face forces by region: each one's index in face_forces and its face on
    // the region's own grid. Walls separate regions, so a face between two
    // fluid cells lies in one.
    std::vector<std::vector<std::pair<std::size_t,FaceForce>>> region_forces(face_forces.empty()?0:regions.size());
    for(std::size_t k=0;k<face_forces.size();++k) {
      bool x=true;std::size_t fx=0,fy=0;double sign=1;
      if(!resolve_face_force(source,face_forces[k],x,fx,fy,sign))continue;
      const auto id=topology.labels[face_forces[k].cell];const auto& r=regions[id];
      region_forces[id].push_back({k,FaceForce{(fx-r.x)+(fy-r.y)*(x?r.box.x_width():r.box.width),x,sign,face_forces[k].stall_pa,face_forces[k].free_speed}});
    }
    setup_ms=lap();
    RegionAudit audit(grid,topology,species_count);Totals before;
    // The audit books only the regions this interval steps: an unstepped
    // region keeps every value, so its entries in the two captures cancel.
    // A row block whose cells (and the row below its first, for its y
    // faces) are all unstepped is unchanged, and the second capture takes
    // its totals partial from the first.
    std::vector<std::uint8_t> stepped(regions.size()),unchanged(grid.blocks,1);
    for(std::size_t id=0;id<regions.size();++id)stepped[id]=steps[id]>0;
    for(std::size_t block=0;block<grid.blocks;++block) {
      const auto y0=grid.block_row_begin(block),y1=grid.block_row_end(block);
      for(std::size_t i=(y0>0?y0-1:0)*grid.W;i<y1*grid.W && unchanged[block];++i)
        if(!grid.wall[i] && stepped[topology.labels[i]])unchanged[block]=0;
    }
    std::vector<Totals> block_sums;
    const bool carried=consumed && carried_valid;carried_valid=false;
    if(carried) {
      // The same state the last call summed: its totals and partials, and
      // bookings for the stepped regions alone.
      before=std::move(carried_totals);block_sums=std::move(carried_block_sums);
      audit.capture(initial,gravity,1,nullptr,&pool,stepped,nullptr,unchanged);
    } else audit.capture(initial,gravity,1,&before,&pool,stepped,&block_sums);
    capture_ms=lap();
    std::vector<TickResult> results(regions.size());std::vector<std::exception_ptr> errors(regions.size());
    std::vector<double> region_ms(regions.size());
    const auto run_region=[&](std::size_t id) {
      if(steps[id]==0)return;
      const auto started=std::chrono::steady_clock::now();
      try {
        auto& r=regions[id];auto state=extract(initial,id);
        std::vector<std::uint8_t> local_blocked;
        if(!blocked.empty()) {local_blocked.resize(r.box.walls.size());for(std::size_t i=0;i<local_blocked.size();++i)local_blocked[i]=blocked[global_cell(r,i)];}
        try {
          if(reject_log::level())reject_log::region={r.x,r.y,static_cast<std::size_t>(r.box.width),static_cast<std::size_t>(source.width)};
          // A sleeping region needs no solver workspace. Each stepped region
          // runs once, on exactly one worker or coordinator.
          const bool parallel=execution.threads_of(id)!=0;
          if (!parallel && !r.tick) r.tick=make_tick(r,1);
          auto& tick=parallel?*parallel_ticks.at(id):*r.tick;
          std::vector<std::uint8_t> local_sinks;
          if(sink_speed>0) {local_sinks.resize(r.box.walls.size());for(std::size_t i=0;i<local_sinks.size();++i)local_sinks[i]=sink_cells[global_cell(r,i)];}
          tick.set_sinks(std::move(local_sinks),sink_speed);
          std::vector<FaceForce> local_forces;
          if(!region_forces.empty())for(const auto& [index,force]:region_forces[id])local_forces.push_back(force);
          tick.set_face_forces(std::move(local_forces));
          results[id]=tick.advance(state,steps[id],gravity,threshold,local_blocked,budget,maximum_active_interval);
        }
        catch(const LiquidBoundaryConflict& e) {
          auto translated=e;
          for(auto& face:translated.faces) {
            face.face=global_face(r,face.x,face.face);
            face.donor=global_cell(r,face.donor);face.target=global_cell(r,face.target);
          }
          throw translated;
        }
      }catch(...){errors[id]=std::current_exception();}
      region_ms[id]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    };
    if(!execution.parallel.empty()) {
      // One coordinator a region with workers of its own, and one for the
      // residual pool with every other region. The residual job is block 0:
      // the pool rethrows its lowest failing block, so a failure of the
      // residual pool itself (not of a region, which run_region records)
      // keeps its precedence. Every job is finished before run returns.
      std::vector<std::size_t> others;others.reserve(regions.size());
      for(std::size_t id=0;id<regions.size();++id)if(execution.threads_of(id)==0)others.push_back(id);
      coordinator_pool->run(execution.parallel.size()+1,[&](std::size_t job) {
        if(job==0)residual_pool->run(others.size(),[&](std::size_t block){run_region(others[block]);});
        else run_region(execution.parallel[job-1].region);
      });
    } else pool.run(regions.size(),run_region);
    run_ms=lap();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
    // The region runs have read `initial`; a consumed state's energy and face
    // momenta become the result's, and the rebuild below reads only its
    // masses and passives.
    TickResult out;
    if(consumed) {out.state.energy=std::move(consumed->energy);out.state.jx=std::move(consumed->jx);out.state.jy=std::move(consumed->jy);}
    else {out.state.energy=initial.energy;out.state.jx=initial.jx;out.state.jy=initial.jy;}
    if(!readings)out.eos.resize(grid.N);
    auto& eos=readings?*readings:out.eos;
    std::vector<DoubleDouble> sinks(species_count),conversion(species_count);
    std::map<std::uint64_t,DoubleDouble> passive_sinks;
    DoubleDouble sink_u,sink_k,sink_g,sink_x,sink_y,wall_x,wall_y,body_y,forced_work,forced_operands;
    out.force_work.assign(face_forces.size(),0.);
    out.regions=topology.count;
    for(std::size_t id=0;id<regions.size();++id) {
      if(steps[id]==0)continue;
      const auto& r=regions[id];const auto& next=results[id];const auto cell=r.representative;
      attempt_depth=std::max(attempt_depth,next.pressure_attempts);
      refinement_depth=std::max(refinement_depth,next.pressure_refinements);
      if(region_samples::enabled().load(std::memory_order_relaxed))
        region_samples::last().push_back({cell,r.x,r.y,r.box.width,r.box.height,execution_keys[id].cells,steps[id],region_ms[id],
          next.flow_substeps,next.pressure_attempts,next.pressure_refinements,next.pressure_iterations,
          next.pressure_ms,next.transport_ms,next.layering_ms,next.interface_ms,next.heat_ms,next.phase_ms,next.trace_audit_ms});
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
      // Face forces: the region's energy account takes their work from outside.
      for(std::size_t n=0;n<next.force_work.size();++n) {
        const double work=next.force_work[n];
        out.force_work[region_forces[id][n].first]=work;
        audit.energy(cell,work);forced_work.add(work);forced_operands.add(std::abs(work));
      }
      for(const auto& [identity,value]:next.sink_passive_quantities) {passive_sinks[identity].add(value);audit.passive(cell,identity,-value);}
#define MA_REGIONAL_SUM(field) out.field+=next.field
      MA_REGIONAL_SUM(flow_ms);MA_REGIONAL_SUM(heat_ms);MA_REGIONAL_SUM(phase_ms);MA_REGIONAL_SUM(trace_audit_ms);
      MA_REGIONAL_SUM(interface_ms);MA_REGIONAL_SUM(layering_ms);MA_REGIONAL_SUM(pressure_ms);MA_REGIONAL_SUM(transport_ms);MA_REGIONAL_SUM(eos_ms);
      MA_REGIONAL_SUM(pressure_iterations);MA_REGIONAL_SUM(pressure_corrections);MA_REGIONAL_SUM(pressure_geometric);
      MA_REGIONAL_SUM(pressure_attempts);MA_REGIONAL_SUM(pressure_refinements);MA_REGIONAL_SUM(refused_trial_ms);
      MA_REGIONAL_SUM(trace_constituents);MA_REGIONAL_SUM(trace_cells);MA_REGIONAL_SUM(microevents);
      MA_REGIONAL_SUM(interface_faces);MA_REGIONAL_SUM(limited_faces);MA_REGIONAL_SUM(interface_clusters);
      MA_REGIONAL_SUM(evaporated);MA_REGIONAL_SUM(condensed);MA_REGIONAL_SUM(entropy_change);MA_REGIONAL_SUM(trace_entropy);MA_REGIONAL_SUM(phase_entropy_change);MA_REGIONAL_SUM(entropy_ms);
      MA_REGIONAL_SUM(wall_operands_x);MA_REGIONAL_SUM(wall_operands_y);MA_REGIONAL_SUM(body_operands_y);
#undef MA_REGIONAL_SUM
      out.flow_substeps=std::max(out.flow_substeps,next.flow_substeps);
      out.largest_interface_cluster=std::max(out.largest_interface_cluster,next.largest_interface_cluster);
      out.mass_error=std::max(out.mass_error,next.mass_error);out.passive_error=std::max(out.passive_error,next.passive_error);
    }
    // Each stepped region's cells and faces into the grid, one region a job:
    // walls separate regions, so no two regions write the same cell or face.
    pool.run(regions.size(),[&](std::size_t id) {
      if(steps[id]==0)return;
      const auto& r=regions[id];const auto& next=results[id];
      for(std::size_t i=0;i<r.box.walls.size();++i)if(!r.box.walls[i]) {
        const auto g=global_cell(r,i);out.state.energy[g]=next.state.energy[i];eos[g]=next.eos[i];
      }
      for(const bool x:{true,false}) {
        const auto& field=x?next.state.jx:next.state.jy;auto& global=x?out.state.jx:out.state.jy;
        const auto& left=x?grid.xl:grid.yl;const auto& right=x?grid.xr:grid.yr;
        for(std::size_t f=0;f<field.size();++f) {const auto g=global_face(r,x,f);if(belongs(left[g],id)||belongs(right[g],id))global[g]=field[f];}
      }
    });
    merge_ms=lap();
    // The grid's lists, cell by cell from each cell's stepped region or the
    // initial state: counted per fixed block of cells on the pool, then each
    // block fills its own slices (the lists a serial pass appends).
    {
      constexpr std::size_t cell_block=4096;
      const auto cell_blocks=(grid.N+cell_block-1)/cell_block;
      // A cell's source state and its cell there (none for a wall).
      const auto source_of=[&](std::size_t i)->std::pair<const State*,std::size_t> {
        if(grid.wall[i])return {nullptr,0};
        const auto id=topology.labels[i];const auto& r=regions[id];
        if(steps[id]>0)return {&results[id].state,i%source.width-r.x+(i/source.width-r.y)*r.box.width};
        return {&initial,i};
      };
      std::vector<std::size_t> mass_start(cell_blocks+1),passive_start(cell_blocks+1);
      pool.run(cell_blocks,[&](std::size_t b) {
        std::size_t masses=0,passives=0;
        for(std::size_t i=b*cell_block,end=std::min(grid.N,i+cell_block);i<end;++i) {
          const auto [state,at]=source_of(i);if(!state)continue;
          masses+=state->masses.offsets[at+1]-state->masses.offsets[at];
          if(!state->passives.offsets.empty())passives+=state->passives.offsets[at+1]-state->passives.offsets[at];
        }
        mass_start[b+1]=masses;passive_start[b+1]=passives;
      });
      for(std::size_t b=0;b<cell_blocks;++b){mass_start[b+1]+=mass_start[b];passive_start[b+1]+=passive_start[b];}
      auto& masses=out.state.masses;auto& passives=out.state.passives;
      masses.offsets.resize(grid.N+1);passives.offsets.resize(grid.N+1);
      masses.species.resize(mass_start.back());masses.mass.resize(mass_start.back());
      passives.species.resize(passive_start.back());passives.mass.resize(passive_start.back());
      pool.run(cell_blocks,[&](std::size_t b) {
        auto mass_at=mass_start[b],passive_at=passive_start[b];
        for(std::size_t i=b*cell_block,end=std::min(grid.N,i+cell_block);i<end;++i) {
          masses.offsets[i]=mass_at;passives.offsets[i]=passive_at;
          const auto [state,at]=source_of(i);if(!state)continue;
          for(auto a=state->masses.offsets[at];a<state->masses.offsets[at+1];++a,++mass_at) {masses.species[mass_at]=state->masses.species[a];masses.mass[mass_at]=state->masses.mass[a];}
          if(!state->passives.offsets.empty())for(auto a=state->passives.offsets[at];a<state->passives.offsets[at+1];++a,++passive_at) {passives.species[passive_at]=state->passives.species[a];passives.mass[passive_at]=state->passives.mass[a];}
        }
      });
      masses.offsets[grid.N]=mass_start.back();passives.offsets[grid.N]=passive_start.back();
    }
    rebuild_ms=lap();
    Totals after;audit.capture(out.state,gravity,-1,&after,&pool,stepped,&block_sums,unchanged);out.region_error=audit.finish();
    capture_ms+=lap();
    out.sink_species.resize(species_count);out.conversion_species.resize(species_count);
    for(std::size_t key=0;key<species_count;++key) {out.sink_species[key]=sinks[key].value();out.conversion_species[key]=conversion[key].value();out.sink_kg+=out.sink_species[key];}
    for(const auto& [identity,value]:passive_sinks)out.sink_passive_quantities.emplace_back(identity,value.value());
    out.sink_internal=sink_u.value();out.sink_kinetic=sink_k.value();out.sink_potential=sink_g.value();out.sink_px=sink_x.value();out.sink_py=sink_y.value();
    out.wall_px=wall_x.value();out.wall_py=wall_y.value();out.body_py=body_y.value();
    DoubleDouble removed_energy;removed_energy.add(sink_u.hi);removed_energy.add(sink_u.lo);removed_energy.add(sink_k.hi);removed_energy.add(sink_k.lo);removed_energy.add(sink_g.hi);removed_energy.add(sink_g.lo);
    Balance energy,mx,my;const auto add=[](Balance& b,const DoubleDouble& v,double sign=1){b.residual.add(sign*v.hi);b.residual.add(sign*v.lo);};
    add(energy,before.energy);add(energy,after.energy,-1);add(energy,removed_energy,-1);add(energy,forced_work);
    energy.operands.add(before.energy_operands.value());energy.operands.add(after.energy_operands.value());energy.operands.add(std::abs(removed_energy.value()));
    energy.operands.add(forced_operands.value());out.force_work_j=forced_work.value();
    add(mx,before.jx);add(mx,after.jx,-1);add(mx,wall_x);add(mx,sink_x,-1);
    mx.operands.add(before.jx_operands.value());mx.operands.add(after.jx_operands.value());mx.operands.add(out.wall_operands_x);mx.operands.add(std::abs(sink_x.value()));
    add(my,before.jy);add(my,after.jy,-1);add(my,wall_y);add(my,body_y);add(my,sink_y,-1);
    my.operands.add(before.jy_operands.value());my.operands.add(after.jy_operands.value());my.operands.add(out.wall_operands_y);my.operands.add(out.body_operands_y);my.operands.add(std::abs(sink_y.value()));
    if(!energy.closed()||!mx.closed()||!my.closed())throw std::runtime_error("Regional pressure energy/momentum audit failed");
    out.energy_residual=energy.residual;out.energy_operands=energy.operands;
    out.energy_error=energy.operands.value()?std::abs(energy.residual.value())/energy.operands.value():0;
    out.momentum_error=std::max(mx.operands.value()?std::abs(mx.residual.value())/mx.operands.value():0,my.operands.value()?std::abs(my.residual.value())/my.operands.value():0);
    out.totals=std::move(after);out.sink_j=removed_energy.value();
    execution_history->committed(execution_work,region_ms,execution);
    if(consumed) {carried_totals=out.totals;carried_block_sums=std::move(block_sums);carried_gravity=gravity;carried_valid=true;}
    out.regional_setup_ms=setup_ms+lap();out.regional_capture_ms=capture_ms;out.regional_run_ms=run_ms;
    out.regional_merge_ms=merge_ms;out.regional_rebuild_ms=rebuild_ms;
    return out;
  }
};
} // namespace ma::experimental::game_kernel
