#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_parcel.hpp"
#include <bit>
#include <fstream>
#include <iomanip>

namespace {
using namespace ma::testing;
void check(bool ok,const char* text) { if(!ok)throw std::runtime_error(text); }
MacClosedWorld restore(const MacClosedWorld& world) {
    const auto bytes=world.checkpoint();
    auto result=MacClosedWorld::from_checkpoint(world.registry(),std::make_shared<const FluidThermodynamics>(world.thermodynamics()),bytes);
    check(result.checkpoint()==bytes && fingerprint(result.state())==fingerprint(world.state()),"Primary-U checkpoint lost carried state");
    return result;
}
void edits(int kind,MacNumericalKernel kernel) {
    const auto& s=setup();auto state=s.state(2,1,false);
    for(auto& cell:state.cells) {
        cell.mixture=s.mixture(1,kind==1 ? 220000000. : 100000.,300,kind==0 ? 0 : kind==1 ? 1 : .3);
        cell.passive_quantities.clear();
    }
    MacClosedSettings settings;settings.numerical_kernel=kernel;
    MacClosedWorld world(s.registry,s.eos,state,settings);
    const auto original=world.checkpoint();check(original[8]==(kernel==MacNumericalKernel::production ? 6 : 2),"Zero remainder changed legacy bytes");
    const double quantum=std::nextafter(state.cells[0].mixture.energy,std::numeric_limits<double>::infinity())-state.cells[0].mixture.energy;
    std::size_t rounded=0;
    for(int n=0;n<31;++n) {
        const auto before=world.state().cells[0];const double q=(n%3==0 ? -1 : 1)*quantum*.125;
        const std::array<MacCellHeat,2> input{{{0,q,1000},{1,-q,1000}}};MacPrimaryRoundingCounts counts;
        world=world.with_cell_heat(input,&counts);rounded+=counts.rounded;
        auto difference=mac_cell_internal_energy_change(before,world.state().cells[0]);difference.add(-q);
        check(difference.sign()==0,"Small native heat edit lost energy across high and low parts");
        const auto& after=world.state().cells[0];const auto a=mac_stored_remainder_value(before.energy_remainder_j),b=mac_stored_remainder_value(after.energy_remainder_j);
        std::cout<<"UCARRY "<<kind<<' '<<static_cast<int>(kernel)<<' '<<n<<' '<<before.mixture.energy<<' '<<a.mantissa()<<' '<<a.exponent()<<' '<<q<<' '<<after.mixture.energy<<' '<<b.mantissa()<<' '<<b.exponent()<<' '<<counts.rounded<<' '<<counts.discarded<<'\n';
        world=restore(world);
    }
    check(rounded>0,"Small heat writes did not count inexact high parts");
    const auto image=world.checkpoint();check(image[8]==10,"Carried primary-U state did not select version 10");
    const auto held=fingerprint(world.state());MacPrimaryRoundingCounts rejected{7,8};bool failed=false;
    const std::array<MacCellHeat,2> invalid{{{0,quantum*.125,1000},{state.cells.size(),1,1000}}};
    try { static_cast<void>(world.with_cell_heat(invalid,&rejected)); }catch(const std::invalid_argument&){failed=true;}
    check(failed && rejected.rounded==7 && rejected.discarded==8 && fingerprint(world.state())==held,"Failed heat batch published a remainder or count");
    auto one=mac_kernel_default_options();one.threads=1;set_mac_kernel_default_options(one);
    const auto a=world.advanced(.001,4096).world;
    auto twelve=one;twelve.threads=12;set_mac_kernel_default_options(twelve);
    const auto b=restore(world).advanced(.001,4096).world;
    check(a.checkpoint()==b.checkpoint(),"Carried U changed checkpoint continuation across thread counts");
    set_mac_kernel_default_options(one);
}
void void_parcels() {
    const auto& s=setup();auto state=s.state(4,3,true);
    state.occupied_volumes_m3.resize(state.cells.size());
    for(std::size_t i=0;i<state.cells.size();++i) {
        const double filled=.25+.125*static_cast<double>(i%4);
        state.occupied_volumes_m3[i]=filled;
        auto& cell=state.cells[i];cell.mixture=s.mixture(filled,100000,300);cell.passive_quantities.clear();
        const double quantum=std::nextafter(cell.mixture.energy,std::numeric_limits<double>::infinity())-cell.mixture.energy;
        cell.energy_remainder_j={quantum*.125,{}};
    }
    const MacMassFluxes flux{std::vector<double>(state.momentum_x.size(),.0625),std::vector<double>(state.momentum_y.size(),0)};
    const auto plan=reconstruct_mac_interface_parcels(s.registry,*s.eos,state,std::vector<double>(state.cells.size(),1),flux,.4,state.occupied_volumes_m3);
    MacEnergyAccount account;
    for(const auto& cell:state.cells)account_mac_cell_internal_energy(account,cell,-1);
    for(const auto& cell:plan.retained)account_mac_cell_internal_energy(account,cell);
    for(const auto& parcel:plan.parcels) {
        check(canonical_mac_primary(parcel.contents.mixture.energy,parcel.contents.energy_remainder_j),"Void strip has a noncanonical carried U");
        account.add(mac_parcel_quantity(parcel,parcel.contents.mixture.energy));
        account.add(mac_parcel_quantity(parcel,mac_stored_remainder_value(parcel.contents.energy_remainder_j)));
    }
    check(!plan.parcels.empty() && account.within_bound(),"Void strips lost total high-plus-remainder U");
    MacClosedSettings settings;settings.vacuum=MacVacuumRepresentation::void_phase;
    const MacClosedWorld world(s.registry,s.eos,state,settings);
    check(restore(world).state().occupied_volumes_m3==state.occupied_volumes_m3,"Version-10 U checkpoint lost void volumes");
    std::cout<<"PASS carried U in void strips and occupied-volume checkpoints\n";
}
void range_and_rejections(const char* output) {
    const auto& s=setup();auto state=s.state(2,1,false);for(auto& c:state.cells)c.passive_quantities.clear();
    const auto low=MacEnergy::from_parts(-.5,MacExponent::from_decimal("-9223372036854775809"));
    state.cells[0].energy_remainder_j={low.project().value,low};
    MacClosedWorld world(s.registry,s.eos,state);const auto image=world.checkpoint();
    check(image[8]==10 && restore(world).state().cells[0].energy_remainder_j.full.exponent()==low.exponent(),"Below-range U correction was flattened");
    if(output) {std::ofstream out(output,std::ios::binary);out.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));check(out.good(),"Could not save range-U checkpoint");}
    for(int bad=0;bad<5;++bad) {
        auto malformed=state;
        if(bad==0)malformed.cells[0].energy_remainder_j.projected=1;
        if(bad==1)malformed.cells[0].energy_remainder_j={malformed.cells[0].mixture.energy,{}};
        if(bad==2){malformed.cells[0].mixture=MixtureInventory(s.registry.size());}
        if(bad==3){malformed.grid.walls[0]=1;malformed.cells[0].mixture=MixtureInventory(s.registry.size());}
        if(bad==4)malformed.cells[0].energy_remainder_j={-0.,{}};
        bool rejected=false;try { static_cast<void>(MacClosedWorld(s.registry,s.eos,malformed)); }catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Malformed, vacuum or wall primary-U remainder was admitted");
    }
    auto redundant=image;
    // This fixture has one full remainder; its final cell is scalar zero.
    redundant.at(redundant.size()-9)=2;
    std::uint64_t checksum=UINT64_C(14695981039346656037);
    for(std::size_t i=0;i+8<redundant.size();++i)checksum=(checksum^redundant[i])*UINT64_C(1099511628211);
    for(unsigned i=0;i<8;++i)redundant[redundant.size()-8+i]=static_cast<std::uint8_t>(checksum>>(8*i));
    bool rejected=false;try { static_cast<void>(MacClosedWorld::from_checkpoint(s.registry,s.eos,redundant)); }catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Checksummed invalid primary-U full flag was admitted");
    std::cout<<"URANGE -0.5 -9223372036854775809 6\n";
}
}
int main(int argc,char** argv) {
    try {
        require_mac_energy_environment();std::cout<<std::setprecision(17);
        for(int kind=0;kind<3;++kind)for(auto kernel:{MacNumericalKernel::reference,MacNumericalKernel::production})edits(kind,kernel);
        void_parcels();range_and_rejections(argc==2 ? argv[1] : nullptr);
        std::cout<<"PASS carried primary-U heat, checkpoint, continuation, range and atomicity\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
