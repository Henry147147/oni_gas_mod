# Sealed native fluid domain

This is the first bounded takeover experiment for the colony work. The
custom native engine advances a six-cell room while the surrounding ONI colony
retains its original simulation. The corrected mixed-air and compressed-water
equilibrium controls pass in actual Windows and Linux colonies. The basic
controls are actor-free development scenes; a separate
registered-actor control exercises an actual game consumer and emitter API.
Whole-colony ownership, first-playable and release gates remain closed.

## Execution and ownership

The mod attaches the exact-build native hooks before initialization. After a
disposable colony starts, the validation driver builds an unoccupied room with
an impermeable, adiabatic Unobtanium border. It verifies the border, absence of
objects, disease and radiation before enabling the domain.

The actual positive `ProcessNextFrame` return supplies the timestep and a
monotonic ticket. `UpdateData` advances the custom `MacClosedWorld` on the native
simulation worker, once for the global timestep. The domain's mutex now spans
`ProcessNextFrame` through `EndFrame`, including native cell work, custom work
and the original update. A managed reader cannot consume a partially completed
native frame. `BeginFrame` may wait for main-thread work and stays outside the
fence. Synchronization-only passes never advance custom time. There is
no managed advance timer.

The physical model retains constituent mass, U and canonical face momentum.
It uses the existing closed MAC pressure/transport solver, its compression
coupling, a declared ideal-gas/elastic-liquid reference and immutable candidate
advances. The initial air case contains nitrogen, oxygen and helium at trace abundance.
Carbon dioxide is also a supported physical constituent in the current model.
The water control initializes 1,100 kg in each cubic metre at 300 K: its
mechanical pressure is 220 MPa, with elastic energy retained separately from
thermal energy. These are explicit development materials, not a complete ONI
material catalogue or free-surface solver.

The driver first queues ONI's gas/liquid impermeability flags and verifies them
in the original native snapshot. Flag installation can displace fluid before
ownership begins. After activation it restores deliberately unbalanced,
nonvacuum scalar cells, then requires an exact match before starting the
30-frame acceptance interval. A missing projection fails within ten frames.

Native per-cell post-processing and pressure, gas, liquid, thermal and transition
entry hooks exclude owned cells. **Native indices include a one-cell halo:**
for public `(x,y)` and width `W`, the native index is
`(x+1) + (y+1)*(W+2)`. Configuration and snapshots use public coordinates; each
cell hook decodes the padded index before checking ownership. Start checks the
actual loaded simulator's padded dimensions against the requested public grid.
Do not apply public-grid indices directly to native cell phases.

Hardware watchpoints identified the earlier gas swap inside native
`PostProcessCell`: the previous hook excluded different cells because of the
wrong pitch. The corrected filter blocks the containing phase. Additional
three-cell pressure-displacement hooks filter every involved cell. Installed
gas-interaction entries still have zero calls, so their symbols alone are not
coverage evidence. Native functions continue outside the rectangle. A hot/cold
Granite pair outside the room must continue to conduct heat.

The scalar native cells are compatibility data. To challenge the exclusions,
the driver first lets original ONI move a deliberately unbalanced scalar mass
pattern. A nonuniform `[280, 300, 320] K` temperature pattern also challenges
thermal exclusion. It restores those patterns after activation and requires the original
element/mass/temperature/disease tuples to remain byte-identical while custom
physical state advances. This
control distinguishes a blocked native fluid phase from a native state that
would never have moved. It does not prove coverage of every game writer.

Each completed custom frame is copied through a separate `ma_oni_domain_*`
extension. An owned copy of all 47 native channels retains the surrounding
colony's events and nonfluid data. Only owned-cell mass, temperature and element
projections change. Immutable native thermodynamic readings feed the shared
mixture renderer and inspection path; scalar projections never reconstruct the
physical inventory. Publishing fails without replacing the last valid owned
snapshot, and a fault never enables scalar fallback.

The solver temporarily selects nearest rounding, masked exceptions and
FTZ/DAZ off on its executing thread. It restores the complete caller environment
before returning to Unity/native code. The normal strict arithmetic checks stay
enabled inside the solver.

## Internal fluid conduction

The native domain enables `MacClosedSettings.thermal_conduction`. Each accepted
physical substep prepares two conductive half stages around the existing
compression/pressure/transport stages. All stages share one clock, revision
and immutable candidate: a rejected thermal solve publishes no partial heat,
mass, actor receipt or elapsed time. The option remains off by default for
separate numerical controls whose physical equations exclude conduction.

Occupied, single-phase cells use the reference EOS heat capacity at fixed
volume. Gas conductivity is weighted by partial volume (equivalently mole
fraction); liquid weights use actual compressed partial volumes. A face's
conductance is its area divided by the sum of the two half-cell thermal
resistances. The existing backward-Euler thermal network advances these
contacts. Walls and exterior faces are insulated. Canonical constituent mass,
passive inventory and face momentum do not change during this operator.

Every stage checks local and global energy accounts and endpoint entropy at
bounds derived from its operands. Independent two-body equations and analytic
time refinement test the heat solve; source immutability and the owner's
single clock are tested separately. The additive thermal diagnostic ABI 1
uses an 80-byte status with session/frame, enabled state, stage/contact/iteration
counts, redistributed energy and entropy change. It also counts heat updates
smaller than the stored binary64 internal-energy resolution. These are reported
explicitly; the diagnostic does not claim arbitrary precision.

`native-conduction-domain` seeds a temperature gradient with the ordinary
+2,000 J / -500 J game heat commands. It requires positive internal heat
redistribution and entropy, exactly two thermal stages per solver substep,
matching diagnostic/session/frame values and the full existing game checks.
`sealed-conduction` exercises the corresponding licensed native-library
control outside Unity on Linux or Windows. Current support uses development
constant properties, rejects gas/liquid coexistence and variable heat capacity,
and excludes fluid/solid exchange, radiation and phase transitions.

## Conserved game heat commands

`SimMessages.ModifyEnergy` now routes owned-cell heating/cooling into the native
domain. The message's kJ is converted to joules. A complete batch reserves an
immutable heat candidate against the exact session and completed frame; enqueue
changes neither the published state nor time. The next native worker timestep
commits the candidate and physical advance together. Every constituent and
canonical face momentum is retained, and the physical clock is preserved.

The additive heat ABI 1 uses 48-byte commands and a 56-byte status. Pending and
committed batch counts distinguish acceptance from application. Accumulated heat
accounts retain full additive expansions; their scalar status values are
read-only projections. The U+K drift diagnostic includes the declared external
heat account. Invalid, stale, temperature-limited or unrepresentable batches
queue nothing; a failed physical advance publishes neither heat nor state.

This is a reserved external heat boundary, not a power/producer adapter.
Mixed native/custom batches require a cross-owner reservation and currently
reject before dispatch. Native heat outside the arena still uses ONI.

## Gravity and carried potential energy

Scenario 2 (`native-gravity-domain` in the game, `sealed-gravity` in the
isolated licensed-library control) enables the closed owner's existing
9.81 m/s2 downward gravity. Its force/work calculation uses the actual face
mass fluxes. The energy account includes internal energy, resolved face
kinetic energy and `sum(m*g*y)` at cell centres. The bottom of the owned
rectangle is the potential datum; it is used consistently for all transfers.
The other scenarios keep their explicitly zero-gravity controls.

Gas injection carries potential at its destination elevation; extraction
carries potential at its source elevation. This applies to both point
messages and registered consumers/emitters. Equal masses at different
elevations therefore still have a boundary-energy exchange. Pending transfers
reserve this account without changing physical state. The native worker
commits it with material/kinetic accounts, actor receipts and the physical
advance. Invalid batches and failed timesteps retain the previous accounts.

The additive gravity ABI 1 has a 96-byte status: session/frame, acceleration,
initial/current potential, committed/pending boundary potential, cumulative
body impulse, signed internal gravity work and maximum absolute total-energy
residual. These are diagnostics of the physical owner. The game control
independently reconstructs potential from all completed cell masses and
heights, requires vertical momentum and force, injects/consumes actual gas
through game callbacks, and checks the complete U+K+G boundary balance.
The native regression also exercises registered actors at different elevations.
This occupied sealed-room control does not establish free surfaces, arbitrary
nonlinear buoyancy or whole-colony gravity coverage.

## Gas point sources and game callbacks

Owned-cell `SimMessages.EmitMass` and single-cell `ConsumeMass` now reserve
physical gas transfers against the same completed native frame. Injection
arrives at rest; selective extraction carries the selected gas's internal
energy and its share of face momentum and kinetic energy. Injection retains
existing momentum and returns the kinetic mixing loss as heat. Every other
constituent, including trace helium, remains in the physical inventory.
These are declared point-source boundaries; pumps still need accountable
aperture flow, work and output reservations.

The current transfer ABI 2 has 64-byte commands, 88-byte receipts and a
104-byte status with all five constituent accounts. Heat and mass commands share an ordered immutable candidate.
Enqueue publishes no physical change or callback. The native worker commits
the candidate, advance, constituent/energy accounts and receipts together.
Session IDs, expected frames and receipt sequences reject stale work.
Receipts remain in a bounded outbox until acknowledged with a coherent owned
snapshot. The snapshot appends the actual game's mass callback records;
normal game callback managers then deliver them. Callback handles retain all
32 bits. Binary32 callback amounts agree exactly with the physical transfers;
unrepresentable extraction remainders stay in the source.

The real-game control injects 0.05 kg of oxygen and consumes 0.02 kg using the
ordinary game message APIs. It requires one successful emission callback and
one consumption callback, unchanged original native cells, no original native
message dispatch, and independently checked mass and U+K accounts. Fault
snapshots clear already delivered event lists before retaining the last physical
frame, so a stopped publication cannot replay callbacks. Handle-indexed emitter,
chunk, building-temperature and disease status tables remain owned and available
while paused; they are read by game components independently of frame events.

Current transfers support nitrogen, oxygen, helium and carbon dioxide in occupied gas cells.
Liquid/empty-cell transitions, disease-bearing injection, spatial consumption,
mixed-owner batches, spatial/bulk actors and producer reservations require
additional adapters. Unsupported requests reject before dispatch.

The current domain ABI 2 uses 96-byte cells: nitrogen, oxygen, helium, water and
carbon dioxide masses followed by the physical readings. Domain configuration
and status stay 40/440 bytes; heat and actor extensions keep ABI 1. Native and
managed clients reject the older four-species layout. The explicit reference
model is `oni-sealed-domain-v2`; this is still a limited development catalogue.

## Registered gas actors

Selective single-cell consumers and disease-free single-cell emitters use the
same conserved point-source boundary. ONI allocates the real actor handle and
delivers its ordinary registration callback. The adapter keeps the native
transfer at its initial inactive setting, admits the full handle, then routes
subsequent rates and intervals to the custom worker. Native actors outside the
domain continue normally. Moving a live actor between owners or admitting an
already active native transfer requires a separate reservation and rejects.

Actor ABI 1 uses 80-byte configurations and a 64-byte status. Consumption uses
the actual native timestep; its binary32 reporting remainder stays with the
actor's demand. Emission retains interval time, resets the interval on changes,
and reports blocked/unblocked edges once. The existing game's output limit is
a gas density in kg/m3; this compatibility setting is not labelled as pascals.
Actor changes, physical transfers, accounts, clocks and receipts are prepared
together. A late actor or solver failure commits none of the candidate.

Completed consumer records feed the actual `ElementConsumer.AddMass` path.
Emitter records overlay the allocated native status slot; they are status
samples, not callback events. The latest completed emission status remains
available during synchronization-only queries. Native positive transfers for
an admitted handle fail verification. Block notifications use ONI's normal
callback manager. Removal retires the custom actor and ONI's inactive handle.

Owned actor rate/interval configuration batches now prepare all commands
against one session/frame and commit the ordered registry update together.
The additive `ma_oni_domain_configure_actors` entry point keeps actor ABI 1;
it accepts at most 256 commands. A late invalid material, stale frame or
missing removal handle changes no earlier configuration or output. Repeated
handles follow command order, with the final configuration governing the next
physical timestep. Registration, configured rates and physical execution are
separate; configuring a batch advances no time and emits no mass callbacks.

The managed native-message bridge preflights ownership before admission.
Supported batches containing only owned consumer/emitter configurations use
the custom registry; batches of unowned actors retain native dispatch.
Moving an owned handle outside, mixing owners, or batching owned native
registration/removal still requires additional admission/retirement work and
rejects before dispatch. Forwarding an inactive registration to a native
outside transfer retires its eligibility for later custom admission.

`native-actor-domain` spawns an actual Unity `ElementConsumer`, registers an
emitter through the ordinary game API, changes both rates, blocks/unblocks
output, disables and removes the actors, then verifies continuing physical
frames without further actor transfers. It does not reserve producer inventory,
power or output work, and it does not enable pumps, spatial consumers, disease,
construction or general building gameplay.
The same control also sends two ordered rate changes for the actual owned
consumer in one batch, verifies the batch avoids original native dispatch,
and forwards a zero-rate batch for a second actual Unity consumer outside the
domain. Subsequent physical callbacks must reflect the final owned rate.

## Run the actual Linux colony controls

The [physical checkpoint format](../native/MAC_CHECKPOINT.md) now preserves the
complete native owner, including physical U and full canonical momentum.
`native-checkpoint-domain` checks restored continuation against the exact next
worker frame in the real game. Export requires a drained actor-free frame.
General actor/outbox persistence remains unfinished.
The actor-free save/load path below now pairs both owners with
Unity's normal managed colony; both scalar-save guards remain enabled outside
that single staged save call.
`native-paired-checkpoint-domain` additionally requests capture at the native
worker barrier, verifies the original native cell tuples in that saved image,
checks immutable native/physical buffers and encodes and validates their shared
`MACOLONY` container. It remains a bounded paired capture, with ordinary
general save/load admission and actor persistence still unfinished.
The isolated `sealed-restore`, `sealed-restore-water` and
`sealed-restore-gravity` controls now reload both paired sections through the
licensed native loader. They preserve the complete physical state/clock,
restore native flow flags at zero time and match twelve resumed worker images
exactly. Source validation rejects corrupt metadata, inventory, geometry,
phase or clock before native loading. This passes on Linux and the Windows
library under Wine. The bounded Unity binding is described below.

### Paused colony persistence

The ordinary `SaveLoader.Save` entry now pauses/synchronizes the bounded owner,
captures both owners and writes a full ONI colony. One staged `Sim.Save`
writes the paired envelope. The save finalizer retires the pair and restores
the exact prior pause count. Pending commands, live owned actors, undelivered
receipts, nested calls and foreign threads reject before serialization.
`native-save-domain` exercises this entry from a running colony, including a
rejected pending-command save that retains its command and creates no file. The file
is flushed and renamed from a fresh temporary destination, and the physical
state must remain byte-identical during serialization.

`native-load-domain` loads that file through the normal scene loader in a fresh
process. It validates both sections before native Load, restores the exact
physical owner after native startup, reinstalls flow flags at zero time and
delivers retained native startup events once. The managed scene, grid,
renderer, outside native heat and every resumed physical frame are verified.
Gas, compressed water and gravity retain their saved scenario. The optional
`--resave-before-resume` saves through `Game.StartDelayedSave` and the normal
autosave coroutine at restored
publication frame zero, requiring the exact saved state/clock and unchanged
pause depth before resuming. New owners at physical time zero remain ineligible.
This remains an actor-free development path; actual menu clicks/day-rollover triggers,
owned actor persistence and whole-colony ownership remain open.

The Linux runner now uses a stable private Dev mod identity across fresh
profiles. For a file made by the older runner, supply its original folder ID
with `--mod-id` to avoid ONI's informational Mod Differences dialog.

```sh
python3 tools/oni_headless.py run --package <package> \
  --mode native-save-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package <package> \
  --mode native-load-domain --load-save <first-run-native-colony.sav> \
  --resave-before-resume --frames 120 --timeout 1200
```

The source file is copied into the new run and its SHA-256 checked before and
after staging. The game loads that private copy. The Windows wrapper accepts
`-NativeDomain save` and `-NativeDomain load -LoadSave <file> -ResaveBeforeResume`.
Use `--save-colony` / `-SaveColony` with another initial native domain scene.
The [Windows feature cohort](#additional-windows-feature-controls) below now
verifies ordinary air/water writes, fresh-process loads and deferred paused
autosaves through these entries. Actual menu clicks and day-rollover triggers
remain separate checks.

Build the native backend and the normal ONI adapter, then create a fresh package:

```sh
python3 tools/build.py --oni-native-observer --build-dir build-oni-observer \
  --game-dir /path/to/OxygenNotIncluded
python3 tools/package_probe.py \
  --native build-oni-observer/libmixed_atmospheres.so \
  --native-observer build-oni-observer/libmixed_atmospheres_oni.so \
  --output artifacts/sealed-domain-package
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-domain --frames 30 --timeout 600
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-liquid-domain --frames 30 --timeout 600
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-heat-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-transfer-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-actor-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-carbon-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-conduction-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-gravity-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-checkpoint-domain --frames 120 --timeout 1200
python3 tools/oni_headless.py run --package artifacts/sealed-domain-package \
  --mode native-paired-checkpoint-domain --frames 120 --timeout 1200
```

These runs use separate fresh profiles on the authenticated virtual desktop.
`native-domain.json` records physical progress, conservation, suppressed native
phases, projection controls, solid conduction and renderer publication. The
runner requires these facts and the native backend report, in addition to the
normal colony/probe report. It also requires the actual game's `Grid` fields
to match the physical projection, texture writes for the current publication
generation, and a save attempt rejected before native saving or any output.
It rejects idle workers, changed compatibility cells, missing controls,
nonfinite diagnostics and claims of whole-colony or playable readiness. A
passing offline native test is insufficient to pass this game-level gate.

The old `snapshot-copy` mode still stages the complete generated colony and
still rejects its unsupported incompressible liquid placement. A passing sealed
water experiment does not repair that importer or authorize full-world import.

## Current evidence

Actual native Linux Unity runs on ONI 744825, 2026-10-16, use a private
authenticated Xvfb desktop with rendering enabled. They are normal licensed
game processes, not a mocked simulation or Unity `-nographics` mode.

| Run | Result |
| --- | --- |
| `20261016T181635Z-d3a772` | Failed. Initially paused because of an incorrect speed-control call; manually unpaused for diagnosis. It does not count as an automatic pass. |
| `20261016T182613Z-13cfc2` | Failed. Original-flow control moved, but restored compatibility cells did not remain fixed. |
| `20261016T183805Z-270de8` | Failed. Additional gas entry hooks did not stop native transport; their counters stayed zero. |
| `20261016T185522Z-8a9584` | Failed before physical advancement. Flow-flag installation displaced scalar gas during bootstrap. |
| `20261016T191212Z-b81fc7` | Failed after ten custom native frames (2.0 s, 210 solver substeps). The restored scalar pattern was verified at frame one, then two scalar masses were exchanged. The original-flow control, flow flags, continuing native solid heat, copied native channels and retained physical frame checks succeeded. Renderer, game-grid projection and save-guard acceptance were not reached. |
| `20261016T191955Z-858237` | Passed compressed-water equilibrium control: 32 native frames, 30 after projection priming at frame two, 6.4 s and 672 solver substeps. Original-flow contrast, unchanged native compatibility tuples, copied channels, retained physical frames, actual game-grid projection, current-generation texture writes, native solid heat and save rejection all pass. No mod failure lines were reported. |
| `20261016T194104Z-564b3b` | Passed corrected mixed-air control: 121 native frames, 120 after projection priming, 24.2 s and 2,541 solver substeps. All six mass/temperature compatibility tuples remain unchanged, while custom oxygen transport, copied snapshots, all-six-cell game-grid projection, current-generation rendering, native solid heat and save rejection pass. No mod failure lines. |
| `20261016T194433Z-dc7013` | Passed corrected compressed-water control: 122 native frames, 120 after priming, 24.4 s and 2,562 solver substeps. Zero mass/energy drift, 1,586 excluded liquid calls, 488 thermal calls and 732 post-processing calls. All-six-cell grid projection, rendering, exact compatibility tuples, solid heat and save rejection pass. No mod failure lines. |
| `20261016T195730Z-4b7c8c` | Passed actual game heat messages: +2,000 J heating and −500 J cooling, committed on the native clock. Net physical U+K rises by 1,500 J, with residual −4.54e−9 J after 120 post-priming frames. The original native message entry is never called for either heat packet; grid, rendering and native compatibility checks pass. |
| `20261016T200324Z-fbdef4` | Repeats the heat pass and additionally verifies that a direct native `SIM_BeginSave` returns null/zero without starting a scalar save. Normal managed saving also rejects before writing. |
| `20261016T200702Z-8e3b53` | Passes after merging the new geometric momentum transport: 122 native frames, 120 after priming, 2,562 substeps, two heat commands and net 1,500 J. U+K heat residual is −1.63e−9 J, maximum relative mass drift 2.06e−15 and energy drift 3.94e−15. Grid, rendering, unchanged native tuples, native solid heat and both save guards pass, with no mod failure lines. |
| `20261016T202909Z-bef415` | Passes real gas injection/consumption and both actual game callbacks exactly once: 122 native frames, 120 after priming, 2,562 substeps. Net oxygen addition is 0.030000001192092896 kg (the exact difference of the game floats); mass residual is 2.01e−15 and U+K residual 1.62e−10 J. Extraction carries 0.0003848342 J of resolved kinetic energy. Both receipts are acknowledged, native compatibility tuples stay exact, grid/render/solid-heat/save checks pass and no mod failure lines occur. Package source is `6bb2b53` plus the recorded dirty gas-transfer changes; private manifests retain the individual library hashes. |
| `20261016T204654Z-021e13` | Passes actual Unity `ElementConsumer` and registered emitter integration: 122 native frames, 120 after priming, 2,562 substeps. The consumer receives 32 positive samples totalling 0.20000000530853868 kg; 40 emitter status samples account for 1.09375 kg. Both blocked/unblocked game callbacks fire once. Rate changes, interval resets, disable/removal and 74 subsequent native frames without actor transfers pass. U+K residual is −1.78e−9 J and constituent-mass residual 1.03e−15. Original native tuples, actual game emitter status, grid, renderer, solid heat and both save guards pass. Package source is `ea948bd` plus the recorded actor changes. |
| `20261016T205811Z-f4c02b` | Passes physical carbon-dioxide injection/consumption on domain/transfer ABI 2: 122 native frames, 120 after priming, 2,562 substeps. Actual game callbacks report 0.05 kg CO2 at 310 K injected and 0.02 kg consumed at 300.481354 K. The fifth mass account retains net 0.030000001192092896 kg; chemical/internal energy and carried kinetic energy remain in the U+K account. Energy residual is −3.33e−9 J and constituent-mass residual 1.85e−15. The actual mixture reading contains transported CO2; native tuples, grid, rendering, solid heat and save guards pass, with no mod failure lines. Package source is `203fd55` plus the recorded ABI/CO2 changes. |
| `20261016T210133Z-3a5041` | Repeats the complete registered-actor pass on domain ABI 2 and the five-species model: 32 actual consumer callbacks, 40 emitter samples, one blocked/unblocked callback each, retained original cells, game status/grid/rendering, conservation and removal checks. No mod failure lines. |
| `20261016T210446Z-16a99c` | Repeats the compressed-water control on domain ABI 2: 32 native frames, 30 after priming, 672 substeps, 1,100 kg/cell at 220 MPa, zero reported mass/energy drift. Native tuples, solid heat, actual grid/rendering and both save guards pass without mod failure lines. |
| `20261016T220819Z-dbcb0f` | Passes internal fluid conduction on the reconstructed-momentum owner: 122 native frames, 120 after priming, 2,562 solver substeps and 5,124 thermal stages. It redistributes 1.01282699 J internally with entropy increase 1.31980e-5 J/K; U+K residual against the external 1,500 J heat input is -3.03e-9 J. Maximum relative mass/energy drift is 1.54e-15 / 5.51e-15. Thermal diagnostics report 35,868 contacts, 4,881 iterations and 222 updates below stored energy resolution. Exact native tuples, game grid/rendering, external native solid heat and both save guards pass, with no mod failure lines. Source is `83b1541` plus the recorded conduction changes; private package manifests retain binary hashes. |
| `20261016T221722Z-2eb0f8` | Repeats the complete conduction pass after pulling the separate compression-heat component from main. All physical counters and thermal metrics match the earlier run; both save guards and exact compatibility cells pass, with no mod failure lines. Source is `7a781d2`; the separate component is preserved without adding it to this owner. All twelve cross-compiled native tests execute under Wine; six Windows forwarding comparisons remain identical. |
| `20261016T222138Z-5dd9d7` | Passes native-clock gravity and actual gas transfers: 121 native frames, 120 after priming, 2,541 substeps and zero rejected attempts. Initial/current potential is 68.4864608 / 68.7330484 J; the actual 0.05 kg injection and 0.02 kg consumption carry net 0.147150006 J of boundary potential. Both game callbacks fire once. Cumulative downward body impulse is -1,664.20014 kg m/s, with signed internal gravity work -0.0520605 J. U+K+G transfer residual is -3.48e-9 J; maximum relative mass/energy drift is 2.01e-15 / 6.09e-15. Exact native cells, game grid/rendering, native solid heat and both save guards pass, with no mod failure lines. Source is `5d39a15` plus the recorded gravity changes. |
| `20261016T223037Z-cf58d3` | Passes atomic owned actor configuration and outside native-batch forwarding with actual Unity consumers. The two ordered owned rate commands avoid native dispatch; the outside zero-rate consumer batch reaches the native batch entry exactly once. Over 120 post-priming frames, 32 consumer callbacks account for 0.2000000053 kg and 40 emitter samples account for 1.09375 kg. Blocked/unblocked callbacks, exact native cells, physical game status, grid/rendering, solid heat and both save guards pass. Removal is followed by 74 further frames without actor transfers. U+K residual is -2.58e-9 J; relative constituent drift is 1.01e-15. No mod failure lines. Source is `4f03eac` plus the recorded batch changes, including the pulled separate heat-component repair. |
| `20261016T224334Z-cee2a7` | Passes physical checkpoint restore and exact continuation in the actual Linux game: the 3,503-byte source at frame 6 / 1.200000018 s independently restores/advances to the exact image published by native worker frame 7 / 1.400000021 s, with zero observation retries. Expected/actual SHA-256 is `ef895aa34a42ec337bbb0143d9ce870e0f873afdce4dbd673988c987817ba32c`. The full scene completes 122 frames, 120 after priming, 2,562 substeps and 24.4 s; relative mass/energy drift is 1.19e-15 / 5.72e-15. Exact native tuples, grid/rendering, native solid heat and both save guards pass without mod failure lines. Source is `e65ebe3` plus the recorded checkpoint changes. Windows native code under Wine also reads/reserializes this actual Linux game image without changing a byte; full colony save/reload remains open. |
| `20261016T225904Z-9e5553` | Repeats the full checkpoint game pass with the native worker fence extended through native processing and `EndFrame`. It completes 120 post-priming frames, exact restored/live continuation, unchanged native tuples, grid/rendering, solid heat and both save guards, with no mod failure lines. Source is `42cae2b` plus the recorded fence changes. Both licensed native-library checkpoint controls pass with that fence too (Windows under Wine). |
| `20261016T232719Z-0a1d98` | Passes paired native/physical capture on the merged compression-heat solver and physical file version 2. Native worker thread 4 captures frame 6 / epoch 541 at 1.200000018 s: 3,585,197 native bytes and 3,507 physical bytes. The physical hash also exactly matches the independently restored next-worker image. The 3,588,900-byte combined envelope retains both sections, original native tuples and geometry; immutable recopy and token retirement pass. The scene completes 121 frames, 120 after priming, 2,541 substeps; relative mass/energy drift is 1.72e-15 / 8.29e-15. Grid/rendering, native solid heat and both scalar-save guards pass, with no mod failure lines. Source is `13afbef` plus the recorded paired-capture changes. Ordinary colony save/reload remains open. |
| `20261016T233539Z-b39f30` | Repeats the real consumer/emitter and atomic actor configuration pass on the merged default compression-heat solver. It completes 121 frames / 2,541 substeps, consumes 0.2000000053 kg and emits 1.09375 kg; the actor energy residual is -2.87e-9 J. Relative mass/energy drift is 1.54e-15 / 5.84e-15. Native tuples, game grid/rendering, native solid heat and both save guards pass. |
| `20261016T234845Z-2dc5ea` | Repeats gravity and actual gas callbacks on the merged default compression-heat solver. It completes 121 frames / 2,541 substeps with relative mass/energy drift 1.30e-15 / 5.31e-15 and a boundary U+K+G residual of -3.21e-9 J. Carried potential is 0.147150006 J; maximum absolute physical energy residual is 3.15e-9 J. Native tuples, grid/rendering, native solid heat and both save guards pass. |
| `20261017T000916Z-64e170` | Passes full paused colony serialization through normal `SaveLoader.Save`, with one staged paired sim section. Source frame 31 / 6.200000092 s retains complete physical SHA-256 `4134507cf55e6af6aaab72ed3a55d6e0ef0b4516f0509e02c224cf70ebbf8c72`. The 742,877-byte colony file has SHA-256 `974bdcde3a3d327320b1b898800ef30e15d36f8b9b96c17fc0d82f973081579f`; serialization changes no physical byte. All normal room, rendering, outside heat and save-guard checks pass. |
| `20261017T001440Z-b1f96f` | Passes fresh-process loading of that real colony file: original colony GUID and all three duplicants return, the initial physical image is exact, both zero-time startup event batches deliver once, and all 30 native worker frames match independent continuation. Final time is 12.200000182 s; relative mass/energy drift is 6.62e-16 / 3.75e-15. Final image SHA-256 is `b8404589c7180b583d421fbc6a1012bb9c0314bfcc1739f45d90ee855844f929`. Native tuples, game grid/rendering, outside native heat and save guards pass without mod failure lines. An informational Mod Differences dialog caused by the old runner's changing folder ID was acknowledged after checking it listed only the same integration mod. The runner now supports a stable ID. |
| `20261017T001826Z-0aaf84` | Repeats the same real-file load fully automatically with the source mod ID preserved; no footprint mismatch or UI intervention occurs. All 120 resumed frames / 2,520 substeps match independent continuation exactly, reaching 30.200000450 s from the saved 6.200000092 s. The complete initial hash and colony GUID/three duplicants are retained, both startup event batches deliver once, and final physical SHA-256 is `bc654e2b5435ca3f69a2ad9aa0f8699170d39f431d3a6b2f9a3a1566bc3a1913`. Relative mass/energy drift is 1.20e-15 / 5.53e-15. Grid/rendering, exact native cells, outside native heat and save guards pass with no mod failure lines. |
| `automatic-admission-merged-gas-save-v1` / `automatic-admission-merged-gas-load-v1` | Ordinary unprepared save admission passes on the merged acoustic-compression owner. The pending-heat rejection creates no file and retains its command/clock. The source at frame 32 / 6.400000095 s has physical SHA-256 `034ad943dae3e385e725da0fe9b6b097652398094872d1e13056bbf39253c0cb`; the colony file is `88df3ecdf0399e0261062827d43bd6a06acd285307421be520b4135817477f52`. Fresh-process loading matches all 120 worker frames, ending at physical SHA-256 `580face75d1b2cfe0227b2c700feda6d9dac1db0873c371d8c2a9a7bd3987e20`. These earlier reports predate explicit pause-depth fields. |
| `automatic-admission-merged-water-save-v1` | Compressed-water ordinary save passes at frame 32 / 6.400000095 s. Physical SHA-256 is `250c2efd1211ab89953e68e4815fa0f239c2e5dd0b09dab0251353d44fae27e7`; file SHA-256 is `509f4bea47789364c7e3942f104064065274f2f0874005f8f7b1d16f2c0e6749`. |
| `automatic-admission-merged-water-load-v1` through `v3` | Retained failures: the first observes 119 exact frames then skips an intermediate publication; the second allows two positive frames before initial paused resave; the third writes an exact frame-zero save but leaks a reference-counted pause and never resumes. The third disposable game was stopped after diagnosis. These are not passes. Publication pacing, initial pause retention and balanced pause requests repair the distinct causes. |
| `automatic-admission-merged-water-load-v4` | Passes an ordinary paused resave before the first new physical timestep and then all 120 exact worker frames. Saved frame is zero, saved time and physical hash match the source, and pause depth is exactly 1 before and after saving. Resaved file SHA-256 is `e11c641081a3c0f26596cac05cc6af76a65042b7f14548bde2aee794eb4d3c76`. This cohort uses the manual-save flag; the later optional resave fixture exercises the autosave flag. |
| `automatic-admission-framezero-water-reload-v1` | Loads that frame-zero file in another fresh game process. Original colony GUID/three duplicants and the initial complete physical image return. All 120 frames / 2,520 substeps match independent continuation, reaching 30.400000453 s with zero reported mass/energy drift. Final SHA-256 is `89e5b8f4aa5112e4accebe74af662b2617273955cbd78bdb3b5711e36b65d3f5`. Grid/rendering, exact native tuples, outside native heat and save guards pass, with no mod failure lines. |
| `automatic-admission-baseline-gravity-save-v1` / `automatic-admission-baseline-gravity-load-v1` | The Ubuntu 22.04 native build passes in the actual Linux player. Ordinary save retains source frame 32 / 6.400000095 s, physical SHA-256 `a006387843d80a367a338a924610145397d5eaaef8bc93937992afbf6e8834f2`, and colony-file SHA-256 `26ae12bc6baa895c94a28ecd1e1d0bb181299a4bfc67a490fbebec126fda08f5`. Fresh-process loading preserves gravity, GUID/three duplicants and the exact initial image. A normal autosave at publication frame zero retains the original pause depth 1 and exact physical time/image; its file SHA-256 is `78c030050bf972a0d8aec6234ccb46d6887d52753d799a7004a3c769f08d271f`. All 120 continued frames match independent replay; final physical SHA-256 is `7a89a92f6ffe56800d3c8fa541d60c52c8d78369b2ea2c877eac04dd90b17b47`. Relative mass/energy drift is 1.80e-15 / 3.35e-15; all ordinary grid/render/native-state/heat/save checks pass without mod failures. Native source is frozen before `6f39991` selects the later matched heat default; this cohort retains heat mode 1. |
| `automatic-admission-matched-gas-save-v1` / `readback-matched-gas-load-v1` | Rebuilt Ubuntu libraries include `6f39991`; inspection of the saved physical settings confirms compression 2 and heat 2. Ordinary save and pending-command rejection pass at frame 31 / 6.200000092 s, physical SHA-256 `e0c675190f5f75874b10f73aaf313eae1fb50dc9dfb6568c9348972e2be515fb`. Fresh-process loading restores that exact image and autosaves it again at frame zero. Readback verifies the complete 5,107,974-byte ordinary body (`8328ed51c4fc7a3f64eb351cc2c8d89b7d675aa1ae6b26545218d9e3b5214d81`), exact paired sim section and colony GUID. Pause depth remains 1. All 120 resumed frames match independent replay, ending at physical SHA-256 `e721d024bcecba9b73891f63045d25c504ae2bd2e0ed55feec77ea94d1b37d4e`; relative mass/energy drift is 1.55e-15 / 2.51e-14. Grid/rendering, outside native heat and scalar save guards pass with no mod failure lines. This cohort calls the public autosave entry directly; the next fixture additionally uses the deferred coroutine. |
| `readback-deferred-legacy-gravity-load-v1` | The latest matched-heat binary loads the preceding frame-zero gravity autosave while retaining its original heat setting 1 and compression 2. `Game.StartDelayedSave` runs its actual deferred coroutine and produces one autosave section at frame zero without changing physical time/state or pause depth 1. The complete 5,104,558-byte serialized body matches disk readback (`f932e778ba95f29e59a28ce4ecb612b11b8ffa565922c5aafc247420dddc0090`); file SHA-256 is `ebb0fa55e8b4b8167f8cac4969e8f80e61ad73af95dbc6c7fb622e49256fbd04`. All 120 resumed images match independent continuation and the preceding gravity cohort, including final physical SHA-256 `7a89a92f6ffe56800d3c8fa541d60c52c8d78369b2ea2c877eac04dd90b17b47`. Grid/rendering, native state, outside heat and save guards pass without mod failures. |
| `matrix-matched-v1-save-domain-save` / `...save-domain-load` / `...liquid-domain-save` | The frozen `fdec539` runner and matched-heat Ubuntu package pass mixed-gas ordinary save and 120 exact reloaded frames, then compressed-water ordinary save after 120 post-priming frames. The following load is retained as a failure, so the whole matrix is not a pass. |
| `matrix-matched-v1-liquid-domain-load` | Fails initial paused resave: one positive frame advances the restored source from 24.400000364 s to 24.600000367 s before the save starts. The pause count alone cannot exclude Unity's previously latched positive delta time. No file is written and the failed disposable session ends. |
| `paused-tick-fixed-matched-water-load-v1` | Reloads the same source with the simulation-tick gate. Nine startup ticks are held, including three positive intervals and the explicit 0.2-second challenge. Native clock, complete physical bytes and managed accumulators remain unchanged for the frame-zero deferred autosave; pause depth remains exactly 1. Its 5,135,996-byte body matches disk readback (`cb7d0ceaddf90d6bc79723344b96a252c35094ce4991d447072863164bfd9332`). All 120 resumed frames match independent continuation, reaching 48.400000721 s and final physical SHA-256 `f6f6f28e99e385156aa28afbf1f30409ec32bfad25525c3ad3fef2207ee328db`. Grid/rendering, native tuples, outside heat and save guards pass with no mod failure lines. |

The gas run's maximum relative constituent-mass drift was `5.29e-16` and total
energy drift `9.87e-16`; oxygen transport changed the target cell. Solver thread
4 differed from activation thread 3. These establish physical solver progress,
not exclusive native ownership. Failure preserves the last valid owned snapshot,
stops custom advancement and keeps saving disabled. It cannot repair the
already observed native write.

The corrected gas run has maximum relative mass drift `1.20e-15` and total
energy drift `7.31e-15`. The target oxygen mass changes from `0.2067902267` to
`0.2409221610` kg. It suppresses 726 owned-cell post-processing calls and 484
thermal calls. Both physical values and exact compatibility tuples are checked;
the earlier nonzero filter counters were insufficient because they referred
to different padded-grid cells. The first water run above is retained as
historical equilibrium/presentation evidence, not proof of the corrected
per-cell filter. The ABI 3 isolated gas and water controls additionally pass
sixty post-priming frames with the temperature gradient.

The water run retains exactly 1,100 kg per cubic metre at 300 K and approximately
220 MPa, with zero reported mass or energy drift. The native compatibility
pattern remains `[1000, 1200, 1100]` in each row, while the actual game grid reads
the custom 1,100 kg target-cell projection. Its physical state is an equilibrium
control: this does not establish moving water, gravity, free surfaces or
multiphase transport. The saved screenshot is `native-domain.png` in that run's
private colony-validation directory. Reports retain whole-colony and
first-playable readiness as false, with saving disabled.

Private reports, game logs and captures remain under
`<headless-state>/runs/<run-id>`; build and sanitizer logs are in
`artifacts/headless-working-20261016`. Neither licensed game data nor private
reports are redistributed. All eleven native CTests pass in Release and with
ASan/UBSan, including instrumented Funchook/diStorm dependency code. Current Windows x64 code also passes all eleven native tests, six forwarding
comparisons and three sealed-domain controls under Wine 10. This executes the
licensed Windows SimDLL and the Windows backend, without a Unity game process.
That historical Wine cohort supplies no actual Windows OS or Unity evidence;
the later MSVC/Windows gas run is recorded below.

The Windows controls use the matching PDB's `SimData` width/height offsets 0/4
and verify them against the loaded padded grid before activation. The same
public/native halo translation protects each platform's post-processing and
thermal phases. Windows inlines the gas interaction routines; their absent
hook slots are not counted as executed coverage. The sixty-frame adversarial
native-cell control passes through the containing phase exclusions.

Private Windows-binary evidence is under `artifacts/native-integration-r2/`:
`wine-forwarding-tool`, `wine-domains-tool` and `wine-native-ctests`. The gas
case reports mass drift 9.26e-16 and energy drift 2.77e-15; water retains exactly
1,100 kg/cell at 220 MPa with zero drift; net 1,500 J heating has a U+K residual
of -1.17e-9 J. Every case advances after a rejected scalar save. Each summary
explicitly records `actualWindowsOS: false` and `unityGameTest: false`.

The MinGW package uses the normal Windows DLL names and statically links its
compiler/thread runtimes. Import inspection shows only Windows system DLLs.
A clean package with just the two native libraries passes all 89 managed/native
Core tests through the normal Windows bootstrap, plus all three licensed
Windows domain controls under Wine. Evidence is in `windows-package-core-tests.log`
and `windows-package-domains` beside the other private reports. The managed
adapter also compiles against the actual Windows game declarations.

For an actual Windows Unity colony, build a Windows observer-enabled package
and run the normal disposable-profile wrapper in PowerShell 7:

```powershell
./tools/run_colony_validation.ps1 -Package ./artifacts/windows-probe `
  -NativeDomain gas -Frames 120 -TimeoutSeconds 1200
```

`-NativeDomain` also accepts `water`, `heat`, `transfer`, `actor`, `carbon`,
`conduction`, `gravity`, `checkpoint`, `pair`, `save` and `load`.
The actor scene needs at least 80 frames; use 120 for its full control. Python
must be available for the shared portable acceptance checker. It requires
`WindowsPlayer` evidence and the same physical, native-source, rendering,
callback, save and scope checks used on Linux. An isolated Wine report cannot
pass this Unity gate.

## Actual Windows gas-owner run

The `windows-native-domain-r3` private cohort passes the six-cell mixed-air
control in Windows ONI 744825 / Unity 6000.3.5f2 using the native MSVC package
built in `windows-native-domain-r2`. Source is `6f39991` plus the recorded
two-file startup-linkage/ABI-test patch. It completes 121 physical frames,
120 after projection priming, 2,541 substeps and 24.20000036 physical seconds,
with zero rejected attempts. Solver thread 12 differs from activation thread
11. Maximum relative constituent-mass/energy error is `9.26e-16 / 2.25e-14`.
Original native cells remain byte-exact; completed snapshot copy, retained
frame, flow exclusions, worker fence, native solid heat, game grid, rendering
and both scalar-save guards pass. The portable checker accepts `WindowsPlayer`
evidence. Whole-colony and first-playable readiness remain false.

A computer-use screenshot shows the retained completed reading at revision
121: **Nitrogen–oxygen mixture (+traces)**, `Breathable`, nitrogen `81.6054%`,
oxygen `18.3946%`, `100501.0868 Pa`, `300.017112 K` and `1.158178546 kg`.
The agent did not click the tile. The benign helium trace is omitted from the
bulk contents list, with the full-composition hint visible. These displayed
values agree with the physical report. This is a two-bulk-gas observation of
the completed validation frame; it does not prove four-gas/contaminant hover,
changing or paused production inspection, save/reload, UI scaling, other
asteroids or rendering performance. The full [hover criterion](mixture-presentation.md)
remains required.

Fresh strict MSVC controls pass all 13 native CTests, 89 Core groups, 41
presentation groups, coupled API/passive/mutation executables, adapter compile
and private packaging. The first compile failed with C4297 because a throwing
startup helper had C linkage. Moving that helper unchanged into the unnamed
C++ namespace fixes `/EHc` exception handling while retaining `/W4 /WX`, the
exported C wrappers and their error boundary. New public-ABI cases verify
invalid start arguments and unattached restore leave source/output bytes
unchanged. A fresh GCC 11 / Ubuntu 22.04 Docker build passes all 13 CTests;
both Linux libraries require at most glibc 2.34, below the 2.35 target. This
compatibility build supplies no new Linux Unity evidence.

The initial executable-path retry failed before process creation and reused
the accepted binaries after locating PowerShell. The successful game exits
normally with wrapper exit 0, drained/closed output pipes and all 13 original
profile files restored and independently hashed. The temporary mod is removed;
the default project DLL and installed SimDLL remain unchanged. No release or
public package is produced.

Private terminal SHA256 receipts:

- Windows game: `local/windows-native-domain-r3/TERMINAL_REVIEW.json`,
  `c37b50b0242c9fb8ad377c8749f3bf3e3acdcdaf8786571bf1416acc33190877`.
- Hover PNG: `local/windows-native-domain-r3/observations/owned-air-hover.png`,
  `8959519a7a2777cecd5a69e4a31924b2c8e586d1eb15f078dc6ee4ce455ac82e`.
- Linux compatibility: `local/windows-linkage-linux-r1/TERMINAL_REVIEW.json`,
  `07554835ba182c243801e8c281bf861d94a3d6b7f93b937f90865493cad89d37`.

These checks were performed by the primary agent; no independent agent review
is claimed. Earlier failed receipts remain retained.

### Additional Windows feature controls

The `local/windows-native-features-r1` cohort passes eight actual Windows Unity
runs from clean source `82c3c50`. The adapter is rebuilt against the Windows
game assemblies. The same accepted MSVC native binaries are reused after
checking all 193 native/build dependency files against their frozen inputs;
the numerical method is unchanged. All 81 synthetic save-layout/readback
groups pass on Windows .NET, and all 51 Linux Python acceptance-guard tests
pass. The Python controls do not launch a native solver or Linux Unity game.

| Scene | Actual Windows result |
| --- | --- |
| Compressed water and ordinary save | 30 frames / 630 substeps; 1,100 kg/cell at 220 MPa, zero reported mass/energy drift. The ordinary save verifies its complete 10,548,715-byte body and exact paired section. |
| Conduction and external heat | 120 frames / 2,520 substeps / 5,040 thermal stages; 0.194107745 J internal redistribution, positive entropy change 1.22099e-6 J/K, and residual -4.19e-9 J against the external 1,500 J input. |
| CO2 injection and consumption | 121 frames; actual 0.05 kg injection and 0.02 kg consumption callbacks, net 0.0300000011921 kg retained CO2, and U+K residual -6.34e-9 J. |
| Registered actors | 120 frames; 32 real consumer callbacks, 40 emitter samples, one blocked/unblocked callback each, atomic owned configuration, ordinary outside native batch, rate changes and removal. U+K residual -3.53e-9 J. |
| Gravity and gas transfers | 121 frames; boundary potential 0.147150006 J, U+K+G residual -2.13e-9 J, and maximum physical energy residual 2.79e-9 J. |
| Ordinary air save | Saves frame 31 / 6.200000092 s. A reserved heat command first rejects saving without a file, time advance, lost command or leaked pause. The drained save matches its complete 10,542,598-byte body and paired section; pause depth returns to zero. |
| Fresh-process air load and deferred autosave | Restores the exact initial image, colony GUID and all three duplicants. `Game.StartDelayedSave` autosaves at publication frame zero without changing physical time/state or pause depth 1. All 120 resumed frames match independent checkpoint continuation, reaching 30.200000450 s. |
| Fresh-process water load and deferred autosave | Restores the exact saved water image, GUID and three duplicants; the same actual deferred autosave retains frame-zero physical state and pause depth 1. All 120 resumed frames match exact continuation, reaching 30.000000447 s with zero reported drift. |

Every scene passes the shared `WindowsPlayer` checker, exact native-cell
compatibility, completed snapshots, game Grid, rendering callbacks, outside
native solid heat and scalar-save guards. These rendering checks and retained
Unity PNGs do not establish complete liquid appearance, four-gas hover or
rendering performance. Both reloads deliver two retained native startup batches
once; their frame-zero autosaves verify the complete bodies (air 10,542,776
bytes; water 10,548,893 bytes), exact paired section and original GUID.

The air source/final physical SHA256 values are
`41de3ef90deef435006e94fc86b38766cb9f981a26d6d3b27d16aa7e998a0f89` /
`a23a15bb94173aa2ee5121e8f31c465c8f654485225fc3c0444fb29d103a5ff2`.
Water source/final values are
`bd530e60d062f6ef7e6dee69e252711dd8e7939e5dde7a7a219f73202e3a2806` /
`bd9d033bcd919185a8bf61eb4a7ad93ab5f5fcc988f2980ce7be4ecb7cef1436`.
Exact continuation uses the same Windows binary; cross-compiler trajectory
identity is not required.

All eight game processes exit normally, their wrappers return 0 and both
output pipes drain/close. Each restores all 13 original profile files; the
temporary mod is removed, and the default project DLL and installed SimDLL
remain unchanged. The primary agent reviews saved evidence; no independent
agent review is claimed. The load-admission helper corrects an unused private
runner receipt filename before either load launch; no game retry or native
rebuild is performed for it.

Source-freeze SHA256:
`e1e2b853db1dd6eb9febf48d17ee4d412ef848ae5c40af0ac4a3bbed70167da0`.
Package-manifest SHA256:
`3f300853298be75423f101ee86925c64cacc9e07e246b810b349061eb1beb7ac`.
Aggregate terminal SHA256:
`1e7be92f88cdf48297aa545acd67b69db8d4fbc3127aa8c136b3d24688254e09`.
Per-case terminal receipts, full logs, saves, physical images and Unity PNGs
remain private beneath that cohort. Production actors/persistence, actual
menu/day-rollover triggers, arbitrary fluid materials and geometry, the failed
strong-flow gate, whole-colony performance and first-playable acceptance
remain open. Nothing is released.

## Limits and next work

Only the fixed sealed scene is enabled. Direct known native mutation packets
targeting its cells or border reject before dispatch; every other native writer
still needs an adapter and dedicated coverage. General production actors, duplicant
breathing, reserved machines, construction, cross-boundary fluid/solid heat and open
boundaries are not implemented for this domain. They must not enter it.

An ordinary scalar save cannot resume the complete physical owner. Unstaged
`Sim.Save` and the native begin-save detour reject. The ordinary colony save
entry now stages a complete pair for a drained, actor-free bounded owner.
The isolated control advances another physical frame after a declined
native save, proving the rejection did not suspend the simulator. Failure stops custom advancement while native fluid exclusions stay
installed until native shutdown; the disposable session must end.

Retain the adversarial native-state, thermal, grid, rendering and save-guard
checks as integration regressions. Native index translation and secondary-base
actor dispatch are now explicit parts of the exact-build contract. Do not
weaken unchanged-native-cell assertions when adding new writers or actors.

Continue with complete actor/process reservations and persistence, actual
menu/day-rollover trigger validation, an occupied development colony, then open/multiphase
geometry and complete reader/writer coverage. Native worker execution alone
is narrower evidence than a playable overhaul.
