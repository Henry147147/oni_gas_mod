#include "mixed_atmospheres/mac_diffusion.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace ma::experimental {
std::optional<MacDiffusionAdvance> advance_mac_diffusion_normal(const SpeciesRegistry&,const FluidThermodynamics&,
    const MacTransportState&,double,const MacDiffusionSettings&,const GasTransportProfile&,std::size_t);
namespace {
void require(bool condition,const char* why) { if(!condition) throw std::invalid_argument(why); }
using Wide=MacScaled;
Wide difference(Wide a,Wide b) { MacScaledSum s;s.add(a);s.add(b.negated());return s.value(); }
struct Face { std::size_t left,right; };
std::vector<Face> faces(const MacGrid& g) {
    std::vector<Face> result;result.reserve(2*g.width*g.height);
    const auto add=[&](std::size_t a,std::size_t b) { if(a!=b && !g.walls[a] && !g.walls[b])result.push_back({a,b}); };
    for(std::size_t y=0;y<g.height;++y)for(std::size_t x=0;x<g.width;++x) {
        const auto i=x+y*g.width;
        if(x+1<g.width)add(i,i+1);else if(g.periodic_x)add(i,y*g.width);
        if(y+1<g.height)add(i,i+g.width);else if(g.periodic_y)add(i,x);
    }
    return result;
}
struct Cell {
    double gas_mass=0,moles=0,temperature=0,pressure=0;
    std::vector<std::size_t> present;
};
struct Flux { std::vector<Wide> mass;Wide heat; };
struct Rates { std::vector<Flux> flux;double maximum_diffusivity=0; };
void check_trial_entropy(const FluidThermodynamics& eos,const MacTransportState& before,const MacTransportState& after) {
    MacEnergySum change,allowance;
    for(std::size_t i=0;i<before.cells.size();++i)if(!before.grid.walls[i]) {
        const auto& a=before.cells[i].mixture;const auto& b=after.cells[i].mixture;
        const auto ta=eos.evaluate(a,before.grid.volume_m3());const auto tb=eos.evaluate(b,before.grid.volume_m3());
        change.add(tb.entropy_j_per_k);change.add(-ta.entropy_j_per_k);
        allowance.add(128*std::numeric_limits<double>::epsilon()*(std::abs(a.energy)/ta.temperature_k+std::abs(b.energy)/tb.temperature_k));
    }
    MacEnergySum balance;balance.add(change.value());balance.add(allowance.value());
    if(balance.sign()<0)throw std::domain_error("Diffusion trial decreases stored-state entropy");
}
Rates rates(const SpeciesRegistry& registry,const FluidThermodynamics& eos,const MacTransportState& state,
    const std::vector<Face>& links,const std::vector<std::size_t>& gases,const MacDiffusionSettings& settings,
    const GasTransportProfile& profile) {
    const auto ng=gases.size();std::vector<Cell> cells(state.cells.size());
    for(std::size_t i=0;i<cells.size();++i)if(!state.grid.walls[i]) {
        auto& c=cells[i];const auto& mixture=state.cells[i].mixture;
        for(std::size_t s=0;s<registry.size();++s) {
            require(mixture.masses[s]>=0 && std::isfinite(mixture.masses[s]),"Invalid diffusion inventory");
            require(mixture.masses[s]==0 || registry[s].phase==Phase::gas,"Gas diffusion does not admit liquid/solid inventories");
        }
        for(std::size_t k=0;k<ng;++k)if(mixture.masses[gases[k]]>0) {
            c.gas_mass+=mixture.masses[gases[k]];c.moles+=mixture.masses[gases[k]]/registry[gases[k]].molar_mass;c.present.push_back(k);
        }
        require(c.gas_mass>0 && c.moles>0,"Gas diffusion does not admit vacuum");
        const auto reading=eos.evaluate(mixture,state.grid.volume_m3());c.temperature=reading.temperature_k;c.pressure=reading.mechanical_pressure_pa;
    }
    Rates result;result.flux.resize(links.size());
    const double area=state.grid.spacing_m*state.grid.depth_m;
    for(std::size_t f=0;f<links.size();++f) {
        const auto [l,r]=links[f];const auto& a=cells[l];const auto& b=cells[r];auto& out=result.flux[f];out.mass.resize(ng);
        std::vector<std::size_t> active;active.reserve(a.present.size()+b.present.size());
        std::set_union(a.present.begin(),a.present.end(),b.present.begin(),b.present.end(),std::back_inserter(active));
        if(active.size()<2)continue;
        const double temperature=.5*(a.temperature+b.temperature),pressure=.5*(a.pressure+b.pressure);
        const double density=.5*(a.gas_mass+b.gas_mass)/state.grid.volume_m3();
        std::vector<Wide> x(active.size()),y(active.size());MacScaledSum raw_total;
        for(std::size_t k=0;k<active.size();++k) {
            const auto s=gases[active[k]];MacScaledSum xs,ys;
            xs.add(Wide::from_double(state.cells[l].mixture.masses[s]).shared(1,a.moles).shared(1,registry[s].molar_mass).multiplied(.5));
            xs.add(Wide::from_double(state.cells[r].mixture.masses[s]).shared(1,b.moles).shared(1,registry[s].molar_mass).multiplied(.5));
            ys.add(Wide::from_double(state.cells[l].mixture.masses[s]).shared(1,a.gas_mass).multiplied(.5));
            ys.add(Wide::from_double(state.cells[r].mixture.masses[s]).shared(1,b.gas_mass).multiplied(.5));
            x[k]=xs.value();y[k]=ys.value();
        }
        for(std::size_t k=0;k<active.size();++k) {
            const auto s=gases[active[k]];MacScaledSum denominator;
            for(std::size_t j=0;j<active.size();++j)if(j!=k) {
                const double d=profile.diffusivity(registry[s].id,registry[gases[active[j]]].id,temperature,pressure)*settings.multiplier;
                require(std::isfinite(d) && d>0,"Diffusivity multiplier overflows");
                result.maximum_diffusivity=std::max(result.maximum_diffusivity,d);
                denominator.add(x[j].shared(1,d));
            }
            // mole-gradient D'_km = (1-Y_k) / sum_j(X_j/D_kj).
            // Sum other Y directly: subtraction from 1 would erase trace gases.
            MacScaledSum other_y;for(std::size_t j=0;j<active.size();++j)if(j!=k)other_y.add(y[j]);
            const auto d=other_y.value().shared(Wide::from_double(1),denominator.value());
            const auto xl=Wide::from_double(state.cells[l].mixture.masses[s]).shared(1,a.moles).shared(1,registry[s].molar_mass);
            const auto xr=Wide::from_double(state.cells[r].mixture.masses[s]).shared(1,b.moles).shared(1,registry[s].molar_mass);
            const double mean_molar_mass=.5*(a.gas_mass/a.moles+b.gas_mass/b.moles);
            out.mass[active[k]]=difference(xl,xr).multiplied(density*registry[s].molar_mass/mean_molar_mass*area/state.grid.spacing_m).shared(d,Wide::from_double(1));
            raw_total.add(out.mass[active[k]]);
        }
        MacScaledSum corrected;std::size_t pivot=0;
        for(std::size_t k=1;k<active.size();++k)if(y[k].project().value>y[pivot].project().value)pivot=k;
        for(std::size_t k=0;k<active.size();++k)if(k!=pivot) {
            auto& value=out.mass[active[k]];value=difference(value,raw_total.value().shared(y[k],Wide::from_double(1)));corrected.add(value);
        }
        // Algebraic zero-net-mass constraint, not an inventory repair.
        out.mass[active[pivot]]=corrected.value().negated();MacScaledSum heat;
        for(const auto k:active)heat.add(out.mass[k].multiplied(registry[gases[k]].enthalpy(temperature)));
        out.heat=heat.value();
    }
    return result;
}
MacTransportState apply(const SpeciesRegistry& registry,const FluidThermodynamics& eos,const MacTransportState& source,
    const std::vector<Face>& links,const std::vector<std::size_t>& gases,const Rates& first,const Rates* second,
    double dt,MacDiffusionReport& report) {
    auto target=source;const auto n=source.cells.size(),ng=gases.size();
    std::vector<MacScaledSum> delta(n*ng),heat(n);
    for(std::size_t f=0;f<links.size();++f) {
        const auto [l,r]=links[f];
        for(std::size_t k=0;k<ng;++k) {
            MacScaledSum value;value.add(first.flux[f].mass[k]);if(second)value.add(second->flux[f].mass[k]);
            const auto amount=value.value().multiplied(second ? .5*dt:dt);
            delta[l*ng+k].add(amount.negated());delta[r*ng+k].add(amount);
            if(second)report.exchanged_mass_kg+=.5*amount.magnitude().project().value;
        }
        MacScaledSum value;value.add(first.flux[f].heat);if(second)value.add(second->flux[f].heat);
        const auto energy=value.value().multiplied(second ? .5*dt:dt);heat[l].add(energy.negated());heat[r].add(energy);
    }
    for(std::size_t i=0;i<n;++i)if(!source.grid.walls[i]) {
        for(std::size_t k=0;k<ng;++k) {
            const auto s=gases[k];MacScaledSum sum;sum.add(source.cells[i].mixture.masses[s]);sum.add(delta[i*ng+k].value());
            if(sum.sign()<0)throw std::domain_error("Diffusion step depletes a constituent");
            const auto p=sum.value().project();
            if(sum.sign()>0 && p.value==0)throw std::domain_error("Positive diffused mass cannot be stored");
            target.cells[i].mixture.masses[s]=p.value;
            if(second && !delta[i*ng+k].value().is_zero() && p.value==source.cells[i].mixture.masses[s])++report.unresolved_updates;
        }
        MacScaledSum sum;sum.add(source.cells[i].mixture.energy);sum.add(heat[i].value());
        target.cells[i].mixture.energy=sum.value().require_representable();
        if(second && !heat[i].value().is_zero() && target.cells[i].mixture.energy==source.cells[i].mixture.energy)++report.unresolved_updates;
        (void)eos.evaluate(target.cells[i].mixture,source.grid.volume_m3());
    }
    (void)registry;return target;
}
}
void MacDiffusionSettings::validate() const {
    require(mode==MacDiffusionMode::disabled || mode==MacDiffusionMode::physical || mode==MacDiffusionMode::accelerated,"Unknown diffusion mode");
    require(std::isfinite(multiplier) && multiplier>=1 && (mode==MacDiffusionMode::accelerated || multiplier==1),"Invalid diffusion multiplier");
}
GasTransportProfile::GasTransportProfile(std::string id,std::vector<GasDiffusionPair> pairs):identity_(std::move(id)),pairs_(std::move(pairs)) {
    require(!identity_.empty() && identity_.size()<=256,"Invalid transport profile identity");
    for(std::size_t i=0;i<pairs_.size();++i) {
        const auto& p=pairs_[i];require(!p.first.empty() && !p.second.empty() && p.first!=p.second && !p.source.empty(),"Invalid gas diffusion pair");
        for(double v:{p.a,p.b,p.c,p.minimum_temperature_k,p.maximum_temperature_k,p.minimum_pressure_pa,p.maximum_pressure_pa})require(std::isfinite(v),"Nonfinite diffusion property");
        require(p.minimum_temperature_k>0 && p.maximum_temperature_k>=p.minimum_temperature_k && p.minimum_pressure_pa>0 && p.maximum_pressure_pa>=p.minimum_pressure_pa,"Invalid diffusion property range");
        for(std::size_t j=0;j<i;++j)require(!((p.first==pairs_[j].first && p.second==pairs_[j].second)||(p.first==pairs_[j].second && p.second==pairs_[j].first)),"Duplicate gas diffusion pair");
    }
}
double GasTransportProfile::diffusivity(std::string_view a,std::string_view b,double t,double p) const {
    for(const auto& pair:pairs_)if((a==pair.first && b==pair.second)||(b==pair.first && a==pair.second)) {
        if(!(t>=pair.minimum_temperature_k && t<=pair.maximum_temperature_k && p>=pair.minimum_pressure_pa && p<=pair.maximum_pressure_pa))
            throw std::domain_error("Gas diffusion is outside its transport profile validity range");
        const double result=1e-4*std::exp(pair.a+pair.b/t+pair.c*std::log(t))*101325/p;
        require(std::isfinite(result) && result>0,"Invalid evaluated binary diffusivity");return result;
    }
    throw std::invalid_argument("Gas pair is absent from transport profile: "+std::string(a)+"/"+std::string(b));
}
std::shared_ptr<const GasTransportProfile> GasTransportProfile::standard() {
    static const auto value=[] {
        std::vector<GasDiffusionPair> pairs;
        const std::string nist="https://doi.org/10.6028/NIST.TN.2279 Table 1a";
        pairs.push_back({"nitrogen","helium",-10.266,27.449,1.724,298,723,10000,200000,false,nist+" (includes tabulated 298 K reference)"});
        pairs.push_back({"nitrogen","carbon_dioxide",-7.317,-139.909,1.103,253,473,10000,200000,false,nist});
        // Fuller-Schettler-Giddings estimates; SI conversion from cm^2/s.
        // Volumes: COMSOL Liquid & Gas Properties, Fuller table (O2=16.3).
        // a = ln(0.001 sqrt(1/W_i+1/W_j) / (cbrt(V_i)+cbrt(V_j))^2), with W =
        // N2 28.0134, O2 31.9988, He 4.002602, CO2 44.0095 g/mol and V = 18.5,
        // 16.3, 2.67, 26.9. The values are stored as exact binary64 literals
        // (correctly rounded cbrt/log, IEEE +,*,/,sqrt) instead of being
        // evaluated with the host libm: checkpoints compare every coefficient
        // bit for bit, and glibc/MSVC cbrt and log are not correctly rounded,
        // so a runtime evaluation would make saves platform/libm specific.
        const std::string fuller="Fuller-Schettler-Giddings correlation; https://doc.comsol.com/6.4/doc/com.comsol.help.lgp/lgp_ug_thermo.5.27.html";
        pairs.push_back({"nitrogen","oxygen",-0x1.71949acae4936p+3,0,1.75,253,473,10000,200000,true,fuller});
        pairs.push_back({"oxygen","helium",-0x1.48d438f95b97dp+3,0,1.75,253,473,10000,200000,true,fuller});
        pairs.push_back({"oxygen","carbon_dioxide",-0x1.793aa1c2c98c3p+3,0,1.75,253,473,10000,200000,true,fuller});
        pairs.push_back({"helium","carbon_dioxide",-0x1.506e1b2bad992p+3,0,1.75,253,473,10000,200000,true,fuller});
        return std::make_shared<const GasTransportProfile>("ma-gas-transport-1",std::move(pairs));
    }();return value;
}
bool mac_diffusion_stationary(const SpeciesRegistry& registry,const MacTransportState& state) {
    const auto& g=state.grid;
    const auto same=[&](std::size_t l,std::size_t r) {
        if(l==r || g.walls[l] || g.walls[r])return true;
        const auto& a=state.cells[l].mixture;const auto& b=state.cells[r].mixture;
        // Sufficient exact certificate. No tolerance may erase a small gradient.
        for(std::size_t s=0;s<registry.size();++s)if(a.masses[s]!=b.masses[s])return false;
        return true;
    };
    for(std::size_t y=0;y<g.height;++y)for(std::size_t x=0;x<g.width;++x) {
        const auto i=x+y*g.width;
        if(x+1<g.width) { if(!same(i,i+1))return false; }
        else if(g.periodic_x && !same(i,y*g.width))return false;
        if(y+1<g.height) { if(!same(i,i+g.width))return false; }
        else if(g.periodic_y && !same(i,x))return false;
    }
    return true;
}
MacDiffusionAdvance advance_mac_diffusion(const SpeciesRegistry& registry,const FluidThermodynamics& eos,
    const MacTransportState& source,double seconds,const MacDiffusionSettings& settings,const GasTransportProfile& profile,std::size_t budget) {
    const auto started=std::chrono::steady_clock::now();require_mac_energy_environment();settings.validate();
    require(std::isfinite(seconds) && seconds>0 && seconds<=10 && budget>0,"Invalid diffusion advance interval/budget");
    require(source.grid.width>0 && source.grid.height>0 && source.grid.width<=std::numeric_limits<std::size_t>::max()/source.grid.height &&
        source.cells.size()==source.grid.width*source.grid.height && source.cells.size()<=std::numeric_limits<std::size_t>::max()/2 &&
        source.grid.walls.size()==source.cells.size() && std::isfinite(source.grid.spacing_m) && std::isfinite(source.grid.depth_m) &&
        std::isfinite(source.grid.volume_m3()) && source.grid.volume_m3()>0 && source.grid.spacing_m>0 && source.grid.depth_m>0,"Invalid diffusion geometry");
    for(std::size_t i=0;i<source.cells.size();++i)require(source.grid.walls[i]<=1 &&
        source.cells[i].mixture.masses.size()==registry.size(),"Invalid diffusion cell/species coverage");
    if(settings.mode==MacDiffusionMode::disabled)return {source,{}};
    if(auto normal=advance_mac_diffusion_normal(registry,eos,source,seconds,settings,profile,budget)) {
        normal->report.elapsed_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return std::move(*normal);
    }
    MacDiffusionAdvance result{source,{}};
    const auto links=faces(source.grid);std::vector<std::size_t> gases;
    for(std::size_t s=0;s<registry.size();++s)if(registry[s].phase==Phase::gas)gases.push_back(s);
    double elapsed=0,next=seconds;std::size_t attempts=0;
    while(elapsed<seconds) {
        const auto first=rates(registry,eos,result.state,links,gases,settings,profile);
        const double stable=first.maximum_diffusivity>0 ? .2*source.grid.spacing_m*source.grid.spacing_m/first.maximum_diffusivity : seconds;
        double dt=std::min({next,seconds-elapsed,stable});
        while(true) {
            if(++attempts>budget)throw std::runtime_error("Diffusion exhausted its attempt budget");
            if(!(dt>0) || elapsed+dt<=elapsed)throw std::runtime_error("Diffusion timestep cannot advance its clock");
            try {
                MacDiffusionReport attempt;
                const auto predictor=apply(registry,eos,result.state,links,gases,first,nullptr,dt,attempt);
                const auto second=rates(registry,eos,predictor,links,gases,settings,profile);
                if(second.maximum_diffusivity*dt/(source.grid.spacing_m*source.grid.spacing_m)>.2)throw std::domain_error("Diffusion predictor violates stability bound");
                auto corrected=apply(registry,eos,result.state,links,gases,first,&second,dt,attempt);
                check_trial_entropy(eos,result.state,corrected);
                result.state=std::move(corrected);result.report.exchanged_mass_kg+=attempt.exchanged_mass_kg;
                result.report.unresolved_updates+=attempt.unresolved_updates;++result.report.substeps;result.report.faces+=links.size();break;
            }catch(const std::domain_error&) { dt*=.5;++result.report.rejected_attempts; }
            catch(const ReferenceError& error) {
                if(error.code()!=ReferenceErrorCode::temperature_outside_model && error.code()!=ReferenceErrorCode::insufficient_thermal_energy)throw;
                dt*=.5;++result.report.rejected_attempts;
            }
        }
        elapsed+=dt;next=std::min(2*dt,seconds-elapsed);
    }
    std::vector<MacEnergyAccount> balances(registry.size());MacEnergyAccount energy;
    MacEnergySum entropy_change,entropy_bound;
    for(std::size_t i=0;i<source.cells.size();++i)if(!source.grid.walls[i]) {
        MacEnergyAccount total;
        for(std::size_t s=0;s<registry.size();++s) {
            balances[s].add(result.state.cells[i].mixture.masses[s]);balances[s].add(-source.cells[i].mixture.masses[s]);
            total.add(result.state.cells[i].mixture.masses[s]);total.add(-source.cells[i].mixture.masses[s]);
        }
        if(!total.within_bound())throw std::runtime_error("Diffusion changed a cell's total mass");
        energy.add(result.state.cells[i].mixture.energy);energy.add(-source.cells[i].mixture.energy);
        const auto a=eos.evaluate(source.cells[i].mixture,source.grid.volume_m3());
        const auto b=eos.evaluate(result.state.cells[i].mixture,source.grid.volume_m3());
        entropy_change.add(b.entropy_j_per_k);entropy_change.add(-a.entropy_j_per_k);
        entropy_bound.add(128*std::numeric_limits<double>::epsilon()*(std::abs(source.cells[i].mixture.energy)/a.temperature_k+
            std::abs(result.state.cells[i].mixture.energy)/b.temperature_k));
    }
    for(const auto& balance:balances) {
        if(!balance.within_bound())throw std::runtime_error("Diffusion constituent conservation failed");
        result.report.species_mass_residual.push_back(balance.residual().project().value);
        result.report.species_mass_bound.push_back(balance.bound().project().value);
    }
    if(!energy.within_bound())throw std::runtime_error("Diffusion energy conservation failed");
    result.report.energy_residual=energy.residual();result.report.energy_bound=energy.bound();
    result.report.entropy_change_j_per_k=entropy_change.value().project().value;
    result.report.entropy_roundoff_j_per_k=entropy_bound.value().project().value;
    if(result.report.entropy_change_j_per_k < -result.report.entropy_roundoff_j_per_k)
        throw std::domain_error("Gas diffusion decreases stored-state entropy outside its roundoff allowance");
    result.report.elapsed_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;
}
}
