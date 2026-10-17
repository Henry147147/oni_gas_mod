#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_thermal.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include <iomanip>

namespace {
using namespace ma;using namespace ma::experimental;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
MacTransportState pair(double pressure=5e8,double cold=280,double hot=420) {
    const auto& setup=testing::setup();auto state=setup.state(2,1,false);
    state.cells[0].mixture=setup.mixture(1,pressure,cold,.35);
    state.cells[1].mixture=setup.mixture(1,pressure,hot,.65);
    state.momentum_x[state.grid.x_face(1,0)]=.1;return state;
}
double conductivity(const MixtureInventory& mixture) {
    const auto& setup=testing::setup();const auto reading=setup.eos->evaluate(mixture,1);
    const auto values=setup.eos->to_reference(mixture,1);const auto& materials=setup.eos->reference().materials();
    double numerator=0,volume=0;
    for (std::size_t k=0;k<materials.size();++k) if (values.masses_kg[k]>0) {
        numerator+=reading.partial_volumes_m3[k]*setup.registry[setup.registry.index(materials[k].id)].conductivity;
        volume+=reading.partial_volumes_m3[k];
    }
    return numerator/volume;
}
void identities(const MacTransportState& before,const MacTransportState& after) {
    const auto same_field=[](const auto& a,const auto& b) {
        return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y) {
            return x.mantissa()==y.mantissa() && x.exponent()==y.exponent();
        });
    };
    require(before.momentum_x==after.momentum_x && before.momentum_y==after.momentum_y &&
        same_field(before.full_momentum_x,after.full_momentum_x) && same_field(before.full_momentum_y,after.full_momentum_y),
        "Mixed conduction changed canonical momentum");
    MacEnergyAccount balance;
    for (std::size_t i=0;i<before.cells.size();++i) {
        const auto& a=before.cells[i];const auto& b=after.cells[i];
        require(a.mixture.masses==b.mixture.masses && a.passive_quantities.size()==b.passive_quantities.size(),
            "Mixed conduction changed a constituent, trace or passive inventory");
        for (std::size_t k=0;k<a.passive_quantities.size();++k)
            require(a.passive_quantities[k].identity==b.passive_quantities[k].identity &&
                a.passive_quantities[k].quantity==b.passive_quantities[k].quantity,"Mixed conduction changed passive metadata");
        balance.add(a.mixture.energy);balance.add(-b.mixture.energy);
    }
    require(balance.within_bound(),"Mixed conduction lost closed internal energy");
}
void emit(int id,const MacTransportState& before,const MacThermalAdvance& after,double duration,double conductance) {
    const auto& setup=testing::setup();std::cout<<"THERMAL "<<id<<' '<<duration<<' '<<conductance<<' '<<after.report.entropy_change_j_per_k<<'\n';
    for (int endpoint=0;endpoint<2;++endpoint) {
        const auto& state=endpoint==0 ? before : after.state;
        for (std::size_t i=0;i<state.cells.size();++i) {
            const auto& cell=state.cells[i];const auto reading=setup.eos->evaluate(cell.mixture,1);
            std::cout<<"TCELL "<<id<<' '<<endpoint<<' '<<i<<' '<<cell.mixture.energy<<' '<<reading.temperature_k<<' '
                <<reading.mechanical_pressure_pa<<' '<<reading.gas_volume_m3<<' '<<reading.liquid_volume_m3<<' '
                <<reading.intrinsic_heat_capacity_j_per_k<<' '<<reading.equilibrium_heat_capacity_at_cell_volume_j_per_k<<'\n';
            for (std::size_t k=0;k<cell.mixture.masses.size();++k) if (cell.mixture.masses[k]!=0)
                std::cout<<"TMASS "<<id<<' '<<endpoint<<' '<<i<<' '<<setup.registry[k].id<<' '<<cell.mixture.masses[k]<<'\n';
        }
    }
}
void pair_controls() {
    const auto& setup=testing::setup();int id=0;
    for (const double pressure : {1e5,5e8}) for (const double duration : {1e7,1e13}) {
        const auto source=pair(pressure);const auto held=testing::fingerprint(source);
        const auto result=advance_mac_conduction(setup.registry,*setup.eos,source,duration);identities(source,result.state);
        const auto a=setup.eos->evaluate(source.cells[0].mixture,1),b=setup.eos->evaluate(source.cells[1].mixture,1);
        const auto c=setup.eos->evaluate(result.state.cells[0].mixture,1),d=setup.eos->evaluate(result.state.cells[1].mixture,1);
        const double g=thermal_conductance(conductivity(source.cells[0].mixture),conductivity(source.cells[1].mixture),.5,.5,1);
        const double q=result.state.cells[0].mixture.energy-source.cells[0].mixture.energy;
        const double flux=duration*g*(d.temperature_k-c.temperature_k);
        const double bound=128*std::numeric_limits<double>::epsilon()*(std::abs(source.cells[0].mixture.energy)+
            std::abs(result.state.cells[0].mixture.energy)+std::abs(q)+std::abs(flux))+
            128*std::numeric_limits<double>::epsilon()*duration*g*(c.temperature_k+d.temperature_k);
        require(q>0 && std::abs(q-flux)<=bound && c.temperature_k>=a.temperature_k && d.temperature_k<=b.temperature_k &&
            c.temperature_k<=d.temperature_k+128*std::numeric_limits<double>::epsilon()*b.temperature_k,
            "Mixed contact failed the independent backward-Euler flux or maximum principle");
        require(result.report.contacts==1 && result.report.iterations>0 && result.report.redistributed_energy.mantissa()>0 &&
            result.report.entropy_change_j_per_k>0 && testing::fingerprint(source)==held,"Mixed heat did not advance immutably");
        if (pressure==5e8 && duration==1e7) {
            const double frozen=duration*g*(b.temperature_k-a.temperature_k)/(1+duration*g*(
                1/a.equilibrium_heat_capacity_at_cell_volume_j_per_k+1/b.equilibrium_heat_capacity_at_cell_volume_j_per_k));
            require(a.equilibrium_heat_capacity_at_cell_volume_j_per_k>a.intrinsic_heat_capacity_j_per_k*1.001 &&
                std::abs(q-frozen)>1e-5*q,"Mixed conduction silently substituted frozen initial heat capacities");
        }
        emit(id++,source,result,duration,g);
    }
}
double reference_cold(const MacTransportState& source,double duration,int steps) {
    const auto& setup=testing::setup();
    const long double total=static_cast<long double>(source.cells[0].mixture.energy)+source.cells[1].mixture.energy;
    const auto rate=[&](double energy) {
        auto a=source.cells[0].mixture,b=source.cells[1].mixture;a.energy=energy;b.energy=static_cast<double>(total-energy);
        const double g=thermal_conductance(conductivity(a),conductivity(b),.5,.5,1);
        return g*(setup.eos->evaluate(b,1).temperature_k-setup.eos->evaluate(a,1).temperature_k);
    };
    const double dt=duration/steps;double energy=source.cells[0].mixture.energy;
    for (int n=0;n<steps;++n) {
        const auto k1=rate(energy),k2=rate(energy+.5*dt*k1),k3=rate(energy+.5*dt*k2),k4=rate(energy+dt*k3);
        energy+=dt/6*(k1+2*k2+2*k3+k4);
    }
    auto cell=source.cells[0].mixture;cell.energy=energy;return setup.eos->evaluate(cell,1).temperature_k;
}
void refinement() {
    const auto& setup=testing::setup();const auto source=pair();constexpr double duration=1e8;
    const double reference=reference_cold(source,duration,2048);
    require(std::abs(reference-reference_cold(source,duration,1024))<1e-7,"Independent explicit heat reference did not converge");
    double previous=0;
    for (const int count : {4,8,16}) {
        auto state=source;
        for (int n=0;n<count;++n) state=advance_mac_conduction(setup.registry,*setup.eos,state,duration/count).state;
        const double error=std::abs(setup.eos->evaluate(state.cells[0].mixture,1).temperature_k-reference);
        require(error>0 && (previous==0 || error<.65*previous),"Mixed conductive time refinement did not approach the independent ODE");
        previous=error;
    }
}
void network_and_rejection() {
    const auto& setup=testing::setup();auto source=setup.state(5,1,false,true);source.grid.walls[2]=1;
    source.cells[2]={MixtureInventory(setup.registry.size()),{}};
    for (std::size_t i=0;i<source.cells.size();++i) if (i!=2) source.cells[i].mixture=setup.mixture(1,1e5,280+20*static_cast<double>(i),.2+.1*static_cast<double>(i));
    const auto held=testing::fingerprint(source);const auto result=advance_mac_conduction(setup.registry,*setup.eos,source,1e7);identities(source,result.state);
    require(result.report.contacts==2 && result.report.entropy_change_j_per_k>0,"Mixed network did not retain its insulating split");
    for (const auto pair_cells : {std::array<std::size_t,2>{0,1},std::array<std::size_t,2>{3,4}}) {
        MacEnergyAccount balance;for (const auto cell : pair_cells) {balance.add(source.cells[cell].mixture.energy);balance.add(-result.state.cells[cell].mixture.energy);}
        require(balance.within_bound(),"Mixed heat crossed a disconnected insulating component");
    }
    bool rejected=false;try{(void)advance_mac_conduction(setup.registry,*setup.eos,source,-1);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected && testing::fingerprint(source)==held,"Invalid mixed heat mutated its source");
    auto invalid=source;invalid.cells[4].mixture.energy=-1e20;const auto invalid_held=testing::fingerprint(invalid);rejected=false;
    try{(void)advance_mac_conduction(setup.registry,*setup.eos,invalid,1e7);}catch(const std::exception&){rejected=true;}
    require(rejected && testing::fingerprint(invalid)==invalid_held,"Late invalid mixed EOS published earlier heat");
    auto uniform=pair(1e5,300,300);const auto uniform_held=testing::fingerprint(uniform);
    require(testing::fingerprint(advance_mac_conduction(setup.registry,*setup.eos,uniform,1e7).state)==uniform_held,
        "Isothermal mixed cells exchanged heat");
}
void model_ranges_and_extreme_interval() {
    const auto& setup=testing::setup();const auto compressed=pair(2.1e9);
    identities(compressed,advance_mac_conduction(setup.registry,*setup.eos,compressed,.1).state);
    auto materials=setup.eos->reference().materials();
    for (auto& material : materials) if (material.id=="water") material.maximum_temperature_k=400;
    const FluidThermodynamics bounded(setup.registry,"mixed-heat-temperature-range-control",materials);
    auto source=pair(1e5,390,390);source.cells[1].mixture=setup.mixture(1,1e9,600,0);
    const auto held=testing::fingerprint(source);
    identities(source,advance_mac_conduction(setup.registry,bounded,source,.1).state);
    bool rejected=false;try{(void)advance_mac_conduction(setup.registry,bounded,source,1e12);}catch(const std::exception&){rejected=true;}
    require(rejected && testing::fingerprint(source)==held,"Mixed heat escaped a constituent temperature range or published a failed exchange");
    const auto stiff=pair(1e5);const auto result=advance_mac_conduction(setup.registry,*setup.eos,stiff,1e300);
    identities(stiff,result.state);
    const auto a=setup.eos->evaluate(result.state.cells[0].mixture,1),b=setup.eos->evaluate(result.state.cells[1].mixture,1);
    require(std::abs(a.temperature_k-b.temperature_k)<1e-8 && result.report.entropy_change_j_per_k>0,
        "Finite stiff mixture heat accepted an overflowing flux residual or missed equilibrium");
}
void weak_heat_entropy() {
    const auto& setup=testing::setup();auto source=setup.state(2,1,false,true);
    source.cells[0].mixture=setup.mixture(1,1e5,300,.2);
    source.cells[1].mixture=setup.mixture(1,1e5,300.0016,.25);
    const auto result=advance_mac_conduction(setup.registry,*setup.eos,source,.005);identities(source,result.state);
    const double q=result.state.cells[0].mixture.energy-source.cells[0].mixture.energy;
    require(q>0 && q==source.cells[1].mixture.energy-result.state.cells[1].mixture.energy,"Weak entropy control needs an exactly closed represented exchange");
    const auto a=setup.eos->evaluate(source.cells[0].mixture,1),b=setup.eos->evaluate(source.cells[1].mixture,1);
    const auto c=setup.eos->evaluate(result.state.cells[0].mixture,1),d=setup.eos->evaluate(result.state.cells[1].mixture,1);
    const double lower=q*(d.temperature_k-c.temperature_k)/c.temperature_k/d.temperature_k;
    const double upper=q*(b.temperature_k-a.temperature_k)/a.temperature_k/b.temperature_k;
    const double bound=q*128*std::numeric_limits<double>::epsilon()*(1/a.temperature_k+1/b.temperature_k);
    require(result.report.entropy_change_j_per_k>=lower-bound && result.report.entropy_change_j_per_k<=upper+bound,
        "Weak mixed heat entropy lost its physical signal by subtracting rounded EOS temperatures");
    const auto g=thermal_conductance(conductivity(source.cells[0].mixture),conductivity(source.cells[1].mixture),.5,.5,1);
    emit(4,source,result,.005,g);
}
}
int main() {
    try {
        std::cout<<std::setprecision(17);const auto& setup=ma::testing::setup();
        for (const std::string id : {"nitrogen","oxygen","helium","water"}) {
            const auto& material=setup.registry[setup.registry.index(id)];
            std::cout<<"TMATERIAL "<<id<<' '<<material.cv()<<' '<<material.internal_energy(0)<<' '
                <<material.molar_mass<<' '<<material.conductivity<<'\n';
        }
        pair_controls();refinement();network_and_rejection();model_ranges_and_extreme_interval();weak_heat_entropy();
        MacClosedSettings settings;settings.thermal_conduction=true;
        const MacClosedWorld world(setup.registry,setup.eos,pair(1e5,280,320),settings);
        const auto image=world.checkpoint();const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
        require(world.advanced(.02).world.checkpoint()==restored.advanced(.02).world.checkpoint(),"Mixed heat checkpoint continuation differs");
        std::cout<<"PASS nonlinear gas/liquid conduction, stiff maximum principle, independent time refinement, immutable rejection, insulation, traces and complete continuation\n";return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
