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
// Jointly round a closed heat exchange. Independently rounding many tiny
// signed changes can cool every hot cell while leaving every cold cell
// unchanged. Choose only the two binary64 neighbors bracketing each exact
// requested endpoint; their power-of-two quanta permit a deterministic
// conservative choice without changing the physical solve or its tolerance.
void round_closed_heat_component(const MacTransportState& source,MacTransportState& target,
    const std::vector<std::size_t>& cells,const std::vector<double>& changes,MacThermalReport& report) {
    struct Choice { std::size_t cell; double upper; MacEnergy quantum,fraction; };
    std::vector<Choice> choices; std::vector<double> nearest; MacEnergySum remaining;
    for (std::size_t n=0;n<cells.size();++n) {
        const auto cell=cells[n]; const double before=source.cells[cell].mixture.energy;
        MacEnergySum exact; exact.add(before); exact.add(changes[n]);
        const double rounded=exact.value().project().value;
        nearest.push_back(rounded);
        auto residual=exact; residual.add(-rounded);
        double lower=rounded,upper=rounded;
        if (residual.sign()>0) upper=std::nextafter(rounded,std::numeric_limits<double>::infinity());
        if (residual.sign()<0) lower=std::nextafter(rounded,-std::numeric_limits<double>::infinity());
        if (!std::isfinite(lower) || !std::isfinite(upper))
            throw FluidFatalError(FluidFatalCode::numerical_precision,"Conductive energy endpoint has no finite rounding bracket");
        target.cells[cell].mixture.energy=lower;
        remaining.add(before); remaining.add(-lower);
        if (upper!=lower) {
            const auto quantum=MacEnergy::from_double(upper-lower);
            auto fraction=exact; fraction.add(-lower);
            choices.push_back({cell,upper,quantum,fraction.value().shared(MacEnergy::from_double(1),quantum)});
        }
    }
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b) {
        if (a.quantum.exponent()!=b.quantum.exponent()) return a.quantum.exponent()>b.quantum.exponent();
        // Equal quanta: minimize endpoint rounding error before the stable
        // cell-index tie break. The fraction lies in (0,1).
        if (a.fraction.exponent()!=b.fraction.exponent()) return a.fraction.exponent()>b.fraction.exponent();
        if (a.fraction.mantissa()!=b.fraction.mantissa()) return a.fraction.mantissa()>b.fraction.mantissa();
        return a.cell<b.cell;
    });
    const auto unavailable=[&] {
        // Widely separated energy quanta need not admit an exactly closed
        // sum inside one neighboring value per cell. Retain the original
        // nearest projections and their existing explicit operand account.
        // Do not move a cell beyond its bracket to hide that limitation.
        for (std::size_t n=0;n<cells.size();++n) target.cells[cells[n]].mixture.energy=nearest[n];
        ++report.unavailable_rounding_components;
    };
    if (remaining.sign()<0) {unavailable();return;}
    for (const auto& choice : choices) {
        auto next=remaining; next.add(choice.quantum.negated());
        if (next.sign()<0) continue;
        target.cells[choice.cell].mixture.energy=choice.upper; remaining=std::move(next);
    }
    if (remaining.sign()!=0) {unavailable();return;}
    for (std::size_t n=0;n<cells.size();++n)
        if (target.cells[cells[n]].mixture.energy!=nearest[n]) ++report.conservative_rounding_adjustments;
}
void round_closed_heat(const MacTransportState& source,MacTransportState& target,
    const std::vector<std::size_t>& cells,const std::vector<double>& changes,const std::vector<ThermalContact>& contacts,
    MacThermalReport& report) {
    std::vector<std::size_t> parent(cells.size()); std::iota(parent.begin(),parent.end(),0);
    const auto root=[&](std::size_t n) { while (parent[n]!=n) {parent[n]=parent[parent[n]];n=parent[n];} return n; };
    for (const auto& contact : contacts) if (contact.conductance_w_per_k>0) parent[root(contact.second)]=root(contact.first);
    std::vector<std::vector<std::size_t>> component_cells(cells.size());
    std::vector<std::vector<double>> component_changes(cells.size());
    for (std::size_t n=0;n<cells.size();++n) {
        const auto component=root(n); component_cells[component].push_back(cells[n]); component_changes[component].push_back(changes[n]);
    }
    for (std::size_t n=0;n<cells.size();++n) if (!component_cells[n].empty())
        round_closed_heat_component(source,target,component_cells[n],component_changes[n],report);
}

// A mixed cell's elastic energy changes with temperature at fixed occupied
// volume. Freeze the face conductances for this stage, but solve each closed
// contact against the complete EOS instead of substituting its initial Cv.
// Forward/reverse half sweeps remove a preferred contact direction. This is
// a first-order contact-split backward-Euler operator, not a coupled graph BE
// solve. Every contact retains the two physical energy endpoints together.
MacThermalAdvance conduct_mixture_contacts(const FluidThermodynamics& eos,const MacTransportState& source,
    const std::vector<std::size_t>& cells,const std::vector<ThermalContact>& contacts,double seconds) {
    MacThermalAdvance result{source,{}};result.report.contacts=contacts.size();
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
            auto c=cold_initial,h=hot_initial;MacEnergySum ce,he;
            ce.add(c.energy);ce.add(q);he.add(h.energy);he.add(-q);
            c.energy=ce.value().require_representable();h.energy=he.value().require_representable();
            return std::pair(eos.evaluate(c,volume),eos.evaluate(h,volume));
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
        round_closed_heat_component(before,after,pair_cells,changes,result.report);
        requested.add(q);
        const std::array initial{cold_reading,hot_reading};
        for (std::size_t i=0;i<2;++i) {
            const auto& old=before.cells[i].mixture;const auto& next=after.cells[i].mixture;
            const auto reading=eos.evaluate(next,volume);const auto& anchor=initial[i];
            const double temperature_bound=128*std::numeric_limits<double>::epsilon()*hot_reading.temperature_k;
            if (reading.temperature_k<cold_reading.temperature_k-temperature_bound ||
                reading.temperature_k>hot_reading.temperature_k+temperature_bound)
                throw std::domain_error("Mixed conductive contact violated its maximum principle");
            MacEnergyAccount local;local.add(old.energy);local.add(changes[i]);local.add(-next.energy);
            if (!local.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conductive cell energy account failed");
            MacEnergySum change;change.add(next.energy);change.add(-old.energy);
            redistributed.add(change.value().magnitude().multiplied(.5));
            if (q!=0 && old.energy==next.energy) ++result.report.unresolved_heat_updates;
            long double delta=static_cast<long double>(anchor.intrinsic_heat_capacity_j_per_k)*
                std::log1p((static_cast<long double>(reading.temperature_k)-anchor.temperature_k)/anchor.temperature_k);
            if (anchor.has_gas) {
                long double mole_heat=0;
                const auto reference=eos.to_reference(old,volume);const auto& materials=eos.reference().materials();
                for (std::size_t k=0;k<materials.size();++k) if (materials[k].phase==ReferencePhase::ideal_gas)
                    mole_heat+=static_cast<long double>(reference.masses_kg[k])/materials[k].molar_mass_kg_per_mol*reference_gas_constant;
                delta+=mole_heat*std::log1p((static_cast<long double>(reading.gas_volume_m3)-anchor.gas_volume_m3)/anchor.gas_volume_m3);
            }
            entropy+=delta;
            entropy_bound+=128*std::numeric_limits<double>::epsilon()*(std::abs(old.energy)/anchor.temperature_k+
                std::abs(next.energy)/reading.temperature_k+anchor.intrinsic_heat_capacity_j_per_k);
        }
        result.state.cells[cold].mixture.energy=after.cells[0].mixture.energy;
        result.state.cells[hot].mixture.energy=after.cells[1].mixture.energy;
    };
    if (contacts.size()==1) exchange(contacts.front(),seconds);
    else {
        for (const auto& contact : contacts) exchange(contact,.5*seconds);
        for (auto it=contacts.rbegin();it!=contacts.rend();++it) exchange(*it,.5*seconds);
    }
    MacEnergyAccount balance;
    for (const auto cell : cells) {balance.add(source.cells[cell].mixture.energy);balance.add(-result.state.cells[cell].mixture.energy);}
    if (!balance.within_bound() || entropy < -entropy_bound)
        throw FluidFatalError(FluidFatalCode::numerical_precision,"Mixed conduction energy/entropy account failed");
    result.report.energy_residual=balance.residual();result.report.energy_bound=balance.bound();
    result.report.redistributed_energy=redistributed.value();result.report.requested_redistributed_energy=requested.value();
    result.report.entropy_change_j_per_k=static_cast<double>(entropy);
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
    for (std::size_t i=0;i<source.cells.size();++i) if (!grid.walls[i]) {
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
    if (cells.empty()) throw std::invalid_argument("MAC conduction has no occupied cells");
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
    round_closed_heat(source,result.state,cells,solved.energy_changes_j,contacts,result.report);
    MacEnergyAccount balance; MacEnergySum redistributed,requested_redistribution;
    long double entropy=0,entropy_bound=0;
    for (std::size_t n=0;n<cells.size();++n) {
        const auto cell=cells[n]; const auto before=source.cells[cell].mixture.energy; const auto change=solved.energy_changes_j[n];
        const auto after=result.state.cells[cell].mixture.energy;
        MacEnergyAccount local; local.add(before); local.add(change); local.add(-after);
        if (!local.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC conductive cell energy account failed");
        balance.add(before); balance.add(-after);
        requested_redistribution.add(MacEnergy::from_double(std::abs(change)).multiplied(.5));
        if (change != 0 && after == before) ++result.report.unresolved_heat_updates;
        const auto reading=eos.evaluate(result.state.cells[cell].mixture,grid.volume_m3());
        // Fixed mass/volume and constant Cv make deltaT=deltaU/C exact for
        // this operator. Subtracting two rounded EOS temperatures loses the
        // entropy of small, conservative heat transfers in dense liquids.
        MacEnergySum represented; represented.add(after); represented.add(-before);
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
