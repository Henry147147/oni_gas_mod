#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_diffusion.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <numbers>
#include <tuple>

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
    for(const auto& [w,h]:{std::pair<std::size_t,std::size_t>{2,1},{3,2},{32,2}}) {
        auto source=setup.state(w,h);const double amplitude=.01;
        for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x) {
            const double oxygen=.25+amplitude*std::cos(std::numbers::pi*(static_cast<double>(x)+.5)/static_cast<double>(w));
            const std::vector<double> masses{1-oxygen,oxygen,0,0};
            source.cells[x+y*w].mixture=setup.eos->initialize_at_temperature(masses,300,1).mixture;
        }
        const double pressure=setup.eos->evaluate(source.cells[0].mixture,1).mechanical_pressure_pa;
        // Production combines the molecular coefficient with the Knudsen limit
        // of the 1 m cell (Bosanquet); the analytic mode uses the same value.
        const double coefficient=evaluate_gas_diffusivity(profile.pairs()[0],300,pressure,
            setup.registry[0].molar_mass,setup.registry[1].molar_mass,1).effective_m2_per_s;
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
    // The multiplier scales molecular coefficients; the Knudsen limit of the
    // cell is kinematic and unscaled. At one bar it changes the accelerated
    // coefficients by the relative amount bounded here (about 1e-5).
    double knudsen_scaling=0;
    for(const auto& pair:profile->pairs()) {
        const double w1=setup.registry[setup.registry.index(pair.first)].molar_mass,w2=setup.registry[setup.registry.index(pair.second)].molar_mass;
        const double fast=evaluate_gas_diffusivity(pair,300,101325,w1,w2,1,100).effective_m2_per_s,slow=evaluate_gas_diffusivity(pair,300,101325,w1,w2,1).effective_m2_per_s;
        knudsen_scaling=std::max(knudsen_scaling,std::abs(fast/(100*slow)-1));
    }
    check(knudsen_scaling>0 && knudsen_scaling<1e-4,"Unexpected unscaled Knudsen contribution at one bar");
    for(std::size_t i=0;i<source.cells.size();++i)for(std::size_t s=0;s<setup.registry.size();++s) {
        const double a=next.state.cells[i].mixture.masses[s],b=accelerated.state.cells[i].mixture.masses[s];
        check(std::abs(a-b)<=1e-12*std::max(a,b)+4*knudsen_scaling*std::abs(a-source.cells[i].mixture.masses[s]),"Accelerated diffusion does not preserve physical time scaling");
    }
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
    // Stored Fuller coefficients are platform-independent literals; they must
    // still agree with the formula evaluated by this host's libm to a few ulps.
    {
        const char* names[]={"nitrogen","oxygen","helium","carbon_dioxide"};
        const double weights[]={28.0134,31.9988,4.002602,44.0095},volumes[]={18.5,16.3,2.67,26.9};
        std::size_t compared=0;
        for(const auto& p:profile->pairs())if(p.estimated) {
            std::size_t i=4,j=4;for(std::size_t k=0;k<4;++k) { if(p.first==names[k])i=k;if(p.second==names[k])j=k; }
            check(i<4 && j<4,"Unknown Fuller pair");
            const double sum=std::cbrt(volumes[i])+std::cbrt(volumes[j]);
            const double formula=std::log(.001*std::sqrt(1/weights[i]+1/weights[j])/(sum*sum));
            check(std::abs(formula-p.a)<=4*std::numeric_limits<double>::epsilon()*std::abs(p.a),"Fuller literal differs from its formula");
            ++compared;
        }
        check(compared==4,"Fuller pair count changed");
    }
    const double d=profile->diffusivity("nitrogen","oxygen",300,101325);
    check(d>1.5e-5 && d<2.5e-5,"N2/O2 coefficient unit conversion failed");
    check(std::abs(profile->diffusivity("nitrogen","oxygen",300,101325./2)-2*d)<1e-14*d,"Gas diffusivity did not scale with pressure");
    // Validated range, extrapolation in the correlation's own form, and the
    // Bosanquet combination with the Knudsen limit (L/3) sqrt(8RT/(pi M_ij)).
    {
        const auto& pair=profile->pair("nitrogen","oxygen");const double w1=setup.registry[0].molar_mass,w2=setup.registry[1].molar_mass;
        const auto inside=evaluate_gas_diffusivity(pair,300,101325,w1,w2,1);
        const double knudsen=std::sqrt(8*gas_constant*300/(std::numbers::pi*w1*w2/(w1+w2)))/3;
        check(inside.validated && std::abs(inside.molecular_m2_per_s/d-1)<1e-14 && std::abs(inside.knudsen_m2_per_s/knudsen-1)<1e-14 &&
            std::abs(inside.effective_m2_per_s/(1/(1/d+1/knudsen))-1)<1e-14,"Bosanquet/Knudsen coefficient differs from its formula");
        for(const auto& [t,p]:{std::pair{200.,101325.},{250.,101325.},{480.,101325.},{600.,101325.},{300.,1000.},{300.,5e5}}) {
            const auto outside=evaluate_gas_diffusivity(pair,t,p,w1,w2,1);
            const double formula=1e-4*std::exp(pair.a+pair.b/t+pair.c*std::log(t))*101325/p;
            check(!outside.validated && std::abs(outside.molecular_m2_per_s/formula-1)<1e-14 && outside.effective_m2_per_s<outside.molecular_m2_per_s,
                "Out-of-range correlation was not extrapolated in its own form");
        }
        // Near vacuum the coefficient approaches the free-molecular limit,
        // bounding the explicit timestep; the multiplier does not scale it.
        for(const double p:{1.,1e-6,1e-300}) {
            const auto vacuum=evaluate_gas_diffusivity(pair,300,p,w1,w2,2,100);
            check(!vacuum.validated && vacuum.effective_m2_per_s<=vacuum.knudsen_m2_per_s && (p>1e-6 || vacuum.effective_m2_per_s>.99*vacuum.knudsen_m2_per_s),
                "Near-vacuum coefficient is not bounded by its Knudsen limit");
        }
    }
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
// One open row/room with a density step on the left and a small linear O2
// mass-fraction gradient: the review's opposing pressure/composition cases.
MacTransportState stepped(const Setup& setup,std::size_t w,std::size_t h,double bump,double gradient,std::size_t stepped_columns) {
    MacTransportState s;s.grid={w,h,1,1,false,false,std::vector<std::uint8_t>(w*h)};
    for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x) {
        std::vector<double> m(setup.registry.size());const double rho=1.17*(x<stepped_columns ? 1+bump : 1);
        const double o=.21+gradient*static_cast<double>(x)/static_cast<double>(w-1);
        m[0]=(1-o)*rho;m[1]=o*rho;s.cells.push_back({setup.eos->initialize_at_temperature(m,300,1).mixture,{}});
    }
    s.momentum_x.resize(s.grid.x_width()*h);s.momentum_y.resize(s.grid.y_height()*w);return s;
}
MacDiffusionSettings mode_setting(int mode) { return mode==1 ? MacDiffusionSettings{} : MacDiffusionSettings{MacDiffusionMode::accelerated,100}; }
void species_conserved(const SpeciesRegistry& registry,const MacTransportState& a,const MacTransportState& b,const char* why) {
    for(std::size_t s=0;s<registry.size();++s) {
        MacEnergyAccount balance;for(std::size_t i=0;i<a.cells.size();++i) { balance.add(b.cells[i].mixture.masses[s]);balance.add(-a.cells[i].mixture.masses[s]); }
        check(balance.within_bound(),why);
    }
}
void entropy_controls() {
    Setup setup;std::size_t runs=0,frames=0;
    // Composed owner: flow, conduction and diffusion, many frames, both modes.
    for(const auto& [w,h,columns,frame_count]:{std::tuple<std::size_t,std::size_t,std::size_t,int>{16,1,8,40},{3,2,1,150}})
    for(const double bump:{.01,.02,.05,.2})for(const double gradient:{1e-4,1e-5})for(int mode:{1,2}) {
        const auto source=stepped(setup,w,h,bump,gradient,columns);
        MacClosedSettings settings;settings.maximum_timestep_s=.01;settings.thermal_conduction=true;settings.diffusion=mode_setting(mode);
        MacClosedWorld world(setup.registry,setup.eos,source,settings);double exchanged=0;
        for(int frame=0;frame<frame_count;++frame) {
            auto next=world.advanced(.2,4096,MacExecutionPolicy::exact_equilibrium);
            check(next.report.diffusion.entropy_change_j_per_k>=-next.report.diffusion.entropy_roundoff_j_per_k,"Composed diffusion decreased entropy");
            exchanged+=next.report.diffusion.exchanged_mass_kg;world=std::move(next.world);++frames;
        }
        check(exchanged>0,"Opposing-gradient diffusion did not exchange mass");
        species_conserved(setup.registry,source,world.state(),"Opposing-gradient run lost a constituent");++runs;
    }
    // The same opposing gradients through the range-safe general path (a
    // 1e-200 helium trace), physical and accelerated.
    for(int mode:{1,2}) {
        auto source=stepped(setup,16,1,.05,1e-5,8);source.cells[3].mixture.masses[2]=1e-200;
        MacClosedSettings settings;settings.maximum_timestep_s=.01;settings.thermal_conduction=true;settings.diffusion=mode_setting(mode);
        MacClosedWorld world(setup.registry,setup.eos,source,settings);
        for(int frame=0;frame<8;++frame) { world=world.advanced(.2,4096,MacExecutionPolicy::exact_equilibrium).world;++frames; }
        species_conserved(setup.registry,source,world.state(),"General-path opposing-gradient run lost a constituent");++runs;
    }
    // Pure diffusion at fixed cell volumes: the entropy maximum balances the
    // composition gradient against pressure diffusion (barodiffusion), so a
    // mole-fraction-only force overshoots it. Checkerboard N2/CO2 at equal
    // moles, helium-rich ternaries, 10 s advances at the stability bound.
    for(double helium:{0.,.5,.8,.95}) {
        const std::size_t w=32;const double dx=.01,V=dx*dx,moles=101325*V/(gas_constant*300);
        MacTransportState state;state.grid={w,1,dx,1,true,false,std::vector<std::uint8_t>(w)};
        for(std::size_t x=0;x<w;++x) {
            const double e=1e-3*((x%2) ? 1 : -1),xn=(1-helium)*(.5+e),xc=(1-helium)*(.5-e);
            const std::array<double,4> m{xn*moles*setup.registry[0].molar_mass,0,helium*moles*setup.registry[2].molar_mass,xc*moles*setup.registry[3].molar_mass};
            state.cells.push_back({setup.eos->initialize_at_temperature(m,300,V).mixture,{}});
        }
        state.momentum_x.resize(state.grid.x_width());state.momentum_y.resize(state.grid.y_height()*w);const auto initial=state;
        for(int advance=0;advance<6;++advance) {
            auto next=advance_mac_diffusion(setup.registry,*setup.eos,state,10,{},*GasTransportProfile::standard());
            check(next.report.entropy_change_j_per_k>=-next.report.entropy_roundoff_j_per_k,"Checkerboard diffusion decreased entropy");
            state=std::move(next.state);
        }
        species_conserved(setup.registry,initial,state,"Checkerboard diffusion lost a constituent");++runs;
    }
    std::cout<<"PASS opposing pressure/composition gradients: "<<runs<<" runs, "<<frames<<" composed frames, physical and accelerated\n";
}
void vacuum_controls() {
    Setup setup;const auto profile=GasTransportProfile::standard();
    // Diffusion beside exact empty cells equals diffusion with those cells as
    // walls, bit for bit; the empty cells stay exactly empty.
    auto source=setup.state(4,2);source.cells[1].mixture.masses[2]=1e-9;source.cells[5].mixture.masses[3]=1e-4;
    source.cells[5].mixture=setup.eos->initialize_at_temperature(source.cells[5].mixture.masses,305,1).mixture;
    for(const std::size_t empty:{std::size_t(3),std::size_t(7)})source.cells[empty].mixture=MixtureInventory(setup.registry.size());
    auto walled=source;walled.grid.walls[3]=walled.grid.walls[7]=1;
    // A 1e-160 trace selects the range-safe general path for the same control.
    auto general=source;general.cells[6].mixture.masses[2]=1e-160;auto general_walled=general;general_walled.grid.walls[3]=general_walled.grid.walls[7]=1;
    for(const auto& [open_source,closed_source]:{std::pair{&source,&walled},{&general,&general_walled}})
    for(int mode:{1,2}) {
        const auto open=advance_mac_diffusion(setup.registry,*setup.eos,*open_source,1,mode_setting(mode),*profile);
        const auto closed=advance_mac_diffusion(setup.registry,*setup.eos,*closed_source,1,mode_setting(mode),*profile);
        check(open.report.exchanged_mass_kg>0,"Diffusion beside vacuum exchanged nothing");
        for(std::size_t i=0;i<open_source->cells.size();++i) {
            const auto& a=open.state.cells[i].mixture;const auto& b=(i==3 || i==7 ? *open_source : closed.state).cells[i].mixture;
            check(a.energy==b.energy && a.masses==b.masses,"Diffusion through an empty-cell face differs from a closed face");
        }
    }
    auto uniform=setup.state(3,1);uniform.cells[1]=uniform.cells[0];uniform.cells[2].mixture=MixtureInventory(setup.registry.size());
    check(mac_diffusion_stationary(setup.registry,uniform),"Empty-cell face broke the zero-flux certificate");
    auto warm=uniform;warm.cells[1].mixture=setup.eos->initialize_at_temperature(warm.cells[1].mixture.masses,301,1).mixture;
    check(!mac_diffusion_stationary(setup.registry,warm),"Unequal U admitted the zero-flux certificate");
    // Composed owner beside vacuum (the review case): gas expands into the
    // empty cell under diffusion, in both modes, conserving every constituent.
    for(int mode:{0,1,2}) {
        MacTransportState s;s.grid={4,1,1,1,false,false,std::vector<std::uint8_t>(4)};
        for(std::size_t x=0;x<4;++x) {
            std::vector<double> m(setup.registry.size());
            if(x<3) { m[0]=.9;m[1]=.25+.01*static_cast<double>(x);m[2]=x==0 ? 1e-28 : 0; }
            s.cells.push_back({x<3 ? setup.eos->initialize_at_temperature(m,300,1).mixture : MixtureInventory(setup.registry.size()),{}});
        }
        s.momentum_x.resize(5);s.momentum_y.resize(8);
        MacClosedSettings settings;settings.maximum_timestep_s=.01;settings.diffusion=mode ? mode_setting(mode) : MacDiffusionSettings::disabled();
        MacClosedWorld world(setup.registry,setup.eos,s,settings);
        for(int frame=0;frame<20;++frame)world=world.advanced(.2,4096,MacExecutionPolicy::exact_equilibrium).world;
        check(world.state().cells[3].mixture.mass()>0,"Gas did not enter the empty cell");
        species_conserved(setup.registry,s,world.state(),"Vacuum expansion lost a constituent");
    }
    // A single gas beside vacuum has no diffusive flux anywhere: the physical
    // and accelerated owners follow the diffusion-off trajectory bit for bit.
    std::vector<MacTransportState> trajectories;
    for(int mode:{0,1,2}) {
        MacTransportState s;s.grid={3,2,1,1,false,false,std::vector<std::uint8_t>(6)};
        for(std::size_t i=0;i<6;++i) {
            const std::array<double,4> m{1.1+.05*static_cast<double>(i),0,0,0};
            s.cells.push_back({i==2 || i==5 ? MixtureInventory(setup.registry.size()) : setup.eos->initialize_at_temperature(m,300+static_cast<double>(i),1).mixture,{}});
        }
        s.momentum_x.resize(s.grid.x_width()*2);s.momentum_y.resize(s.grid.y_height()*3);
        MacClosedSettings settings;settings.maximum_timestep_s=.01;settings.thermal_conduction=true;
        settings.diffusion=mode ? mode_setting(mode) : MacDiffusionSettings::disabled();
        MacClosedWorld world(setup.registry,setup.eos,s,settings);
        for(int frame=0;frame<20;++frame)world=world.advanced(.1,4096,MacExecutionPolicy::exact_equilibrium).world;
        trajectories.push_back(world.state());
    }
    check(trajectories[0].cells[2].mixture.mass()>0,"Single-gas control did not expand into vacuum");
    for(int mode:{1,2})check(ma::testing::fingerprint(trajectories[static_cast<std::size_t>(mode)])==ma::testing::fingerprint(trajectories[0]),
        "Diffusion changed a single gas beside vacuum");
    std::cout<<"PASS empty cells: closed empty faces, zero-flux certificate, vacuum expansion, single-gas trajectory identity\n";
}
// Mixed N2/O2/He(/CO2) room at a chosen temperature and pressure with composition gradients.
MacTransportState mixture_room(const Setup& setup,double temperature,double pressure,bool carbon_dioxide=true) {
    MacTransportState s;s.grid={3,2,1,1,false,false,std::vector<std::uint8_t>(6)};
    for(std::size_t i=0;i<6;++i) {
        const double f=static_cast<double>(i)/5,xo=.18+.06*f,xh=.02+.03*(1-f),xc=carbon_dioxide ? .03+.02*f : 0,xn=1-xo-xh-xc,moles=pressure/(gas_constant*temperature);
        const std::array<double,4> x{xn,xo,xh,xc};std::array<double,4> m{};
        for(std::size_t k=0;k<4;++k)m[k]=x[k]*moles*setup.registry[k].molar_mass;
        s.cells.push_back({setup.eos->initialize_at_temperature(m,temperature,1).mixture,{}});
    }
    s.momentum_x.resize(s.grid.x_width()*2);s.momentum_y.resize(s.grid.y_height()*3);return s;
}
double knudsen_limit(const Setup& setup,double temperature) {
    double result=0;
    for(const auto& pair:GasTransportProfile::standard()->pairs())
        result=std::max(result,evaluate_gas_diffusivity(pair,temperature,1,setup.registry[setup.registry.index(pair.first)].molar_mass,
            setup.registry[setup.registry.index(pair.second)].molar_mass,1).knudsen_m2_per_s);
    return result;
}
void check_recorded(const MacDiffusionReport& total,double temperature,double pressure,bool inside) {
    if(inside) { check(total.out_of_range_evaluations==0,"In-range state was counted as extrapolated");return; }
    check(total.out_of_range_evaluations>0,"Extrapolated state was not counted");
    const auto near=[](double a,double b) { return std::abs(a/b-1)<.05; };
    check(near(total.out_of_range_minimum_temperature_k,temperature) || near(total.out_of_range_maximum_temperature_k,temperature),"Extreme temperature not recorded");
    check(near(total.out_of_range_minimum_pressure_pa,pressure) || near(total.out_of_range_maximum_pressure_pa,pressure),"Extreme pressure not recorded");
}
void range_controls() {
    Setup setup;std::size_t runs=0;
    // Composed owner (flow, conduction, diffusion). At 200 K the room omits
    // CO2: the EOS rejects that mixture there even with diffusion disabled.
    struct Case { double temperature,pressure;bool inside,carbon_dioxide; };
    for(const auto& c:{Case{300,101325,true,true},Case{200,101325,false,false},Case{250,101325,false,true},Case{480,101325,false,true},
        Case{600,101325,false,true},Case{300,1000,false,true},Case{300,5e5,false,true},Case{300,1,false,true}})
    for(int mode:{1,2}) {
        const auto source=mixture_room(setup,c.temperature,c.pressure,c.carbon_dioxide);const double knudsen=knudsen_limit(setup,1.05*c.temperature);
        MacClosedSettings settings;settings.maximum_timestep_s=.01;settings.thermal_conduction=true;settings.diffusion=mode_setting(mode);
        MacClosedWorld world(setup.registry,setup.eos,source,settings);MacDiffusionReport total;
        for(int frame=0;frame<10;++frame) {
            auto next=world.advanced(.2,4096,MacExecutionPolicy::exact_equilibrium);const auto& d=next.report.diffusion;
            check(d.exchanged_mass_kg>0 && d.entropy_change_j_per_k>=-d.entropy_roundoff_j_per_k,"Range control did not diffuse");
            // Four half-intervals per 0.2 s frame; Knudsen caps every coefficient.
            check(static_cast<double>(d.substeps)<=4*(std::ceil(.05*knudsen/.18)+2),"Low-pressure diffusion substeps are unbounded");
            total.merge_out_of_range(d);world=std::move(next.world);
        }
        species_conserved(setup.registry,source,world.state(),"Range control lost a constituent");
        check_recorded(total,c.temperature,c.pressure,c.inside);++runs;
    }
    // Near vacuum the flow owner itself exhausts its attempt budget (with
    // diffusion disabled too), so the diffusion stage is exercised directly:
    // four 0.05 s half-intervals stay within the Knudsen-bounded substep count.
    for(const double pressure:{1e-4,1e-9})for(int mode:{1,2}) {
        auto state=mixture_room(setup,300,pressure);const auto initial=state;const double knudsen=knudsen_limit(setup,315);MacDiffusionReport total;
        for(int half=0;half<4;++half) {
            auto next=advance_mac_diffusion(setup.registry,*setup.eos,state,.05,mode_setting(mode),*GasTransportProfile::standard());
            check(next.report.exchanged_mass_kg>0 && static_cast<double>(next.report.substeps)<=std::ceil(.05*knudsen/.18)+2,"Near-vacuum diffusion substeps are unbounded");
            total.merge_out_of_range(next.report);state=std::move(next.state);
        }
        species_conserved(setup.registry,initial,state,"Near-vacuum diffusion lost a constituent");
        check_recorded(total,300,pressure,false);++runs;
    }
    std::cout<<"PASS out-of-range extrapolation and Knudsen bound: "<<runs<<" runs at 200-600 K and 1e-9 Pa-500 kPa, physical and accelerated\n";
}
// A single gas has no diffusive flux, so a diffusion-enabled owner must report
// exactly the flow counters of the diffusion-off owner. This 16x2 wave at a
// 0.1 s timestep takes main's pressure-increment restart.
void report_composition_control() {
    Setup setup;std::vector<MacClosedAdvance> results;
    for(int mode:{0,1,2}) {
        const std::size_t w=16,h=2;MacTransportState s;s.grid={w,h,1,1,false,false,std::vector<std::uint8_t>(w*h)};
        for(std::size_t i=0;i<w*h;++i) {
            const double x=(static_cast<double>(i%w)+.5)/static_cast<double>(w),p=100000+20*std::sin(2*std::numbers::pi*x);
            const std::array<double,4> m{p/(gas_constant*300)*setup.registry[0].molar_mass,0,0,0};
            s.cells.push_back({setup.eos->initialize_at_temperature(m,300,1).mixture,{}});
        }
        s.momentum_x.resize(s.grid.x_width()*h);s.momentum_y.resize(s.grid.y_height()*w);
        MacClosedSettings settings;settings.maximum_timestep_s=.1;settings.diffusion=mode ? mode_setting(mode) : MacDiffusionSettings::disabled();
        results.push_back(MacClosedWorld(setup.registry,setup.eos,s,settings).advanced(.1));
    }
    const auto& off=results[0].report;
    check(off.pressure_increment_restarts>0,"Report control did not take a pressure restart");
    for(std::size_t mode=1;mode<3;++mode) {
        const auto& r=results[mode].report;
        check(r.substeps==off.substeps && r.attempts==off.attempts && r.rejected_attempts==off.rejected_attempts &&
            r.pressure_iterations==off.pressure_iterations && r.nonlinear_iterations==off.nonlinear_iterations &&
            r.pressure_increment_restarts==off.pressure_increment_restarts && r.exact_equilibrium_intervals==off.exact_equilibrium_intervals,
            "Diffusion composition dropped or changed a flow report counter");
        check(ma::testing::fingerprint(results[mode].world.state())==ma::testing::fingerprint(results[0].world.state()),"Zero-flux diffusion changed the flow result");
    }
    std::cout<<"PASS composed reports keep flow counters, including pressure-increment restarts\n";
}
std::vector<std::uint8_t> resealed(std::vector<std::uint8_t> bytes) {
    std::uint64_t sum=14695981039346656037ULL;
    for(std::size_t i=0;i+8<bytes.size();++i) { sum^=bytes[i];sum*=1099511628211ULL; }
    for(int bit=0;bit<64;bit+=8)bytes[bytes.size()-8+static_cast<std::size_t>(bit/8)]=static_cast<std::uint8_t>(sum>>bit);
    return bytes;
}
bool rejects(const Setup& setup,const std::vector<std::uint8_t>& bytes) {
    try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,bytes); }catch(const std::invalid_argument&) { return true; }
    return false;
}
// Checkpoint versions: 2/3 reference, 4 interval kernel (main), 5 diffusion
// with either kernel. Unreleased version-4 diffusion layouts must reject.
void checkpoint_version_controls() {
    Setup setup;
    for(int mode:{1,2})for(const auto kernel:{MacNumericalKernel::reference,MacNumericalKernel::interval_audited}) {
        MacClosedSettings settings;settings.diffusion=mode_setting(mode);settings.numerical_kernel=kernel;settings.maximum_timestep_s=.01;
        const MacClosedWorld owner(setup.registry,setup.eos,setup.state(3,2),settings);const auto image=owner.checkpoint();
        check(image[8]==5,"Diffusion checkpoint is not version 5");
        const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
        check(restored.checkpoint()==image && restored.settings().numerical_kernel==kernel && restored.settings().diffusion.mode==settings.diffusion.mode,
            "Version-5 checkpoint lost its kernel or diffusion setting");
        // Diffusion wraps either kernel's flow interval; restore continues exactly.
        const auto a=owner.advanced(.2,4096,MacExecutionPolicy::exact_equilibrium),b=restored.advanced(.2,4096,MacExecutionPolicy::exact_equilibrium);
        check(a.world.checkpoint()==b.world.checkpoint() && a.report.diffusion.substeps>0,"Diffusion/kernel continuation differs after restore");
        if(kernel==MacNumericalKernel::interval_audited)
            check(a.report.interval_kernel_intervals+a.report.interval_reference_fallbacks>0,"Interval kernel did not run under diffusion");
        // Rebuild both unreleased version-4 diffusion layouts: formulation 1
        // (no kernel or formulation fields) and the first fix (no kernel field).
        const std::string id="ma-gas-transport-1";std::vector<std::uint8_t> marker{2,0,0,0,static_cast<std::uint8_t>(id.size()),0,0,0};
        marker.insert(marker.end(),id.begin(),id.end());
        const auto at=std::search(image.begin(),image.end(),marker.begin(),marker.end());
        check(at!=image.end(),"Diffusion formulation field is missing");
        const auto formulation=static_cast<std::size_t>(at-image.begin()),kernel_field=formulation-16;
        for(const bool keep_formulation:{false,true}) {
            std::vector<std::uint8_t> old(image.begin(),image.begin()+static_cast<std::ptrdiff_t>(kernel_field));
            old.insert(old.end(),image.begin()+static_cast<std::ptrdiff_t>(kernel_field+4),image.begin()+static_cast<std::ptrdiff_t>(formulation));
            old.insert(old.end(),image.begin()+static_cast<std::ptrdiff_t>(keep_formulation ? formulation : formulation+4),image.end());
            old[8]=4;check(rejects(setup,resealed(old)),"Unreleased version-4 diffusion checkpoint was admitted");
        }
        auto bad_kernel=image;bad_kernel[kernel_field]=2;check(rejects(setup,resealed(bad_kernel)),"Unknown kernel admitted in version 5");
        auto bad_formulation=image;bad_formulation[formulation]=1;check(rejects(setup,resealed(bad_formulation)),"Formulation 1 admitted in version 5");
        auto disabled=image;disabled[kernel_field+4]=0;check(rejects(setup,resealed(disabled)),"Diffusion-disabled version 5 admitted");
    }
    // Diffusion-disabled owners keep main's versions: 2 reference, 4 interval.
    MacClosedSettings reference;const MacClosedWorld plain(setup.registry,setup.eos,setup.state(3,2),reference);check(plain.checkpoint()[8]==2,"Reference owner left version 2");
    reference.numerical_kernel=MacNumericalKernel::interval_audited;
    const MacClosedWorld interval(setup.registry,setup.eos,setup.state(3,2),reference);check(interval.checkpoint()[8]==4,"Interval owner left version 4");
    std::cout<<"PASS checkpoint versions 2/4/5, diffusion with both kernels, unreleased v4 diffusion layouts rejected\n";
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
    try { if(argc==3 && std::string(argv[1])=="--benchmark")benchmark(std::stoull(argv[2]));else if(argc==2 && std::string(argv[1])=="--reference")reference_cases();else { analytic();controls();general_range_controls();vacuum_controls();entropy_controls();range_controls();checkpoint_version_controls();report_composition_control(); }return 0; }
    catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; }
}
