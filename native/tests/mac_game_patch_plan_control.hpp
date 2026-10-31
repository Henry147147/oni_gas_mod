#pragma once
#include "../src/mac_game/patch.hpp"
#include "../src/oni_sha256.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ma::testing::patch_plan_control {
namespace gk=ma::experimental::game_kernel;
namespace k=ma::experimental::mac_kernel;
struct Input {
  gk::SparseMass masses;gk::SparsePassive passives;
  std::vector<double> energy,jx,jy,qx,qy;
};
inline void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Digest {
  std::vector<std::uint8_t> bytes;
  void word(std::uint64_t value){for(unsigned shift=0;shift<64;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
  template<class T>void integers(const std::vector<T>& values){word(values.size());for(const auto value:values)word(value);}
  void doubles(const std::vector<double>& values){word(values.size());for(const auto value:values)word(std::bit_cast<std::uint64_t>(value));}
  template<class Contents>void contents(const Contents& values){integers(values.offsets);integers(values.species);doubles(values.mass);}
  std::string finish() const{return ma::oni::hash_memory(bytes.data(),bytes.size());}
};
inline std::string signature(const Input& input){Digest d;d.contents(input.masses);d.contents(input.passives);for(const auto* v:{&input.energy,&input.jx,&input.jy,&input.qx,&input.qy})d.doubles(*v);return d.finish();}
template<class Result>std::string signature(const Result& result){
  Digest d;d.contents(result.masses);d.contents(result.passives);d.doubles(result.energy);d.doubles(result.jx);d.doubles(result.jy);
  d.word(result.microticks);d.word(result.cell_updates);d.word(result.face_updates);
  for(const auto value:{result.wall_x,result.wall_y,result.wall_ops_x,result.wall_ops_y,result.mass_error})d.word(std::bit_cast<std::uint64_t>(value));
  return d.finish();
}
inline ma::experimental::MacGrid grid(){
  ma::experimental::MacGrid value{41,33,1,1,false,false,std::vector<std::uint8_t>(41*33)};
  for(std::size_t y=8;y<25;++y)if(y!=16)value.walls[20+y*41]=1;
  return value;
}
// All operands are synthetic dyadic values. Mode0 has slow faces throughout
// the grid and two small fast clusters with different clock levels: its first
// event is dense; later events alternate small logical prefixes. Mode1 is
// sparse throughout; modes2/4 reset zero sweeps/empty inventories; modes3/5
// reverse flow or remove passives before mode0 is repeated on the same handle.
inline Input input(const ma::experimental::MacGrid& source,unsigned mode){
  k::Grid shaped(source);Input out;std::vector<double> mass(shaped.N);
  out.energy.resize(shaped.N);out.jx.resize(shaped.NX);out.jy.resize(shaped.NY);out.qx.resize(shaped.NX);out.qy.resize(shaped.NY);
  for(std::size_t i=0;i<shaped.N;++i){
    out.masses.offsets.push_back(out.masses.mass.size());out.passives.offsets.push_back(out.passives.mass.size());
    if(shaped.wall[i]||mode==4)continue;
    mass[i]=1+static_cast<double>(i%7)/16;
    if(mode!=5&&i%3){out.masses.species.push_back(0);out.masses.mass.push_back(mass[i]*.75);out.masses.species.push_back(3);out.masses.mass.push_back(mass[i]*.25);}
    else{out.masses.species.push_back(0);out.masses.mass.push_back(mass[i]);}
    out.energy[i]=mass[i]*(262144+static_cast<double>(i%17)*128);
    if(mode!=5){out.passives.species.push_back(17);out.passives.mass.push_back(mass[i]/8);if(i%2){out.passives.species.push_back(99);out.passives.mass.push_back(mass[i]/32);}}
  }
  out.masses.offsets.push_back(out.masses.mass.size());out.passives.offsets.push_back(out.passives.mass.size());
  const auto faces=[&](bool x,const auto& left,const auto& right,auto& j,auto& q){
    for(std::size_t f=0;f<j.size();++f){if(left[f]==k::none||right[f]==k::none||shaped.closed(!x,f)||mode==4)continue;
      j[f]=.5*(mass[left[f]]+mass[right[f]])*(f%2?1.:-1.)/(x?256:512);
      if(mode==0||mode==3||mode==5)q[f]=(mode==3?-1.:1.)/64;
    }
  };
  faces(true,shaped.xl,shaped.xr,out.jx,out.qx);faces(false,shaped.yl,shaped.yr,out.jy,out.qy);
  if(mode!=2&&mode!=4){out.qx[source.x_face(8,8)]=(mode==3?-1.:1.)*(mode==1?.75:2.);out.qy[source.y_face(30,22)]=(mode==3?1.:-1.)*(mode==1?.125:1.);}
  return out;
}
struct Expected {const char* sha256;std::size_t microticks,cells,faces;};
// Pinned original-oracle values are generated once from committed86e5eb3,
// patch.hpp SHA6600e8e079f824a4d30a78d34c25bda00ec6a1f4d8922e4ea2a9d9a7ea799cda.
// No expected result is computed from the implementation under test at runtime.
inline constexpr std::array<Expected,6> expected{{
  {"5abd15deb369dc8b638b6a64294e0388200296351c240557363c0e285ad8dae1",8,1387,2926},{"7cf1f77b95c3b30537acc90e1a979f09d706ddbe6390b19551f0ea65aa6bedd6",2,6,21},{"842b990bafe690ca72748acaa732dcea2a0dfb13761831ff04606cf2aa4fc526",0,0,0},
  {"c20c891c605da48ca407b3f0f8b6b4d4b9a2096e6a3fc85a8c3737b907b4542c",8,1387,2926},{"2db5d9589d37913b8e58b6b6dc3d6145df80e208db0b45d6f1774aec7958c1ee",0,0,0},{"d232a56a358a783cddad5d4b9f96136b95ad96ea364d536c08ea2de79d3c78cf",8,1387,2926}
}};
template<class Patch>void analytic(){
  ma::experimental::MacGrid source{5,3,1,1,false,false,std::vector<std::uint8_t>(15,1)};source.walls[6]=source.walls[7]=0;
  k::Grid shaped(source);Input in;in.energy.resize(15);in.jx.resize(shaped.NX);in.jy.resize(shaped.NY);in.qx.resize(shaped.NX);in.qy.resize(shaped.NY);
  for(std::size_t i=0;i<15;++i){in.masses.offsets.push_back(in.masses.mass.size());in.passives.offsets.push_back(in.passives.mass.size());if(i==6||i==7){const double m=i==6?1.:2.;in.masses.species.push_back(0);in.masses.mass.push_back(m);in.passives.species.push_back(17);in.passives.mass.push_back(m/8);in.energy[i]=m*256;}}
  in.masses.offsets.push_back(2);in.passives.offsets.push_back(2);in.qx[source.x_face(2,1)]=.125;
  Patch patch(source);const auto result=patch.advance(in.masses,in.energy,in.jx,in.jy,in.qx,in.qy,0,true,&in.passives);
  check(result.masses.mass==std::vector<double>({.875,2.125})&&result.passives.mass==std::vector<double>({.109375,.265625}),"Analytic two-cell remap mass/passive flux differs");
  check(result.energy[6]==224&&result.energy[7]==544&&result.microticks==1&&result.cell_updates==2,"Analytic two-cell remap energy/clock differs");
  for(const auto* field:{&result.jx,&result.jy})for(const auto j:*field)check(j==0,"Analytic zero-momentum remap created momentum");
}
template<class Contents,class Key>void append(Contents& values,std::size_t cell,Key key,double quantity){
  const auto at=static_cast<std::ptrdiff_t>(values.offsets[cell+1]);values.species.insert(values.species.begin()+at,key);values.mass.insert(values.mass.begin()+at,quantity);
  for(std::size_t i=cell+1;i<values.offsets.size();++i)++values.offsets[i];
}
// These valid positive input quantities fail independently when an incoming
// fraction rounds the minimum subnormal to zero. The combined case pins the
// original lowest-cell error, including across parallel128-position chunks.
// Valid-output fingerprints need not be sensitive to a harmless reordering.
template<class Patch>void failure_order(const ma::experimental::MacGrid& source,Patch& patch){
  for(const unsigned failures:{1u,2u,3u}){
    auto in=input(source,0);const double tiny=std::numeric_limits<double>::denorm_min();
    if(failures&1u){append(in.masses,5+4*source.width,74u,tiny);in.qx[source.x_face(5,4)]=-.015625;}
    if(failures&2u)append(in.passives,30+25*source.width,std::uint64_t{777},tiny);
    std::string error;try{(void)patch.advance(in.masses,in.energy,in.jx,in.jy,in.qx,in.qy,0,false,&in.passives);}catch(const std::runtime_error& e){error=e.what();}
    check(error==((failures&1u)?"Private local material lost a constituent":"Private local passive lost positivity"),"Original lowest-cell material/passive error precedence differs");
    const auto good=input(source,0);const auto recovered=patch.advance(good.masses,good.energy,good.jx,good.jy,good.qx,good.qy,0,false,&good.passives);
    check(signature(recovered)==expected[0].sha256,"Remap plan recovery after a cell-update error differs");
  }
}
} // namespace ma::testing::patch_plan_control

namespace ma::testing {
template<class Patch=ma::experimental::game_kernel::PatchTransport>inline void mac_game_patch_plan_control(){
  namespace c=patch_plan_control;const auto source=c::grid();
  for(const unsigned workers:{1u,4u}){
    c::k::Pool pool(workers);Patch patch(source,&pool);
    for(const unsigned mode:{0u,1u,2u,3u,4u,5u,0u}){
      const auto in=c::input(source,mode);const auto before=c::signature(in);const auto result=patch.advance(in.masses,in.energy,in.jx,in.jy,in.qx,in.qy,0,mode%2!=0,&in.passives);const auto& gold=c::expected[mode];
      c::check(c::signature(in)==before,"Remap plan mutated its input");
      c::check(result.microticks==gold.microticks&&result.cell_updates==gold.cells&&result.face_updates==gold.faces,"Pinned original remap work differs");
      c::check(c::signature(result)==gold.sha256,"Pinned original complete remap output differs");
      if(mode==2)c::check(result.masses.mass==in.masses.mass&&result.passives.mass==in.passives.mass&&result.energy==in.energy&&result.jx==in.jx&&result.jy==in.jy,"Zero sweep changed an inventory or momentum");
    }
    c::failure_order(source,patch);
  }
  c::analytic<Patch>();std::cout<<"PASS remap plan reuse: pinned original dense/sparse prefixes, reset, workers1/4, lowest-cell error/recovery and analytic two-cell flux\n";
}
} // namespace ma::testing
