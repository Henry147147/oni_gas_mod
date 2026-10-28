#pragma once
#include "mac_transport.hpp"
#include "fluid_thermodynamics.hpp"
#include <limits>

namespace ma::experimental {
// A liquid constituent and its vapour (registry indices, same composition and
// molar mass). The vapour material's entropy offset places the pair in phase
// equilibrium (equal specific Gibbs energy) at a reference saturation point,
// so the saturation curve follows from the EOS itself (Clausius-Clapeyron with
// the EOS's own temperature-dependent latent heat).
// critical_temperature_k: at or above it the pair has no liquid (none
// condenses, and liquid evaporates); infinity keeps every temperature.
struct MacSaturationPair { std::size_t liquid = 0, vapour = 0; double critical_temperature_k = std::numeric_limits<double>::infinity(); };
// Saved phase-change rule (MAC_PHASE_CHANGE.md). Value 0: the liquid and its
// vapour relax to equilibrium at the vapour's Dalton partial pressure, so a
// liquid evaporates into unsaturated gas and the gas holds equilibrium
// humidity. Value 1 (boil-only, user decision 2026-10-28): the comparison
// uses the cell's total gas pressure, so a liquid vaporises only above its
// boiling point at that pressure and vapour below it condenses (as in ONI).
// A cell whose only gas is the vapour is unaffected. Both keep U, momentum,
// passives and every mass account.
enum class MacPhaseRule { equilibrium = 0, boil_only = 1 };
// The vapour entropy offset that puts liquid and vapour in equilibrium at the
// given temperature and pressure (liquid entropy offset zero).
[[nodiscard]] double mac_saturation_entropy_offset(const ReferenceMaterial& liquid, const ReferenceMaterial& vapour,
    double temperature_k, double pressure_pa);
// Vapour partial pressure in equilibrium with the liquid at temperature T,
// the liquid being at pressure liquid_pressure_pa (the cell pressure). The
// materials are EOS profile indices.
[[nodiscard]] double mac_saturation_pressure(const FluidThermodynamics&, std::size_t liquid_material, std::size_t vapour_material,
    double temperature_k, double liquid_pressure_pa);
// Temperature at which the liquid (at liquid_pressure_pa) is in equilibrium
// with its vapour at partial pressure vapour_pressure_pa: the inverse of
// mac_saturation_pressure in T, by Newton on ln p_sat from guess_k.
[[nodiscard]] double mac_saturation_temperature(const FluidThermodynamics&, std::size_t liquid_material, std::size_t vapour_material,
    double vapour_pressure_pa, double liquid_pressure_pa, double guess_k);
// Specific latent heat h_v - h_l at temperature T (vapour ideal-gas enthalpy;
// liquid enthalpy at liquid_pressure_pa, elastic energy and p v included).
[[nodiscard]] double mac_latent_heat(const FluidThermodynamics&, std::size_t liquid_material, std::size_t vapour_material,
    double temperature_k, double liquid_pressure_pa);
// Sharp-interface boiling (MAC_PHASE_CHANGE.md, "Sharp interfaces") applies
// to a cell whose only liquid is a pair's liquid, with at least this fraction
// of the cell in each phase, and whose gas is mostly that pair's vapour.
inline constexpr double mac_interface_phase_fraction = 1e-6;
inline constexpr double mac_interface_vapour_fraction = .5;
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
// With skip_sharp_interfaces, cells that sharp-interface boiling converts
// (mac_interface_phase_fraction, mac_interface_vapour_fraction) are left as
// they are.
// With fast_trial_eos, root trials use the production EOS, refining uncertain
// states with the reference. Accepted endpoints and entropy remain strict.
// With fraction < 1 each cell moves only that fraction of the way to
// equilibrium (a first-order relaxation over the caller's interval).
// With MacPhaseRule::boil_only the vapour's partial pressure is replaced by
// the cell's total gas pressure. That relaxation does not maximize the
// mixture entropy (it condenses unsaturated vapour), so its entropy check
// uses the entropy with each gas taken at the total gas pressure: the
// mixture entropy less the ideal entropy of mixing, which this rule never
// decreases (MAC_PHASE_CHANGE.md, "Boil-only rule").
[[nodiscard]] MacPhaseChangeAdvance advance_mac_phase_change(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const MacSaturationPair>, bool skip_sharp_interfaces = false, double fraction = 1, bool fast_trial_eos = false,
    MacPhaseRule rule = MacPhaseRule::equilibrium);
// Restrict that operation to these cells; an empty list changes no cells.
[[nodiscard]] MacPhaseChangeAdvance advance_mac_phase_change_in_cells(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, std::span<const MacSaturationPair>, std::span<const std::size_t> cells, double fraction = 1, bool fast_trial_eos = false,
    MacPhaseRule rule = MacPhaseRule::equilibrium);
}
