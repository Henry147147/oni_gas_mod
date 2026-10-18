# MAC empty-cell support and failed front gate

The standalone MAC owner now admits exact empty cells and transports gas into
them without seed mass. This completes an inventory/topology prerequisite;
**vacuum-front accuracy, cavitation and free surfaces remain unfinished**. Normal
colony activation is still disabled. The [original vacuum gate](GAS_VACUUM.md)
and [strong-flow gate](MAC_STRONG_FLOW.md) remain false.

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

## Remaining front and volume failures

`ma_mac_vacuum_front_diagnostic` uses the original [gas/vacuum problem](GAS_VACUUM.md):
8 m domain, gas in [0,2] m, initial density 1 kg/m3, pressure 100000 Pa, zero
velocity, O2/N2/He fractions 0.22/0.78/1e-28, the same ideal-gas profile and
`dt_max = 0.025 dx/c0`. It selects existing 2/4/3/2 numerical modes. The right
closed wall is beyond the exact front throughout the observation interval.

At N32, positive transported tails reach every cell before 0.8t. Both observed
fronts are 8 m. At the final time the exact front is 4.5097460905 m, leaving a
3.4902539095 m error; recent speed is zero instead of 5.0194921810 c0. The last
cell still has positive mass about 2.03849e-23 kg. Applying a density cutoff would
hide inventory and cannot repair this failure. Interface/occupied-volume geometry
is needed before this path can satisfy the front gate.

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
