#include "../src/oni_domain.hpp"
#include "../src/oni_actor_checkpoint.hpp"
#include "../src/oni_configured_domain.hpp"
#include "../src/oni_frame_inputs.hpp"
#include "mixed_atmospheres/mac_energy.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace ma::oni;
using ma::experimental::MacEnergyAccount;
using Clock=std::chrono::steady_clock;
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class Error=std::invalid_argument,class Action> void rejects(Action action,const char* why) {
    bool rejected=false;
    try { action(); } catch (const Error&) { rejected=true; }
    require(rejected,why);
}
constexpr std::uint32_t native_width=300,ox=10,oy=10;
constexpr std::uint32_t public_cell(std::uint32_t x,std::uint32_t y) { return ox+x+(oy+y)*native_width; }
constexpr int native_cell(std::uint32_t x,std::uint32_t y) { return static_cast<int>(ox+x+1+(oy+y+1)*(native_width+2)); }
MAOniDomainConfig configured(std::uint32_t width,std::uint32_t height) {
    return {40,2,1,native_width,ox,oy,width,height,MA_ONI_DOMAIN_CONFIGURED_SCENARIO,0};
}
MAOniDomainPhysics physics(std::uint32_t flags,double gravity=0,double shear=0,double bulk=0) { return {40,1,flags,0,gravity,shear,bulk}; }
MAOniDomainInitialCell record(std::array<double,5> masses,double temperature) {
    MAOniDomainInitialCell cell{};cell.struct_size=56;cell.abi_version=1;
    std::copy(masses.begin(),masses.end(),std::begin(cell.masses));cell.temperature_k=temperature;return cell;
}
MAOniDomainInitialCell solid_record() { return record({},0); }
double at(std::uint32_t value) { return static_cast<double>(value); }
struct Room {
    MAOniDomainConfig config{};
    MAOniDomainPhysics settings{};
    std::vector<std::uint8_t> mask;
    std::vector<MAOniDomainInitialCell> cells;
    std::uint32_t cell(std::uint32_t x,std::uint32_t y) const { return x+y*config.width; }
};
Room room(std::uint32_t width,std::uint32_t height,const MAOniDomainPhysics& settings,
    const std::function<bool(std::uint32_t,std::uint32_t)>& solid,
    const std::function<MAOniDomainInitialCell(std::uint32_t,std::uint32_t)>& fluid) {
    Room result{configured(width,height),settings,{},{}};
    for (std::uint32_t y=0;y<height;++y) for (std::uint32_t x=0;x<width;++x) {
        const bool wall=solid(x,y);result.mask.push_back(wall ? 1 : 0);result.cells.push_back(wall ? solid_record() : fluid(x,y));
    }
    return result;
}
void start(const Room& value) { start_configured_domain(value.config,value.settings,value.mask,1,{},0,value.cells); }
MAOniDomainStatus capture(std::vector<MAOniDomainCell>& cells) {
    MAOniDomainStatus value{};copy_domain(value,cells.data(),static_cast<std::uint32_t>(cells.size()));
    if (value.state!=1) throw std::runtime_error(std::string("Configured owner faulted: ")+value.error);
    return value;
}
std::uint32_t drain(std::vector<MAOniDomainCell>& cells) {
    MAOniDomainStatus source{};std::array<MAOniDomainReceipt,64> receipts{};std::uint32_t count=0;
    copy_domain_receipts(source,cells.data(),static_cast<std::uint32_t>(cells.size()),receipts.data(),64,count);
    if (source.state!=1) throw std::runtime_error(std::string("Configured owner faulted: ")+source.error);
    if (count) ack_domain_receipts(source.session,receipts[count-1].sequence);
    return count;
}
bool zero_cell(const MAOniDomainCell& cell) { const MAOniDomainCell zero{};return std::memcmp(&cell,&zero,sizeof(cell))==0; }
std::vector<std::uint8_t> physical_part(const std::vector<std::uint8_t>& image) { return configured_domain::decode(image).physical; }
std::vector<std::uint8_t> reseal(std::vector<std::uint8_t> image) {
    const auto digest=hash_memory(image.data(),image.size()-64);std::copy(digest.begin(),digest.end(),image.end()-64);return image;
}
long peak_resident_kib() {
#if defined(__linux__)
    std::ifstream input("/proc/self/status");std::string line;
    while (std::getline(input,line)) if (line.rfind("VmHWM:",0)==0) return std::stol(line.substr(6));
#endif
    return -1;
}

// Admission accepts any 0<g<=100. One rigid fluid row under solids at
// 4e-308 m/s2 keeps a normal potential energy but a subnormal frame impulse;
// that must fault the scalar gravity diagnostics rather than be rounded away.
void below_range_gravity() {
    const auto value=room(2,2,physics(MA_ONI_DOMAIN_PHYSICS_GRAVITY,4e-308),
        [](auto,auto y) { return y==1; },[](auto,auto) { return record({.9,.27,0,0,0},290); });
    start(value);std::vector<MAOniDomainCell> cells(value.mask.size());(void)capture(cells);
    advance_domain(1,.2,2);
    MAOniDomainStatus status{};copy_domain(status,cells.data(),static_cast<std::uint32_t>(cells.size()));
    require(status.state!=1 && std::string(status.error).find("not exactly representable")!=std::string::npos,
        "A below-range body impulse was recorded as zero");
    shutdown_domain();
    std::cout<<"PASS below-range gravity diagnostics fault the configured owner\n";
}
Room probe_room() {
    return room(4,3,physics(MA_ONI_DOMAIN_PHYSICS_GRAVITY|MA_ONI_DOMAIN_PHYSICS_CONDUCTION,9.81),
        [](auto x,auto y) { return x==1 && y==1; },[](auto x,auto y) { return record({.9,.27+.01*at(x),1e-28,0,0},290+at(x+y)); });
}
void admission_rejection() {
    static_assert(sizeof(MAOniDomainPhysics)==40);
    require(ma_oni_domain_configured_abi_version()==1 && MA_ONI_DOMAIN_CONFIGURED_SCENARIO==7,"Configured capability ABI differs");
    const auto base=probe_room();
    auto refuse=[](const Room& value,const char* why) {
        rejects([&] { start(value); },why);
        require(!domain_active(),"A rejected configured start activated ownership");
    };
    // Geometry and opt-in.
    for (int fault=0;fault<16;++fault) {
        auto bad=base;auto& g=bad.config;
        if (fault==0) g.scenario=2;
        if (fault==1) g.scenario=8;
        if (fault==2) g.flags=0;
        if (fault==3) g.flags=2;
        if (fault==4) g.reserved=1;
        if (fault==5) g.struct_size=36;
        if (fault==6) g.abi_version=1;
        if (fault==7) g.offset_x=0;
        if (fault==8) g.offset_y=0;
        if (fault==9) g.native_grid_width=5;
        if (fault==10) g.offset_x=native_width-4;
        if (fault==11) g.offset_y=static_cast<std::uint32_t>(std::numeric_limits<int>::max()/(native_width+2));
        if (fault==12) { g.width=1;bad.mask.resize(3);bad.cells.resize(3); }
        if (fault==13) { g.width=257;g.height=2;bad.mask.assign(514,0);bad.cells.assign(514,base.cells[0]); }
        if (fault==14) { g.width=65;g.height=64;bad.mask.assign(4160,0);bad.cells.assign(4160,base.cells[0]); }
        // (offset_y+height+2)*(pitch+2) is exactly 2^64 here: the unbounded
        // uint64 product would wrap to zero and pass the int guard.
        if (fault==15) { g.native_grid_width=std::numeric_limits<std::uint32_t>::max()-1;g.offset_y=g.native_grid_width-g.height; }
        refuse(bad,"Invalid configured geometry, size or opt-in was admitted");
    }
    // Physics record.
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    for (int fault=0;fault<20;++fault) {
        auto bad=base;auto& p=bad.settings;
        if (fault==0) p.struct_size=32;
        if (fault==1) p.abi_version=2;
        if (fault==2) p.reserved=1;
        if (fault==3) p.flags|=64;
        if (fault==4) p.flags|=MA_ONI_DOMAIN_PHYSICS_MIXTURE|MA_ONI_DOMAIN_PHYSICS_LIQUID;
        if (fault==5) p.gravity_m_per_s2=0;
        if (fault==6) p.gravity_m_per_s2=-9.81;
        if (fault==7) p.gravity_m_per_s2=nan;
        if (fault==8) p.gravity_m_per_s2=inf;
        if (fault==9) p.gravity_m_per_s2=100.5;
        if (fault==10) { p.flags&=~MA_ONI_DOMAIN_PHYSICS_GRAVITY; }
        if (fault==11) { p.flags&=~MA_ONI_DOMAIN_PHYSICS_GRAVITY;p.gravity_m_per_s2=-0.0; }
        if (fault==12) p.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;
        if (fault==13) { p.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;p.shear_viscosity_pa_s=-1; }
        if (fault==14) { p.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;p.shear_viscosity_pa_s=nan;p.bulk_viscosity_pa_s=.25; }
        if (fault==15) { p.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;p.bulk_viscosity_pa_s=1001; }
        if (fault==16) { p.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;p.shear_viscosity_pa_s=-0.0;p.bulk_viscosity_pa_s=.25; }
        if (fault==17) p.shear_viscosity_pa_s=.5;
        if (fault==18) p.bulk_viscosity_pa_s=-0.0;
        if (fault==19) { p.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;p.shear_viscosity_pa_s=inf; }
        refuse(bad,"Invalid configured physics record was admitted");
    }
    // Solid mask.
    for (int fault=0;fault<3;++fault) {
        auto bad=base;
        if (fault==0) bad.mask.pop_back();
        if (fault==1) bad.mask[5]=2;
        if (fault==2) { std::fill(bad.mask.begin(),bad.mask.end(),1);std::fill(bad.cells.begin(),bad.cells.end(),solid_record()); }
        refuse(bad,"Invalid configured solid mask was admitted");
    }
    // Initial records: late faults in every position class.
    for (int fault=0;fault<14;++fault) {
        auto bad=base;auto& wall=bad.cells[base.cell(1,1)];auto& last=bad.cells.back();
        if (fault==0) bad.cells.pop_back();
        if (fault==1) wall.masses[0]=1e-30;
        if (fault==2) wall.temperature_k=300;
        if (fault==3) wall.temperature_k=nan;
        if (fault==4) last=record({},300);
        if (fault==5) last.masses[1]=-1;
        if (fault==6) last.masses[0]=nan;
        if (fault==7) last.temperature_k=inf;
        if (fault==8) last.temperature_k=0;
        if (fault==9) last.masses[3]=1;
        if (fault==10) last.struct_size=48;
        if (fault==11) last.abi_version=2;
        if (fault==12) wall.abi_version=0;
        if (fault==13) { bad.settings.flags|=MA_ONI_DOMAIN_PHYSICS_LIQUID;last=record({0,0,0,1000,0},300); }
        refuse(bad,"Invalid configured initial record was admitted");
    }
    rejects([&] { start_configured_domain(base.config,base.settings,base.mask,1,{},1,base.cells); },"Configured cells were admitted with a saved clock");
    std::vector<std::uint8_t> bogus(64,7);
    rejects([&] { start_configured_domain(base.config,base.settings,base.mask,1,bogus,0,base.cells); },"Configured start admitted both cells and an image");
    rejects([&] { start_domain(base.config,1,{},0,base.cells); },"Legacy start admitted the configured scenario");
    rejects([&] { validate_domain_checkpoint(base.config,bogus,0); },"Legacy validation admitted the configured scenario");
    require(!domain_active(),"Configured admission failure activated ownership");
    // An active legacy owner is unchanged by every configured request.
    MAOniDomainConfig legacy{40,2,1,native_width,ox,oy,3,2,0,0};start_domain(legacy,1);
    std::vector<MAOniDomainCell> cells(6),after(6);const auto status=capture(cells);const auto held=snapshot_domain(status.session,0);
    rejects<std::logic_error>([&] { start(base); },"Configured start replaced an active legacy owner");
    rejects([&] { MAOniDomainConfig c{};MAOniDomainPhysics p{};std::vector<std::uint8_t> m;copy_domain_configuration(status.session,c,p,m); },
        "A legacy owner reported a configured physics record");
    rejects<std::logic_error>([&] { MAOniDomainConfig c{};MAOniDomainPhysics p{};std::vector<std::uint8_t> m;copy_domain_configuration(status.session+1,c,p,m); },
        "A stale session copied a configuration");
    const auto check=capture(after);
    require(check.session==status.session && snapshot_domain(status.session,0)==held &&
        std::memcmp(cells.data(),after.data(),sizeof(MAOniDomainCell)*cells.size())==0,"A rejected configured request changed the legacy owner");
    require(suppress_domain_cell(MA_ONI_TEMPERATURE,native_cell(0,0)),"Legacy rectangle ownership changed");
    shutdown_domain();
    // Exported wrappers: outputs are unchanged on every failure. The shared
    // library is unattached here, so a well-formed start reports BUSY.
    MAOniDomainStatus output{};output.struct_size=sizeof(output);output.abi_version=MA_ONI_DOMAIN_ABI_VERSION;const auto output_held=output;
    const auto count=static_cast<std::uint32_t>(base.mask.size());
    require(ma_oni_domain_start_configured(nullptr,&base.settings,base.mask.data(),count,base.cells.data(),count,&output)==MA_ONI_INVALID_ARGUMENT &&
        ma_oni_domain_start_configured(&base.config,nullptr,base.mask.data(),count,base.cells.data(),count,&output)==MA_ONI_INVALID_ARGUMENT &&
        ma_oni_domain_start_configured(&base.config,&base.settings,base.mask.data(),count-1,base.cells.data(),count,&output)==MA_ONI_INVALID_ARGUMENT &&
        ma_oni_domain_start_configured(&base.config,&base.settings,base.mask.data(),count,base.cells.data(),count-1,&output)==MA_ONI_INVALID_ARGUMENT &&
        ma_oni_domain_start_configured(&base.config,&base.settings,base.mask.data(),count,base.cells.data(),count,&output)==MA_ONI_BUSY &&
        ma_oni_domain_restore_configured(&base.config,&base.settings,base.mask.data(),count,bogus.data(),64,0,&output)==MA_ONI_BUSY &&
        ma_oni_domain_restore_configured(&base.config,&base.settings,base.mask.data(),count,nullptr,0,0,&output)==MA_ONI_INVALID_ARGUMENT &&
        ma_oni_domain_configured_checkpoint_validate(&base.config,&base.settings,base.mask.data(),count,bogus.data(),64,0)==MA_ONI_INVALID_ARGUMENT &&
        std::memcmp(&output,&output_held,sizeof(output))==0,"Configured C ABI accepted invalid inputs or changed output");
    MAOniDomainConfig copied{};MAOniDomainPhysics copied_physics{};std::array<std::uint8_t,16> copied_mask{};std::uint32_t copied_count=99;
    const auto copied_held=copied_mask;
    require(ma_oni_domain_configuration_copy(1,&copied,&copied_physics,copied_mask.data(),16,&copied_count)==MA_ONI_BUSY &&
        ma_oni_domain_configuration_copy(1,nullptr,&copied_physics,copied_mask.data(),16,&copied_count)==MA_ONI_INVALID_ARGUMENT &&
        ma_oni_domain_configuration_copy(1,&copied,&copied_physics,nullptr,16,&copied_count)==MA_ONI_INVALID_ARGUMENT &&
        copied_count==99 && copied_mask==copied_held,"Inactive configuration copy changed outputs");
    std::cout<<"PASS configured admission: 16 geometry, 20 physics, 3 mask and 16 record faults, legacy isolation and C ABI rejection without state change\n";
}

struct Trace {
    std::vector<std::vector<std::uint8_t>> images;
    std::vector<MAOniDomainCell> cells;
    std::vector<MAOniDomainReceipt> receipts;
    MAOniDomainStatus status{};
    MAOniDomainThermalStatus thermal{};
    MAOniDomainViscosityStatus viscosity{};
    MAOniDomainGravityStatus gravity{};
};
Trace run_sequence(bool configured_session,std::uint32_t injected,std::uint32_t consumed) {
    Trace trace;std::vector<MAOniDomainCell> cells(6);auto status=capture(cells);
    const auto physical=[&](std::vector<std::uint8_t> image) { return configured_session ? physical_part(image) : image; };
    trace.images.push_back(physical(checkpoint_domain(status.session,0)));
    MAOniDomainHeat heat{48,1,status.session,0,public_cell(0,0),3,500,1e6};MAOniDomainHeatStatus heat_status{};queue_domain_heat(&heat,1,heat_status);
    const std::array<MAOniDomainTransfer,2> transfers{{{64,2,status.session,0,public_cell(1,0),injected,1,1,5,0,.015625,310},
        {64,2,status.session,0,public_cell(2,1),consumed,2,1,6,0,.0078125,0}}};
    MAOniDomainTransferStatus transfer_status{};queue_domain_transfers(transfers.data(),2,transfer_status);
    for (std::uint64_t frame=1;frame<=3;++frame) {
        advance_domain(frame,.2,2);
        std::array<MAOniDomainReceipt,8> receipts{};std::uint32_t count=0;
        copy_domain_receipts(status,cells.data(),6,receipts.data(),8,count);
        if (status.state!=1) throw std::runtime_error(std::string("Equivalence owner faulted: ")+status.error);
        for (std::uint32_t i=0;i<count;++i) { auto value=receipts[i];value.session=0;trace.receipts.push_back(value); }
        if (count) ack_domain_receipts(status.session,receipts[count-1].sequence);
        trace.images.push_back(physical(checkpoint_domain(status.session,frame)));
    }
    trace.cells=cells;trace.status=status;
    copy_domain_thermal_status(trace.thermal);copy_domain_viscosity_status(trace.viscosity);copy_domain_gravity_status(trace.gravity);
    trace.thermal.session=trace.viscosity.session=trace.gravity.session=0;
    return trace;
}
void legacy_equivalence() {
    struct Case { std::uint32_t scenario,flags;double gravity,shear,bulk;std::uint32_t injected,consumed; };
    using P=std::uint32_t;
    constexpr P G=MA_ONI_DOMAIN_PHYSICS_GRAVITY,C=MA_ONI_DOMAIN_PHYSICS_CONDUCTION,V=MA_ONI_DOMAIN_PHYSICS_VISCOSITY,
        M=MA_ONI_DOMAIN_PHYSICS_MIXTURE,L=MA_ONI_DOMAIN_PHYSICS_LIQUID;
    const std::array<Case,6> cases{{{0,C,0,0,0,1,1},{1,C|L,0,0,0,3,3},{2,G|C,9.81,0,0,1,1},{4,M,0,0,0,3,1},{5,M|C,0,0,0,3,1},
        {6,M|C|V,0,.5,.25,3,1}}};
    for (const auto& item : cases) {
        std::vector<MAOniDomainInitialCell> cells;
        for (std::uint32_t i=0;i<6;++i) {
            if (item.scenario==1) cells.push_back(record({0,0,0,1100+at(i),0},300+at(i)));
            else cells.push_back(record({.9+.01*at(i),.25+.005*at(i),1e-28,item.scenario>=4 ? 200+5*at(i) : 0,.002},290+3*at(i)));
        }
        const MAOniDomainConfig legacy{40,2,1,native_width,ox,oy,3,2,item.scenario,0};
        start_domain(legacy,1,{},0,cells);const auto expected=run_sequence(false,item.injected,item.consumed);shutdown_domain();
        const Room value{configured(3,2),physics(item.flags,item.gravity,item.shear,item.bulk),std::vector<std::uint8_t>(6),cells};
        start(value);const auto actual=run_sequence(true,item.injected,item.consumed);
        require(actual.images==expected.images,"Configured solver checkpoints differ from the corresponding legacy scenario");
        require(std::memcmp(actual.cells.data(),expected.cells.data(),sizeof(MAOniDomainCell)*6)==0 &&
            actual.receipts.size()==expected.receipts.size() && actual.receipts.size()==2 &&
            std::memcmp(actual.receipts.data(),expected.receipts.data(),sizeof(MAOniDomainReceipt)*2)==0,
            "Configured publication or callback receipts differ from the legacy scenario");
        const auto& a=actual.status;const auto& e=expected.status;
        require(a.completed_frames==e.completed_frames && a.substeps==e.substeps && a.rejected_attempts==e.rejected_attempts &&
            a.time_seconds==e.time_seconds && a.maximum_relative_mass_error==e.maximum_relative_mass_error &&
            a.maximum_relative_energy_error==e.maximum_relative_energy_error && a.initial_oxygen_kg==e.initial_oxygen_kg &&
            a.current_oxygen_kg==e.current_oxygen_kg && a.pressure_pa==e.pressure_pa && a.liquid_mass_kg==e.liquid_mass_kg &&
            a.cells==e.cells,"Configured status differs from the legacy scenario");
        require(std::memcmp(&actual.thermal,&expected.thermal,sizeof(actual.thermal))==0 &&
            std::memcmp(&actual.viscosity,&expected.viscosity,sizeof(actual.viscosity))==0 &&
            std::memcmp(&actual.gravity,&expected.gravity,sizeof(actual.gravity))==0,"Configured diagnostics differ from the legacy scenario");
        std::vector<MAOniDomainCell> held(6);const auto source=capture(held);
        require(advance_domain_checkpoint(checkpoint_domain(source.session,3),0)==checkpoint_domain(source.session,3),
            "Configured wrapper did not round trip exactly");
        shutdown_domain();
    }
    std::cout<<"PASS byte-identical solver checkpoints, publications, receipts and diagnostics against legacy scenarios 0, 1, 2, 4, 5 and 6\n";
}

struct Accounts { std::array<MacEnergyAccount,5> mass;MacEnergyAccount energy; };
void add_state(Accounts& accounts,const std::vector<MAOniDomainCell>& cells,double potential,double sign) {
    for (const auto& cell : cells) {
        for (std::size_t k=0;k<5;++k) accounts.mass[k].add(sign*cell.masses[k]);
        accounts.energy.add(sign*cell.internal_energy);accounts.energy.add(sign*cell.resolved_kinetic_energy);
    }
    accounts.energy.add(sign*potential);
}
void suppression_contract(const Room& value) {
    for (std::uint32_t y=0;y<value.config.height;++y) for (std::uint32_t x=0;x<value.config.width;++x) {
        const bool solid=value.mask[value.cell(x,y)]!=0;const auto cell=native_cell(x,y);
        for (const auto hook : {MA_ONI_PRESSURE,MA_ONI_LIQUID,MA_ONI_TEMPERATURE,MA_ONI_GAS,MA_ONI_GAS_PAIR,
                MA_ONI_GAS_DISPLACEMENT,MA_ONI_LIQUID_DISPLACEMENT,MA_ONI_CHUNK_HEAT})
            require(suppress_domain_cell(hook,cell)==!solid,"Per-cell native ownership differs from the solid mask");
        for (const auto hook : {MA_ONI_POST_PROCESS,MA_ONI_STATE_TRANSITION,MA_ONI_LOAD_TRANSITION,MA_ONI_SUBLIMATION})
            require(suppress_domain_cell(hook,cell),"A phase-change entry was released inside the configured rectangle");
    }
    for (const auto hook : {MA_ONI_PRESSURE,MA_ONI_TEMPERATURE,MA_ONI_POST_PROCESS,MA_ONI_STATE_TRANSITION})
        require(!suppress_domain_cell(hook,native_cell(value.config.width,0)) && !suppress_domain_cell(hook,static_cast<int>(public_cell(0,0))),
            "Native exterior or a public index was claimed by the configured owner");
}
void interior_walls() {
    using P=std::uint32_t;
    constexpr P G=MA_ONI_DOMAIN_PHYSICS_GRAVITY,C=MA_ONI_DOMAIN_PHYSICS_CONDUCTION,V=MA_ONI_DOMAIN_PHYSICS_VISCOSITY,
        M=MA_ONI_DOMAIN_PHYSICS_MIXTURE,L=MA_ONI_DOMAIN_PHYSICS_LIQUID;
    const std::array<Room,3> rooms{{
        room(6,5,physics(G|C|V,9.81,.5,.25),[](auto x,auto y) { return x>=3 && y>=3; },
            [](auto x,auto y) { return record({.9+.02*at(x),.27-.01*at(y),1e-28,0,.001*at(x*y)},280+5*at(x)+3*at(y)); }),
        room(7,5,physics(M|G|C|V,9.81,.5,.25),[](auto x,auto y) { return (x==3 && (y==1 || y==2)) || (x==5 && y==3); },
            [](auto x,auto y) { return record({.8+.01*at(y),.25,1e-28,y==0 ? 200+10*at(x) : 0,0},290+2*at(x)+at(y)); }),
        room(5,3,physics(L|G|C,9.81),[](auto x,auto) { return x==2; },
            [](auto x,auto y) { return record({0,0,0,1050+10*at(x)+at(y),0},300+5*at(x)+2*at(y)); })}};
    const std::array<const char*,3> names{"L-shaped gas with gravity/conduction/viscosity","pillared wet mixture with gravity/conduction/viscosity",
        "two disconnected water pockets with gravity/conduction"};
    for (std::size_t index=0;index<rooms.size();++index) {
        const auto& value=rooms[index];const auto n=value.mask.size();
        start(value);std::vector<MAOniDomainCell> cells(n);auto status=capture(cells);
        MAOniDomainGravityStatus gravity{};copy_domain_gravity_status(gravity);
        Accounts accounts;add_state(accounts,cells,gravity.potential_j,1);double heat=0;
        suppression_contract(value);
        MAOniDomainConfig copied{};MAOniDomainPhysics copied_physics{};std::vector<std::uint8_t> copied_mask;
        copy_domain_configuration(status.session,copied,copied_physics,copied_mask);
        require(actor_checkpoint::same_geometry(copied,value.config) && configured_domain::same_physics(copied_physics,value.settings) &&
            copied_mask==value.mask,"Configuration copy differs from the admitted geometry, physics or mask");
        // Native item heat in an interior solid keeps its native exchange.
        std::uint32_t solid_x=0,solid_y=0,fluid_x=0,fluid_y=0;bool found=false;
        for (std::uint32_t y=0;y<value.config.height && !found;++y) for (std::uint32_t x=0;x+1<value.config.width && !found;++x)
            if (value.mask[value.cell(x,y)] && !value.mask[value.cell(x+1,y)]) { solid_x=x;solid_y=y;fluid_x=x+1;fluid_y=y;found=true; }
            else if (!value.mask[value.cell(x,y)] && value.mask[value.cell(x+1,y)]) { solid_x=x+1;solid_y=y;fluid_x=x;fluid_y=y;found=true; }
        require(found,"Test room has no fluid/solid face");
        require(!exchange_domain_chunk_heat(native_cell(solid_x,solid_y),350,1,1,1,1,.2F).handled,"Item heat in a configured solid was captured");
        // Solid-heat registration admits outside faces of owned fluid only.
        const MAOniDomainSolidHeatCell interior{48,1,public_cell(solid_x,solid_y),1,status.session,0,10,.8F,2,255};MAOniDomainSolidHeatStatus solid{};
        rejects([&] { configure_domain_solid_heat(&interior,1,solid); },"An interior solid was registered as a heat endpoint");
        for (std::uint32_t y=0;y<value.config.height;++y) {
            const auto right=value.config.width-1;
            MAOniDomainSolidHeatCell face{48,1,public_cell(value.config.width,y),1,status.session,0,10,.8F,2,255};
            if (value.mask[value.cell(right,y)]) rejects([&] { configure_domain_solid_heat(&face,1,solid); },"An outside face of a configured solid was registered");
            else configure_domain_solid_heat(&face,1,solid);
        }
        for (std::uint64_t frame=1;frame<=10;++frame) {
            if (frame==5) {
                MAOniDomainHeat edit{48,1,status.session,status.completed_frames,public_cell(fluid_x,fluid_y),4,750,1e6};
                MAOniDomainHeatStatus heat_status{};queue_domain_heat(&edit,1,heat_status);heat+=750;
            }
            advance_domain(frame,.2,2);status=capture(cells);
        }
        copy_domain_gravity_status(gravity);add_state(accounts,cells,gravity.potential_j,-1);accounts.energy.add(heat);
        for (const auto& mass : accounts.mass) require(mass.within_bound(),"Configured walls lost or created a constituent");
        const auto energy_residual=accounts.energy.residual().require_representable(),energy_bound=accounts.energy.bound().require_representable();
        require(accounts.energy.within_bound(),"Configured walls failed 128-epsilon U+K+G accounting");
        require(status.maximum_relative_mass_error<1e-8 && status.maximum_relative_energy_error<1e-6,"Configured owner exceeded its drift bound");
        MAOniDomainThermalStatus thermal{};copy_domain_thermal_status(thermal);
        MAOniDomainViscosityStatus viscosity{};copy_domain_viscosity_status(viscosity);
        require(thermal.enabled==1 && thermal.stages==2*status.substeps && thermal.contacts>0 && thermal.redistributed_energy_j>0,
            "Configured conduction was not applied around solids");
        if (value.settings.flags&MA_ONI_DOMAIN_PHYSICS_VISCOSITY)
            require(viscosity.enabled==1 && viscosity.stages==2*status.substeps && viscosity.modes>0 && viscosity.dissipated_kinetic_j>0,
                "Configured viscosity did not dissipate around solids");
        else require(viscosity.enabled==0 && viscosity.stages==0,"Disabled configured viscosity reported work");
        require(gravity.enabled==1 && gravity.gravity_m_per_s2==9.81 && gravity.body_impulse_y<0 && gravity.potential_j!=gravity.initial_potential_j,
            "Configured gravity did not act on the fluid cells");
        bool moved=false;
        for (std::size_t i=0;i<n;++i) {
            if (value.mask[i]) require(zero_cell(cells[i]),"A configured solid published fluid state");
            else {
                require(cells[i].temperature>0 && cells[i].pressure>0 && cells[i].masses[0]+cells[i].masses[1]+cells[i].masses[3]>0,
                    "A configured fluid cell lost its occupied reading");
                moved|=cells[i].resolved_kinetic_energy>0;
            }
        }
        require(moved,"Configured walls suppressed every motion");
        const auto first=static_cast<std::size_t>(std::find(value.mask.begin(),value.mask.end(),std::uint8_t{0})-value.mask.begin());
        require(status.cells==n && status.initial_oxygen_kg==value.cells[first].masses[1] && status.current_oxygen_kg==cells[first].masses[1],
            "Configured status sampled a solid cell");
        const auto substeps=status.substeps;const auto mass_drift=status.maximum_relative_mass_error,energy_drift=status.maximum_relative_energy_error;
        // Exact replay of the completed frame and checkpoint continuation.
        const auto image=snapshot_domain(status.session,status.completed_frames);const auto time=status.time_seconds;
        const auto expected=advance_domain_snapshot(image,.2);
        const auto source_frame=status.completed_frames;
        MAOniDomainHeat edit{48,1,status.session,source_frame,public_cell(fluid_x,fluid_y),4,-250,1e6};MAOniDomainHeatStatus heat_status{};
        queue_domain_heat(&edit,1,heat_status);advance_domain(source_frame+1,.2,2);status=capture(cells);
        require(advance_domain_snapshot_with_inputs(image,copy_domain_frame_inputs(status.session,source_frame+1))==snapshot_domain(status.session,source_frame+1),
            "Configured frame inputs did not replay exactly around solids");
        shutdown_domain();start_configured_domain(value.config,value.settings,value.mask,1,image,time);status=capture(cells);
        require(snapshot_domain(status.session,0)==image,"Configured restore changed its source image");
        advance_domain(1,.2,2);status=capture(cells);
        require(snapshot_domain(status.session,1)==expected,"Configured restore lost exact continuation around solids");
        std::cout<<"  "<<names[index]<<": substeps="<<substeps<<" thermal contacts="<<thermal.contacts
                 <<" viscous modes="<<viscosity.modes<<" mass drift="<<mass_drift<<" energy drift="<<energy_drift
                 <<" U+K+G residual="<<energy_residual<<" J (128-epsilon bound "<<energy_bound<<" J)\n";
        shutdown_domain();
    }
    std::cout<<"PASS interior solids: per-cell native ownership, all-zero solid copies, adiabatic interior faces, conservation, exact replay and continuation\n";
}

Room pillar_room() {
    return room(5,4,physics(MA_ONI_DOMAIN_PHYSICS_CONDUCTION),[](auto x,auto y) { return x==2 && y==1; },
        [](auto x,auto y) { return record({.9,.27+.005*at(x+y),1e-28,0,0},295+at(x)); });
}
void wall_targets() {
    const auto value=pillar_room();const auto n=value.mask.size();start(value);
    std::vector<MAOniDomainCell> cells(n);auto status=capture(cells);
    const auto wall=public_cell(2,1),fluid=public_cell(1,1);
    const auto held=snapshot_domain(status.session,0);
    MAOniDomainHeatStatus heat{};copy_domain_heat(heat);const auto heat_held=heat;
    const std::array<MAOniDomainHeat,2> heats{{{48,1,status.session,0,fluid,1,100,1e6},{48,1,status.session,0,wall,1,100,1e6}}};
    rejects([&] { queue_domain_heat(heats.data(),2,heat); },"Heat into a configured solid was admitted");
    require(std::memcmp(&heat,&heat_held,sizeof(heat))==0,"Rejected solid heat changed its status output");
    MAOniDomainTransferStatus transfer{};copy_domain_transfer_status(transfer);const auto transfer_held=transfer;
    std::array<MAOniDomainTransfer,2> transfers{{{64,2,status.session,0,fluid,1,1,1,7,0,.015625,300},{64,2,status.session,0,wall,1,1,1,8,0,.015625,300}}};
    rejects([&] { queue_domain_transfers(transfers.data(),2,transfer); },"Point injection into a configured solid was admitted");
    transfers[1].kind=2;rejects([&] { queue_domain_transfers(transfers.data(),2,transfer); },"Point removal from a configured solid was admitted");
    transfers[1].flags=1U|(2U<<8);rejects([&] { queue_domain_transfers(transfers.data(),2,transfer); },"A radius centred on a configured solid was admitted");
    require(std::memcmp(&transfer,&transfer_held,sizeof(transfer))==0,"Rejected solid transfer changed its status output");
    MAOniDomainActorStatus actor{};copy_domain_actor_status(actor);const auto actor_held=actor;
    const std::array<MAOniDomainActor,2> point_actors{{{80,1,status.session,0,11,1,fluid,1,0,0,.01F,0,0,0},{80,1,status.session,0,12,2,wall,1,0,0,.01F,.2,300,10}}};
    rejects([&] { configure_domain_actors(point_actors.data(),2,actor); },"An actor on a configured solid was admitted");
    const MAOniDomainPersistentActor durable{{80,1,status.session,0,13,1,wall,1,0,0,.01F,0,0,0},77,88,0};
    rejects([&] { configure_persistent_domain_actors(&durable,1,actor); },"A durable actor on a configured solid was admitted");
    require(std::memcmp(&actor,&actor_held,sizeof(actor))==0 && actor.live_actors==0,"Rejected solid actor changed registration or output");
    require(snapshot_domain(status.session,0)==held,"A rejected solid target changed the configured owner");
    // A rectangle footprint names an area. Its solids, including a solid
    // anchor, are skipped exactly as the native rectangular loop skips them.
    const MAOniDomainTransfer box{64,2,status.session,0,wall,1,2,1U|(2U<<8)|(2U<<16),9,0,1,0};
    queue_domain_transfers(&box,1,transfer);advance_domain(1,.2,2);
    std::array<MAOniDomainReceipt,4> receipts{};std::uint32_t count=0;copy_domain_receipts(status,cells.data(),static_cast<std::uint32_t>(n),receipts.data(),4,count);
    require(status.state==1 && count==1 && receipts[0].callback==9 && receipts[0].kind==2 && receipts[0].mass_kg>0 && receipts[0].public_cell==wall,
        "A rectangle anchored on a solid did not consume its fluid footprint");
    double footprint=0;
    for (const auto x : {2U,3U}) for (const auto y : {1U,2U}) if (!value.mask[value.cell(x,y)]) footprint+=value.cells[value.cell(x,y)].masses[1];
    require(receipts[0].mass_kg<=footprint && zero_cell(cells[value.cell(2,1)]),"Rectangular consumption used a solid or outside inventory");
    ack_domain_receipts(status.session,receipts[0].sequence);
    require(advance_domain_snapshot_with_inputs(held,copy_domain_frame_inputs(status.session,1))==snapshot_domain(status.session,1),
        "A solid-skipping rectangle did not replay exactly");
    shutdown_domain();
    std::cout<<"PASS solid targets: heat, point, radius and actor batches reject atomically; rectangle footprints skip solids and replay exactly\n";
}

void checkpoint_identity() {
    using P=std::uint32_t;
    constexpr P G=MA_ONI_DOMAIN_PHYSICS_GRAVITY,C=MA_ONI_DOMAIN_PHYSICS_CONDUCTION;
    const auto value=room(5,4,physics(G|C,9.81),[](auto x,auto y) { return x>=3 && y>=2; },
        [](auto x,auto y) { return record({.9+.01*at(y),.27,1e-28,0,.003},285+4*at(x)); });
    const auto n=value.mask.size();start(value);std::vector<MAOniDomainCell> cells(n);auto status=capture(cells);
    for (std::uint64_t frame=1;frame<=3;++frame) advance_domain(frame,.2,2);
    status=capture(cells);const auto image=checkpoint_domain(status.session,3);const auto time=status.time_seconds;
    const auto envelope=configured_domain::decode(image);
    require(actor_checkpoint::same_geometry(envelope.geometry,value.config) && configured_domain::same_physics(envelope.physics,value.settings) &&
        envelope.mask==value.mask && std::equal(envelope.physical.begin(),envelope.physical.begin()+8,"MACCLS01"),
        "Configured checkpoint omitted its geometry, physics or mask");
    require(copy_completed_domain_snapshot(status.session,3)==image && snapshot_domain(status.session,3)==image,
        "Configured completed snapshot differs from its drained checkpoint");
    const auto mask_count=static_cast<std::uint32_t>(n),bytes=static_cast<std::uint32_t>(image.size());
    validate_configured_domain_checkpoint(value.config,value.settings,value.mask,image,time);
    require(ma_oni_domain_configured_checkpoint_validate(&value.config,&value.settings,value.mask.data(),mask_count,image.data(),bytes,time)==MA_ONI_OK,
        "Exported configured validation rejected its exact source");
    auto refuse=[&](const MAOniDomainConfig& config,const MAOniDomainPhysics& settings,const std::vector<std::uint8_t>& mask,
        const std::vector<std::uint8_t>& candidate,double saved_time,const char* why) {
        rejects([&] { validate_configured_domain_checkpoint(config,settings,mask,candidate,saved_time); },why);
    };
    auto mask=value.mask;mask[value.cell(4,3)]=0;refuse(value.config,value.settings,mask,image,time,"A solid became fluid on restore");
    mask=value.mask;mask[value.cell(0,0)]=1;refuse(value.config,value.settings,mask,image,time,"A fluid cell became solid on restore");
    auto settings=value.settings;settings.flags&=~C;refuse(value.config,settings,value.mask,image,time,"Restore admitted conduction removal");
    settings=value.settings;settings.gravity_m_per_s2=9.8;refuse(value.config,settings,value.mask,image,time,"Restore admitted a different gravity");
    settings=value.settings;settings.flags|=MA_ONI_DOMAIN_PHYSICS_MIXTURE;refuse(value.config,settings,value.mask,image,time,"Restore admitted another phase policy");
    settings=value.settings;settings.flags|=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;settings.shear_viscosity_pa_s=.5;
    refuse(value.config,settings,value.mask,image,time,"Restore admitted added viscosity");
    auto config=value.config;config.offset_x++;refuse(config,value.settings,value.mask,image,time,"Restore admitted moved geometry");
    config=value.config;config.native_grid_width++;refuse(config,value.settings,value.mask,image,time,"Restore admitted another native pitch");
    refuse(value.config,value.settings,value.mask,image,time+1e-9,"Restore admitted another clock");
    auto damaged=image;damaged.back()^=1;refuse(value.config,value.settings,value.mask,damaged,time,"Restore admitted a damaged digest");
    // Self-consistent wrappers whose solver image disagrees still reject.
    auto relabelled=image;relabelled[configured_domain::header_bytes+value.cell(0,0)]=1;relabelled=reseal(relabelled);
    mask=value.mask;mask[value.cell(0,0)]=1;refuse(value.config,value.settings,mask,relabelled,time,"Wrapper mask was trusted over solver walls");
    auto policy=image;policy[56]&=static_cast<std::uint8_t>(~C);policy=reseal(policy);settings=value.settings;settings.flags&=~C;
    refuse(value.config,settings,value.mask,policy,time,"Wrapper physics were trusted over solver settings");
    rejects([&] { validate_domain_checkpoint({40,2,1,native_width,ox,oy,5,4,0,0},image,time); },"Legacy validation admitted a configured image");
    refuse(value.config,value.settings,value.mask,physical_part(image),time,"Configured validation admitted a bare solver image");
    require(ma_oni_domain_configured_checkpoint_validate(&value.config,&value.settings,mask.data(),mask_count,image.data(),bytes,time)==MA_ONI_INVALID_ARGUMENT,
        "Exported configured validation admitted a changed mask");
    require(advance_domain_checkpoint(image,0)==image,"Configured checkpoint did not reserialize exactly");
    const auto expected=advance_domain_checkpoint(image,.2);
    shutdown_domain();start_configured_domain(value.config,value.settings,value.mask,1,image,time);status=capture(cells);
    require(status.completed_frames==0 && status.time_seconds==time && checkpoint_domain(status.session,0)==image,"Configured restore changed its source");
    advance_domain(1,.2,2);status=capture(cells);
    require(checkpoint_domain(status.session,1)==expected,"Configured restore lost exact checkpoint continuation");
    // Durable actor images wrap the configured section.
    const std::array<MAOniDomainPersistentActor,2> inputs{{{{80,1,status.session,1,21,1,public_cell(0,0),1,0,0,.05F,0,0,0},301,41,0},
        {{80,1,status.session,1,22,2,public_cell(1,1),1,0,0,.03125F,.4,300,10000},301,41,0}}};
    MAOniDomainActorStatus actor{};configure_persistent_domain_actors(inputs.data(),2,actor);
    for (std::uint64_t frame=2;frame<=4;++frame) { advance_domain(frame,.2,2);(void)drain(cells); }
    status=capture(cells);const auto snapshot=snapshot_domain(status.session,4);const auto snapshot_time=status.time_seconds;
    const auto saved=actor_checkpoint::decode(snapshot);
    require(saved.actors.size()==2 && actor_checkpoint::same_geometry(saved.geometry,value.config) && configured_domain::has_magic(saved.physical),
        "Durable actor image omitted the configured section");
    require(advance_domain_snapshot(snapshot,0)==snapshot,"Configured actor image did not reserialize exactly");
    validate_configured_domain_checkpoint(value.config,value.settings,value.mask,snapshot,snapshot_time);
    mask=value.mask;mask[value.cell(4,3)]=0;refuse(value.config,value.settings,mask,snapshot,snapshot_time,"Actor image admitted another solid mask");
    settings=value.settings;settings.gravity_m_per_s2=9.8;refuse(value.config,settings,value.mask,snapshot,snapshot_time,"Actor image admitted other physics");
    auto moved=saved;moved.actors[0].cell=public_cell(4,3);
    refuse(value.config,value.settings,value.mask,actor_checkpoint::encode(moved),snapshot_time,"A durable actor was restored onto a solid");
    auto expected_snapshot=snapshot;
    shutdown_domain();start_configured_domain(value.config,value.settings,value.mask,1,snapshot,snapshot_time);status=capture(cells);
    require(pending_domain_actor_restores()==2,"Configured restore lost pending actors");
    auto rebound=inputs;for (auto& item : rebound) { item.actor.session=status.session;item.actor.expected_frame=0; }
    configure_persistent_domain_actors(rebound.data(),2,actor);
    for (std::uint64_t frame=1;frame<=2;++frame) {
        expected_snapshot=advance_domain_snapshot(expected_snapshot,.2);advance_domain(frame,.2,2);(void)drain(cells);
        require(snapshot_domain(status.session,frame)==expected_snapshot,"Configured actors diverged from exact saved continuation");
    }
    shutdown_domain();
    std::cout<<"PASS configured checkpoints: wrapped identity, 18 mismatch rejections, exact restore/continuation and durable actor images\n";
}

void largest_domain() {
    using P=std::uint32_t;
    constexpr P G=MA_ONI_DOMAIN_PHYSICS_GRAVITY,C=MA_ONI_DOMAIN_PHYSICS_CONDUCTION,V=MA_ONI_DOMAIN_PHYSICS_VISCOSITY;
    const auto side=static_cast<std::uint32_t>(std::sqrt(static_cast<double>(MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS)));
    require(side*side==MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS,"Largest square differs from the cell budget");
    const auto value=room(side,side,physics(G|C|V,9.81,.5,.25),[](auto x,auto y) { return x%4==2 && y%4==2; },
        [](auto x,auto y) { return record({.9+.001*at(x),.27-.0005*at(y),1e-28,0,0},285+.25*at(x)+.1*at(y)); });
    const auto n=value.mask.size();const auto initial_rss=peak_resident_kib();
    const auto elapsed=[](Clock::time_point start_time) { return std::chrono::duration<double,std::milli>(Clock::now()-start_time).count(); };
    auto t=Clock::now();start(value);const auto start_ms=elapsed(t);
    std::vector<MAOniDomainCell> cells(n);auto status=capture(cells);
    t=Clock::now();advance_domain(1,.2,2);const auto advance_ms=elapsed(t);
    t=Clock::now();status=capture(cells);const auto copy_ms=elapsed(t);const auto substeps=status.substeps;
    require(status.completed_frames==1 && status.maximum_relative_mass_error<1e-8 && status.maximum_relative_energy_error<1e-6,
        "Largest configured frame failed conservation");
    for (std::size_t i=0;i<n;++i) require(value.mask[i] ? zero_cell(cells[i]) : cells[i].temperature>0,"Largest copy misreported a solid");
    t=Clock::now();const auto image=checkpoint_domain(status.session,1);const auto checkpoint_ms=elapsed(t);const auto time=status.time_seconds;
    t=Clock::now();validate_configured_domain_checkpoint(value.config,value.settings,value.mask,image,time);const auto validate_ms=elapsed(t);
    shutdown_domain();
    t=Clock::now();start_configured_domain(value.config,value.settings,value.mask,1,image,time);const auto restore_ms=elapsed(t);
    status=capture(cells);require(checkpoint_domain(status.session,0)==image,"Largest configured restore changed its image");
    shutdown_domain();
    // The longest admitted side, within the same cell budget.
    const auto corridor=room(MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_SIDE,MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS/MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_SIDE,
        physics(C),[](auto x,auto) { return x==128; },[](auto x,auto) { return record({.9,.27,0,0,0},290+.01*at(x)); });
    start(corridor);std::vector<MAOniDomainCell> corridor_cells(corridor.mask.size());(void)capture(corridor_cells);shutdown_domain();
    std::cout<<std::setprecision(6)<<"Largest configured domain "<<side<<"x"<<side<<" ("<<n<<" cells, "
             <<std::count(value.mask.begin(),value.mask.end(),std::uint8_t{1})<<" solids, gravity/conduction/viscosity): start="<<start_ms
             <<" ms, one 0.2 s frame="<<advance_ms<<" ms ("<<substeps<<" substeps), copy="<<copy_ms
             <<" ms, checkpoint="<<checkpoint_ms<<" ms ("<<image.size()<<" bytes), validate="<<validate_ms<<" ms, restore="<<restore_ms
             <<" ms, peak RSS "<<initial_rss<<" -> "<<peak_resident_kib()<<" KiB\n";
    std::cout<<"PASS largest configured domain start, frame, copy, checkpoint, validation and restore; 256-wide corridor admitted\n";
}
}
int main() {
    try {
        std::cout<<std::setprecision(17);
        admission_rejection();
        below_range_gravity();
        legacy_equivalence();
        interior_walls();
        wall_targets();
        checkpoint_identity();
        largest_domain();
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
