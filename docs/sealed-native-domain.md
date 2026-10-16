# Sealed native fluid domain

This is the first bounded takeover experiment for the Linux colony work. The
custom native engine advances a six-cell room while the surrounding ONI colony
retains its original simulation. The corrected mixed-air control passes in
the actual Linux colony, as does the corrected compressed-water equilibrium
control. Both are actor-free development scenes;
whole-colony ownership, first-playable and release gates remain closed.

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
advances. The air case contains nitrogen, oxygen and helium at trace abundance.
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

## Gas point sources and game callbacks

Owned-cell `SimMessages.EmitMass` and single-cell `ConsumeMass` now reserve
physical gas transfers against the same completed native frame. Injection
arrives at rest; selective extraction carries the selected gas's internal
energy and its share of face momentum and kinetic energy. Injection retains
existing momentum and returns the kinetic mixing loss as heat. Every other
constituent, including trace helium, remains in the physical inventory.
These are declared point-source boundaries; pumps still need accountable
aperture flow, work and output reservations.

The additive transfer ABI 1 has 64-byte commands, 88-byte receipts and a
96-byte status. Heat and mass commands share an ordered immutable candidate.
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
frame, so a stopped publication cannot replay callbacks.

Current transfers support nitrogen, oxygen and helium in occupied gas cells.
Liquid/empty-cell transitions, disease-bearing injection, spatial consumption,
mixed-owner batches, registered actors and producer reservations require
additional adapters. Unsupported requests reject before dispatch.

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
```

Both runs use separate fresh profiles on the authenticated virtual desktop.
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
ASan/UBSan, including instrumented Funchook/diStorm dependency code. Current
Windows code cross-compiles; ABI 3 runtime execution and domain activation are
not validated on Windows, where domain start explicitly rejects.

## Limits and next work

Only the fixed sealed scene is enabled. Direct known native mutation packets
targeting its cells or border reject before dispatch; every other native writer
still needs an adapter and dedicated coverage. Production actors, duplicant
breathing, machines, construction, cross-boundary fluid/solid heat and open
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

After that gate passes, add actor transactions and native callbacks, real
closed-domain checkpoints, an occupied development colony, then open/multiphase
geometry and complete reader/writer coverage. Native worker execution alone
is narrower evidence than a playable overhaul.
