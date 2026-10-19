#include "mixed_atmospheres/mac_diffusion.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <optional>
#include <thread>
#include <type_traits>

namespace ma::experimental {
namespace {
constexpr std::size_t absent=std::numeric_limits<std::size_t>::max();
using Row=std::array<double,4>;
struct Sum {
    double hi=0,lo=0,scale=0;
    void add(double v) { const double s=hi+v,z=s-hi,e=(hi-(s-z))+(v-z),t=lo+e,h=s+t,q=h-s;lo=(s-(h-q))+(t-q);hi=h;scale+=std::abs(v); }
    double value() const { return hi+lo; }
    double bound() const { return std::ldexp(scale,-45); }
};
template<class Function> void parallel(std::size_t count,Function f) {
    const std::size_t workers=std::max(std::size_t(1),std::min({std::size_t(8),std::size_t(std::thread::hardware_concurrency()),count/8192}));
    // jthread also joins already-created workers if launching a later one fails.
    std::vector<std::exception_ptr> errors(workers);std::vector<std::jthread> threads;
    const auto run=[&](std::size_t worker) { try { require_mac_energy_environment();for(std::size_t i=count*worker/workers;i<count*(worker+1)/workers;++i) {
        if constexpr(std::is_invocable_v<Function,std::size_t,std::size_t>)f(i,worker);else f(i);
    } }catch(...) { errors[worker]=std::current_exception(); } };
    for(std::size_t worker=1;worker<workers;++worker)threads.emplace_back(run,worker);
    run(0);for(auto& thread:threads)thread.join();for(const auto& error:errors)if(error)std::rethrow_exception(error);
}
struct Link { std::size_t l,r; };
struct Adjacent { std::array<std::size_t,4> face{absent,absent,absent,absent};std::array<double,4> sign{};std::size_t count=0; };
struct Reading { Row x{},y{},partial{};double mass=0,moles=0,temperature=0,pressure=0; };
// range: pair evaluations outside their validated range at this face state.
struct Flux { Row mass{};double heat=0,maximum_d=0,temperature=0,pressure=0;std::uint32_t range=0; };
struct Workspace {
    std::vector<Row> masses,predictor,next;
    std::vector<double> energy,predictor_energy,next_energy;
    std::vector<Reading> reading;
    std::vector<Flux> first,second;
    std::vector<Link> links;
    std::vector<Adjacent> adjacent;
};
// Scratch belongs to the invoking solver thread. It is not authoritative and
// retains capacity only; no coefficient/state cache survives an advance.
thread_local Workspace scratch;
}
// Common <=4-species, ordinary-range path. This is a storage/arithmetic
// specialization of the same equations. It never discards a constituent:
// arbitrary species counts/extreme operands use the general range-safe path.
std::optional<MacDiffusionAdvance> advance_mac_diffusion_normal(const SpeciesRegistry& registry,const FluidThermodynamics& eos,
    const MacTransportState& source,double seconds,const MacDiffusionSettings& settings,const GasTransportProfile& profile,std::size_t budget,MacDiffusionRounding rounding_policy,std::size_t& consumed) {
    consumed=0;
    const auto started=std::chrono::steady_clock::now();
    std::vector<std::size_t> gases;
    for(std::size_t s=0;s<registry.size();++s) {
        bool present=false;
        for(std::size_t i=0;i<source.cells.size();++i)if(!source.grid.walls[i] && source.cells[i].mixture.masses[s]!=0) {
            const double m=source.cells[i].mixture.masses[s];
            if(registry[s].phase!=Phase::gas || !std::isfinite(m) || m<1e-150 || m>1e150)return {};
            present=true;
        }
        if(present)gases.push_back(s);
    }
    if(gases.empty() || gases.size()>4)return {};
    const auto n=source.cells.size(),ng=gases.size();const auto& g=source.grid;auto& w=scratch;
    Row molecular{},cv{},offset{},minimum_t{},maximum_t{},formation{},cp{};
    std::array<std::array<const GasDiffusionPair*,4>,4> properties{};
    for(std::size_t k=0;k<ng;++k) {
        const auto& s=registry[gases[k]];const auto& materials=eos.reference().materials();
        const auto m=std::find_if(materials.begin(),materials.end(),[&](const auto& v){return v.id==s.id;});
        if(m==materials.end() || m->phase!=ReferencePhase::ideal_gas || m->molar_mass_kg_per_mol!=s.molar_mass ||
            m->cv_j_per_kg_k!=s.cv() || m->energy_offset_j_per_kg!=s.internal_energy(0))return {};
        molecular[k]=s.molar_mass;cv[k]=s.cv();offset[k]=s.internal_energy(0);cp[k]=s.cp;formation[k]=s.formation_enthalpy/s.molar_mass;
        minimum_t[k]=m->minimum_temperature_k;maximum_t[k]=m->maximum_temperature_k;
        for(std::size_t j=0;j<k;++j) {
            for(const auto& p:profile.pairs())if((p.first==s.id && p.second==registry[gases[j]].id)||(p.second==s.id && p.first==registry[gases[j]].id))properties[k][j]=properties[j][k]=&p;
            if(!properties[k][j])return {};
        }
    }
    w.masses.resize(n);w.predictor.resize(n);w.next.resize(n);w.energy.resize(n);w.predictor_energy.resize(n);w.next_energy.resize(n);w.reading.resize(n);
    w.links.clear();w.links.reserve(2*n);w.adjacent.assign(n,{});
    // Faces touching an exact empty cell carry no diffusive flux; zero net
    // face mass keeps every cell's emptiness fixed during the advance.
    const auto add=[&](std::size_t l,std::size_t r) {
        if(l==r || g.walls[l] || g.walls[r] || !(source.cells[l].mixture.mass()>0) || !(source.cells[r].mixture.mass()>0))return;
        const auto f=w.links.size();w.links.push_back({l,r});
        auto& a=w.adjacent[l];a.face[a.count]=f;a.sign[a.count++]=-1;
        auto& b=w.adjacent[r];b.face[b.count]=f;b.sign[b.count++]=1;
    };
    for(std::size_t y=0;y<g.height;++y)for(std::size_t x=0;x<g.width;++x) {
        const auto i=x+y*g.width;
        if(x+1<g.width)add(i,i+1);else if(g.periodic_x)add(i,y*g.width);
        if(y+1<g.height)add(i,i+g.width);else if(g.periodic_y)add(i,x);
    }
    w.first.resize(w.links.size());w.second.resize(w.links.size());
    parallel(n,[&](std::size_t i) {
        w.masses[i]={};for(std::size_t k=0;k<ng;++k)w.masses[i][k]=source.cells[i].mixture.masses[gases[k]];
        w.energy[i]=source.cells[i].mixture.energy;
    });
    std::atomic<bool> ordinary=true;
    const auto read=[&](const std::vector<Row>& mass,const std::vector<double>& energy) {
        parallel(n,[&](std::size_t i) {
            if(g.walls[i])return;
            Reading c;Sum moles,total,capacity,thermal;
            thermal.add(energy[i]);bool empty=true;
            for(std::size_t k=0;k<ng;++k) {
                const double m=mass[i][k];if(!std::isfinite(m) || m<0 || (m>0 && (m<1e-150 || m>1e150))) { ordinary=false;return; }
                empty=empty && m==0;
                total.add(m);moles.add(m/molecular[k]);capacity.add(m*cv[k]);thermal.add(-m*offset[k]);
            }
            // Empty cells have no faces and no defined temperature.
            if(empty) { if(energy[i]!=0)ordinary=false;w.reading[i]=c;return; }
            c.mass=total.value();c.moles=moles.value();c.temperature=thermal.value()/capacity.value();
            c.pressure=c.moles*gas_constant*c.temperature/g.volume_m3();
            if(!std::isfinite(c.temperature) || !std::isfinite(c.pressure) || c.mass<=0 || c.moles<=0) { ordinary=false;return; }
            for(std::size_t k=0;k<ng;++k) {
                if(mass[i][k]>0 && (c.temperature<minimum_t[k] || c.temperature>maximum_t[k])) { ordinary=false;return; }
                c.x[k]=mass[i][k]/molecular[k]/c.moles;c.y[k]=mass[i][k]/c.mass;
                c.partial[k]=mass[i][k]/molecular[k]*gas_constant*c.temperature/g.volume_m3();
            }
            w.reading[i]=c;
        });
    };
    std::array<double,6> reduced{}; // pair reduced molar masses, index k(k-1)/2+j for j<k
    for(std::size_t k=0;k<ng;++k)for(std::size_t j=0;j<k;++j)
        reduced[k*(k-1)/2+j]=molecular[k]*molecular[j]/(molecular[k]+molecular[j]);
    std::atomic<bool> out_of_range=false;
    const auto rates=[&](const std::vector<Row>& mass,const std::vector<double>& energy,std::vector<Flux>& flux) {
        read(mass,energy);if(!ordinary)return 0.;
        struct CoefficientCache {
            // Pressure-free atmospheric coefficient and Knudsen limit per pair.
            struct Entry { double temperature=0;std::array<Row,4> atmospheric{},knudsen{};std::array<std::array<bool,4>,4> valid{}; };
            std::array<Entry,8> entries{};std::size_t replacement=0;
            Entry& at(double temperature) {
                for(auto& entry:entries)if(entry.temperature==temperature)return entry;
                auto& entry=entries[replacement++%entries.size()];entry={};entry.temperature=temperature;return entry;
            }
        };
        // Exact-temperature, per-worker cache, reset for every predictor stage.
        // Pressure scaling and every pair's validity check still use this face.
        std::array<CoefficientCache,8> caches;
        parallel(w.links.size(),[&](std::size_t f,std::size_t worker) {
            const auto [l,r]=w.links[f];const auto& a=w.reading[l];const auto& b=w.reading[r];Flux out;
            Row x{},y{};bool different=a.temperature!=b.temperature;
            for(std::size_t k=0;k<ng;++k) { x[k]=.5*(a.x[k]+b.x[k]);y[k]=.5*(a.y[k]+b.y[k]);different=different || a.partial[k]!=b.partial[k]; }
            if(!different) { flux[f]=out;return; }
            const double temperature=.5*(a.temperature+b.temperature),pressure=.5*(a.pressure+b.pressure);
            out.temperature=temperature;out.pressure=pressure;
            auto& cached=caches[worker].at(temperature);
            std::array<Row,4> binary{};std::size_t present=0;
            for(std::size_t k=0;k<ng;++k)if(x[k]>0)++present;
            if(present<2) { flux[f]=out;return; }
            for(std::size_t k=0;k<ng;++k)for(std::size_t j=0;j<k;++j)if(x[k]>0 && x[j]>0) {
                const auto& p=*properties[k][j];
                // Correlations are extrapolated in their own form outside the
                // validated range; such evaluations are counted, not rejected.
                if(!(temperature>=p.minimum_temperature_k && temperature<=p.maximum_temperature_k && pressure>=p.minimum_pressure_pa && pressure<=p.maximum_pressure_pa))++out.range;
                if(!cached.valid[k][j]) {
                    cached.atmospheric[k][j]=1e-4*std::exp(p.a+p.b/temperature+p.c*std::log(temperature))*101325;
                    cached.knudsen[k][j]=g.spacing_m/3*std::sqrt(8*gas_constant*temperature/(std::numbers::pi*reduced[k*(k-1)/2+j]));
                    cached.valid[k][j]=true;
                }
                // Bosanquet: 1/D = 1/(multiplier D_molecular) + 1/D_Knudsen.
                const double d=1/(pressure/(cached.atmospheric[k][j]*settings.multiplier)+1/cached.knudsen[k][j]);
                if(!std::isfinite(d) || d<=0) { ordinary=false;return; }
                binary[k][j]=binary[j][k]=d;out.maximum_d=std::max(out.maximum_d,d);
            }
            if(out.range)out_of_range=true;
            // Exact discrete force per species in partial-pressure form:
            // F_k = Lambda_k (ln(p_k^L/p_k^R) + G_k) = (p_k^L - p_k^R) + Lambda_k G_k,
            // Lambda_k the log-mean partial pressure (zero for a one-sided species).
            const double thermal=a.temperature==b.temperature ? 0 : [&] {
                const double u=(b.temperature-a.temperature)/(b.temperature+a.temperature);return 2*std::atanh(u)-2*u/((1-u)*(1+u)); }();
            Row force{},lambda{},mobility{},raw{};std::array<bool,4> both{};Sum weight_total,force_total;bool any_both=false;
            for(std::size_t k=0;k<ng;++k)if(x[k]>0) {
                Sum denominator,other_y;
                for(std::size_t j=0;j<ng;++j)if(j!=k && x[j]>0) { denominator.add(x[j]/binary[k][j]);other_y.add(y[j]); }
                // Mixture-averaged D'_k times W_k (A/dx) / (R T_f).
                mobility[k]=other_y.value()/denominator.value()*(molecular[k]*g.depth_m/(gas_constant*temperature));
                const double left=a.partial[k],right=b.partial[k];
                both[k]=left>0 && right>0;force[k]=left-right;
                if(both[k]) {
                    if(left==right)lambda[k]=left;
                    else {
                        const double high=std::max(left,right),low=std::min(left,right),t=(high-low)/low;
                        lambda[k]=t<=1 ? low*(t/std::log1p(t)) : (high-low)/(std::log(high)-std::log(low));
                    }
                    if(thermal!=0)force[k]+=lambda[k]*(molecular[k]*cp[k]/gas_constant*thermal);
                    weight_total.add(molecular[k]*lambda[k]);force_total.add(force[k]);any_both=true;
                }
                if(!std::isfinite(mobility[k]) || !std::isfinite(force[k]) || !std::isfinite(lambda[k])) { ordinary=false;return; }
            }
            std::size_t pivot=ng;
            if(any_both) {
                // J = P_w A P_w^T psi with w_k = W_k Lambda_k / sum(W_j Lambda_j):
                // symmetric positive semidefinite in the exact force, zero net mass,
                // continuum limit driven by d_k = grad x_k + (x_k - w_k) grad ln p.
                Sum raw_total;const double weights=weight_total.value(),mean=force_total.value();
                for(std::size_t k=0;k<ng;++k)if(x[k]>0) {
                    raw[k]=(both[k] ? force[k]-molecular[k]*lambda[k]/weights*mean : force[k])*mobility[k];raw_total.add(raw[k]);
                }
                for(std::size_t k=0;k<ng;++k)if(both[k]) {
                    raw[k]-=molecular[k]*lambda[k]/weights*raw_total.value();
                    if(pivot==ng || molecular[k]*lambda[k]>molecular[pivot]*lambda[pivot])pivot=k;
                }
            } else {
                // Disjoint inventories: downhill Fick fluxes scaled per side to the
                // corrected group totals Y_R S_L + Y_L S_R.
                Sum left_sum,right_sum,left_y,right_y;
                for(std::size_t k=0;k<ng;++k)if(x[k]>0) {
                    raw[k]=force[k]*mobility[k];
                    if(a.partial[k]>0) { left_sum.add(raw[k]);left_y.add(y[k]); } else { right_sum.add(-raw[k]);right_y.add(y[k]); }
                }
                const double transfer=left_sum.value()*right_y.value()+right_sum.value()*left_y.value();
                if(!(left_sum.value()>0 && right_sum.value()>0)) { ordinary=false;return; }
                for(std::size_t k=0;k<ng;++k)if(x[k]>0) {
                    raw[k]*=transfer/(a.partial[k]>0 ? left_sum.value() : right_sum.value());
                    if(pivot==ng || std::abs(raw[k])>std::abs(raw[pivot]))pivot=k;
                }
            }
            for(std::size_t k=0;k<ng;++k)if(!std::isfinite(raw[k]) || (raw[k]!=0 && !std::isnormal(raw[k]))) { ordinary=false;return; }
            // Algebraic zero-net-mass constraint, not an inventory repair.
            Sum corrected,heat;
            for(std::size_t k=0;k<ng;++k)if(k!=pivot) { out.mass[k]=raw[k];corrected.add(out.mass[k]); }
            out.mass[pivot]=-corrected.value();
            for(std::size_t k=0;k<ng;++k)heat.add(out.mass[k]*(formation[k]+cp[k]*(temperature-reference_temperature)));
            out.heat=heat.value();flux[f]=out;
        });
        double maximum=0;for(const auto& f:flux)maximum=std::max(maximum,f.maximum_d);return maximum;
    };
    const auto record_range=[&](const std::vector<Flux>& flux,MacDiffusionReport& report) {
        if(!out_of_range)return;
        for(const auto& f:flux)for(std::uint32_t k=0;k<f.range;++k)report.record_out_of_range(f.temperature,f.pressure);
    };
    std::size_t trial_unresolved=0;
    const auto apply=[&](double dt,bool corrected,std::vector<Row>& target,std::vector<double>& energy) {
        std::atomic<bool> positive=true;std::atomic<std::size_t> unresolved_count=0;
        parallel(n,[&](std::size_t i) {
            if(g.walls[i])return;
            Sum heat;const auto& adjacent=w.adjacent[i];
            for(std::size_t k=0;k<ng;++k) {
                Sum change,sum;sum.add(w.masses[i][k]);
                for(std::size_t j=0;j<adjacent.count;++j) {
                    const auto f=adjacent.face[j];const double value=corrected ? .5*w.first[f].mass[k]+.5*w.second[f].mass[k]:w.first[f].mass[k];
                    const double amount=adjacent.sign[j]*dt*value;
                    if(rounding_policy==MacDiffusionRounding::counted && value!=0 && !std::isnormal(amount))ordinary=false;
                    sum.add(amount);change.add(amount);
                }
                target[i][k]=sum.value();if(target[i][k]<0)positive=false;
                if(target[i][k]>0 && target[i][k]<1e-150)ordinary=false;
                if(corrected && change.value()!=0 && target[i][k]==w.masses[i][k])unresolved_count.fetch_add(1,std::memory_order_relaxed);
            }
            Sum heat_change;heat.add(w.energy[i]);for(std::size_t j=0;j<adjacent.count;++j) {
                const auto f=adjacent.face[j];const double amount=adjacent.sign[j]*dt*(corrected ? .5*w.first[f].heat+.5*w.second[f].heat:w.first[f].heat);
                if(rounding_policy==MacDiffusionRounding::counted &&
                    (corrected ? .5*w.first[f].heat+.5*w.second[f].heat:w.first[f].heat)!=0 && !std::isnormal(amount))ordinary=false;
                heat.add(amount);heat_change.add(amount);
            }
            energy[i]=heat.value();
            if(corrected && heat_change.value()!=0 && energy[i]==w.energy[i])unresolved_count.fetch_add(1,std::memory_order_relaxed);
        });trial_unresolved=unresolved_count.load();return positive.load();
    };
    MacDiffusionReport report;double elapsed=0,next=seconds;std::size_t attempts=0;
    while(elapsed<seconds) {
        const auto maximum=rates(w.masses,w.energy,w.first);if(!ordinary)return {};
        // 10% inside the 0.2 acceptance bound (see the general path).
        double dt=std::min({next,seconds-elapsed,maximum>0 ? .18*g.spacing_m*g.spacing_m/maximum:seconds});
        while(true) {
            if(++attempts>budget)throw std::runtime_error("Diffusion exhausted its attempt budget");
            if(rounding_policy==MacDiffusionRounding::counted)consumed=attempts;
            if(!(dt>0) || elapsed+dt<=elapsed)throw std::runtime_error("Diffusion clock cannot advance");
            if(apply(dt,false,w.predictor,w.predictor_energy)) {
                const auto second_max=rates(w.predictor,w.predictor_energy,w.second);if(!ordinary)return {};
                if(second_max*dt/(g.spacing_m*g.spacing_m)<=.2 && apply(dt,true,w.next,w.next_energy))break;
            }
            if(!ordinary)return {};
            dt*=.5;++report.rejected_attempts;
        }
        if(!ordinary)return {};
        record_range(w.first,report);record_range(w.second,report);out_of_range=false;
        for(std::size_t f=0;f<w.first.size();++f)for(std::size_t k=0;k<ng;++k)report.exchanged_mass_kg+=.5*dt*std::abs(.5*w.first[f].mass[k]+.5*w.second[f].mass[k]);
        report.unresolved_updates+=trial_unresolved;
        w.masses.swap(w.next);w.energy.swap(w.next_energy);elapsed+=dt;next=std::min(2*dt,seconds-elapsed);++report.substeps;report.faces+=w.links.size();
    }
    read(w.masses,w.energy);if(!ordinary)return {};
    std::array<Sum,4> balances;Sum energy,entropy,entropy_allowance;
    for(std::size_t i=0;i<n;++i)if(!g.walls[i]) {
        if(!(source.cells[i].mixture.mass()>0)) {
            // Empty cells have no faces; they must be unchanged.
            for(std::size_t k=0;k<ng;++k)if(w.masses[i][k]!=0)return {};
            if(w.energy[i]!=0)return {};
            continue;
        }
        Sum total,old_capacity,old_thermal;old_thermal.add(source.cells[i].mixture.energy);
        for(std::size_t k=0;k<ng;++k) {
            const auto a=source.cells[i].mixture.masses[gases[k]],b=w.masses[i][k];
            balances[k].add(b);balances[k].add(-a);total.add(b);total.add(-a);
            old_capacity.add(a*cv[k]);old_thermal.add(-a*offset[k]);
        }
        if(std::abs(total.value())>total.bound())return {};
        energy.add(w.energy[i]);energy.add(-source.cells[i].mixture.energy);
        const double ta=old_thermal.value()/old_capacity.value(),tb=w.reading[i].temperature;
        for(std::size_t k=0;k<ng;++k) {
            const double a=source.cells[i].mixture.masses[gases[k]],b=w.masses[i][k],na=a/molecular[k],nb=b/molecular[k];
            entropy.add(cv[k]*(b*std::log(tb/entropy_reference_temperature_k)-a*std::log(ta/entropy_reference_temperature_k)));
            if(nb>0)entropy.add(nb*gas_constant*std::log(g.volume_m3()/nb));
            if(na>0)entropy.add(-na*gas_constant*std::log(g.volume_m3()/na));
        }
        entropy_allowance.add(std::abs(w.energy[i])/tb);entropy_allowance.add(std::abs(source.cells[i].mixture.energy)/ta);
    }
    if(!std::isfinite(energy.scale) || std::abs(energy.value())>energy.bound() || entropy.value() < -entropy_allowance.bound())return {};
    report.species_mass_residual.resize(registry.size());report.species_mass_bound.resize(registry.size());
    for(std::size_t k=0;k<ng;++k) {
        if(!std::isfinite(balances[k].scale) || std::abs(balances[k].value())>balances[k].bound())return {};
        report.species_mass_residual[gases[k]]=balances[k].value();report.species_mass_bound[gases[k]]=balances[k].bound();
    }
    report.energy_residual=MacEnergy::from_double(energy.value());report.energy_bound=MacEnergy::from_double(energy.bound());
    report.entropy_change_j_per_k=entropy.value();report.entropy_roundoff_j_per_k=entropy_allowance.bound();
    MacDiffusionAdvance result{source,std::move(report)};
    parallel(n,[&](std::size_t i) { if(!g.walls[i]) { for(std::size_t k=0;k<ng;++k)result.state.cells[i].mixture.masses[gases[k]]=w.masses[i][k];result.state.cells[i].mixture.energy=w.energy[i]; } });
    result.report.elapsed_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;
}
}
