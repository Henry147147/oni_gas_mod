#include "mixed_atmospheres/mac_diffusion.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
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
struct Reading { Row x{},y{};double mass=0,moles=0,temperature=0,pressure=0; };
struct Flux { Row mass{};double heat=0,maximum_d=0; };
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
    const MacTransportState& source,double seconds,const MacDiffusionSettings& settings,const GasTransportProfile& profile,std::size_t budget) {
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
    const auto add=[&](std::size_t l,std::size_t r) {
        if(l==r || g.walls[l] || g.walls[r])return;
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
            thermal.add(energy[i]);
            for(std::size_t k=0;k<ng;++k) {
                const double m=mass[i][k];if(!std::isfinite(m) || m<0 || (m>0 && (m<1e-150 || m>1e150))) { ordinary=false;return; }
                total.add(m);moles.add(m/molecular[k]);capacity.add(m*cv[k]);thermal.add(-m*offset[k]);
            }
            c.mass=total.value();c.moles=moles.value();c.temperature=thermal.value()/capacity.value();
            c.pressure=c.moles*gas_constant*c.temperature/g.volume_m3();
            if(!std::isfinite(c.temperature) || !std::isfinite(c.pressure) || c.mass<=0 || c.moles<=0) { ordinary=false;return; }
            for(std::size_t k=0;k<ng;++k) {
                if(mass[i][k]>0 && (c.temperature<minimum_t[k] || c.temperature>maximum_t[k])) { ordinary=false;return; }
                c.x[k]=mass[i][k]/molecular[k]/c.moles;c.y[k]=mass[i][k]/c.mass;
            }
            w.reading[i]=c;
        });
    };
    const auto rates=[&](const std::vector<Row>& mass,const std::vector<double>& energy,std::vector<Flux>& flux) {
        read(mass,energy);if(!ordinary)return 0.;
        struct CoefficientCache {
            struct Entry { double temperature=0;std::array<Row,4> value{};std::array<std::array<bool,4>,4> valid{}; };
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
            Row x{},y{},raw{};bool different=false;
            for(std::size_t k=0;k<ng;++k) { x[k]=.5*(a.x[k]+b.x[k]);y[k]=.5*(a.y[k]+b.y[k]);different=different || a.x[k]!=b.x[k]; }
            if(!different) { flux[f]=out;return; }
            const double temperature=.5*(a.temperature+b.temperature),pressure=.5*(a.pressure+b.pressure);
            const double rho=.5*(a.mass+b.mass)/g.volume_m3(),mw=.5*(a.mass/a.moles+b.mass/b.moles);
            auto& cached=caches[worker].at(temperature);
            std::array<Row,4> binary{};
            for(std::size_t k=0;k<ng;++k)for(std::size_t j=0;j<k;++j)if(x[k]>0 && x[j]>0) {
                const auto& p=*properties[k][j];
                if(temperature<p.minimum_temperature_k || temperature>p.maximum_temperature_k || pressure<p.minimum_pressure_pa || pressure>p.maximum_pressure_pa) { ordinary=false;return; }
                if(!cached.valid[k][j]) {
                    cached.value[k][j]=1e-4*std::exp(p.a+p.b/temperature+p.c*std::log(temperature))*101325;
                    cached.valid[k][j]=true;
                }
                const double d=cached.value[k][j]/pressure*settings.multiplier;
                if(!std::isfinite(d) || d<=0) { ordinary=false;return; }
                binary[k][j]=binary[j][k]=d;out.maximum_d=std::max(out.maximum_d,d);
            }
            Sum raw_total;std::size_t pivot=0;
            for(std::size_t k=0;k<ng;++k)if(x[k]>0) {
                Sum denominator,other_y;
                for(std::size_t j=0;j<ng;++j)if(j!=k && x[j]>0) { denominator.add(x[j]/binary[k][j]);other_y.add(y[j]); }
                if(denominator.value()==0)continue;
                raw[k]=(a.x[k]-b.x[k])*(rho*molecular[k]/mw*g.depth_m)*(other_y.value()/denominator.value());
                if(!std::isfinite(raw[k]) || (raw[k]!=0 && !std::isnormal(raw[k]))) { ordinary=false;return; }
                raw_total.add(raw[k]);if(y[k]>y[pivot])pivot=k;
            }
            Sum corrected,heat;
            for(std::size_t k=0;k<ng;++k)if(k!=pivot) { out.mass[k]=raw[k]-y[k]*raw_total.value();corrected.add(out.mass[k]); }
            out.mass[pivot]=-corrected.value();
            for(std::size_t k=0;k<ng;++k)heat.add(out.mass[k]*(formation[k]+cp[k]*(temperature-reference_temperature)));
            out.heat=heat.value();flux[f]=out;
        });
        double maximum=0;for(const auto& f:flux)maximum=std::max(maximum,f.maximum_d);return maximum;
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
                    const double amount=adjacent.sign[j]*dt*value;sum.add(amount);change.add(amount);
                }
                target[i][k]=sum.value();if(target[i][k]<0)positive=false;
                if(target[i][k]>0 && target[i][k]<1e-150)ordinary=false;
                if(corrected && change.value()!=0 && target[i][k]==w.masses[i][k])unresolved_count.fetch_add(1,std::memory_order_relaxed);
            }
            Sum heat_change;heat.add(w.energy[i]);for(std::size_t j=0;j<adjacent.count;++j) {
                const auto f=adjacent.face[j];const double amount=adjacent.sign[j]*dt*(corrected ? .5*w.first[f].heat+.5*w.second[f].heat:w.first[f].heat);
                heat.add(amount);heat_change.add(amount);
            }
            energy[i]=heat.value();
            if(corrected && heat_change.value()!=0 && energy[i]==w.energy[i])unresolved_count.fetch_add(1,std::memory_order_relaxed);
        });trial_unresolved=unresolved_count.load();return positive.load();
    };
    MacDiffusionReport report;double elapsed=0,next=seconds;std::size_t attempts=0;
    while(elapsed<seconds) {
        const auto maximum=rates(w.masses,w.energy,w.first);if(!ordinary)return {};
        double dt=std::min({next,seconds-elapsed,maximum>0 ? .2*g.spacing_m*g.spacing_m/maximum:seconds});
        while(true) {
            if(++attempts>budget)throw std::runtime_error("Diffusion exhausted its attempt budget");
            if(!(dt>0) || elapsed+dt<=elapsed)throw std::runtime_error("Diffusion clock cannot advance");
            if(apply(dt,false,w.predictor,w.predictor_energy)) {
                const auto second_max=rates(w.predictor,w.predictor_energy,w.second);if(!ordinary)return {};
                if(second_max*dt/(g.spacing_m*g.spacing_m)<=.2 && apply(dt,true,w.next,w.next_energy))break;
            }
            if(!ordinary)return {};
            dt*=.5;++report.rejected_attempts;
        }
        if(!ordinary)return {};
        for(std::size_t f=0;f<w.first.size();++f)for(std::size_t k=0;k<ng;++k)report.exchanged_mass_kg+=.5*dt*std::abs(.5*w.first[f].mass[k]+.5*w.second[f].mass[k]);
        report.unresolved_updates+=trial_unresolved;
        w.masses.swap(w.next);w.energy.swap(w.next_energy);elapsed+=dt;next=std::min(2*dt,seconds-elapsed);++report.substeps;report.faces+=w.links.size();
    }
    read(w.masses,w.energy);if(!ordinary)return {};
    std::array<Sum,4> balances;Sum energy,entropy,entropy_allowance;
    for(std::size_t i=0;i<n;++i)if(!g.walls[i]) {
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
