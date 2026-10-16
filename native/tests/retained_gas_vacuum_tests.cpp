#include "mixed_atmospheres/occupied_staggered.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

// Same documented physical problem/analytic integrals as the earlier Eulerian
// gate. Only the retained nodal formulation is advanced here. The Cartesian
// samples are read-only overlap integrals; they never reset canonical state.
// Primary analytic source: https://www.clawpack.org/riemann_book/html/Euler.html
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(double actual, double expected, double relative, double absolute = 0) {
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= relative * std::abs(expected) + absolute, "Analytic comparison failed");
}
struct Integrals { double mass = 0, momentum = 0, pressure = 0, kinetic = 0; };
struct Problem {
    double length = 8, interface_x = 2, rho = 1, pressure = 100000, velocity = 0;
    double gamma = 0, sound_speed = 0, temperature = 0;
    [[nodiscard]] double time() const { return .5 / sound_speed; }
    [[nodiscard]] double front_speed() const { return velocity + 2 * sound_speed / (gamma - 1); }
    [[nodiscard]] double head(double t) const { return interface_x + (velocity - sound_speed) * t; }
    [[nodiscard]] double front(double t) const { return interface_x + front_speed() * t; }
    [[nodiscard]] Integrals integrate(double left, double right, double t) const {
        Integrals result;
        const double uniform_end = std::min(right, head(t));
        if (uniform_end > left) {
            const double width = uniform_end - left;
            result.mass = rho * width; result.momentum = rho * velocity * width;
            result.pressure = pressure * width; result.kinetic = .5 * rho * velocity * velocity * width;
        }
        const double a = std::max(left, head(t)), b = std::min(right, front(t));
        if (b <= a) return result;
        const double exponent = 2 / (gamma - 1), slope = (gamma - 1) / (gamma + 1);
        const auto lambda = [&](double x) {
            // The exact fan is bounded by c/c0=1 and 0. No numerical fluid
            // state or density is clipped by this analytic integration.
            if (x == head(t)) return 1.0;
            if (x == front(t)) return 0.0;
            return (2 * sound_speed + (gamma - 1) * (velocity - (x - interface_x) / t)) /
                ((gamma + 1) * sound_speed);
        };
        const auto integral_power = [&](double power) {
            return sound_speed * t / (slope * (power + 1)) *
                (std::pow(lambda(a), power + 1) - std::pow(lambda(b), power + 1));
        };
        const double i0 = integral_power(exponent), i1 = integral_power(exponent + 1), i2 = integral_power(exponent + 2);
        const double terminal = front_speed(), coefficient = 2 * sound_speed / (gamma - 1);
        result.mass += rho * i0;
        result.momentum += rho * (terminal * i0 - coefficient * i1);
        result.pressure += pressure * integral_power(exponent * gamma);
        result.kinetic += .5 * rho * (terminal * terminal * i0 - 2 * terminal * coefficient * i1 + coefficient * coefficient * i2);
        return result;
    }
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

using ma::experimental::OccupiedStaggeredState;
struct Sample { double mass=0, momentum=0, pressure_integral=0, occupied_width=0; };
struct Evidence {
    std::size_t n=0, steps=0, nonlinear=0, linear=0, unresolved_momentum=0, unresolved_energy=0, unresolved_geometry=0;
    double courant=0, front=0, recent_speed=0, tip_velocity=0, tip_density=0, tip_pressure=0;
    double density_l1=0, velocity_l1=0, pressure_l1=0, mass_relative=0, energy_relative=0, entropy_change=0;
    double wall_momentum_residual=0, analytic_momentum_relative=0, signed_geometry_residual=0;
    double absolute_geometry_residual=0, maximum_root_residual=0, reported_energy_error=0;
};
bool same_state(const OccupiedStaggeredState& a,const OccupiedStaggeredState& b) {
    if (a.model()!=b.model() || a.model_metadata()!=b.model_metadata() || a.time_seconds()!=b.time_seconds() ||
        a.origin_high_m()!=b.origin_high_m() || a.origin_low_m()!=b.origin_low_m() ||
        a.area_m2()!=b.area_m2() || a.gravity_m_per_s2()!=b.gravity_m_per_s2() || a.retains_local_geometry()!=b.retains_local_geometry() ||
        a.cumulative_energy_balance_error_j()!=b.cumulative_energy_balance_error_j() ||
        a.cumulative_geometry_volume_residual_m3()!=b.cumulative_geometry_volume_residual_m3() ||
        a.cells().size()!=b.cells().size() || a.nodes().size()!=b.nodes().size()) return false;
    for (std::size_t i=0;i<a.nodes().size();++i) {
        const auto& x=a.nodes()[i];const auto& y=b.nodes()[i];
        if(x.relative_position_m!=y.relative_position_m || x.relative_position_low_m!=y.relative_position_low_m || x.inertia_kg!=y.inertia_kg ||
            x.momentum_kg_m_per_s!=y.momentum_kg_m_per_s || x.fixed!=y.fixed) return false;
    }
    for (std::size_t i=0;i<a.cells().size();++i) {
        const auto& x=a.cells()[i];const auto& y=b.cells()[i];
        if(x.mixture.masses!=y.mixture.masses || x.mixture.energy!=y.mixture.energy || x.occupied_volume_m3!=y.occupied_volume_m3 ||
            x.bulk_momentum_kg_m_per_s!=y.bulk_momentum_kg_m_per_s || x.mechanical_variance_j!=y.mechanical_variance_j ||
            x.thermodynamics.temperature_k!=y.thermodynamics.temperature_k ||
            x.thermodynamics.mechanical_pressure_pa!=y.thermodynamics.mechanical_pressure_pa ||
            x.thermodynamics.entropy_j_per_k!=y.thermodynamics.entropy_j_per_k) return false;
    }
    return true;
}
struct Coordinate {
    double high=0, low=0;
    explicit Coordinate(double value=0):high(value) {}
    void add(double value) {
        const auto sum=high+value,z=sum-high,error=(high-(sum-z))+(value-z);
        const auto tail=low+error,next=sum+tail,q=next-sum;low=(sum-(next-q))+(tail-q);high=next;
    }
    [[nodiscard]] double value() const {return high+low;}
};
Coordinate difference(Coordinate a,Coordinate b) {a.add(-b.high);a.add(-b.low);return a;}
bool less(Coordinate a,Coordinate b) {const auto d=difference(a,b);return d.high<0 || (d.high==0 && d.low<0);}
Coordinate coordinate(const OccupiedStaggeredState& state,std::size_t node) {
    Coordinate result(state.origin_high_m());result.add(state.origin_low_m());result.add(state.nodes()[node].relative_position_m);
    result.add(state.nodes()[node].relative_position_low_m);return result;
}
double position(const OccupiedStaggeredState& state,std::size_t node) {
    return coordinate(state,node).value();
}
void oracle(const Setup& setup) {
    const auto& p=setup.problem;const auto total=p.integrate(0,p.length,p.time());
    near(total.mass,p.rho*p.interface_x,2e-14);near(total.momentum,p.pressure*p.time(),2e-14);
    near(total.pressure/(p.gamma-1)+total.kinetic,p.pressure*p.interface_x/(p.gamma-1),3e-14);
    near(p.front_speed(),p.velocity+2*std::sqrt(p.gamma*p.pressure/p.rho)/(p.gamma-1),1e-15);
    std::cout<<std::setprecision(17)<<"ORACLE gamma="<<p.gamma<<" c="<<p.sound_speed<<" T="<<p.temperature<<
        " t="<<p.time()<<" front="<<p.front(p.time())<<" speed/c="<<p.front_speed()/p.sound_speed<<std::endl;
}
Evidence run(const Setup& setup,std::size_t n,double courant,const std::string& csv) {
    require(n>=16 && n<=2048 && n%4==0 && std::isfinite(courant) && courant>0 && courant<=.05,"Invalid retained rarefaction case");
    const auto& p=setup.problem;const auto dx=p.length/static_cast<double>(n);const auto dt=courant*dx/p.sound_speed;
    ma::experimental::StaggeredInitialization input;input.thermodynamics=setup.eos;input.area_m2=1;
    input.retain_local_geometry=true;
    input.gravity_m_per_s2=0;input.left=ma::experimental::StaggeredBoundary::rigid_wall;
    input.right=ma::experimental::StaggeredBoundary::traction_free;
    auto masses=setup.fractions;for(auto& mass:masses) mass*=p.rho*dx;
    const auto initial=setup.eos->initialize_at_temperature(masses,p.temperature,dx);
    for(std::size_t i=0;i<n/4;++i) input.cells.push_back({initial.mixture,0});
    for(std::size_t i=0;i<=n/4;++i) {input.positions_m.push_back(static_cast<double>(i)*dx);input.velocities_m_per_s.push_back(0);}
    auto state=ma::experimental::initialize_occupied_staggered(input);const auto before=state.totals();
    double initial_entropy=0,entropy_capacity=0,thermal_scale=0;
    for(const auto& cell:state.cells()) {initial_entropy+=cell.thermodynamics.entropy_j_per_k;
        entropy_capacity+=cell.thermodynamics.intrinsic_heat_capacity_j_per_k;thermal_scale+=cell.thermodynamics.thermal_energy_j;}
    Evidence result;result.n=n;result.courant=courant;double wall_impulse=0;
    const auto limit=static_cast<std::size_t>(std::ceil(p.time()/dt))+4;
    const auto march=[&](double target) {
        while(state.time_seconds()<target) {
            require(result.steps<limit,"Retained front step-count cap exceeded");
            const auto h=std::min(dt,target-state.time_seconds());const auto prior=state;
            ma::experimental::StaggeredStepReport report;
            try {report=ma::experimental::advance_occupied_staggered(state,h);}
            catch(const ma::FluidFatalError& failure) {
                const bool atomic=same_state(state,prior);
                std::cerr<<std::setprecision(17)<<"STEP_FAILURE N="<<n<<" completed_steps="<<result.steps<<
                    " time="<<prior.time_seconds()<<" h="<<h<<" code="<<static_cast<int>(failure.code())<<
                    " inspected_state_unchanged="<<atomic<<" reason="<<failure.what()<<std::endl;
                require(atomic,"Retained failed step changed canonical state/readings");throw;
            }
            ++result.steps;result.nonlinear+=report.nonlinear_iterations;result.linear+=report.pressure_iterations;
            result.unresolved_momentum+=report.unresolved_momentum_updates;result.unresolved_energy+=report.unresolved_energy_updates;
            result.unresolved_geometry+=report.unresolved_volume_updates+report.unresolved_position_updates;
            result.signed_geometry_residual+=report.signed_geometry_volume_residual_m3;
            result.absolute_geometry_residual+=report.absolute_geometry_volume_residual_m3;
            result.maximum_root_residual=std::max(result.maximum_root_residual,report.maximum_endpoint_relative_volume_residual);
            result.reported_energy_error+=report.energy_balance_error_j;wall_impulse+=report.wall_reaction_impulse_kg_m_per_s;
        }
    };
    std::cout<<"CASE_BEGIN N="<<n<<" C="<<courant<<" material_cells="<<state.cells().size()<<" step_cap="<<limit<<std::endl;
    march(.8*p.time());const auto first_front=position(state,state.nodes().size()-1);
    march(p.time());result.front=position(state,state.nodes().size()-1);result.recent_speed=(result.front-first_front)/(.2*p.time());
    const auto after=state.totals();result.energy_relative=((after.internal_energy_j-before.internal_energy_j)+
        (after.nodal_kinetic_energy_j-before.nodal_kinetic_energy_j))/thermal_scale;
    for(std::size_t s=0;s<before.masses_kg.size();++s) {
        if(before.masses_kg[s]>0) result.mass_relative=std::max(result.mass_relative,std::abs(after.masses_kg[s]/before.masses_kg[s]-1));
        else require(after.masses_kg[s]==0,"Retained fan invented a constituent");
    }
    double entropy=0;std::vector<Sample> samples(n);
    for(std::size_t i=0;i<state.cells().size();++i) {
        const auto& cell=state.cells()[i];const auto left=coordinate(state,i),right=coordinate(state,i+1);
        const auto width=difference(right,left).value();
        const auto ul=state.nodes()[i].velocity_m_per_s(),ur=state.nodes()[i+1].velocity_m_per_s();
        const auto density=cell.mixture.mass()/cell.occupied_volume_m3;
        require(!less(left,Coordinate(0)) && less(left,right) && !less(Coordinate(p.length),right),"Retained material interval left the fixed probe domain");
        require(cell.thermodynamics.mechanical_pressure_pa>0 && cell.thermodynamics.temperature_k>0 && !cell.thermodynamics.has_vacuum,
            "Positive retained gas acquired a floor or internal vacuum");
        require(cell.mixture.masses==initial.mixture.masses,"Material trajectory changed a constituent/trace");
        entropy+=cell.thermodynamics.entropy_j_per_k;
        for(std::size_t j=0;j<n;++j) {
            const Coordinate bin_left(static_cast<double>(j)*dx),bin_right(static_cast<double>(j+1)*dx);
            const auto a=less(left,bin_left)?bin_left:left,b=less(right,bin_right)?right:bin_right;
            if(!less(a,b)) continue;
            const auto overlap=difference(b,a).value();
            const auto offset=std::midpoint(difference(a,left).value(),difference(b,left).value());
            const auto u=ul+(ur-ul)*offset/width;
            samples[j].mass+=density*overlap;samples[j].momentum+=density*overlap*u;
            samples[j].pressure_integral+=cell.thermodynamics.mechanical_pressure_pa*overlap;samples[j].occupied_width+=overlap;
        }
    }
    const auto& tip=state.cells().back();result.tip_velocity=state.nodes().back().velocity_m_per_s();
    result.tip_density=tip.mixture.mass()/tip.occupied_volume_m3;result.tip_pressure=tip.thermodynamics.mechanical_pressure_pa;
    result.entropy_change=entropy-initial_entropy;result.wall_momentum_residual=after.momentum_kg_m_per_s-before.momentum_kg_m_per_s-wall_impulse;
    result.analytic_momentum_relative=after.momentum_kg_m_per_s/(p.pressure*p.time())-1;
    const auto fan_width=p.front(p.time())-p.head(p.time());double weight=0,sampled_momentum=0;
    std::ofstream output;if(!csv.empty()) {output.open(csv);require(output.good(),"Cannot open retained fan profile");
        output<<std::setprecision(17)<<"x,rho,rho_exact,u,u_exact,p,p_exact,occupied_fraction\n";}
    for(std::size_t j=0;j<n;++j) {
        const auto a=static_cast<double>(j)*dx,b=static_cast<double>(j+1)*dx;const auto exact=p.integrate(a,b,p.time());const auto& sample=samples[j];
        const auto u=sample.mass>0?sample.momentum/sample.mass:0;const auto exact_u=exact.mass>0?exact.momentum/exact.mass:0;
        result.density_l1+=std::abs(sample.mass-exact.mass)/(p.rho*fan_width);
        result.pressure_l1+=std::abs(sample.pressure_integral-exact.pressure)/(p.pressure*fan_width);sampled_momentum+=sample.momentum;
        if(exact.mass>0 && a>=p.head(p.time()) && b<=p.front(p.time())-.5*p.sound_speed*p.time()) {
            result.velocity_l1+=exact.mass*std::abs(u-exact_u);weight+=exact.mass;
        }
        if(output.is_open()) output<<std::midpoint(a,b)<<','<<sample.mass/dx<<','<<exact.mass/dx<<','<<u<<','<<exact_u<<','<<
            sample.pressure_integral/dx<<','<<exact.pressure/dx<<','<<sample.occupied_width/dx<<'\n';
    }
    require(weight>0,"No resolved retained fan velocity sample");result.velocity_l1/=weight*p.sound_speed;
    require(samples.back().mass==0 && samples.back().momentum==0 && samples.back().pressure_integral==0 && samples.back().occupied_width==0,
        "Retained fan seeded far exterior vacuum");
    require(result.mass_relative<2e-11 && std::abs(result.energy_relative)<2e-10,"Retained fan failed trace/total-energy conservation");
    require(std::abs(result.wall_momentum_residual)<2e-10*p.pressure*p.time() &&
        std::abs(sampled_momentum-after.momentum_kg_m_per_s)<2e-10*p.pressure*p.time(),"Retained fan momentum/overlap balance failed");
    require(result.entropy_change>=-512*std::numeric_limits<double>::epsilon()*entropy_capacity,"Retained fan decreased total entropy");
    return result;
}
void print(const Setup& setup,const Evidence& r) {
    const auto& p=setup.problem;
    std::cout<<std::setprecision(17)<<"N="<<r.n<<" C="<<r.courant<<" steps="<<r.steps<<" front="<<r.front<<
        " front_exact="<<p.front(p.time())<<" front_error="<<std::abs(r.front-p.front(p.time()))<<" recent_speed/c="<<r.recent_speed/p.sound_speed<<
        " speed_error/c="<<std::abs(r.recent_speed-p.front_speed())/p.sound_speed<<" tip_u/c="<<r.tip_velocity/p.sound_speed<<
        " tip_rho="<<r.tip_density<<" tip_p="<<r.tip_pressure<<" rho_L1="<<r.density_l1<<" p_L1="<<r.pressure_l1<<" u_L1="<<r.velocity_l1<<
        " mass_rel="<<r.mass_relative<<" energy_rel="<<r.energy_relative<<" entropy_change="<<r.entropy_change<<
        " wall_momentum_residual="<<r.wall_momentum_residual<<" analytic_momentum_rel="<<r.analytic_momentum_relative<<
        " geometry_signed="<<r.signed_geometry_residual<<" geometry_absolute="<<r.absolute_geometry_residual<<
        " root_relative="<<r.maximum_root_residual<<" reported_energy_error="<<r.reported_energy_error<<
        " unresolved_P="<<r.unresolved_momentum<<" unresolved_U="<<r.unresolved_energy<<" unresolved_geometry="<<r.unresolved_geometry<<
        " nonlinear="<<r.nonlinear<<" linear="<<r.linear<<std::endl;
}
}
int main(int argc,char** argv) {
    try {
        const Setup setup;oracle(setup);
        if(argc==2 && std::string(argv[1])=="--front-gate") {
            std::vector<Evidence> cases;
            for(const std::size_t n:{512u,1024u,2048u}) {cases.push_back(run(setup,n,.025,""));print(setup,cases.back());}
            const auto exact=setup.problem.front(setup.problem.time());const auto speed=setup.problem.front_speed();
            for(std::size_t i=1;i<cases.size();++i) require(std::abs(cases[i].front-exact)<std::abs(cases[i-1].front-exact) &&
                std::abs(cases[i].recent_speed-speed)<std::abs(cases[i-1].recent_speed-speed),"Retained front position AND recent speed do not refine");
            std::cout<<"PASS retained-material front position and speed decrease on 512/1024/2048; no remap, 2D or production acceptance"<<std::endl;return 0;
        }
        require(argc>=4 && argc<=5 && std::string(argv[1])=="--case","Usage: --case N Courant [profile.csv] | --front-gate");
        const auto evidence=run(setup,static_cast<std::size_t>(std::stoul(argv[2])),std::stod(argv[3]),argc==5?argv[4]:"");print(setup,evidence);
        std::cout<<"PASS retained-material case conservation/profile export only; full front gate not invoked"<<std::endl;return 0;
    } catch(const std::exception& failure) {std::cerr<<"FAIL "<<failure.what()<<std::endl;return 1;}
}
