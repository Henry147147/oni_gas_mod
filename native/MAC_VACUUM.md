# MAC empty cells, void phase and vacuum front

The standalone MAC owner admits exact empty cells and transports gas into them
without seed mass. By default, gas entering an empty cell fills it. The opt-in
[void phase](#void-phase) instead keeps the volume the gas occupies, so a gas/vacuum
line crosses each cell and no gas runs ahead of it; it passes
[gate V](#gate-v-vacuum-front). The void phase is reference-path and gas-only so far.
Normal colony activation is still disabled, and the
[strong-flow gate](MAC_STRONG_FLOW.md) remains false.

## Representation and pressure

An empty cell has zero constituent mass, zero internal energy and no passive
inventory. A zero-inertia face must have zero momentum, including full-range
momentum whose binary64 projection is zero. There is no assigned physical
temperature or velocity in vacuum. Zero EOS diagnostic readings are sentinels.

The pressure map includes only occupied cells. An occupied/empty face uses the
existing material solver's missing-neighbour, zero-traction boundary and the
actual adjacent half mass. It stays movable; it is not a solid wall. Empty-cell
material volume is `V_grid + sum(outward midpoint sweeps)`, checked against the
same volume closure and outgoing-volume CFL. Exhausted swept volume rejects the
candidate. It never creates a pressure unknown or background inertia.

Empty retained parcels and swept empty volume carry no material. Their target
volume closure and every donor's constituent/U/passive account are still checked.
Gas arrivals use the same primal parcels and compatible dual momentum/energy
fluxes as occupied transport. Velocity reconstruction excludes massless source
faces, whose velocity is undefined. Positive half masses, traces and parcel amounts
retain their existing representability checks.

Momentum corrections cannot leave momentum on a fully depleted endpoint. Such
endpoints retain their conservative upwind result; corrections continue elsewhere.
Empty-volume parcels validate geometry and carry exactly zero bulk mass, energy
and metadata. They are needed when empty space moves into a material cell.

Conduction and shock heat have no contact through vacuum, and
[gas diffusion](MAC_DIFFUSION.md#discrete-face-flux) carries no flux across a
face touching an empty cell. Compression and viscous
stencils have material only in occupied cells; shear stencils spanning empty cells
are omitted. Native body heat contact with empty fluid transfers zero energy.
Entropy bounds exclude only the zero-inventory side's undefined temperature.
Occupied-state arithmetic and bounds remain unchanged.

Checkpoint layout and numerical selectors are unchanged. An empty inventory is
already represented by exact zero fields. Restoring an empty-cell checkpoint now
passes owner validation and resumes exactly. Gameplay point transfers still require
occupied cells, and complete paired colony persistence remains open.

## Verified scope

`ma_mac_vacuum_tests` covers twelve two-step expansions: both axes, both
directions, defaults, swept/coupled pressure, conduction, viscosity and gravity.
Initially empty cells receive every gas and positive helium/passive trace while
other cells stay exactly empty. Actual U+K+G, constituent and impulse accounts,
source immutability and save/restore continuation are checked. Further controls
cover an entirely empty owner, both execution policies, body heat, massless U,
passives and full-range momentum, and an invented parcel constituent.
Six transport-depletion controls cover an exact whole-cell translation in donor
and all three physical modes, plus nonuniform linear/quadratic reconstruction
beside newly massless faces. A closing empty gap retries its volume CFL twice;
an exhausted one-attempt budget leaves the original owner and checkpoint intact.

For the twelve captured endpoint pairs, the saved-only reader independently reconstructs EOS, entropy, inertia, kinetic
and potential energy, impulses and each constituent/passive account at 100 digits
or with exact fractions. It rejects five corruptions. The other identity,
depletion, CFL and rejection controls are native assertions. These are bounded component
controls, not a vacuum-front, colony or performance certificate.

Two older malformed-domain controls previously rejected an empty cell itself.
They now reject energy without mass and positive mass exported by an empty donor.
Their scientific occupied cases and conservation/refinement criteria are unchanged.

## Void phase

`MacClosedSettings::vacuum = MacVacuumRepresentation::void_phase` (persisted value 1).

**State.** `MacTransportState::occupied_volumes_m3` holds the volume each cell's
material occupies; the rest of the cell is exact vacuum. It is empty while every
occupied cell fills its grid volume, and otherwise has one value per cell: zero for
empty cells and walls, in (0, V] for occupied cells. `mac_material_volume` returns it
(or the grid volume). Every EOS reading of a cell uses that volume: the pressure
stage's initial material volume, compression coefficients, shock heat, split
compression, gravity reconciliation entropy, the interval entropy report, cell
heat edits and native body heat.

**Pressure stage.** A cell's material starts at its occupied volume and changes by
its face sweeps. Its void is unchanged, so the face on a cell's void side carries
the gas/void line. Face inertias are still half cell masses, and occupied/empty
faces keep the zero-traction boundary of exact empty cells.

**Transport.** Each partly filled donor places its material behind a PLIC line
(Youngs normal from its neighbours' filled fractions). Each outgoing strip carries
material and void in their geometric proportion (`allocate_strips`, as for
gas/liquid interfaces), with the material at the donor's one density. Donors beside
partly filled or empty cells keep donor order. The parcel plan records each
parcel's and each retained remainder's material and void volumes. A target's
occupied volume is the sum of the material parts it receives, or the grid volume
less the void parts when those are smaller, so neither a thin layer nor a thin void
loses precision. A rejected split falls back to the donor's average composition.

**Scope.** The void phase requires reconstructed parcels (material selector 1, 2
or 3) and midpoint pressure. It rejects thermal conduction, viscosity and gas
diffusion, because those stages still assume filled cells. A partly filled cell
holding liquid rejects ("does not yet hold liquid"), and fluid point transfers
reject partly filled states. The production kernel does not implement it; the
owner runs the audited reference fallback. Checkpoints use
[version 9](MAC_CHECKPOINT.md).

`ma_mac_void_phase_tests` (CTest `mac_void_phase`) covers 1-D and 2-D corner
expansions and gas falling into vacuum under gravity:
- species totals stay within 1e-14 and every energy residual within its bound;
- occupied volumes stay valid, and no partly filled cell is enclosed by filled cells;
- in 1-D, only the front cell is partly filled and no gas runs ahead of the exact
  front plus one cell.

It also covers version-9 save/restore with exact continuation, the production
fallback (identical to the reference), the legacy checkpoint version, and the
rejected combinations listed above.

The 2-D expansion cools gas near its front below about 5 K. Before this change, the
reference EOS rejected that ("Material energy reference erases resolved thermal or
elastic energy"). It required each material's sensible energy to survive the
round trip through `offset + sensible` to 32 epsilons of the sensible part alone.
That is impossible once the chemical offset dominates (N2 below about 5 K, CO2
below about 215 K). Stored U has the same absolute precision, about eps × |offset|.
The check now accepts the larger of that bound and 32 epsilons of the represented
specific energy (user decision, 2026-10-19).

## Gate V: vacuum front

Gate V uses the [original problem](GAS_VACUUM.md), with the void phase, material
selector 2, donor-cell momentum, coupled compression and pressure-sensed heat.
`ma_mac_vacuum_front_diagnostic --gate` (CTest `mac_gate_v_vacuum_front`, 21 s)
measures the front with 0.1% of the gas mass beyond it. In the exact fan, the mass
beyond x is ρ c0 t (c/c0)^((γ+1)/(γ−1)), so that front is not self-similar; its
exact position and its displacement over the last 0.2t come from that law. At
N512 the front must lie within 5% of its exact travel, and the displacement within
10% of exact, with both errors decreasing over N128, N256 and N512. Also, no gas
may lie more than one cell ahead of the exact front at either endpoint:

| Cells | Front error | Displacement error | Last gas vs exact front |
| ---: | ---: | ---: | ---: |
| 128 | 14.2% | 9.3% | 15.5 cells behind |
| 256 | 8.2% | 1.7% | 20.8 cells behind |
| 512 | 4.2% | 0.32% | 5.5 cells behind |

**Why not the last gas-bearing cell.** The gate first measured the last cell holding
any gas. That measure cannot converge at these resolutions with any grid method.
Near the edge the exact density falls like (distance)^5 and the mass beyond like
(distance)^6. Any front cell holding about one cell of mass therefore sits about
h^(1/6) behind the true edge. A pure Lagrangian solver of the same problem
(scratch reference, no remap) is 62%, 55%, 48% and 42% short at N128 to N1024.
The gate was redefined by user decision on 2026-10-19.

**Remaining error.** The last gas-bearing cell moves too fast: 6.0 c0 at N512
against 5.02 c0, and it is still behind the exact front only because it starts
slowly. Gas within a few cells of the front stays hot (100–200 K where the exact
expansion approaches 0 K). Each step, the thin front layer pushes its half-mass face
hard, and the momentum remap then turns that face's extra kinetic energy into
heat. The front layer so expands almost without cooling. Limited-linear momentum
amplifies this: the last gas runs about 5 and 70 cells ahead of the exact front at N256 and
N512, failing the gate. A Lagrangian-style front cell that rides on its upstream face,
and central momentum fluxes, were tried in a 1-D toy; neither removed the error.

## Empty-cell front (default)

`ma_mac_vacuum_front_diagnostic` uses the original [gas/vacuum problem](GAS_VACUUM.md):
8 m domain, gas in [0,2] m, initial density 1 kg/m3, pressure 100000 Pa, zero
velocity, O2/N2/He fractions 0.22/0.78/1e-28, the same ideal-gas profile and
`dt_max = 0.025 dx/c0`. It selects existing 2/4/3/2 numerical modes. The right
closed wall is beyond the exact front throughout the observation interval.

At N32, positive transported tails reach every cell before 0.8t. Both observed
fronts are 8 m. At the final time the exact front is 4.5097460905 m, leaving a
3.4902539095 m error; recent speed is zero instead of 5.0194921810 c0. The last
cell still has positive mass about 2.03849e-23 kg. Applying a density cutoff would
hide inventory and cannot repair this failure. The [void phase](#void-phase) avoids
it, and also runs N64 to N512 without the entropy-guard stop below;
`ma_mac_vacuum_front_diagnostic --void <cells>` prints its records for one grid.

N64 and N128 stop on the original compressive entropy guard. A captured N64 trial
has a material-volume addition error of -1.1414615997e-17 m3. Independent 100-digit
analysis of its actual stored U and volume gives entropy change
-4.9916788264e-80 J/K, below the unchanged allowance 2.9255213546e-80 J/K.
The rejection is correct. Persisted volume/energy remainders remain necessary;
the entropy bound is not enlarged. Captured inputs, private instrumentation,
arithmetic analysis and raw results are under `local/mac-vacuum/`.

The front diagnostic exits 2 when its position or recent displacement differs by
more than one cell. Exit 0 on one grid would still not prove refinement or the full
vacuum gate. Its saved reader reports `EvidenceValid` separately from
`FrontGatePassed`, which remains false for this single-grid diagnostic.

```sh
cmake --build build-production --target ma_mac_vacuum_tests ma_mac_vacuum_front_diagnostic
build-production/ma_mac_vacuum_tests > vacuum.stdout
python3 native/tests/review_mac_vacuum.py vacuum.stdout vacuum.json
build-production/ma_mac_vacuum_front_diagnostic 32 > front.stdout
python3 native/tests/review_mac_vacuum_front.py front.stdout front.json
```

The analytic front follows the ideal-gas rarefaction limit in the
[Clawpack Euler chapter](https://www.clawpack.org/riemann_book/html/Euler.html#Riemann-problems-with-vacuum).
