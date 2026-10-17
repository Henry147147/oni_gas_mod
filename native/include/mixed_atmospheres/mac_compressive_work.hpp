#pragma once
#include "fluid_thermodynamics.hpp"
#include "mac_energy.hpp"

namespace ma::experimental {
// A local constitutive response, not a pressure solve or an owning timestep.
// q is a compressive bulk stress. Its work -q*dV is added to actual material U
// in the same candidate as the isentropic pressure work. No heat is imposed on
// an expanding material volume. The quadratic coefficient is exactly one;
// linear_speed is an explicitly frozen, nonnegative incoming coefficient.
struct MacCompressiveWork {
    MixtureInventory mixture;
    ReferenceEquilibriumState state;
    ReferenceIsentropicResponse isentropic;
    MacEnergy stress_pa, stress_volume_derivative_pa_per_m3, dissipated_heat_j;
    MacEnergy total_work_pressure_pa, work_pressure_volume_derivative_pa_per_m3;
    MacEnergy requested_energy_change_j, energy_residual_j, energy_bound_j;
    MacEnergy entropy_change_j_per_k, entropy_allowance_j_per_k;
    bool unresolved_energy_update = false;
};

// s=max(-dV/V0,0), a=h/dt, rho=M/V0:
// q=rho*(linear_speed*a*s + a*a*s*s), Q=q*(-dV).
// V0, h and dt remain fixed when differentiating the response with respect to
// dV. At dV=0 the returned tangent is the compressive one-sided derivative.
// Reference energies may be negative. The source and model remain unchanged
// after success or failure; the returned EOS reads the actual stored endpoint.
[[nodiscard]] MacCompressiveWork evaluate_mac_compressive_work(
    const FluidThermodynamics&, const MixtureInventory&, double source_volume_m3,
    double volume_change_m3, double spacing_m, double timestep_s,
    MacEnergy linear_speed_m_per_s, ReferenceRootOptions isentropic_root_options = {});
}
