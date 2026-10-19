// Public reference definitions only; no licensed game state or exports.
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <iomanip>
using namespace ma::testing;
MacEnergyAccount change(const MacClosedWorld& a,const MacClosedWorld& b) {
 MacEnergyAccount account;
 for(int which=0;which<2;++which) {
  const auto& world=which?b:a;const auto& state=world.state();const double sign=which?1.:-1.;
  for(const auto& cell:state.cells)account.add(sign*cell.mixture.energy);
  const auto I=mac_face_inertias(world.registry(),state,world.thermodynamics());
  for(std::size_t i=0;i<I.x.size();++i)if(I.x[i]>0)account.add(MacEnergy::kinetic(I.x[i],state.momentum_x[i]).multiplied(sign));
  for(std::size_t i=0;i<I.y.size();++i)if(I.y[i]>0)account.add(MacEnergy::kinetic(I.y[i],state.momentum_y[i]).multiplied(sign));
 }
 account.add(-1500000.);return account;
}
int main() {
 try {
  require_mac_energy_environment();const auto& s=setup();auto state=s.state(3,2,false);
  for(auto& cell:state.cells){cell.mixture=s.mixture(1,220000000.,300,1);cell.passive_quantities.clear();}
  MacClosedSettings settings;settings.numerical_kernel=MacNumericalKernel::production;settings.thermal_conduction=true;
  MacClosedWorld world(s.registry,s.eos,state,settings);
  for(int frame=0;frame<5;++frame)world=world.advanced(static_cast<double>(.2f),65536,MacExecutionPolicy::production).world;
  const auto before=world;world=world.with_cell_heat(std::array<MacCellHeat,1>{{{0,2000000,1000}}});
  world=world.with_cell_heat(std::array<MacCellHeat,1>{{{0,-500000,1000}}});
  std::size_t stages=0,unresolved=0,rejected=0;double entropy=0;MacEnergySum redistributed;
  std::cout<<std::setprecision(17);
  for(int frame=5;frame<123;++frame) {
   auto next=world.advanced(static_cast<double>(.2f),65536,MacExecutionPolicy::production);const auto& r=next.report;
   stages+=r.conduction_stages;unresolved+=r.unresolved_conduction_updates;rejected+=r.rejected_attempts;
   entropy+=r.conductive_entropy_j_per_k;redistributed.add(r.conductive_redistribution_j);world=std::move(next.world);
   if(frame%20==0 || frame==122) {
    const auto a=change(before,world);std::cout<<"WATERHEAT frame="<<frame+1<<" residual_J="<<a.residual().project().value<<" bound_J="<<a.bound().project().value
     <<" stages="<<stages<<" unresolved="<<unresolved<<" rejected="<<rejected<<" conductive_entropy="<<entropy<<" redistributed_J="<<redistributed.value().project().value<<std::endl;
   }
  }
  const auto a=change(before,world);std::cout<<"FINAL original_endpoint_account_passed="<<a.within_bound()<<std::endl;
  return a.within_bound()?0:1;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 2;}
}
