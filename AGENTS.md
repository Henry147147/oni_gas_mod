# Working on Mixed Atmospheres

Start with [feature completion status](docs/feature-completion-status.md) for open work,
release history and playtest notes. The beta target is sustained 1× on full-colony saves.
The speed goal is 3×. User decisions in the Amendments of the
[completion plan](docs/completion-plan.md) override older guidance.

Mixed Atmospheres replaces Oxygen Not Included’s fluid simulation. Use
`MacClosedWorld` in `native/src/mac_*.cpp` as the solver. ONI’s native sim and
GPU acceleration are excluded. The game backend is in `native/src/oni_*.cpp`,
the C# mod in `managed/`, and build, packaging and harness scripts in `tools/`.
See the [solver guide](native/MAC_SOLVER.md), [production kernel](native/MAC_KERNEL.md)
and [physics gates](native/MAC_GATES.md) for solver work. For gameplay coverage,
see [implementation status](docs/implementation-status.md).

## Project rules

- Work on `main` only. Commit with explicit pathspecs and end each commit message
  with the attribution your tool requires. Fetch and rebase before pushing. Push promptly.
- Claim a checkpoint version only immediately after a fetch and push it promptly.
  Follow the [version assignment procedure](native/MAC_CHECKPOINT.md#version-assignment).
- Only the user may change gate inputs, grids or acceptance bounds. For every solver
  change, compare all `ma_mac_*` test and diagnostic outputs from clean builds of
  both revisions. Touch sources first when reusing a build folder with files from
  `git archive`. State in the commit message whether solver arithmetic or checkpoint bytes change.
- Support Linux and Windows x64. The private playtest runs on the user’s Windows PC.
  Preserve every supported build entry when adding a game build. Follow the [compatibility procedure](docs/game-update-resilience.md#keeping-an-earlier-build-working).
- Keep licensed game files, decompiled code, saves, third-party mods and native or
  memory captures out of git. Gameplay screenshots and short clips for the README
  are allowed. Keep [playtest evidence](docs/playtest-plan.md#evidence) light.
- Never enter Steam credentials or bypass Steam checks. Ask the user if sign-in
  is needed. Public releases and Workshop uploads need user authorization for
  that version. Check the live item with `python3 tools/workshop.py status`.
  Use `--execute` only for an authorized upload. See the [Workshop procedure](docs/workshop-upload.md#uploading-a-version).
- Describe scope and steps. Omit calendar estimates.

## Build and verify

```sh
cmake -S . -B build-production -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-production -j 8
ctest --test-dir build-production -j 8
python3 tools/build.py  # full native, managed and Python suites
```

- For patch feedback on a pushed revision, run
  `python3 tools/acceptance_suite.py run HEAD --tier patch --keep-going`. Add `--scenario NAME` for affected gameplay batches.
  See [patch regression](docs/performance-acceptance.md#patch-regression).
- Measure colony speed on generated colonies and private captures with
  `tools/run_locked.sh high python3 tools/colony_speed.py --build build-production`.
  See the [game-free speed check](docs/performance-acceptance.md#game-free-speed-check-2026-11-16).
- Run long gates on request only. These are `ma_mac_multiphase_gates M5 --run`,
  `ma_mac_free_surface_gates M2`, `ma_mac_free_surface_gates M3` (full grids)
  and `ma_mac_riemann_tests`.
- Run each game separately through `tools/game_slot.sh low|high|whole CMD` and timed work through
  `tools/run_locked.sh`. The Linux harness `tools/oni_headless.py` provides `status`, `start`,
  `run` and `screenshot`. Keep its Steam home at `~/.local/share/oni-headless`.

## Workspace hygiene

Keep all project work in this checkout. Do not create clones, worktrees or
project folders elsewhere. Keep temporary scratch files outside the repository.
Remove one-off build folders when finished and leave no stashes or uncommitted work.

Keep the shared builds (`build-production`, `build-windows`) and installed tools
(`build-proton-official10`, `build-wine`, `build-oni-cross-tools`, `build-powershell`).
In the ignored `build*/` and `artifacts/` directories, keep the libraries, packages
and evidence recorded in the playtest notes, plus `artifacts/steam-media`.
Never delete `local/`. It holds locks, private saves,
replay captures, profiling tools, other mods and retired work. `archive/MANIFEST.txt`
indexes the historical handoff material in `archive/`.
