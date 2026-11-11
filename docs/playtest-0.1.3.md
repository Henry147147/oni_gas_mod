# Private playtest 0.1.3, 2026-11-11

Version 0.1.3 carries out the project review of 2026-11-10/11 (speed, bugs
and playability; requested by the user with "implement on your best
judgement"). It keeps everything in [0.1.2](playtest-0.1.2.md). It is a
private candidate; no public release or Workshop upload was made.

## Changes

### Speed

- **Spaced Out! full colony, the cost 0.1.2 added.** The rule that re-enters
  an overfilled liquid cell into the pressure solve also entered thin-gas
  cells holding a mist droplet, with the droplet's compressibility (almost
  none). The vented base of the Spaced Out! full colony then re-solved a
  nearly incompressible system in 30 % of its intervals. A cell now re-enters
  only when liquid can overfill it
  ([game kernel](../native/MAC_GAME_KERNEL.md#flooded-cells-and-boiling-profile-12-2026-11-09)).
  Bisected on the full-colony replay (owner tick, twelve workers): `40a1465`
  158 ms, 0.1.1 170-177, `08ce0fe` 516 (444 flow intervals a tick), `8578ddf`
  250, 0.1.2 228, this candidate 172.
- **Large connected regions.** A pressure system that is one connected
  component of at least 2,000 cells uses the production kernel's multigrid
  solve, checked against the same residual bound
  ([game kernel](../native/MAC_GAME_KERNEL.md#large-connected-regions-profile-12-2026-11-11)):
  134 to 15 iterations a solve on the 32-duplicant industrial base, replay
  tick 78 to 63 ms.
- **Solver workers.** Idle workers yielded for 2 ms after every parallel
  section; a third of the solver's CPU time was idle workers. They now yield
  for 200 µs. Same tick time and bytes, 27 % less CPU time on the replays,
  which leaves cores to the game's own threads
  ([kernel](../native/MAC_KERNEL.md)).
- **Transfers.** A pipe port's sub-ulp rest no longer fails the frame's
  transfer batch, and such rests gather per source, cell and gas instead of
  one entry a frame (36,000 entries after one cycle of the industrial save
  before; 148 added in 1,500 ticks now)
  ([owner](colony-owner.md)).
- **Game thread.** A paused game no longer rebuilds the actor census or
  re-uploads the five map textures every frame, and every publication reuses
  the previous snapshot's buffers instead of allocating 47 blocks; the
  allocation-free texture path now serves any number of visible cells (it
  stopped at 4,093, after which each visible cell built objects every tick);
  the development-only storage guards no longer box their arguments on 21
  `Storage` methods in every game; the reading pool is bounded by
  publications and emptied at the end of a session; per-tick lists and
  strings are reused ([owner](colony-owner.md),
  [rendering](native-fluid-rendering.md)).

### Fixes

- **Fault path.** The previous game-data snapshot was freed before a
  publication had finished; a failure after that left the game reading freed
  memory. A publication is now built as a candidate and handed over as its
  last step.
- **Freezing.** A native frame that froze more than 4,096 constituents kept
  the rest of the liquid and displaced it beside the new ice. Every frozen
  cell's liquid now leaves.
- **Native capture.** A failed capture of ONI's own cell changes was
  swallowed; it now refuses the next frame.
- **Polluted oxygen.** Duplicants breathed it while the hover card called the
  tile unsafe; every reader now counts it as oxygen.
- **A stopped simulation on the Forest asteroid.** With 0.1.2 a new colony
  there stopped at owner frame 14: something drew most of a cell's hydrogen
  beside a trace droplet, and the gas source energy rule cooled the rest to
  0.43 K, outside the liquid's range. A draw now leaves what stays no colder
  than twice its liquids' floor
  ([source energy](../native/MAC_GAME_KERNEL.md#source-energy)). Found by
  the acceptance run of this candidate.
- **The fault scenario.** `fault-containment`, which crashed the game with
  0.1.1 and 0.1.2, passes.

### Play

- **Gas pressure overlay** runs from vacuum to 500 kPa, with normal base air
  (about 250 kPa) mid-scale; it used to saturate at 200 kPa.
- **Oxygen margin overlay** (was "Oxygen partial pressure") shows how far a
  tile is above the breathing minimums: red cannot be breathed, yellow
  barely, teal comfortably (four times the minimums). ONI's own Oxygen
  overlay shades breathable air by the same margin.
- **Toxic gas.** Chlorine, sour gas and nuclear fallout together at 5 % of a
  tile's gas by moles or more make it unbreathable (**Advanced → Breathing
  thresholds → Maximum toxic gas share**; 100 % turns it off). One rule now
  drives the breathing decision, hover card, suffocation tooltip and
  overlays.
- **CO₂ limits**, still off by default, use 2 % and 5 kPa when switched on
  (0.5 % and 500 Pa were reached in about a minute in normal base air). The
  Relaxed breathing preset no longer switches them on.
- **Start dialog** leads with the oxylite reserve and lists what plays
  differently; **Settings → Gameplay → How it plays** shows the list again.
- **Readings.** Breathing warnings use kPa and percent at the chosen
  precision.
- **Settings.** The development-region controls only show where they can do
  something.
- **Slowdown.** While the game runs more than 10 % below the chosen speed for
  a few seconds, the mod's top-bar button shows the actual speed.

Colony settings records move to version 5 (versions 3 and 4 load, taking the
defaults for what they lack). Profile 12 arithmetic changes (the room rule
and the multigrid solve); profiles 2-11, the checkpoint format and version 32
are unchanged.

## Verification

Before the push, on the integrated tree:

- Linux: `tools/build.py --oni-native-observer` passes (93/93 native tests,
  516 Python tests with 11 platform skips, every managed suite including 450
  settings checks and 49 presentation tests; game contract 2,829
  dependencies, 0 differences, 14/14 synthetic changes refused).
- Every `ma_mac_*` invocation against a clean build of `b75d712`: 62 of 62
  pass on both; 59 outputs identical, the production oracle differs in
  timing fields only, `mac_game_kernel` adds the new control's line, and
  `mac_phase_change` changes its profile-12 boiling line (6.83 to 6.22 kg
  boiled in a second, peak 213.7 to 204.6 kPa; bounds more than 2.6 kg and
  below 500 kPa).
- Windows: both DLLs cross-build with MinGW; `ma_mac_game_kernel_tests` and
  `ma_oni_colony_tests` pass as Windows executables under Wine, the kernel
  test's output identical to the Linux build's.
- Replays, twelve workers: owner bytes equal at 1, 3 and 12 workers; ledger
  maxima unchanged ([measurements](performance-acceptance.md#review-measurements-and-013-2026-11-11)).

In-game results are recorded in [the acceptance section below](#acceptance).

## Open

Found by this candidate's acceptance run:

- **Germs above pools (from the liquid stability change of 2026-11-10).** The
  `storage-ledger` check "the emptied gas brings its germs once" fails: the
  arena's air rows gain 394,670 germs where the bottle emptier held 80,000
  (its mass is exact; the builds of 2026-11-09 gained about 75,000). The
  delivered 0.1.2 package fails it identically, and a build of this candidate
  with sharpening along the surface normal only (`b75d712`) switched off
  passes it (74,695). So that rule lets germs of the pools' liquid reach the
  air rows; the sharpening's own passive bookkeeping is proportional to the
  mass it moves, and the mechanism is not located. Not reverted: the rule is
  the fix for the liquid churn of the 2026-11-09 recording.

Found by the review and not changed here:

- A mist droplet still moves a thin-gas front by about 1.5 % (it changes the
  intervals the transport takes); the cause is not located.
- The core does not round a batch of transfer edits like the same edits one
  at a time (a few bytes of state), so results depend on how edits are
  batched. Conservation is exact either way.
- Plants, food storage and critter diets still see each tile's main gas by
  mass, while breathing and the hover card use mole shares.
- ONI's table gives oxygen 16 g/mol and hydrogen 1 g/mol, so normal air reads
  about 250 kPa. Using molecular masses would halve the readings; it changes
  solver inputs and every pressure-based check, and is the user's decision.
- The pressure solve's long-double dot products (about 10 % of solver time)
  and the IC(0) sweeps were measured but not changed.
- Older saves keep the held entries they already have; only new rests gather.
- Not seen in game by anyone yet: the two rescaled overlays' colours, the
  slowdown sign on the top-bar button and the "How it plays" button.

## Acceptance

Code revision **`d9dd945`** (clean manifest). Windows ZIP:
`/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.3-d9dd945/MixedAtmospheres-private-windows-0.1.3-d9dd945.zip`,
SHA256 `00b7a08498f65bb60c2acc1ee8c857f12883ca3a0eddbd06e679d33faa81bce6`.
Close ONI, extract and run `install_windows.ps1`; a new colony is
recommended. The earlier folder `MixedAtmospheres-0.1.3-c990ad7/` is
superseded (it has the Forest fault).

The quick tier of `tools/acceptance_suite.py` ran in full on `c990ad7`
(46 steps, tag `quick-v1`) and its failures were then fixed or attributed and
re-run on `d9dd945` (12 steps):

| Step | `c990ad7` | `d9dd945` |
| --- | --- | --- |
| Linux build, 93 native tests; Windows (MinGW) build, 94 tests under Wine | pass | pass |
| Linux batches 1-12, 14 (`fault-containment`), 15 | pass | 4, 7, 11 re-run: pass |
| Linux batch 13 | `storage-ledger` fails one check of 14 (see Open) | not re-run |
| Nine starting asteroids, 3,000 frames each | 8 pass; Forest faults at frame 14 | Forest passes |
| One cycle on the frozen prepared save | pass | |
| Proton batches 3, 4, 6, 7, 10, 11, 12 | pass | 7 re-run: pass |
| Proton batch 2 | all six scenarios pass; the suite still required five | pass |
| Proton contact, base source, prepared cycle, autosave cold | pass | |
| Proton Spaced Out! source, cold load, menu reload | pass | |
| Proton settings source and load | the stage waited for a log line 0.1.1 changed | pass |
| Proton ordinary Spaced Out! new game, save, quit, reload | the stage clicked the old dialog's button | pass |

The four suite repairs (the scenario count of batch 2, the settings log
line, the start dialog's button) change no scenario input or bound. The
Proton steps are the WindowsPlayer under Proton, not Windows itself. Their
screenshots show the start dialog and the settings screen (the toxic gas
row, the new CO₂ defaults) laid out correctly.

Also in game, Linux: `liquid-settling`, `flood-pocket`, `flood-co2-corner`,
`airflow` and `freeze-melt` pass all 17 checks with the pool figures of
0.1.2 (late column variation 70.89 kg under the 80 kg bound, flood peak
135.6 kPa, no liquid CO₂, 70 kg of corner CO₂ gone within 20 s);
`starting-air`, `playtest-diffusers`, `playtest-pump-vent` and `breathing`
pass 25 checks.

Attribution runs with the delivered 0.1.2 Linux package (`b75d712`): it
faults on the Forest asteroid at frame 14 exactly as `c990ad7` did, and it
fails the same `storage-ledger` check with 393,797 germs.

Speed in game (Linux harness, GPU display, one run each in one session; one
cycle of 3,000 frames at speed 3 loaded from the frozen saves):

| Save | 0.1.2 `b75d712` | 0.1.3 `d9dd945` |
| --- | ---: | ---: |
| Industrial base, 32 duplicants: game seconds per real second | 1.730 | 2.189 |
| ... warmed windows: owner / flow ms a tick | 87.5 / 65.7 | 61.5 / 49.6 |
| Spaced Out! full colony: game seconds per real second | 0.875 | 1.021 |
| ... warmed windows: owner / flow ms a tick | 201.3 / 177.6 | 170.6 / 150.0 |

0.1.2 was below the 1× beta bar on the Spaced Out! full colony; 0.1.3 is
back above it (1.06× in warmed windows). These are not Windows figures. The
game-thread changes are not separately measured: frames a second on this
640×480 harness display fall as ticks a second rise.

Not established here: Windows itself, the user's own saves, and a human
look at the overlays and the slowdown sign.
