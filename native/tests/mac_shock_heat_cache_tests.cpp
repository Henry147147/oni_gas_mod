#include "mac_shock_heat_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string receipt(const MixtureInventory& left,const MixtureInventory& right,const MacShockHeatPair& pair) {
    std::ostringstream result;auto* previous=std::cout.rdbuf(result.rdbuf());
    try{ma::testing::export_shock_heat_pair(0,left,right,1,1,pair);}
    catch(...){std::cout.rdbuf(previous);throw;}
    std::cout.rdbuf(previous);return result.str();
}
MacTransportState source(bool periodic,bool wet,bool tiny) {
    const auto& s=ma::testing::setup();auto result=s.state(4,3,periodic,wet,!periodic);
    for(std::size_t i=0;i<result.cells.size();++i)if(!result.grid.walls[i])
        result.cells[i].mixture=s.eos->initialize_at_temperature(result.cells[i].mixture.masses,
            250+43*static_cast<double>((7*i)%13),1).mixture;
    const auto inertia=mac_face_inertias(s.registry,result,*s.eos);
    for(int axis=0;axis<2;++axis) {
        auto& j=axis==0?result.momentum_x:result.momentum_y;const auto& m=axis==0?inertia.x:inertia.y;
        std::vector<MacScaled> values(j.size());
        for(std::size_t f=0;f<j.size();++f)if(!ma::testing::compression_fixed_face(result.grid,axis,f)) {
            const auto coordinate=axis==0?f%result.grid.x_width():f/result.grid.width;
            values[f]=MacScaled::from_double(m[f]).multiplied(12-6*static_cast<double>(coordinate)).shifted(tiny?-1200:0);
        }
        store_mac_field(j,axis==0?result.full_momentum_x:result.full_momentum_y,values);
    }
    return result;
}
void replay(MacTransportState input,MacShockHeatLaw law,bool tiny,bool wet,std::size_t& events,std::size_t& stale_witnesses) {
    const auto& s=ma::testing::setup();
    if(wet) {
        // Shock heat joins gas cells only (MAC_HEAT.md, "Phase interfaces").
        const auto held=ma::testing::fingerprint(input);
        const auto actual=advance_mac_shock_heat(s.registry,*s.eos,input,.001,law);
        check(actual.report.events.empty() && ma::testing::fingerprint(actual.state)==held,"Shock heat joined gas/liquid cells");
        return;
    }
    for(int stage=0;stage<3;++stage) {
        // A caller edit between stages must never reuse a prior call's EOS.
        if(stage==1){input.cells[0].mixture=s.eos->initialize_at_temperature(input.cells[0].mixture.masses,875,1).mixture;input.cells[0].energy_remainder_j={};}
        const auto held=ma::testing::fingerprint(input);auto expected=input;
        const auto actual=advance_mac_shock_heat(s.registry,*s.eos,input,.001,law);
        check(!actual.report.events.empty(),"Cache replay did not exercise any face transfer");
        std::size_t positive=0,changed=0;
        for(const auto& event:actual.report.events) {
            auto& l=expected.cells[event.left].mixture;auto& r=expected.cells[event.right].mixture;
            const auto fresh=relax_mac_shock_heat_pair(*s.eos,l,r,1,1,event.pair.integrated_conductance,
                expected.cells[event.left].energy_remainder_j,expected.cells[event.right].energy_remainder_j);
            check(receipt(l,r,fresh)==receipt(l,r,event.pair),"Cached heat receipt differs from freshly evaluated actual endpoint inventories");
            if(l.energy!=input.cells[event.left].mixture.energy || r.energy!=input.cells[event.right].mixture.energy)++stale_witnesses;
            if(!fresh.transfer.is_zero())++positive;
            if(fresh.after_left_j!=l.energy || fresh.after_right_j!=r.energy)++changed;
            l.energy=fresh.after_left_j;r.energy=fresh.after_right_j;
            expected.cells[event.left].energy_remainder_j=fresh.after_left_remainder_j;
            expected.cells[event.right].energy_remainder_j=fresh.after_right_remainder_j;++events;
        }
        // A below-range closing velocity leaves every law's heat unresolved:
        // the sensed laws' acoustic term ramps in with the compression
        // (MAC_HEAT.md, "Compression ramp").
        const bool unresolved=tiny;
        check(positive>0 && (unresolved?changed==0:changed>0),"Cache replay missed resolved or below-range heat updates");
        check(ma::testing::fingerprint(expected)==ma::testing::fingerprint(actual.state) && ma::testing::fingerprint(input)==held,
            "Cached heat stage differs from uncached ordered transfer replay or mutated its source");
        std::size_t fluid=0;for(const auto wall:input.grid.walls)if(!wall)++fluid;
        check(actual.report.reused_pair_eos_readings==2*actual.report.events.size() && actual.report.reused_endpoint_eos_readings==fluid,
            "Cache reuse counters differ from actual pair and endpoint coverage");
        input=actual.state;
    }
}
}
int main(){try{
    require_mac_energy_environment();std::cout<<std::setprecision(17);std::size_t events=0,witnesses=0;
    for(const bool periodic:{false,true})for(const bool wet:{false,true})for(const bool tiny:{false,true})
        for(const auto law:{MacShockHeatLaw::closing_velocity,MacShockHeatLaw::pressure_sensed_viscosity,MacShockHeatLaw::pressure_sensed_series})
            replay(source(periodic,wet,tiny),law,tiny,wet,events,witnesses);
    check(events>0 && witnesses>0,"Cache controls missed changed endpoint reuse");
    std::cout<<"PASS 36 dry heat stages with fresh-EOS receipt replay, walls, periodicity, three laws, traces and below-range transfers; 12 inactive wet mixtures\n";
    std::cout<<"PASS "<<events<<" exact transfer receipts and "<<witnesses<<" changed-inventory reuse witnesses, including edits between stages\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
