#include "mixed_atmospheres/mac_thermal.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "mixed_atmospheres/thermal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ma::experimental {
MacThermalAdvance advance_mac_conduction(const SpeciesRegistry& registry,const FluidThermodynamics& eos,
    const MacTransportState& source,double seconds) {
    require_mac_energy_environment();
    if (!std::isfinite(seconds) || seconds <= 0) throw std::invalid_argument("Invalid MAC conduction timestep");
    (void)mac_face_inertias(registry,source,eos); // Validate complete canonical input before reading geometry.
    const auto& grid=source.grid; const auto absent=std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> nodes(source.cells.size(),absent),cells;
    std::vector<ThermalBody> bodies; std::vector<double> conductivity;
    for (std::size_t i=0;i<source.cells.size();++i) if (!grid.walls[i]) {
        const auto& mixture=source.cells[i].mixture; const auto reading=eos.evaluate(mixture,grid.volume_m3());
        const auto capacity=reading.equilibrium_heat_capacity_at_cell_volume_j_per_k;
        if ((reading.has_gas && reading.liquid_volume_m3 > 0) ||
            !std::isfinite(capacity) || capacity <= 0 ||
            std::abs(capacity-reading.intrinsic_heat_capacity_j_per_k) > 64*std::numeric_limits<double>::epsilon()*capacity)
            throw FluidFatalError(FluidFatalCode::unsupported_topology,"MAC conduction needs a single-phase constant-capacity cell");
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
    ThermalResult solved;
    try { solved=conduct_heat(bodies,contacts,seconds); }
    catch (const std::runtime_error& error) { throw std::domain_error(std::string("MAC conductive solve rejected: ")+error.what()); }
    MacThermalAdvance result{source,{}}; result.report.contacts=contacts.size(); result.report.iterations=solved.iterations;
    MacEnergyAccount balance; MacEnergySum redistributed;
    long double entropy=0,entropy_bound=0;
    for (std::size_t n=0;n<cells.size();++n) {
        const auto cell=cells[n]; const auto before=source.cells[cell].mixture.energy; const auto change=solved.energy_changes_j[n];
        MacEnergySum next; next.add(before); next.add(change);
        result.state.cells[cell].mixture.energy=next.value().project().value;
        const auto after=result.state.cells[cell].mixture.energy;
        MacEnergyAccount local; local.add(before); local.add(change); local.add(-after);
        if (!local.within_bound()) throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC conductive cell energy account failed");
        balance.add(before); balance.add(-after);
        redistributed.add(MacEnergy::from_double(std::abs(change)).multiplied(.5));
        if (change != 0 && after == before) ++result.report.unresolved_heat_updates;
        const auto reading=eos.evaluate(result.state.cells[cell].mixture,grid.volume_m3());
        entropy+=static_cast<long double>(bodies[n].heat_capacity_j_per_k)*std::log1p((reading.temperature_k-bodies[n].temperature_k)/bodies[n].temperature_k);
        entropy_bound+=128*std::numeric_limits<double>::epsilon()*(std::abs(before)/bodies[n].temperature_k+
            std::abs(after)/reading.temperature_k+bodies[n].heat_capacity_j_per_k);
    }
    if (!balance.within_bound() || entropy < -entropy_bound)
        throw FluidFatalError(FluidFatalCode::numerical_precision,"MAC conduction energy/entropy account failed");
    result.report.energy_residual=balance.residual(); result.report.energy_bound=balance.bound();
    result.report.redistributed_energy=redistributed.value(); result.report.entropy_change_j_per_k=static_cast<double>(entropy);
    return result;
}
}
