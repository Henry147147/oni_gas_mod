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

Relaxation uses the production worker pool in fixed blocks of 1,024 cells.
Each cell keeps the caller's pair order and owns all its primary writes;
entropy expansions and diagnostics merge in block order. Worker count changes
neither primary state nor reports. The focused two-pair control checks ordered
sequential equivalence, one/many-worker bytes and reports, and source immutability.
Three asteroid dump controls retain every endpoint checkpoint byte. On the
frame-50 dump, sixteen workers reduce this stage from 284 ms to 65 ms under the
whole-machine CPU lock. The trade is more concurrent CPU use; physical laws,
relaxation fraction, root tolerance and checkpoint fields are unchanged.

A fresh 60-frame Terra pair under the whole-machine lock measures owner
advance mean 1.029 s → 0.764 s, median 0.939 s → 0.711 s, and p95
1.554 s → 1.290 s. Both pass; candidate maximum relative mass/energy
ledger errors are `5.84e-16` / `3.31e-17`. These owner timings exclude view
publication. Release/83 CTests, the focused ASan/UBSan/TSan control, and clean
comparisons of 83 short MAC executables plus sixteen variants pass; only timing
and the added control's PASS line change.

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

## Boil-only rule

New colonies default to the boil-only rule (user decision 2026-10-28,
`MacPhaseRule::boil_only`; the player's **Evaporation below boiling** setting
off, [settings](../docs/settings.md)). The relaxation compares saturation with
the cell's **total gas pressure** where the equilibrium rule uses the vapour's
Dalton partial pressure:
- **Boiling.** A liquid vaporises only above its boiling point at the total gas
  pressure, which stays pressure-dependent: 100 °C at 1 atm, lower in thin air.
  Water still boils in vacuum, and a gas-free liquid still needs the 5 K
  nucleation superheat at its own pressure. Boiling stops when the total gas
  pressure reaches saturation.
- **Condensation.** Vapour below its boiling point at the total gas pressure
  condenses, as in ONI, until it is gone or its latent heat brings the cell to
  boiling. The gas holds no equilibrium humidity: 25 °C air beside water carries
  no vapour instead of 3.2 kPa.
- **Unchanged.** A cell whose only gas is the vapour (a kettle, sealed steam) is
  unaffected: the total is the sum of the Dalton partials, so it is the same
  number, bit for bit. Fraction, critical temperature, latent heat, fixed U and
  the conserved masses, momentum and passives are those of the equilibrium rule.

**Compressed mixtures.** Because every pair compares with the total gas pressure,
a minor gas in a compressed mixture condenses once the total exceeds its
saturation pressure, although its own partial pressure is far below it. A fresh
Terra (seed 147147) has such pockets: chlorine with hydrogen at about 1.9 MPa and
383–387 K (chlorine's saturation pressure there is 0.78 MPa, below its 417 K
critical temperature), and carbon dioxide with hydrogen at 0.84–1.3 MPa and
277–303 K. From the frame-2 state, 6 s of play condense 214 kg of chlorine and
28 kg of carbon dioxide under the boil-only rule; the equilibrium rule
evaporates those liquids again and instead evaporates 54 kg of liquids into
unsaturated gas. Vanilla ONI keeps both gases (it ignores pressure).

**Entropy.** Condensing vapour below its saturation pressure lowers the mixture
entropy, so the boil-only rule does not maximize it. It maximizes the entropy
with each gas taken at the total gas pressure, S + R Σ n_k ln x_k over the cell's
gases (the mixture entropy less the ideal entropy of mixing): condensing dn moles
changes it by R ln(P / p_sat) dn. The reference relaxation checks that entropy
instead, with its own rounding bound added to the allowance. In the control,
humid air over water at 25 °C condenses 7.27 g with ΔS = −4.95 J/K and a rule
entropy change of +11.5 J/K.

**Where.** The game kernel's per-tick relaxation (`mac_game/phase.hpp`) and the
reference `advance_mac_phase_change` / `with_phase_relaxation` take the rule. Only
game profile6 saves it, as physical [version25](MAC_CHECKPOINT.md#version-assignment);
profile6 with the equilibrium rule keeps version24, and other owners always use
the equilibrium rule. Sharp-interface boiling and gate M5 (production kernel,
`phase_change = saturation`) keep the equilibrium rule; no colony runs them. Under
the boil-only rule an interface cell would be held at the saturation temperature
of its gas neighbours' total pressure, the same value for the steam-only film M5
tests.

The control `ma_mac_phase_change_tests --boil-only` checks single cells under
both rules (humid air condenses entirely, dry air over 25 °C water stays dry,
11 kPa of vapour in 1 atm air at 50 °C condenses until the cell boils, water at
105 °C boils until the total gas pressure reaches saturation, pure steam and
water in vacuum give identical bytes) and a 4×6 game-kernel pool: 25 °C water
under humid air loses 0.2326 kg of vapour to 1.06e-5 kg in 10 s with no
evaporation (the equilibrium rule evaporates to 0.273 kg), with closed energy
and water+steam ledgers, core25 restore and cold continuation; 110 °C water
and 25 °C water in vacuum boil.

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
`MacClosedWorld::with_phase_relaxation(pairs, fraction, rule)`: the same
relaxation, each cell moving `fraction` of the way to equilibrium. The game
kernel relaxes pairs inside each tick with its own EOS and the saved
[rule](#boil-only-rule). A pair's
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
