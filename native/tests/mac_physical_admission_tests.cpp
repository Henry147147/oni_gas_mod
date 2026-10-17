#include "mac_fixture.hpp"
#include <iomanip>
using namespace ma;using namespace ma::experimental;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void emit(MacScaled v){std::cout<<' '<<v.mantissa()<<' '<<v.exponent();}
void state(int endpoint,const MacTransportState& s,const FluidThermodynamics& eos,const std::vector<double>& volumes){
 const auto& set=ma::testing::setup();const auto inertia=mac_face_inertias(set.registry,s,eos,volumes);
 for(std::size_t i=0;i<s.cells.size();++i){
  std::cout<<"BCELL "<<endpoint<<' '<<i<<' '<<volumes[i]<<' '<<s.cells[i].mixture.energy<<'\n';
  for(std::size_t k=0;k<s.cells[i].mixture.masses.size();++k)if(s.cells[i].mixture.masses[k]>0)
   std::cout<<"BMASS "<<endpoint<<' '<<i<<' '<<k<<' '<<s.cells[i].mixture.masses[k]<<'\n';
  for(const auto& p:s.cells[i].passive_quantities)std::cout<<"BPASSIVE "<<endpoint<<' '<<i<<' '<<p.identity<<' '<<p.quantity<<'\n';
 }
 for(int axis=0;axis<2;++axis){
  const auto j=axis==0?mac_field_values(s.momentum_x,s.full_momentum_x):mac_field_values(s.momentum_y,s.full_momentum_y);
  const auto& inertia_values=axis==0?inertia.x:inertia.y;
  for(std::size_t i=0;i<j.size();++i){std::cout<<"BFACE "<<endpoint<<' '<<axis<<' '<<i<<' '<<inertia_values[i];emit(j[i]);std::cout<<'\n';}
 }
}
void result(int endpoint,const MacTransportReport& r){
 std::cout<<"BRESULT "<<endpoint<<' '<<r.physical_correction_fraction<<' '<<r.physical_admission_backtracks;
 for(auto v:{r.energy_residual_wide_j,r.energy_bound_wide_j,r.entropy_change_j_per_k,r.entropy_allowance_j_per_k})emit(v);
 std::cout<<'\n';
}
}
int main(){try{
 require_mac_energy_environment();std::cout<<std::setprecision(17);const auto& set=ma::testing::setup();
 auto definitions=set.eos->reference().materials();for(auto& material:definitions)material.minimum_temperature_k=300;
 FluidThermodynamics bounded(set.registry,"deformed-minimum-temperature-admission",std::move(definitions));
 auto source=set.state(8,4,true);const auto& g=source.grid;std::vector<double> volumes(source.cells.size());
 std::vector<double> qx(source.momentum_x.size()),qy(source.momentum_y.size());
 for(std::size_t y=0;y<g.height;++y)for(std::size_t x=0;x<g.width;++x){
  qx[g.x_face(x,y)]=.17*std::sin(.7853981633974483*static_cast<double>(x))+.03*std::cos(1.5707963267948966*static_cast<double>(y));
  qy[g.y_face(x,y)]=.1*std::cos(1.5707963267948966*static_cast<double>(y))+.015*std::sin(.7853981633974483*static_cast<double>(x));
 }
 for(std::size_t y=0;y<g.height;++y)for(std::size_t x=0;x<g.width;++x){
  const auto i=x+y*g.width;volumes[i]=1+qx[g.x_face((x+1)%g.width,y)]-qx[g.x_face(x,y)]+qy[g.y_face(x,(y+1)%g.height)]-qy[g.y_face(x,y)];
  const double density=std::exp(4*std::sin(.7*static_cast<double>(x)+1.1*static_cast<double>(y)));
  std::vector<double> amounts(set.registry.size());amounts[set.registry.index("nitrogen")]=density*volumes[i];amounts[set.registry.index("helium")]=density*volumes[i]*1e-28;
  source.cells[i].mixture=bounded.initialize_at_temperature(amounts,300,volumes[i]).mixture;
 }
 const auto inertia=mac_face_inertias(set.registry,source,bounded,volumes);
 MacMassFluxes flux{std::vector<double>(qx.size()),std::vector<double>(qy.size())};
 for(std::size_t y=0;y<g.height;++y)for(std::size_t x=0;x<g.width;++x){
  const auto xf=g.x_face(x,y),yf=g.y_face(x,y);
  source.momentum_x[xf]=inertia.x[xf]*(30+25*std::sin(.7853981633974483*static_cast<double>(x))*std::cos(1.5707963267948966*static_cast<double>(y)));
  source.momentum_y[yf]=inertia.y[yf]*(15+10*std::cos(.7853981633974483*static_cast<double>(x))*std::sin(1.5707963267948966*static_cast<double>(y)));
  const auto dx=(qx[xf]>0?(x+g.width-1)%g.width:x)+y*g.width;
  const auto dy=x+(qy[yf]>0?(y+g.height-1)%g.height:y)*g.width;
  flux.x[xf]=qx[xf]*source.cells[dx].mixture.mass()/volumes[dx];flux.y[yf]=qy[yf]*source.cells[dy].mixture.mass()/volumes[dy];
 }
 const auto held=ma::testing::fingerprint(source);
 const auto accepted=advance_mac_transport(set.registry,source,flux,bounded,volumes,MacMomentumReconstruction::swept_physical);
 const auto donor=advance_mac_transport(set.registry,source,flux,bounded,volumes,MacMomentumReconstruction::donor_cell);
 const auto unrestricted=advance_mac_transport(set.registry,source,flux,*set.eos,volumes,MacMomentumReconstruction::swept_physical);
 check(ma::testing::fingerprint(source)==held,"Admission control changed its source");
 check(accepted.report.physical_correction_fraction>0&&accepted.report.physical_correction_fraction<1&&accepted.report.physical_admission_backtracks>0,"No intermediate whole-state admission witness");
 check(unrestricted.report.physical_correction_fraction==1,"Broad-domain control unexpectedly limited");
 double minimum=1e9;for(const auto& cell:unrestricted.state.cells)minimum=std::min(minimum,set.eos->evaluate(cell.mixture,1).temperature_k);
 check(minimum<299.99,"Unrestricted state no longer crosses the declared boundary");
 std::cout<<"BGRID 8 4 1 1 1\nBDOMAIN 300 1000000 32\nBUNCHANGED 1\n";
 for(const auto& m:set.eos->reference().materials())std::cout<<"BMATERIAL "<<set.registry.index(m.id)<<' '<<(m.phase==ReferencePhase::ideal_gas?0:1)<<' '<<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<' '<<m.energy_offset_j_per_kg<<' '<<m.zero_pressure_density_kg_per_m3<<' '<<m.reference_bulk_modulus_pa<<'\n';
 for(std::size_t i=0;i<qx.size();++i){std::cout<<"BSWEEP 0 "<<i<<' '<<qx[i]<<'\n'<<"BSWEEP 1 "<<i<<' '<<qy[i]<<'\n';std::cout<<"BFLUX 0 "<<i<<' '<<flux.x[i]<<'\n'<<"BFLUX 1 "<<i<<' '<<flux.y[i]<<'\n';}
 state(0,source,*set.eos,volumes);const std::vector<double> nominal(source.cells.size(),1);
 state(1,accepted.state,*set.eos,nominal);state(2,donor.state,*set.eos,nominal);state(3,unrestricted.state,*set.eos,nominal);
 result(1,accepted.report);result(2,donor.report);result(3,unrestricted.report);
 std::cout<<"BEND 4\nPASS intermediate EOS admission preserves actual state accounts\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
