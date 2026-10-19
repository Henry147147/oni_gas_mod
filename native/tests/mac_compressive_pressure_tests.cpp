#include "mac_compressive_fixture.hpp"
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
void near(double a,double b,const char* why) {MacEnergyAccount account;account.add(a);account.add(-b);check(account.within_bound(),why);}
int next_response=0;
std::vector<MaterialCell> cells(std::size_t n,double water=0) {
    const auto& set=ma::testing::setup();std::vector<MaterialCell> result;
    for(std::size_t i=0;i<n;++i)result.push_back({set.mixture(1,water==1 ? 300000 : 100000,300,water),1,0});
    return result;
}
std::vector<OccupiedMaterialNode> nodes(const std::vector<MaterialCell>& material,bool periodic,double velocity,double acceleration=0) {
    std::vector<OccupiedMaterialNode> result;const auto n=material.size();
    for(std::size_t f=0;f<(periodic ? n : n+1);++f) {
        const auto l=f>0 || periodic ? (f+n-1)%n : no_material_cell,r=f<n ? f : no_material_cell;
        const double inertia=.5*(l==no_material_cell ? 0 : material[l].mixture.mass())+.5*(r==no_material_cell ? 0 : material[r].mixture.mass());
        const bool fixed=!periodic && (f==0 || f==n);
        result.push_back({l,r,1,inertia,fixed ? 0 : velocity,fixed ? 0 : acceleration,fixed});
    }
    return result;
}
MacCompressivePressureStep scene(int id,std::vector<MaterialCell> source,std::vector<OccupiedMaterialNode> faces,
    bool periodic,double dt,double speed,bool stationary=false) {
    const auto& set=ma::testing::setup();const auto held=source;const auto held_nodes=faces;
    std::vector<MacEnergy> linear(source.size(),MacEnergy::from_double(speed));ReferenceRootOptions options;
    options.relative_pressure_tolerance=std::numeric_limits<double>::epsilon()/4;
    const auto step=advance_mac_compressive_pressure(*set.eos,source,faces,dt,1,linear,PressurePreconditioner::automatic,
        16*std::numeric_limits<double>::epsilon(),options);
    const auto& p=step.pressure;
    check(p.cells.size()==source.size() && step.responses.size()==source.size() && p.velocities_m_per_s.size()==faces.size(),"Incomplete coupled pressure output");
    MacEnergyAccount energy,momentum;MacEnergySum entropy,entropy_allowance;std::size_t active=0;
    for(std::size_t i=0;i<source.size();++i) {
        check(source[i].mixture.masses==held[i].mixture.masses && source[i].mixture.energy==held[i].mixture.energy &&
            source[i].volume_m3==held[i].volume_m3,"Coupled pressure mutated source material");
        check(p.cells[i].mixture.masses==source[i].mixture.masses,"Coupled pressure changed a constituent");
        energy.add(p.cells[i].mixture.energy);energy.add(-source[i].mixture.energy);
        entropy.add(step.responses[i].entropy_change_j_per_k);entropy_allowance.add(step.responses[i].entropy_allowance_j_per_k);
        if(!step.responses[i].dissipated_heat_j.is_zero())++active;
        if(stationary)check(p.volume_changes_m3[i]==0 && step.responses[i].dissipated_heat_j.is_zero() &&
            p.cells[i].mixture.energy==source[i].mixture.energy,"Uniform translation generated material work or shock heat");
    }
    std::cout<<"CPCASE "<<id<<' '<<source.size()<<' '<<faces.size()<<' '<<static_cast<int>(periodic)<<' '<<dt<<' '<<
        p.nonlinear_iterations<<' '<<p.pressure_iterations<<' '<<active<<' '<<p.wall_reaction_impulse_kg_m_per_s<<' '<<p.body_force_work_j<<'\n';
    for(std::size_t f=0;f<faces.size();++f) {
        const auto& node=faces[f];const double next=p.velocities_m_per_s[f];
        check(node.velocity_m_per_s==held_nodes[f].velocity_m_per_s && node.inertia_kg==held_nodes[f].inertia_kg,"Coupled pressure mutated source node");
        energy.add(MacEnergy::variance(node.inertia_kg,next));energy.add(MacEnergy::variance(node.inertia_kg,node.velocity_m_per_s).negated());
        energy.add(MacEnergy::from_double(dt).multiplied(node.inertia_kg).multiplied(node.acceleration_m_per_s2)
            .multiplied(.5*node.velocity_m_per_s+.5*next).negated());
        momentum.add(MacEnergy::from_double(node.inertia_kg).multiplied(next));momentum.add(MacEnergy::from_double(node.inertia_kg).multiplied(-node.velocity_m_per_s));
        momentum.add(MacEnergy::from_double(dt).multiplied(node.inertia_kg).multiplied(-node.acceleration_m_per_s2));
        const double pl=node.left==no_material_cell ? 0 : p.work_pressures_pa[node.left],pr=node.right==no_material_cell ? 0 : p.work_pressures_pa[node.right];
        if(node.fixed_zero_velocity)momentum.add(MacEnergy::from_double(dt).multiplied(node.area_m2*(pl-pr)+node.inertia_kg*node.acceleration_m_per_s2));
        if(stationary)check(next==node.velocity_m_per_s,"Uniform translation changed node velocity");
        std::cout<<"CPNODE "<<id<<' '<<f<<' '<<(node.left==no_material_cell ? -1 : static_cast<int>(node.left))<<' '<<
            (node.right==no_material_cell ? -1 : static_cast<int>(node.right))<<' '<<node.area_m2<<' '<<node.inertia_kg<<' '<<
            node.velocity_m_per_s<<' '<<node.acceleration_m_per_s2<<' '<<static_cast<int>(node.fixed_zero_velocity)<<' '<<next<<'\n';
    }
    check(energy.within_bound(),"Coupled actual U plus nodal K minus body work exceeds original operand bound");
    check(momentum.within_bound(),"Coupled actual nodal momentum and wall/body impulses exceed original operand bound");
    MacEnergySum lower;lower.add(entropy.value());lower.add(entropy_allowance.value());check(lower.sign()>=0,"Coupled actual total entropy decreased");
    if(!stationary)check(p.nonlinear_iterations>0 && active>0,"Coupled pressure fixture did not exercise its nonlinear stress");
    std::cout<<"CPFULL "<<id;
    for(const auto& value:{energy.residual(),energy.bound(),momentum.residual(),momentum.bound(),entropy.value(),entropy_allowance.value()})
        std::cout<<' '<<value.mantissa()<<' '<<value.exponent();
    std::cout<<'\n';
    for(std::size_t i=0;i<source.size();++i) {
        const int identifier=next_response++;
        std::cout<<"CPMAP "<<id<<' '<<i<<' '<<identifier<<' '<<p.volume_residuals_m3[i]<<'\n';
        ma::testing::export_compressive_response(identifier,0,source[i].mixture,set.eos->evaluate(source[i].mixture,source[i].volume_m3),
            source[i].volume_m3,p.volume_changes_m3[i],1,dt,linear[i],step.responses[i],source[i].energy_remainder_j);
    }
    return step;
}
void reflect() {
    auto material=cells(2);const auto a=scene(0,material,nodes(material,false,20),false,.0001,50);
    const auto b=scene(1,material,nodes(material,false,-20),false,.0001,50);
    for(std::size_t i=0;i<2;++i)near(a.pressure.cells[i].mixture.energy,b.pressure.cells[1-i].mixture.energy,"Reflected coupled material work differs");
    near(a.pressure.velocities_m_per_s[1],-b.pressure.velocities_m_per_s[1],"Reflected coupled node velocity differs");
}
void invalid() {
    const auto& set=ma::testing::setup();const auto source=cells(2);const auto faces=nodes(source,false,20);int rejected=0;
    const auto reject=[&](auto call) {bool bad=false;try {call();}catch(const std::exception&){bad=true;}check(bad,"Invalid coupled pressure request accepted");++rejected;};
    const std::vector<MacEnergy> linear(2,MacEnergy::from_double(50));
    reject([&]{static_cast<void>(advance_mac_compressive_pressure(*set.eos,source,faces,.0001,1,std::span<const MacEnergy>(linear).first(1)));});
    reject([&]{auto negative=linear;negative[1]=MacEnergy::from_double(-1);static_cast<void>(advance_mac_compressive_pressure(*set.eos,source,faces,.0001,1,negative));});
    reject([&]{static_cast<void>(advance_mac_compressive_pressure(*set.eos,source,faces,.0001,0,linear));});
    reject([&]{static_cast<void>(advance_mac_compressive_pressure(*set.eos,source,faces,0,1,linear));});
    auto definitions=set.eos->reference().materials();for(auto& d:definitions)d.maximum_temperature_k=300.0001;
    const FluidThermodynamics limited(set.registry,"coupled-compressive-late-rejection-v1",std::move(definitions));
    reject([&]{static_cast<void>(advance_mac_compressive_pressure(limited,source,faces,.01,1,linear));});
    check(rejected==5 && source[0].mixture.energy==source[1].mixture.energy && faces[1].velocity_m_per_s==20,"Coupled rejection changed input");
    std::cout<<"CWREJECTED "<<rejected<<'\n';
}
}
int main(){try {
    require_mac_energy_environment();std::cout<<std::setprecision(17);ma::testing::export_compressive_definitions(0,*ma::testing::setup().eos);
    reflect();std::cout<<"PASS coupled dry pressure reflection and complete energy\n";
    auto wet=cells(2,.2);wet[0].mixture=ma::testing::setup().mixture(1,200000,300,.2);
    scene(2,wet,nodes(wet,false,3),false,.0001,50);std::cout<<"PASS coupled wet pressure and constituent preservation\n";
    auto water=cells(2,1);scene(3,water,nodes(water,false,.001),false,.0001,10);std::cout<<"PASS coupled filled liquid stress and EOS\n";
    auto gas=cells(3);scene(4,gas,nodes(gas,false,0,-9.81),false,.0001,50);std::cout<<"PASS coupled gravity body work and wall reaction\n";
    auto periodic=cells(4);scene(5,periodic,nodes(periodic,true,3),true,.0001,50,true);std::cout<<"PASS uniform periodic translation has no false shock heating\n";
    invalid();std::cout<<"PASS coupled early and late rejection keeps source unchanged\n";
    check(next_response==15,"Coupled accepted response count differs");return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
