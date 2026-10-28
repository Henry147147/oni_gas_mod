# Handoff to Codex, 2026-10-28

Claude's session on Steam readiness and real-time speed stopped here at the
user's request. This page is enough to resume. Read [AGENTS.md](../AGENTS.md)
first, then the 2026-10-28 entries in the
[completion plan amendments](completion-plan.md#amendments) and the rows
"Performance", "Boil-only phase change", "Speed work, 2026-10-28" and "Smooth
input and UI" in [feature completion status](feature-completion-status.md).

## User decisions of 2026-10-28

1. **Real time blocks release.** The private `5d11aea` (profile 5) Windows
   playtest ran at about 0.85× with stuttering mouse, camera and UI, and the
   user found it "very unenjoyable". Before release the game must sustain at
   least 1× with smooth input on the user's PC (Ryzen 9 9950X, 16 cores, RTX
   5070 Ti, Windows). The fixed performance targets of the completion plan
   stay unchanged.
2. **Features may be removed for speed**, but only by the user's choice.
   Measure each option, state what the player sees, and ask.
3. **Chosen for speed:**
   - Momentum-carrying interface sharpening and layering (a defect fix).
   - A 1 mg floor on the net-volume interval bound.
   - Pausing untouched regions until something touches them, with no
     catch-up.
   - Remote regions without actors stepping every 1 s instead of 0.2 s.
4. **Not chosen:** turning off liquid-surface sharpening, softer liquids
   (stiffness 0.01) or a larger trace threshold. Leave them as they are.
5. **Boil-only phase change**, on by default for new colonies (done). A liquid
   vaporises only when it boils at the cell's total gas pressure, and vapour
   below that boiling point condenses, so the air holds no humidity. The user
   then confirmed total pressure for every gas, even though minor gases in
   strongly compressed mixtures condense (Terra's ~19 bar hot hydrogen pocket
   liquefies about 214 kg of chlorine at ~110 °C).

## What is on main

Today's series, oldest first (`16da60c` was main before it):

| Commit | What |
| --- | --- |
| `058683a`, `3f18696` | Decisions and tracker updates |
| `3549185`, `1058d0d` | Checkpoint 25 and the boil-only rule. Settings: Gameplay → Evaporation below boiling, off by default; options struct version 2. Control: `ma_mac_phase_change_tests --boil-only` |
| `d223793`, `2b46613` | Stutter fix 1: lazy owner readings, a 2 ms receipt budget per publication, `MA_FRAME_LOG=1` frame log. The worker default stays at physical cores |
| `a19d6ba`, `23882ca` | Exact speedups: only changed cells' EOS is re-read; the pressure solve's component structure is cached. No change to results or bytes |
| `94c42f6`, `e1615ba`, `ab6eea3`, `eb40d9b` | Profile 7 / checkpoint 26: pressure intervals sized from the measured swept volume. Root-cause notes are in [MAC_GAME_KERNEL.md](../native/MAC_GAME_KERNEL.md#retry-storms-and-speed-profile-7) |
| `f7dd8f3` | **Reserves checkpoint 27 for profile 8** (momentum plus the 1 mg floor). Nothing is implemented on main yet |
| `7d3d09b` | New colonies use profile 7 / core 26 with boil-only (selector 1). The legacy liquid-boundary control is pinned to profile 6, and a new profile-7 airflow-tile control was added |

All 89 CTests passed at each pushed solver commit, and the `ma_mac_*`
outputs were compared with the parent from clean builds.

## Unfinished work, preserved and not committed

Copies of both diffs are in the ignored folder
`/home/henry/projects/oni_gas_mod/artifacts/handoff-2026-10-28/`. The
worktrees themselves were left as they were.

### A. Profile 8: momentum-carrying sharpening and layering, plus the 1 mg floor

- **Location:** `worktrees/perf` at `7d3d09b`, 10 files changed (+315/−58).
  Copy: `perf-profile8-momentum-wip.patch`.
- **Already present:** checkpoint 27 read/write and
  `MacGameProfile::game_kernel_carried_momentum = 8`; the
  `bound_mass_floor` threading in `flow_local.hpp`/`tick.hpp`; a
  `carry_momentum` path in `surface.hpp` and `layering.hpp` (parcels keep the
  velocities of the faces they leave; momentum reaching a closed face becomes
  a wall impulse audited by `momentum_receipt`); managed
  `ColonyCheckpointMetadata` accepting 27.
- **Status:** stopped mid-implementation of the sharpening stage. It has not
  been built or tested.
- **Done when:**
  - momentum closes exactly in the audits;
  - the magma face no longer forms in the fresh Spaced Out! owner replay
    (frames 4–43; it reached about −2,081 m/s on profile 7);
  - the twelve-dig overfill regression replay and the expansion /
    temporal-reference controls still pass;
  - full ctest and the `ma_mac_*` comparison pass;
  - profile 8 is then the new-colony default, with boil-only.
- **Expected effect:** the no-sharpening replay suggests about 23% less frame
  time in fresh Spaced Out!. The 1 mg floor took fresh replay frame 60 from
  0.353/0.821 s to 0.283/0.434 s (mean/p95).

### B. Smoothness, round 2

- **Location:** `worktrees/distribution` at `eb40d9b`, which is behind main,
  so rebase first. 11 files changed plus the untracked
  `tools/ReadingEquivalenceTests/`. Copies: `distribution-smoothness-wip.patch`
  and `distribution-untracked.tgz`.
- **Contents:**
  - fewer allocations in Core readings (`MixtureThermodynamicReading`,
    `FluidMaterialCatalog`), `ColonyLazyReadings` and `ColonyPresentation`;
  - a preference, Advanced → Performance → "Log frame timing", for frame logs
    on the user's Windows PC (documented in `docs/settings.md`);
  - `MA_COLONY_NATIVE_WAIT_LOG=1` phase timing in `native/src/oni_backend.cpp`
    and `oni_colony.cpp`;
  - the equivalence driver, still being written when the agent stopped.
- **Status:** unverified.
- **Goal:** vanilla-like garbage-collection frequency, about one collection
  per 15 s. After `d223793` the base-game harness still showed a ~200 ms
  pause about every 3 s. Readings must stay byte-identical, so prove it with
  the equivalence driver.
- **Then:** scope the main thread's wait in ONI's `PrepareGameData` (p99
  40–50 ms, much worse when the solver is behind) and move the end-of-frame
  projection off the critical path if that can be done within `oni_*.cpp`.

### C. Region activity tiers

- **Status:** not started.
- **Intent:** the user's choices 3c and 3d above.
- **Proposed design:**
  - Classify each solid-separated region every tick, deterministically:
    - **Active (0.2 s steps):** any owner input this tick or within a short
      hold time, or containing a duplicant, critter, building or port.
      Inputs are digs, builds, tile changes, transfers, geyser output,
      building or body heat, debris and merging with an active region.
    - **Remote (1 s steps):** touched since load but idle past the hold time.
    - **Paused:** pristine world-generation regions, and remote regions idle
      past a cooling-off time (about 60 s of game time, tunable).
  - Passive solid-tile conduction alone must not count as activity: the
    Terra input journal showed "heat inputs" in 82–94% of regions.
  - Save the activity state, so a new profile and checkpoint are needed;
    claim the version after a fetch.
  - Make it a colony setting, on by default, with an "Always simulate"
    choice.
  - The tiers must not depend on the camera, worker count or frame rate.
    Conservation stays exact, and a region that wakes resumes from its stored
    state.
- **Scope:** in fresh Spaced Out!, untouched regions hold 87–98% of
  single-thread solver time; freezing them cut those regions' cost 2.6–4.4×,
  and 1 s steps cut their CPU about 44%.

### D. Failing in-game scenarios

Partial results on the package built from `eb40d9b` (Linux harness), from
`worktrees/accuracy/artifacts/bis/`:

| Scenario | `eb40d9b` | Message |
| --- | --- | --- |
| freeze-melt | fail | ice melted into an owned cell; owned water in the arena 356 kg (bound > 380) |
| airflow | fail | air passed the airflow tiles (4.25 -> 4.052 kg) |
| starting-air | pass | — |

- **Packages built:** `16da60c`, `058683a`, `1058d0d`, `d223793` and
  `eb40d9b` (`bis/pkg-*`).
- **Baseline runs at `16da60c`:** killed by the stop, so they have no result.
- **Next steps:** finish the bisect, check whether either failure predates
  today, and check whether boil-only or profile 7 changed what the scenarios
  assume. Never change scenario inputs or bounds.
- **History:** freeze-melt failed earlier too, at 337.9 kg on `dde2a09`; see
  "Freeze/melt handoff" in the tracker.

## Speed: where it stands

The dev machine is a Ryzen 9 5900X; timings are under
`tools/run_locked.sh high` (12 workers) and often contended. Calibration: the
profile 5 fresh Spaced Out! owner frame here, about 0.48 s, matches the
user's 0.85×, so **the user's PC is about 2× this machine**. Real time on
their PC therefore needs an owner frame of about 0.4 s here; aim for
0.2–0.25 s to leave headroom for UI and larger colonies.

**Profile 7, owner frames:**
- fresh Spaced Out!, frames 4–43: about 1.16 s;
- the user's colony with the twelve-dig inputs: 1.49 s (2.94 s on profile 6).

**Physical advance only, same states:**

| State | Profile 6 mean/p95 (s) | Profile 7 mean/p95 (s) |
| --- | --- | --- |
| `writer32` | 1.905 / 2.978 | 1.171 / 1.724 |
| Fresh replay frame 60 | 0.591 / 1.498 | 0.337 / 0.750 |

`writer32` is the fresh Spaced Out! frame-32 state with digs, not the user's
colony.

**Findings:**
- **Workers:** wall time is flat beyond 4–6 workers, because the largest
  active region is the critical path. The 9950X's gain is per-core speed, not
  core count.
- **Where the time goes:** pressure 40–49%, interface sharpening 14–16%,
  phase change 7–14% (it visits most gas cells, because O₂, CO₂, H₂ and Cl₂
  are vapours of liquid pairs), transport 9–28%, audits 6–12%, heat about 2%,
  layering under 1%.
- **Owner overhead:** the owner frame is about 1.35× the physical advance.
  The space-sink step alone costs about 65 ms per frame in fresh Spaced Out!
  (12,913 space cells).

**Exact speedups still open (no decision needed):**
- the space sink;
- owner overhead;
- per-interval transport cost (1–2 ms per interval on 5,000–9,000 cells);
- a multigrid or aggregation preconditioner for large, calm, connected bases
  (270–440 IC(0) iterations; an offline prototype converged in 26–33).

**Options measured and not adopted:**
- flow parts 1, gas heat mixing 0 and phase relaxation 10 s were all slower;
- phase change off triggers retry storms;
- sleeping regions at rest gain little.

**Data:**
- `worktrees/accuracy/artifacts/perf/`: probes, per-region CSVs and owner
  timings;
- `worktrees/perf/artifacts/mac-compare`;
- `artifacts/temperature-*`.

**Private states** (never commit them):
- `local/temperature-release-regression/` (`writer32.owner`,
  `flux-core24-private.sav`, the user's colony);
- `local/windows-temperature-video/` (`flux.owner`, the probe helpers);
- `/mnt/data/share/oni/playtest/flux/` (the user's upload and its
  `config.json`).

## Steam release gaps

Release logistics are not yet done: no licence (the user's choice), no
Workshop authorization, and the README and Steam draft still describe a
private playtest. Engineering gaps, in rough order:

1. **Speed and smoothness:** A–C above plus the exact speedups. Then a
   private Windows package with the frame log for the user's PC
   (`tools/package_windows.py`, delivered to `/mnt/data/share/oni/playtest`;
   ask for Player.log). The user's machine is the acceptance hardware.
2. **Save compatibility after release:** ordinary loading refuses earlier
   R&D profiles today. Once published, every released profile must keep
   loading after mod updates; this needs a policy and controls.
3. **Exact component storage** (bottles, canisters, recipes, nonempty
   demolition) through the normal player path, and **active continuation**
   (the world diverges on the first resumed frame).
4. **Accuracy proofs:** F3/F4 floor; strong-flow collision refinement (0.856
   against 0.85) and separation velocity.
5. **Progression:** representative base-game and Spaced Out! starts reaching
   sustainable life support and the endgame without developer help.
6. **Mixture presentation:** container coverage and real-game
   hover/overlay checks on both operating systems.
7. **Release tests:** the compatibility matrix (base game and Spaced Out! on
   Windows and Linux, plus refusal of the other packs); disturbance
   recovery; memory; 100-cycle and 1,000-cycle long runs (the latter is about
   167 h of game time at 1×, so the user may want to revisit it).
8. **Player-facing gaps:** a minimum-hardware statement, popular-mod
   compatibility, a re-targeting routine for Klei game updates (the mod is
   pinned to build 744825 and fails closed), README and Steam text, and the
   user's own Windows OS check (only Proton is tested here).

## Housekeeping

- **Worktrees:**
  - `perf` holds A;
  - `distribution` holds B;
  - `accuracy` has clean code, with bisect and perf data in its
    `artifacts/`;
  - `progression` is clean at `7d3d09b` (C was not started);
  - `acceptance` holds older uncommitted work from 2026-10-27, not from this
    session. Inspect it before use.
- **Build folders left by the stop:** `worktrees/perf/build-speed` and
  `worktrees/distribution/build-smooth*`. Reuse or delete them.
- **Processes:** every harness run started by this session was stopped.
  `OxygenNotIncluded.exe` PID 832594 (Proton, an archived
  `claude-team/spacedout-cargo` run) has been running for over 31 hours from
  an earlier session. It is probably stale; check before killing it.
- **Shell trap:** `pkill -f PATTERN` matches the shell running it. Kill by
  PID.
- **Checkpoint numbering:** 27 is reserved for profile 8. Region tiers need
  the next free number, claimed after a fetch
  ([MAC_CHECKPOINT.md](../native/MAC_CHECKPOINT.md#version-assignment)).
