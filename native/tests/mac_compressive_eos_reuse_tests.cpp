#include "mac_compressive_fixture.hpp"
#include "../src/mac_compressive_work_detail.hpp"
#include "../src/occupied_material_work_detail.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string record(const MixtureInventory& source,const ReferenceEquilibriumState& initial,double change,
    double spacing,double dt,MacEnergy linear,const MacCompressiveWork& result) {
    std::ostringstream text;auto* previous=std::cout.rdbuf(text.rdbuf());
    try{ma::testing::export_compressive_response(0,0,source,initial,1,change,spacing,dt,linear,result);}
    catch(...){std::cout.rdbuf(previous);throw;}
    std::cout.rdbuf(previous);return text.str();
}
void run(int material,std::size_t& trials) {
    const auto& set=ma::testing::setup();std::vector<MaterialCell> cells;
    for(std::size_t i=0;i<4;++i)cells.push_back({set.mixture(1,(material==2?300000:100000)*(1+.05*static_cast<double>(i)),
        300+5*static_cast<double>(i),material==2?1:material==1?.3:0),1,0});
    const auto held=cells;std::vector<OccupiedMaterialNode> nodes;
    for(std::size_t f=0;f<=4;++f){
        const auto l=f==0?no_material_cell:f-1,r=f==4?no_material_cell:f;
        const double inertia=(f==0?0:.5*cells[l].mixture.mass())+(f==4?0:.5*cells[r].mixture.mass());
        const bool fixed=f==0 || f==4;
        nodes.push_back({l,r,1,inertia,fixed?0:material==2?.001:f%2?3:-3,0,fixed});
    }
    const auto held_nodes=nodes;const double dt=material==2?1e-5:1e-4;
    const std::vector<MacEnergy> linear(4,MacEnergy::from_double(material==2?10:50));
    ReferenceRootOptions options;options.relative_pressure_tolerance=std::numeric_limits<double>::epsilon()/4;
    std::size_t callbacks=0;
    const ma::detail::OccupiedWorkLaw fresh_law=[&](std::size_t cell,double change,const ReferenceIsentrope& anchor,
        const ReferenceIsentropicResponse& isentropic,double derivative,std::optional<double> increment){
        const auto fresh=evaluate_mac_compressive_work(*set.eos,cells[cell].mixture,1,change,1,dt,linear[cell],options);
        const auto reused=compression_detail::finish(*set.eos,cells[cell].mixture,anchor,isentropic,derivative,1,dt,linear[cell]);
        check(anchor.reference_cell().masses_kg==set.eos->to_reference(cells[cell].mixture,1).masses_kg &&
            anchor.reference_cell().internal_energy_j==cells[cell].mixture.energy,"Pressure callback reused another source inventory");
        check(record(cells[cell].mixture,anchor.reference_state(),change,1,dt,linear[cell],fresh)==
            record(cells[cell].mixture,anchor.reference_state(),change,1,dt,linear[cell],reused),
            "Borrowed pressure response/tangent differs from a freshly anchored constitutive query");
        ++callbacks;
        return ma::detail::OccupiedWorkResponse{fresh.total_work_pressure_pa.project().value,
            fresh.work_pressure_volume_derivative_pa_per_m3.project().value,fresh.requested_energy_change_j.project().value,
            fresh.mixture.energy,fresh.unresolved_energy_update,compression_detail::pressure_increment(fresh.stress_pa,increment),
            fresh.energy_remainder_j,fresh.primary_energy};
    };
    const auto fresh=ma::detail::advance_occupied_material_pressure_with_work(*set.eos,cells,nodes,dt,PressurePreconditioner::automatic,
        LiquidFillRoundoffPolicy::strict_eos,16*std::numeric_limits<double>::epsilon(),options,fresh_law);
    const auto reused=advance_mac_compressive_pressure(*set.eos,cells,nodes,dt,1,linear,PressurePreconditioner::automatic,
        16*std::numeric_limits<double>::epsilon(),options);
    const auto& actual=reused.pressure;
    check(callbacks>2*cells.size() && fresh.nonlinear_iterations>0,"Constitutive reuse control missed repeated nonlinear queries");
    check(actual.volume_changes_m3==fresh.volume_changes_m3 && actual.work_pressures_pa==fresh.work_pressures_pa &&
        actual.endpoint_pressures_pa==fresh.endpoint_pressures_pa && actual.velocities_m_per_s==fresh.velocities_m_per_s &&
        actual.volume_residuals_m3==fresh.volume_residuals_m3 && actual.nonlinear_iterations==fresh.nonlinear_iterations &&
        actual.pressure_iterations==fresh.pressure_iterations && actual.energy_balance_error_j==fresh.energy_balance_error_j &&
        actual.pressure_work_roundoff_j==fresh.pressure_work_roundoff_j,"Pressure response reuse changed an accepted solution or its solver path");
    for(std::size_t i=0;i<cells.size();++i)check(cells[i].mixture.masses==held[i].mixture.masses && cells[i].mixture.energy==held[i].mixture.energy &&
        actual.cells[i].mixture.masses==fresh.cells[i].mixture.masses && actual.cells[i].mixture.energy==fresh.cells[i].mixture.energy &&
        actual.cells[i].volume_m3==fresh.cells[i].volume_m3,"Constitutive reuse changed its source or accepted inventory");
    for(std::size_t i=0;i<nodes.size();++i)check(nodes[i].velocity_m_per_s==held_nodes[i].velocity_m_per_s &&
        nodes[i].inertia_kg==held_nodes[i].inertia_kg,"Constitutive reuse mutated a pressure node");
    trials+=callbacks;
}
}
int main(){try{
    require_mac_energy_environment();std::cout<<std::setprecision(17);std::size_t trials=0;
    for(int material=0;material<3;++material)run(material,trials);
    std::cout<<"PASS "<<trials<<" borrowed constitutive queries equal freshly anchored gas, wet and liquid responses\n";
    std::cout<<"PASS accepted pressure roots, inventories, nodes, iteration paths and source immutability remain exact\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
