# Liquid/vapour phase change

`MacClosedSettings::phase_change = MacPhaseChange::saturation` (persisted value 1)
boils and condenses calibrated liquid/vapour pairs at a pressure-dependent
saturation point (user decision, 2026-10-19). Water boils at 100 °C at 1 atm, at
lower temperatures under lower pressure and higher under pressure. Water in vacuum
or a thin atmosphere therefore boils at room temperature, unlike ONI's fixed
boiling point.

Code: `mac_phase_change.hpp`/`.cpp`; the entropy offset in `liquid_reference.cpp`.

## Saturation from the EOS

A pair is a registry liquid and a gas with the same composition and molar mass
(water and steam). `ReferenceMaterial::entropy_offset_j_per_kg_k` adds a constant to
the vapour's specific entropy. `mac_saturation_entropy_offset` chooses it so the two
phases have equal specific Gibbs energy at a reference point, here 373.15 K and
101325 Pa. The offset is zero for every other material. It enters the model identity
and checkpoints only when nonzero, so uncalibrated models are byte-for-byte unchanged.
A constant offset cancels in every process that keeps each constituent's mass, so it
only affects phase change.

`mac_saturation_pressure(T, P)` is the vapour partial pressure at which the
liquid (at pressure P) and its vapour have equal Gibbs energy. It follows from the
EOS's own energies and entropies: an integrated Clausius–Clapeyron law with the EOS's
temperature-dependent latent heat. For the standard water/steam constants
(`ma_mac_phase_change_tests`):

| Temperature | EOS saturation pressure | Steam tables |
| ---: | ---: | ---: |
| 298.15 K | 3.214 kPa | 3.169 kPa |
| 373.15 K | 101.325 kPa (calibration) | 101.325 kPa |
| 423.15 K | 463.4 kPa | 476.2 kPa |

The slope at 373.15 K is 3.577 kPa/K, which is Clausius–Clapeyron with the EOS's
latent heat of 2.268 MJ/kg (steam tables: 3.62 kPa/K, 2.257 MJ/kg).

## Equilibrium relaxation

`advance_mac_phase_change` relaxes each cell that holds a pair, at fixed U and
occupied volume:
- **Evaporation:** liquid evaporates while the vapour's Dalton partial pressure is
  below saturation.
- **Condensation:** vapour condenses while above.
- **End point:** this stops when the two match, or when one phase is used up. The
  mass moved is found by bisection.

Only the pair's masses change. U, momentum, passives and every energy account hold,
and an entropy decrease beyond 128 epsilons of |U|/T is rejected.

Evaporation needs an interface: a gas phase of at least a millionth of the cell.
Smaller gas is a trace. Otherwise a liquid evaporates only when superheated by more
than 5 K at its own pressure (`mac_nucleation_superheat_k`, about the onset of
nucleate boiling for water). Without this, pressure ripples of a few pascals in water
held at saturation flashed micro-bubbles of about 1e-11 kg throughout a water slug.
Vapour condenses wherever it is.

With saturation tied to pressure, boiling limits itself. Vapour formed in a nearly
full cell raises that cell's pressure only to the saturation pressure, and then stops.
A fixed boiling point instead forced evaporation regardless of pressure: in a
prototype, a full water cell reached about 10 bar and the water slug cavitated.

## Where it runs

The reference path relaxes after every accepted substep. The production kernel has
no phase change, so the owner relaxes once after each production interval (operator
splitting per interval). Its entropy change joins the interval report. Fluid point
transfers, diffusion and the void phase combine with phase change only as they
already do with liquids.

Phase-change owners do not checkpoint yet.

## Gate M5

M5 is open. `ma_mac_multiphase_gates M5 --run` (or `--grid N` for one grid) runs it on
the production kernel. The 1-D setup:
- a heater cell, a liquid whose huge heat capacity and metal-like conductivity hold
  T_sat + 10 K at its face;
- a steam film started from the exact Stefan solution at t0 = 1 ms (19 μm thick);
- water at T_sat;
- a non-volatile liquid plug;
- a nitrogen reservoir 100 times the film's growth.

The interface position is the film's steam volume per unit area, compared with
2λ√(αt) over [t0, 10 t0] with λ = 0.0640 and α = 2.28e-5 m²/s.

Four setup lessons:
- Water at 100 °C next to dry air evaporates fast, as it physically should; hence
  the plug.
- An owner-level wall-heat exchange rebuilds the world, and its strict EOS rejects
  the subnormal gas traces the production kernel admits; hence the heater cell.
- The strict reference path rejects those traces in transport; hence the
  production kernel.
- A 1% pressure rise in the closed domain shifts T_sat by 0.28 K, which costs about
  2% of the interface position; hence the large reservoir.

**Blocker.** The film grows at about a tenth of the exact rate: 16% short after
0.5 t0 on the coarse grid. At the interface, conduction delivers about 2 kW/m² where
the film's gradient implies about 9 kW/m². Whenever a gas/liquid cell is present,
conduction switches from the coupled backward-Euler graph solve to first-order
pairwise contact splitting, in both kernels. At the film's diffusion number α Δt/h²
of about 6 (10 cells across δ(t0), Δt = t0/1000), each contact lets the low-capacity
steam cell nearly equilibrate with the interface cell within the step. That limits
the heat it passes. The diffusion number grows with the square of the resolution,
so refinement does not help. M5 needs a coupled mixed-cell conduction solve, for
example the graph solve with each mixed cell's equilibrium heat capacity, corrected
against the complete EOS.

