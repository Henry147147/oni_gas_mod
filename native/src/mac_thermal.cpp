#include "mixed_atmospheres/mac_thermal.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace ma::experimental {
namespace {
// Persist the low part of each endpoint rather than independently dropping
// small heat changes or moving a high part away from its nearest value.
void round_closed_heat_component(const MacTransportState& source,MacTransportState& target,
    const std::vector<std::size_t>& cells,const std::vector<double>& changes,MacThermalReport& report) {

    for (std::size_t n=0;n<cells.size();++n) {
        target.cells[cells[n]]=source.cells[cells[n]];
        const auto write=add_mac_cell_internal_energy(target.cells[cells[n]],MacEnergy::from_double(changes[n]),&report.primary_energy);
        if(report.storage_rounding_j.empty())report.storage_rounding_j.resize(source.cells.size());
        for(std::size_t k=0;k<write.discarded.size();++k)report.storage_rounding_j[cells[n]].add(write.discarded.component(k));
    }
}
long double mixed_pair_entropy(const FluidThermodynamics& eos,const MacTransportState& before,
    const MacTransportState& after,double volume,long double allowance) {
    std::array<double,2> changes{};
    for (std::size_t i=0;i<2;++i) {
        const auto delta=mac_cell_internal_energy_change(before.cells[i],after.cells[i]);
        changes[i]=(i==0 ? 1 : -1)*delta.value().require_representable();
        if (changes[i]<0) throw FluidFatalError(FluidFatalCode::numerical_precision,"Closed conductive rounding reversed a heat exchange");
    }
    const auto common=std::min(changes[0],changes[1]);
    const auto temperature=[&](std::size_t cell,double shared,double extra) {
        auto value=before.cells[cell];MacEnergySum energy;
        account_mac_cell_internal_energy(energy,value);energy.add(shared);energy.add(extra);
        (void)store_mac_cell_internal_energy(value,energy);
        return eos.evaluate(value.mixture,volume).temperature_k;
    };
    const auto integrate=[&](const auto& integrand,double amount) -> long double {
        if (amount==0) return 0;
        const auto sample=[&](double x) {return static_cast<long double>(amount)*integrand(x);};
        const auto fa=sample(0),fm=sample(.5),fb=sample(1);
        const auto initial=(fa+4*fm+fb)/6;
        const auto refine=[&](auto&& self,double a,double b,long double left,long double middle,long double right,
            long double estimate,long double tolerance,unsigned depth) -> long double {
            const auto center=std::midpoint(a,b),ql=std::midpoint(a,center),qr=std::midpoint(center,b);
            const auto fl=sample(ql),fr=sample(qr);
            const auto first=(center-a)/6*(left+4*fl+middle),second=(b-center)/6*(middle+4*fr+right);
            const auto refined=first+second,error=refined-estimate;
            if (!std::isfinite(refined)) throw FluidFatalError(FluidFatalCode::numerical_precision,"Conductive entropy integral is unrepresentable");
            if (std::abs(error)<=15*tolerance) return refined+error/15;
            if (depth==0 || ql==a || qr==b)
                throw std::domain_error("Mixed conductive entropy integral did not resolve its arithmetic allowance");
            return self(self,a,center,left,fl,middle,first,tolerance/2,depth-1)+
                self(self,center,b,middle,fr,right,second,tolerance/2,depth-1);
        };
        return refine(refine,0,1,fa,fm,fb,initial,allowance/8,20);
    };
    // At fixed mass and volume, dS=dU/T includes caloric and elastic/gas
    // headspace effects. Integrate the closed pair jointly: subtracting two
    // Cv*log(T1/T0) values magnifies rounded EOS-temperature errors when the
    // physical heat is only a few stored-energy quanta.
    long double result=integrate([&](double x) {
        const long double cold=temperature(0,common*x,0),hot=temperature(1,-common*x,0);
        return (hot-cold)/cold/hot;
    },common);
    const double cold_extra=changes[0]-common,hot_extra=changes[1]-common;
    result+=integrate([&](double x) {return 1/static_cast<long double>(temperature(0,common,cold_extra*x));},cold_extra);
    result-=integrate([&](double x) {return 1/static_cast<long double>(temperature(1,-common,-hot_extra*x));},hot_extra);
    return result;
}

// A mixed cell's elastic energy changes with temperature at fixed occupied
// volume. Freeze the face conductances for this stage, but solve each closed
// contact against the complete EOS instead of substituting its initial Cv.
// Forward/reverse half sweeps remove a preferred contact direction. This is
// a first-order contact-split backward-Euler operator, not a coupled graph BE
// solve. Every contact retains the two physical energy endpoints together.
MacThermalAdvance conduct_mixture_contacts(const FluidThermodynamics& eos,const MacTransportState& source,
    const std::vector<std::size_t>& cells,const std::vector<ThermalContact>& contacts,double seconds) {
    MacThermalAdvance result{source,{}};result.report.contacts=contacts.size();result.report.storage_rounding_j.resize(source.cells.size());
    MacEnergySum redistributed,requested;long double entropy=0,entropy_bound=0;
    const auto volume=source.grid.volume_m3();
    const auto exchange=[&](const ThermalContact& contact,double duration) {
        const auto a=cells[contact.first],b=cells[contact.second];
        const auto first=eos.evaluate(result.state.cells[a].mixture,volume);
        const auto second=eos.evaluate(result.state.cells[b].mixture,volume);
        if (first.temperature_k==second.temperature_k) return;
        const bool forward=first.temperature_k<second.temperature_k;
        const auto cold=forward ? a : b,hot=forward ? b : a;
        const auto& cold_reading=forward ? first : second;const auto& hot_reading=forward ? second : first;
        const auto cold_initial=result.state.cells[cold].mixture,hot_initial=result.state.cells[hot].mixture;
        const double multiplier=duration*contact.conductance_w_per_k;
        if (!std::isfinite(multiplier) || multiplier<=0)
            throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive contact scale is unrepresentable");
        const auto endpoint_energy=[&](const MixtureInventory& value,double temperature) {
            const auto reference=eos.to_reference(value,volume);const auto& materials=eos.reference().materials();
            double minimum=0,maximum=std::numeric_limits<double>::infinity();
            for (std::size_t k=0;k<materials.size();++k) if (reference.masses_kg[k]>0) {
                minimum=std::max(minimum,materials[k].minimum_temperature_k);
                maximum=std::min(maximum,materials[k].maximum_temperature_k);
            }
            const auto target=std::clamp(temperature,minimum,maximum);
            const auto initialize=[&](double t) { return eos.initialize_at_temperature(value.masses,t,volume).mixture.energy; };
            try { return std::pair(initialize(target),target); }
            catch (const ReferenceError& problem) {
                const auto current=eos.evaluate(value,volume).temperature_k;
                if (problem.code()!=ReferenceErrorCode::compression_outside_model || target<=current) throw;
                // The opposite cell's temperature can lie beyond this cell's
                // supported compression, while a small actual exchange is
                // admissible. Bracket the last valid temperature first.
                double low=current,high=target,energy=value.energy;
                for (std::size_t n=0;n<256;++n) {
                    const auto middle=std::midpoint(low,high);if (middle==low || middle==high) break;
                    try {energy=initialize(middle);low=middle;}
                    catch (const ReferenceError& boundary) {
                        if (boundary.code()!=ReferenceErrorCode::compression_outside_model) throw;
                        high=middle;
                    }
                }
                return std::pair(energy,low);
            }
        };
        const auto cold_endpoint=endpoint_energy(cold_initial,hot_reading.temperature_k);
        const auto hot_endpoint=endpoint_energy(hot_initial,cold_reading.temperature_k);
        const bool temperature_residual=multiplier>1;
        const double inverse_capacity=1/cold_reading.equilibrium_heat_capacity_at_cell_volume_j_per_k+
            1/hot_reading.equilibrium_heat_capacity_at_cell_volume_j_per_k;
        const double contrast=hot_reading.temperature_k-cold_reading.temperature_k;
        const double initial_request=temperature_residual ? contrast/(1/multiplier+inverse_capacity) :
            multiplier*contrast/(1+multiplier*inverse_capacity);
        if (!std::isfinite(initial_request) || initial_request<=0)
            throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive heat request is unrepresentable");
        MacEnergySum cold_limit,hot_limit;
        cold_limit.add(cold_endpoint.first);cold_limit.add(-cold_initial.energy);
        hot_limit.add(hot_initial.energy);hot_limit.add(-hot_endpoint.first);
        const double upper=std::min(cold_limit.value().require_representable(),hot_limit.value().require_representable());
        if (upper<=0) {
            if (cold_endpoint.second<=cold_reading.temperature_k || hot_endpoint.second>=hot_reading.temperature_k)
                throw std::domain_error("Mixed conductive exchange would leave a constituent's temperature model");
            // Distinct returned temperatures can lie inside one stored-energy
            // quantum. Report the unavailable heat; never impose a flux floor.
            requested.add(initial_request);result.report.unresolved_heat_updates+=2;return;
        }
        const auto at=[&](double q) {
            auto c=result.state.cells[cold],h=result.state.cells[hot];
            (void)add_mac_cell_internal_energy(c,MacEnergy::from_double(q));
            (void)add_mac_cell_internal_energy(h,MacEnergy::from_double(-q));
            return std::pair(eos.evaluate(c.mixture,volume),eos.evaluate(h.mixture,volume));
        };
        double low=0,high=upper;
        double q=std::min(upper,initial_request);
        bool converged=false;
        for (std::size_t iteration=0;iteration<256;++iteration) {
            ++result.report.iterations;const auto [c,h]=at(q);
            const double difference=h.temperature_k-c.temperature_k;
            // Scale stiff roots in kelvin rather than forming dt*G*deltaT,
            // which can overflow despite a finite admissible heat transfer.
            const double stored=temperature_residual ? q/multiplier : q;
            const double flux=temperature_residual ? difference : multiplier*difference,residual=stored-flux;
            const double temperature_roundoff=64*std::numeric_limits<double>::epsilon()*(c.temperature_k+h.temperature_k)+
                std::abs(c.energy_residual_j)/c.equilibrium_heat_capacity_at_cell_volume_j_per_k+
                std::abs(h.energy_residual_j)/h.equilibrium_heat_capacity_at_cell_volume_j_per_k;
            const double bound=128*std::numeric_limits<double>::epsilon()*(std::abs(stored)+std::abs(flux))+
                (temperature_residual ? temperature_roundoff : multiplier*temperature_roundoff);
            if (!std::isfinite(residual) || !std::isfinite(bound))
                throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive root residual is unrepresentable");
            if (std::abs(residual)<=bound) {converged=true;break;}
            if (residual<0) low=q;else high=q;
            const double capacity=1/c.equilibrium_heat_capacity_at_cell_volume_j_per_k+
                1/h.equilibrium_heat_capacity_at_cell_volume_j_per_k;
            const double derivative=temperature_residual ? 1/multiplier+capacity : 1+multiplier*capacity;
            const double newton=q-residual/derivative;
            const double next=newton>low && newton<high ? newton : std::midpoint(low,high);
            if (next==q || next==low || next==high)
                throw std::domain_error("Mixed conductive contact root is below available precision or outside the material range");
            q=next;
        }
        if (!converged) throw std::domain_error("Mixed conductive contact root did not converge");
        MacTransportState before;before.cells={result.state.cells[cold],result.state.cells[hot]};
        auto after=before;const std::vector<std::size_t> pair_cells{0,1};const std::vector<double> changes{q,-q};
        MacThermalReport pair_rounding;
        round_closed_heat_component(before,after,pair_cells,changes,pair_rounding);
        result.report.primary_energy.merge(pair_rounding.primary_energy);
        for(std::size_t side=0;side<2;++side)for(std::size_t k=0;k<pair_rounding.storage_rounding_j[side].size();++k)
            result.report.storage_rounding_j[side==0 ? cold : hot].add(pair_rounding.storage_rounding_j[side].component(k));
        requested.add(q);
        const std::array initial{cold_reading,hot_reading};long double pair_entropy_bound=0;
        for (std::size_t i=0;i<2;++i) {
            const auto& old=before.cells[i].mixture;const auto& next=after.cells[i].mixture;
            const auto reading=eos.evaluate(next,volume);const auto& anchor=initial[i];
            const double temperature_bound=128*std::numeric_limits<double>::epsilon()*hot_reading.temperature_k;
            if (reading.temperature_k<cold_reading.temperature_k-temperature_bound ||
                reading.temperature_k>hot_reading.temperature_k+temperature_bound)
                throw std::domain_error("Mixed conductive contact violated its maximum principle");
            MacEnergyAccount local;account_mac_cell_internal_energy(local,before.cells[i]);local.add(changes[i]);account_mac_cell_internal_energy(local,after.cells[i],-1);
            if (!local.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive cell energy account failed");
            const auto change=mac_cell_internal_energy_change(before.cells[i],after.cells[i]);
            redistributed.add(change.value().magnitude().multiplied(.5));
            if (q!=0 && same_mac_cell_internal_energy(before.cells[i],after.cells[i])) ++result.report.unresolved_heat_updates;
            pair_entropy_bound+=128*std::numeric_limits<double>::epsilon()*(std::abs(old.energy)/anchor.temperature_k+
                std::abs(next.energy)/reading.temperature_k+anchor.intrinsic_heat_capacity_j_per_k);
        }
        entropy+=mixed_pair_entropy(eos,before,after,volume,pair_entropy_bound);entropy_bound+=pair_entropy_bound;
        result.state.cells[cold]=after.cells[0];
        result.state.cells[hot]=after.cells[1];
    };
    if (contacts.size()==1) exchange(contacts.front(),seconds);
    else {
        for (const auto& contact : contacts) exchange(contact,.5*seconds);
        for (auto it=contacts.rbegin();it!=contacts.rend();++it) exchange(*it,.5*seconds);
    }
    MacEnergyAccount balance;
    for (const auto cell : cells) {account_mac_cell_internal_energy(balance,source.cells[cell]);account_mac_cell_internal_energy(balance,result.state.cells[cell],-1);}
    if (!balance.within_bound() || entropy < -entropy_bound)
        throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conduction energy/entropy account failed");
    result.report.energy_residual=balance.residual();result.report.energy_bound=balance.bound();
    result.report.redistributed_energy=redistributed.value();result.report.requested_redistributed_energy=requested.value();
    result.report.entropy_change_j_per_k=static_cast<double>(entropy);
    if (!std::isfinite(result.report.entropy_change_j_per_k))
        throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive entropy report is unrepresentable");
    return result;
}
}
MacThermalAdvance advance_mac_conduction(const SpeciesRegistry& registry,const FluidThermodynamics& eos,
    const MacTransportState& source,double seconds) {
    require_mac_energy_environment();
    if (!std::isfinite(seconds) || seconds <= 0) throw std::invalid_argument("Invalid MAC conduction timestep");
    (void)mac_face_inertias(registry,source,eos); // Validate complete canonical input before reading geometry.
    const auto& grid=source.grid; const auto absent=std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> nodes(source.cells.size(),absent),cells;
    std::vector<ThermalBody> bodies; std::vector<double> conductivity;bool mixed=false;
    for (std::size_t i=0;i<source.cells.size();++i) if (!grid.walls[i] && source.cells[i].mixture.mass() > 0) {
        const auto& mixture=source.cells[i].mixture; const auto reading=eos.evaluate(mixture,grid.volume_m3());
        const auto capacity=reading.equilibrium_heat_capacity_at_cell_volume_j_per_k;
        const bool variable=reading.has_gas && reading.liquid_volume_m3>0;mixed|=variable;
        if (!std::isfinite(capacity) || capacity<=0 || (!variable &&
            std::abs(capacity-reading.intrinsic_heat_capacity_j_per_k)>64*std::numeric_limits<double>::epsilon()*capacity))
            throw FluidFatalError(FluidFatalCode::unsupported_topology,"MAC conductive cell has an unsupported equilibrium heat capacity");
        double numerator=0,denominator=0;
        for (std::size_t k=0;k<registry.size();++k) if (mixture.masses[k] > 0) {
            const auto& species=registry[k];
            if (!std::isfinite(species.conductivity) || species.conductivity < 0)
                throw std::invalid_argument("MAC conduction has an invalid constituent conductivity");
            const auto& materials=eos.reference().materials();
            const auto material=std::find_if(materials.begin(),materials.end(),[&](const auto& value) { return value.id == species.id; });
            if (material == materials.end()) throw std::invalid_argument("Missing conductive material in the EOS profile");
            // Gas partial volumes are proportional to mole fractions; liquid
            // volumes include the actual elastic compression at this state.
            const auto weight=reading.partial_volumes_m3[static_cast<std::size_t>(material-materials.begin())];
            numerator+=weight*species.conductivity; denominator+=weight;
        }
        const auto k=numerator/denominator;
        if (!std::isfinite(k) || k < 0) throw std::invalid_argument("MAC mixture conductivity is unrepresentable");
        nodes[i]=cells.size(); cells.push_back(i); bodies.push_back({capacity,reading.temperature_k}); conductivity.push_back(k);
    }
    if (cells.empty()) return {source, {}};
    std::vector<ThermalContact> contacts;
    const auto edge=[&](std::size_t a,std::size_t b) {
        if (nodes[a] == absent || nodes[b] == absent) return;
        const auto conductance=thermal_conductance(conductivity[nodes[a]],conductivity[nodes[b]],
            .5*grid.spacing_m,.5*grid.spacing_m,grid.spacing_m*grid.depth_m);
        if (conductance > 0) contacts.push_back({nodes[a],nodes[b],conductance});
    };
    for (std::size_t y=0;y<grid.height;++y) for (std::size_t x=0;x<grid.width;++x) {
        const auto cell=x+y*grid.width;
        if (x+1<grid.width || grid.periodic_x) edge(cell,(x+1)%grid.width+y*grid.width);
        if (y+1<grid.height || grid.periodic_y) edge(cell,x+((y+1)%grid.height)*grid.width);
    }
    if (mixed) return conduct_mixture_contacts(eos,source,cells,contacts,seconds);
    ThermalResult solved;
    try { solved=conduct_heat(bodies,contacts,seconds); }
    catch (const std::runtime_error& error) { throw std::domain_error(std::string("MAC conductive solve rejected: ")+error.what()); }
    MacThermalAdvance result{source,{}}; result.report.contacts=contacts.size(); result.report.iterations=solved.iterations;
    result.report.storage_rounding_j.resize(source.cells.size());
    if(solved.shared_face_energy_j.size()!=contacts.size())throw std::runtime_error("Missing shared conductive face transfers");
    std::vector<MacEnergySum> requested(cells.size());
    for(std::size_t f=0;f<contacts.size();++f) {
        int exponent=0;const long double mantissa=std::frexp(solved.shared_face_energy_j[f],&exponent);
        const double high=static_cast<double>(mantissa),low=static_cast<double>(mantissa-static_cast<long double>(high));
        for(double part:{high,low})if(part!=0) {
            const auto q=MacEnergy::from_double(part).shifted(exponent);
            requested[contacts[f].first].add(q);requested[contacts[f].second].add(q.negated());
        }
    }
    for(std::size_t n=0;n<cells.size();++n) {
        auto total=requested[n];account_mac_cell_internal_energy(total,source.cells[cells[n]]);
        const auto write=store_mac_cell_internal_energy(result.state.cells[cells[n]],total,&result.report.primary_energy);
        result.report.storage_rounding_j[cells[n]]=write.discarded;
    }
    MacEnergyAccount balance; MacEnergySum redistributed,requested_redistribution;
    long double entropy=0,entropy_bound=0;
    for (std::size_t n=0;n<cells.size();++n) {
        const auto cell=cells[n]; const auto before=source.cells[cell].mixture.energy; const auto change=requested[n].value().project().value;
        const auto after=result.state.cells[cell].mixture.energy;
        MacEnergyAccount local; account_mac_cell_internal_energy(local,source.cells[cell]); for(std::size_t k=0;k<requested[n].size();++k)local.add(requested[n].component(k)); account_mac_cell_internal_energy(local,result.state.cells[cell],-1);
        if (!local.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC conductive cell energy account failed");
        account_mac_cell_internal_energy(balance,source.cells[cell]); account_mac_cell_internal_energy(balance,result.state.cells[cell],-1);
        requested_redistribution.add(MacEnergy::from_double(std::abs(change)).multiplied(.5));
        if (change != 0 && same_mac_cell_internal_energy(source.cells[cell],result.state.cells[cell])) ++result.report.unresolved_heat_updates;
        const auto reading=eos.evaluate(result.state.cells[cell].mixture,grid.volume_m3());
        // Fixed mass/volume and constant Cv make deltaT=deltaU/C exact for
        // this operator. Subtracting two rounded EOS temperatures loses the
        // entropy of small, conservative heat transfers in dense liquids.
        const auto represented=mac_cell_internal_energy_change(source.cells[cell],result.state.cells[cell]);
        redistributed.add(represented.value().magnitude().multiplied(.5));
        const long double relative=static_cast<long double>(represented.value().require_representable())/
            bodies[n].heat_capacity_j_per_k/bodies[n].temperature_k;
        if (relative<=-1) throw FluidFatalError(FluidFatalCode::numerical_precision,"Conductive entropy endpoint is nonpositive");
        entropy+=static_cast<long double>(bodies[n].heat_capacity_j_per_k)*std::log1p(relative);
        entropy_bound+=128*std::numeric_limits<double>::epsilon()*(std::abs(before)/bodies[n].temperature_k+
            std::abs(after)/reading.temperature_k+bodies[n].heat_capacity_j_per_k);
    }
    if (!balance.within_bound() || entropy < -entropy_bound)
        throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC conduction energy/entropy account failed");
    result.report.energy_residual=balance.residual(); result.report.energy_bound=balance.bound();
    result.report.redistributed_energy=redistributed.value(); result.report.entropy_change_j_per_k=static_cast<double>(entropy);
    result.report.requested_redistributed_energy=requested_redistribution.value();
    return result;
}
}
