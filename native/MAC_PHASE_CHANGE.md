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

## Sharp interfaces

Boiling at a liquid surface is limited by the heat reaching the surface, not by
equilibrium in a cell. The production kernel therefore converts boiling interfaces
inside each substep, with conduction on (user decision, 2026-10-20). The approach
follows sharp-interface boiling methods ([Welch & Wilson 2000](https://doi.org/10.1006/jcph.2000.6481);
[Tanguy et al. 2014](https://doi.org/10.1016/j.jcp.2014.01.014)).

**Interface cells.** A cell holds a boiling interface when all of the following hold:
- its only liquid is a pair's liquid;
- gas and liquid each fill at least a millionth of it (`mac_interface_phase_fraction`);
- that liquid's vapour is at least half of its gas moles (`mac_interface_vapour_fraction`);
- it borders the other phase: a mostly-liquid cell beside a mostly-gas cell, or the reverse.

Without the last rule, trace gas smeared into a liquid's interior became spurious
interfaces. Across a face where transport has left a liquid film in a mostly-gas cell
and a gas film in the mostly-liquid cell beside it, only the cell with the larger
minority phase holds the interface.

**Conduction** runs once per substep, before the pressure stage; backward Euler is
first order either way.
- **Held temperature.** Each interface cell is held at T_sat of its vapour partial
  pressure, using the mean pressure of its mostly-gas neighbours. Pressure is
  continuous across a flat interface, and a nearly full cell's own pressure is too
  stiff: when it was used, the interface temperature rang by several kelvin.
- **Interface distance.** A neighbour conducts through its own half cell plus the
  interface cell's layer of the neighbour's phase, (phase volume fraction) × h deep.
  It conducts with that phase's conductivity, not a volume-weighted average.
- **Heat.** The heat Q conducted into the held cell enters its U as usual.

**Conversion** happens in the pressure stage, so the new vapour pushes the liquid
within the same implicit solve rather than spiking the cell's pressure:
- **Isentrope.** The cell's isentrope starts from its state before the conducted heat
  (U − Q), so that heat cannot raise the starting pressure; in a mostly-gas cell it
  raised it by hundreds of pascals.
- **Volume change.** The unknown is the total change D = x + s:
  - x along the isentrope;
  - then the vapour volume s = Δm (R T_int / (M p1) − 1/ρ_l(p1)) of converting Δm at
    the endpoint pressure p1.
- **Converted mass.** Δm = (Q − G dT_sat/dp Δp1) / L(T_cell), clamped to the
  available phase:
  - G is the interface's integrated face conductance;
  - L is taken at the cell's own temperature, so conversion leaves that temperature
    unchanged.
  - The −G dT_sat/dp Δp1 term makes the interface temperature follow the endpoint
    pressure. Held at the start pressure instead, the coupling was an explicit loop
    whose gain grows like dt/h; it rang at the 20-cell grid and would be unstable at
    40.
- **Inner solve.** x + s(x) = D is solved by Newton, because the coupling makes s far
  more sensitive to x than x itself.
- **Exact energy.** The face force uses the mean work pressure (W(x) + p1 s)/D,
  formed in increments, so U + K closes exactly.
- **Interval audit.** The kernel's audit takes each species' converted mass as an
  external change, with every write's rounding.
- **Entropy.** Production entropy now includes the vapour's calibrated entropy
  offset. That constant was irrelevant until mass changed phase.

**Elsewhere.** The per-interval equilibrium relaxation leaves interface cells alone
when conduction is on. Every other pair cell still relaxes, including traces and
nucleation in a liquid's interior. Shock-heat regularization never crosses a
gas/liquid interface ([heat](MAC_HEAT.md#phase-interfaces)).

## Where it runs

The reference path relaxes after every accepted substep. The production kernel
converts sharp boiling interfaces in each substep and relaxes every other pair cell
once after each interval (operator splitting per interval). Its entropy change
joins the interval report. Sharp-interface boiling is production-only: the reference
path still uses the equilibrium relaxation for interfaces too, and cannot run the
[M5 gate](#gate-m5), because its strict transport rejects the traces M5 creates.
Fluid point transfers, diffusion and the void phase combine with phase change only
as they already do with liquids.

Phase-change owners checkpoint as [version 11](MAC_CHECKPOINT.md#version-assignment).
With the void phase, liquid resting in vacuum boils into its cell's void
([vacuum](MAC_VACUUM.md#phase-change)).

An owner without phase change in its settings (the colony owner, whose
production kernel runs the void phase) can relax between intervals with
`MacClosedWorld::with_phase_relaxation(pairs, fraction)`: the same equilibrium
relaxation, each cell moving `fraction` of the way to equilibrium. A pair's
`critical_temperature_k` (infinite by default) bounds its liquid: at or above
it none condenses and liquid evaporates, since the elastic-liquid saturation
curve otherwise continues far past the critical point. Neither changes any
existing owner's arithmetic.

## Gate M5

M5 passes (2026-10-20). `ma_mac_multiphase_gates M5 --run` runs the full gate on the
production kernel, on grids of 10, 20 and 40 cells across the initial film. `--quick`
runs a 5-cell grid with a 4% bound as the CTest `mac_gate_m5_stefan_coarse`; `--grid N`
runs one grid as a diagnostic.

The 1-D setup:
- a heater cell, a liquid whose huge heat capacity and metal-like conductivity hold
  T_sat + 10 K at its face;
- a steam film started from the exact Stefan solution at t0 = 1 ms (19 μm thick),
  with the liquid moving away at the exact speed (1 − ρ_v/ρ_l) dδ/dt;
- water at T_sat;
- a non-volatile liquid plug;
- a nitrogen reservoir 100 times the film's growth.

The interface position is the film's steam volume per unit area, compared with
2λ√(αt) over [t0, 10 t0] with λ = 0.0640 and α = 2.28e-5 m²/s. The gate passes when:
- the worst error on the 40-cell grid is within 2%;
- the error falls with each refinement;
- water and steam together keep their mass to 1e-12.

| Cells across δ(t0) | Worst error | Water mass drift |
| --- | --- | --- |
| 5 (`--quick`) | 3.17% | 3.7e-16 |
| 10 | 1.36% | 1.0e-16 |
| 20 | 0.87% | 6.4e-17 |
| 40 | 0.73% | 8.9e-17 |

The errors approach a small positive offset rather than zero:
- The analytic solution has constant properties, with steam at its saturation
  density everywhere.
- The model's steam is an ideal gas up to 10 K hotter near the wall, so the same
  evaporated mass fills about 0.5–1% more volume.
- A mass-based measure (steam mass over the saturation density) gives −0.1% on the
  10-cell grid and −0.45% on the 20-cell grid.

Five setup lessons:
- Water at 100 °C next to dry air evaporates fast, as it physically should; hence
  the plug.
- An owner-level wall-heat exchange rebuilds the world, and its strict EOS rejects
  the subnormal gas traces the production kernel admits; hence the heater cell.
- The strict reference path rejects those traces in transport; hence the
  production kernel.
- The closed domain stands in for 1 atm. The film's growth would still compress the
  reservoir by about 1.4 kPa, which is 0.4 K of saturation temperature. Water
  conducts 24 times better than steam, so water that far below the interface
  temperature takes much of the film's heat. After each interval the reservoir
  therefore vents nitrogen: its readable pure-nitrogen cells are scaled by one
  common factor that returns their median pressure to 1 atm. Resetting each cell
  separately created a pressure step at the plug.
- Started at rest, the liquid slug first has to be accelerated by the film, which
  rings; the fixture starts it at the exact speed.

**History.** The equilibrium relaxation alone, run after each pressure stage,
could not pass M5. Coupled mixed-cell conduction (2026-10-20) barely moved the
coarse film (14% short after 0.5 t0, against 16% before). In a heat budget of that
run:
- the wall delivered about 12 kW/m²;
- evaporation absorbed about 1.8 kW/m²;
- the rest warmed the water.

Vapour formed at fixed volume raised the interface cell's pressure and its
saturation temperature (3.58 kPa/K). Only about 1% of the arriving heat boiled per
relaxation, and the cell, the liquid slug and that slope formed an undamped
oscillator. [Sharp interfaces](#sharp-interfaces) replaced it. Shock-heat
regularization had also carried heat across the interface into the water.
