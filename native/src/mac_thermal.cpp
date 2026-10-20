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
// Each signed face energy as both endpoints' exact requested heat.
std::vector<MacEnergySum> face_requests(const ThermalResult& solved,const std::vector<ThermalContact>& contacts,std::size_t count) {
    if(solved.shared_face_energy_j.size()!=contacts.size())throw std::runtime_error("Missing shared conductive face transfers");
    std::vector<MacEnergySum> requested(count);
    for(std::size_t f=0;f<contacts.size();++f) {
        int exponent=0;const long double mantissa=std::frexp(solved.shared_face_energy_j[f],&exponent);
        const double high=static_cast<double>(mantissa),low=static_cast<double>(mantissa-static_cast<long double>(high));
        for(double part:{high,low})if(part!=0) {
            const auto q=MacEnergy::from_double(part).shifted(exponent);
            requested[contacts[f].first].add(q);requested[contacts[f].second].add(q.negated());
        }
    }
    return requested;
}
// dS=dU/T along one mixed cell's heat at fixed mass and volume: its elastic
// and headspace energies make the capacity vary along the path.
long double mixed_cell_entropy(const FluidThermodynamics& eos,const MacMaterialCell& before,double change,double volume,
    long double allowance) {
    if (change==0) return 0;
    const auto sample=[&](double x) {
        auto value=before;MacEnergySum energy;account_mac_cell_internal_energy(energy,value);energy.add(x*change);
        (void)store_mac_cell_internal_energy(value,energy);
        return static_cast<long double>(change)/eos.evaluate(value.mixture,volume).temperature_k;
    };
    const auto fa=sample(0),fm=sample(.5),fb=sample(1);
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
    return refine(refine,0,1,fa,fm,fb,(fa+4*fm+fb)/6,allowance/8,20);
}
// Coupled backward Euler with the complete EOS (MAC_HEAT.md, "Mixed cells").
// A mixed cell's U(T) at fixed volume is nonlinear, so Newton relinearizes it
// at its current state as C_k (T - start), start = T_k - Q_k / C_k, and
// solves the whole graph again. Other cells keep (C, T^n). Each solve works
// relative to its components' equilibrium, so stiff contacts keep the heat's
// relative precision. Returns the converged solve's face energies.
ThermalResult conduct_mixture_graph(const FluidThermodynamics& eos,const MacTransportState& source,
    const std::vector<std::size_t>& cells,std::vector<ThermalBody> bodies,const std::vector<std::uint8_t>& variable,
    const std::vector<ThermalContact>& contacts,double seconds,std::size_t& iterations) {
    const auto volume=source.grid.volume_m3();
    // Temperatures equal within their rounding across every contact are
    // already equilibrium: heat driven by an ulp of temperature is noise.
    if (std::all_of(contacts.begin(),contacts.end(),[&](const ThermalContact& contact) {
        const double a=bodies[contact.first].temperature_k,b=bodies[contact.second].temperature_k;
        return std::abs(a-b)<=64*std::numeric_limits<double>::epsilon()*(a+b); })) {
        ThermalResult none;none.shared_face_energy_j.assign(contacts.size(),0);return none;
    }
    // A contact whose integrated conductance exceeds 2^40 times its smaller
    // capacity holds both cells at one temperature to about 1e-12 of their
    // difference. Cap it there: beyond C/epsilon the capacity would vanish
    // from the solve's diagonal and the graph would become singular.
    auto capped=contacts;
    for (auto& contact : capped) contact.conductance_w_per_k=std::min(contact.conductance_w_per_k,std::ldexp(
        std::min(bodies[contact.first].heat_capacity_j_per_k,bodies[contact.second].heat_capacity_j_per_k),40)/seconds);
    double previous=std::numeric_limits<double>::infinity();
    for (std::size_t newton=1;;++newton) {
        ThermalResult solved;
        try { solved=conduct_heat(bodies,capped,seconds); }
        catch (const std::runtime_error& error) { throw std::domain_error(std::string("MAC conductive solve rejected: ")+error.what()); }
        iterations+=solved.iterations;
        const auto requested=face_requests(solved,contacts,cells.size());
        double worst=0;
        for (std::size_t n=0;n<cells.size();++n) if (variable[n]) {
            auto trial=source.cells[cells[n]];auto total=requested[n];account_mac_cell_internal_energy(total,trial);
            (void)store_mac_cell_internal_energy(trial,total);
            const double heat=requested[n].value().require_representable();
            ReferenceEquilibriumState reading;
            try { reading=eos.evaluate(trial.mixture,volume); }
            catch (const ReferenceError& problem) {
                if (problem.code()!=ReferenceErrorCode::temperature_outside_model &&
                    problem.code()!=ReferenceErrorCode::compression_outside_model &&
                    problem.code()!=ReferenceErrorCode::insufficient_thermal_energy) throw;
                throw std::domain_error("Mixed conduction would leave a constituent's temperature or compression model");
            }
            const double capacity=reading.equilibrium_heat_capacity_at_cell_volume_j_per_k;
            const double mismatch=reading.temperature_k-(bodies[n].temperature_k+heat/bodies[n].heat_capacity_j_per_k);
            const double bound=128*std::numeric_limits<double>::epsilon()*(reading.temperature_k+std::abs(trial.mixture.energy)/capacity)+
                std::abs(reading.energy_residual_j)/capacity;
            if (!std::isfinite(mismatch) || !std::isfinite(bound) || !(capacity>0))
                throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive linearization is unrepresentable");
            worst=std::max(worst,std::abs(mismatch)/bound);
            bodies[n]={capacity,reading.temperature_k-heat/capacity};
        }
        // At the arithmetic floor a further correction only re-rounds.
        if (worst<=1 || (newton>2 && worst<=64 && worst>=.5*previous)) return solved;
        if (newton==32) throw std::domain_error("Mixed conduction did not converge; retry smaller timestep");
        previous=worst;
    }
}
}
MacThermalAdvance advance_mac_conduction(const SpeciesRegistry& registry,const FluidThermodynamics& eos,
    const MacTransportState& source,double seconds) {
    require_mac_energy_environment();
    if (!std::isfinite(seconds) || seconds <= 0) throw std::invalid_argument("Invalid MAC conduction timestep");
    (void)mac_face_inertias(registry,source,eos); // Validate complete canonical input before reading geometry.
    const auto& grid=source.grid; const auto absent=std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> nodes(source.cells.size(),absent),cells;
    std::vector<ThermalBody> bodies; std::vector<double> conductivity;std::vector<std::uint8_t> variable_cell;bool mixed=false;
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
        variable_cell.push_back(variable);
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
    ThermalResult solved;std::size_t iterations=0;
    if (mixed) solved=conduct_mixture_graph(eos,source,cells,bodies,variable_cell,contacts,seconds,iterations);
    else {
        try { solved=conduct_heat(bodies,contacts,seconds); }
        catch (const std::runtime_error& error) { throw std::domain_error(std::string("MAC conductive solve rejected: ")+error.what()); }
        iterations=solved.iterations;
    }
    MacThermalAdvance result{source,{}}; result.report.contacts=contacts.size(); result.report.iterations=iterations;
    result.report.storage_rounding_j.resize(source.cells.size());
    const auto requested=face_requests(solved,contacts,cells.size());
    for(std::size_t n=0;n<cells.size();++n) {
        auto total=requested[n];account_mac_cell_internal_energy(total,source.cells[cells[n]]);
        const auto write=store_mac_cell_internal_energy(result.state.cells[cells[n]],total,&result.report.primary_energy);
        result.report.storage_rounding_j[cells[n]]=write.discarded;
    }
    MacEnergyAccount balance; MacEnergySum redistributed,requested_redistribution;
    long double entropy=0,entropy_bound=0;
    double minimum=std::numeric_limits<double>::infinity(),maximum=0;
    for (const auto& body : bodies) {minimum=std::min(minimum,body.temperature_k);maximum=std::max(maximum,body.temperature_k);}
    for (std::size_t n=0;n<cells.size();++n) {
        const auto cell=cells[n]; const auto before=source.cells[cell].mixture.energy; const auto change=requested[n].value().project().value;
        const auto after=result.state.cells[cell].mixture.energy;
        MacEnergyAccount local; account_mac_cell_internal_energy(local,source.cells[cell]); for(std::size_t k=0;k<requested[n].size();++k)local.add(requested[n].component(k)); account_mac_cell_internal_energy(local,result.state.cells[cell],-1);
        if (!local.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC conductive cell energy account failed");
        account_mac_cell_internal_energy(balance,source.cells[cell]); account_mac_cell_internal_energy(balance,result.state.cells[cell],-1);
        requested_redistribution.add(MacEnergy::from_double(std::abs(change)).multiplied(.5));
        if (change != 0 && same_mac_cell_internal_energy(source.cells[cell],result.state.cells[cell])) ++result.report.unresolved_heat_updates;
        const auto reading=eos.evaluate(result.state.cells[cell].mixture,grid.volume_m3());
        const auto represented=mac_cell_internal_energy_change(source.cells[cell],result.state.cells[cell]);
        redistributed.add(represented.value().magnitude().multiplied(.5));
        if (variable_cell[n]) {
            const double capacity=reading.equilibrium_heat_capacity_at_cell_volume_j_per_k;
            const double temperature_bound=128*std::numeric_limits<double>::epsilon()*maximum+std::abs(reading.energy_residual_j)/capacity;
            if (reading.temperature_k<minimum-temperature_bound || reading.temperature_k>maximum+temperature_bound)
                throw std::domain_error("Mixed conduction violated its maximum principle");
            const long double bound=128*std::numeric_limits<double>::epsilon()*(std::abs(before)/bodies[n].temperature_k+
                std::abs(after)/reading.temperature_k+reading.intrinsic_heat_capacity_j_per_k);
            entropy+=mixed_cell_entropy(eos,source.cells[cell],represented.value().require_representable(),grid.volume_m3(),bound);
            entropy_bound+=bound;
            continue;
        }
        // Fixed mass/volume and constant Cv make deltaT=deltaU/C exact for
        // this operator. Subtracting two rounded EOS temperatures loses the
        // entropy of small, conservative heat transfers in dense liquids.
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
