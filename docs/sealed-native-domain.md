# Sealed native fluid domain

This is the first bounded takeover experiment for the colony work. The
custom native engine advances a six-cell room while the surrounding ONI colony
retains its original simulation. The corrected mixed-air control passes in
the actual Linux colony, as does the corrected compressed-water equilibrium
control. The basic controls are actor-free development scenes; a separate
registered-actor control exercises an actual game consumer and emitter API.
Whole-colony ownership, first-playable and release gates remain closed.

## Execution and ownership

The mod attaches the exact-build native hooks before initialization. After a
disposable colony starts, the validation driver builds an unoccupied room with
an impermeable, adiabatic Unobtanium border. It verifies the border, absence of
objects, disease and radiation before enabling the domain.

The actual positive `ProcessNextFrame` return supplies the timestep and a
monotonic ticket. `UpdateData` advances the custom `MacClosedWorld` on the native
simulation worker, once for the global timestep. A mutex spans custom work and
the original update so a managed reader cannot consume a partially completed
native frame. Synchronization-only passes never advance custom time. There is
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
Actual Windows OS and Windows Unity colony validation remain open.

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

`-NativeDomain` also accepts `water`, `heat`, `transfer`, `actor` and `carbon`.
The actor scene needs at least 80 frames; use 120 for its full control. Python
must be available for the shared portable acceptance checker. It requires
`WindowsPlayer` evidence and the same physical, native-source, rendering,
callback, save and scope checks used on Linux. An isolated Wine report cannot
pass this Unity gate. The Windows colony wrapper is syntax-checked here; this
machine has not executed that Windows Unity run.

## Limits and next work

Only the fixed sealed scene is enabled. Direct known native mutation packets
targeting its cells or border reject before dispatch; every other native writer
still needs an adapter and dedicated coverage. General production actors, duplicant
breathing, reserved machines, construction, cross-boundary fluid/solid heat and open
boundaries are not implemented for this domain. They must not enter it.

Saving is disabled while the development domain owns fluids. Its physical
state is not an integrated ONI checkpoint, and an ordinary scalar save cannot
resume it. Both the normal managed save path and the native begin-save detour
reject. The isolated control advances another physical frame after a declined
native save, proving the rejection did not suspend the simulator. Failure stops custom advancement while native fluid exclusions stay
installed until native shutdown; the disposable session must end.

Retain the adversarial native-state, thermal, grid, rendering and save-guard
checks as integration regressions. Native index translation and secondary-base
actor dispatch are now explicit parts of the exact-build contract. Do not
weaken unchanged-native-cell assertions when adding new writers or actors.

Continue with complete actor/process reservations, real closed-domain
checkpoints, an occupied development colony, then open/multiphase
geometry and complete reader/writer coverage. Native worker execution alone
is narrower evidence than a playable overhaul.
