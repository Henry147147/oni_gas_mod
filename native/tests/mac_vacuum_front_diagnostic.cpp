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

};
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
// the measured front position and recent speed.
int run(std::size_t n,double& measured_front,double& measured_speed){
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
 MacClosedWorld world(set.registry,set.eos,s,settings);
 std::cout<<"VFCONFIG "<<n<<' '<<h<<' '<<dt<<' '<<p.sound_speed<<' '<<p.gamma<<' '<<p.time()<<'\n';
 for(std::size_t i=0;i<n;++i)std::cout<<"VFOCCUPIED 0 "<<i<<' '<<s.cells[i].mixture.mass()<<'\n';
 double last=0;for(int endpoint=0;endpoint<2;++endpoint){auto r=world.advanced((endpoint==0?.8:.2)*p.time(),65536);world=std::move(r.world);
 double front=0;std::size_t occupied=0;
 for(std::size_t i=0;i<n;++i){const auto mass=world.state().cells[i].mixture.mass();std::cout<<"VFOCCUPIED "<<endpoint+1<<' '<<i<<' '<<mass<<'\n';if(mass>0){front=(static_cast<double>(i)+1)*h;++occupied;}}
 std::cout<<std::setprecision(17)<<"VFRONT "<<n<<' '<<world.time_s()<<' '<<front<<' '<<p.front(world.time_s())<<' '<<occupied<<' '<<r.report.substeps<<' '<<r.report.rejected_attempts<<' '<<r.report.energy_residual_j<<' '<<r.report.energy_bound_j<<'\n';
 if(endpoint==1){const auto speed=(front-last)/(.2*p.time());std::cout<<"VSPEED "<<speed/p.sound_speed<<' '<<p.front_speed()/p.sound_speed<<'\n';
 measured_front=front;measured_speed=speed;
 if(std::abs(front-p.front(p.time()))>h || std::abs(speed-p.front_speed())*.2*p.time()>h)return 2;}
 last=front;}
 return 0;
}
// Gate V (docs/completion-plan.md): at N512 the front within 5% of the exact
// travel and the recent speed within 10% of exact, both errors decreasing over
// N128, N256 and N512.
int gate(){
 const Problem p=Setup{}.problem;const double travel=p.front(p.time())-p.interface_x;
 std::vector<double> front_errors,speed_errors;
 for(const std::size_t n:{128,256,512}){
  double front=0,speed=0;
  try{(void)run(n,front,speed);}
  catch(const std::exception& e){std::cout<<"VGATE n="<<n<<" stopped: "<<e.what()<<"\nGATE V FAIL\n";return 1;}
  front_errors.push_back(std::abs(front-p.front(p.time()))/travel);speed_errors.push_back(std::abs(speed-p.front_speed())/p.front_speed());
  std::cout<<"VGATE n="<<n<<" front_error="<<front_errors.back()<<" speed_error="<<speed_errors.back()<<'\n';
 }
 const bool decreasing=front_errors[1]<front_errors[0]&&front_errors[2]<front_errors[1]&&speed_errors[1]<speed_errors[0]&&speed_errors[2]<speed_errors[1];
 const bool pass=front_errors[2]<=.05&&speed_errors[2]<=.1&&decreasing;
 std::cout<<"GATE V "<<(pass?"PASS":"FAIL")<<'\n';return pass?0:1;
}
}
int main(int argc,char**argv){try{
 require(argc==1 || argc==2,"Expected an optional grid count or --gate");
 if(argc==2 && std::string(argv[1])=="--gate")return gate();
 double front=0,speed=0;return run(argc>1?std::stoul(argv[1]):32,front,speed);
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
