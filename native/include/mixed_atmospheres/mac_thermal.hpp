#pragma once
#include "mac_transport.hpp"

namespace ma::experimental {
struct MacThermalReport {
    std::size_t contacts=0, iterations=0, unresolved_heat_updates=0;
    MacEnergy energy_residual, energy_bound, redistributed_energy;
    double entropy_change_j_per_k=0;
};
struct MacThermalAdvance { MacTransportState state; MacThermalReport report; };
// Closed finite-volume, backward-Euler conduction with harmonic face
// resistance. Constant-capacity, single-phase occupied cells only; no heat
// crosses walls/exterior. Gas conductivity is a declared mole-weighted
// reference, liquid conductivity is volume-weighted. Species/passives and
// canonical momentum are retained. No phase transitions or solid adapter.
[[nodiscard]] MacThermalAdvance advance_mac_conduction(const SpeciesRegistry&,const FluidThermodynamics&,
    const MacTransportState&,double seconds);
}
