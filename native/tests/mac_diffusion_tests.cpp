#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_diffusion.hpp"
#include <chrono>
#include <iomanip>
#include <numbers>

namespace {
using namespace ma;using namespace ma::experimental;
void check(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
struct Setup {
    SpeciesRegistry registry;
    std::shared_ptr<const FluidThermodynamics> eos;
    explicit Setup(bool equal=false) {
        const auto standard=SpeciesRegistry::standard();std::vector<ReferenceMaterial> materials;
        for(const char* id:{"nitrogen","oxygen","helium","carbon_dioxide"}) {
            auto s=standard[standard.index(id)];if(equal) { s.atoms={{"N",2}};s.molar_mass=molecular_mass(s.atoms);s.cp=1000;s.formation_enthalpy=0; }
            registry.add(s);ReferenceMaterial m;m.id=id;m.phase=ReferencePhase::ideal_gas;
            m.molar_mass_kg_per_mol=s.molar_mass;m.cv_j_per_kg_k=s.cv();m.energy_offset_j_per_kg=s.internal_energy(0);
            m.minimum_temperature_k=1;m.maximum_temperature_k=1e6;m.provenance="diffusion test ideal gas";materials.push_back(m);
        }
        eos=std::make_shared<const FluidThermodynamics>(registry,"diffusion-tests-1",std::move(materials));
    }
    MacTransportState state(std::size_t w,std::size_t h,double dx=1) const {
        MacTransportState s;s.grid={w,h,dx,1,false,false,std::vector<std::uint8_t>(w*h)};
        for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x) {
            const double oxygen=.2+.1*static_cast<double>(x)/static_cast<double>(std::max(std::size_t(1),w-1));
            std::vector<double> mass(registry.size());mass[0]=(1-oxygen)*dx*dx;mass[1]=oxygen*dx*dx;
            s.cells.push_back({eos->initialize_at_temperature(mass,300,dx*dx).mixture,{}});
        }
        s.momentum_x.resize(s.grid.x_width()*h);s.momentum_y.resize(s.grid.y_height()*w);return s;
    }
};
GasTransportProfile constant(double coefficient) {
    return {"analytic-constant-D",{{"nitrogen","oxygen",std::log(coefficient*1e4),0,0,1,1e6,1,1e9,true,"analytical control"}}};
}
double span(const MacTransportState& s,std::size_t species=1) {
    double low=1,high=0;for(std::size_t i=0;i<s.cells.size();++i)if(!s.grid.walls[i]) {
        const auto& m=s.cells[i].mixture;const double v=m.masses[species]/m.mass();low=std::min(low,v);high=std::max(high,v);
    }return high-low;
}
void analytic() {
    Setup setup(true);const auto profile=constant(.01);const MacDiffusionSettings mode;
    double worst=0,worst_refined=0;
    for(const auto [w,h]:{std::pair<std::size_t,std::size_t>{2,1},{3,2},{32,2}}) {
        auto source=setup.state(w,h);const double amplitude=.01;
        for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x) {
            const double oxygen=.25+amplitude*std::cos(std::numbers::pi*(static_cast<double>(x)+.5)/static_cast<double>(w));
            const std::vector<double> masses{1-oxygen,oxygen,0,0};
            source.cells[x+y*w].mixture=setup.eos->initialize_at_temperature(masses,300,1).mixture;
        }
        const double pressure=setup.eos->evaluate(source.cells[0].mixture,1).mechanical_pressure_pa;
        const double coefficient=profile.diffusivity("nitrogen","oxygen",300,pressure);
        const double eigenvalue=-4*coefficient*std::pow(std::sin(std::numbers::pi/(2*static_cast<double>(w))),2);
        const auto next=advance_mac_diffusion(setup.registry,*setup.eos,source,2,mode,profile);
        auto refined=source;
        for(int k=0;k<16;++k)refined=advance_mac_diffusion(setup.registry,*setup.eos,refined,.125,mode,profile).state;
        double error=0;
        for(std::size_t x=0;x<w;++x) {
            const double expected=.25+amplitude*std::cos(std::numbers::pi*(static_cast<double>(x)+.5)/static_cast<double>(w))*std::exp(eigenvalue*2);
            error=std::max(error,std::abs(next.state.cells[x].mixture.masses[1]-expected)/amplitude);
            worst_refined=std::max(worst_refined,std::abs(next.state.cells[x].mixture.masses[1]-refined.cells[x].mixture.masses[1])/amplitude);
        }
        worst=std::max(worst,error);
        check(error<.01,"Analytic binary diffusion error exceeds 1%");
        check(span(next.state)<span(source),"Concentration gradient did not relax");
    }
    check(worst_refined<.01,"Binary numerical error against refined reference exceeds 1%");
    std::cout<<"PASS binary 2/6/64-cell analytical error="<<worst<<", refined numerical error="<<worst_refined<<" (relative to initial perturbation)\n";
}
void controls() {
    Setup setup;const auto profile=GasTransportProfile::standard();auto source=setup.state(3,2);
    source.cells[0].mixture.masses[2]=1e-28;
    source.cells[1].mixture.masses[3]=1e-6;
    const auto held=ma::testing::fingerprint(source);
    const auto next=advance_mac_diffusion(setup.registry,*setup.eos,source,1,{},*profile);
    check(ma::testing::fingerprint(source)==held,"Diffusion mutated its input");
    check(next.state.cells[1].mixture.masses[2]>0 && next.state.cells[0].mixture.masses[3]>0,"Absent species or trace did not cross a face");
    check(next.report.substeps>0 && next.report.exchanged_mass_kg>0 && next.report.species_mass_residual.size()==4,"Diffusion accounting is missing");
    const auto accelerated=advance_mac_diffusion(setup.registry,*setup.eos,source,.01,{MacDiffusionMode::accelerated,100},*profile);
    for(std::size_t i=0;i<source.cells.size();++i)for(std::size_t s=0;s<setup.registry.size();++s)
        check(std::abs(next.state.cells[i].mixture.masses[s]-accelerated.state.cells[i].mixture.masses[s])<=1e-12*std::max(next.state.cells[i].mixture.masses[s],accelerated.state.cells[i].mixture.masses[s]),"Accelerated diffusion does not preserve physical time scaling");
    source.grid.walls[1]=source.grid.walls[4]=1;
    const auto blocked=advance_mac_diffusion(setup.registry,*setup.eos,source,1,{},*profile);
    check(blocked.state.cells[2].mixture.masses[2]==0,"Trace crossed a solid wall");
    auto unequal=setup.state(2,1);unequal.cells[0].mixture=setup.eos->initialize_at_temperature(unequal.cells[0].mixture.masses,310,1).mixture;
    check(advance_mac_diffusion(setup.registry,*setup.eos,unequal,.1,{},*profile).report.exchanged_mass_kg>0,"Unequal-temperature diffusion failed");
    auto pure=setup.state(2,1);const std::array<double,4> pure_mass{1,0,0,0};for(auto& c:pure.cells)c.mixture=setup.eos->initialize_at_temperature(pure_mass,300,1).mixture;
    check(ma::testing::fingerprint(advance_mac_diffusion(setup.registry,*setup.eos,pure,.1,{},*profile).state)==ma::testing::fingerprint(pure),"Pure gas diffused into itself");
    MacClosedSettings settings;settings.diffusion={};
    MacClosedWorld owner(setup.registry,setup.eos,setup.state(3,2),settings);
    const auto image=owner.checkpoint();const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
    check(restored.checkpoint()==image,"Diffusion checkpoint failed roundtrip");
    check(restored.advanced(.02).world.checkpoint()==owner.advanced(.02).world.checkpoint(),"Diffusion continuation is not deterministic");
    const auto changed=owner.advanced(.02,4096,MacExecutionPolicy::exact_equilibrium);
    check(changed.report.diffusion.substeps>0,"Equilibrium bypassed diffusion");
    bool rejected=false;try { (void)owner.advanced(.2,1); }catch(const std::runtime_error&) { rejected=true; }
    check(rejected && owner.checkpoint()==image,"Late budget failure changed the source owner");
    const auto old=owner.with_diffusion(MacDiffusionSettings::disabled()).checkpoint();
    check(MacClosedWorld::from_checkpoint(setup.registry,setup.eos,old).settings().diffusion.mode==MacDiffusionMode::disabled,"Legacy checkpoint silently enabled diffusion");
    rejected=false;try { (void)advance_mac_diffusion(setup.registry,*setup.eos,source,.1,{MacDiffusionMode::physical,100},*profile); }catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Physical mode admitted an accelerated multiplier");
    // An unrelated transport object cannot silently reinterpret saved physics.
    auto altered_pairs=profile->pairs();altered_pairs[0].c+=.01;
    auto altered=std::make_shared<const GasTransportProfile>(profile->identity(),altered_pairs);
    rejected=false;try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image,altered); }catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Checkpoint admitted changed transport coefficients under an unchanged ID");
    auto moving=setup.state(3,2);const auto inertia=mac_face_inertias(setup.registry,moving,*setup.eos);
    moving.momentum_x[moving.grid.x_face(1,0)]=.01*inertia.x[moving.grid.x_face(1,0)];
    const auto mixed=MacClosedWorld(setup.registry,setup.eos,moving,settings).advanced(.02);
    check(mixed.report.diffusion.exchanged_mass_kg>0 && mixed.world.state().momentum_x!=moving.momentum_x,"Diffusion did not compose with bulk momentum transport");
    const double d=profile->diffusivity("nitrogen","oxygen",300,101325);
    check(d>1.5e-5 && d<2.5e-5,"N2/O2 coefficient unit conversion failed");
    check(std::abs(profile->diffusivity("nitrogen","oxygen",300,101325./2)-2*d)<1e-14*d,"Gas diffusivity did not scale with pressure");
    std::cout<<"PASS binary analytics, conservation, enthalpy, trace/absent gases, walls, unequal T, modes, checkpoint, rollback\n";
}
void general_range_controls() {
    SpeciesRegistry registry;std::vector<ReferenceMaterial> materials;std::vector<GasDiffusionPair> pairs;
    for(std::size_t k=0;k<6;++k) {
        SpeciesDefinition s;s.id="gas-"+std::to_string(k);s.name=s.id;s.atoms={{"N",2}};s.cp=1000;registry.add(s);
        const auto& actual=registry[k];ReferenceMaterial m;m.id=s.id;m.phase=ReferencePhase::ideal_gas;
        m.molar_mass_kg_per_mol=actual.molar_mass;m.cv_j_per_kg_k=actual.cv();m.energy_offset_j_per_kg=actual.internal_energy(0);
        m.minimum_temperature_k=1;m.maximum_temperature_k=1e6;m.provenance="six constituent range control";materials.push_back(m);
        for(std::size_t j=0;j<k;++j)pairs.push_back({registry[j].id,s.id,std::log(.001*1e4),0,0,1,1e6,1,1e9,true,"constant test coefficient"});
    }
    const FluidThermodynamics eos(registry,"six-gas-range",materials);const GasTransportProfile profile("six-gas",pairs);
    MacTransportState state;state.grid={2,1,1,1,false,false,{0,0}};state.momentum_x.resize(3);state.momentum_y.resize(4);
    state.cells.push_back({eos.initialize_at_temperature(std::array<double,6>{.8,.2,1e-28,0,0,0},300,1).mixture,{}});
    state.cells.push_back({eos.initialize_at_temperature(std::array<double,6>{.6,.4,0,1e-28,1e-30,1e-40},300,1).mixture,{}});
    const auto held=ma::testing::fingerprint(state);
    const auto next=advance_mac_diffusion(registry,eos,state,.1,{},profile);
    check(next.state.cells[0].mixture.masses[5]>0 && next.state.cells[1].mixture.masses[2]>0,"General path lost a constituent beyond the normal specialization");
    check(ma::testing::fingerprint(state)==held,"General path mutated source state");
    auto normal_source=state;normal_source.cells[1].mixture.masses[4]=normal_source.cells[1].mixture.masses[5]=0;
    const auto normal=advance_mac_diffusion(registry,eos,normal_source,.1,{},profile);
    for(std::size_t i=0;i<2;++i)for(std::size_t k=0;k<4;++k)
        check(std::abs(normal.state.cells[i].mixture.masses[k]-next.state.cells[i].mixture.masses[k])<=1e-12*std::max(normal.state.cells[i].mixture.masses[k],next.state.cells[i].mixture.masses[k]),"Normal and general arithmetic paths disagree");
    state.cells[0].mixture.masses[2]=std::numeric_limits<double>::denorm_min();
    const auto tiny=ma::testing::fingerprint(state);std::optional<MacDiffusionAdvance> subnormal;
    try { subnormal=advance_mac_diffusion(registry,eos,state,.1,{},profile); }
    catch(const std::runtime_error&) { }
    check(!subnormal || subnormal->state.cells[0].mixture.masses[2]>0,"Subnormal trace was silently removed");
    check(ma::testing::fingerprint(state)==tiny,"Unrepresentable trace update did not fail atomically");
    std::cout<<"PASS >4-species range fallback, positive traces, subnormal atomicity, moving owner and profile identity\n";
}
void benchmark(std::size_t cells) {
    Setup setup;const auto profile=GasTransportProfile::standard();std::vector<double> samples;
    for(const bool uniform:{true,false}) {
        auto state=setup.state(500,cells/500);
        if(uniform)for(auto& c:state.cells)c=state.cells[0];
        samples.clear();
        for(int i=0;i<36;++i) {
            const auto started=std::chrono::steady_clock::now();
            // Same two half-stages and exact zero-flux certificate as the owner.
            // No bulk-flow cost is included in this incremental allocation.
            for(int half=0;half<2;++half)if(!mac_diffusion_stationary(setup.registry,state))
                state=advance_mac_diffusion(setup.registry,*setup.eos,state,.05,{},*profile).state;
            if(i>=5)samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
        }
        std::sort(samples.begin(),samples.end());
        std::cout<<std::setprecision(10)<<"{\"scene\":\""<<(uniform ? "uniform":"mixing")<<"\",\"cells\":"<<cells
            <<",\"simulation_seconds\":0.1,\"half_stages\":2,\"samples\":31,\"warmup\":5,\"p50_ms\":"<<samples[15]<<",\"p95_ms\":"<<samples[29]<<",\"budget_ms\":6.25,\"passed\":"<<(samples[29]<=6.25 ? "true":"false")<<"}\n";
    }
}
void reference_cases() {
    Setup setup;const auto profile=GasTransportProfile::standard();
    std::cout<<std::setprecision(17)<<"{\"species\":[";
    for(std::size_t k=0;k<setup.registry.size();++k) {
        const auto& s=setup.registry[k];if(k)std::cout<<',';
        std::cout<<"{\"id\":\""<<s.id<<"\",\"weight\":"<<s.molar_mass<<",\"cv\":"<<s.cv()<<",\"offset\":"<<s.internal_energy(0)<<",\"cp\":"<<s.cp<<",\"formation\":"<<s.formation_enthalpy/s.molar_mass<<'}';
    }
    std::cout<<"],\"pairs\":[";bool first=true;
    for(const auto& p:profile->pairs()) {
        if(!first)std::cout<<',';
        first=false;
        std::cout<<'['<<setup.registry.index(p.first)<<','<<setup.registry.index(p.second)<<','<<p.a<<','<<p.b<<','<<p.c<<']';
    }
    std::cout<<"],\"cases\":[";
    for(int kind=0;kind<3;++kind) {
        if(kind)std::cout<<',';
        auto state=setup.state(2,1);
        const std::array<double,4> left=kind==2 ? std::array<double,4>{.80,.199,0,.001}:std::array<double,4>{.78,kind ? .2198:.22,0,kind ? .0002:0};
        const std::array<double,4> right=kind==2 ? std::array<double,4>{.72,.2798,0,.0002}:std::array<double,4>{.75,kind ? .249:.25,0,kind ? .001:0};
        state.cells[0].mixture=setup.eos->initialize_at_temperature(left,300,1).mixture;
        state.cells[1].mixture=setup.eos->initialize_at_temperature(right,300,1).mixture;
        std::cout<<"{\"name\":\""<<(kind==2 ? "ordinary-air-CO2-reversed":kind ? "ordinary-air-CO2":"binary-air")<<"\",\"initial\":[";
        for(std::size_t i=0;i<2;++i) {
            if(i)std::cout<<',';
            std::cout<<'[';
            for(const auto m:state.cells[i].mixture.masses)std::cout<<m<<',';
            std::cout<<state.cells[i].mixture.energy<<']';
        }
        const auto initial=state;const auto original=span(state);
        const auto mixing_time=[&](double dt) {
            state=initial;double elapsed=0;
            while(span(state)>.05*original && elapsed<200000) {
                state=advance_mac_diffusion(setup.registry,*setup.eos,state,dt,{},*profile).state;elapsed+=dt;
            }
            check(elapsed<200000,"Reference mixing case did not reach 95% uniformity");return elapsed;
        };
        const double coarse=mixing_time(10),fine=mixing_time(5);
        std::cout<<"],\"time_to_95_s\":"<<fine<<",\"coarse_time_to_95_s\":"<<coarse<<'}';
    }
    std::cout<<"]}\n";
}
}
int main(int argc,char** argv) {
    try { if(argc==3 && std::string(argv[1])=="--benchmark")benchmark(std::stoull(argv[2]));else if(argc==2 && std::string(argv[1])=="--reference")reference_cases();else { analytic();controls();general_range_controls(); }return 0; }
    catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; }
}
