#include "mac_fixture.hpp"
// Original GAS_VACUUM.md problem and ideal-gas validation profile.
// This diagnostic checks the strictly positive cell support, not a density cutoff.
#include <numeric>
#include <iomanip>
#include <fstream>
namespace {
void require(bool x,const char* message){if(!x)throw std::runtime_error(message);}
struct Problem {
    double length = 8, interface_x = 2, rho = 1, pressure = 100000, velocity = 0;
    double gamma = 0, sound_speed = 0, temperature = 0;
    [[nodiscard]] double time() const { return .5 / sound_speed; }
    [[nodiscard]] double front_speed() const { return velocity + 2 * sound_speed / (gamma - 1); }
    [[nodiscard]] double head(double t) const { return interface_x + (velocity - sound_speed) * t; }
    [[nodiscard]] double front(double t) const { return interface_x + front_speed() * t; }
    // Position with the given fraction of the gas mass beyond it. In the fan the
    // mass beyond x is rho c0 t (c/c0)^((g+1)/(g-1)), c/c0 = (2 - (g-1) zeta)/(g+1).
    [[nodiscard]] double mass_front(double t, double fraction) const {
        const double c = std::pow(fraction * rho * interface_x / (rho * sound_speed * t), (gamma - 1) / (gamma + 1));
        return interface_x + (2 - (gamma + 1) * c) / (gamma - 1) * sound_speed * t;
    }
};
// Gate V's front: the position with 0.1% of the gas mass beyond it.
constexpr double front_fraction = 1e-3;
// Measured counterpart; a partly filled void-phase cell holds its gas at its start.
double mass_front(const ma::experimental::MacTransportState& s, double h, double fraction) {
    double total = 0; for (const auto& c : s.cells) total += c.mixture.mass();
    double beyond = 0;
    for (std::size_t i = s.cells.size(); i-- > 0;) {
        const double m = s.cells[i].mixture.mass(); if (m == 0) continue;
        const double width = std::min(h, ma::experimental::mac_material_volume(s, i));
        if (beyond + m >= fraction * total) return static_cast<double>(i) * h + width * (1 - (fraction * total - beyond) / m);
        beyond += m;
    }
    return 0;
}
struct Fronts { double support[2]{}, mass[2]{}; };
struct Setup {
    ma::SpeciesRegistry registry = ma::SpeciesRegistry::standard();
    std::vector<double> fractions;
    std::shared_ptr<const ma::FluidThermodynamics> eos;
    Problem problem;
    Setup() : fractions(registry.size()) {
        fractions[registry.index("oxygen")] = .22; fractions[registry.index("nitrogen")] = .78;
        fractions[registry.index("helium")] = 1e-28;
        std::vector<ma::experimental::ReferenceMaterial> materials;
        double gas_coefficient = 0, capacity = 0;
        for (const auto* id : {"oxygen", "nitrogen", "helium"}) {
            const auto index = registry.index(id); const auto& species = registry[index];
            ma::experimental::ReferenceMaterial m;
            m.id = id; m.phase = ma::experimental::ReferencePhase::ideal_gas;
            m.molar_mass_kg_per_mol = species.molar_mass; m.cv_j_per_kg_k = species.cv();
            m.energy_offset_j_per_kg = species.internal_energy(0);
            m.minimum_temperature_k = 1e-20; m.maximum_temperature_k = 1e9;
            m.provenance = "Explicit constant-property ideal-gas rarefaction validation profile";
            materials.push_back(std::move(m));
            gas_coefficient += fractions[index] * ma::gas_constant / species.molar_mass;
            capacity += fractions[index] * species.cv();
        }
        problem.gamma = 1 + gas_coefficient / capacity;
        problem.sound_speed = std::sqrt(problem.gamma * problem.pressure / problem.rho);
        problem.temperature = problem.pressure / (problem.rho * gas_coefficient);
        eos = std::make_shared<const ma::FluidThermodynamics>(registry, "ideal-gas-vacuum-validation-v1", std::move(materials));
    }
};

// One grid: prints the diagnostic records; returns 2 when the front or its
// recent speed misses the exact fan by more than a cell. front/speed receive
// the measured front position and recent speed. With voids, the void phase
// keeps each cell's occupied volume, the front is the gas/void line, and the
// gate's mass-fraction fronts are also recorded at both endpoints.
int run(std::size_t n,double& measured_front,double& measured_speed,bool voids=false,Fronts* fronts=nullptr,bool production=false,bool subcell=false){
 using namespace ma;using namespace ma::experimental;
 Setup set;const auto& p=set.problem;
 require(n>=16 && n<=2048 && n%4==0,"Invalid vacuum front grid");
 std::cout<<std::setprecision(17);
 for(const auto& m:set.eos->reference().materials())std::cout<<"VFMATERIAL "<<set.registry.index(m.id)<<' '<<m.molar_mass_kg_per_mol<<' '<<m.cv_j_per_kg_k<<'\n';
 for(std::size_t k=0;k<set.fractions.size();++k)if(set.fractions[k]>0)std::cout<<"VFFRACTION "<<k<<' '<<set.fractions[k]<<'\n';
 const double h=p.length/static_cast<double>(n),v=h,dt=.025*h/p.sound_speed;
 MacTransportState s;s.grid={n,1,h,1/h,false,false,std::vector<std::uint8_t>(n)};
 s.momentum_x.assign(n+1,0);s.momentum_y.assign(2*n,0);
 for(std::size_t i=0;i<n;++i){MacMaterialCell c{MixtureInventory(set.registry.size()),{}};if(i<n/4){auto m=set.fractions;for(auto& x:m)x*=p.rho*v;c.mixture=set.eos->initialize_at_temperature(m,p.temperature,v).mixture;}s.cells.push_back(std::move(c));}
 MacClosedSettings settings;settings.maximum_timestep_s=dt;settings.reconstruction=MacMaterialReconstruction::swept_first_moment;
 settings.momentum_reconstruction=MacMomentumReconstruction::swept_energy_flux;settings.compression_viscosity=MacCompressionViscosity::coupled_pressure;
 if(voids){settings.vacuum=MacVacuumRepresentation::void_phase;settings.momentum_reconstruction=MacMomentumReconstruction::donor_cell;}
 // The production kernel implements material selector 1 and the default compression law.
 if(production){settings.reconstruction=MacMaterialReconstruction::limited_linear;settings.compression_viscosity=MacCompressionViscosity{};}
 if(subcell){settings.numerical_kernel=MacNumericalKernel::production;settings.subcell_void=MacSubcellVoidPolicy::expand_unresolved_gas;}
 const auto policy=production?MacExecutionPolicy::production:MacExecutionPolicy::reference;
 MacClosedWorld world(set.registry,set.eos,s,settings);
 std::cout<<"VFCONFIG "<<n<<' '<<h<<' '<<dt<<' '<<p.sound_speed<<' '<<p.gamma<<' '<<p.time()<<'\n';
 for(std::size_t i=0;i<n;++i)std::cout<<"VFOCCUPIED 0 "<<i<<' '<<s.cells[i].mixture.mass()<<'\n';
 double last=0;for(int endpoint=0;endpoint<2;++endpoint){auto r=world.advanced((endpoint==0?.8:.2)*p.time(),65536,policy);
 require(!production || r.report.interval_reference_fallbacks==0,"Production vacuum front fell back to the reference");world=std::move(r.world);
 double front=0;std::size_t occupied=0;
 for(std::size_t i=0;i<n;++i){const auto mass=world.state().cells[i].mixture.mass();std::cout<<"VFOCCUPIED "<<endpoint+1<<' '<<i<<' '<<mass<<'\n';if(mass>0){front=(static_cast<double>(i)+1)*h;++occupied;
  // The x face area is spacing * depth = 1 m2, so the occupied volume is the gas depth.
  if(voids)front=static_cast<double>(i)*h+mac_material_volume(world.state(),i);}}
 std::cout<<std::setprecision(17)<<"VFRONT "<<n<<' '<<world.time_s()<<' '<<front<<' '<<p.front(world.time_s())<<' '<<occupied<<' '<<r.report.substeps<<' '<<r.report.rejected_attempts<<' '<<r.report.energy_residual_j<<' '<<r.report.energy_bound_j<<'\n';
 if(voids){const double x=mass_front(world.state(),h,front_fraction);
  std::cout<<"VMASSFRONT "<<n<<' '<<world.time_s()<<' '<<x<<' '<<p.mass_front(world.time_s(),front_fraction)<<'\n';
  if(fronts){fronts->support[endpoint]=front;fronts->mass[endpoint]=x;}}
 if(endpoint==1){const auto speed=(front-last)/(.2*p.time());std::cout<<"VSPEED "<<speed/p.sound_speed<<' '<<p.front_speed()/p.sound_speed<<'\n';
 measured_front=front;measured_speed=speed;
 if(std::abs(front-p.front(p.time()))>h || std::abs(speed-p.front_speed())*.2*p.time()>h)return 2;}
 last=front;}
 return 0;
}
// Gate V (docs/completion-plan.md), void phase: at N512 the front with 0.1% of
// the gas mass beyond it lies within 5% of its exact travel and moves over the
// last 0.2t within 10% of its exact displacement, both errors decreasing over
// N128, N256 and N512; and no gas lies more than one cell ahead of the exact
// front at either endpoint.
int gate(bool production=false,bool subcell=false){
 const Problem p=Setup{}.problem;
 std::vector<double> front_errors,speed_errors;bool contained=true;
 for(const std::size_t n:{128,256,512}){
  double front=0,speed=0;Fronts f;
  try{(void)run(n,front,speed,true,&f,production,subcell);}
  catch(const std::exception& e){std::cout<<"VGATE n="<<n<<" stopped: "<<e.what()<<"\nGATE V FAIL\n";return 1;}
  const double t1=.8*p.time(),t2=p.time(),h=p.length/static_cast<double>(n);
  const double exact=p.mass_front(t2,front_fraction),moved=exact-p.mass_front(t1,front_fraction);
  front_errors.push_back(std::abs(f.mass[1]-exact)/(exact-p.interface_x));
  speed_errors.push_back(std::abs(f.mass[1]-f.mass[0]-moved)/moved);
  const double lead=std::max(f.support[0]-p.front(t1),f.support[1]-p.front(t2));contained=contained&&lead<=h;
  std::cout<<"VGATE n="<<n<<" front_error="<<front_errors.back()<<" speed_error="<<speed_errors.back()
   <<" support_lead_cells="<<lead/h<<'\n';
 }
 const bool decreasing=front_errors[1]<front_errors[0]&&front_errors[2]<front_errors[1]&&speed_errors[1]<speed_errors[0]&&speed_errors[2]<speed_errors[1];
 const bool pass=front_errors[2]<=.05&&speed_errors[2]<=.1&&decreasing&&contained;
 std::cout<<"GATE V "<<(pass?"PASS":"FAIL")<<'\n';return pass?0:1;
}
}
int main(int argc,char**argv){try{
 // --void selects the void phase for one grid; --gate always uses it.
 if(argc==2 && std::string(argv[1])=="--gate")return gate();
 // --gate-production runs gate V on the production kernel (selector 1).
 if(argc==2 && std::string(argv[1])=="--gate-production")return gate(true);
 if(argc==2 && std::string(argv[1])=="--gate-production-subcell")return gate(true,true);
 const bool voids=argc>1 && std::string(argv[1])=="--void";const int first=voids?2:1;
 require(argc==first || argc==first+1,"Expected --gate, or [--void] and an optional grid count");
 double front=0,speed=0;return run(argc>first?std::stoul(argv[first]):32,front,speed,voids);
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
