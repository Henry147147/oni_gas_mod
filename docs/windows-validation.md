# Windows validation

Current private Windows candidate: **`bfb1168`**. Ordinary Spaced Out! New Game,
manual saves and fresh-process loading are verified under Proton; the full
release gate remains open. See [install instructions and exact provenance](spaced-out-private-playtest.md).

Spaced Out! update, 2026-10-27: native `6c91161` passes all 86 Windows
cross-build CTests under Wine. The `c942141` managed recovery controls pass
actual flight/self-destruct and landed retirement/reuse, each followed by a
fresh-process load. `0b93056` passes gas and liquid cargo loading/unloading.
These retain their exact revision and native-binary provenance; they are not
relabeled as current-package or Windows OS acceptance. Ordinary DLC activation
and its separate package/UI/load controls pass as recorded in the current guide.

## Private playtest build (2026-10-25)

The current cross-build uses MinGW-w64 GCC 13 (POSIX threads), rather than
MSVC. WindowsPlayer-under-Proton evidence still needs an actual Windows PC
check. Build from a Linux checkout with CMake, Ninja, .NET and the MinGW tools:

```sh
export MA_MINGW_ROOT=/path/to/mingw/usr
export PATH="$MA_MINGW_ROOT/bin:$PATH"
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DMA_BUILD_ONI_NATIVE_BACKEND=ON -DMA_WINE=/absolute/path/to/wine
cmake --build build-windows -j 8
ctest --test-dir build-windows -j 8 --output-on-failure
MSBUILDDISABLENODEREUSE=1 dotnet build managed/MixedAtmospheres.Oni -c Release \
  -p:UseSharedCompilation=false -p:OniGameDir=/path/to/windows/OxygenNotIncluded \
  --artifacts-path "$PWD/build-managed-windows"
python3 tools/package_probe.py --native build-windows/mixed_atmospheres.dll \
  --native-observer build-windows/mixed_atmospheres_oni.dll \
  --managed-dir build-managed-windows/bin/MixedAtmospheres.Oni/release \
  --output artifacts/windows-playtest/package
x86_64-w64-mingw32-objdump -p build-windows/mixed_atmospheres.dll
x86_64-w64-mingw32-objdump -p build-windows/mixed_atmospheres_oni.dll
```

Use the team's CPU lock wrapper around builds/tests on the shared machine.
Set `WINEPREFIX` to a private directory before CTest. For an unpacked Wine
installation use its real `wine` executable and set `WINEDLLPATH` and
`WINESERVER` to that installation. Both DLL import tables must contain only
Windows system libraries: no libstdc++, libgcc or winpthread DLLs. The toolchain
links these runtimes statically and leaves GCC 13's harmless small-pair-copy
warning visible without treating it as a build error. Solver arithmetic and
floating-point flags are unchanged. The Python Maxwell–Stefan reference check
uses the same configured emulator as the native test executables.

The Proton runner accepts `--mode colony-owner` (new Terra on defaults),
`--scenario` with a comma-separated gameplay batch, `--save-colony`, and
`--mode colony-owner-load --load-save <host path>` for a fresh process.
These owner modes remove inherited `MA_` overrides. `--frames 3132` continues
past the first cycle; normal game autosaves remain enabled. Owner reports,
scenario checks, saved paths/hashes and conservation errors are retained in
`result.json`. A main-menu pass alone never establishes colony ownership.

The extracted private ZIP contains `install_windows.ps1` and a
`MixedAtmospheres` directory. With ONI closed, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\install_windows.ps1
```

Then start ONI, enable Mixed Atmospheres in Mods and restart when requested.
**In the main menu, turn OFF Spaced Out! and any other gameplay packs (ONI
restarts), then start a new base-game colony.** No environment variables are needed. The script
uses Windows' Documents known folder (including redirected Documents), verifies
the package hashes, installs one Dev copy, and moves previous Mixed Atmospheres
copies outside the mod scan into a backup folder. It preserves
`mods/config/MixedAtmospheres` and all saves. `-OniUserRoot` overrides a custom
ONI user-data location; `-Package` overrides the extracted package directory.

The current private package targets 64-bit Windows and ONI 744825 with the
MinGW build above. Older Windows OS evidence below used MSVC and verifies only
its recorded revisions. Three routes exercise the Windows target; only the
first establishes Windows OS behaviour.

| Route | Tool | Establishes |
| --- | --- | --- |
| [Windows OS game runs](#windows-os-game-runs) | `tools/run_colony_validation.ps1` on the Windows desktop | Actual WindowsPlayer on Windows with the MSVC build |
| [WindowsPlayer under Proton](#windowsplayer-under-proton) | `tools/oni_proton_headless.py` on Linux | Real Windows Unity player execution; not Windows OS evidence |
| [Isolated native worker harness](#isolated-native-worker-harness) | `tools/NativeShadowProbe` on the Windows host | The licensed Windows SimDLL with the custom owner; no Unity |

Every route uses the licensed Steam installation and disposable colonies.
Steam license checks stay active, and no first-playable or whole-colony field is
ever set by these runs. The current per-feature scope is in
[implementation status](implementation-status.md).

## Windows OS game runs

Build an observer-enabled Windows package ([README](../README.md#compile-install-and-enable)),
close ONI, then run the disposable-profile wrapper in PowerShell 7:

```powershell
./tools/run_colony_validation.ps1 -Package ./artifacts/windows-probe `
  -NativeDomain gas -Frames 120 -TimeoutSeconds 1200
```

`-NativeDomain` selects a scene (`gas`, `water`, `mixed`, `heat`, `transfer`,
`actor`, `carbon`, `conduction`, `gravity`, `water-gravity`, `checkpoint`,
`pair`, `save`, `load` and others listed in the script). Add `-SaveColony` to a
source scene, then reload with `-NativeDomain load -LoadSave <file>
-ResaveBeforeResume`, optionally `-ReloadInProcess`. `-TileHover` and
`-Presentation` run the display fixtures. Actor scenes need at least 80 frames.
Python must be available for the shared acceptance checker. The wrapper
restores the original mods, preferences and generated-world files afterwards.

Verified on Windows OS: mixed-air and compressed-water scenes, conduction, CO2,
registered gas/water actors, gas and filled-water gravity, ordinary air/water
saves with fresh-process loads (paused frame-zero autosave and 120 exact
continued frames), same-process main-menu reload, Save As creation and reload,
and the display fixtures (four-gas hover, presentation, nitrogen inspection,
reservoir bridge). Still open on Windows OS: the features verified only under
Proton (below), including live-actor and in-flight particle saves; configured
rooms; and the save-failure UI.

### Windows game checks, 2026-10-17

Source `9eb807b`, ONI 744825, Unity 6000.3.5f2, WindowsPlayer with Direct3D 11,
disposable Spaced Out! colonies:

- Water: a freshly generated source completed 121 native frames and an
  admitted save with GPU renderer binding/readback; its reload restored the
  identical checkpoint, resaved at frame zero and matched 120 frames. Gas source
  and reload passed the same physics and save checks. Both reloads pass the
  saved frame-input journal reader (a transcript check, not an independent
  fluid solution); water equilibrium recorded zero mass and energy drift.
- Tile hover: four real mouse-hover cases list nitrogen 60.9997%, oxygen 25%,
  carbon dioxide 10% and steam 4% without selection. Benign hydrogen and
  hazardous chlorine are omitted from the bulk list while the chlorine warning
  reports 0.0002% against a 0.0001% limit. The exact-threshold case keeps steam;
  moving the cutoff one binary64 step above its mole fraction removes it. The
  warning survives both cases and 125% UI scale.
- Overlay shortcut: Shift+F5 lost to ONI's light overlay. The fix remembers the
  overlay mode before the F5 frame and applies the queued cycle in `LateUpdate`,
  including while paused, and clears pending input outside a colony. All five
  cycle positions keep the same sampled reading and publication.
- Presentation fixture: normal, oxygen-rich and thin air, CO2, chlorine and
  vacuum; colour-only mode, paused composition changes, checkpoint restore,
  zoom, UI scaling, asteroid switching; six bound inventory surfaces (pipe,
  reservoir, bottle, suit, vent, machine) including replacement and retirement.
  19,705 frame samples had median 6.237 ms, p95 7.367 ms and p99 32.165 ms in a
  mostly paused colony with screenshots and no baseline subtraction; this does
  **not** pass the active simulation performance target.

Open issues from this cohort:

- Reusing a cached generated world fails at startup in
  `CustomGameSettings.GetCurrentClusterLayout`, before the colony starts; fresh
  generation passes.
- The reload runner misses the late archive ONI writes under the source
  colony's name; it needs a scoped cleanup repair before claiming automatic
  restoration for reloads.
- The boundary outline can cross a hover card at some zooms, and the status
  panel samples the hovered or first cell, which can differ from the selection.

Hover receipts are read through a reader that shares deletion but never
writing: on Windows a renamed receipt can be visible while the rename's DELETE
handle is still open, and an unfinished exclusive writer must still reject.

## WindowsPlayer under Proton

`tools/oni_proton_headless.py` runs the licensed Windows Unity player through an
installed Proton tool on a private Xvfb display, with the same native scene and
save/load acceptance checks as the Linux and Windows runners. Reports identify
WindowsPlayer on Linux; a pass cannot certify the Windows OS or the MSVC build's
Windows-specific behaviour.

Start the private Steam desktop with `tools/oni_headless.py start` and sign in
with an account that owns ONI. Install a complete official Proton tool and pass
its directory containing `proton`, `version` and `files/bin/wineserver`. Build
the managed adapter against the Windows game assemblies and package both Windows
native DLLs with `tools/package_probe.py --native ... --native-observer ...`.
The runner verifies the package hashes and the pinned game assembly/SimDLL
hashes.

```sh
python3 tools/oni_proton_headless.py \
  --proton /path/to/proton \
  --game-dir /path/to/Windows/OxygenNotIncluded \
  --package /path/to/windows-probe \
  --graphics-api glcore \
  --mode native-liquid-gravity-domain --frames 120 --save-colony \
  --output artifacts/windows-water-gravity-001
```

A successful source save exports `host_saved_file` and its verified SHA-256.
Pass that host path to `--mode native-load-domain --load-save ...
--resave-before-resume` for paused frame-zero saving and exact fresh-process
continuation, and add `--reload-in-process` to repeat through the main menu in
the same Unity process. `--mode menu` is a separate startup control that needs
only the scoped integration checks and main-menu contract export
(`passed-scoped-main-menu`); it never passes native-domain ownership.
[`tools/oni_native_matrix.py --proton`](native-validation-matrix.md) runs the
save/reload matrix through this runner.

Isolation and receipts:

- The runner holds the Linux runner's lock for the whole test and declines
  while a game is running; finish or pause a test orchestrator between games.
- Each invocation creates a fresh Wine prefix, Windows Documents/mods/save
  profile, protected X display and log directory. The Steam session and
  licensed installation are mounted read-only; Proton's own directory is
  writable for its `dist.lock` and tool fixups. The runner never changes the
  shared Steam desktop's display or signals another prefix's Wine processes.
- Input saves are copied and checked for mutation. C: and Z: paths in reports
  must resolve to real files inside the invocation's directory; UNC paths,
  traversal and symlink escapes reject.
- `result.json` records the target, Linux host, Proton version, package
  revision and hashes, raw mod reports, logs and acceptance result, plus the
  nonsecret settings `MONOMOD_RunningOnWine`, `MONOMOD_DMDType`,
  `PROTON_USE_WINED3D` (default `1`; an explicit inherited value is honoured)
  and `WINEDEBUG`. The rest of the inherited environment is never exported.
  Record the exact Proton version and download hashes for any comparison.
- A main-menu boot alone does not pass a scene: passing requires the
  integration probe, contract export, completed colony report, native
  ownership, rendering and physical conservation checks. Failures, including
  prefix initialization and cleanup failures, stay in the evidence directory.

Graphics and clock: the runner selects OpenGL Core by default and records
`graphics_api`; `--graphics-api d3d11` is for explicit comparisons. With
software Direct3D 11 under Proton (official 10.0-4b, Experimental 11 and
GE-Proton10-34), Unity's unscaled clock stays frozen near 65,358,361,937 s,
close to Unity's [IntegerTime limit](https://docs.unity.com/en-us/engine/6000.3/script-reference/unity/integertime/discretetime/maxvalueseconds),
while scaled time, realtime and the Win32/NT performance counters advance. An
independent diagnostic with no Harmony patches and no custom native libraries
reproduces it. Changing the counter origin, adding a window manager or
reinitializing the fixed step does not help; DXVK with software Vulkan fails at
graphics startup. The root cause is undiagnosed. On D3D11 the full mod also
failed startup in `NitrogenElementTagPatch.VerifyCalls`
(`GameTagExtensions.CreateTag` null reference). On OpenGL Core the clock
advances normally and the patched tag helpers, scoped probe and menu export
pass, so OpenGL Core is the supported route. GE-Proton builds are alternative
experiments, not evidence for Valve's builds or for the Windows OS.

Verified under official Proton 10 with OpenGL: gas and water live-actor saves
and reloads, natural-day autosave, nine-world Spaced Out! sources, the
twelve-control [import and reader matrix](native-validation-matrix.md#windowsplayer-feature-matrix),
gas/liquid coexistence, conduction, viscosity, radius consumption, body heat
and in-flight CO2 particles, with local and cross-player reloads against native
Linux ([actor persistence](native-domain-persistence.md#durable-point-actors)).
The runner accepts both configured modes; the configured gas room's save and
fresh-process load pass with MinGW-built DLLs
([configured saves](native-configured-domain.md#saves-and-reloads)). That is
neither Windows OS nor MSVC evidence.

The completed-frame publication repair also passes WindowsPlayer under
official Proton 10.0-4b with MinGW-built DLLs. The solid-heat source/save
completes 121 contacts and its fresh-process load completes 120 more, with zero
published-temperature energy residuals, a paused frame-zero resave and 120
exact accepted-input continuations. This is real Windows Unity player execution
on Linux; actual Windows OS/MSVC acceptance of the new bindings remains open.
See [the publication contract](native-domain-publication.md).

## Isolated native worker harness

The [NativeShadowProbe harness](../tools/NativeShadowProbe/README.md) runs the
installed Windows x64 SimDLL in a separate console process with disposable
native worlds and the custom owner; it does not run Unity. The PowerShell
wrapper takes `-Observer` and `-GameDirectory`; `MA_VERIFY_FRAME_INPUTS`,
`MA_VERIFY_CHUNK_HEAT` and `MA_VERIFY_COMPLETED_SNAPSHOT` add the input-replay,
body-heat and completed-image controls. The same controls also run the Windows
binaries under Wine on Linux, which is recorded separately from Windows OS
evidence.

Verified on the Windows host: the full native regression and saved-data reviews
pass on Windows, Linux and Linux ASan/UBSan, with every sanitizer compile
command instrumented. Licensed gas and water solid-heat controls each pass 56
positive contacts and 61 exact accepted-input replays; body-heat controls pass
the same counts; the pending-input scene keeps its immutable completed image,
two batches, three commands and exactly 1,500 J of committed heat. Saved review
checks digest chains and represented heat/energy endpoints; the console report
does not export raw journals or every per-contact operand, so it is not an
independent fluid solution.

Pitfalls found on this route:

- Native body registration leaves the output accumulator uninitialized until a
  zero-time output-list update, so the first readout can be wrong. The harness
  initializes registration with zero-time lifecycle updates before the first
  positive contact.
- Under the game's DAZ/FTZ, cold JIT compilation folded tiny binary32 test
  constants to zero. Tiny parcel sources use a raw-bit boundary
  ([wire conversion](native-platform-numerics.md#binary32-wire-values)).

Open: strong-flow accuracy, complete native reader/writer coverage and the
first playable colony, under the [completion plan](completion-plan.md).
