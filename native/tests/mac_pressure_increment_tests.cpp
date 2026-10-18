#include "mac_compressive_fixture.hpp"
#include <chrono>
#include <iomanip>
#include <numbers>
namespace {
using namespace ma;using namespace ma::experimental;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::vector<MaterialCell> wave(std::size_t w,std::size_t h,double water=0){
 const auto&s=ma::testing::setup();std::vector<MaterialCell> cells;
 for(std::size_t i=0;i<w*h;++i){const double x=(static_cast<double>(i%w)+.5)/static_cast<double>(w);cells.push_back({s.mixture(1,(water==1?300000:100000)+20*std::sin(2*std::numbers::pi*x),300,water),1,0});}return cells;
}
std::vector<OccupiedMaterialNode> nodes(const std::vector<MaterialCell>&cells,std::size_t w,std::size_t h){
 std::vector<OccupiedMaterialNode> result;
 for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x){const auto c=y*w+x,l=y*w+(x+w-1)%w,d=((y+h-1)%h)*w+x;
  result.push_back({l,c,1,.5*(cells[l].mixture.mass()+cells[c].mixture.mass()),0,0,false});
  if(h>1)result.push_back({d,c,1,.5*(cells[d].mixture.mass()+cells[c].mixture.mass()),0,0,false});
 }return result;
}
ReferenceRootOptions options(){ReferenceRootOptions r;r.relative_pressure_tolerance=std::numeric_limits<double>::epsilon()/4;return r;}
void queries(){const auto&s=ma::testing::setup();ma::testing::export_compressive_definitions(0,*s.eos);int id=0;
 for(int material=0;material<3;++material){const auto m=s.mixture(1,material==2?300000:100000,300,material==2?1:material==1?.3:0);const auto anchor=s.eos->reference().make_isentrope(s.eos->to_reference(m,1));
  for(int power:{-12,-20,-40,-70})for(int sign:{-1,1}){const double dv=std::ldexp(static_cast<double>(sign),power);const auto r=s.eos->reference().isentropic_work(anchor,dv,options());
   check(r.work_pressure_change_pa.has_value(),"Ordinary gas, liquid or mixed pressure increment is unavailable");const double increment=*r.work_pressure_change_pa;
   check(increment!=0 && (increment<0)==(dv>0),"Pressure change vanished or has the wrong monotone sign");
   const auto&a=anchor.reference_state();
   std::cout<<"PQUERY "<<id<<' '<<material<<' '<<m.energy<<" 1 "<<dv<<' '<<a.mechanical_pressure_pa<<' '<<a.temperature_k<<' '<<a.gas_volume_m3<<' '<<r.response.work_pressure_pa<<' '<<r.work_pressure_volume_derivative_pa_per_m3<<' '<<increment<<'\n';
   for(std::size_t k=0;k<m.masses.size();++k)if(m.masses[k]>0)std::cout<<"PQMASS "<<id<<' '<<k<<' '<<m.masses[k]<<'\n';
   ++id;
  }
 }
 const auto huge=s.mixture(1e200,.1,300);const auto a=s.eos->reference().make_isentrope(s.eos->to_reference(huge,1e200));
 const auto r=s.eos->reference().isentropic_work(a,1e-124,options());
 check(!r.work_pressure_change_pa && r.response.internal_energy_change_j<0 && r.work_pressure_volume_derivative_pa_per_m3<0,"Unrepresentable pressure increment was hidden or spoiled ordinary work");
 std::cout<<"PUNAVAILABLE 1\n";
}
void roots(){const auto&s=ma::testing::setup();int id=0;
 for(double dt:{.01,.025,.05,.1}){const auto cells=wave(16,16);const auto faces=nodes(cells,16,16);const auto held=cells;
  const auto r=advance_occupied_material_pressure(*s.eos,cells,faces,dt,PressurePreconditioner::automatic,LiquidFillRoundoffPolicy::strict_eos,16*std::numeric_limits<double>::epsilon(),options());
  check(r.maximum_endpoint_relative_volume_residual<=16*std::numeric_limits<double>::epsilon(),"Restart relaxed the volume tolerance");
  check(r.pressure_increment_restarts==(dt==.01?0U:1U),"Pressure fixture missed its ordinary/restarted paths");
  MacEnergyAccount energy,momentum;
  for(std::size_t i=0;i<cells.size();++i){check(cells[i].mixture.energy==held[i].mixture.energy && cells[i].mixture.masses==held[i].mixture.masses,"Pressure restart mutated source");energy.add(r.cells[i].mixture.energy);energy.add(-cells[i].mixture.energy);}
  for(std::size_t f=0;f<faces.size();++f){energy.add(MacEnergy::variance(faces[f].inertia_kg,r.velocities_m_per_s[f]));momentum.add(MacEnergy::from_double(r.velocities_m_per_s[f]).multiplied(faces[f].inertia_kg));}
  check(energy.within_bound() && momentum.within_bound(),"Restarted material pressure violates the original energy/momentum accounts");
  std::cout<<"PROOT "<<id<<' '<<dt<<' '<<r.pressure_increment_restarts<<' '<<r.nonlinear_iterations<<' '<<r.pressure_iterations<<' '<<r.maximum_endpoint_relative_volume_residual<<'\n';
  if(dt==.1){
   for(std::size_t i=0;i<cells.size();++i){std::cout<<"PROOTCELL "<<i<<' '<<cells[i].mixture.energy<<' '<<r.cells[i].mixture.energy<<' '<<r.cells[i].volume_m3<<' '<<r.volume_changes_m3[i]<<' '<<r.volume_residuals_m3[i]<<' '<<r.reference_pressures_pa[i]<<' '<<r.work_pressure_changes_pa[i]<<'\n';
    check(cells[i].mixture.masses==r.cells[i].mixture.masses,"Pressure restart changed a constituent");
    for(std::size_t k=0;k<cells[i].mixture.masses.size();++k)if(cells[i].mixture.masses[k]>0)std::cout<<"PROOTMASS "<<i<<' '<<k<<' '<<cells[i].mixture.masses[k]<<'\n';
   }
   for(std::size_t f=0;f<faces.size();++f)std::cout<<"PROOTNODE "<<f<<' '<<faces[f].left<<' '<<faces[f].right<<' '<<faces[f].inertia_kg<<' '<<r.velocities_m_per_s[f]<<' '<<r.node_pressure_differences_pa[f]<<'\n';
  }++id;
 }
 // The same private constitutive override used by the production coupled mode.
 int material=0;
 for(double water:{0.,.3,1.}){
  const double dt=material==0?.1:.5;const auto cells=wave(16,2,water);const auto faces=nodes(cells,16,2);const std::vector<MacEnergy> linear(cells.size(),MacEnergy::from_double(50));
  const auto coupled=advance_mac_compressive_pressure(*s.eos,cells,faces,dt,1,linear,PressurePreconditioner::automatic,16*std::numeric_limits<double>::epsilon(),options());
  const auto&r=coupled.pressure;check(r.pressure_increment_restarts==1 && r.maximum_endpoint_relative_volume_residual<=16*std::numeric_limits<double>::epsilon(),"Coupled restart was missed or relaxed geometric closure");
  MacEnergyAccount energy,momentum;
  for(std::size_t i=0;i<cells.size();++i){check(cells[i].mixture.masses==r.cells[i].mixture.masses,"Coupled restart changed a constituent");energy.add(r.cells[i].mixture.energy);energy.add(-cells[i].mixture.energy);}
  for(std::size_t f=0;f<faces.size();++f){energy.add(MacEnergy::variance(faces[f].inertia_kg,r.velocities_m_per_s[f]));momentum.add(MacEnergy::from_double(r.velocities_m_per_s[f]).multiplied(faces[f].inertia_kg));}
  check(energy.within_bound() && momentum.within_bound(),"Coupled restart violates original actual U/K/momentum bounds");
  std::cout<<"PCOUPLED "<<material<<' '<<dt<<' '<<r.pressure_increment_restarts<<' '<<r.nonlinear_iterations<<' '<<r.maximum_endpoint_relative_volume_residual<<'\n';++material;
 }
}
void owner(){const auto&s=ma::testing::setup();auto state=s.state(16,16,true);const auto cells=wave(16,16);for(std::size_t i=0;i<cells.size();++i)state.cells[i].mixture=cells[i].mixture;
 MacClosedSettings settings;settings.maximum_timestep_s=.1;const MacClosedWorld source(s.registry,s.eos,std::move(state),settings);const auto held=source.checkpoint();
 auto advanced=source.advanced(.1);check(advanced.report.pressure_increment_restarts>0 && advanced.report.substeps==1 && advanced.report.rejected_attempts==0,"Owner did not use its completed pressure restart");
 const auto bytes=advanced.world.checkpoint();auto restored=MacClosedWorld::from_checkpoint(s.registry,s.eos,bytes);check(bytes==restored.checkpoint() && source.checkpoint()==held,"Restarted checkpoint changed source or restoration");
 const auto a=advanced.world.advanced(.1),b=restored.advanced(.1);check(a.world.checkpoint()==b.world.checkpoint(),"Restarted same-platform continuation differs");
 std::cout<<"POWNER "<<advanced.report.substeps<<' '<<advanced.report.rejected_attempts<<' '<<advanced.report.pressure_increment_restarts<<" 1\n";
}
void walls(){const auto&s=ma::testing::setup();int id=0;
 for(double gravity:{0.,9.81}){auto state=s.state(16,2,false);const auto cells=wave(16,2);for(std::size_t i=0;i<cells.size();++i)state.cells[i].mixture=cells[i].mixture;
  MacClosedSettings settings;settings.maximum_timestep_s=.1;settings.gravity_m_per_s2=gravity;const MacClosedWorld source(s.registry,s.eos,std::move(state),settings);const auto held=source.checkpoint();
  const auto next=source.advanced(.1);check(next.report.pressure_increment_restarts>0,"Fixed-wall/gravity fixture missed pressure restart");MacEnergyAccount energy;
  for(int endpoint:{0,1}){const auto&v=endpoint==0?source:next.world;const double sign=endpoint==0?-1:1;const auto&g=v.state().grid;
   for(std::size_t i=0;i<v.state().cells.size();++i){energy.add(sign*v.state().cells[i].mixture.energy);energy.add(MacEnergy::from_double(v.state().cells[i].mixture.mass()).multiplied(sign*gravity*(static_cast<double>(i/g.width)+.5)));}
   const auto inertia=mac_face_inertias(s.registry,v.state(),*s.eos);
   for(int axis=0;axis<2;++axis){const auto j=axis==0?mac_field_values(v.state().momentum_x,v.state().full_momentum_x):mac_field_values(v.state().momentum_y,v.state().full_momentum_y);const auto&m=axis==0?inertia.x:inertia.y;
    for(std::size_t f=0;f<j.size();++f)energy.add(MacEnergy::kinetic(m[f],j[f]).multiplied(sign));
   }
  }
  check(energy.within_bound() && source.checkpoint()==held,"Restarted fixed-wall/gravity actual U/K/G account or rollback differs");
  std::cout<<"PWALL "<<id++<<' '<<gravity<<' '<<next.report.substeps<<' '<<next.report.pressure_increment_restarts<<' '<<next.report.rejected_attempts<<" 1\n";
 }
}

}
int main(){try{require_mac_energy_environment();std::cout<<std::setprecision(17);queries();roots();owner();walls();std::cout<<"PASS anchored pressure increments, unchanged closure/account bounds, coupled roots and owning continuation\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
