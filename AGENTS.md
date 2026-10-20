# Working on Mixed Atmospheres

Mixed Atmospheres replaces Oxygen Not Included's fluid simulation with its own
solver. `native/` holds the solver (`MacClosedWorld`, `native/src/mac_*.cpp`) and
the game backend (`native/src/oni_*.cpp`); `managed/` holds the C# mod; `tools/`
holds build, packaging and in-game harness scripts. The docs are large: read the
few that your task touches, starting here.

## Where things stand

- [docs/playtest-plan.md](docs/playtest-plan.md): **the current work**, the full
  base game on one asteroid, playable on Linux. Start here.
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
- The licensed game stays private: never commit game files, decompiled code or
  captures. Never type Steam credentials or bypass Steam's checks; if sign-in is
  needed, ask the user. No public release or Workshop upload without the user's
  authorization.
- Linux only for now; the Windows port comes later. Keep evidence light
  ([playtest plan](docs/playtest-plan.md#evidence)).
- Describe scope and steps, not calendar estimates.

## Build and verify

```sh
cmake -S . -B build-production -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-production -j 8
ctest --test-dir build-production -j 8          # about 75 tests, a few minutes
python3 tools/build.py                           # full native + managed + Python suites
```

- Long gates run on request: `ma_mac_multiphase_gates M5 --run`,
  `ma_mac_free_surface_gates M2`/`M3` (full grids), `ma_mac_riemann_tests`.
- For any solver change, state in the commit message whether solver arithmetic or
  checkpoint bytes change. Compare every `ma_mac_*` test and diagnostic output
  between the old and new revision from clean builds: a reused build folder with
  files from `git archive` can keep stale objects, so touch the sources first.
- The in-game harness is `tools/oni_headless.py` (`status`, `start`, `run`,
  `screenshot`): it runs the licensed Linux game on a private virtual desktop. Its
  state lives in the ignored `local/` folder; do not delete it.

## Workspace hygiene

Build folders (`build*/`), `artifacts/` and `local/` are ignored. Delete one-off
build folders when you are done, keep scratch work outside the repository, and do
not leave stashes or uncommitted work behind.
