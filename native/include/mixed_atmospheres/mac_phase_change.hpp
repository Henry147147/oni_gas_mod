#pragma once
#include "mac_transport.hpp"
#include "fluid_thermodynamics.hpp"

namespace ma::experimental {
// A liquid constituent and its vapour (registry indices, same composition and
// molar mass). The vapour material's entropy offset places the pair in phase
// equilibrium (equal specific Gibbs energy) at a reference saturation point,
// so the saturation curve follows from the EOS itself (Clausius-Clapeyron with
// the EOS's own temperature-dependent latent heat).
struct MacSaturationPair { std::size_t liquid = 0, vapour = 0; };
// The vapour entropy offset that puts liquid and vapour in equilibrium at the
// given temperature and pressure (liquid entropy offset zero).
[[nodiscard]] double mac_saturation_entropy_offset(const ReferenceMaterial& liquid, const ReferenceMaterial& vapour,
    double temperature_k, double pressure_pa);
// Vapour partial pressure in equilibrium with the liquid at temperature T,
// the liquid being at pressure liquid_pressure_pa (the cell pressure). The
// materials are EOS profile indices.
[[nodiscard]] double mac_saturation_pressure(const FluidThermodynamics&, std::size_t liquid_material, std::size_t vapour_material,
    double temperature_k, double liquid_pressure_pa);
// Every registry liquid with a vapour of the same composition whose EOS
// material carries a saturation calibration (nonzero entropy offset).
[[nodiscard]] std::vector<MacSaturationPair> mac_saturation_pairs(const SpeciesRegistry&, const FluidThermodynamics&);
// Superheat above saturation at its own pressure before a gas-free liquid cell
// nucleates vapour (onset of nucleate boiling for water is a few kelvin).
inline constexpr double mac_nucleation_superheat_k = 5;
struct MacPhaseChangeReport {
    std::size_t evaporating_cells = 0, condensing_cells = 0, eos_evaluations = 0, unevaluable_cells = 0;
    double evaporated_kg = 0, condensed_kg = 0;
    double entropy_change_j_per_k = 0, entropy_allowance_j_per_k = 0;
};
struct MacPhaseChangeAdvance { MacTransportState state; MacPhaseChangeReport report; };
// Each cell holding a pair relaxes, at fixed U and occupied volume, to phase
// equilibrium: liquid evaporates while the vapour's partial pressure is below
// saturation and vapour condenses while above, until they match or one phase
// is used up. A cell without gas holds its liquid unless it is superheated by
// more than the nucleation superheat at its own pressure; the vapour it then
// forms raises that pressure toward saturation. Only the pair's masses change; momentum, passives and U are
// retained, and entropy cannot fall beyond its stored-U roundoff allowance.
[[nodiscard]] MacPhaseChangeAdvance advance_mac_phase_change(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const MacSaturationPair>);
}
