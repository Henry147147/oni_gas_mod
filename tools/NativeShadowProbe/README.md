# Isolated ONI native interface experiment

This original harness loads the user's licensed Windows x64 `SimDLL.dll` or
Linux x64 `libSimDLL.so` in a
separate console process. It creates only a disposable 64×64 native world. It
does not attach to ONI, touch colony saves, or initialize Unity's singleton.
Each platform's native-library hash is pinned to inspected ONI build 744825.

Build with `dotnet build tools/NativeShadowProbe -c Release`. Run the resulting
`NativeShadowProbe.dll` using .NET 10, with these positional arguments:

```
dotnet NativeShadowProbe.dll <SimDLL.dll> <game-contract-local> <mode> <report.json>
```

The private main-menu integration probe exports `game-contract-local` beside
its mod assembly, containing element and disease message payloads plus metadata.
Those files derive from the licensed game. Keep them, game binaries, logs and
test reports outside this source repository and distribution packages.

Modes:

- `baseline`: initialize oxygen, water, vacuum and solid cells, advance twelve
  native frames, and verify that solid heat conduction operates.
- `sanitize`: replace initialized fluids with vacuum, call PrepareGameData
  **without submitting a frame**, and inspect whether sanitation is already
  committed. Refuse to step if this fails. Then assert no native fluid cells
  across twelve frames while solid heat conduction continues.
- `rebuild-shadow`: capture initial fluid inventories, shut down the disposable
  native owner, and reinitialize with solids/vacuum. Compare every nonfluid
  cell, then step twelve frames with zero native fluids and continuing solid heat.
- `load-shadow`: validate and split a real native v15 save into captured fluid
  records and a sanitized native blob. Compare the transformed load against an
  actual unmodified native-load control, then verify native fluid absence and
  continuing solid heat. This uses the shared `NativeWorldBlob` parser.
- `transition`: heat an ice cell above its transition threshold and verify the
  unmodified native definitions produce fluid. This is the positive control.
- `filtered-transition`: suppress native fluid-producing element definitions,
  repeat that control, and check that the cell stays solid and no fluid appears.

`run.ps1` starts one test process with a 45-second timeout and captures stdout,
stderr and its JSON result. Its timeout kills only the process it starts.
The runner copies managed assemblies to a unique local temporary folder;
Windows may refuse loading them directly from SMB. The native library can
crash on an invalid contract; isolation is intentional. Submitted frames are
asynchronous: snapshot polling must observe their completion before assertions.

On Linux, `run.py` runs all six modes in separate processes, each with a
45-second timeout and core dumps disabled. Its summary counts only the exact
expected sanitize failure as a successful negative control; other failures,
missing reports, crashes and timeouts fail the suite. Use a new evidence folder:

```sh
python3 tools/NativeShadowProbe/run.py \
  --native /path/to/OxygenNotIncluded_Data/Plugins/libSimDLL.so \
  --contract /private/path/game-contract-local \
  --output /private/path/new-evidence-directory
```

The current Windows and Linux 744825 results are baseline/rebuild/transition/filter and
load-shadow passes. The sanitize experiment fails as expected: without a
frame, the edits do not become visible. Keep that failure as evidence against
using this activation path. Do not reinterpret a zero-frame snapshot as an
acknowledgement of queued cell mutations.

The Linux test used the element/disease payloads exported by the Windows Unity
probe. Both platforms produced byte-identical initial native saves. The managed
`Sim` and `SimMessages` declarations match; the Linux native import/heat tests
exercised that contract in its own process. A separate actual Linux Unity run
has now passed the six scoped loading/interception/settings checks. That run
exited before the main menu with no local Steam session, so it did not export
Linux element/disease tables. Neither result certifies full colony ownership.

The native save contains a padded grid: public 64×64 produces 66×66 serialized
cells. Version15 has a 29-byte header plus separate 16-byte cell, 8-byte disease
and 12-byte backwall arrays. The initial synthetic save is 156,845 bytes. The
baseline mode exports that file plus copied cell records privately beside its
report. Original initialization metadata can differ from normal native-load
metadata; transformed loads must be compared with an unmodified load control.

These experiments do not prove colony integration, absence of transient fluid
inside a frame, chunk/building/backwall transition coverage, correct fluid-solid
coupling, or usable performance. Filtering native definitions also removes
phase behavior that the mixture engine must implement. A successful result
always retains `authoritativeColonySimulationReady: false`.
