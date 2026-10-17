#pragma once
#include "mac_transport.hpp"

namespace ma::experimental {
struct MacThermalReport {
    std::size_t contacts=0, iterations=0, unresolved_heat_updates=0;
    std::size_t conservative_rounding_adjustments=0, unavailable_rounding_components=0;
    MacEnergy energy_residual, energy_bound, redistributed_energy, requested_redistributed_energy;
    double entropy_change_j_per_k=0;
};
struct MacThermalAdvance { MacTransportState state; MacThermalReport report; };
// Closed finite-volume, backward-Euler conduction with harmonic face
// resistance. Single-phase cells retain the constant-capacity graph solve.
// A graph containing gas/liquid cells uses complete-EOS, closed pairwise
// backward-Euler contact solves (symmetric half sweeps for multiple contacts).
// Conductivity is frozen at the beginning of each stage. No heat crosses
// walls/exterior. Gas conductivity is a declared mole-weighted
// reference, liquid conductivity is volume-weighted. Species/passives and
// canonical momentum are retained. No phase transitions or solid adapter.
// Each connected component uses conservative neighboring rounding when that
// lattice admits its exact closed U sum. Otherwise nearest endpoints retain
// the original explicit energy account. Sub-ULP heat is reported unresolved;
// redistributed_energy counts only changes represented in cell U.
[[nodiscard]] MacThermalAdvance advance_mac_conduction(const SpeciesRegistry&,const FluidThermodynamics&,
    const MacTransportState&,double seconds);
}
