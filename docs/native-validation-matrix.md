# Native Unity validation matrix

`tools/oni_native_matrix.py` runs the bounded native-domain scenes in fresh
Unity processes. Each scene writes an ordinary paired colony save, then a second
process reloads it, runs the paused frame-zero autosave and compares every
resumed physical frame with independent continuation. It is a regression
matrix for development scenes; first-playable, whole-colony and release
readiness remain false in every report.

## Platforms and terms

| Term | Meaning |
| --- | --- |
| Linux Unity (LinuxPlayer) | The licensed native Linux player on the private Steam desktop, driven by `tools/oni_headless.py` |
| WindowsPlayer under Proton | The Windows Unity player on a Linux host through official Proton 10 with OpenGL, driven by `tools/oni_proton_headless.py` ([Proton runner](windows-validation.md#windowsplayer-under-proton)). Reports keep `actual_windows_os: false` |
| Actual Windows | A Windows OS host with MSVC-built libraries, driven by `tools/run_colony_validation.ps1` |
| Wine | GNU-built Windows binaries running native CTests or licensed SimDLL worker controls under Wine on Linux, without Unity |
| Licensed worker | `tools/NativeShadowProbe` / `tools/NativeHookProbe` controls that drive the real SimDLL without Unity |

A Proton result never certifies the Windows OS, and a Linux matrix never
supplies Windows evidence.

## Running the matrix

Use a frozen runner checkout and an already built package; neither may change
until the matrix finishes, so other work can continue on `main` meanwhile.

```sh
git worktree add --detach /tmp/oni-native-matrix-runner HEAD
python3 tools/oni_native_matrix.py \
  --runner-source /tmp/oni-native-matrix-runner \
  --package /absolute/path/to/built-probe \
  --output artifacts/native-matrix-001 \
  --frames 120 --timeout 1800
```

- The output directory must be new.
- By default every scene in the script's `SCENES` map runs; select some with,
  for example, `--scenes water gravity`. Scene names are the short names used
  throughout these documents (`gas`, `water`, `radius`, `mixed-heat`, ...).
- Frames: 1–10,000; at least 80 for `actor`/`water-actor`, 30 for
  `carbon-particle`/`breathing-read` and 16 for `save`.
- `--timeout` applies to each game separately, 30–7,200 seconds.
- `--private-display` gives each native Linux game its own X server and window
  manager, leaving the shared Steam desktop free for login. Each child must
  report a different display and verified retirement; see
  [display lifecycle](../tools/ONI_HEADLESS.md#separate-test-display).
- `--proton /path/to/proton --game-dir /path/to/windows-game` with a Windows
  probe package runs WindowsPlayer, one fresh prefix per game. The matrix loads
  only the runner's authenticated host save; raw and host file hashes must match.

`matrix.json` records hashes of the runner, reviewer, matrix script and every
packaged file, each child result's hash, physical counters and checkpoint
receipts. Changed frozen inputs, an unsuccessful exit, a missing or failed
result, a save outside its disposable run, a mismatched source receipt or a
changed source file abort the matrix, keeping completed reports and logs. The
matrix is successful only after every selected save and reload passes.
Orchestration tests exercise these failures, including mutation of the matrix
script itself, without launching a game.

Runner Git metadata is recorded only when the runner directory is itself a
checkout root; an archive inside another checkout reports null Git metadata.
Use the recorded file hashes for source identity.

Actual Windows runs use `run_colony_validation.ps1` per scene (`-NativeDomain
<scene>`, `-SaveColony`, then `-NativeDomain load -LoadSave <file>
-ResaveBeforeResume [-ReloadInProcess]`); see
[Windows validation](windows-validation.md#windows-os-game-runs).

## Fluid regression matrix

A frozen cohort with native build v18 completes 18 real-game cases: six sources
and twelve load processes holding 24 continued sessions, each verifying all 120
complete physical frames individually (2,880 exact frames). New LinuxPlayer and
WindowsPlayer (Proton) sources each pass local and cross-platform reloads, with
two sessions per load process. The scenes cover legacy fixed-opacity
compatibility, phase-volume appearance, [physical viscosity](native-domain-physics.md#physical-viscosity)
and [radius consumption](native-domain-transactions.md#radius-consumption).
Frame-zero saves, native references and readers, completed rendering and
unchanged source files pass in every case. These are six-cell controls;
free-surface geometry and whole-colony ownership remain open.

## WindowsPlayer feature matrix

A frozen cohort with native build v15 passes twelve WindowsPlayer source/load
controls in ONI 744825 / Unity 6000.3.5f2 under Proton. Each source starts a
fresh nine-world Spaced Out colony; each load uses a fresh process and the
source's complete save.

| Scene | Source behaviour | Fresh load |
| --- | --- | --- |
| [Gas import](native-domain-physics.md#initial-inventory-admission) | Imports completed native gas masses and temperatures before fluid exclusion | Paused frame-zero autosave, 120 exact complete frames |
| Water import | Imports completed native water masses and temperatures | Same |
| [CO2 particle](native-domain-transactions.md#additive-cell-mass-edits) | Real `CO2Manager.SpawnBreath`, one 0.03125 kg receipt, native fluid dispatch avoided; the particle retires and its component is reused | Same |
| [Breathing reader](native-domain-readers.md#breathing-selection) | 24 owned reads through the native breathing helper; outside behaviour unchanged | Same |
| [Gas pressure](native-domain-readers.md#physical-pressure) | Six physical pressure samples, five caller patches, 824 game call-site reads; outside behaviour unchanged | Same |
| Water pressure | Six compressed-water pressure samples; caller patches verified, no call-site read in this scene | Same |

The historical runs below included the former nitrogen tag checks; that
content is now removed. Current starts check the original native enum-to-tag
helpers without registering aliases. Every save passes body/header readback and
the strict [orbital save-root gate](native-domain-persistence.md#orbital-reference-admission).
Every reload keeps its source image before advancing, checks complete state on
every frame and passes restored readers, the calibrated GPU check and
pause-depth restoration. This accepts the bounded incoming reader, import and
particle paths on WindowsPlayer. It does not establish complete metabolism,
production machinery, every native reader, whole-colony ownership or all-DLC
progression.

## Open

- No single cohort has run every default scene; scenes have passed in separate
  frozen cohorts.
- Most feature scenes have WindowsPlayer-under-Proton evidence only. The
  scenes verified on the Windows OS are listed in
  [Windows validation](windows-validation.md#windows-os-game-runs).
