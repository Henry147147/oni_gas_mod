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
- The reservoir is 100 times the film's growth, so the closed domain's pressure
  rises about 1% over [t0, 10 t0].

**Blocker.** Coupled mixed-cell conduction (2026-10-20) replaced pairwise contact
splitting, but it barely moved the film: 14% short after 0.5 t0 on the coarse grid,
against 16% before. Conduction was not the main loss. In a heat budget of the coarse
run, the wall delivers about 12 kW/m², evaporation absorbs about 1.8 kW/m², and the
rest warms the water. That water holds no gas and stays below the nucleation
superheat, so it cannot boil. Three mechanisms:
- **The fixed-volume relaxation self-limits.** Vapour formed in the interface cell
  raises that cell's pressure, and with it the saturation temperature (3.58 kPa/K).
  In a half-full coarse cell, about 1% of the arriving heat evaporates per
  relaxation; the rest warms the cell. The cell's liquid, the liquid slug's mass and
  that pressure–temperature slope form an undamped oscillator, with a period of
  about 80 μs on the coarse grid. Started at the exact liquid velocity, the slug
  stops within about 11 μs and then oscillates, and the water's pressure swings by
  thousands of pascals.
- **The water is very sensitive to the interface temperature.** It conducts 24
  times better than steam, so a liquid 1% of the wall superheat below the interface
  temperature takes about a fifth of the vapour-side flux
  ((k_l/k_v)·√(α_v/α_l)·erf λ ≈ 21). Each pressure swing moves the saturation
  temperature by tenths of a kelvin.
- **Shock-heat regularization.** The default regularization activates in the
  compressing cells at the interface and moves more heat into the water. With it
  off, the error at 1.3 ms falls from 10.8% to 7.6%.

Zero water conductivity alone leaves the water warming at the same rate; with the
regularization also off the early error is 2%, but the steam-side flux then falls
short when the interface cell is mostly liquid, because its volume-weighted
conductivity is small. A 10 times larger reservoir changed nothing over the first
0.5 t0. Over the whole run, though, the 100 times reservoir rises about 1.4 kPa
adiabatically (0.4 K of saturation temperature), which the liquid-side sensitivity
would make significant. The fixture also starts the liquid at rest rather than at
the exact solution's velocity.

M5 therefore needs evaporation coupled to the flow: interfacial mass transfer from
the interface heat balance at the interface's own pressure, with the new vapour
volume entering the pressure stage, instead of a fixed-volume equilibrium relaxation
after it.
