> Gameplay update, 2026-10-27: the mod defaults to CO₂ breathing limits Off.
> The Gameplay switch changes both the duplicant reader and display, and is
> saved per colony. Oxygen remains required. Numeric thresholds live under
> Advanced. The synthetic reference fixtures and historical evidence below
> retain explicit CO₂ limits and nitrogen reference fluids; they add no
> nitrogen content to the game. See [settings](settings.md).

# Mixture presentation

The presentation model is a read-only view of explicit fluid inventories and
completed physical readings. World colours, tile hover, selected details,
overlays and container inspection all consume the same immutable view. It does
not enable colony takeover, create mixtures from ONI's dominant element,
advance fluid physics or establish material support. Disposable display
fixtures are labelled separately from owned colony state. The first-playable
gate that these requirements belong to is
[completion plan milestone D](completion-plan.md#d-first-playable-development-colony).

## Player-facing requirements

### Tile hover

- Hovering a tile lists **every non-trace gas** as a separate name and gas
  mole-percentage entry, directly in the tooltip. There is no top-N limit, and
  the player does not need to select the tile or open details. The two-name
  mixture heading does not replace this list.
- A gas row is a trace when its fraction of gas moles is below
  `MixtureVisualOptions.MeaningfulGasMoleFraction` (default 0.01). Rows at 1%
  and above are listed. Every surface uses the shared row's `IsTrace`.
- Percentages keep their fraction of all gas moles. Omitting traces does not
  renormalize the visible entries. A short hint points to full composition
  when traces are omitted.
- Breathing and contaminant warnings always show, including warnings for
  hazardous traces left out of the contents list. Expanded details show every
  constituent.
- Acceptance needs actual mouse hover on Windows and Linux. The tile must have
  at least four non-trace gases plus benign and hazardous traces, including a
  gas exactly at the threshold. Every displayed entry must match one completed
  snapshot. See the [acceptance procedure](#tile-hover-acceptance-procedure).

### Names, status and numbers

- Names join the two leading meaningful (non-trace) gases with an en dash and
  identify them as a mixture. A ` (+ traces)` or ` (+ other gases)` suffix
  marks the remaining gases. Single-gas names keep the species label;
  liquid-only names identify the phase. Full details keep every trace.
- A second short line reports `Breathable`, `Unsafe atmosphere` or
  `Vacuum / no atmosphere` from the numerical assessment. Liquid-only readings
  say `No gas phase / not breathable`; the numerical breathing failure and its
  warning stay unchanged. `BreathingStatus` supplies the same wording to names
  and inspection.
- Positive trace quantities use significant or scientific notation, never zero.
  A major constituent shows `<100%` when another gas remains. A positive value
  below binary64 range is labelled explicitly instead of being shown as absent.

### Breathing and contaminant warnings

- Checks cover vacuum, oxygen fraction low/high, oxygen partial pressure low,
  carbon-dioxide fraction and partial pressure high, and explicit per-species
  contaminant mole-fraction limits. `AtmosphereSettings` defaults are oxygen
  fraction 0.15–0.35, oxygen partial pressure at least 15,000 Pa, and CO2 at
  most 0.005 of gas moles and 500 Pa.
- Flags follow the native reference's inclusive thresholds and a 16-ulp
  arithmetic tolerance relative to the compared values, without a unit-sized
  absolute floor. A positive contaminant or CO2 mass violates an explicit zero
  limit even when its computed fraction underflows.
- Rows, assessment and warning text use the same partial pressures. Changing
  colours cannot change the assessment.

### Pressure and liquid rows

- When liquid is present, hover and selected details show the supplied
  mechanical pressure as **Fluid pressure**. It is shown separately from gas
  pressure and constituent partial pressures, so compressed liquid can show
  220 MPa with no gas phase.
- Legacy liquid inventories show **Fluid pressure unavailable**. Gas pressure
  is never substituted for a missing mechanical reading.
- Liquid rows have mass and moles. Gas fraction and partial pressure are shown
  as not applicable.

### Appearance, overlays and controls

- Base colour is the gas mole-fraction weighted average **in linear RGB**,
  encoded as sRGB. A stable species-ID hash gives a smooth, world-anchored
  4-by-4-cell pattern. Each gas's contribution blends continuously, so a tie
  between dominant species cannot switch the pattern abruptly. The pattern is
  on by default at contrast 0.04. `BaseColor` and `ColorAt` keep this gas-only
  contract.
- The composition overlay offers colour-only mode and contrasts
  0.02/0.04/0.08/0.12. Settings persist to
  `mods/config/MixedAtmospheres/appearance.json` under ONI's user root. They
  replace the model through `WithVisualOptions` and never change inventory or
  breathing policy. Core accepts contrast up to 0.25; the game settings reader
  accepts up to 0.15.
- The composition, gas-pressure, oxygen-partial-pressure and breathability
  overlays use the same model as normal colour, hover and details. Harmful
  traces stay in warnings and the breathability legend even when their colour
  contribution is imperceptible.
- The native F1 legend limits mixture assessment to owned cells; unowned cells
  keep native colours. Custom modes reuse the native oxygen mode's status-item
  visibility mapping. Without it, ONI warns about an unknown view every frame.
- Unregistered native pipe, storage, bottle, equipped-suit and vent surfaces
  stay labelled as native single-material packets. Their packet mass and
  temperature can be shown. Container pressure and mixed composition are never
  inferred from them.

The shared view does not replace gameplay respiration, machine transactions,
liquid geometry, native pressure forces or save ownership.

## Model contract

### Inventory descriptions

`MixturePresentationModel` takes an immutable `FluidMaterialCatalog`, explicit
`MixtureSpeciesStyle` definitions, copied `AtmosphereSettings`, explicit oxygen
and carbon-dioxide material IDs and explicit contaminant mole-fraction limits.
It rejects unknown materials, missing styles, invalid thresholds and mismatched
catalogue fingerprints.

`Describe(inventory, volume, revision, sourceId)` returns an immutable
`MixturePresentation`. Every positive constituent has a row with stable ID,
phase, mass and moles. Gas rows also have their fraction of **gas moles** and
partial pressure; liquid rows have neither. The common temperature comes from
conserved energy. Gas density and ideal-gas pressure use the remaining
headspace. This projection does not invent liquid mechanical pressure.

### Supplied thermodynamic readings

`Describe(MixtureThermodynamicReading)` is a separate read-only input for
supplied actual-EOS facts. The reading owns copied constituent records with
canonical molar masses, moles and actual partial volumes. It also owns stored
internal energy, resolved kinetic energy, bulk momentum, actual temperature,
gas and mechanical pressures, phase volumes and source/revision/profile
identity.

- The catalogue supplies species identity, phase, style and assessment
  definitions. The reading's molar masses are used even when they differ from
  the catalogue.
- The overload never constructs a `FluidInventory`, subtracts bulk kinetic
  energy from total energy, or derives temperature or liquid volume from the
  legacy model. It runs no EOS solve and advances no simulation.
- The supported contract is common-temperature ideal gases and elastic liquids.
  Finite scalar checks cover gas moles, pressure and headspace, per-species
  partial-volume consistency, mass closure, and resolved kinetic energy at
  least the bulk kinetic energy within a local arithmetic bound.
- Phase-volume closure accepts a declared relative tolerance up to `2e-10` and
  keeps signed residuals. This is EOS root precision, not a geometric
  conservation gate. Stored U may include negative formation-reference energy.
- Gas partial volumes are thermodynamic shares; liquid partial volumes are
  actual compressed volumes. Rounding never removes a positive constituent.
  Scalar products use exponent scaling, so a tiny intermediate mole fraction
  cannot erase a representable partial volume or pressure.

The view keeps the immutable `ThermodynamicReading` and each constituent's
`ThermodynamicPartialVolumeCubicMetres`. `GasPressurePa` and `TotalPressurePa`
stay gas-only. Nullable `MechanicalPressurePa` is available for this input,
including compressed gasless liquid. The reading's EOS profile identity is
separate from the catalogue's `MaterialModelVersion` and
`MaterialModelFingerprint`. Legacy views have no thermodynamic reading and
claim no liquid mechanical pressure.

### Phase-volume world appearance

For a canonical reading with gas and liquid, `WorldFluidColorAt` gives one
homogeneous cell appearance. It scales the gas linear RGB by actual
`Vgas / Vcell` and adds each liquid's configured linear RGB times its actual
compressed partial volume divided by cell volume. `WorldFluidOpacity`
composites density-based gas visibility with opaque liquid by the same phase
volumes, rounding only the final byte. `HasPhaseVolumeAppearance` identifies
this path.

- Liquid mass divided by nominal density is never substituted for the completed
  EOS volume.
- Gas mole percentages, partial pressure, breathing policy and the composition
  overlay keep their gas-only meaning. Dry readings and legacy inventories keep
  their existing policy. Gasless liquid goes through the native liquid renderer.
- This is a cell average with no inferred liquid surface or interface geometry.
- Both the normal and the diagnostic opacity upload loops must call
  `WorldFluidOpacity`. A loop that uploaded gas-only opacity was the one wiring
  error found in the game.

Development receipts capture styles, pattern settings, opacity range and the
exact physical readings. `tools/oni_world_appearance_evidence.py` independently
rebuilds each colour and opacity byte from those saved operands. The native
domain gate also needs shader-bound GPU readback, calibrated coordinates and
matching publication revisions; see
[GPU readback](native-domain-readers.md#gpu-texture-readback).

### Frames and caching

`CaptureFrame(completedSnapshot, containers)` prepares one
`MixturePresentationFrame`. Its sparse `Cells`, `Containers` and shared
`Vacuum` are immutable. `TrySampleCell` rejects solid and outside cells and
allocates nothing.

- Owned containers come from the same completed snapshot. Full-handle lookup
  validates colony, ordinal generation and unique allocation token; see
  [owned containers](container-inventories.md).
- An additional `MixtureContainerSample(id, inventory, volume, revision)` must
  match the colony revision and cannot override an owned container ID.
- Source labels and revisions on older views are never rewritten. Settings and
  options getters return owned copies. Primitive threshold properties and
  `GasMaterialIds`/`Styles` serve rendering workers and legends.

Numeric assessment and colour analysis are cached per model in a
`ConditionalWeakTable` keyed by the immutable inventory reference. Each key
keeps at most four volume variants and allocates no dictionary in the common
single-volume case. Per-species pattern and linear-colour definitions are
shared across the model.

- Small contextual wrappers carry source and revision. Cell labels, inspection
  rows, names and warning text are built only when inspected, using thread-safe
  publication of logically immutable values.
- Numeric warnings, temperature, pressure and colour are validated before the
  frame is published.
- Past inventories, including their cache cycles, can be collected. Replacing
  settings, palette or options makes a new model. New inventories still need
  numeric analysis.
- One pass over the inventory resolves material properties, energy-derived
  temperature, gas headspace and pressure, and colour inputs. Differential
  tests compare its temperature, pressure and liquid fraction bit-for-bit with
  the canonical cell model, including offset, range, occupied-volume and
  unknown-material rejection.

Inventory mass storage is a sorted array behind the same read-only dictionary
API. Hot loops read a span without boxed enumerators, and owned maps can be
shared by new immutable energy/revision values. Construction keeps input-order
total-mass summation, ordinal public enumeration, every positive trace and
caller isolation.

## Game integration

### Publication runtime

`MixturePresentationRuntime.Configure(model)` and `Publish(completedSnapshot)`
are internal ONI integration boundaries. Inventory providers register
explicitly with an ID and capture callback.

- The presentation thread runs callbacks once per Unity frame, validates
  owned-source revisions and pins the resulting immutable frame for texture and
  overlay workers.
- Provider errors stay `unavailable`; they never become a native pure packet.
- Worker paths must not call Unity, invoke providers, deserialize settings or
  rebuild analysis.
- Capture pins the exact source, model and world before calling external
  adapters, so reentrant publication cannot mix revisions. Recursive capture
  rejects. Teardown aborts capture before any later retired callback runs.
- Liveness cleanup checks the registration object's identity before removing
  an ID that a callback may have replaced. Worker frames carry a session epoch,
  so a fresh lease cannot reactivate a frame from a retired colony.

Actor bridges bind a completed prepared publication with
`BindCompletedSource` instead of independent providers; see
[frame commands](fluid-frame-commands.md).

### Physical source

`CoupledColonySimulation` can prepare a presentation companion from its
completed native worlds before their all-world commit. Each cell's U, momentum,
temperature and mechanical pressure come from that physical frame. Partial
volumes use the closed reference's explicit constant liquid densities and
ideal-gas headspace, and its model identity is kept. The rounded scalar
total-energy projection is never used to rebuild physical readings.

`MixturePresentationPublication.PhysicalSource` keeps the identical immutable
frame, and appearance recapture preserves it. The ONI completed-source adapter
uses recapture for colour-only and contrast changes. The companion marks its
source as a read-only physical projection. Display sampling works, but its
legacy inventory ledgers, standalone inventory writers and Core checkpoint
codec reject. See the
[publication contract](../native/COUPLED_ABI.md#closed-reference-colony-frame-coordination)
and [frame transactions](../native/COUPLED_ABI.md#all-world-frame-transactions-and-boundary-accounts).

The physical scene is private. Package it with
`tools/package_probe.py --coupled-reference <matching platform library>` and
start the disposable Windows runner with `-CoupledPresentation`. That library is
loaded only for this scene; it selects no production solver and enables no
colony takeover.

### Inspection bindings

- `MixtureInspection.RegisterInventory(owner, id, provider, volume, label)`
  binds an independent display adapter to hover and selection.
  `RegisterInventoryOwner` binds an already registered independent provider.
  Unregister both on teardown.
- `RegisterContainerOwner(owner, handle, label)` binds the exact conserved
  allocation in the shared completed frame and creates no provider. It
  suppresses native packet fallback even when that allocation is retired,
  replaced, foreign or unpublished; those cases show unavailable.
- Independent ID-only bindings cannot resolve owned containers. Stale
  independent or old-handle cleanup cannot remove a newer exact-handle binding.

### Session teardown

Teardown is hooked before `Sim.Shutdown` and `Game.DestroyInstances`. It
retires providers, the model, cached and pinned frames, texture scheduling and
inspection bindings, including prefab keys that survive scene changes. An old
worker lease cannot restore a previous colony's view. Property-texture
reset/recreation only restores texture scheduling and does not retire a newly
installed provider. A private, explicitly enabled observer can record state
before and after teardown.

## Verification and limits

### Host tests

```sh
dotnet run --project managed/MixedAtmospheres.Presentation.Tests -c Release
dotnet run --project managed/MixedAtmospheres.Presentation.Tests -c Release -- --thermodynamic-readings
dotnet run --project managed/MixedAtmospheres.Presentation.Tests -c Release -- --presentation-benchmark
python tools/build.py --game-dir /path/to/licensed/OxygenNotIncluded
```

- The presentation suite has 45 groups and passes on the Linux .NET 10 host
  against the pinned native assessment library. They cover assessments against
  the native reference, tiny and zero-limit traces, bit-exact cell comparisons,
  formatting, phase separation, linear colour, pattern continuity, immutable
  frames and containers, concurrent cache and lazy-inspection access, weak-key
  collection, prepared publications and supplied readings. Windows last passed
  the 41-group revision.
- `--thermodynamic-readings` runs only the 18 supplied-reading groups and does
  not load the native reference solver.
- With `--game-dir`, `tools/build.py` builds the ONI adapter and runs
  `tools/OwnedSnapshotTests`, `tools/MixtureInspectionTests`,
  `tools/NativeDescriptionTests` and the other licensed-declaration suites. The
  37 inspection and 5 native-description groups pass against both the Windows
  and Linux declarations. They run against real game declarations and method
  bodies but do not draw the Unity UI; see
  [inspection checks](../tools/MixtureInspectionTests/README.md).
- The coupled-boundary groups cover physical reading agreement, paired
  rollback, liquid pressure, appearance recapture and a six-cell scene with
  four bulk gases plus benign and hazardous transported traces. They pass on
  Windows and Linux; see [the coupled ABI](../native/COUPLED_ABI.md).
- `tools/build.py` sets `MA_TEST_NATIVE_LIBRARY` to the freshly built native
  library. The test-only resolver pins that file and prints its SHA256 for
  correctness runs, so stale libraries beside test executables cannot override
  it. Benchmark output stays pure JSON. No game binaries belong in source
  control or the package.

### Tile-hover acceptance procedure

Both platforms run the same four-case `tile-hover` display fixture in a
disposable colony. It uses the real game window, pointer, `SelectTool` and
`HoverTextDrawer`. Native ONI keeps physics;
immutable fixture inventories exercise presentation only. The tile holds, at
101.325 kPa and 20 °C, nitrogen 60.9997%, oxygen 25%, carbon dioxide 10%,
steam 4%, benign hydrogen 1 ppm and hazardous chlorine 2 ppm, with an explicit
0.0001% chlorine limit.

| Case | Required tooltip |
| --- | --- |
| Default threshold | Four bulk rows; hydrogen and chlorine omitted from contents; chlorine warning shows 0.0002% |
| Threshold set to steam's fraction (0.04) | Steam still listed |
| Threshold one binary64 step higher (0.04000000000000001) | Steam omitted as a trace; other percentages unchanged |
| 125% UI scale | All four rows and the warning fully visible at the requested font size |

1. Start the scene. On Linux:

   ```sh
   python3 tools/oni_headless.py run --package /path/to/frozen/probe \
     --mode tile-hover --private-display --timeout 1800 \
     --output /path/to/new-evidence-directory
   ```

   The scene points the camera at the arena and writes the tile's screen
   coordinates to `tile-hover-contents.json` in the staged mod's
   `game-contract-local/colony-validation` directory. On Windows, run
   `tools/run_colony_validation.ps1 -TileHover`; each case has a fixed
   180-second deadline.
2. Move the actual pointer over the tile without selecting it. Three distinct
   hover frames must agree with one immutable publication before the game saves
   its PNG and holds the case.
3. Review the screenshot, copy the held JSON to a private file, then publish
   the case's receipt. On Linux:

   ```sh
   python3 tools/oni_tile_hover.py \
     --reviewed-report /path/to/held-case-0.json \
     --output-directory /path/to/staged/colony-validation \
     --expected-case 0 --all-non-trace-rows-visible --trace-rows-omitted \
     --hazard-warning-visible --no-selection
   ```

   On Windows, use `tools/write_tile_hover_receipt.ps1` with the same
   assertions (`-ReviewedReport`, `-OutputDirectory`, `-ExpectedCase`,
   `-AllNonTraceRowsVisible -TraceRowsOmitted -HazardWarningVisible
   -NoSelection`).
4. Repeat for all four cases. The game checks every displayed row against its
   completed mixture. The runner requires all four final observations, their
   receipts, the retained reviewed reports and unchanged screenshots.

The four assertion switches attest the operator's image review and must never
be inferred from the JSON. The writer records the reviewed report and PNG
hashes. A missing image, different dimensions, a selected tile or a stale case
rejects before continuation. The result keeps the display-fixture scope and
never sets first-playable or production-authority flags.

Receipts are bounded to 4096 bytes of strict UTF-8. The writer writes a
temporary file, closes it and renames it in the same directory without
replacing an existing receipt. The game reads with
`FileShare.Read | FileShare.Delete` and no write share or retry: on Windows the
rename's DELETE handle can stay open briefly after the final name appears
([rename semantics](https://devblogs.microsoft.com/oldnewthing/20211022-00/?p=105822)).
Writing the final name directly, for example with PowerShell `Set-Content`,
causes a sharing violation. The protocol tests run with
`python -m unittest discover -s tools/tests -p test_tile_hover_receipt.py -v`
on Windows with PowerShell 7; `MA_PWSH` selects its executable.

At 1280 by 720 and 125% UI scale the original tooltip ran below the screen and
hid the chlorine warning. The adapter now measures the game's completed shadow
bars and selection borders in the hover canvas and moves text, icons and both
borders together inside it. Font size, rows and warnings are unchanged. The
next native drawing restores the previous pool positions, and session teardown
retires the adjustment.

### Display fixtures in the game

These runs use ONI 744825 with native ONI physics and explicit, conserved
display inventories. They verify presentation, not fluid ownership.

- **Tile hover.** All four cases pass on Windows (V54, repeated with the
  viewport repair in the
  [2026-10-17 retest](windows-validation.md#windows-game-checks-2026-10-17))
  and on Linux at 1280 by 720 with real XTest pointer movement. After fixture
  cleanup the normal native tooltip returns.
- **Presentation fixture, Windows.** 34 numbered Unity screenshots plus
  inspection captures over 504 test-owned cells cover the normal world, all
  four labelled overlays, colour-only and contrast, paused composition changes,
  fixture checkpoint restore, zoom, 125% UI scaling and asteroid switching.
  Cases include ordinary, oxygen-rich and thin air, CO2, chlorine, vacuum,
  oxygen fraction versus partial pressure and CO2 fraction versus partial
  pressure; every assessment matches its independently specified flags. An
  equal-mass composition change alters colour, pressure and danger but not
  density opacity. Adding 500 kg of liquid to half the gas in half the
  headspace keeps gas colour, density and opacity. Hazards appear before
  composition rows, and safe cases show no stale warning. Native scrolling
  exposes the full water row (500 kg, 27,754.218 mol, gas partial pressure not
  applicable).
- **GPU, Windows.** Eighteen asynchronous readbacks match the bound colour,
  pressure and danger textures for air, trace-contaminated air and vacuum on
  D3D11/Gamma. These are sampled bytes, not every pixel, device or lighting
  condition.
- **Containers, Windows.** Real pipe, reservoir, bottle, suit, vent and machine
  prefabs show their fixture composition, warnings first, then the unchanged
  native description. Owned containers filled from world cells pass a
  selective transfer, checkpoint, bottle-ID reuse, exact-handle rebinding,
  stale-allocation unavailability and full evacuation, and runtime views match
  Core. Owned reservoirs are covered by the
  [reservoir bridge](owned-reservoir-bridge.md).
- **Lifecycle, Windows.** Save And Quit to the main menu and same-process
  resume clear the provider, model, frame and bindings idempotently. The
  composition legend then shows `No mixture readings available` and the native
  F1 oxygen legend returns. This is not owned-state save/load.
- **Linux.** The harness passes the menu and full presentation fixture with 34
  Unity captures; see [the Linux harness](../tools/ONI_HEADLESS.md). Linux GPU
  evidence comes from the native domain below.

### Completed coupled reference scene on Windows

Windows (V57) passes three hover and Unity-screenshot cases backed by the
closed native reference, in six synthetic cells inside a fresh nine-world
colony whose surroundings stay native. Each hover lists all four non-trace
gases without selection; helium and chlorine stay in the full reading but not
the bulk list.

| Gas | First frame, mole % | Later frame, mole % |
| --- | ---: | ---: |
| Nitrogen | 49.8521 | 36.8073 |
| Steam | 25.1562 | 26.5718 |
| Oxygen | 19.2903 | 23.0891 |
| Carbon dioxide | 5.7014 | 13.5318 |

Pressure rises from 51,999.5203 to 69,699.4663 Pa and temperature from
264.8920 to 294.9469 K. The low oxygen partial-pressure warning clears once
its minimum is met, while CO2 and chlorine warnings (about 0.0927 and
0.2458 ppm against a 0.001-ppm limit) remain. Colour-only recapture changes no
physical value, row or warning.

### Completed colony transactions on Windows

Windows (V58) passes four hover and Unity-PNG cases through the physical frame
transaction path in the same scene. An explicit synthetic inlet adds 0.125 kg
oxygen and a separate 4,096 J heat account before one completed publication.

| Inspected case | Completed frame | Heading | Pressure (kPa) | Temperature (K) |
| --- | ---: | --- | ---: | ---: |
| Initial mixture | 1 | Nitrogen–steam | 51.9995 | 264.8920 |
| Changed mixture | 2 | Nitrogen–steam | 69.6995 | 294.9469 |
| Physical inlet/heat edit | 3 | Nitrogen–oxygen | 69.7185 | 294.6263 |
| Colour-only recapture | 3 | Nitrogen–oxygen | 69.7185 | 294.6263 |

After the edit, hover lists nitrogen 29.6869%, oxygen 29.0195%, steam
24.6906% and carbon dioxide 16.603%; chlorine's warning shows `3.17141E-05%`.
The energy residual is about `-3.30e-10 J` within its `1.95e-7 J` bound. This is
explicit boundary input, not a powered device or complete colony ownership.

### Physical frames bound to rendering

- **Windows native domain.** Water hover and details show 1100 kg, 220 MPa
  fluid pressure, no gas phase and no breathable atmosphere. A gas scene
  matches all six GPU colours and opacity/danger bytes, and its hover
  (nitrogen 78.295%, oxygen 21.705%) binds to the same physical object and
  publication as rendering. See [native fluid rendering](native-fluid-rendering.md).
- **Linux native domain.** Gas and water GPU bytes, hover and details bind to
  the same completed physical object; see
  [GPU readback](native-domain-readers.md#gpu-texture-readback).
- **Phase-volume appearance.** A heated six-cell gas/water room passes a fresh
  source and two reload sessions in WindowsPlayer under Proton/OpenGL and in
  native Linux, matching all six shader-bound colour and opacity texels and the
  independent rebuild, with 120 exact frames per reload. This was not an actual
  Windows OS run.

### Performance

Host benchmark (`--presentation-benchmark`): 100,000 cells of 21% O2/79% N2,
single shared-host samples with ONI closed, allocations in decimal MB. The
all-changed update gives every cell a distinct heat input, creating new
inventory references.

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

All-changed update plus capture exceeds 0.1 s on both systems before any
production solver or rendering. Mostly unchanged capture depends on reusing
inventory references. In the game, the latest Windows fixture records 19,705
frames at p50 6.237 ms, p95 7.367 ms and p99 32.165 ms, with a maximum texture
batch of 16.2322 ms, for at most 504 cells in a mostly paused colony including
screenshots and probes. It does **not** pass the active simulation target.

Rejected optimizations, with the original code kept: a composition cache
(cold capture 67.12 to 79.87 ms), a `Combine` allocation candidate (allocation
rose 114,400,000 to 120,800,048 B), and an owned-energy copy that missed the
exact-allocation equality control by 15,520 B. Two runs of the unchanged
baseline also differed (82,471,912 versus 82,692,360 B), so that control does
not reproduce; the cause is unresolved.

### Open items

`FirstPlayableColonyPassed` is false. Still required:

- Hover, details, overlays and container panels driven by the authoritative
  production colony state on Windows and Linux, not display fixtures or
  six-cell development domains.
- Linux Unity acceptance of four-gas hover against a completed physical frame.
  The V57/V58 scene is Windows-only.
- Production pipes, suits, vents and machines bound to owned inventories. The
  six container surfaces are read-only fixtures, and the reservoir bridge is
  not connected to the native owner.
- The 100,000-cell presentation gate on both systems, as defined in the
  completion plan: sustained publication without backlog at 1x, retained
  memory, game frame times with overlays on and off, p50/p95 and peak memory.
- Liquid surface and interface geometry, other liquid materials and free
  surfaces. The current appearance is a cell average.
- Owned-state colony save/load that includes presentation.
- Polish noted in the
  [2026-10-17 retest](windows-validation.md#windows-game-checks-2026-10-17):
  the development boundary outline can cross a hover card at some zooms, and the
  status panel samples the hovered or first supplied cell rather than the
  selected object.
