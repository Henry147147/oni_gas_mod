# Mixture presentation

The presentation model is a read-only view of explicit fluid inventories. It
does not enable colony takeover, create mixtures from ONI's dominant element,
advance fluid physics or establish production material support. Disposable
visual fixtures are labelled separately from owned colony state.

## Shared model and ownership

`MixturePresentationModel` accepts an immutable `FluidMaterialCatalog`, explicit
`MixtureSpeciesStyle` definitions, copied `AtmosphereSettings`, explicit oxygen
and carbon-dioxide material IDs, and explicit contaminant mole-fraction limits.
Unknown materials, missing styles, invalid thresholds and mismatched catalog
fingerprints reject. Changing colours cannot change breathing assessment.

`Describe(inventory, volume, revision, sourceId)` returns an immutable
`MixturePresentation`. Every positive constituent has a row with stable ID,
phase, mass and moles. Gas rows also have their fraction of **gas moles** and
partial pressure. Liquid rows have no gas fraction or partial pressure. The
common temperature comes from conserved energy; gas density and ideal-gas
pressure use the remaining headspace. Liquid mechanical pressure is not
invented by this projection.

Names join the two leading meaningful gases with an en dash (default threshold
1% by moles), identify them as a mixture, and retain an explicit traces/other-gases
suffix. A second short line reports `Breathable`, `Unsafe atmosphere`, or
`Vacuum / no atmosphere` from the numerical assessment. Single-gas names retain
their species label; liquid-only names identify the phase. Full details retain every trace.
Positive trace quantities use significant/scientific notation instead of zero;
a major constituent is shown as `<100%` when another gas remains. Warning flags
follow the native reference's inclusive thresholds and 16-ulp arithmetic
tolerance relative to the compared values, without a unit-sized absolute floor.
A positive contaminant or carbon-dioxide mass violates an explicit zero limit
even if its computed fraction underflows. The checks cover vacuum, oxygen fraction low/high, oxygen partial pressure low,
carbon-dioxide fraction/partial pressure high, and explicit contaminants.

`CaptureFrame(completedSnapshot, containers)` prepares one
`MixturePresentationFrame`. Its sparse `Cells`, `Containers` and shared `Vacuum`
are immutable; `TrySampleCell` rejects solids/outside cells and allocates nothing
when reading. Owned containers come directly from the same completed snapshot;
full-handle lookup validates colony, ordinal generation and unique allocation
token. Additional `MixtureContainerSample(id, inventory, volume, revision)` must
match the colony revision. Source labels and revisions are never rewritten on
older views. Settings/options getters return owned copies; primitive threshold
properties and `GasMaterialIds`/`Styles` serve rendering workers and legends.

Numeric assessment and colour analysis is cached per model in a
`ConditionalWeakTable` keyed by the immutable inventory reference. Each key
retains at most four volume variants, with no dictionary allocated for the common
single-volume case. Per-species pattern/linear-colour definitions are shared
across the model. Small contextual wrappers carry the source/revision; cell
labels, inspection rows, names and warning text materialize only when inspected,
using thread-safe publication of logically immutable values. Numeric warnings,
temperature, pressure and colour are validated before the frame is published.
Past inventories are collectable, including their cache cycles; replacing
settings/palette/options makes a new model. Entirely new inventories still
require numeric analysis. The current analysis resolves material properties,
energy-derived temperature, gas headspace/pressure and colour inputs in one
pass over the inventory. Differential tests compare its temperature, pressure
and liquid fraction bit-for-bit with the canonical cell model and preserve
offset, range, occupied-volume and unknown-material rejection behavior.

The newer packed-storage revision owns a sorted mass-entry array behind the
same read-only dictionary API. Hot internal loops consume a read-only span
without allocating boxed dictionary enumerators; already owned maps can be
shared safely by new immutable energy/revision values. Construction retains
input-order total-mass summation, ordinal public enumeration, every positive
trace and caller isolation. Four additional Core scenarios cover those
properties, inaccurate source count hints, duplicates and concurrent readers.

## Appearance and game adapters

Base colour is weighted by gas mole fraction **in linear RGB**, then encoded
as sRGB. A stable species-ID hash supplies a smooth world-anchored 4-by-4-cell
pattern. Each gas's contribution is blended continuously; crossing a dominant
species tie cannot switch the pattern abruptly. Liquids do not tint the gas
phase. Texture is enabled at contrast 0.04 by default.

The game adapter's appearance controls offer colour-only mode and contrasts
0.02/0.04/0.08/0.12 in the composition overlay. They persist to
`mods/config/MixedAtmospheres/appearance.json` under ONI's user root. Settings
replace the presentation model through `WithVisualOptions`; they do not alter
inventory or breathing policy. The Core supports contrasts up to 0.25; the game
settings reader accepts up to 0.15.

`MixturePresentationRuntime.Configure(model)` and `Publish(completedSnapshot)`
are internal ONI integration boundaries. Inventory providers register explicitly
with an ID and capture callback. The presentation thread captures callbacks
once per Unity frame, validates owned-source revisions and pins the resulting
immutable frame for texture/overlay workers. Provider errors remain
`unavailable`; they cannot silently become a native pure packet. Worker paths
must not call Unity, invoke providers, deserialize settings or rebuild analysis.
Capture pins the exact source, model and world before invoking external adapters,
so reentrant publication cannot mix revisions. Recursive capture rejects;
teardown aborts capture before any later retired callback runs. Liveness cleanup
checks the registration object identity before removing an ID that a callback
may have replaced. Worker frames carry a session epoch, preventing a fresh lease
from reactivating a frame from a retired colony.

`MixtureInspection.RegisterInventory(owner, id, provider, volume, label)` binds
independent display adapters to hover/selection surfaces; `RegisterInventoryOwner`
binds an already registered independent provider. Unregister both on teardown.
`RegisterContainerOwner(owner, handle, label)` instead binds the exact conserved
allocation in the shared completed world frame and creates no provider. It
suppresses native packet fallback even when that allocation is retired,
replaced, foreign or unpublished. Independent ID-only bindings cannot resolve
owned containers, and stale independent/old-handle cleanup cannot remove a new
exact-handle binding. Existing unregistered
native pipe/storage/bottle/equipped-suit/vent surfaces remain labelled native
single-material packets. Their packet mass/temperature can be shown; unknown
container pressure and mixed composition cannot be inferred from them.

Real session teardown is hooked before `Sim.Shutdown` and
`Game.DestroyInstances`. It retires providers, the model, cached/pinned frames,
texture scheduling and inspection bindings, including prefab keys that survive
scene changes. An old worker lease cannot restore a previous colony's view.
Property-texture reset/recreation only restores texture scheduling; it does
not retire a newly installed provider. A private, explicitly enabled
presentation-mode observer can record before/after teardown state. Its report
is paired with actual same-process return-to-menu/resume UI observations in
the scoped Windows v9 run described below.

The normal gas-colour renderer, hover, full details, composition overlay,
gas-pressure overlay, oxygen-partial-pressure overlay and breathability overlay
consume the same model. This shared view does not replace gameplay respiration,
machine transactions, liquid geometry, native pressure forces or save ownership.
Custom modes use the native oxygen mode's status-item visibility mapping to
avoid an unknown-view warning each frame. The native F1 legend explicitly
limits mixture assessment to owned cells; unowned cells retain native colours.

## Verification and limits

Current Windows and isolated WSL Linux checks pass Core 88, presentation 27,
owned snapshot/runtime 94 and inspection 27 scenarios. New coverage includes
same-frame cell/container agreement, checkpoint-preserved allocation identities,
retired/replaced/foreign handles, provider collision in both registration orders,
reentrant publication/teardown/liveness replacement, recursive capture and
cross-session worker frame rejection. These are correctness checks against local
licensed game declarations, without Linux Unity rendering.

The ordinary managed build runs
`managed/MixedAtmospheres.Presentation.Tests` even without a game directory.
Its 27 scenarios include 39 assessments against the native reference, tiny/zero-limit
trace regressions, 100
bit-exact canonical cell comparisons, trace
formatting, phase separation, linear colour, pattern/tie continuity, copied
settings, immutable frames/containers, concurrent cache/lazy inspection access
and weak-key collection. With `--game-dir`, `tools/build.py` builds the ONI adapter and runs
`tools/OwnedSnapshotTests`, `tools/MixtureInspectionTests` and
`tools/NativeDescriptionTests` against the licensed
local declarations. Earlier Linux runs passed 43 snapshot/router/render-policy
checks, 25 inspection checks and five native-description checks, including one-line headers, player units,
owned-container warning priority while preserving native description/order,
native link formatting, safe native-port bounds races, session retirement,
provider closure collection, stale worker leases and opt-in shutdown evidence.
These checks execute
against real game declarations and method bodies but do not draw the Unity UI.
The complete ONI adapter also compiles against the licensed Linux assemblies,
with two dependency-version warnings and no errors. A separate six-check replay
accepts V9's recorded Windows pipe description through the actual Linux native
text-formatting code. That replay establishes formatting parity, not Linux Unity
layout or a retroactive pass of V9's overall fixture report.

```sh
dotnet run --project managed/MixedAtmospheres.Presentation.Tests -c Release
dotnet run --project managed/MixedAtmospheres.Presentation.Tests -c Release -- --presentation-benchmark
python tools/build.py --game-dir /path/to/licensed/OxygenNotIncluded
```

`tools/build.py` sets `MA_TEST_NATIVE_LIBRARY` to the freshly built native
library. The test-only resolver pins that exact file and prints its SHA256 for
correctness runs; benchmark output remains pure JSON. This prevents stale
libraries beside test executables from overriding a new build. No game binaries belong
in source control or the package.

A Windows Release sample and isolated WSL Linux sample of the packed-storage
revision, each with ONI closed, use 100,000 21% O2/79% N2 cells. Allocations use
decimal MB. Initialization includes native-format fixture construction/import
and managed inventory injection; the all-changed update applies a distinct
positive heat input to every cell and creates new inventory references and
temperatures. Source snapshot construction is included in the measured work.

| Work | Windows time | Linux time | Windows allocation | Linux allocation |
| --- | ---: | ---: | ---: | ---: |
| Fixture/inventory initialization | 755.4 ms | 938.0 ms | 359.7 MB | 362.6 MB |
| Initial presentation capture | 76.5 ms | 165.0 ms | 46.7 MB | 46.7 MB |
| Initialization plus capture | 831.9 ms | 1,103.0 ms | 406.4 MB | 409.3 MB |
| Next revision, two changed cells, capture only | 17.5 ms | 15.4 ms | 7.84 MB | 7.84 MB |
| All 100,000 inventories changed, update only | 71.8 ms | 134.7 ms | 33.9 MB | 33.9 MB |
| All 100,000 inventories changed, capture only | 109.6 ms | 97.3 ms | 46.7 MB | 46.7 MB |
| All changed, update plus capture | 181.4 ms | 231.9 ms | 80.6 MB | 80.6 MB |
| 100,000 cached `Describe` calls | 25.0 ms | 24.7 ms | 8.00 MB | 8.00 MB |
| 1,000,000 cell reads | 17.5 ms | 103.4 ms | 0 bytes | 0 bytes |
| 1,000,000 patterned colour reads | 92.3 ms | 83.0 ms | 0 bytes | 0 bytes |

These are individual shared-host samples, not p50/p95 sustained game
measurements. Other background load was not profiled. Private outputs are
`local/mixture-presentation-packed-idle-windows.json` and
`local/linux-presentation/presentation-benchmark.json`, with timestamps,
source/test-assembly hashes and the raw Linux JSON preserved. Both use
presentation source SHA256
`21240684d147a5cd27aede0be59548da2b34caebd176895e1ddc02db5834cf9b`
and packed mass-storage source
`4e9f0f1cf50e541d6bc6f0153b9b0152df71ac47759316d516bbb90e84aad1b9`.
The pre-packed V10 sample, manifests, results and compiled artifacts are retained
under `local/linux-presentation/history-pre-packed-v10`; the older Windows
sample also remains in `local/mixture-presentation-benchmark.json`.

Cold/all-changed capture allocation falls from 58.69 MB to 46.69 MB, saving
12 MB per 100k-cell capture. Timing does not show an all-around speedup: the
Windows cold sample improves, while its all-changed capture and the Linux
cold/read samples are slower than their preceding single samples. The table
therefore includes update and initialization costs instead of moving work
outside capture's accounting. All-changed update plus capture still exceeds
0.1 seconds on both systems before any production solver or Unity rendering.
Mostly unchanged capture relies on inventory-reference reuse. Sustained
publication cadence, retained memory and absence of a growing backlog remain
unproved; the separate native solver also still fails its 25-ms gate.

The V10 screenshots and game acceptance below belong to checkpoint `20c18bc`
and their archived V10 binary hashes. V11 separately rechecks the newer packed
Windows assembly through the automated fixture sequence; its scope is recorded
below. Neither run establishes Linux Unity acceptance.

The frozen Windows presentation-v7 run separately records twelve actual Unity
screenshots: normal world, trace inspection, all four overlays, colour-only
mode, paused change/restore, zoom/UI scaling and return to the world. The report
covers 504 explicit test-owned cells in a native-physics colony and six actual
game prefabs bound to conserved fixture inventory providers. These are not
spawned production machines or proof of native packet/owned transport.
Computer-use observations and input diagnostics separately confirm actual
trace hover (1,353 draws), the chlorine warning, full-table scrolling through
all three constituent rows with trace mass/moles/partial pressure, all four
overlay icons, and the native F1 legend. Live `DetailLabel` assertions confirm
the warning precedes constituent rows. Native colour-only and strong-contrast
callbacks persisted disabled patterns and contrast 0.12 respectively. Evidence
is under `local/colony-runs/presentation-windows-v7`, including
`manual-ui-observations.json`. The older v6 run's zero hover draws and
harness-driven controls describe that historical package only. The later
failed v8/v9 container-helper runs remain recorded separately.

Windows v9 separately exercised native Save And Quit to the main menu and
resumed the same disposable save in the same process. Its archived
`presentation-shutdown.json` records five successful, idempotent teardown
observations. The first retires a live fixture model/world/frame and six
inventory providers; every after-state has no provider/model/frame, zero
inventories, cleared inspection bindings and no cleanup error. Native UI
observations confirm ordinary gas without fixture labels, native pipe details
without fixture rows, and the restored native F1 legend after resume. Evidence
is in `local/colony-runs/presentation-windows-v9`, including
`manual-lifecycle-observations.json` and `lifecycle-review.json`. The overall
fixture run remains **failed** at its spawned-pipe native-description comparison;
this narrower lifecycle pass does not establish container acceptance or a
full-owned colony save/load. V10 subsequently verifies the no-provider
custom-overlay legend clarification.

Windows v10 completes all eighteen automatic Unity captures, including one
selected-object panel for each actual spawned pipe, reservoir, bottle, suit,
vent and machine. Every live label assertion passes: correct selected target
and panel, complete fixture composition, warnings before constituent rows,
owned inspection before the native description, unchanged native description,
and no reported errors. All six container PNGs were visually reviewed across
the review in addition to their live assertions. These are explicitly
labelled, conserved read-only fixture inventories bound to native objects, not
native packet transport, an equipped suit or working production machines.
The full fixture sequence passes, followed by successful native Save And Quit
to the main menu and same-process resume of the same disposable save. Five
clean, idempotent shutdown observations retire the remaining world/model/frame;
the six inventory adapters were already removed by fixture cleanup. Six
interactive captures supplement the eighteen automatic captures. Post-resume
`interactive-05-overlay.png` records no world provider, zero inventory providers
and zero inspection bindings, and visibly shows `No mixture readings available`
in the composition legend. `interactive-06-overlay.png` records the same cleared
context and shows the restored native F1 oxygen legend. Fixture labels remain
absent. Exact file/binary hashes and PNG dimensions are recorded in
`local/colony-runs/presentation-windows-v10/presentation-review.json`, with
`manual-ui-observations.json` and the frozen mod reports. This remains native
colony persistence with presentation fixtures removed, not full-owned state
save/load or production fluid gameplay.

Windows v11 separately repeats the eighteen automatic captures and all six
spawned selected-container checks with the packed assembly. The package's Core
hash matches the current Windows build, and every package hash is verified.
All container target, composition, warning priority and native-description
assertions pass; the paused-hazard and machine screenshots were visually
reviewed. Both actual shutdown observations clear provider/model/frame and
bindings without errors. V11 has no interactive captures and does not repeat
the same-process menu/resume sequence: that evidence remains attached to V10's
pre-packed binaries. Frozen reports, PNG dimensions, source and package hashes
are in `local/colony-runs/presentation-windows-v11/presentation-review.json`.

The v7 paused 504-cell frame p50/p95 is 8.35/10.38 ms and p99 is 117.47 ms,
including screenshot/UI work and without baseline subtraction. The maximum
measured mixture texture batch is 3.35 ms. That scope does not establish 1x simulation,
100k-cell publication or overlay overhead. V10's same scoped sequence records
7,151 texture batches, frame p50/p95/p99 of 8.41/10.21/113.08 ms and a maximum
texture batch of 3.56 ms. It includes six spawned fixture panels, screenshots
and UI changes; the surrounding native colony is mostly paused and no baseline
is subtracted. V11's packed recheck records 7,275 batches, frame p50/p95/p99 of
8.24/9.76/110.74 ms and a maximum batch of 3.14 ms under the same limited scope.
Those are separate runs, without a comparative production-speed claim.
Windows v17 and v18 use `PublishConservedFixture` through the production snapshot
projection: six world cells are emptied into six owned containers, followed by
a constituent-selective 0.1 kg oxygen transfer, a custom format-3 checkpoint,
retirement/reuse of a bottle ID, exact-handle rebinding, and complete evacuation
before cleanup. Each run completes 25 Unity captures, nine selected-container
checks, five whole-inventory conservation checks and an explicit unavailable
panel for the stale allocation. No independent provider supplies these owned
container readings. V18 additionally compares nine runtime views directly with
Core revision, every constituent mass, total mass, temperature and pressure;
the replacement must be breathable with zero warnings and no chlorine row.
The actual cached panel must remove its old warning/composition rows.

Independent decoders of v17/v18's frozen checkpoints verify the six removed
world inventories, container parcels, selective oxygen/thermal-energy transfer,
unchanged geometry and external ledgers, saved full allocation identity, and
the recorded UI values, with source/package hash checks and actual screenshot
reviews in each run's `shared-snapshot-review.json`. V17 also exercises native Save And Quit, same-save
resume, retirement of the fixture world/model/frame and restored native F1
legend. V18 automatically quits and does not repeat that manual sequence.
Private reports and source/package/PNG hashes are under
`local/colony-runs/presentation-windows-v17` and `presentation-windows-v18`.
These checks cover conserved custom presentation fixtures; the native colony
still runs its own physics and its save does not establish custom-mixture or
owned-container persistence. V16's failed cached-world-reuse startup is retained
separately and contributes no acceptance evidence.

V18's mostly paused 504-cell/six-container sequence records 10,900 frames,
p50/p95/p99 of 8.06/9.91/110.23 ms and a maximum stock-plus-mixture texture batch
of 3.55 ms, including screenshots/UI transitions with no baseline subtraction.
This is a scoped observation, not sustained 1x gameplay or a 100k-cell gate pass.

Windows v23/v24 additionally exercise two actual native-empty owned gas
reservoirs through conserved actor batches and the exact prepared presentation.
The native meter, fullness, logic and selected panels agree with completed Core
allocations; hazardous chlorine remains visible as a warning after transfer.
V24's captured empty reservoir retains its owned vacuum reading before normal
unbinding. Mutation/save fences and forced-cleanup recovery are checked in the
same disposable colony. See [the reservoir bridge](owned-reservoir-bridge.md)
for exact scope, frozen evidence and V23's earlier asynchronous capture limit.
These checks do not establish pipe flow, equipped-suit gameplay or owned-state
save/reload.

V25-lease additionally binds that completed source with world-cell presentation
disabled. Container readings still come from the exact prepared frame, model
and revision, while its synthetic anchor geometry cannot replace native world
gas with vacuum. Three runtime regressions preserve this scope through source
refresh, visual options and worker-frame pinning, and reset it on retirement.
The actual Windows run checks this separation at each reservoir reading and
captures the destination trace warning and owned vacuum state. Its idle Core
checkpoint exports do not establish native owned-state save/reload.

Actual Linux Unity presentation
screenshots remain unverified; the WSL test environment has no authenticated
Linux Steam session or network route/DNS, and its EGL probe reports llvmpipe.
Linux Unity, production container transactions,
long-run memory and coherent full-owned colony save/load remain open acceptance
evidence under milestone D. `FirstPlayableColonyPassed` is false.
