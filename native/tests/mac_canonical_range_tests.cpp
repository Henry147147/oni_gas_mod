#include "mac_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
MacTransportState gas(double mass, double volume = 1, bool periodic = true) {
    const auto& set = ma::testing::setup(); auto state = set.state(2, 2, periodic); state.grid.spacing_m = std::sqrt(volume);
    for (auto& c : state.cells) {
        std::vector<double> amounts(set.registry.size()); amounts[set.registry.index("nitrogen")] = mass;
        amounts[set.registry.index("helium")] = mass * 1e-28;
        c.mixture = set.eos->initialize_at_temperature(amounts, 300, volume).mixture;
        c.passive_quantities = {{17, mass}, {UINT64_C(0x80000000), mass * 1e-30}};
    }
    return state;
}
void closed_range(int id, double mass, double volume, MacScaled momentum) {
    const auto& set = ma::testing::setup(); auto source = gas(mass, volume);
    std::vector<MacScaled> full(source.momentum_x.size(), momentum); store_mac_field(source.momentum_x, source.full_momentum_x, full);
    const auto held = ma::testing::fingerprint(source); constexpr double dt = 1e-4;
    const MacClosedWorld owner(set.registry, set.eos, source); const auto step = advance_mac_closed_substep(set.registry, *set.eos, source, dt);
    const auto result = owner.advanced(dt, 4);
    check(ma::testing::fingerprint(source)==held && ma::testing::fingerprint(owner.state())==held &&
        ma::testing::fingerprint(step.state)==held && ma::testing::fingerprint(result.world.state())==held, "Full canonical closed flow changed uniform state/source/owner");
    check(result.report.substeps==1 && result.report.rejected_attempts==0 && owner.time_s()==0 && owner.revision()==1 && result.world.time_s()==dt && result.world.revision()==2,
        "Full canonical owner failed atomic clock/revision checks");
    const auto sweeps=mac_field_values(step.volume_fluxes_m3.x,step.volume_fluxes_m3.full_x);
    const auto fluxes=mac_field_values(step.mass_fluxes_kg.x,step.mass_fluxes_kg.full_x);
    check(!step.mass_fluxes_kg.full_x.empty() && std::all_of(sweeps.begin(),sweeps.end(),[](MacScaled v){return v.mantissa()>0;}) &&
        std::all_of(fluxes.begin(),fluxes.end(),[](MacScaled v){return v.mantissa()>0;}),"Full canonical velocity/sweep/bulk flux was lost");
    check(id==0 ? !step.volume_fluxes_m3.full_x.empty() && step.rounded_pressure_velocity_inputs>0 : step.volume_fluxes_m3.full_x.empty() && step.rounded_pressure_velocity_inputs==0,
        "Pressure source-velocity projection control differs");
    ma::testing::export_case(id,source,step,dt);
    std::cout<<"CANONICAL "<<id<<' '<<result.report.substeps<<' '<<result.report.rejected_attempts<<' '<<result.world.time_s()<<' '<<result.world.revision()<<' '<<step.rounded_pressure_velocity_inputs<<'\n';
}
void open_destination() {
    const auto& set=ma::testing::setup();auto source=gas(3.0517578125e-5,.000244140625);source.momentum_x[0]=2.5697272365554806e-293;
    const auto held=ma::testing::fingerprint(source);
    MacMassFluxes q{std::vector<double>(source.momentum_x.size(),.000244140625*4.2934097091770358e-297/3.0517578125e-5),std::vector<double>(source.momentum_y.size())};
    const auto plan=reconstruct_mac_parcels(set.registry,*set.eos,source,std::vector<double>(4,source.grid.volume_m3()),q);
    const auto result=advance_mac_parcel_transport(set.registry,source,*set.eos,plan);
    const auto j=mac_field_values(result.state.momentum_x,result.state.full_momentum_x);
    check(j[1].mantissa()>0 && j[1].project().rounded && result.state.momentum_x[1]==0 && ma::testing::fingerprint(source)==held,"Original open-face range failure remains");
    ma::testing::export_transport(2,source,result,plan);
}
void wall_reaction() {
    const auto& set=ma::testing::setup();auto source=gas(3.0517578125e-5,.000244140625,false);const auto& g=source.grid;
    for(std::size_t y=0;y<g.height;++y)source.momentum_x[g.x_face(1,y)]=2.5697272365554806e-293;
    MacMassFluxes q{std::vector<double>(source.momentum_x.size()),std::vector<double>(source.momentum_y.size())};
    for(std::size_t y=0;y<g.height;++y)q.x[g.x_face(1,y)]=.000244140625*4.2934097091770358e-297/3.0517578125e-5;
    const auto held=ma::testing::fingerprint(source);
    const auto plan=reconstruct_mac_parcels(set.registry,*set.eos,source,std::vector<double>(4,g.volume_m3()),q);
    const auto result=advance_mac_parcel_transport(set.registry,source,*set.eos,plan);
    const auto at=g.x_face(2,0);const auto reaction=result.report.x.full_wall_impulse[at];
    check(reaction.mantissa()<0 && reaction.project().rounded && result.report.x.wall_impulse[at]==0 &&
        result.report.x.energies[at].wall_heat.mantissa()>0 && ma::testing::fingerprint(source)==held,"Subrange wall impulse/kinetic removal was lost");
    ma::testing::export_transport(3,source,result,plan);
}
void invalid_full_fields() {
    const auto& set=ma::testing::setup();auto source=gas(1);std::vector<MacScaled> j(source.momentum_x.size(),MacScaled::from_parts(.75,-1941));
    store_mac_field(source.momentum_x,source.full_momentum_x,j);
    for(int control=0;control<2;++control){auto bad=source;if(control==0)bad.momentum_x[0]=1;else bad.full_momentum_x.pop_back();
        const auto held=ma::testing::fingerprint(bad);bool rejected=false;
        try{static_cast<void>(MacClosedWorld(set.registry,set.eos,bad));}catch(const std::invalid_argument&){rejected=true;}
        check(rejected && ma::testing::fingerprint(bad)==held,"Stale/invalid full canonical projection was accepted or mutated");}
}
}
int main(){try{
    require_mac_energy_environment();std::cout<<std::setprecision(17);const auto& set=ma::testing::setup();
    for(const auto& m:set.eos->reference().materials())std::cout<<"MATERIAL "<<set.registry.index(m.id)<<' '<<static_cast<int>(m.phase)<<' '<<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<' '<<m.energy_offset_j_per_kg<<' '<<m.zero_pressure_density_kg_per_m3<<' '<<m.reference_bulk_modulus_pa<<'\n';
    closed_range(0,3.0517578125e-5,.000244140625,MacScaled::from_parts(.72004705831217664,-1941));std::cout<<"PASS full canonical owner with subrange velocity volume and mass sweeps\n";
    closed_range(1,1e-200,1,MacScaled::from_parts(.75,-1200));std::cout<<"PASS subrange J with representable velocity and full bulk flux\n";
    open_destination();std::cout<<"PASS original new open-face J persists\n";
    wall_reaction();std::cout<<"PASS full wall reaction and positive wall heat\n";
    invalid_full_fields();std::cout<<"PASS stale full field projections and invalid shapes reject atomically\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
