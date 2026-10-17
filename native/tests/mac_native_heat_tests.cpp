#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_native_heat.hpp"
#include <iomanip>

namespace {
using namespace ma;using namespace ma::experimental;
void require(bool value,const char* why) {if (!value) throw std::runtime_error(why);}
template<class Action> void rejects(Action action) {
    bool rejected=false;try {action();} catch(const std::exception&) {rejected=true;}
    require(rejected,"Invalid native heat contact was accepted");
}
MacClosedWorld owner(double water=0,double pressure=1e5,double temperature=300) {
    const auto& setup=testing::setup();auto state=setup.state(2,1,false);
    state.cells[0].mixture=setup.mixture(1,pressure,temperature,water);
    state.momentum_x[state.grid.x_face(1,0)]=.125;
    return {setup.registry,setup.eos,std::move(state),{}};
}
void check(const MacClosedWorld& source,const MacNativeHeatExchange& result,MacNativeHeatBody body) {
    auto expected=source.state();expected.cells[0].mixture.energy=result.world.state().cells[0].mixture.energy;
    require(testing::fingerprint(expected)==testing::fingerprint(result.world.state()),"Native heat changed material, traces, passive metadata, another cell or canonical momentum");
    require(source.time_s()==result.world.time_s(),"Native heat changed the physical clock");
    const long double physical=static_cast<long double>(body.heat_capacity_kj_per_k)*1000*(static_cast<long double>(body.temperature_k)-result.body_temperature_k);
    const long double stored=static_cast<long double>(result.world.state().cells[0].mixture.energy)-source.state().cells[0].mixture.energy;
    require(std::abs(stored-physical)<=result.report.energy_bound_j || (stored==0 && physical==0),"Native body/fluid closed energy differs from captured endpoints");
    require(result.report.entropy_lower_bound_j_per_k>=0 && std::isfinite(result.report.entropy_lower_bound_j_per_k),"Native heat entropy lower bound is invalid");
    require(std::abs(result.report.body_heat_loss_j)<=std::abs(result.report.requested_heat_j),"Rounded native body heat exceeded its implicit root");
}
void physical_contacts() {
    int id=0;
    for (const double water:{0.,.35,1.}) for (const bool heating:{false,true}) for (const double g:{100.,1e300}) {
        const double pressure=water==1 ? 2.2e8 : water>0 ? 5e8 : 1e5;
        const auto source=owner(water,pressure);const auto held=source.checkpoint();
        const MacNativeHeatBody body{heating ? 310.F : 290.F,100.F};
        const auto result=exchange_mac_native_heat(source,0,body,g,.2);check(source,result,body);
        const auto& q=result.report;
        require(q.body_heat_loss_j!=0 && q.stored_fluid_heat_j!=0 && !q.unresolved &&
            q.entropy_lower_bound_j_per_k>0 && source.checkpoint()==held,"Native contact failed to advance immutably");
        if(water>0 && water<1) require(q.iterations>0,"Wet native contact did not evaluate its full nonlinear EOS");
        const auto before=source.thermodynamics().evaluate(source.state().cells[0].mixture,1);
        const auto after=source.thermodynamics().evaluate(result.world.state().cells[0].mixture,1);
        require(heating ? result.body_temperature_k<body.temperature_k && after.temperature_k>before.temperature_k && result.body_temperature_k>=after.temperature_k :
            result.body_temperature_k>body.temperature_k && after.temperature_k<before.temperature_k && result.body_temperature_k<=after.temperature_k,
            "Native contact violated its maximum principle");
        if (water==0 || water==1) {
            const auto capacity=before.equilibrium_heat_capacity_at_cell_volume_j_per_k;
            const long double exact=(static_cast<long double>(body.temperature_k)-before.temperature_k)/
                (1/(static_cast<long double>(body.heat_capacity_kj_per_k)*1000)+1/static_cast<long double>(capacity)+1/(static_cast<long double>(g)*.2L));
            require(std::abs(q.requested_heat_j-exact)<=128*std::numeric_limits<double>::epsilon()*std::abs(exact),
                "Native heat root differs from independent constant-capacity backward Euler");
        }
        std::cout<<"NHEAT "<<id++<<' '<<water<<' '<<pressure<<' '<<g<<" 0.2 "<<body.temperature_k<<' '<<body.heat_capacity_kj_per_k<<' '
            <<source.state().cells[0].mixture.energy<<' '<<before.temperature_k<<' '<<result.world.state().cells[0].mixture.energy<<' '
            <<after.temperature_k<<' '<<result.body_temperature_k<<' '<<q.requested_heat_j<<' '<<q.body_heat_loss_j<<' '<<q.stored_fluid_heat_j<<' '
            <<q.energy_residual_j<<' '<<q.energy_bound_j<<' '<<q.entropy_lower_bound_j_per_k<<'\n';
    }
    std::cout<<"PASS native gas/liquid/wet heat, both directions, stiff limits, independent linear roots, closed energy, entropy, traces and canonical momentum\n";
}
void unresolved_and_invalid() {
    const auto source=owner();const auto image=source.checkpoint();const MacNativeHeatBody body{310,100};
    for (const auto g:{1e-300,1e-20}) {
        const auto result=exchange_mac_native_heat(source,0,body,g,.2);check(source,result,body);
        require(result.report.unresolved && result.report.requested_heat_j>0 && result.report.body_heat_loss_j==0 &&
            result.world.checkpoint()==image && result.body_temperature_k==body.temperature_k,"Sub-binary32 heat was silently exchanged or given a minimum flux");
    }
    for (const auto& [capacity,g,seconds]:{std::tuple{0.F,100.,.2},std::tuple{100.F,0.,.2},std::tuple{100.F,100.,0.}}) {
        const auto result=exchange_mac_native_heat(source,0,{310,capacity},g,seconds);
        require(result.world.checkpoint()==image && result.body_temperature_k==310,"An insulated or zero-time contact changed state");
    }
    for (const auto g:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&] {(void)exchange_mac_native_heat(source,0,body,g,.2);});
    rejects([&] {(void)exchange_mac_native_heat(source,9,body,100,.2);});
    rejects([&] {(void)exchange_mac_native_heat(source,0,{0,100},100,.2);});
    rejects([&] {(void)exchange_mac_native_heat(source,0,{310,-1},100,.2);});
    rejects([&] {(void)exchange_mac_native_heat(source,0,body,100,1.1);});
    const auto& setup=testing::setup();auto definitions=setup.eos->reference().materials();for (auto& m:definitions) m.maximum_temperature_k=301;
    auto limited=std::make_shared<const FluidThermodynamics>(setup.registry,"native-heat-limited-v1",std::move(definitions));
    MacClosedWorld constrained(setup.registry,limited,source.state(),{});const auto held=constrained.checkpoint();
    rejects([&] {(void)exchange_mac_native_heat(constrained,0,{500,100},1e300,.2);});
    require(constrained.checkpoint()==held && source.checkpoint()==image,"Rejected native heat changed an owner");
    std::cout<<"PASS unresolved heat without a floor, insulation, zero time, EOS limit and immutable invalid-input rejection\n";
}
}
int main() {
    try {std::cout<<std::setprecision(17);physical_contacts();unresolved_and_invalid();return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
