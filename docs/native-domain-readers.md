# Native domain readers

Some game code reads fluid state directly instead of through the mixture
presentation. Inside the [sealed native domain](sealed-native-domain.md) these
readers must see the completed physical owner, never the scalar compatibility
cells. This document covers the pressure accessor, the breathing-cell reader,
the checks that rebind both after a reload, and the GPU texture check that
proves the rendered bytes match the completed frame. Liquid rendering and
player-visible status are in [native fluid rendering](native-fluid-rendering.md).

Outside the owned cells every reader keeps its original result. A failed
owner never falls back to the scalar projection.

## Physical pressure

ONI's `Grid.Pressure` derives a pressure-like scalar from cell mass. For owned
cells it now returns the completed physical mechanical pressure, converted from
pascals to the accessor's kPa scale. This covers mixed gas and filled
compressed water. Invalid, overflowing or underflowing positive projections
reject; there is no pressure floor.

The adapter patches the accessor and rewrites all five inspected game callers:
the property-pressure texture, the debug pressure/danger/error views and
developer rendering. Rewriting the call sites also covers a runtime that
inlines the tiny value-type accessor. Both pinned game assemblies contain the
same five methods; declaration tests scan their IL and compare the complete set
with the plan, and every planned method must still call the expected accessor
before patching. The wrapper keeps the by-reference value-type receiver and the
original native fallback.

Texture workers read the same pinned immutable presentation frame as the other
mixture render jobs. The pressure comes from the supplied physical reading;
neither density nor the dominant compatibility element reconstructs it.
Legacy devices whose settings use kg/m³ keep density units. This change adds
no mechanical forces or pressure damage.

**Controls.** `pressure-read` (`native-pressure-read-domain`) and
`water-pressure-read` (`native-liquid-pressure-read-domain`); Windows
`-NativeDomain pressure-read` or `water-pressure-read`. They call the actual
accessor for every owned cell, compare its binary32 kPa result with the
completed mechanical pressure and require it to differ from the old scalar
formula. An outside native granite cell must keep its original result, compared
as exact binary32 bit patterns without an owned-reader increment. (Comparing a
returned binary32 value with a Mono intermediate product gave a false failure.)
The gas control also requires owned reads from rewritten callers. Filled
liquid's gas-opacity job never reads liquid pressure, so the water control
requires the accessor and all five installed caller patches instead.

```sh
python3 tools/oni_native_matrix.py \
  --runner-source /path/to/frozen/source --package /path/to/frozen/probe \
  --output artifacts/native-pressure-reader-001 \
  --scenes pressure-read water-pressure-read --frames 120 --timeout 1800
```

**Status.** Linux Unity gas and water sources, saves and fresh-process reloads
pass with 120 exact continued frames. Owned gas cells read about 100.5 kPa,
where the scalar approximation gives 117.3–118.4 kPa; the game worker performs
over 800 owned reads and the outside cell keeps bits `1204148736` (101300).
Filled water reads 220000 kPa from about 220 MPa (scalar approximation
111430). WindowsPlayer under Proton passes both sources and reloads in the
[feature matrix](native-validation-matrix.md#windowsplayer-feature-matrix).
Actual Windows OS is not tested. Mechanical fluid/solid coupling, general
materials, free surfaces and production-device pressure semantics remain open.

## Breathing selection

The adapter patches ONI's world breathing-cell reader. For an owned cell,
`GasBreatherFromWorldProvider` receives the available oxygen mass from one
completed physical mixture. Hover, overlay and this reader use the same
material and breathing model. Scalar compatibility oxygen and total mixture
mass never drive a breathing decision.

The reader applies the configured oxygen fraction and partial pressure, the
hard-mode upper oxygen bound, the CO2 fraction and partial pressure and the
contaminant limits. A rejected atmosphere returns zero breathable mass and the
game's vacuum sentinel. A permitted one returns only gaseous oxygen, rounded
down to binary32 so the advertised amount never exceeds the source. ONI keeps
its six-cell search and duplicant oxygen thresholds. Missing physical data
faults the owner.

**Control.** `breathing-read` (`native-breathing-read-domain`; Windows
`-NativeDomain breathing-read`; at least 30 frames) calls the private reader
for all six owned cells and the public six-cell selection with a live
duplicant's `OxygenBreather`. The initial mixed air must be breathable,
advertised oxygen must differ from scalar total mass, and the selection must
pick the largest permitted oxygen supply. An outside granite cell keeps its
result. The game then admits 0.125 kg of CO2 through its explicit-material
`ModifyMass` path. At least one owned cell must then fail the CO2 policy while
its scalar element is still oxygen, and every native result must match its
completed reading. The reviewer independently checks the fixed policy, mass
rounding, cell revisions, the selection and the once-only transfer account.

**Status.** The Linux Unity source and fresh-process reload pass (24 owned
reads). Before CO2 admission the cells advertise 0.213–0.346 kg of oxygen
against scalar totals of 1.156–1.171 kg. After it, five cells fail both CO2
bounds and the selection takes the remaining cell's 0.308 kg. WindowsPlayer
under Proton passes the same source and reload in the feature matrix. Actual
Windows OS is not tested.

This implements the atmosphere reader only. Production duplicant acceptance,
bulk device reservations, coupling consumed oxygen to metabolic products,
food/body inventories and suit/tube providers remain open. Vanilla's early
requested-mass CO2 emission cannot form a conserved metabolism transaction.
Consumption itself is the [radius transaction](native-domain-transactions.md#radius-consumption).

## Restored reader checks

Every native-domain reload control calls the actual pressure and world
breathing readers before completing. The check runs again after a
same-process return through the main menu, following native shutdown and
session reset.

- All six pressure results must match the current completed mechanical
  pressure in kPa, all five caller patches must still be installed, and the
  outside cell must keep its exact scalar bits without entering the owned
  reader. A restored state can coincide with the old mass approximation, so
  this check does not require the two pressures to differ; the source
  controls keep that challenge.
- The world breathing helper and the six-cell selection run with a live
  duplicant's `OxygenBreather`, with at least twelve owned calls. Every
  advertised parcel must match the current oxygen inventory and policy, with
  downward binary32 projection. Filled water advertises no breathable gas. The
  selection must choose the largest permitted parcel, or reject when none meets
  the duplicant's threshold.

The report's `RestoredNativeReaders` field holds copied cell readings, the
selection and pressure results from the current physical frame. It resets only
after the original native shutdown. The reviewer checks it when present; older
reports keep their original scope.

**Status.** Linux Unity passes both mixed-air/CO2 and water cohorts in two
sessions each. Mixed air rejects the same five CO2-unsafe cells in both
sessions, and the breather instance changes between sessions, confirming the
selection reads the newly loaded colony. Water advertises no breathable gas and
reads 220000 kPa. WindowsPlayer under Proton passes the restored readers in the
actor, mixed-phase and feature-matrix cohorts. Actual Windows OS is not tested.

## GPU texture readback

Every native-domain completion reads the shader-bound gas colour,
density-opacity and hazard textures for all six owned cells. Their bytes must
match the completed mixture samples, and the bound textures, physical frame and
presentation/texture publication must agree. Worker write counters alone do not
show a successful upload.

Page coordinates are calibrated independently of those gas samples. The check
reads the native phase-and-mass texture for the 5-by-4 outline around the room,
including its unobtanium walls, and compares the complete signature with the
current game grid in both direct and vertically reflected coordinates. Exactly
one orientation must match; a reflected rectangle also reverses row order.
Missing, contradictory or ambiguous controls reject. The selected convention
then applies to the phase and gas readbacks. Externally supplied liquid
textures keep their own readback path. The report keeps both raw orientation
controls, the expected signature, the selected coordinates and all gas texels.
Native upload, interpolation and world shaders are unchanged.

The calibration fixed a real measurement error: an uncalibrated readback of
visibly blue water reported solid rock because it addressed the wrong
page-texture rows. The native gas upload was correct.

The reviewer requires the `GpuGasTextures` extension when present, including
the orientation control and exact colour/alpha matches. Older reports keep
their CPU/publication scope.

**Status.** Linux Unity gas and water pass in two sessions each, all selecting
reflected page rows. Gas colours match exactly; density opacity is alpha 150
or 151 and hazard alpha is zero. Water phase pixels are `[0,255,0,140]` and
liquid colour is `[14,101,130,255]`, with every gas channel zero. Second-session
hover and selected details show the same completed physical object and
publication as the checked textures. WindowsPlayer under Proton passes the
calibrated check in later cohorts. Actual Windows OS execution of the
calibration, arbitrary world ownership, free surfaces, other liquids and
sustained performance remain open.
