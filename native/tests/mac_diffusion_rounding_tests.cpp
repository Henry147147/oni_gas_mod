#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_diffusion.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <fstream>
#include <iomanip>
#include <numeric>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
struct Binary {
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> eos;
    GasTransportProfile profile{"quantized-binary-v1",{{"nitrogen","oxygen",std::log(10e4),0,0,1,1e6,1,1e9,true,"Equal-property analytical control"}}};
    Binary() {
        const auto standard=SpeciesRegistry::standard();std::vector<ReferenceMaterial> models;
        for(const char* id:{"nitrogen","oxygen"}) {
            auto species=standard[standard.index("nitrogen")];species.id=id;registry.add(species);
            ReferenceMaterial m;m.id=id;m.molar_mass_kg_per_mol=species.molar_mass;m.cv_j_per_kg_k=species.cv();
            m.energy_offset_j_per_kg=species.internal_energy(0);m.minimum_temperature_k=1;m.maximum_temperature_k=1e6;
            m.provenance="Equal-property analytical diffusion control";models.push_back(m);
        }
        eos=std::make_shared<const FluidThermodynamics>(registry,"quantized-binary-v1",models);
    }
    MacTransportState state(std::size_t w,std::size_t h,double trace) const {
        MacTransportState state;state.grid={w,h,1,1,false,false,std::vector<std::uint8_t>(w*h)};
        for(std::size_t i=0;i<w*h;++i)state.cells.push_back({eos->initialize_at_temperature(std::array<double,2>{1.,i==0?trace:0.},300,1).mixture,{}});
        state.momentum_x.resize((w+1)*h);state.momentum_y.resize(w*(h+1));return state;
    }
};
void quantized_binary() {
    const Binary f;const double dt=.018,min=std::numeric_limits<double>::denorm_min();
    for(int units:{2,4}) {
        const auto source=f.state(2,1,static_cast<double>(units)*min);const auto held=ma::testing::fingerprint(source);
        const auto result=advance_mac_diffusion(f.registry,*f.eos,source,dt,{},f.profile,8,MacDiffusionRounding::counted);
        if(result.report.substeps==0 || result.report.rejected_attempts!=0 || result.report.rounded_mass_transfers!=4*result.report.substeps || result.report.rounded_energy_transfers!=0)
            throw std::runtime_error("Binary projections: units="+std::to_string(units)+" steps="+std::to_string(result.report.substeps)+
                " rejected="+std::to_string(result.report.rejected_attempts)+" mass="+std::to_string(result.report.rounded_mass_transfers)+
                " energy="+std::to_string(result.report.rounded_energy_transfers));
        const auto expected_left=static_cast<double>(units==2?2:3)*min,expected_right=units==2?0.:min;
        check(result.state.cells[0].mixture.masses[1]==expected_left && result.state.cells[1].mixture.masses[1]==expected_right,
            "Quantized binary Heun endpoint differs from the analytical transfer");
        check(ma::testing::fingerprint(source)==held,"Rounded diffusion mutated its source");
        const auto p=f.eos->evaluate(source.cells[0].mixture,1).mechanical_pressure_pa;
        const auto d=evaluate_gas_diffusivity(f.profile.pairs()[0],300,p,f.registry[0].molar_mass,f.registry[1].molar_mass,1).effective_m2_per_s;
        std::cout<<"QROUND "<<units<<' '<<d<<' '<<dt<<' '<<result.report.substeps<<' '<<source.cells[0].mixture.masses[1]<<' '<<expected_left<<' '<<expected_right<<' '
            <<result.report.rounded_mass_transfers;
        for(std::size_t k=0;k<2;++k)std::cout<<' '<<result.report.species_rounded_transfers[k]<<' '
            <<result.report.species_transfer_roundoff[k].mantissa()<<' '<<result.report.species_transfer_roundoff[k].exponent();
        std::cout<<'\n';
        if(units==2) {
            bool rejected=false;try { (void)advance_mac_diffusion(f.registry,*f.eos,source,dt,{},f.profile,8); }
            catch(const std::runtime_error&) { rejected=true; }
            check(rejected && ma::testing::fingerprint(source)==held,"Strict reference ceased rejecting an unstorable positive arrival");
        }
    }
}
void normal_range_refinement() {
    const Binary f;auto source=f.state(32,512,0);
    // Four storage quanta of time make every nonzero face transfer smaller
    // than half a mass quantum, independent of low libm significand bits.
    constexpr double seconds=0x1p-1072;
    for(std::size_t i=0;i<source.cells.size();++i)source.cells[i].mixture=f.eos->initialize_at_temperature(
        std::array<double,2>{.75+.01*static_cast<double>(i%2),.25-.01*static_cast<double>(i%2)},300,1).mixture;
    const auto held=ma::testing::fingerprint(source);bool exhausted=false;
    try { (void)advance_mac_diffusion(f.registry,*f.eos,source,seconds,{},f.profile,1,MacDiffusionRounding::counted); }
    catch(const std::runtime_error&) { exhausted=true; }
    check(exhausted && ma::testing::fingerprint(source)==held,"Normal-range fallback reset its consumed budget or published state");
    const auto result=advance_mac_diffusion(f.registry,*f.eos,source,seconds,{},f.profile,2,MacDiffusionRounding::counted);
    check(result.report.substeps==1 && result.report.rejected_attempts==1 && result.report.rounded_mass_transfers>0 &&
        ma::testing::fingerprint(result.state)==held,"Threaded range refinement lost its audit, source or shared attempt");
    std::cout<<"QREFINE 16384 1 1 "<<result.report.rounded_mass_transfers<<'\n';
}
void ternary_constraint() {
    const Binary binary;auto registry=binary.registry;
    auto extra=registry[0];extra.id="helium";registry.add(extra);
    auto models=binary.eos->reference().materials();auto m=models.front();m.id=extra.id;models.push_back(m);
    const FluidThermodynamics eos(registry,"quantized-ternary-v1",models);
    auto pairs=binary.profile.pairs();
    for(const char* id:{"nitrogen","oxygen"})pairs.push_back({id,"helium",std::log(10e4),0,0,1,1e6,1,1e9,true,"Equal-property ternary control"});
    const GasTransportProfile profile("quantized-ternary-v1",pairs);
    auto source=binary.state(2,1,0);const double q=std::numeric_limits<double>::denorm_min();
    // U=0 is a valid caloric reference at positive temperature. It keeps
    // this subnormal test's energy exactly representable; initializing at
    // an arbitrary T would require a separate primary-U remainder.
    source.cells[0].mixture=MixtureInventory(3);source.cells[0].mixture.masses={4*q,0,0};
    source.cells[1].mixture=MixtureInventory(3);source.cells[1].mixture.masses={0,2*q,2*q};
    (void)eos.evaluate(source.cells[0].mixture,1);(void)eos.evaluate(source.cells[1].mixture,1);
    const auto held=ma::testing::fingerprint(source);
    const auto result=advance_mac_diffusion(registry,eos,source,.01,{},profile,4096,MacDiffusionRounding::counted);
    check(result.report.rejected_attempts>0 && result.report.rounded_mass_transfers>0 &&
        ma::testing::fingerprint(result.state)==held && ma::testing::fingerprint(source)==held,
        "Quantized ternary flux repaired a tiny inventory instead of retrying zero-net-mass admission");
    std::cout<<"QTERNARY "<<result.report.substeps<<' '<<result.report.rejected_attempts<<' '<<result.report.rounded_mass_transfers<<" 1\n";
}
MacTransportState front(std::size_t w,std::size_t h) {
    const auto& f=ma::testing::setup();auto state=f.state(w,h,false);const auto he=f.registry.index("helium");
    for(std::size_t i=0;i<state.cells.size();++i) {
        auto mass=state.cells[i].mixture.masses;mass[he]=i==(h==1?0:w/2+(h/2)*w)?1e-28:0;
        state.cells[i].mixture=f.eos->initialize_at_temperature(mass,300,1).mixture;
    }
    return state;
}
void trace_front(std::size_t w,std::size_t h,int frames,double multiplier,bool composed) {
    const auto& f=ma::testing::setup();const auto profile=GasTransportProfile::standard();auto state=front(w,h);
    MacClosedSettings settings;settings.numerical_kernel=MacNumericalKernel::production;
    settings.diffusion={multiplier==1?MacDiffusionMode::physical:MacDiffusionMode::accelerated,multiplier};
    auto owner=MacClosedWorld(f.registry,f.eos,state,settings);const auto source=owner.checkpoint();
    const auto helium=f.registry.index("helium");std::size_t rounded=0,rounded_energy=0;
    for(int frame=0;frame<frames;++frame) {
        if(composed) {
            const auto result=owner.advanced(.1);rounded+=result.report.diffusion.rounded_mass_transfers;
            rounded_energy+=result.report.diffusion.rounded_energy_transfers;
            check(result.report.interval_rounded_updates>=result.report.diffusion.rounded_mass_transfers+result.report.diffusion.rounded_energy_transfers,
                "Composed report lost rounded diffusion transfers");
            owner=result.world;state=owner.state();
            if(frame==frames/2) {
                const auto restored=MacClosedWorld::from_checkpoint(f.registry,f.eos,owner.checkpoint());
                check(restored.advanced(.1).world.checkpoint()==owner.advanced(.1).world.checkpoint(),"Rounded-front checkpoint continuation differs");
                owner=restored;
            }
        }else {
            const auto result=advance_mac_diffusion(f.registry,*f.eos,state,.1,settings.diffusion,*profile,4096,MacDiffusionRounding::counted);
            rounded+=result.report.rounded_mass_transfers;rounded_energy+=result.report.rounded_energy_transfers;state=result.state;
        }
    }
    // The historical 100x pure-diffusion control can reach the far wall
    // before its leading transfers leave range. Its composed half steps do
    // cross that range boundary; physical mode crosses it in both routes.
    if(composed || multiplier==1)check(rounded>0 && rounded_energy>0,"Spreading trace front did not exercise below-range mass/energy rounding");
    MacEnergyAccount mass;mass.add(-1e-28);std::size_t positive=0;
    const auto background=f.mixture(1,100000,300);const auto oxygen=f.registry.index("oxygen");
    const double reference_o2=100000*(background.masses[oxygen]/f.registry[oxygen].molar_mass)/background.gas_moles(f.registry);
    double maximum_t=0,maximum_p=0,maximum_o2=0,maximum_constituent=0;
    for(std::size_t i=0;i<state.cells.size();++i) {
        const auto& c=state.cells[i];mass.add(c.mixture.masses[helium]);positive+=c.mixture.masses[helium]>0?1U:0U;
        const auto reading=f.eos->evaluate(c.mixture,1);
        maximum_t=std::max(maximum_t,std::abs(reading.temperature_k-300));
        maximum_p=std::max(maximum_p,std::abs(reading.mechanical_pressure_pa-100000)/100000);
        const double o2=reading.gas_pressure_pa*(c.mixture.masses[oxygen]/f.registry[oxygen].molar_mass)/c.mixture.gas_moles(f.registry);
        maximum_o2=std::max(maximum_o2,std::abs(o2-reference_o2)/reference_o2);
        for(std::size_t k=0;k<f.registry.size();++k)if(k!=helium && background.masses[k]>0)
            maximum_constituent=std::max(maximum_constituent,std::abs(c.mixture.masses[k]-background.masses[k])/background.masses[k]);
        std::cout<<"QMASS "<<w<<' '<<h<<' '<<multiplier<<' '<<composed<<' '<<i<<' '<<c.mixture.masses[helium]<<'\n';
    }
    check(maximum_t<=.1 && maximum_p<=.01 && maximum_o2<=.01 && maximum_constituent<=.01,
        "Rounded trace front changed a bulk production metric beyond its original tolerance");
    check(mass.within_bound(),"Trace-front aggregate endpoint lost conserved helium");
    if(composed)check(MacClosedWorld::from_checkpoint(f.registry,f.eos,source).checkpoint()==source,"Rounded front changed its source image");
    std::cout<<"QFRONT "<<w<<' '<<h<<' '<<frames<<' '<<multiplier<<' '<<composed<<' '<<rounded<<' '<<rounded_energy<<' '<<positive<<' '
        <<mass.residual().mantissa()<<' '<<mass.residual().exponent()<<' '<<mass.bound().mantissa()<<' '<<mass.bound().exponent()<<' '
        <<maximum_t<<' '<<maximum_p<<' '<<maximum_o2<<' '<<maximum_constituent<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        require_mac_energy_environment();std::cout<<std::setprecision(17);
        if(argc==2 && std::string_view(argv[1])=="--large-fronts") {
            for(double multiplier:{1.,100.})for(bool composed:{false,true})trace_front(128,64,400,multiplier,composed);
        }else {
            check(argc==1,"Unknown diffusion rounding test argument");quantized_binary();normal_range_refinement();ternary_constraint();
            for(double multiplier:{1.,100.}) {trace_front(64,1,400,multiplier,false);trace_front(256,1,40,multiplier,true);}
        }
        std::cout<<"PASS counted shared diffusion transfers, strict oracle, audit and rollback\n";return 0;
    }catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
