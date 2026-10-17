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

`Describe(MixtureThermodynamicReading)` is a separate, read-only input for
supplied actual-EOS facts. The reading owns copied constituent records, their
canonical molar masses/moles and actual partial volumes, stored internal energy,
resolved kinetic energy, bulk momentum, actual temperature, gas/mechanical
pressures, phase volumes, and source/revision/profile identity. This overload
uses the catalog for species identity, phase, style and assessment definitions;
it uses the reading's molar masses even when they differ from the legacy
catalog. It never constructs a `FluidInventory`, subtracts bulk kinetic energy
from total energy, or derives temperature/liquid volume from the legacy model.

The supported reading contract is common-temperature ideal gases and elastic
liquids. Its finite scalar checks include gas mole/pressure/headspace and
per-species partial-volume consistency, mass closure, and resolved kinetic
energy at least bulk kinetic energy within a local arithmetic bound. Phase
volume closure accepts a declared relative tolerance up to `2e-10` and retains
signed residuals; this is EOS root precision, not a geometric conservation
gate. Stored U may include negative formation-reference energy. Gas partial
volumes are thermodynamic shares, while liquid partial volumes are actual
compressed volumes. Positive constituents are never removed by rounding.
Scalar products use exponent scaling so a tiny intermediate mole fraction does
not erase a representable partial volume or pressure. Rows, breathing assessment
and warning text share those partial pressures. A positive value below binary64
range is labelled explicitly rather than displayed as an absent constituent.

The resulting view retains the immutable `ThermodynamicReading` and each
constituent's `ThermodynamicPartialVolumeCubicMetres`. `GasPressurePa` and
`TotalPressurePa` remain gas-only; nullable `MechanicalPressurePa` is available
for this input, including compressed gasless liquid. The reading's actual EOS
profile identity is separate from the presentation catalog's existing
`MaterialModelVersion`/`MaterialModelFingerprint`. Legacy views have no supplied
thermodynamic reading or claimed liquid mechanical pressure.

Hover and selected details show supplied mechanical pressure as **Fluid pressure**
when liquid is present, separately from gas pressure and constituent partial
pressures. Compressed liquid can therefore show 220 MPa with no gas phase.
Legacy liquid inventories explicitly show **Fluid pressure unavailable**;
gas pressure is never substituted for a missing mechanical reading.

This reading API does not run an EOS equilibrium solve or advance a simulation.
`CoupledColonySimulation` now optionally prepares a presentation companion from
its completed native worlds before their all-world commit. Each cell's U,
momentum, temperature and mechanical pressure come from that physical frame;
partial volumes use this closed reference's explicit constant liquid densities
and ideal-gas headspace. Its model identity is retained. The rounded scalar
total-energy projection is never used to reconstruct physical readings.
`MixturePresentationPublication.PhysicalSource` retains the identical immutable
frame, and appearance recapture preserves it. The ONI completed-source adapter
uses that recapture path for colour-only and contrast changes.

Seventeen Windows/Linux coupled-boundary groups pass, including physical
reading agreement, paired rollback, liquid pressure, appearance recapture and a
six-cell scene with four bulk gases plus benign and hazardous transported
traces. This closed-reference binding does not establish production world or
container ownership, save authority, or first-playable acceptance. See the
[publication contract](../native/COUPLED_ABI.md#closed-reference-colony-frame-coordination).
The independent supplied-reading controls run with
`dotnet run --project managed/MixedAtmospheres.Presentation.Tests -c Release -- --thermodynamic-readings`;
that selection does not load or invoke the native reference solver.
All 14 focused groups pass on Windows and Linux .NET 10. The combined 41-group
Windows presentation suite also passes against the pinned native assessment
library; it does not advance physical simulation or establish Unity rendering.

Names join the two leading meaningful gases with an en dash (default threshold
1% by moles), identify them as a mixture, and retain an explicit traces/other-gases
suffix. A second short line reports `Breathable`, `Unsafe atmosphere`, or
`Vacuum / no atmosphere` from the numerical assessment. Liquid-only readings
instead say `No gas phase / not breathable`; the numerical breathing failure
and its warning remain unchanged. `BreathingStatus` supplies the same wording
to names and inspection. Single-gas names retain their species label;
liquid-only names identify the phase. Full details retain every trace.
Positive trace quantities use significant/scientific notation instead of zero;
a major constituent is shown as `<100%` when another gas remains. Warning flags
follow the native reference's inclusive thresholds and 16-ulp arithmetic
tolerance relative to the compared values, without a unit-sized absolute floor.
A positive contaminant or carbon-dioxide mass violates an explicit zero limit
even if its computed fraction underflows. The checks cover vacuum, oxygen fraction low/high, oxygen partial pressure low,
carbon-dioxide fraction/partial pressure high, and explicit contaminants.

Tile hover must show every non-trace gas as a separate name and mole-percentage
entry, without a top-N limit or requiring a click. The two-name mixture heading
does not replace this contents list. Hover uses each shared row's `IsTrace`
classification: by default fractions **below 1% of gas moles** are traces, while
1% and above are included. The model's `MeaningfulGasMoleFraction` controls that
boundary. Percentages retain their fraction of all gas moles; excluding traces
does not renormalize the visible entries. A short hint points to full composition
when traces are omitted. Specific breathing and contaminant warnings still show
hazardous traces, and the expanded table continues to show every constituent.

This is a first-playable success criterion. Native mouse-hover evidence on both
platforms must include at least four non-trace gases plus benign and hazardous
traces, check the threshold boundary, and match all displayed entries to one
completed snapshot. Earlier two-gas screenshots and the V51 automatic fixture
run do not establish this added criterion. The formatter and focused inspection
controls implement it. All 29 Windows inspection groups pass, including a
four-gas list, retained trace warnings/full details, and the exact configured
trace boundary. Windows V54 below completes the four-case native UI fixture.
Linux hover and presentation from an authoritative completed colony snapshot
remain required for the full first-playable gate.

The later `windows-liquid-inspection-r1` cohort passes all 31 inspection groups,
91 Core groups and 41 presentation groups. It adds supplied compressed-water
pressure, wet-mixture pressure/fraction separation, legacy unknown pressure and
true vacuum controls while retaining every non-trace hover check. Actual
Windows ONI hover and the expanded selected-cell panel show 1100 kg water,
220 MPa fluid pressure, no gas phase and no breathable atmosphere at the paused
120-frame milestone. The water row retains its mass/moles and correctly says
gas partial pressure is not applicable. All 120 load-continuation frames match
the retained physical image; this does not establish liquid fill rendering or
full-colony acceptance. Private terminal review SHA-256:
`7f41fea48323a37e072b483072bcca6dc815412531db2cbcde90d6c012bf4fa5`.

The later [Windows native gas-owner run](sealed-native-domain.md#actual-windows-gas-owner-run)
also visibly matches the retained physical reading at native frame 121:
nitrogen 81.6054%, oxygen 18.3946%, with benign helium omitted as a trace.
This actual game screenshot was taken without an agent tile click. Its two
bulk gases do not complete the four-gas/contaminant criterion above, and the
completed validation frame does not establish changing production inspection.

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

The physical Core companion explicitly marks its source as a read-only physical
projection. Display sampling remains available; its legacy inventory ledgers,
standalone inventory writers and Core checkpoint codec reject. Actual native
U/P/EOS readings and physical frame boundary accounts remain separate from the
rounded display total-energy projection.

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

### Completed colony transactions on Windows

Windows ONI V58 passes four native-hover and Unity-PNG cases using the physical
frame transaction path. An explicit synthetic inlet adds 0.125 kg oxygen and a
separate 4,096 J heat account, then the actual reference solver advances before
one completed physical/presentation publication. The surrounding 736-by-304,
nine-world colony with three duplicants remains native ONI; only six synthetic
closed-reference cells supply these readings.

| Inspected case | Completed frame | Heading | Pressure (kPa) | Temperature (K) |
| --- | ---: | --- | ---: | ---: |
| Initial mixture | 1 | Nitrogen–steam | 51.9995 | 264.8920 |
| Changed mixture | 2 | Nitrogen–steam | 69.6995 | 294.9469 |
| Physical inlet/heat edit | 3 | Nitrogen–oxygen | 69.7185 | 294.6263 |
| Colour-only recapture | 3 | Nitrogen–oxygen | 69.7185 | 294.6263 |

After the physical edit, hover visibly lists nitrogen 29.6869%, oxygen 29.0195%,
steam 24.6906% and carbon dioxide 16.603% by moles. Helium/chlorine stay omitted
from the bulk contents list and remain present in all six constituent records.
Chlorine's specific warning remains visible at `3.17141E-05%`. All four tooltip
lists were inspected against their completed observations; no selection or
details panel was needed. Colour-only recapture retains identical reported
physical facts, constituent rows, warnings and boundary receipt/account values.

The accepted native receipt records source revision 2, prepared revision 3, the
oxygen/heat accounts and an energy residual about `-3.30e-10 J` within its
`1.95e-7 J` arithmetic bound. This is explicit boundary input, not proof of a
powered pump/heater, another owner's reservation or complete colony ownership.
The game exits normally and original mods, preferences and world-generation
files restore byte-for-byte. The default game native DLL is unchanged.

The current coupled/Core/presentation suites pass 29/89/41 groups on both
Windows and WSL Linux. Exact declared-account records have a separate rational
review and corrupted-unit/rounded-bit rejection. No C++ control or earlier
failed numerical/performance gate was replayed. The game acceptance is Windows
only and supplies neither production containers, native callback routing,
physical save/reload, sustained performance nor first-playable acceptance.

Private game evidence is under `local/colony-runs/colony-boundary-windows-v58`;
its source/binary/pixel/receipt/restoration review is
`local/occupied-staggered-validation/colony-boundary-game-windows-r1/COMPLETED_REVIEW.json`,
SHA256 `ad784416b096c151067092ccbbe2ebce393de4f49004b6515a0b8c65d597f18d`.
See [frame transaction and projection contracts](../native/COUPLED_ABI.md#all-world-frame-transactions-and-boundary-accounts).

### Completed coupled reference scene on Windows

Windows ONI V57 passes three real hover/Unity-screenshot cases backed by the
closed native reference: the completed mixture, a later physical frame with
changed composition, and colour-only recapture of that same later frame. Each
hover shows all four non-trace gases without selecting the tile:

| Gas | First frame, mole % | Later frame, mole % |
| --- | ---: | ---: |
| Nitrogen | 49.8521 | 36.8073 |
| Steam | 25.1562 | 26.5718 |
| Oxygen | 19.2903 | 23.0891 |
| Carbon dioxide | 5.7014 | 13.5318 |

Helium and chlorine remain in the full six-constituent reading but are omitted
from the bulk contents list. The tooltip explicitly points to full composition.
Chlorine warnings remain visible at approximately 0.0927 ppm and 0.2458 ppm;
this test explicitly configures a 0.001-ppm limit. Visible percentages retain
their original gas-mole denominator, including the omitted traces.

The pressure rises from 51,999.5203 to 69,699.4663 Pa and temperature from
264.8920 to 294.9469 K as the reference advances from 0.03 to 0.06 seconds.
The low-oxygen-partial-pressure warning clears when its configured minimum is
met; CO2 and chlorine warnings remain. Colour-only recapture changes no physical
frame, stored U, p, T, constituent row or warning. The actual hover callback
checks its pinned reading against the native physical snapshot, and all three
2560-by-1440 Unity PNGs received matching root pixel-review receipts.

This is an original synthetic six-cell physical scene inside a fresh disposable
736-by-304, nine-world colony with three duplicants. Native ONI owns the
surrounding colony. It is not a ten-cycle owned-colony, phase-change, save/reload
or performance result. The screenshots verify hover text and physical-source
binding; they provide no new GPU readback or global colour-discrimination gate.
Linux's seventeen coupled controls and forty-one presentation regressions pass
under .NET, but this new physical scene has no Linux Unity acceptance.

V55 failed before the scene because reused generated-world data lacked current
cluster quality settings. V56 reached a fresh colony but was stopped after
startup shutdown callbacks incorrectly aborted the uninitialized test. Its raw
report remains pending and is not counted as a pass. V57 ignores those premature
callbacks and records ten of them; the physical inputs and fixed 180-second
case deadlines remain unchanged. Both earlier runs are retained.

Evidence is under `local/colony-runs/coupled-presentation-windows-v55` through
`v57`, with source/input freezes, assembly hashes and root reviews under
`local/occupied-staggered-validation/coupled-presentation-game-windows-r1`
and `r2`. The passing package manifest SHA256 is
`72557ad6cce1c6081515b6fd9dae79500591ba0f9a93adbddcaaf75b46aec440`;
its ONI assembly is
`fa6ee51a5588aba52a98b84f0f48f44bfd540eaf39d613cd8257a959c6c16144`.
The runner restored the original files byte-for-byte, independently checked
against its restoration manifest. The default game native DLL is unchanged.

The private mode requires packaging with `tools/package_probe.py
--coupled-reference <matching platform library>` and launching the disposable
runner with `-CoupledPresentation`. It loads that explicit library only for
this scene. It does not select a production solver or enable colony takeover.

### Fixed tile-hover fixture on Windows

Windows V54 passes the complete native hover fixture from clean source
`6fc628a`, using the atomic receipt producer and the same four cases and
180-second per-case deadlines. Default hover shows nitrogen 60.9997%, oxygen
25%, carbon dioxide 10% and steam 4%. Steam remains visible exactly at its
configured cutoff and disappears when the cutoff rises one binary64 step;
the other percentages remain unchanged. Hydrogen/chlorine traces are omitted
from contents, while the chlorine warning still reports 0.0002% against its
0.0001% limit. All four rows and the warning remain fully visible at 125% UI
scale. No tile selection or click is required.

Root reviews all four native tooltips and their 2560-by-1440 Unity PNGs. Each
held observation records three distinct hover frames. All four bound receipts
are accepted; revisions/publication generations 13/16/19/22 and session epochs
11/12/13/14 agree with their saved observations. The primary and helper reports
finish passed with empty errors. Final cleanup restores the UI scale, removes
the private banner and returns the native packet tooltip. No game input is
injected before normal Alt-F4.

Read-only review matches all 70 frozen source/tool files, seven package
payloads and four image/receipt pairs. Package manifest SHA256 is
`c78dd05b805ee8f43938b795b18966f54ccfafc61bbcdf0869cda5ef7c5cb3cf`.
Private evidence is under `local/colony-runs/tile-hover-contents-windows-v54`
and `local/tile-hover-contents-windows-v54-root`. The six scoped integration
probe checks and both presentation shutdown boundaries pass. ONI closes
normally, the runner exits 0, and all 283 personal files plus 13 settings/cache
files match their original membership and bytes. The game's OS exit code is
not independently captured.

This is a mostly paused, nine-world native Spaced Out! colony with three
duplicants and explicit immutable display inventories. Native ONI owns physics.
V54 closes the Windows fixture's tooltip/pixel-review protocol; it does not
establish Linux four-gas hover, authoritative fluid ownership, GPU readback,
performance, independent review or first-playable acceptance. V52/V53 below
retain their original failed outcomes.

Windows V52 exercises the new contents list through native `SelectTool` hover,
with no selected tile. At 101.325 kPa and 20 °C the display inventory contains
nitrogen 60.9997%, oxygen 25%, carbon dioxide 10%, steam 4%, hydrogen 1 ppm and
chlorine 2 ppm. Four actual Unity screenshots show all four non-trace rows at
the default cutoff, steam included exactly at its configured cutoff, steam
omitted when the cutoff is raised one binary64 step, and all four rows at 125%
UI scale. Displayed percentages retain the original gas-mole denominator.
Hydrogen and chlorine are omitted from contents; the explicit chlorine warning
still reports 0.0002% against its 0.0001% limit. Each capture comes after three
distinct native hover frames and retains its completed revision, publication
generation and session epoch.

**V52 fails overall.** The game accepts the first three screenshot-review
receipts, then reaches the fixed 180-second deadline while awaiting the final
125% UI review receipt. Root reviews that screenshot, but writes its receipt
after the primary failure; it cannot count as accepted completion. The failed
report and all original screenshots/receipts are preserved without changing
that invocation or its deadline. A subsequent source correction records aborts
in the helper's own report and hides its inactive test banner. V53 below
exercises that failure path; the ordinary hover formatter is unchanged.

The evidence is private under `local/colony-runs/tile-hover-contents-windows-v52`
and `local/tile-hover-contents-windows-v52-root`. The package manifest SHA256 is
`67495d220f82ebb771ce751207b1b6acd35d48c4d9c59eeb1280ec7b97d34192`.
Read-only review matches all 68 frozen source files, seven package payloads,
four held observations/screenshots and their receipts. After normal Alt-F4,
both presentation shutdown boundaries pass, ONI is closed, and all 283 personal
files plus 13 settings/cache files match their original membership and bytes.
The runner exits 1; the game's OS exit code is not independently captured.
These are explicit immutable display fixtures with native ONI physics, not an
authoritative mixed-fluid colony. There is no new Linux hover, GPU readback,
performance, independent-review or first-playable acceptance.

Windows V53 is a fresh invocation from clean source `9463fe2`, with the same
four cases and deadlines. Its default native tooltip and Unity capture show all
four non-trace gases and the chlorine warning. It then **fails** while reading
`default-all-gases.continue.json`: direct PowerShell `Set-Content` exposes the
final name while its write handle is open, and the native poller reports an
`IOException` sharing violation. No continuation is accepted. Both primary and
helper reports retain the same error, the inactive banner/fixture disappears,
and both shutdown boundaries pass after normal Alt-F4. All 283 personal files
and 13 settings/cache files restore exactly; the runner exits 1. Package manifest
SHA256 is `6dc62416e4346c40482794a73e770049c6c2d174b1a41f3a434f0c4f80d21cc7`.
Private evidence is under `local/colony-runs/tile-hover-contents-windows-v53`
and its sibling root review directory. This does not complete the hover gate.

The new [receipt producer](../tools/write_tile_hover_receipt.ps1) requires all
four explicit operator pixel-review assertions and a matching held report. It
writes a bounded temporary file, closes its writer, then renames it in the same
directory without replacing an existing receipt. It changes no game criterion
and adds no native read retry. Four Windows protocol tests pass: an eager reader
checks 64 complete receipts, with a live producer confirmed before each read, and missing
assertions, invalid/stale observations and attempted overwrite reject. These
use synthetic protocol data and establish no pixels or game acceptance.

On 2026-10-16 the Windows V51 fresh-world fixture run passes the three-frame
pacing change introduced by the Linux run. It uses source `2b91864` plus a
private-validation reporting correction: native `LoadFromWorldGen` exceptions
now reach the primary failure report and still propagate to the game. That
error branch has source/build review; V51 exercises its successful-load path.
The new all-non-trace hover formatter was added after V51 and is not covered by
this game run.

V51 produces 34 numbered actual Unity captures, 18 exact immediate CPU
texture-write/readback proof records (54 embedded colour/pressure/danger checks),
nine container/Core agreements, five conservation guards, two appearance
comparisons, passing container/nitrogen/owned-reservoir reports and passing
shutdown observations. It reaches a 736-by-304, nine-world Spaced Out! colony
with three duplicants. Root directly observes the game and representative trace,
composition-overlay and gas/liquid screenshots. This run performs no sampled GPU
readback and records zero custom mouse-hover draws; the separate V48 evidence
retains that earlier scope. V51 supplies no new hover or performance acceptance.
After normal Alt-F4, the runner exits 0, ONI is closed, and all 283 protected
personal files plus 13 settings/cache files match their pre-run membership and
bytes. The runner's exit is not an independently captured game OS exit code.

The preceding V50 cached-world invocation fails before colony creation with a
`SaveLoader.LoadFromWorldGen` null-reference error. Its original primary report
remains pending, raw error and restoration evidence are retained, and no visual
acceptance follows. V51 uses the separate fresh-generation path; it does not
establish that cached-world startup was repaired. Private evidence is under
`local/colony-runs/presentation-pacing-windows-v50`,
`local/colony-runs/presentation-fresh-world-windows-v51` and their sibling root
review directories. A metadata auditor's initial `Passed`/`Conserved` field-name
mistake is retained; its corrected data-only review changes no game criterion.

Current Windows and isolated WSL Linux checks pass Core 88, presentation 27,
owned snapshot/runtime 97 and inspection 27 scenarios. New coverage includes
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

Windows v34 adds six actual normal-world captures without changing the existing
container sequence: low oxygen fraction with adequate oxygen partial pressure,
isolated CO2 fraction and partial-pressure failures, a before/after composition
change at exactly equal total mass, and gas/liquid coexistence. All six numerical
assessments match their independently specified failure flags. The equal-mass
pair changes gas colour, physical pressure and danger while density opacity
stays fixed. Adding 500 kg of liquid to half the original gas in half the
headspace preserves gas colour, gas density and opacity. Water has a separate
liquid row with no invented gas mole percentage or partial pressure. These are
explicit conserved display fixtures; native liquid geometry remains unowned.

The private driver enables bounded renderer probes for six known cells. It
records the actual successful `SetBytes` arguments and immediate CPU buffer
readback for gas colour, pressure/opacity and danger. Eighteen captured records
(54 channel observations at the selected air and trace cell positions) pass exact cell coordinates, padded page addressing,
destination identity, pinned reading/model, completed frame, revision,
publication generation and session epoch checks. These do not sample every
declared probe cell or every write; vacuum has visual/model evidence rather
than a captured CPU triple in this run. Writes may precede capture by
at most two Unity frames. Property-texture reset, destruction, session retirement
or batch errors invalidate the probes; this driver explicitly rearms them and
waits for fresh writes. The ordinary inactive renderer keeps its original loops
without per-cell probe work. This proves the hook's CPU writes, not GPU readback
or a history with no transient diagnostic read failures.

The complete v34 report passes with 34 numbered Unity screenshots, six exact
Core fixture checkpoints, both relational comparisons and restored baseline
fixture bytes before container testing. Live label checks and reviewed images
show each hazard before density/composition rows and no stale warning in safe
cases. PNG34 visibly shows the 50% liquid-volume header; its water constituent
row is below the initial viewport. A scrolled actual UI capture remains required
before claiming that row is visibly readable. Twenty-two focused host checks
and the existing 97 runtime checks also pass against the frozen sources.

The private archive is `local/colony-runs/presentation-windows-v34`. Its 66-file
source manifest SHA256 is
`075a0b445c19dc650b5efa77671875b0ea7b178863c6cf7d180dd6954e6884a9`;
the actual Oni/Core assemblies are
`e65700251488948cdc32f75c4ab68741e6542f035633da024fdcdf66294643f1` /
`d01679044e744690b469d40dd678b4173075fab4e25cb0b8751786e08f246cda`.
The native DLL remains the previously tested
`77671261d559e60a2ea8a80b2a431d01064f416fa6540e27f4cddb7ac6dedf38`.
After native Quit Without Saving, the log contains normal application cleanup
and no exception/crash. All 13 original settings/worldgen files and all 283
personal save files, including membership, match their prelaunch hashes.
V34's mostly paused 504-cell sequence records frame p50/p95/p99 of
5.81/6.83/22.71 ms and a maximum stock-plus-mixture texture batch of 2.59 ms,
including private probes, screenshots and UI work, with no baseline subtraction.
Those observations do not establish sustained 1x or 100k-cell performance.

The separate Windows v35 run closes that water-row visibility gap. A private
copy of the v34 driver holds its existing gas/liquid case while actual native
wheel input and a scrollbar drag expose the full table. The renderer,
inspection and Core sources are unchanged. Both the actual Unity
`interactive-03-inspection-scroll.png` and computer-use screenshot show all
three constituent rows, the 50% liquid-volume header, and the water tooltip:
500 kg, 27754.218 mol, liquid constituent, gas partial pressure not applicable.
The input report and held checkpoint retain revision 7/publication 39 throughout
all three scroll captures. Explicit continuation then restores the exact
baseline and completes the full fixture sequence. Normal quit and independent
rehashing confirm all 13 originals and 283 personal files/membership restored.
Evidence is `local/colony-runs/presentation-scroll-windows-v35`; its source
manifest is `07c7deee3e76ef007d5aca29bf29574e61cc2d56a329ec617c81881d67b6716d`
and independent verification is
`9dcae30ecb1c95bf434c6d54edb8bf55caa27bc74bc98689d79609112b813d29`.
V34's original unscrolled image remains unchanged. V35 also independently
recomputes the held palette's RGB/pattern bytes for its 18 captured CPU records.
Its longer inspection hold is not a performance sample.

The v34 private audit initially used 16000 Pa in its independent oxygen
partial-pressure oracle. The actual configured/product minimum is 15000 Pa.
A preserved erratum rechecks all 2,940 checkpoint inventories and 18 sampled
records with the correct policy; sampled outcomes remain unchanged. Separate
pure 14900/15000/15500-Pa calibration distinguishes the two policies. This was
an audit-script correction, with no product or Unity acceptance change.

The later Windows-v45 regression exercises the merged adapter and newly
integrated layout helpers in a fresh nine-world, three-duplicant colony on
build 744825/Unity 6000.3.5f2. Independent review accepts all 34 actual
2560-by-1440 screenshots, the four labelled overlays, trace warnings, appearance
controls, zoom/scaling, six native container selection surfaces, nominal
nitrogen-phase items and the two isolated owned reservoirs. The six generic
container surfaces remain read-only visual fixtures; they do not establish
production pipe, suit, vent or machine mixture behavior. Native ONI still owns
world physics.

V45 records 18 CPU probes across two distinct cells and three texture channels;
these establish captured CPU bytes, not GPU readback. Details rendering occurs,
but `HoverDrawCount=0`, so this run does not establish custom mouse-hover
drawing. Screenshot 23 has no expanded Details panel. The checkpoint views
concern presentation-state restoration, not native save/reload of owned
mixtures. Its timings cover only 504 cells and up to six containers in a mostly
paused colony, including diagnostics: p50 5.529 ms, p95 6.410 ms and maximum
texture batch 9.14 ms. They establish no controlled or 100,000-cell performance
pass. Normal quit restores all 283 personal file identities/membership and all
13 settings/backups. Evidence is
`local/colony-runs/post-merge-presentation-windows-v45`; independent closed
review `76f6013c7cf2c35c2934efa8e6ac1dbf14295d415784625ea603e5d9fdc9da96`
is under `local/v45-independent-game-review`.

Windows V48 closes those actual-hover and sampled-GPU gaps using a private
observer around the unchanged merged renderer, inspection and Core sources.
All 34 original numbered screenshots and three additional inspection captures
pass independent review. Actual mouse hover shows the trace chlorine warning
at 0.0002% against its 0.0001% limit. In the gas/liquid case, native wheel input
and a scrollbar drag expose the complete water row: 500 kg, 27754.218 mol,
liquid constituent, gas partial pressure not applicable. Both holds preserve
their completed revision/publication/session identities until explicit
continuation. Appearance controls, all four labelled overlays, changing and
paused mixtures, checkpoint presentation, zoom and 125% UI scaling also pass.

Eighteen asynchronous GPU callbacks independently match the actual bound
colour, pressure and danger resources for air, trace-contaminated air and
vacuum across two snapshots. All requests complete with no pending callback.
This establishes the sampled texture bytes on Windows D3D11/Gamma; it does
not sample every pixel or certify every device, shader result or lighting
condition. The six generic container surfaces remain read-only selection
fixtures. The later two-reservoir protocol exercises owned transfer and
evacuation, native meters/logic and fences, but denies both attempted saves
before file side effects. It supplies no owned mixture save/reload acceptance.

V48 completes and exits through a normal application quit. All 283 personal
file identities/membership and 13 settings/backups restore exactly. Evidence is
`local/colony-runs/presentation-hover-gpu-windows-v48`; independent completed
review is `local/presentation-hover-gpu-v48-completed-review`, SHA256
`9fd6f894d014e01064ddcaab58ed230eea5eb46f529c162421c50e2869ec0a21`.
The preceding V47 attempt remains rejected: its second 240-second manual hold
expired before the full water-row observation and continuation. Its later
input cannot count retroactively. V48 is a separately declared fresh manual
attempt with the same binary, payloads and criteria. Neither mostly paused
504-cell run establishes sustained performance or first-playable acceptance.

A separate copied-source composition-cache experiment was rejected and never
applied to the product. Although repeated heat-only captures improved, its
three-pair Windows medians regressed cold capture from 67.12 to 79.87 ms and
real mass-changing capture from 45.74 to 78.50 ms. Mass-changing update plus
capture regressed from 261.04 to 321.54 ms. Differential correctness was retained,
but the broader workload did not justify the change. Exact evidence remains in
`local/mixture-presentation-current-v34/independent-review`; this is no new
performance gate pass.

A later private Combine allocation candidate was also rejected. Its first
protected pair increased infected Combine allocation from 114,400,000 to
120,800,048 B and positive-disease-trace allocation from 108,800,000 to
114,685,008 B. The required exact-allocation controls failed; no later phase or
live integration followed. The original product Combine remains in place.

The separate V41 owned-energy-copy candidate preserves the original public
constructor and Combine. Its exact correctness corpora pass 6,259 constructor,
12,521 Combine and 10,754 heat/transaction records. Its later untimed build
passes 211 complete-report validator controls and identical 27,770-record
presentation outputs. The first timed constructor pair nevertheless fails the
protected right-only Combine allocation equality: 82,699,848 B baseline versus
82,684,328 B candidate, a 15,520-B decrease. This control requires equality in
both directions. Both children and frozen closures pass, but the experiment
stops after that one pair; no stress, matrix or heat-benefit measurement runs.
The cause is unresolved. V41 remains unintegrated and establishes no performance
benefit. Earlier negative candidates, report-contract source failures and all
criteria are preserved. Private evidence and independent negative review are
under `local/applyheat-owned-copy-v41-measurement-r2` and its sibling review
directories. Live Core retains the original implementation.

A distinct same-baseline diagnostic then runs that frozen baseline assembly
twice, with unchanged inputs, warmup and runtime settings. Both complete reports
validate and all twelve checksums agree, but right-only Combine allocations differ:
82,471,912 B versus 82,692,360 B. The other 23 protected operation/array readings
match. Independent closure confirms this pair did not reproduce exact byte
equality. It establishes no cause or candidate benefit and does not change the
original V41 rejection. Evidence remains under
`local/applyheat-baseline-aa-source-only-v1` and
`local/applyheat-baseline-aa-independent-result-review-r2`.

The dedicated Linux harness now records menu and full presentation-fixture
success with 34 Unity captures; see [the Linux harness evidence](../tools/ONI_HEADLESS.md).
That is separate from WSL host correctness checks. The newly added four-gas
native-hover criterion, production container transactions, sustained performance,
long-run memory and coherent full-owned colony save/load remain open under
milestone D. `FirstPlayableColonyPassed` is false.
