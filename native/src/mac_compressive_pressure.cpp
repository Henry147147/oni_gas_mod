#include "mixed_atmospheres/mac_compressive_work.hpp"
#include "occupied_material_work_detail.hpp"
#include "mac_compressive_work_detail.hpp"
#include <cmath>

namespace ma::experimental {
MacCompressivePressureStep advance_mac_compressive_pressure(const FluidThermodynamics& model,
    std::span<const MaterialCell> cells,std::span<const OccupiedMaterialNode> nodes,double dt,double spacing,
    std::span<const MacEnergy> linear,PressurePreconditioner pc,double tolerance,ReferenceRootOptions options) {
    require_mac_energy_environment();
    if (linear.size()!=cells.size() || !std::isfinite(spacing) || spacing<=0 || !std::isfinite(dt) || dt<=0)
        throw std::invalid_argument("Invalid coupled compressive pressure coefficients");
    for (const auto& value : linear) if (value.mantissa()<0) throw std::invalid_argument("Negative coupled compressive pressure coefficient");
    const auto response=[&](std::size_t cell,double change) {
        return evaluate_mac_compressive_work(model,cells[cell].mixture,cells[cell].volume_m3,change,spacing,dt,linear[cell],options,cells[cell].energy_remainder_j);
    };
    const detail::OccupiedWorkLaw law=[&](std::size_t cell,double,const ReferenceIsentrope& anchor,
        const ReferenceIsentropicResponse& isentropic,double derivative,std::optional<double> increment) {
        const auto r=compression_detail::finish(model,cells[cell].mixture,anchor,isentropic,derivative,spacing,dt,linear[cell],cells[cell].energy_remainder_j);
        return detail::OccupiedWorkResponse{r.total_work_pressure_pa.project().value,r.work_pressure_volume_derivative_pa_per_m3.project().value,
            r.requested_energy_change_j.project().value,r.mixture.energy,r.unresolved_energy_update,
            compression_detail::pressure_increment(r.stress_pa,increment),r.energy_remainder_j,r.primary_energy};
    };
    MacCompressivePressureStep result;
    result.pressure=detail::advance_occupied_material_pressure_with_work(model,cells,nodes,dt,pc,
        LiquidFillRoundoffPolicy::strict_eos,tolerance,options,law);
    result.responses.reserve(cells.size());
    for (std::size_t i=0;i<cells.size();++i) {
        auto r=response(i,result.pressure.volume_changes_m3[i]);
        if (r.mixture.masses!=result.pressure.cells[i].mixture.masses || r.mixture.energy!=result.pressure.cells[i].mixture.energy ||
            !same_mac_stored_remainder(r.energy_remainder_j,result.pressure.cells[i].energy_remainder_j) ||
            r.total_work_pressure_pa.project().value!=result.pressure.work_pressures_pa[i])
            throw std::runtime_error("Coupled pressure publication differs from its completed constitutive response");
        result.responses.push_back(std::move(r));
    }
    return result;
}
}
