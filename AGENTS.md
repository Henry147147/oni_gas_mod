# Working on Mixed Atmospheres

Mixed Atmospheres replaces Oxygen Not Included's fluid simulation with its own
solver. `native/` holds the solver (`MacClosedWorld`, `native/src/mac_*.cpp`) and
the game backend (`native/src/oni_*.cpp`); `managed/` holds the C# mod; `tools/`
holds build, packaging and in-game harness scripts. The docs are large: read the
few that your task touches, starting here.

## Where things stand

- [docs/feature-completion-status.md](docs/feature-completion-status.md):
  **resume here** — current status toward the beta (sustained 1× on the
  full-colony saves; 3× stays the goal) and what remains open. The user
  published 0.1.8 (`4bdfceb`) on the Steam Workshop on 2026-11-18;
  [0.1.9](docs/playtest-2026-11-19.md) answers the first session on it (a
  save that no longer loaded after a meteor left the map's edge), and
  [1.10.0](docs/playtest-1.10.0.md) (the user's version number) begins the
  speed fixes: a liquid surface under thin gas no longer falls freely, and
  every expensive region gets workers of its own.
  [1.10.1](docs/playtest-1.10.1.md) is 1.10.0 retargeted to game build
  763796, which Steam delivered on 2026-11-19 and every earlier package
  refuses ([how a new game build is retargeted](docs/game-update-resilience.md#retargeting-a-new-build)).
  [1.10.2](docs/playtest-1.10.2.md) is the same code retargeted to the
  hotfix build 764025 of 2026-11-20. Starting with **1.11.0**, one
  package supports all three builds back to 744825. Preserve their entries
  when [adding a supported build](docs/game-update-resilience.md#keeping-an-earlier-build-working).
  [1.11.0](docs/playtest-1.11.0.md) (`ff95138`) also lets pumps that share a
  pipe run steadily; it has been staged for upload since 2026-11-20 with
  its acceptance run incomplete
  ([what ran](docs/playtest-1.11.0.md#staged-package-ff95138)). The Workshop
  copy was 1.10.2 at that time (the user uploads).
  [1.12.0](docs/playtest-1.12.0.md) (`cb39fa5`) is staged on the private
  share. It reduces paused activity checks and per-tick conversion work on
  the November 20 colony and adds storage preferences and a smaller UI.
  [1.12.1](docs/playtest-1.12.1.md) (`08faa66`) fixes stranded Pacu,
  existing storage capacities and compact UI clipping. It retains region
  worker estimates across small terrain edits. All release and patch steps
  passed. The private share holds `publish/update-1.12.1`.
  [1.12.1.1](docs/playtest-1.12.1.1.md) (`3a7ebfd`) fixes the pipe-heat
  autosave crash. The submitted Cycle 49 save passed the Cycle 50 rollover.
  The private share holds `publish/update-1.12.1.1`.
  [1.12.2](docs/playtest-1.12.2.md) reached 2.078× on the November 21
  colony with full-detail fluids every eighth tick.
  [1.12.2.1](docs/playtest-1.12.2.1.md) fixes the Cycle 55 momentum failure
  when motion decays in an almost still liquid pocket.
  [1.12.2.2](docs/playtest-1.12.2.2.md) addresses a second Windows crash
  found while qualifying that candidate. The [performance handoff](docs/performance-handoff.md)
  records how to repeat the November 21 colony measurement.
  [1.12.2.3](docs/playtest-1.12.2.3.md) also handles a critter's pending
  fluid meal when a colony reloads.
  [1.13.0](docs/playtest-1.13.0.md) adds [face forces](docs/face-forces.md),
  the public interface another mod's fans push air through (built for
  `../oni_fan_mod`), shows a tile's liquid whenever it holds any (user
  decision 2026-11-23, [the shown phase](docs/colony-owner.md#the-shown-phase))
  and begins the speed work on the November 22 colony
  (`local/perf/nov22-session`), whose findings and open levers its notes list.
  Its packages (`7b521cb`) are staged on the private share as
  `publish/update-1.13.0` and `releases/1.13.0`; every release step has
  passed ([what ran](docs/playtest-1.13.0.md#staged-package)) and the upload
  waits for the user's go-ahead.
  [1.13.1](docs/playtest-1.13.1.md) is 1.13.0 with less work around each
  tick and identical results. The
  [speed study of 2026-11-23](docs/speed-study-2026-11-23.md) is where to
  continue the speed work: it has the measured cost of each part of the
  tick on the November 22 colony, what every accuracy setting buys and
  costs, the two routes to an estimated 3× on that colony, the ideas that
  did not work, and the next structural step (cutting large open regions
  into pieces solved side by side).
  [0.1.8](docs/playtest-0.1.8.md) lets
  two gases change places through an opening one tile high;
  [0.1.7](docs/playtest-0.1.7.md) kept a pool levelling past the activity
  pause and said when evaporation below boiling is on;
  [0.1.6](docs/playtest-0.1.6.md) (withdrawn) let liquid fall through a
  one-tile opening onto trapped gas, kept germs with their phase and stopped
  interface sharpening lifting mass;
  [0.1.5](docs/playtest-0.1.5.md) damped liquid motion after the user's
  recording of 0.1.4;
  [0.1.4](docs/playtest-0.1.4.md) gave oxygen, hydrogen and chlorine their
  molecular masses;
  [0.1.3](docs/playtest-0.1.3.md) carried out the project review of
  2026-11-10/11 and [0.1.2](docs/playtest-0.1.2.md) answered
  [the 0.1.1 playtest review](docs/playtest-0.1.1-review.md).
  The `docs/handoff-codex-*.md` files are historical; continue on `main`.
- [docs/workshop-upload.md](docs/workshop-upload.md): the Steam Workshop item,
  what it serves (`python3 tools/workshop.py status`: 1.12.2.3, `9c7bda6`, on
  2026-11-23) and the upload procedure. The tool only reads unless it is given
  `--execute` with the user's go-ahead for that version.
- [docs/playtest-plan.md](docs/playtest-plan.md): the original first-playtest
  plan (the full base game on one asteroid, playable on Linux).
- [docs/completion-plan.md](docs/completion-plan.md): the accepted plan. Its
  **Amendments** list the user's decisions; they override older text anywhere.
- [docs/playtesting-readiness.md](docs/playtesting-readiness.md): gate status and
  measurements, kept as a record.
- [native/MAC_SOLVER.md](native/MAC_SOLVER.md): the solver, its settings and the
  links to every `native/MAC_*.md` topic. [native/MAC_KERNEL.md](native/MAC_KERNEL.md)
  covers the production kernel, which the game runs;
  [native/MAC_GATES.md](native/MAC_GATES.md) the physics gates.
- [docs/implementation-status.md](docs/implementation-status.md): what is
  implemented and verified, in game and standalone.

## Rules (user decisions)

- Work on `main` only; fetch and rebase before pushing, commit with explicit
  pathspecs, and push promptly. End commit messages with the attribution your
  tool requires.
- `MacClosedWorld` is the solver. ONI's native fluid simulation is what the mod
  replaces and is never used as the solver. The GPU route is closed
  ([why](docs/gpu-acceleration-research.md)).
- Gate inputs, grids and acceptance bounds are fixed; only a user decision changes
  them. Never "fix" a failure by changing them.
- Claim a new checkpoint version only right after a fetch, and push it promptly
  ([MAC_CHECKPOINT.md](native/MAC_CHECKPOINT.md#version-assignment)).
- The licensed game stays private: never commit game files, decompiled code,
  saves or native/memory captures. Gameplay screenshots and short clips for the
  README are allowed (user decision 2026-10-25). Never type Steam credentials or
  bypass Steam's checks; if sign-in is needed, ask the user. No public release or
  Workshop upload without the user's authorization, given for that version
  ([procedure](docs/workshop-upload.md#uploading-a-version)).
- Linux and Windows x64 (user decision 2026-10-25: the private playtest runs on
  the user's Windows PC). Keep evidence light
  ([playtest plan](docs/playtest-plan.md#evidence)).
- Describe scope and steps, not calendar estimates.

## Build and verify

```sh
cmake -S . -B build-production -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-production -j 8
ctest --test-dir build-production -j 8          # about 90 tests, a few minutes
python3 tools/build.py                           # full native + managed + Python suites
```

- Speed of large colonies without the game: `tools/run_locked.sh high python3
  tools/colony_speed.py --build build-production` runs generated late-game
  colonies and the private captures in `local/perf/`
  ([what it measures](docs/performance-acceptance.md#game-free-speed-check-2026-11-16)).
- For patch feedback on a pushed revision, run `python3 tools/acceptance_suite.py
  run HEAD --tier patch --keep-going`. This covers the standalone suites, fixed
  colony comparisons and three live games. Use `--scenario NAME` to include the
  original batches for affected gameplay. [Scope, commands and timing](docs/performance-acceptance.md#patch-regression).
- Long gates run on request: `ma_mac_multiphase_gates M5 --run`,
  `ma_mac_free_surface_gates M2`/`M3` (full grids), `ma_mac_riemann_tests`.
- For any solver change, state in the commit message whether solver arithmetic or
  checkpoint bytes change. Compare every `ma_mac_*` test and diagnostic output
  between the old and new revision from clean builds: a reused build folder with
  files from `git archive` can keep stale objects, so touch the sources first.
- The in-game harness is `tools/oni_headless.py` (`status`, `start`, `run`,
  `screenshot`): it runs the licensed Linux game on a private virtual desktop. Its
  Steam home is `~/.local/share/oni-headless`; do not move it. Run each game
  through `tools/game_slot.sh low|high|whole CMD` and timed work through
  `tools/run_locked.sh`. Their locks and the prepared private saves live in the
  ignored `local/` folder (`local/locks`, `local/saves`); do not delete it.

## Workspace hygiene

Build folders (`build*/`), `artifacts/` and `local/` are ignored. Delete one-off
build folders when you are done, keep scratch work outside the repository, and do
not leave stashes or uncommitted work behind. All project work lives in this
folder; do not create clones, worktrees or work folders elsewhere.

The leftovers of the development phases were removed on 2026-11-19, after the
Workshop release. What is kept on purpose:

- `build-production` (Linux) and `build-windows` (MinGW cross build); the tool
  installs `build-proton-official10`, `build-wine`, `build-oni-cross-tools` and
  `build-powershell`; `build-linux-ubuntu2204-4bdfceb`, `-f51be6a` and
  `-ff95138`, the native libraries of the 0.1.8, 1.10.2 and 1.11.0 packages.
- `artifacts/acceptance/4bdfceb/packages` and `artifacts/release-4bdfceb` (the
  published build), `artifacts/steam-media` (the Workshop page's pictures).
- `artifacts/release-ff95138` and `artifacts/acceptance/ff95138` (the staged
  1.11.0 package and its runs) and `artifacts/compat-1.11.0` (its evidence;
  `hazard` holds the solver runs of `a2c9193`, `final` those of `ff95138`).
- `artifacts/release-cb39fa5`, `artifacts/acceptance/cb39fa5`,
  `artifacts/v1.12.0` and `build-linux-ubuntu2204-v112` hold the 1.12.0
  packages, patch runs, playtest evidence and Linux compatibility build.
- `artifacts/release-08faa66`, `artifacts/acceptance/08faa66`,
  `artifacts/v1.12.1` and `build-linux-ubuntu2204-08faa66` hold the
  1.12.1 packages, full release and patch runs, and native comparisons.
- `artifacts/release-7b521cb`, `artifacts/acceptance/7b521cb`,
  `artifacts/v1.13.0` and `build-linux-ubuntu2204-7b521cb` hold the 1.13.0
  packages, runs, replay measurements and the review of the November 22
  recording.
- `local/locks`, `local/saves` (prepared and playtest saves), `local/perf`
  (private replay captures), `local/perf-tools` (the sampling profiler and
  capture helpers of the speed work) and `local/retired-2026-11-19` (uncommitted
  diffs of the removed worktrees and the few archived commits that never reached
  `main`).
- `archive/`: the handoff notes, the agents' board and the helper scripts of the
  first phases (excluded through `.git/info/exclude`; `archive/MANIFEST.txt`
  lists where they came from). The archived clones themselves are gone: every
  commit of theirs is on `main` or in `local/retired-2026-11-19`.
