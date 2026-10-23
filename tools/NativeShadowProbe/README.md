# Isolated native interface harness

`NativeShadowProbe` loads the licensed ONI native simulator (`SimDLL.dll` on
Windows x64, `libSimDLL.so` on Linux x64) in its own console process. It creates
a disposable 64×64 native world and checks the native interface the mod relies
on. It never attaches to ONI, Unity or Steam and never touches colony saves.
Every report keeps `authoritativeColonySimulationReady: false`.

With the observer backend, the sealed-domain modes run the mod's production
colony fluid solver, `MacClosedWorld`. It advances a sealed room on ONI's real
native clock while SimDLL keeps solids and heat. ONI's native fluid simulation
is what the mod replaces, so the ordinary modes use it only as the original
behaviour to compare against.

## Requirements

- .NET 10 on Windows or Linux x64.
- The licensed build-744825 native library. Its SHA256 is pinned: Windows
  `031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`, Linux
  `85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d`.
- A private `game-contract-local` directory (`contract.json`, `elements.bin`,
  `diseases.bin`) exported by the main-menu probe; see
  [the environment notes](../ONI_ENVIRONMENT.md#main-menu-probe-and-contract-export).
  It is game-derived: keep it, game binaries, logs and reports outside the
  repository and packages.
- For the domain modes and `zero-time-frame`: the game root and the observer
  from `python3 tools/build.py --oni-native-observer`. That is
  `libmixed_atmospheres_oni.so` on Linux or `mixed_atmospheres_oni.dll` on
  Windows, in the build directory.

## Build and run

```sh
dotnet build tools/NativeShadowProbe -c Release
dotnet tools/NativeShadowProbe/bin/Release/net10.0/NativeShadowProbe.dll \
  <SimDLL> <game-contract-local> <mode> <report.json> [<observer library> <game root>]

python3 tools/NativeShadowProbe/run.py --native /path/to/OxygenNotIncluded_Data/Plugins/libSimDLL.so \
  --contract /private/game-contract-local --output /private/new-evidence-dir \
  [--mode all|domains|<mode>] [--observer build/libmixed_atmospheres_oni.so --game /path/to/OxygenNotIncluded]
```

On Linux, `run.py` runs each mode in its own process, with a 45-second timeout
and no core dumps, because a bad contract can crash the native library. The
output directory must be new or empty. `all` (the default) runs the nine
ordinary modes. `domains` runs the fifteen domain modes and needs `--observer`
and `--game`. Each mode writes `<mode>.json` and `<mode>.log`, plus a
`suite.json` summary. For the Windows binaries under Wine, add `--wine`,
`--wine-prefix` (a prefix mapping the host at `Z:`, with Windows x64 .NET 10)
and `--windows-dotnet`. Wine runs never count as Windows OS or Unity tests.

The explicit `floor-f4` mode measures only the licensed native simulation,
without loading an observer or mod owner. Supply `--floor-request` with the
private `MAFLRQ01` inputs already proved by the Unity admission capture. It
checks all 28 starting material/mass/temperature tuples, embeds the fixed
7×4 arena in the disposable 64×64 world, and takes 100,001 samples over
20,000 nominal seconds, with a 240-second process bound. Its report contains
aggregate numerical metrics only. Different coordinates/partitions and Unity
frame alignment remain unproved; this isolated result is not floor acceptance.
The ordinary 45-second bounds and default mode lists are unchanged.

On Windows, `run.ps1` runs one mode (`-ProbeDirectory`, `-NativeDll`,
`-ContractDirectory`, `-OutputDirectory`, `-Mode`, optional `-Observer` and
`-GameDirectory`). It copies the assemblies to a local temporary folder first,
because Windows may refuse to load them from SMB.

A mode passes when the child exits 0 and its report says `passed: true`. The
exception is `sanitize`, an expected failure. It counts only if it fails exactly
as recorded: exit code 1, no commit before a frame, zero frames, and two fluid
cells holding 102 kg. Crashes, timeouts and missing reports fail the suite.

## Ordinary modes

| Mode | Checks |
| --- | --- |
| `baseline` | Oxygen, water, vacuum and solids run twelve frames, and solid heat conducts. Exports the initial native save. |
| `multi-region` | Two regions for twelve timesteps. Heat moves in both, and time is not multiplied per region. |
| `fluid-actors` | A native oxygen consumer and emitter: one registration step, eight active steps (rates changed halfway) and four after removal. Checks callbacks, consumed-mass records, the emitter interval reset and mass ledgers. |
| `rebuild-shadow` | Reinitializes with solids and vacuum. Nonfluid cells must match, with no native fluid for twelve frames. |
| `load-shadow` | Splits a real v15 save into fluid records and a sanitized blob (`NativeColonyImport`, `NativeWorldBlob`). Compares with an unmodified load. |
| `transition` | Heated ice melts under unmodified definitions (positive control). |
| `filtered-transition` | With fluid-producing definitions suppressed, the ice stays solid. |
| `sanitize` | Expected failure: vacuum edits and `PrepareGameData` without a frame do not commit. |
| `reject-custom-envelope` | After an ordinary load succeeds, a fresh unpatched loader must return null for a valid `MACOLONY` envelope. |

`NativeElementTable` disables whole fluid-producing temperature, sublimation and
conversion branches (`filteredDefinitions`). Dropping only a fluid byproduct
would not conserve mass. The mod's engine must supply the removed phase
behaviour.

## Domain and experiment modes

Each domain mode runs sixty native frames after priming. Every one requires an
original-flow contrast, verified flow flags, exact native mass/temperature
tuples, conserved custom physics, continuing solid heat, and frames that
continue after a declined scalar save.

| Mode | Additional checks |
| --- | --- |
| `sealed-gas`, `sealed-water` | Base gas and compressed-water rooms. |
| `sealed-heat`, `sealed-water-heat`, `sealed-water-small-heat` | Game heat commands with a committed energy account. The small-heat case requires positive conduction and a byte-exact round trip of the carried-U version-10 checkpoint; see [rounding](../../native/MAC_HEAT.md#physical-conduction-and-stored-energy-rounding). |
| `sealed-conduction` | Positive internal redistribution and entropy, with two thermal stages per physical substep, excluding certified equilibrium intervals. |
| `sealed-gravity`, `sealed-water-gravity` | Downward impulse, vertical momentum and an independent U+K+G account. |
| `sealed-water-transfer` | Liquid injection and removal publish only after a native frame; a void-creating request is rejected. |
| `sealed-checkpoint` | A restored image's continuation matches the next native worker frame byte for byte. |
| `sealed-pair` | Captures the native world and physical image at a zero-time barrier with one stable save, immutable buffers and token retirement. |
| `sealed-restore`, `-water`, `-gravity`, `-water-gravity` | Writes the pair as a `MACOLONY` envelope, reloads both sections and matches the resumed frames exactly; see [sealed native domain](../../docs/sealed-native-domain.md). |
| `zero-time-frame` | A first zero-time update only maps the empty backwall cache (`65535`) to vacuum. A second update keeps every cell exactly. Queued vacuum-cell edits then commit without positive timestep returns. |
| `native-disease-retention` | No observer needed. Disease hash 0 with count 24 loads as no disease, mass and temperature are kept, and four frames continue. |

| Variable | Effect |
| --- | --- |
| `MA_VERIFY_FRAME_INPUTS=1` | Copies each accepted-input journal through the C ABI and replays the previous image. All sixty frames must match. Heat scenes need exactly one two-command batch. See [frame inputs](../../docs/native-domain-transactions.md#accepted-frame-inputs-and-completed-images). |
| `MA_VERIFY_COMPLETED_SNAPSHOT=1` | With heat replay, a pending command must leave the completed image unchanged while drained saves reject. Expects 2 batches, 3 commands and exactly 1,500 J. |
| `MA_VERIFY_CHUNK_HEAT=1` | Plain gas or water only. Native body contact heat with full U+K; see [object heat](../../docs/native-domain-physics.md#native-object-heat). |
| `MA_VERIFY_SOLID_HEAT=1` | Plain gas or water only. Solid-cell heat, starting at 400 K, or at the hexadecimal binary32 in `MA_NATIVE_SOLID_TEMPERATURE_BITS`; see [solid heat](../../docs/native-domain-physics.md#native-solid-cell-heat). |
| `MA_NATIVE_DOMAIN_SECOND_REGION=1` | Puts the room in the second of two regions. |
| `MA_VERIFY_CHUNK_HEAT_FORWARDING=1` | Adds a native body to `baseline`; set by [`NativeHookProbe`](../NativeHookProbe/README.md) `--native-body-heat`. |

## Native behaviour these tests depend on

- Frames are asynchronous. Poll snapshots for completion before asserting. A
  zero-frame snapshot does not acknowledge queued edits, which is why `sanitize`
  fails.
- Frame-message counts can rise without physical time, so count positive
  timestep returns instead.
- The first query normalizes empty backwall identities. Compare against a
  warmed baseline.
- Prime body registration with two zero-time updates before the first contact.
  Otherwise the first Windows output slot can hold an uninitialized
  accumulator; see
  [the worker harness](../../docs/windows-validation.md#isolated-native-worker-harness).
- A public 64×64 grid serializes as 66×66 cells. Version 15 has a 29-byte
  header, then 16-byte cell, 8-byte disease and 12-byte backwall arrays. The
  initial save is 156,845 bytes.
- Initialization metadata differs from normal load metadata, so always compare
  a transformed load with an unmodified load.

## Status and limits

With `MA_VERIFY_SOLID_HEAT=1`, the plain sealed gas/water controls exercise
native solid boundary heat. Add `MA_VERIFY_SOLID_HEAT_STARTUP=1` to prepare the
solid before owner creation and register it in the atomic startup call. The
first positive owner frame must exchange heat; the final report distinguishes
this path with `nativeSolidHeat.AtomicStartup`. Omitting the startup option
retains the later, revision-checked registration control.

On build 744825, the default suite passes on Windows and Linux, with `sanitize`
failing as expected. Linux uses Windows-exported tables, and both produce
byte-identical initial saves. The gas, water, heat, conduction, gravity,
checkpoint and pair controls pass with the Linux library and the Windows
binaries under Wine 10, as do the frame-input, completed-snapshot and body-heat
options. The linked feature docs record later controls. In `fluid-actors`, eight
active steps consume 0.3 kg and six positive emission samples produce
0.28125 kg; emission entries are handle-indexed status slots. Blocked callbacks
and multiple actor handles are not covered.

The harness does not prove colony integration, fluid–solid coupling, absence of
transient fluid within a frame, chunk, building or backwall transitions,
performance, Unity presentation or colony save and reload. The in-game controls
in [sealed native domain](../../docs/sealed-native-domain.md) and
[ONI_HEADLESS.md](../ONI_HEADLESS.md) cover those.
