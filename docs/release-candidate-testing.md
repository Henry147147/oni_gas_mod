# Release-candidate testing

This is the working test plan for the first Steam release, not a release claim.
The current gate status and private evidence paths are in the ignored
`artifacts/release-candidate-2026-10-29/gate-ledger.json`. A prior result is
evidence for its recorded revision and package; it does not pass a gate for a
later candidate. Record each candidate's complete source revision, dirty state,
package and game hashes, platform, renderer, settings, test inputs and results.
The authoritative scope and bounds remain in [the completion plan](completion-plan.md#release-acceptance)
and its amendments. No physics setup, scenario input, grid or acceptance bound
is changed here.

## Current preflight

On 2026-10-29, normal Steam integrity verification and app information reported
the installed Linux public build 24423041 as the latest public build; the public
testing branch reported the same build. The 135 recorded game-code hashes did
not change. The exact Steam evidence is private under
`artifacts/release-candidate-2026-10-29/steam-latest-verification.json`.

Independently of any new contract generation, the preserved Linux probe and its
original contract resolve 2,588 dependencies with zero differences against that
installation. The preserved `b7a41be` Windows package and its original contract
resolve 2,588 dependencies with zero differences against the retained Windows
744825 game copy. All 303 sources in the package's preserved managed snapshot
match this workspace at the time checked. These are contract and provenance
preflights, not gameplay results. Normal signed-in Steam depot downloads have
also provided the current public Windows base-game and owned-DLC files; their
recorded code hashes match the retained Windows copy. The preserved Windows
package's original contract independently resolves 2,588 dependencies against
the new payload with zero differences. This Linux-hosted download is not an
actual Windows OS update or acceptance test. The Windows
contract has no Linux platform entry; attempting to verify it as Linux refuses,
as recorded privately.

## Frozen candidate 1fa31a4

The latest Steam payload passed fresh Linux and Windows native builds and all
90 CTests on each (Windows under Wine), the complete managed/Python suites,
and 2,588-dependency checks in each game's own Mono runtime. A separate clean
Ubuntu 22.04 build passed all 90 tests; both libraries require glibc at most
2.34. These preflights do not establish Windows OS acceptance. The user chose
to keep actual Windows checks pending on 2026-10-29.

All 15 original Linux gameplay batches finished: 11 batches and 61 of their
65 scenarios passed. Freeze/melt, airflow, whole-world breathing CO₂ and the
world-germ growth comparison failed their unchanged criteria. The separate
Spaced Out! nine-world control passed. Full M2 and M3 passed every required
grid. Full M5 failed on grid 40 near its endpoint with a primary-storage
rounding rejection; the passing coarser grids do not pass M5. Both original
180-second strong-flow captures timed out without a complete scientific
verdict. Private details are in `original-matrix-summary.json` and
`native-gates/` under the evidence directory above. This candidate is rejected
for release; long runs and progression cannot qualify it.

The germ comparison exposed a diagnostic clock defect: an unrelated region's
batched ticks inflated the reported elapsed-frame counter. The correction
counts native frames and leaves per-cell evolution untouched. Twenty-nine
old/new physical snapshots match exactly; only the evolved wrapper's diagnostic
counter and checksum differ. Existing saved counters retain their absolute
baseline; cold reencoding and subsequent nominal-frame deltas are tested.
No schema, ABI, profile or physical checkpoint version changes.

The benchmark now runs current profile 9, records its committed activity
census, and uses an explicit always-simulate policy for the unchanged fully
active release workloads. Its frame timer includes native queues, census,
advance, publication copy and receipt work. It still excludes managed/UI
costs. The new whole-colony `breath-particle` diagnostic isolates normal native
breath deposition and retirement using one coherent arena publication. The
retired nitrogen-based particle fixture is refused before a headless launch;
its refusal is not evidence of a whole-colony deposition failure. The original
breathing criterion remains unchanged. Current-candidate game rechecks follow
the rebuild of these fixes.

The rebuilt `4638bfc` probe passed the original world-germ scenario in game.
Its new breath diagnostic observed the exact declared parcel and native pool
retirement, but the fluid endpoint was 0.031249999647146417 kg rather than
0.03125 kg. That fluid-only comparison remains recorded as failed. The
diagnostic now checks compatibility with the separately sampled whole-owner
trace-sink increment, keeping the 1e-10 kg residual tolerance. This increment
is only an upper bound for the arena's trace loss: it does not attribute trace
to the arena or establish an exact regional mass/energy ledger. The original
whole-world breathing gate remains open.

The corrected fully-active native performance suite completed under WHOLE
locks without owner faults. It fails the fixed cost and disturbance bounds:

| Case | Mean ms / 0.1 simulated s | p95 ms / 0.1 simulated s | Fixed requirement |
| --- | ---: | ---: | --- |
| 100k, default | 930.604 | 989.731 | mean ≤ 25 |
| 100k, cap4000 diagnostic | 847.663 | 910.443 | mean ≤ 25 |
| 100k, no hydrogen rooms | 401.020 | 443.654 | mean ≤ 25 |
| 250k, no hydrogen rooms | 2374.710 | 4463.010 | p95 ≤ 62.5 |

The default 100k case spends 1823.598 of its 1861.207 ms frame in native flow;
the view copy costs 0.347 ms. All three fixed disturbances remain unrecovered
after their 45-second observation windows, exceeding the required 30 seconds.
Observed extra peak memory stays below 2 GiB, but these short windows do not
prove bounded long-run memory. The cases preserve the original fixtures and
inputs, run profile9 with tiers explicitly disabled for the fully-active
workload, and include queue/census/receipt work in their frame cost. They do
not measure managed code, rendering or Windows OS performance. Private results
are in `native-performance-4638/` and `native-performance-gate-evaluation.json`.

## Gate matrix

| Gate | Required current-candidate evidence | Status |
| --- | --- | --- |
| Builds and compatibility | Clean Linux and Windows native/managed builds, test suites, actual game contracts and Mono checks; supported game builds and changed hooks verified; fail-closed negative controls | Fresh Linux native build, 90 CTests and the managed/Python suite pass; complete committed-candidate and Windows verification pending |
| Numerical physics | All fixed gravity, free-surface, multiphase, vacuum and strong-flow gates, including full-grid runs; 10,000-step closed mass drift ≤ 1e-8 and energy drift ≤ 1e-6, with separate trace and source/sink ledgers | Full M2/M3 pass; M5 grid40 fails primary-storage rounding. Strong-flow remains open; both original captures timed out before a complete verdict |
| Game accuracy | Fixed F1–F5 owner and vanilla comparisons, including independent F3/F4 timing truth and solid heat exchange | Open |
| Gameplay | Base game and Spaced Out! generation and all supported producer, consumer, storage, disease, machine, rocket and inter-asteroid lifecycles; starting colonies reach sustainable life support and endgame without developer intervention | Open |
| Persistence | Corruption/missing-mod refusal, save/load and identical future inputs with complete owner, pipe, scheduling and particle state across source, cold and menu continuation | Full active continuation open |
| Performance and presentation | Fixed 100k/250k-cell, memory, publication, allocation and disturbance bounds; sustained actual 3 game seconds/wall second and smooth input, camera and menus on the user's Windows PC for representative base and Spaced Out! colonies; real hover/detail/overlay/contrast on both OSes | Open |
| Long regression | 100-cycle regression per supported pack and both modes, and 1,000-cycle supported all-content colonies on both OSes, with autosaves and inter-asteroid transfers | Harness and continuation work required |
| Unsupported content | Frosty, Bionic, Prehistoric and Aquatic active: ONI retains its simulation, a notice is shown and mixture saves are refused | Pending; those packs are outside first-release support |
| Distribution | Reproducible mod-only folder for both OSes; install, upgrade, stable identity, native imports, Ubuntu 22.04 ABI, supported-build and save policy, documentation and no mod-folder writes | Pending |

The original 32-combination takeover requirement is superseded by the
2026-10-27 scope amendment. A ten-cycle private survival test and a successful
paused-save image check are useful smaller controls; neither replaces the
public long-run or active-continuation requirements.

## Linux run preparation

Freeze a private integration probe only after the native and managed sources
are frozen. Check its manifest and each listed file against the actual build;
use the current official Steam installation, not a regenerated contract as a
substitute for the preserved-contract check. Preserve the package and its
source and binary hashes for the entire matrix. Fresh output directories and
the game/CPU locks are required. The following are entrypoints, not claims of
results; substitute the frozen paths and available GPU display:

```sh
tools/run_locked.sh high dotnet build managed/MixedAtmospheres.Oni -c Release \
  -p:UseSharedCompilation=false -p:OniGameDir="$ONI_GAME_DIR" \
  --artifacts-path "$RC_MANAGED_ARTIFACTS"
tools/run_locked.sh high python3 tools/package_probe.py \
  --native "$RC_NATIVE_BUILD/libmixed_atmospheres.so" \
  --native-observer "$RC_NATIVE_BUILD/libmixed_atmospheres_oni.so" \
  --managed-dir "$RC_MANAGED_ARTIFACTS/bin/MixedAtmospheres.Oni/release" \
  --game-dir "$ONI_GAME_DIR" --output "$RC_LINUX_PROBE"
tools/game_slot.sh low python3 tools/oni_headless.py run \
  --game-dir "$ONI_GAME_DIR" --package "$RC_LINUX_PROBE" \
  --dlc-mask 0 --mode colony-owner --scenario freeze-melt \
  --frames 60 --timeout 1200 --gpu-display "$ONI_GPU_DISPLAY" \
  --output "$RC_OUTPUT/base-freeze-melt"
tools/game_slot.sh low python3 tools/oni_headless.py run \
  --game-dir "$ONI_GAME_DIR" --package "$RC_LINUX_PROBE" \
  --dlc-mask 0 --mode colony-owner --scenario airflow \
  --frames 60 --timeout 1200 --gpu-display "$ONI_GPU_DISPLAY" \
  --output "$RC_OUTPUT/base-airflow"
tools/game_slot.sh low python3 tools/oni_headless.py run \
  --game-dir "$ONI_GAME_DIR" --package "$RC_LINUX_PROBE" \
  --dlc-mask 1 --mode colony-owner --scenario spaced-out-worlds \
  --frames 80 --timeout 1200 --gpu-display "$ONI_GPU_DISPLAY" \
  --output "$RC_OUTPUT/spaced-out-worlds"
```

These focused controls do not replace the 15 fixed Linux batches and nine
starting asteroids in `tools/acceptance_suite.py`, the additional supported-DLC
matrix, or ordinary play. The private JSON ledger records the original batches
and asteroid names. The LOW slot pins these to cores 0–11; they establish
functional correctness only and do not measure performance or achieved speed.
The suite currently creates a detached worktree; project
policy requires work on `main`, so use the main-only private matrix driver
instead of that worktree workflow. The old prepared save is a historical
fixture, not proof that an earlier R&D save is supported.

For each F1–F5 floor arena, use `--floor f1` through `f5` in separate
`colony-owner` and `colony-vanilla` runs. Follow the full timings, sampled
metrics and truth rules in [the floor protocol](base-game-floor.md); use
`tools/floor_metrics.py` to compute its metrics. Keep vanilla measurements as
numbers, not licensed saves or captures. Use the long-run mode where its
existing limits permit it.

Run the commands in [MAC_GATES.md](../native/MAC_GATES.md), including full
M2/M3 and `M5 --run`, both vacuum variants, and the default and release-selector
Riemann checks in [MAC_STRONG_FLOW.md](../native/MAC_STRONG_FLOW.md). Time
long gates with `tools/run_locked.sh`, and retain their exit statuses and
measurements. CTest's coarse M2/M3/M5 checks do not replace the full grids.

The explicit `--long-run --release-run` path allows up to 3,100,000 frames and
14 days for `colony-owner`, `colony-owner-load` or `colony-vanilla`, with
explicit DLC mask 0 or 1, no scenarios and automatic cleanup. The ordinary
`--long-run` limits remain 100,000 frames and 48 hours. Approximately 300,000
and 3,000,000 native 0.2 s frames are required for 100 and 1,000 cycles.
The expanded limit is not a long-run pass: validate bounded telemetry,
physical-progress watchdogs, natural autosaves, memory, backlog and
conservation in the actual runs. Resumable segments require verified exact
next-frame continuation. Preserve the required cycle counts and both OSes.

## Windows preflight and real Windows evidence

Cross-build the Windows native DLLs, run their CTests under Wine, verify system
imports and the managed game contract, and run WindowsPlayer-under-Proton
scenarios and cold/menu load tests using the commands in
[Windows validation](windows-validation.md). These are Linux-hosted preflights,
not Windows OS or user-hardware acceptance.

For a concrete actual-Windows probe, provide the user a frozen integration
package and the matching repository tools. With ONI closed and the ordinary
licensed Steam installation available, the existing PowerShell runner supports
fresh owner checks, for example:

```powershell
./tools/run_colony_validation.ps1 -Package <frozen-windows-probe> `
  -ColonyOwner -DlcMask 0 -Frames 120 -TimeoutSeconds 1200 -SaveColony
./tools/run_colony_validation.ps1 -Package <frozen-windows-probe> `
  -ColonyOwner -DlcMask 1 -Frames 120 -TimeoutSeconds 1200 -SaveColony
```

Each invocation uses a fresh output directory and must retain its result and
package/game provenance. Separately check install/update, save paths and locks,
real rendering and input, the two boil-only settings, representative sustained
3× speed and the Windows half of the gameplay and long-run matrices. The
PowerShell runner now admits `-ColonyOwner -LoadSave -ResaveBeforeResume`;
exercise it on actual Windows before claiming that paused owner-resave check.
Do not present Proton or Wine as its substitute.

The user chooses a licence before publication. A release package is created
only after the gates pass; public release or Workshop upload requires separate
authorization.
