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
- `multi-region`: run the twelve timesteps with two active regions and require
  heat transfer in both, without multiplying elapsed physical time.
- `fluid-actors`: register a native oxygen consumer and emitter in separate
  sealed cells. One registration timestep precedes eight active timesteps
  (with changed rates halfway) and four timesteps after removal. Check two
  registration callbacks, eight consumed-mass records, the emitter's interval
  clock reset on modification, cell/world mass ledgers and continuing solid
  heat transfer. Emitted-mass entries are handle-indexed status slots, including
  zero-mass waiting placeholders; they are sampled once per physical frame.
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
  continuing solid heat. This uses the shared `NativeColonyImport` staging API
  (including the outer length prefix) and `NativeWorldBlob` parser. The harness
  binds captured identities for inspection; it does not claim material-model support.
- `transition`: heat an ice cell above its transition threshold and verify the
  unmodified native definitions produce fluid. This is the positive control.
- `filtered-transition`: suppress native fluid-producing element definitions,
  repeat that control, and check that the cell stays solid and no fluid appears.
- `reject-custom-envelope`: first load an ordinary saved native world as a
  positive control, then pass a valid MACOLONY envelope to a fresh unpatched
  native loader. Require a null rejection result and shut down without starting
  or inspecting the failed load. Its custom payload is explicitly opaque
  diagnostic data, with no owned-reservoir or full colony save.

Filtering uses `NativeElementTable`, which validates all rows first and disables
whole fluid-producing temperature/sublimation/conversion branches. The reported
`filteredDefinitions` counts suppressed branches. This includes the case where
only a transition byproduct is fluid; dropping that byproduct alone is not a
conservation-consistent suppression policy.

`run.ps1` starts one test process with a 45-second timeout and captures stdout,
stderr and its JSON result. Its timeout kills only the process it starts.
The runner copies managed assemblies to a unique local temporary folder;
Windows may refuse loading them directly from SMB. The native library can
crash on an invalid contract; isolation is intentional. Submitted frames are
asynchronous: snapshot polling must observe their completion before assertions.

The current domain client uses ABI 2 and five physical species. The Linux
wrapper accepts `--mode domains --observer /path/to/backend --game /path/to/game`
for the sealed gas, compressed-water, heat, conduction, gravity and checkpoint controls. Each runs the actual
native clock for sixty post-priming frames, preserves exact original native
mass/temperature tuples, and advances after declining a scalar save. Individual
`sealed-gas`, `sealed-water`, `sealed-heat`, `sealed-conduction`, `sealed-gravity` and `sealed-checkpoint` modes are also available.
Conduction additionally requires positive internal redistribution/entropy and
two coherent thermal stages per accepted solver substep; its walls remain insulated.
Gravity additionally checks downward body impulse, physical vertical momentum
and an independent U+K+G energy account using the cell masses and elevations.
Checkpoint restores a complete physical image and compares its independent
continuation byte for byte against the actual next native worker frame. It
retains ordinary scalar-save rejection; full colony persistence remains open.
The Windows PowerShell wrapper accepts these modes with `-Observer` and
`-GameDirectory`.

On Linux, those same controls can execute the Windows binaries by adding
`--wine`, `--wine-prefix` and `--windows-dotnet` to the wrapper; point `--native`
at the licensed Windows SimDLL and `--observer` at the Windows backend.
The prefix must already map the host root at `Z:`. All three controls pass
under Wine 10, with explicit metadata preventing them from being counted as
an actual Windows OS or Unity game test. Linux native execution passes too.

On Linux, `run.py` runs the selected modes in separate processes, each with a
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

The new `fluid-actors` case passes on Windows 744825, both unhooked and under
the forwarding observer, with identical physical and event reports. It requires
thirteen actual native timestep returns and physical updates, not thirteen
queries: registration publishes on the first physical update. The eight active
steps consume 0.3 kg; six positive emission samples produce 0.28125 kg, with two
waiting samples after interval-clock resets. Four later steps show no transfers
after removal. Linux actor execution, blocked/unblocked callbacks, multiple
actor handles and Unity gameplay routing remain unverified. See the
[backend evidence](../../docs/native-backend-integration.md#current-evidence-and-limits).

The frozen Windows-v4 and isolated Linux-v5 seven-mode matrices both exercise
`reject-custom-envelope`. In each, an ordinary native-load control preserves
the two fluid cells and their 102 kg; the unpatched native API then returns null
for a valid MACOLONY envelope, with no native diagnostics and normal child
completion after owner disposal. Linux reuses the exact Windows-v4 managed
IL/Core/runtime files and Windows-exported element/disease tables against the
pinned licensed Linux `libSimDLL.so`, without a rebuild or Unity/Steam launch.
Its real native payload and complete envelope bytes match Windows exactly;
an independent decoder reconstructs all 4,096 public cells and the envelope
checksum. The custom payload is opaque diagnostic data, not an owned
Core/reservoir save. Neither matrix verifies Unity's outer SaveLoader, saved
objects, frontend abort, world-generation fallback or post-rejection native
state. Evidence remains under `local/native-envelope-suite-windows-v4` and
`local/native-envelope-suite-linux-v5`. Linux's six successful child modes
exit zero; sanitation still exits one with its exact documented failed
zero-frame observation. Suite acceptance does not mean sanitation succeeded.

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

The explicit `zero-time-frame` lifecycle experiment requires the observer.
Its current Linux and Windows-under-Wine controls pass: the first zero-time
update resolves only the initial empty backwall index cache (`65535` to the
registered vacuum index); a second zero-time update retains every complete
cell tuple exactly. Queued cell edits and flags on a separate vacuum cell then
commit on another zero-time frame, while solid temperatures stay exact and
the observer records zero positive timestep returns. Native frame-message
counts can still increase, so they must not stand in for physical time.

Earlier negative results remain private: v1 retained 2 kg of fluid when flags
targeted the occupied oxygen cell; v2/v3 compared the first update directly
against its uninitialized backwall cache and failed on both platforms. These
are not counted as passing the original controls. The current protocol tests
the exact cache mapping before using a fully initialized baseline. It does
not prove that flagging occupied cells is displacement-free, nor does it
complete paired colony save/load. The default nine-mode suite is unchanged.
