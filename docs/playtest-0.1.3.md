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

## Open, found by the review and not changed here

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
- The `fault-containment` scenario still needs a look in game.
- Older saves keep the held entries they already have; only new rests gather.

## Acceptance

Pending.
