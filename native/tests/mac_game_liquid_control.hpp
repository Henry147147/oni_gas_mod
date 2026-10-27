#pragma once
#include "mac_fixture.hpp"
#include "../src/mac_game/liquid.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <numeric>
#include <random>

namespace ma::testing::positive_game {
namespace p = ma::experimental::game_kernel::positive_liquid;
using namespace ma::experimental;
inline void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
inline void rewrite_checksum(std::vector<std::uint8_t> &image) {
  std::uint64_t sum=14695981039346656037ULL;
  for(std::size_t i=0;i<image.size()-8;++i){sum^=image[i];sum*=1099511628211ULL;}
  for(unsigned j=0;j<8;++j)image[image.size()-8+j]=static_cast<std::uint8_t>(sum>>(8*j));
}
inline std::uint32_t word(const std::vector<std::uint8_t> &image, std::size_t at) {
  std::uint32_t value=0;
  for(unsigned j=0;j<4;++j)value|=std::uint32_t(image.at(at+j))<<(8*j);
  return value;
}
inline void run() {
  for(double pressure : {1e3,2e7,7e7,3e8,1e12}) {
    const double K=4e7,rho=1000,h=pressure*1e-6;
    const double derivative=(p::specific_elastic(pressure+h,K,rho)-p::specific_elastic(pressure-h,K,rho))/(2*h);
    const double work=-pressure/rho*(p::volume_fraction(pressure+h,K)-p::volume_fraction(pressure-h,K))/(2*h);
    if(pressure!=.5*K)check(std::abs(derivative-work)<=1e-5*std::abs(work),"Liquid elastic energy disagrees with pressure work");
    check(p::volume_fraction(pressure,K)>0,"Nonpositive constituent volume");
  }
  for(int n=0;n<20;++n) {
    const double pressure=std::ldexp(2e7,n),K=4e7,lo=std::nextafter(pressure,0.),hi=std::nextafter(pressure,INFINITY);
    check(std::abs(p::volume_fraction(lo,K)-p::volume_fraction(hi,K))<=1e-14*p::volume_fraction(pressure,K),"Liquid volume is discontinuous");
    check(std::abs(p::specific_elastic(lo,K,1000)-p::specific_elastic(hi,K,1000))<=1e-14*p::specific_elastic(pressure,K,1000),"Liquid energy is discontinuous");
  }
  std::mt19937 random(2417);
  for(unsigned test=0;test<2000;++test) {
    std::vector<std::pair<double,double>> liquids;
    for(unsigned j=0;j<1+test%4;++j)liquids.push_back({.01+double(random()%10000)/1000,std::ldexp(1e6,int(random()%12))});
    const auto each=[&](auto emit){for(const auto &[v,K]:liquids)emit(v,K);};
    const double V=1,T=300,C=100000,nR=test%5==0?0:std::ldexp(1e-3,int(random()%30));
    const auto initial=p::coefficients(0,each);
    const double guess=p::positive_quadratic(initial.c,V-initial.b,nR*T);
    const auto a=p::pressure(V,0,0,nR*T,true,guess,each);
    const double energy=C*T+.5*a.coefficients.c*a.pressure*a.pressure+a.coefficients.d;
    const auto b=p::pressure(V,nR/C,energy,0,false,guess,each);
    const double volume=b.coefficients.b-b.coefficients.c*b.pressure,
                 temperature=(energy-(.5*b.coefficients.c*b.pressure*b.pressure+b.coefficients.d))/C;
    check(volume>0 && std::abs(a.pressure-b.pressure)<=1e-11*std::max(1.,a.pressure),"Liquid pressure closures disagree");
    check(std::abs(temperature-T)<=1e-9*T,"Liquid caloric closure disagrees");
    if(nR==0 && b.pressure==0)check(volume<=V,"Free liquid exceeds cell volume");
    else check(std::abs(volume+(nR>0?nR*temperature/b.pressure:0)-V)<=1e-11,"Liquid mixture volume closure failed");
  }
  const auto &f=setup();
  auto state=f.state(3,3,false);
  for(std::size_t i=0;i<state.cells.size();++i){state.grid.walls[i]=i!=4;state.cells[i].mixture=MixtureInventory(f.registry.size());state.cells[i].passive_quantities.clear();}
  const auto oxygen=f.registry.index("oxygen"),water=f.registry.index("water");
  state.cells[4].mixture.masses[oxygen]=3000;
  state.cells[4].mixture.masses[water]=.001;
  MacClosedSettings cfg;cfg.numerical_kernel=MacNumericalKernel::game;
  cfg.game_profile=MacGameProfile::game_kernel_positive_liquid;cfg.vacuum=MacVacuumRepresentation::void_phase;
  cfg.maximum_timestep_s=.2;cfg.gravity_m_per_s2=9.81;cfg.trace_sink_threshold_kg=1e-11;
  cfg.game.liquid_law=MacGameLiquidLaw::positive_segments;cfg.game.pressure_maximum_iterations=400;cfg.game.heat_maximum_iterations=800;
  auto world=MacClosedWorld::game_colony(f.registry,f.eos,state,std::vector<double>(9,300),cfg);
  const auto reading=world.cell_reading(4);
  check(reading.mechanical_pressure_pa>2.2e9*.04 && std::abs(reading.temperature_k-300)<1e-9,"High-pressure liquid control did not exercise extension");
  check(reading.liquid_volume_m3>0 && std::abs(reading.gas_volume_m3+reading.liquid_volume_m3-1)<1e-12,"High-pressure reading has inconsistent volumes");
  check(std::abs(std::accumulate(reading.partial_volumes_m3.begin(),reading.partial_volumes_m3.end(),0.)-1)<1e-12,"Partial volumes disagree with state");
  const auto image=world.checkpoint();check(word(image,8)==21,"Positive liquid profile did not write core21");
  const auto cold=MacClosedWorld::from_checkpoint(f.registry,f.eos,image);
  check(cold.checkpoint()==image && cold.settings().game.liquid_law==MacGameLiquidLaw::positive_segments && cold.settings().game.pressure_maximum_iterations==400 && cold.settings().game.heat_maximum_iterations==800,"Core21 omitted saved liquid law/work caps");
  const auto saved=mac_kernel_default_options();auto workers=saved;workers.threads=1;set_mac_kernel_default_options(workers);
  const auto one=world.advanced(.2).world;workers.threads=12;set_mac_kernel_default_options(workers);
  const auto many=cold.advanced(.2).world;set_mac_kernel_default_options(saved);
  check(one.checkpoint()==many.checkpoint(),"Core21 cold/worker continuation differs");
  check(one.state().cells[4].mixture.masses==world.state().cells[4].mixture.masses && one.state().cells[4].mixture.energy==world.state().cells[4].mixture.energy,"Quiet high-pressure continuation changed mass/U");
  const std::size_t policy=16+word(image,12)+110;
  check(word(image,policy)==1 && word(image,policy+4)==400 && word(image,policy+8)==800,"Core21 policy layout differs");
  for(const auto&[offset,value] : {std::pair{policy,2U},std::pair{policy,0U},std::pair{policy+4,0U},std::pair{policy+8,65537U}}) {
    auto bad=image;for(unsigned j=0;j<4;++j)bad[offset+j]=static_cast<std::uint8_t>(value>>(8*j));rewrite_checksum(bad);bool refused=false;
    try{(void)MacClosedWorld::from_checkpoint(f.registry,f.eos,bad);}catch(const std::invalid_argument&){refused=true;}
    check(refused,"Core21 accepted noncanonical liquid law/work cap");
  }
  std::cout<<"PASS positive-liquid closed forms/work, core21 identity/refusal and cold worker continuation\n";
}
} // namespace ma::testing::positive_game
