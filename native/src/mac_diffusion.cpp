#include "mixed_atmospheres/mac_diffusion.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace ma::experimental {
std::optional<MacDiffusionAdvance> advance_mac_diffusion_normal(const SpeciesRegistry&,const FluidThermodynamics&,
    const MacTransportState&,double,const MacDiffusionSettings&,const GasTransportProfile&,std::size_t);
namespace {
void require(bool condition,const char* why) { if(!condition) throw std::invalid_argument(why); }
using Wide=MacScaled;
Wide difference(Wide a,Wide b) { MacScaledSum s;s.add(a);s.add(b.negated());return s.value(); }
int compare(const Wide& a,const Wide& b) { MacScaledSum s;s.add(a);s.add(b.negated());return s.sign(); }
// Natural logarithm of a positive range value without projecting it.
double logarithm(const Wide& value) {
    int power=0;const double fraction=std::frexp(std::abs(value.mantissa()),&power);
    return std::log(fraction)+(static_cast<double>(power)+static_cast<double>(value.exponent().to_int64_exact()))*std::numbers::ln2;
}
// Logarithmic mean (a-b)/ln(a/b) of positive a,b. Its product with the exact
// force ln(a/b) is the difference a-b, so one-sided limits stay finite.
Wide log_mean(const Wide& a,const Wide& b) {
    const int order=compare(a,b);if(order==0)return a;
    const Wide& high=order>0 ? a : b;const Wide& low=order>0 ? b : a;const auto delta=difference(high,low);
    if(compare(delta,low)<=0) {
        // t=(high-low)/low in (0,1]; log1p keeps the near-equal force accurate.
        const double t=delta.shared(Wide::from_double(1),low).project().value;
        return t>1e-300 ? low.multiplied(t/std::log1p(t)) : low;
    }
    const double ratio_log=logarithm(high)-logarithm(low);
    require(std::isfinite(ratio_log) && ratio_log>0,"Unrepresentable diffusion log-mean");
    return delta.multiplied(1/ratio_log);
}
// T_f(1/T_R-1/T_L)+ln(T_R/T_L) for T_f the arithmetic face temperature:
// the exact enthalpy-flux correction of the discrete chemical-potential force
// (third order in the temperature difference).
double thermal_force(double left,double right) {
    if(left==right)return 0;
    const double u=(right-left)/(right+left);
    return 2*std::atanh(u)-2*u/((1-u)*(1+u));
}
struct Face { std::size_t left,right; };
// Faces touching an exact empty cell carry no diffusive flux. Diffusion moves
// zero net mass across every face, so emptiness is invariant during an advance.
std::vector<Face> faces(const MacTransportState& state) {
    const auto& g=state.grid;std::vector<Face> result;result.reserve(2*g.width*g.height);
    const auto add=[&](std::size_t a,std::size_t b) {
        if(a!=b && !g.walls[a] && !g.walls[b] && state.cells[a].mixture.mass()>0 && state.cells[b].mixture.mass()>0)result.push_back({a,b});
    };
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
struct Rates { std::vector<Flux> flux;double maximum_diffusivity=0;MacDiffusionReport range; };
void check_trial_entropy(const FluidThermodynamics& eos,const MacTransportState& before,const MacTransportState& after) {
    MacEnergySum change,allowance;
    for(std::size_t i=0;i<before.cells.size();++i)if(!before.grid.walls[i] && before.cells[i].mixture.mass()>0) {
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
    const auto ng=gases.size();std::vector<Cell> cells(state.cells.size());const double volume=state.grid.volume_m3();
    for(std::size_t i=0;i<cells.size();++i)if(!state.grid.walls[i]) {
        auto& c=cells[i];const auto& mixture=state.cells[i].mixture;
        for(std::size_t s=0;s<registry.size();++s) {
            require(mixture.masses[s]>=0 && std::isfinite(mixture.masses[s]),"Invalid diffusion inventory");
            require(mixture.masses[s]==0 || registry[s].phase==Phase::gas,"Gas diffusion does not admit liquid/solid inventories");
        }
        for(std::size_t k=0;k<ng;++k)if(mixture.masses[gases[k]]>0) {
            c.gas_mass+=mixture.masses[gases[k]];c.moles+=mixture.masses[gases[k]]/registry[gases[k]].molar_mass;c.present.push_back(k);
        }
        if(c.present.empty()) { require(mixture.energy==0,"Empty diffusion cell holds energy");continue; }
        require(c.gas_mass>0 && c.moles>0,"Unrepresentable diffusion cell inventory");
        const auto reading=eos.evaluate(mixture,volume);c.temperature=reading.temperature_k;c.pressure=reading.mechanical_pressure_pa;
    }
    Rates result;result.flux.resize(links.size());
    const double area=state.grid.spacing_m*state.grid.depth_m,geometry=area/state.grid.spacing_m;
    std::vector<double> binary;
    for(std::size_t f=0;f<links.size();++f) {
        const auto [l,r]=links[f];const auto& a=cells[l];const auto& b=cells[r];auto& out=result.flux[f];out.mass.resize(ng);
        std::vector<std::size_t> active;active.reserve(a.present.size()+b.present.size());
        std::set_union(a.present.begin(),a.present.end(),b.present.begin(),b.present.end(),std::back_inserter(active));
        if(active.size()<2)continue;
        const auto na=active.size();
        const double temperature=.5*(a.temperature+b.temperature),pressure=.5*(a.pressure+b.pressure);
        std::vector<Wide> x(na),y(na);
        for(std::size_t k=0;k<na;++k) {
            const auto s=gases[active[k]];MacScaledSum xs,ys;
            xs.add(Wide::from_double(state.cells[l].mixture.masses[s]).shared(1,a.moles).shared(1,registry[s].molar_mass).multiplied(.5));
            xs.add(Wide::from_double(state.cells[r].mixture.masses[s]).shared(1,b.moles).shared(1,registry[s].molar_mass).multiplied(.5));
            ys.add(Wide::from_double(state.cells[l].mixture.masses[s]).shared(1,a.gas_mass).multiplied(.5));
            ys.add(Wide::from_double(state.cells[r].mixture.masses[s]).shared(1,b.gas_mass).multiplied(.5));
            x[k]=xs.value();y[k]=ys.value();
        }
        // Symmetric binary table: each unordered pair is evaluated once per face.
        binary.assign(na*na,0);
        for(std::size_t k=0;k<na;++k)for(std::size_t j=k+1;j<na;++j) {
            const auto& first=registry[gases[active[k]]];const auto& second=registry[gases[active[j]]];
            const auto value=evaluate_gas_diffusivity(profile.pair(first.id,second.id),temperature,pressure,
                first.molar_mass,second.molar_mass,state.grid.spacing_m,settings.multiplier);
            require(std::isfinite(value.effective_m2_per_s) && value.effective_m2_per_s>0,"Diffusivity multiplier overflows");
            if(!value.validated)result.range.record_out_of_range(temperature,pressure);
            binary[k*na+j]=binary[j*na+k]=value.effective_m2_per_s;
            result.maximum_diffusivity=std::max(result.maximum_diffusivity,value.effective_m2_per_s);
        }
        // Mixture-averaged D'_k = (1-Y_k) / sum_j(X_j/D_kj). Sum other Y
        // directly: subtraction from 1 would erase trace gases.
        std::vector<Wide> mobility(na);
        for(std::size_t k=0;k<na;++k) {
            MacScaledSum denominator,other_y;
            for(std::size_t j=0;j<na;++j)if(j!=k) { denominator.add(x[j].shared(1,binary[k*na+j]));other_y.add(y[j]); }
            const auto s=gases[active[k]];
            // Mass flux per unit partial-pressure difference: W_k D'_k (A/dx) / (R T_f).
            mobility[k]=other_y.value().shared(Wide::from_double(1),denominator.value()).multiplied(registry[s].molar_mass*geometry/(gas_constant*temperature));
        }
        // Partial pressures, their log-means and the exact discrete force.
        // F_k = Lambda_k * (ln(p_k^L/p_k^R) + G_k) = (p_k^L - p_k^R) + Lambda_k G_k.
        const double thermal=thermal_force(a.temperature,b.temperature);
        std::vector<Wide> force(na),lambda(na);std::vector<bool> both(na),from_left(na);
        MacScaledSum weight_total,force_total;bool any_both=false;
        for(std::size_t k=0;k<na;++k) {
            const auto s=gases[active[k]];
            const auto left=Wide::from_double(state.cells[l].mixture.masses[s]).shared(1,registry[s].molar_mass).multiplied(gas_constant*a.temperature/volume);
            const auto right=Wide::from_double(state.cells[r].mixture.masses[s]).shared(1,registry[s].molar_mass).multiplied(gas_constant*b.temperature/volume);
            both[k]=!left.is_zero() && !right.is_zero();from_left[k]=!left.is_zero();
            MacScaledSum value;value.add(left);value.add(right.negated());
            if(both[k]) {
                lambda[k]=log_mean(left,right);any_both=true;
                if(thermal!=0)value.add(lambda[k].multiplied(registry[s].molar_mass*registry[s].cp/gas_constant*thermal));
                weight_total.add(lambda[k].multiplied(registry[s].molar_mass));force_total.add(value.value());
            }
            force[k]=value.value();
        }
        std::vector<Wide> flux(na);std::size_t pivot=0;
        if(any_both) {
            // J = P_w A P_w^T psi, P_w = I - w 1^T, w_k = W_k Lambda_k / sum(W_j Lambda_j)
            // over two-sided species (-> Y_k in the continuum). Symmetric
            // positive semidefinite in the exact force psi, zero net mass. Its
            // continuum limit is the corrected mixture-averaged flux driven by
            // d_k = grad x_k + (x_k - w_k) grad ln p. One-sided species have
            // zero weight and log-mean, leaving their finite downhill Fick flux.
            const auto weights=weight_total.value();MacScaledSum raw_total;
            for(std::size_t k=0;k<na;++k) {
                auto projected=force[k];
                if(both[k])projected=difference(force[k],force_total.value().shared(lambda[k].multiplied(registry[gases[active[k]]].molar_mass),weights));
                flux[k]=projected.shared(mobility[k],Wide::from_double(1));raw_total.add(flux[k]);
            }
            for(std::size_t k=0;k<na;++k)if(both[k]) {
                const auto w=lambda[k].multiplied(registry[gases[active[k]]].molar_mass);
                if(!both[pivot] || compare(w,lambda[pivot].multiplied(registry[gases[active[pivot]]].molar_mass))>0)pivot=k;
                flux[k]=difference(flux[k],raw_total.value().shared(w,weights));
            }
        } else {
            // Disjoint inventories: every species is absent on one side and its
            // exact force is infinite. Downhill Fick fluxes are balanced to zero
            // net mass with the corrected group totals sum_L J = Y_R S_L + Y_L S_R
            // (Y_L, Y_R face mass fractions of each side's species), scaling each
            // group so that no constituent leaves a cell where it is absent.
            MacScaledSum left_sum,right_sum,left_y,right_y;
            for(std::size_t k=0;k<na;++k) {
                flux[k]=force[k].shared(mobility[k],Wide::from_double(1));
                if(from_left[k]) { left_sum.add(flux[k]);left_y.add(y[k]); }
                else { right_sum.add(flux[k].negated());right_y.add(y[k]); }
            }
            MacScaledSum transfer;transfer.add(left_sum.value().shared(right_y.value(),Wide::from_double(1)));
            transfer.add(right_sum.value().shared(left_y.value(),Wide::from_double(1)));
            require(left_sum.sign()>0 && right_sum.sign()>0,"Disjoint diffusion face lacks a downhill flux");
            for(std::size_t k=0;k<na;++k) {
                flux[k]=flux[k].shared(transfer.value(),from_left[k] ? left_sum.value() : right_sum.value());
                if(compare(flux[k].magnitude(),flux[pivot].magnitude())>0)pivot=k;
            }
        }
        // Algebraic zero-net-mass constraint, not an inventory repair.
        MacScaledSum corrected;for(std::size_t k=0;k<na;++k)if(k!=pivot)corrected.add(flux[k]);
        flux[pivot]=corrected.value().negated();MacScaledSum heat;
        for(std::size_t k=0;k<na;++k) { out.mass[active[k]]=flux[k];heat.add(flux[k].multiplied(registry[gases[active[k]]].enthalpy(temperature))); }
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
void MacDiffusionReport::record_out_of_range(double temperature,double pressure) {
    if(out_of_range_evaluations==0) {
        out_of_range_minimum_temperature_k=out_of_range_maximum_temperature_k=temperature;
        out_of_range_minimum_pressure_pa=out_of_range_maximum_pressure_pa=pressure;
    } else {
        out_of_range_minimum_temperature_k=std::min(out_of_range_minimum_temperature_k,temperature);
        out_of_range_maximum_temperature_k=std::max(out_of_range_maximum_temperature_k,temperature);
        out_of_range_minimum_pressure_pa=std::min(out_of_range_minimum_pressure_pa,pressure);
        out_of_range_maximum_pressure_pa=std::max(out_of_range_maximum_pressure_pa,pressure);
    }
    ++out_of_range_evaluations;
}
void MacDiffusionReport::merge_out_of_range(const MacDiffusionReport& other) {
    if(other.out_of_range_evaluations==0)return;
    const auto count=out_of_range_evaluations;
    record_out_of_range(other.out_of_range_minimum_temperature_k,other.out_of_range_minimum_pressure_pa);
    record_out_of_range(other.out_of_range_maximum_temperature_k,other.out_of_range_maximum_pressure_pa);
    out_of_range_evaluations=count+other.out_of_range_evaluations;
}
void MacDiffusionSettings::validate() const {
    require(mode==MacDiffusionMode::disabled || mode==MacDiffusionMode::physical || mode==MacDiffusionMode::accelerated,"Unknown diffusion mode");
    require(std::isfinite(multiplier) && multiplier>=1 && (mode==MacDiffusionMode::accelerated || multiplier==1),"Invalid diffusion multiplier");
}
GasDiffusivity evaluate_gas_diffusivity(const GasDiffusionPair& pair,double t,double p,double first_mass,double second_mass,double spacing,double multiplier) {
    require(std::isfinite(t) && t>0 && std::isfinite(p) && p>0 && std::isfinite(first_mass) && first_mass>0 &&
        std::isfinite(second_mass) && second_mass>0 && std::isfinite(spacing) && spacing>0 && std::isfinite(multiplier) && multiplier>=1,
        "Invalid gas diffusivity state");
    GasDiffusivity result;
    result.validated=t>=pair.minimum_temperature_k && t<=pair.maximum_temperature_k && p>=pair.minimum_pressure_pa && p<=pair.maximum_pressure_pa;
    // The correlation's own form, extrapolated outside its validated range.
    const double atmospheric=1e-4*std::exp(pair.a+pair.b/t+pair.c*std::log(t))*101325;
    require(std::isfinite(atmospheric) && atmospheric>0,"Invalid evaluated binary diffusivity");
    // Free-molecular limit across one cell, with the pair's mean relative speed.
    const double reduced=first_mass*second_mass/(first_mass+second_mass);
    result.knudsen_m2_per_s=spacing/3*std::sqrt(8*gas_constant*t/(std::numbers::pi*reduced));
    // Resistances, so a vanishing pressure approaches the Knudsen limit
    // without forming an overflowing molecular coefficient.
    const double molecular_resistance=p/(atmospheric*multiplier);
    result.molecular_m2_per_s=1/molecular_resistance;
    result.effective_m2_per_s=1/(molecular_resistance+1/result.knudsen_m2_per_s);
    require(std::isfinite(result.knudsen_m2_per_s) && result.knudsen_m2_per_s>0 && std::isfinite(result.effective_m2_per_s) &&
        result.effective_m2_per_s>0,"Invalid evaluated binary diffusivity");
    return result;
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
const GasDiffusionPair& GasTransportProfile::pair(std::string_view a,std::string_view b) const {
    for(const auto& pair:pairs_)if((a==pair.first && b==pair.second)||(b==pair.first && a==pair.second))return pair;
    throw std::invalid_argument("Gas pair is absent from transport profile: "+std::string(a)+"/"+std::string(b));
}
double GasTransportProfile::diffusivity(std::string_view a,std::string_view b,double t,double p) const {
    const auto& pair=this->pair(a,b);
    require(std::isfinite(t) && t>0 && std::isfinite(p) && p>0,"Invalid gas diffusivity state");
    const double result=1e-4*std::exp(pair.a+pair.b/t+pair.c*std::log(t))*101325/p;
    require(std::isfinite(result) && result>0,"Invalid evaluated binary diffusivity");return result;
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
        // Equal inventories and U give equal T, p and partial pressures; a face
        // touching an empty cell carries no flux. Emptiness is examined only
        // when inventories differ, keeping the uniform certificate cheap.
        for(std::size_t s=0;s<registry.size();++s)if(a.masses[s]!=b.masses[s])return a.mass()==0 || b.mass()==0;
        return a.energy==b.energy;
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
    const auto links=faces(source);std::vector<std::size_t> gases;
    for(std::size_t s=0;s<registry.size();++s)if(registry[s].phase==Phase::gas)gases.push_back(s);
    double elapsed=0,next=seconds;std::size_t attempts=0;
    while(elapsed<seconds) {
        const auto first=rates(registry,eos,result.state,links,gases,settings,profile);
        // Select 10% inside the 0.2 acceptance bound: the predictor's coefficients
        // can be marginally larger (e.g. near the Knudsen limit), and a trial
        // exactly at the bound would then always be rejected once.
        const double stable=first.maximum_diffusivity>0 ? .18*source.grid.spacing_m*source.grid.spacing_m/first.maximum_diffusivity : seconds;
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
                result.report.merge_out_of_range(first.range);result.report.merge_out_of_range(second.range);
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
        // Empty cells have no faces and no defined temperature; they are unchanged.
        if(source.cells[i].mixture.mass()==0) {
            if(result.state.cells[i].mixture.mass()!=0 || result.state.cells[i].mixture.energy!=0)throw std::runtime_error("Diffusion changed an empty cell");
            continue;
        }
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
