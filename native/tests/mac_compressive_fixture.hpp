#pragma once
#include "mac_fixture.hpp"
#include "mixed_atmospheres/mac_compressive_work.hpp"

namespace ma::testing {
inline void export_compressive_definitions(int profile,const FluidThermodynamics& model) {
    for(const auto& d:model.reference().materials())
        std::cout<<"CWDEF "<<profile<<' '<<setup().registry.index(d.id)<<' '<<
            (d.phase==ReferencePhase::ideal_gas ? 0 : 1)<<' '<<d.molar_mass_kg_per_mol<<' '<<d.cv_j_per_kg_k<<' '<<
            d.energy_offset_j_per_kg<<' '<<d.zero_pressure_density_kg_per_m3<<' '<<d.reference_bulk_modulus_pa<<' '<<
            d.minimum_temperature_k<<' '<<d.maximum_temperature_k<<'\n';
}
inline void export_compressive_response(int id,int profile,const MixtureInventory& source,
    const experimental::ReferenceEquilibriumState& before,double v,double dv,double h,double dt,
    experimental::MacEnergy linear,const experimental::MacCompressiveWork& r) {
    const auto scaled=[](experimental::MacEnergy x) { std::cout<<' '<<x.mantissa()<<' '<<x.exponent(); };
    std::cout<<"CWCASE "<<id<<' '<<profile<<' '<<v<<' '<<dv<<' '<<h<<' '<<dt;scaled(linear);
    std::cout<<' '<<static_cast<int>(r.unresolved_energy_update)<<'\n';
    std::cout<<"CWINIT "<<id<<' '<<source.energy<<' '<<before.mechanical_pressure_pa<<' '<<before.temperature_k<<' '<<before.entropy_j_per_k<<'\n';
    std::cout<<"CWEND "<<id<<' '<<r.isentropic.endpoint.volume_m3<<' '<<r.mixture.energy<<' '<<r.state.mechanical_pressure_pa<<' '<<
        r.state.temperature_k<<' '<<r.state.entropy_j_per_k<<'\n';
    std::cout<<"CWISO "<<id<<' '<<r.isentropic.internal_energy_change_j<<' '<<r.isentropic.work_pressure_pa<<' '<<
        static_cast<int>(r.isentropic.endpoint_volume_resolves_change)<<' '<<static_cast<int>(r.isentropic.endpoint_energy_resolves_change)<<'\n';
    for(std::size_t s=0;s<source.masses.size();++s) if(source.masses[s]!=0 || r.mixture.masses[s]!=0)
        std::cout<<"CWMASS "<<id<<' '<<s<<' '<<source.masses[s]<<' '<<r.mixture.masses[s]<<'\n';
    std::cout<<"CWFULL "<<id;
    for(const auto& x:{r.stress_pa,r.stress_volume_derivative_pa_per_m3,r.dissipated_heat_j,r.total_work_pressure_pa,
        r.work_pressure_volume_derivative_pa_per_m3,r.requested_energy_change_j,r.energy_residual_j,r.energy_bound_j,
        r.entropy_change_j_per_k,r.entropy_allowance_j_per_k}) scaled(x);
    std::cout<<'\n';
}
}
