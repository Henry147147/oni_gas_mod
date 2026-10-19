# Sealed native fluid domain

The sealed native domain is the bounded first step of colony fluid ownership.
`MacClosedWorld` (`native/src/mac_*.cpp`), the project's production colony fluid
solver, advances one sealed rectangle on ONI's native simulation worker while
the rest of the colony keeps its original simulation. ONI's native SimDLL fluid
simulation is what the mod replaces: inside the rectangle it is excluded, and it
is never used as the solver. The fixed scenes own a 3×2 room; the
[configured domain](native-configured-domain.md) adds larger rooms with
interior solids. Every scene starts only from a development
`colony-validation.json` in a disposable colony. Whole-colony ownership,
first-playable and release gates remain closed, and every report keeps those
readiness fields false.

This document is the hub: the execution and ownership contract, the base
physics and boundary ABIs, paired save/load, the scene list, status per
platform and open items. Later features have their own documents:

| Topic | Document |
| --- | --- |
| Scenarios 3–6 (water gravity, gas/liquid cells, mixed conduction, viscosity), initial inventory, native object and solid-cell heat | [Domain physics](native-domain-physics.md) |
| Additive cell-mass edits, radius and rectangular consumption, the frame-input journal, live duplicant breathing | [Boundary transactions](native-domain-transactions.md) |
| Pressure and breathing readers, restored-reader checks, GPU texture readback | [Readers](native-domain-readers.md) |
| Durable actors, in-flight CO2, day autosaves, main-menu reload, orbital references, Spaced Out saves | [Persistence](native-domain-persistence.md) |
| Scenario 7: physics flags, interior solids, rooms up to 4,096 cells | [Configured domain](native-configured-domain.md) |
| Status display, flow-guard flags and liquid fill | [Fluid rendering](native-fluid-rendering.md) |
| Hook trampolines, solver arithmetic scope, binary32 decoding | [Platform and numerics](native-platform-numerics.md) |
| Hooks, observer and every capability version | [Backend integration](native-backend-integration.md) |
| Physical checkpoint (`MACCLS01`), paired capture and save/load mechanics | [Physical checkpoints](../native/MAC_CHECKPOINT.md) |
| Durable actor records (`MAACTR01`) | [Actor checkpoint](../native/ONI_ACTOR_CHECKPOINT.md) |
| Save/load matrix and platform terms | [Validation matrix](native-validation-matrix.md) |
| Windows OS, Proton and native-worker routes | [Windows validation](windows-validation.md) |
| Isolated licensed-SimDLL controls (`sealed-*`) | [NativeShadowProbe](../tools/NativeShadowProbe/README.md) |
| Linux game runner | [ONI_HEADLESS.md](../tools/ONI_HEADLESS.md) |
| The solver | [MacClosedWorld](../native/MAC_SOLVER.md) |

## Execution and ownership

The mod attaches the exact-build native hooks before initialization. After a
disposable colony starts, the validation driver builds an unoccupied room with
an impermeable, adiabatic Unobtanium border. It verifies the border and the
absence of objects, disease and radiation before enabling the domain.

**Clock.** The actual positive `ProcessNextFrame` return supplies the timestep
and a monotonic ticket. `UpdateData` advances the custom owner on the native
simulation worker, once per global timestep. The domain's mutex spans
`ProcessNextFrame` through `EndFrame`, including native cell work, custom work
and the original update, so a managed reader cannot consume a partially
completed native frame. `BeginFrame` may wait for main-thread work and stays
outside the fence. Synchronization-only passes never advance custom time, and
there is no managed advance timer.

**Solver selection.** New owners and live advances use the production kernel
(selection 2), with twelve requested workers and fixed-block caps for small
rooms. Supported reference saves retain their exact idle image and migrate on
the first live frame; its journal records production flag 4. Historical
unflagged replay and stateless continuation preserve the saved selection.
Native regression evidence covers this transition. Production gas saves and
fresh-process reloads pass Linux Unity, including paused frame-zero saving
and 120 exact continued frames. The managed save wrapper admits physical
version 6 without changing its outer payload framing. The frozen `73abf47`
package passes twenty-eight Linux pairs, including both configured rooms, and
four WindowsPlayer/Proton pairs (solid heat, gas, water and paired checkpoints).
Every reload verifies paused frame-zero saving and 120 exact continued frames.
These bounded fixtures do not certify a colony or actual Windows OS execution. Production
`water-heat` fails its unchanged endpoint energy gate after 123 frames:
−0.0177001953125 J residual against 0.005945804622783117 J, with no rejected
solver attempts. A standalone public-species reproducer matches the drift,
conduction counters and entropy report; it is retained with the game evidence
in `local/production-water-heat-drift-v1/` and `local/production-save-v6-v1/`.
Persistent primary-U and volume remainders now belong to Codex.

**Native exclusions.** The validation scenes queue ONI's gas/liquid
impermeability flags and verify them in the original native snapshot. Native
per-cell post-processing and pressure, gas, liquid, thermal and transition
entry hooks exclude owned cells; three-cell pressure-displacement hooks filter
every involved cell. Native functions continue outside the rectangle, and a
hot/cold Granite pair outside the room must keep conducting heat. Installed
gas-interaction entries have zero calls (Windows inlines those routines), so
their symbols alone are not coverage evidence.

**Native indices include a one-cell halo:** for public `(x,y)` and width `W`,
the native index is `(x+1) + (y+1)*(W+2)`. Configuration and snapshots use
public coordinates; each cell hook decodes the padded index before checking
ownership, and start checks the loaded simulator's padded dimensions against
the requested public grid (on Windows through the PDB's `SimData` width/height
offsets 0/4). Never apply public-grid indices directly to native cell phases:
an earlier filter did, excluded the wrong cells, and hardware watchpoints found
the resulting native gas swap inside `PostProcessCell`.

**Compatibility cells.** The scalar native cells are compatibility data. Flag
installation can displace fluid before ownership begins. The synthetic
controls therefore first let original ONI move a deliberately unbalanced scalar
mass pattern and a `[280, 300, 320] K` temperature pattern, restore those
nonvacuum cells after activation, and require an exact match before the
acceptance interval starts; a missing projection fails within ten frames. The
original element/mass/temperature/disease tuples must then stay byte-identical
while the physical state advances. This distinguishes a blocked native phase
from a native state that would never have moved; it does not prove coverage of
every game writer. Scenes that start from the native inventory capture it
before installing the flags ([initial inventory](native-domain-physics.md#initial-inventory-admission)).

**Publication.** Each completed custom frame is copied through the
`ma_oni_domain_*` extension. An owned copy of all 47 native channels keeps the
surrounding colony's events and nonfluid data; only owned-cell mass,
temperature and element projections change, and the two synthetic flow flags
are cleared in that copy ([fluid rendering](native-fluid-rendering.md#flow-guards-and-liquid-fill)).
Immutable thermodynamic readings feed the mixture renderer and inspection;
scalar projections never reconstruct the physical inventory. A failed
publication keeps the last valid owned snapshot, and a fault never enables
scalar fallback. A fault stops custom advancement while the native fluid
exclusions stay installed until native shutdown, so the disposable session must
end.

The solver sets and restores its own floating-point environment on the worker
thread ([arithmetic scope](native-platform-numerics.md#solver-arithmetic-scope)).

## Physical model and domain ABI

The owner keeps constituent mass, internal energy U and canonical face
momentum. It uses the closed MAC pressure/transport solver with its compression
coupling, the declared ideal-gas/elastic-liquid reference model
`oni-sealed-domain-v2` and immutable candidate advances. The five species are
nitrogen, oxygen, helium, water and carbon dioxide. The mixed-air control holds
nitrogen and oxygen with trace helium. The water control initializes 1,100 kg
in each cubic metre at 300 K: its mechanical pressure is 220 MPa, with elastic
energy kept separately from thermal energy. These are development materials,
not a complete ONI material catalogue or a free-surface solver. Scenario
numbers 0–7 select the physics; the table is in
[domain physics](native-domain-physics.md).

Domain ABI 2 (`native/include/mixed_atmospheres/oni_domain.h`) uses a 40-byte
`MAOniDomainConfig`, a 440-byte `MAOniDomainStatus` and 96-byte
`MAOniDomainCell` readings: five constituent masses followed by U, pressure,
temperature, liquid fraction, resolved kinetic energy and momentum x/y. Native
and managed clients reject the older four-species layout. The base boundary
extensions are described below; the full capability list is in
[backend integration](native-backend-integration.md#interfaces-and-saves).

| Extension | ABI | Records | Section |
| --- | ---: | --- | --- |
| Thermal diagnostic | 1 | 80-byte status | [Conduction](#internal-fluid-conduction) |
| Heat commands | 1 | 48-byte command, 56-byte status | [Heat commands](#conserved-game-heat-commands) |
| Gravity diagnostic | 1 | 96-byte status | [Gravity](#gravity-and-carried-potential-energy) |
| Transfers | 2 | 64-byte command, 88-byte receipt, 104-byte status | [Point sources](#gas-point-sources-and-game-callbacks) |
| Actors | 1 | 80-byte configuration, 64-byte status | [Registered actors](#registered-gas-actors) |

## Internal fluid conduction

The domain enables `MacClosedSettings.thermal_conduction`. Each accepted
physical substep prepares two conductive half stages around the
compression/pressure/transport stages. All stages share one clock, revision
and immutable candidate: a rejected thermal solve publishes no partial heat,
mass, actor receipt or elapsed time. The option stays off by default for
numerical controls whose equations exclude conduction.

Occupied single-phase cells use the reference EOS heat capacity at fixed
volume. Gas conductivity is weighted by partial volume (equivalently mole
fraction); liquid weights use actual compressed partial volumes. A face's
conductance is its area divided by the sum of the two half-cell thermal
resistances, and a backward-Euler thermal network advances the contacts.
Walls and exterior faces are insulated. Constituent mass, passive inventory and
face momentum do not change during this operator.

Every stage checks local and global energy accounts and endpoint entropy at
bounds derived from its operands. Independent two-body equations and analytic
time refinement test the heat solve; source immutability and the single clock
are tested separately. Thermal diagnostic ABI 1 reports session/frame, enabled
state, stage/contact/iteration counts, redistributed energy and entropy change,
and counts heat updates smaller than the stored binary64 internal-energy
resolution; these are reported, not hidden. Stored cell energy uses
[conservative component rounding](../native/MAC_HEAT.md#physical-conduction-and-stored-energy-rounding),
the diagnostic counts represented redistribution, and entropy uses the
represented U increment.

- `native-conduction-domain` (`conduction`) seeds a gradient with the ordinary
  +2,000 J / -500 J game heat commands. It requires positive internal
  redistribution and entropy, exactly two thermal stages per substep and
  matching diagnostic session/frame values.
- `native-liquid-heat-domain` (`water-heat`) applies +2 MJ / -0.5 MJ to filled
  water. It requires warming of the source and a conductive neighbour,
  positive represented redistribution and entropy, and the paired save/reload.
- `sealed-conduction` and `sealed-water-small-heat` are the isolated licensed
  library controls; the latter checks sub-ULP accounting only.

This path uses development constant properties. It rejects gas/liquid
coexistence (see [mixed-cell conduction](native-domain-physics.md#mixed-cell-conduction))
and variable heat capacity, and excludes fluid/solid exchange, radiation and
phase transitions.

## Conserved game heat commands

`SimMessages.ModifyEnergy` routes owned-cell heating and cooling into the
domain, converting the message's kJ to joules. A complete batch reserves an
immutable heat candidate against the exact session and completed frame;
enqueue changes neither the published state nor time. The next native worker
timestep commits the candidate and the physical advance together. Every
constituent, the canonical face momentum and the physical clock are kept.

Heat ABI 1 status counts pending and committed batches separately, so
acceptance is distinguishable from application. Accumulated heat accounts keep
full additive expansions; their scalar status values are read-only
projections. The U+K drift diagnostic includes the declared external heat
account. Invalid, stale, temperature-limited or unrepresentable batches queue
nothing, and a failed physical advance publishes neither heat nor state.

This is a reserved external heat boundary, not a power or producer adapter.
Mixed native/custom batches need a cross-owner reservation and reject before
dispatch. Native heat outside the room still uses ONI. `native-heat-domain`
(`heat`) sends +2,000 J and -500 J through the game API and requires the net
1,500 J in the U+K account with the original native message entry never
called.

## Gravity and carried potential energy

Scenario 2 (`native-gravity-domain` in the game, `sealed-gravity` in the
isolated control) enables the owner's 9.81 m/s² downward gravity. Its
force/work calculation uses the actual face mass fluxes. The energy account
includes internal energy, resolved face kinetic energy and `sum(m*g*y)` at cell
centres, with the bottom of the owned rectangle as the potential datum.
Scenarios 0, 1 and 4–6 have explicitly zero gravity; scenario 3 is filled water
with gravity ([filled-liquid gravity](native-domain-physics.md#filled-liquid-gravity)).

Injection carries potential at its destination elevation and extraction at its
source elevation, for point messages and registered actors alike, so equal
masses at different elevations still exchange boundary energy. Pending
transfers reserve this account without changing physical state; the worker
commits it with the material and kinetic accounts, actor receipts and the
physical advance. Invalid batches and failed timesteps keep the previous
accounts.

Gravity ABI 1 reports session/frame, acceleration, initial and current
potential, committed and pending boundary potential, cumulative body impulse,
signed internal gravity work and maximum absolute total-energy residual. The
game control independently reconstructs potential from all completed cell
masses and heights, requires vertical momentum and force, injects and consumes
gas through game callbacks, and checks the complete U+K+G boundary balance.
This occupied sealed room does not establish free surfaces, nonlinear buoyancy
or whole-colony gravity.

## Gas point sources and game callbacks

Owned-cell `SimMessages.EmitMass` and single-cell `ConsumeMass` reserve
physical gas transfers against the completed native frame. Injection arrives
at rest, keeps existing momentum and returns the kinetic mixing loss as heat.
Selective extraction carries the selected gas's internal energy and its share
of face momentum and kinetic energy; every other constituent, including trace
helium, stays. These are declared point-source boundaries: pumps still need
accountable aperture flow, work and output reservations.

Transfer ABI 2 status carries all five constituent accounts. Heat and mass
commands share one ordered immutable candidate. Enqueue publishes no physical
change or callback; the worker commits the candidate, advance,
constituent/energy accounts and receipts together. Session IDs, expected
frames and receipt sequences reject stale work. Receipts stay in a bounded
outbox until acknowledged with a coherent owned snapshot, which appends the
game's mass callback records for ONI's normal callback managers to deliver.
Callback handles keep all 32 bits. Binary32 callback amounts equal the physical
transfers exactly; unrepresentable extraction remainders stay in the source.
Fault snapshots clear already delivered event lists before keeping the last
physical frame, so a stopped publication cannot replay callbacks.
Handle-indexed emitter, chunk, building-temperature and disease status tables
stay owned and available while paused.

`native-transfer-domain` (`transfer`) injects 0.05 kg of oxygen and consumes
0.02 kg through the ordinary game APIs. It requires one emission and one
consumption callback, unchanged original native cells, no original native
message dispatch and independently checked mass and U+K accounts.
`native-carbon-domain` (`carbon`) does the same with CO2 at 310 K and requires
transported CO2 in the completed mixture reading.

Supported: nitrogen, oxygen, helium and carbon dioxide in occupied gas cells,
and water in filled cells (below). Radius and rectangular consumption and
additive cell-mass edits are in [transactions](native-domain-transactions.md).
Empty-cell transitions, disease-bearing injection, mixed-owner batches, bulk
actors and producer reservations need further adapters; unsupported requests
reject before dispatch.

## Filled-liquid point sources

The same transfer ABI and message route admit water in an already filled,
single-liquid cell. Injection declares a parcel at rest, at the supplied
temperature and the receiving cell's pre-edit pressure; its internal energy
includes both the caloric reference and the stored elastic compression energy.
Extraction carries the local specific internal energy and its share of face
momentum and kinetic energy. The fixed-volume EOS resolves the resulting
pressure and temperature from conserved state. No implicit pump work or
enthalpy flux is added.

The operator rejects gas/liquid mixing, another liquid material, passive
metadata without an adapter, empty cells and removal that would leave a void.
A later invalid request leaves every earlier candidate, publication, clock,
account and receipt unpublished. Gas-only scenes keep their phase restriction.
Physical checkpoints keep the resulting water inventory, elastic energy and
face momentum.

Water's caloric reference makes absolute internal energies negative, so
parcel checks compare the EOS value; positivity is not an energy invariant.
The independent liquid U+K report uses an explicit binary64 roundoff bound,
`128 * epsilon * (sum(abs(U)+K) before + after)`, instead of a sub-ULP absolute
tolerance. Gas tolerances and the native expansion-based accounts are
unchanged.

`native-liquid-transfer-domain` (`water-transfer`) injects 0.05 kg through
`EmitMass`, lets a positive native interval create motion, then extracts
0.02 kg through single-cell `ConsumeMass`. It checks the original native
cells, both callbacks once, both acknowledged receipts and their ordered
frames, the constituent accounts and U+K. Add `--save-colony` and reload with
`native-load-domain --resave-before-resume`; restored water need not equal the
1,100 kg equilibrium fixture, but replay must match every resumed frame.

## Registered gas actors

Selective single-cell consumers and disease-free single-cell emitters use the
same conserved point-source boundary. ONI allocates the real actor handle and
delivers its ordinary registration callback. The adapter keeps the native
transfer at its initial inactive setting, admits the full handle, then routes
later rates and intervals to the custom worker. Native actors outside the room
continue normally. Moving a live actor between owners or admitting an already
active native transfer needs a cross-owner reservation and rejects.

Actor ABI 1: consumption uses the actual native timestep, and its binary32
reporting remainder stays with the actor's demand. Emission keeps interval
time, resets the interval on changes and reports blocked/unblocked edges once.
The game's output limit is a gas density in kg/m³; this compatibility setting
is not labelled as pascals. Actor changes, physical transfers, accounts,
clocks and receipts are prepared together, and a late actor or solver failure
commits none of the candidate.

Completed consumer records feed the actual `ElementConsumer.AddMass` path.
Emitter records overlay the allocated native status slot; they are status
samples, not callback events, and the latest completed status stays available
during synchronization-only queries. A native positive transfer for an
admitted handle fails verification. Block notifications use ONI's normal
callback manager. Removal retires the custom actor and ONI's inactive handle.

### Atomic actor batches

`ma_oni_domain_configure_actors` (actor ABI 1, at most 256 commands) prepares
every rate/interval command against one session/frame and commits the ordered
registry update together. A late invalid material, stale frame or missing
removal handle changes no earlier configuration or output. Repeated handles
follow command order; the final configuration governs the next timestep.
Configuring a batch advances no time and emits no mass callbacks.

The managed native-message bridge preflights ownership before admission.
Batches of owned actors use the custom registry and batches of unowned actors
keep native dispatch. Registration batches decode all owned callback bindings
before publishing any, then forward the original inactive native
registrations; duplicate callbacks, unsupported materials or radii and
malformed late entries reject the whole plan. Owned removal batches retire
their complete custom set, then forward the original batch to retire ONI's
inactive registrations. An outside consumer keeps its native admission rules,
and emitter location/material admission happens at its later configuration
message. Mixed-owner mutation batches reject before dispatch.
Forwarding an inactive registration to a native outside transfer retires its
eligibility for later custom admission.

An emitter's first owned configuration resolves its declared material even
when the output mass is zero (a new water emitter otherwise kept the binding's
default gas species and failed phase admission). Later zero-output
configurations keep their admitted material; positive output always validates
the current material.

### Filled-liquid actors

Actor ABI 1 also admits water consumers and emitters in the compressed-water
domain, with the same elastic-energy parcel contract as the liquid point
messages. Gas actors cannot enter filled water and liquid actors cannot enter a
gas domain. The ABI's historical `maximum_gas_density_kg_m3` field compares
total fluid density for water emitters; it is a compatibility output limit
with no pump work model.

### Actor scenes

`native-actor-domain` (`actor`) and `native-liquid-actor-domain`
(`water-actor`) need at least 80 frames (use 120). They spawn a real Unity
`ElementConsumer`, register two emitters in one native batch, change rates in
an owned batch while a second real consumer outside the room keeps native
dispatch, hold the second emitter at zero output for eight frames with
correct-phase zero-mass status samples, enable and disable it, block and
unblock output, and retire everything in one owned/native batch. They then
require continuing frames without actor transfers. Reports separately require
both registration callbacks and batch admissions, all receipts and the
zero-output interval. The optional ordinary save runs after retirement; live
actor saves are covered in
[durable point actors](native-domain-persistence.md#durable-point-actors). The
scenes reserve no producer inventory, power or output work and enable no pumps,
spatial consumers, disease or construction.

## Paused colony persistence

The [physical checkpoint](../native/MAC_CHECKPOINT.md) keeps the complete
owner, including physical U and full canonical momentum.
`native-checkpoint-domain` (`checkpoint`) exports a drained, actor-free frame
and requires its independent continuation to match the exact next worker frame.
`native-paired-checkpoint-domain` (`pair`) captures native cells and the
physical owner at one worker barrier, verifies the original native tuples in
the saved image, checks immutable buffers and validates their shared
`MACOLONY` container. The isolated `sealed-restore*` controls reload both
sections through the licensed native loader.

**Save.** The ordinary `SaveLoader.Save` entry (manual saves and autosaves)
pauses and synchronizes the owner, captures both owners and writes a full ONI
colony; one staged `Sim.Save` writes the paired envelope, and a finalizer
retires the pair and restores the exact prior pause count. Pending commands,
owned actors without durable identities, undelivered receipts, nested calls and
foreign threads reject before serialization. Unstaged `Sim.Save` and the native
begin-save detour reject (a direct `SIM_BeginSave` returns null/zero), so an
ordinary scalar save can never stand in for the physical owner. The file is flushed and
renamed from a fresh temporary destination, readback checks the complete body
and paired section, and the physical state must stay byte-identical during
serialization. `native-save-domain` (`save`) exercises this from a running
colony, including a pending-command save that is declined, keeps its command
and creates no file; `--save-colony` adds the save to another scene.

**Load.** `native-load-domain` (`load`) loads the file through the normal
scene loader in a fresh process. It validates both sections before native
Load, restores the exact physical owner after native startup, reinstalls flow
flags at zero time and delivers retained native startup events once. The
managed scene, colony GUID and duplicants, grid, renderer, outside native heat
and every resumed physical frame are verified. Gas, compressed water and
gravity keep their saved scenario. `--resave-before-resume` autosaves through
`Game.StartDelayedSave` and the normal coroutine at restored publication frame
zero, requiring the exact saved state and clock and unchanged pause depth
before resuming. A simulation-tick gate holds positive intervals until that
save completes: the pause count alone cannot exclude a positive delta time that
Unity latched before the pause. New owners at physical time zero are
ineligible. The save/load mechanics are specified in
[paused Unity colony save/load](../native/MAC_CHECKPOINT.md#paused-unity-colony-saveload).

```sh
python3 tools/oni_headless.py run --package <package> \
  --mode native-save-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package <package> \
  --mode native-load-domain --load-save <saved-colony.sav> \
  --resave-before-resume --frames 120 --timeout 1200
```

The runners copy the input into the new run and check its SHA-256 before and
after; a changed original fails, and Save As paths stay inside the run. The
Linux runner uses a stable private Dev mod identity across profiles; for a file
made with another folder ID, pass it with `--mod-id` to avoid ONI's Mod
Differences dialog. On Windows use `-NativeDomain save` and `-NativeDomain load
-LoadSave <file> -ResaveBeforeResume`. Live actors, in-flight particles, day
autosaves and main-menu reloads extend this path in
[persistence](native-domain-persistence.md).

## Save failure recovery and native metadata

Save admission reports a busy source without escaping through ONI's save
coroutine. Handled disk and readback declines keep the previous active save
selection and ONI's existing dialog, retire the pair and pause request, and let
ONI finish its deferred-save callbacks and restore dragging.
`--verify-save-io-recovery` (`-VerifySaveIoRecovery`; `native-save-domain`,
at least 16 frames) targets a directory as the save file, accepts only ONI's two
stock disk-error messages (Mono reports access denied for a directory target),
verifies the retained sentinel and the unchanged physical image, then resumes
and completes a normal verified save.

ONI 744825 can serialize a native cell outside the domain with disease hash
zero and a positive count (observed: count 24). Both licensed native loaders
normalize it to no disease. Preserved-world checks accept that opaque
native-owned metadata; fluid migration stays strict and owned cells still
require zero disease fields.

**Status.** Linux Unity passes one pending-command decline and one denied
write with all three instrumented callbacks once, and restores dragging, pause
depth and the previous selection. The resumed save matches the uninterrupted
control's physical image, and its fresh-process reload passes the frame-zero
autosave and 120 exact frames. The failure UI is not yet tested on Windows.

## Scenes and how to run them

Short names are the Windows `-NativeDomain` values and the
[matrix](native-validation-matrix.md) scene names; Linux modes are
`oni_headless.py --mode` values.

| Short name | Linux mode | Checks | Contract |
| --- | --- | --- | --- |
| `gas` | `native-domain` | Mixed-air owner, exclusions, projection, rendering | [Execution](#execution-and-ownership) |
| `water` | `native-liquid-domain` | Compressed-water owner | [Model](#physical-model-and-domain-abi) |
| `heat` | `native-heat-domain` | Game heat messages | [Heat commands](#conserved-game-heat-commands) |
| `conduction`, `water-heat` | `native-conduction-domain`, `native-liquid-heat-domain` | Internal conduction | [Conduction](#internal-fluid-conduction) |
| `transfer`, `carbon` | `native-transfer-domain`, `native-carbon-domain` | O2 and CO2 point transfers | [Point sources](#gas-point-sources-and-game-callbacks) |
| `water-transfer` | `native-liquid-transfer-domain` | Water point transfers | [Liquid point sources](#filled-liquid-point-sources) |
| `actor`, `water-actor` | `native-actor-domain`, `native-liquid-actor-domain` | Registered actors (80+ frames) | [Actors](#registered-gas-actors) |
| `gravity` | `native-gravity-domain` | Gas gravity, carried potential | [Gravity](#gravity-and-carried-potential-energy) |
| `water-gravity` | `native-liquid-gravity-domain` | Scenario 3 | [Physics](native-domain-physics.md#filled-liquid-gravity) |
| `mixed`, `mixed-heat`, `viscous` | `native-mixed-domain`, `native-mixed-heat-domain`, `native-viscous-domain` | Scenarios 4–6 | [Physics](native-domain-physics.md#occupied-gasliquid-cells) |
| `import`, `water-import` | `native-import-domain`, `native-liquid-import-domain` | Start from native cells | [Physics](native-domain-physics.md#initial-inventory-admission) |
| `solid-heat` | `native-solid-heat-domain` | Solid-cell heat (open in Unity) | [Physics](native-domain-physics.md#native-solid-cell-heat) |
| `radius`, `rectangular` | `native-radius-domain`, `native-rectangular-domain` | Spatial consumption | [Transactions](native-domain-transactions.md#radius-consumption) |
| `carbon-particle` | `native-carbon-particle-domain` | Real breath particle (30+ frames) | [Transactions](native-domain-transactions.md#additive-cell-mass-edits) |
| `breathing-read` | `native-breathing-read-domain` | Breathing reader (30+ frames) | [Readers](native-domain-readers.md#breathing-selection) |
| `pressure-read`, `water-pressure-read` | `native-pressure-read-domain`, `native-liquid-pressure-read-domain` | Pressure reader | [Readers](native-domain-readers.md#physical-pressure) |
| `checkpoint`, `pair` | `native-checkpoint-domain`, `native-paired-checkpoint-domain` | Checkpoint continuation, paired capture | [Persistence](#paused-colony-persistence) |
| `save`, `load` | `native-save-domain`, `native-load-domain` | Ordinary save, fresh-process load | [Persistence](#paused-colony-persistence) |
| — (Linux only) | `native-configured-domain`, `native-configured-water-domain` | 7×5 rooms with interior solids | [Configured domain](native-configured-domain.md) |

Build an observer-enabled package as in the
[README](../README.md#compile-install-and-enable), then on Linux:

```sh
python3 tools/oni_headless.py run --package <package> \
  --mode native-domain --frames 120 --timeout 1200
```

On Windows, in PowerShell 7 with Python available for the shared acceptance
checker ([Windows validation](windows-validation.md#windows-os-game-runs)):

```powershell
./tools/run_colony_validation.ps1 -Package ./artifacts/windows-probe `
  -NativeDomain gas -Frames 120 -TimeoutSeconds 1200
```

`tools/oni_proton_headless.py` runs the same scenes in WindowsPlayer under
Proton, and `tools/oni_native_matrix.py` runs sources with their fresh-process
reloads from one frozen runner and package.

**Acceptance checks common to every scene.** `native-domain.json` records
physical progress, conservation, suppressed native phases, projection
controls, native solid conduction and renderer publication. The runner
requires these facts, the native backend report and the normal colony/probe
report. It also requires the game's `Grid` fields to match the physical
projection, texture writes for the current publication generation, the
calibrated GPU check and the scalar-save guards. It rejects idle workers,
changed compatibility cells, missing controls, nonfinite diagnostics and any
claim of whole-colony or playable readiness. Windows reports must come from
`WindowsPlayer`; an isolated native-worker or Wine report cannot pass a Unity
gate, and a passing offline native test is never enough.

`ColonyValidationConfiguration.LongRun` is the separate request for ten-cycle
runs of `native-domain`, `native-liquid-domain`, `native-mixed-domain` and the
two [configured rooms](native-configured-domain.md#saves-and-reloads); it
rejects the fixed-window fixtures and produces development telemetry, not
evidence of a playable colony ([long runs](../tools/ONI_HEADLESS.md#long-development-runs)).
The older `snapshot-copy` mode still rejects its unsupported incompressible
liquid placement; a passing sealed water scene does not repair that importer.

## Current evidence

Linux Unity runs use the licensed LinuxPlayer (ONI 744825) on a private
authenticated Xvfb desktop with rendering enabled: normal game processes, not
a mocked simulation or Unity `-nographics`. Windows OS runs use MSVC-built
libraries with ONI 744825 / Unity 6000.3.5f2; each process exits normally,
removes its temporary mod and restores all 13 original profile files, and the
installed SimDLL stays unchanged. Exact continuation compares runs of the same
binary; cross-compiler trajectory identity is not required. Platform terms are
defined in the [validation matrix](native-validation-matrix.md#platforms-and-terms).

| Platform | Verified |
| --- | --- |
| Linux Unity | Every base scene in this document: gas, water, heat, conduction, liquid heat, O2/CO2/water transfers, gas and water actors (including batches and initially disabled emitters), gravity, checkpoint, paired capture, ordinary save, save-failure recovery, and fresh-process reload of gas, water, gravity, liquid-transfer, liquid-heat and actor saves with 120 exact frames each; the feature scenes listed in each feature document; configured gas and water rooms |
| Windows OS (MSVC) | Mixed air; compressed water with ordinary save; conduction with game heat commands; liquid heat with save, fresh load and a second session through the main menu; CO2 transfers; gas gravity with transfers; filled-water gravity with a main-menu reload; gas and water actors with batches, the initially disabled water emitter and a water-actor reload through the main menu; ordinary air/water saves with fresh loads and the paused frame-zero autosave; Save As creation and fresh-process reload of a menu-created file; repaired liquid rendering and inspection |
| WindowsPlayer under Proton | The feature scenes listed in [Windows validation](windows-validation.md#windowsplayer-under-proton), with local and cross-player reloads against native Linux |
| Native and Wine | All native CTests in Linux Release and ASan/UBSan (including the Funchook/diStorm sources), MSVC `/W4 /WX`, the Ubuntu 22.04 compatibility build (glibc at most 2.34), and MinGW Windows binaries under Wine 10; the licensed `sealed-*` worker controls on Linux and under Wine. The MinGW package uses the normal DLL names, statically links its compiler/thread runtimes and imports only Windows system DLLs |

Scenes not listed for Windows OS have no recorded Windows OS run, notably the
standalone O2 and water point-transfer scenes, checkpoint and pair, the
save-failure UI, the feature scenes verified only under Proton and the
configured rooms.

Key Linux Unity results (six-cell scenes, 120 frames after projection priming):

- **Mixed air.** 2,541 solver substeps over 24.2 s. All six compatibility
  tuples stay unchanged while custom oxygen transport moves the target cell
  from 0.2068 to 0.2409 kg; 726 owned post-processing and 484 thermal calls are
  suppressed. Maximum relative mass/energy drift about 1.2e-15 / 7.3e-15.
- **Compressed water.** 1,100 kg per cell at 300 K and about 220 MPa with zero
  reported mass and energy drift; the native compatibility pattern stays
  `[1000, 1200, 1100]` in each row while the game grid reads the 1,100 kg
  projection. This is an equilibrium control, not moving water.
- **Heat and conduction.** Net +1,500 J with a U+K residual of about -1.6e-9 J.
  Conduction runs 5,124 thermal stages over 2,562 substeps, redistributes
  1.013 J and raises entropy by 1.32e-5 J/K. Liquid heat redistributes
  9.0625 J (entropy +3.278e-5 J/K) with zero measured U+K drift, the same on
  Windows OS.
- **Transfers.** Net oxygen 0.030000001192092896 kg, the exact difference of
  the game's binary32 amounts, with a U+K residual of about 1.6e-10 J; CO2
  matches. Water transfers close at -0.000496 J against the 0.005946 J
  roundoff bound.
- **Actors.** 32 consumer callbacks and 80 emitter status samples, both
  density edges once and 74 frames without actor transfers after removal; gas
  U+K residual about -1.8e-9 J against 1e-6 J, water -0.0028 J against
  0.0059 J.
- **Gravity.** Carried boundary potential 0.147150006 J, cumulative body
  impulse -1,664.2 kg m/s and a U+K+G residual of about -3.5e-9 J.
- **Checkpoint and pair.** A frame-6 image restores and advances to exactly the
  image the worker publishes for frame 7. Paired capture holds 3,585,197
  native and 3,507 physical bytes from one worker epoch.

Windows OS mixed air completes 121 frames with maximum relative mass/energy
error 9.26e-16 / 2.25e-14, and the paused hover shows
**Nitrogen–oxygen mixture (+traces)**, nitrogen 81.6054%, oxygen 18.3946%,
100,501.0868 Pa and 300.017112 K, matching the physical report. Water hover
shows **Fluid pressure 220 MPa** and **No gas phase / not breathable**, and the
water-actor reload keeps the repaired liquid rendering in both sessions. A
read-only managed reviewer checks a menu-created Save As file's paired section
and all physical bytes against the completed game frame, without invoking the
native simulator. These observations do not establish four-gas production
hover, other UI scales, rendering performance or a playable colony.

## Limits and open work

- Only the bounded development rooms are enabled. Known native mutation
  packets targeting owned cells or the border reject before dispatch; every
  other native writer still needs an adapter and dedicated coverage.
- General production actors, reserved machines and pumps, construction,
  cross-boundary fluid/solid heat (the Unity
  [solid-heat gate](native-domain-physics.md#native-solid-cell-heat) fails),
  open boundaries, vacuum and free surfaces are not implemented for the domain
  and must not enter it. Mixed native/custom batches reject.
- Persistence: pending callback outboxes, the Load screen's file selection,
  overwrite Save, crash durability, actor saves on Windows OS and general
  managed-reference integrity
  ([persistence](native-domain-persistence.md#same-process-main-menu-reload)).
  An older water input logs `Missing KPrefabID reference: 246809` on resave.
- Strict cross-platform agreement fails for compressed water and the heated
  mixture ([arithmetic scope](native-platform-numerics.md#solver-arithmetic-scope)).
- The [strong-flow accuracy gate](../native/MAC_STRONG_FLOW.md) fails, and the
  production timestep and whole-colony performance gates are open.
- Next: complete actor and process reservations and persistence, an occupied
  development colony with life support, then open and multiphase geometry and
  complete reader/writer coverage ([playtesting readiness](playtesting-readiness.md)).

Keep the adversarial native-state, thermal, grid, rendering and save-guard
checks as integration regressions. Native index translation and
secondary-base actor dispatch are part of the exact-build contract. Do not
weaken unchanged-native-cell assertions when adding writers or actors.
