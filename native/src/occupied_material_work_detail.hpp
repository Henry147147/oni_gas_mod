#pragma once
#include "mixed_atmospheres/volume_work.hpp"
#include <functional>

namespace ma::detail {
// Private extension point for a complete constitutive pressure-work response.
// The public isentropic entry supplies no override and retains its old path.
struct OccupiedWorkResponse {
    double pressure_pa, derivative_pa_per_m3, requested_energy_change_j, stored_energy_j;
    bool unresolved_energy_update;
};
using OccupiedWorkLaw=std::function<OccupiedWorkResponse(std::size_t,double)>;
[[nodiscard]] OccupiedMaterialPressureStep advance_occupied_material_pressure_with_work(
    const FluidThermodynamics&,std::span<const MaterialCell>,std::span<const OccupiedMaterialNode>,double,
    PressurePreconditioner,LiquidFillRoundoffPolicy,double,experimental::ReferenceRootOptions,const OccupiedWorkLaw&);
}
