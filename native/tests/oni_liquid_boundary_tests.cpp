// Public synthetic materials and independently generated old-owner fixture.
#include "../src/oni_colony.hpp"
#include "../src/oni_catalogue.hpp"
#include "../src/oni_liquid_boundary.hpp"
#include "../src/oni_sha256.hpp"
#include "../src/mac_game/layering.hpp"
#include "../src/mac_game/surface.hpp"
#include "oni_owner11_fixture.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>

namespace {
using namespace ma;
using namespace ma::experimental;
using namespace ma::oni::colony;
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
// Creates new colonies with game profile 6 while it lives
// (MA_COLONY_KERNEL3_PROFILE6=1), restoring the variable afterwards.
struct Profile6Colonies {
    std::string previous;bool present=false;
    Profile6Colonies() {if(const char* v=std::getenv("MA_COLONY_KERNEL3_PROFILE6")){previous=v;present=true;}set("1");}
    ~Profile6Colonies() {set(present?previous.c_str():nullptr);}
    static void set(const char* value) {
#ifdef _WIN32
        _putenv_s("MA_COLONY_KERNEL3_PROFILE6",value?value:"");
#else
        if(value)setenv("MA_COLONY_KERNEL3_PROFILE6",value,1);else unsetenv("MA_COLONY_KERNEL3_PROFILE6");
#endif
    }
};
constexpr std::uint32_t oxygen=3,marker=4,water=5,steam=6;
constexpr double dt=static_cast<double>(.2f);
MAOniColonyMaterial material(const char* id,int hash,unsigned native,bool gas,double molar,double cp,double rho=0) {
    MAOniColonyMaterial m{};m.struct_size=sizeof(m);m.abi_version=1;m.element_hash=hash;m.native_index=native;m.phase=gas?1u:2u;
    m.molar_mass_kg_per_mol=molar;m.specific_heat_j_per_kg_k=cp;m.density_kg_per_m3=rho;m.conductivity_w_per_m_k=gas?.02:.5;
    m.low_temperature_k=gas?50:250;m.high_temperature_k=gas?10000:700;std::snprintf(m.id,sizeof(m.id),"%s",id);return m;
}
std::vector<MAOniColonyMaterial> materials() {
    auto w=material("Water",222,water,false,.018015,4184,1000);w.high_target=333;w.high_temperature_k=373.15;
    auto v=material("Steam",333,steam,true,.018015,2010);v.low_target=222;v.low_temperature_k=373.15;
    return {material("Oxygen",111,oxygen,true,.032,1005),material("MarkerGas",444,marker,true,.032,1005),w,v};
}
ma::oni::Catalogue catalogue(const std::vector<MAOniColonyMaterial>& mats) {
    std::vector<ma::oni::GameFluidElement> elements;
    for(const auto& m:mats) {
        ma::oni::GameFluidElement e;e.id=m.id;e.hash=m.element_hash;e.native_index=m.native_index;e.gas=m.phase==1;
        e.molar_mass_kg_per_mol=m.molar_mass_kg_per_mol;e.specific_heat_j_per_kg_k=m.specific_heat_j_per_kg_k;e.conductivity_w_per_m_k=m.conductivity_w_per_m_k;
        e.density_kg_per_m3=m.density_kg_per_m3;e.low_temperature_k=m.low_temperature_k;e.high_temperature_k=m.high_temperature_k;
        e.low_target=m.low_target;e.high_target=m.high_target;e.low_byproduct=m.low_byproduct;e.high_byproduct=m.high_byproduct;
        e.low_byproduct_fraction=m.low_byproduct_fraction;e.high_byproduct_fraction=m.high_byproduct_fraction;elements.push_back(e);
    }
    return ma::oni::build_catalogue(std::move(elements));
}
MAOniColonyConfig config() {
    MAOniColonyConfig c{};c.struct_size=sizeof(c);c.abi_version=1;c.flags=13;c.vacuum_index=9;
    c.grid_width=c.width=10;c.grid_height=c.height=8;c.gravity_m_per_s2=9.81;c.maximum_timestep_s=static_cast<double>(.1f);return c;
}
std::vector<MAOniColonyCell> cells(bool column_rock=true) {
    std::vector<MAOniColonyCell> c(80);
    for(unsigned y=1;y<=6;++y)for(unsigned x=1;x<=8;++x)
        if(!column_rock || x!=5)c[x+y*10]={9,MA_ONI_COLONY_FLUID,0,0,0,0};
    return c;
}
std::vector<std::uint8_t> marks() {std::vector<std::uint8_t>b(80);for(unsigned y=1;y<=6;++y)b[5+y*10]=1;return b;}
void tick(std::uint64_t ticket,double seconds=dt) {advance(ticket,seconds,2);wait();check(status().state==1,status().error);}
std::vector<std::uint8_t> fill_front(const MAOniColonyConfig& cfg,const std::vector<MAOniColonyMaterial>& mats) {
    start(cfg,mats,cells(),1);std::vector<MAOniColonyTransfer> fill;
    for(unsigned y=1;y<=6;++y)for(unsigned x=1;x<=8;++x)if(x!=5)
        fill.push_back({x+y*10,x<5&&y<=3?water:oxygen,MA_ONI_COLONY_INJECT,0,-1,0,x<5?(y<=3?990.:static_cast<double>(1.2f)):static_cast<double>(.3f),300,0});
    queue_transfers(fill);tick(1);return checkpoint();
}
void open_membrane() {
    std::vector<MAOniColonyTerrain> e;for(unsigned y=1;y<=6;++y)e.push_back({5+y*10,MA_ONI_COLONY_BLOCK_LIQUID});
    for(unsigned y=1;y<=6;++y)e.push_back({5+y*10,MA_ONI_COLONY_OPEN});
    queue_terrain(e);
}
double owner_mass(unsigned native,unsigned from) {
    double total=0;std::array<std::uint32_t,8> ids{};std::array<double,8> mass{};
    for(unsigned y=1;y<=6;++y)for(unsigned x=from;x<=8;++x) {
        const auto n=composition(x+y*10,ids.data(),mass.data(),8);for(unsigned k=0;k<n;++k)if(ids[k]==native)total+=mass[k];
    }return total;
}
MacClosedWorld physical(const std::vector<std::uint8_t>& image,const ma::oni::Catalogue& cat) {
    constexpr char magic[]="MACCLS01";const auto at=std::search(image.begin(),image.end(),magic,magic+8);
    check(at!=image.end(),"public owner has no core image");const auto offset=static_cast<std::size_t>(at-image.begin());
    std::uint64_t size{};std::memcpy(&size,image.data()+offset-8,8);check(size==image.size()-offset-64,"public core framing differs");
    return MacClosedWorld::from_checkpoint(cat.registry,cat.eos,std::span(image).subspan(offset,static_cast<std::size_t>(size)));
}
void rehash(std::vector<std::uint8_t>& image) {
    const auto hash=ma::oni::hash_memory(image.data(),image.size()-64);std::copy(hash.begin(),hash.end(),image.end()-64);
}
long double mass(const MacClosedWorld& w) {long double m=0;for(const auto& c:w.state().cells)for(auto v:c.mixture.masses)m+=v;return m;}
long double energy(const MacClosedWorld& w) {
    const auto& s=w.state();long double total=0;
    for(std::size_t i=0;i<s.cells.size();++i)total+=s.cells[i].mixture.energy+static_cast<long double>(s.cells[i].mixture.mass())*w.settings().gravity_m_per_s2*(static_cast<long double>(i/s.grid.width)+.5L)*s.grid.spacing_m;
    const auto I=MacGameKernelAccess::inertias(w.registry(),s);
    for(std::size_t f=0;f<s.momentum_x.size();++f)if(s.momentum_x[f])total+=.5L*s.momentum_x[f]*s.momentum_x[f]/I.x[f];
    for(std::size_t f=0;f<s.momentum_y.size();++f)if(s.momentum_y[f])total+=.5L*s.momentum_y[f]*s.momentum_y[f]/I.y[f];
    return total;
}
long double sum(const std::vector<double>& v) {return std::accumulate(v.begin(),v.end(),0.L);}
void audit(const MacClosedWorld& before,const MacClosedAdvance& after) {
    const auto& r=after.report;const auto& w=after.world;long double sink=0;for(auto v:r.trace_sink.mass_kg)sink+=mac_energy_long_double(v);
    const auto U=mac_energy_long_double(r.trace_sink.internal_energy_j)+mac_energy_long_double(r.trace_sink.kinetic_energy_j)+mac_energy_long_double(r.trace_sink.potential_energy_j);
    check(std::abs(mass(before)-mass(w)-sink)<1e-12L*mass(before),"boundary mass/sink audit");
    check(std::abs(energy(before)-energy(w)-U)<1e-12L*(std::abs(energy(before))+std::abs(energy(w))),"boundary energy/sink audit");
    check(std::abs(sum(before.state().momentum_x)+r.wall_impulse_x-sum(w.state().momentum_x)-mac_energy_long_double(r.trace_sink.momentum_x))<1e-8L,"boundary x wall impulse audit");
    check(std::abs(sum(before.state().momentum_y)+r.wall_impulse_y+r.body_impulse_y-sum(w.state().momentum_y)-mac_energy_long_double(r.trace_sink.momentum_y))<1e-8L,"boundary y wall/body impulse audit");
}
double water_beyond(const MacClosedWorld& w,std::size_t water_key) {double m=0;for(unsigned y=1;y<=6;++y)for(unsigned x=5;x<=8;++x)m+=w.state().cells[x+y*10].mixture.masses[water_key];return m;}
// The retry guard relies on these two later operators never creating the
// first liquid in a dry cell. Exercise both axes/directions and active controls.
void dry_cell_operators() {
    using namespace game_kernel;
    std::vector<Species> species(3);
    species[0].gas=true;species[0].cv=700;
    species[1].gas=species[2].gas=false;
    species[1].positive_liquid=species[2].positive_liquid=true;
    species[1].cv=species[2].cv=4200;species[1].rho0=800;species[2].rho0=1000;
    species[1].liquid_group=1;species[2].liquid_group=2;
    for(bool vertical:{false,true})for(bool reverse:{false,true}) {
        MacGrid grid;grid.width=vertical?1:2;grid.height=vertical?2:1;grid.walls.assign(2,0);
        State state;state.masses.offsets={0,1,2};
        state.masses.species=reverse?std::vector<std::uint32_t>{2,0}:std::vector<std::uint32_t>{0,2};
        state.masses.mass=reverse?std::vector<double>{500,1}:std::vector<double>{1,500};
        for(unsigned i=0;i<2;++i)state.energy.push_back(state.masses.mass[i]*species[state.masses.species[i]].cv*300);
        state.jx.assign(grid.x_width()*grid.height,0);state.jy.assign(grid.width*grid.y_height(),0);
        Flow flow(grid,species,1);const auto eos=flow.evaluate_state(state);
        check(layer(grid,species,state,eos,.2,9.81).faces==0 && sharpen(grid,species,state,eos,.2,9.81).faces==0,
            "layer/surface operator seeded a dry membrane cell");
    }
    MacGrid grid;grid.width=1;grid.height=2;grid.walls.assign(2,0);
    State state;state.masses.offsets={0,1,2};state.masses.species={1,2};state.masses.mass={400,500};
    state.energy={400*4200.*300,500*4200.*300};state.jx.assign(4,0);state.jy.assign(3,0);
    Flow flow(grid,species,1);auto eos=flow.evaluate_state(state);
    check(layer(grid,species,state,eos,.2,9.81).faces==1,"layer positive control inactive");
    state.masses.offsets={0,2,4};state.masses.species={0,2,0,2};state.masses.mass={.1,200,.1,800};
    state.energy={.1*700*300+200*4200.*300,.1*700*300+800*4200.*300};eos=flow.evaluate_state(state);
    check(sharpen(grid,species,state,eos,.2,9.81).faces>0,"surface positive control inactive");
}
void legacy_fixture() {
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};std::vector<std::uint8_t> image;
    for(std::size_t n=0;n<sizeof(owner11_control_hex)-1;n+=2)image.push_back(static_cast<std::uint8_t>(16*digit(owner11_control_hex[n])+digit(owner11_control_hex[n+1])));
    MAOniColonyConfig cfg{};std::memcpy(&cfg,image.data()+12,sizeof(cfg));std::uint32_t count{};std::memcpy(&count,image.data()+68,4);
    std::vector<MAOniColonyMaterial> mats(count);std::memcpy(mats.data(),image.data()+72,mats.size()*sizeof(mats[0]));
    restore(cfg,mats,image,1);check(checkpoint()==image,"independent native11 image changed on resave");tick(1);const auto warm=checkpoint();
    stop();restore(cfg,mats,image,1);tick(1);check(checkpoint()==warm && warm[8]==11,"legacy11 continuation or law changed");stop();
}
}

namespace {
// The airflow-tile column on the new-colony default, profile 7 (boil-only):
// under the current owner law and under the old version-11 law the water
// front stays behind the column, the column's water returns each frame and
// no water reaches it or the cells beyond. In ONI airflow tiles block liquid.
void profile7_airflow_column_control() {
    set_options(nullptr);set_diseases({}, {},0);const auto cfg=config();const auto mats=materials();const auto cat=catalogue(mats);
    const auto filled=fill_front(cfg,mats);const auto world=physical(filled,cat);
    check(world.settings().game_profile==MacGameProfile::game_kernel_sized_intervals &&
          world.settings().game.phase_rule==MacPhaseRule::boil_only,"New colony is not boil-only profile7");
    const double water_total=owner_mass(water,1);
    open_membrane();const auto pending=checkpoint();
    for(std::uint64_t t=2;t<=8;++t) {
        tick(t);check(owner_mass(water,5)==0,"Profile7 water crossed the airflow column");
    }
    check(std::abs(owner_mass(water,1)-water_total)<=1e-9*water_total,"Profile7 airflow column changed the water inventory");
    stop();
    auto old=pending;old[8]=11;rehash(old);restore(cfg,mats,old,1);
    for(std::uint64_t t=1;t<=6;++t) {
        tick(t);check(owner_mass(water,5)==0,"Profile7 water crossed the airflow column under the old law");
    }
    check(std::abs(owner_mass(water,1)-water_total)<=1e-9*water_total,"Profile7 old-law airflow column changed the water inventory");
    stop();
    std::cout<<"PROFILE7 AIRFLOW COLUMN PASS: boil-only core26; water beyond the column 0 kg for 7 frames (current law) and 6 frames (old law); water inventory "<<water_total<<" kg kept\n";
}
}

void airflow_boundary_control() {
    profile7_airflow_column_control();
    // The controls below were written for the profile-6 default; they keep
    // their inputs and bounds on that profile.
    const Profile6Colonies profile6;
    using ma::oni::colony::detail::advance_liquid_boundary;
    dry_cell_operators();
    set_options(nullptr);set_diseases({}, {},0);const auto cfg=config();const auto mats=materials();const auto cat=catalogue(mats);
    const auto filled=fill_front(cfg,mats);check(filled[8]==12 && status().completed_frames==1,"new game did not select wrapper12");
    if(const char* path=std::getenv("MA_AIRFLOW_TEST_IMAGE")) {std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(filled.data()),static_cast<std::streamsize>(filled.size()));check(bool(file),"cannot write public native12 image");}
    open_membrane();const auto pending=checkpoint();tick(2);check(owner_mass(water,5)==0,"correctly marked moving front crossed new owner boundary");
    const auto fresh=checkpoint();stop();restore(cfg,mats,pending,1);check(checkpoint()==pending,"new law pending image resave differs");tick(1);
    check(checkpoint()==fresh && owner_mass(water,5)==0,"new law cold front continuation differs");
    for(std::uint64_t t=2;t<=8;++t)tick(t);
    check(owner_mass(water,5)==0,"later water crossed new owner boundary");
    const auto cold=checkpoint();stop();restore(cfg,mats,cold,1);check(checkpoint()==cold,"new law cold state differs");stop();
    // Version11 has exactly this framing. The version alone must select the
    // old owner law; the independent old-produced image below also roundtrips.
    auto old=pending;old[8]=11;rehash(old);restore(cfg,mats,old,1);check(checkpoint()==old,"old law framing changed");tick(1);
    const double old_cross=owner_mass(water,5);check(old_cross>0,"legacy boundary law was silently changed");const auto old_end=checkpoint();stop();
    restore(cfg,mats,old,1);tick(1);check(checkpoint()==old_end && old_end[8]==11,"old law cold continuation changed");stop();legacy_fixture();
    auto malformed=pending;malformed[8]=13;rehash(malformed);bool refused=false;
    try{restore(cfg,mats,malformed,1);}catch(const std::invalid_argument&){refused=true;}check(refused,"unknown future owner law accepted");
    malformed=pending;const auto blocking_offset=72+mats.size()*sizeof(MAOniColonyMaterial)+4+80+4;malformed[blocking_offset+15]=2;rehash(malformed);refused=false;
    try{restore(cfg,mats,malformed,1);}catch(const std::invalid_argument&){refused=true;}check(refused,"nonbinary saved boundary accepted");
    // Direct liquid input and the marks arrive together, after the old early
    // mask point. The new preflow refresh closes it, then ordinary expulsion
    // returns its physical inventory to a legal neighbour without losing mass.
    start(cfg,mats,cells(false),1);std::vector<MAOniColonyTerrain> tiles;for(unsigned y=1;y<=6;++y)tiles.push_back({5+y*10,MA_ONI_COLONY_BLOCK_LIQUID});queue_terrain(tiles);
    const MAOniColonyTransfer direct{35,water,MA_ONI_COLONY_INJECT,0,-1,0,10,300,0};queue_transfers(std::span(&direct,1));tick(1);
    check(owner_mass(water,5)==0 && std::abs(owner_mass(water,1)-10)<.01,"direct-source liquid was not conservatively expelled");stop();
    // Source-specific gas passage, not recirculation from the receiving side.
    auto gas=cells(false);for(unsigned y=1;y<=6;++y)for(unsigned x=1;x<=8;++x)gas[x+y*10]={x<5?marker:oxygen,MA_ONI_COLONY_FLUID,x<5?1.2f:.3f,300,0,0};
    start(cfg,mats,gas,1);queue_terrain(tiles);for(std::uint64_t t=1;t<=16;++t)tick(t);const double passed=owner_mass(marker,6);check(passed>.05,"gas did not pass liquid boundary");stop();
    // Guard/controller controls use the same public physical snapshot. They
    // check the reports that the owner consumes only after a complete success.
    auto source=physical(filled,cat);const std::array<std::size_t,6> opening{15,25,35,45,55,65};source=source.with_opened_cells(opening);
    const auto blocked=marks();const auto initial=source.checkpoint();auto result=advance_liquid_boundary(source,dt,65536,blocked);
    check(result.retries>0 && result.added_faces<=24 && water_beyond(result.advance.world,cat.species_of_native(water))==0,"guarded public front control inactive");audit(source,result.advance);check(source.checkpoint()==initial,"failed boundary trials mutated source");
    for(const auto& invalid:{std::vector<std::uint8_t>(1),std::vector<std::uint8_t>(80,2)}) {refused=false;try{(void)advance_liquid_boundary(source,dt,65536,invalid);}catch(const std::invalid_argument&){refused=true;}check(refused && source.checkpoint()==initial,"invalid marks mutated source");}
    auto normal=source.advanced(dt);auto zero=MacGameKernelAccess::advance_guarded(source,dt,65536,std::vector<std::uint8_t>(80));check(normal.world.checkpoint()==zero.world.checkpoint(),"zero boundary metadata changed core bytes");
    try{(void)MacGameKernelAccess::advance_guarded(source,dt,65536,blocked);}catch(const game_kernel::LiquidBoundaryConflict&){}
    check(source.advanced(dt).world.checkpoint()==normal.world.checkpoint(),"failed boundary trial polluted persistent kernel cache");
    auto moving=source.state();moving.momentum_x[moving.grid.x_face(5,4)]=.125;MacClosedWorld moving_source(cat.registry,cat.eos,std::move(moving),source.settings());
    auto moving_result=advance_liquid_boundary(moving_source,dt,65536,blocked);audit(moving_source,moving_result.advance);
    auto restored=MacClosedWorld::from_checkpoint(cat.registry,cat.eos,moving_source.checkpoint());
    check(advance_liquid_boundary(restored,dt,65536,blocked).advance.world.checkpoint()==moving_result.advance.world.checkpoint(),"nonzero-J boundary cold continuation differs");
    // Condensation creates liquid after tick1 inside the membrane. Tick2 must
    // isolate every wet marked cell before flow/layer/sharpen, including faces
    // with zero pressure sweep. One batched conflict should resolve all17.
    auto vapour=source.state();vapour.grid.closed_x.clear();vapour.grid.closed_y.clear();std::fill(vapour.momentum_x.begin(),vapour.momentum_x.end(),0);std::fill(vapour.momentum_y.begin(),vapour.momentum_y.end(),0);
    for(std::size_t i=0;i<vapour.cells.size();++i){vapour.cells[i].mixture=MixtureInventory(cat.registry.size());vapour.cells[i].passive_quantities.clear();if(!vapour.grid.walls[i])vapour.cells[i].mixture.masses[cat.species_of_native(steam)]=1;}
    auto phase_source=MacClosedWorld::game_colony(cat.registry,cat.eos,std::move(vapour),std::vector<double>(80,300),source.settings());
    auto first=phase_source.advanced(dt);check(first.report.condensed_kg>0,"merged condensation control inactive");auto merged=advance_liquid_boundary(phase_source,2*dt,65536,blocked);
    check(merged.retries==1 && merged.added_faces==17,"wet entry did not batch unique incident faces");audit(phase_source,merged.advance);
    restored=MacClosedWorld::from_checkpoint(cat.registry,cat.eos,merged.advance.world.checkpoint());
    check(advance_liquid_boundary(restored,2*dt,65536,blocked).advance.world.checkpoint()==advance_liquid_boundary(merged.advance.world,2*dt,65536,blocked).advance.world.checkpoint(),"merged condensation cold continuation differs");
    std::cout<<"AIRFLOW BOUNDARY PASS: new-law water 0; old-law crossed "<<old_cross<<" kg; marker gas passed "<<passed<<" kg; front retries "<<result.retries<<"; merged wet faces "<<merged.added_faces<<" in "<<merged.retries<<" retry; M/E/J, cold and legacy bytes close\n";
}
