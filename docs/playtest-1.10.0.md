# Version 1.10.0: the first speed fixes after the Workshop release

The user asked, after [the first session on the published build](playtest-2026-11-19.md),
for the speed fixes to begin: the prototype for the slow surface region
tested and shipped, separate worlds simulated on their own threads, all of it
tested extensively and released as version 1.10.0 (the user's number). It
includes the load fix of 0.1.9. Saves of 0.1.8 and 0.1.9 load unchanged.

## What changed

1. **A liquid surface under thin gas no longer falls freely** (solver
   arithmetic of profile 12, [kernel notes](../native/MAC_GAME_KERNEL.md#a-liquid-surface-under-thin-gas-profile-12-2026-11-19)).
   The top face of a partly filled liquid cell whose own gas cannot carry it
   and with next to nothing above it (a puddle on a surface open to space,
   whose vapour ONI deletes every tick) fell at a terminal 34 m/s and cut its
   whole region into 21 flow intervals a tick. Such a face is now closed
   against a trace of at most 1 mg and otherwise takes only the weight of what
   is above it. A pool under a room's air, a pool in vacuum that its own
   vapour holds, and liquid falling onto a surface are unchanged bit for bit.
2. **Every expensive region gets workers of its own**
   ([kernel notes](../native/MAC_GAME_KERNEL.md#bounded-regional-worker-scheduling)).
   Regions, and so the asteroids of a cluster, already ran side by side, one
   to a worker. Only one region could have additional workers, and only when
   it cost twice the next one, so the session's two expensive regions ran on
   one worker each beside ten idle ones; a region with no measured cost yet
   (after any dig or door) also switched the rule off for everyone, so in a
   real colony it almost never applied. Now the worker budget, less one worker
   for all cheap regions, is divided evenly among the regions with at least
   20 ms of work, up to four workers each. The user's sketch was the budget
   divided by the number of worlds with a ceiling of 16; four is the measured
   ceiling, because most of a region's interval is serial (limits of two and
   of six were no faster than four on the session's colony).
3. **A thread waiting for a slow region sleeps.** The caller of a parallel
   run used to yield in a loop until its slowest block ended, a whole regional
   interval; it now sleeps after 200 µs like an idle worker
   ([pool](../native/MAC_KERNEL.md#determinism)). Beside a busy game this
   gives the core back to the thread being waited for.

Neither 2 nor 3 changes any result: which thread runs a region, with how many
workers, never changes a byte.

## Measured

Game-free replays of the session's final save (`published-nov19`, 150 ticks,
20 of them warm-up, on six reserved cores of the Ryzen 9 5900X test host;
milliseconds a tick, published 0.1.9 against 1.10.0):

| Workers, other load | Owner tick | Flow stage | Regional run | Base region | Surface region |
| --- | ---: | ---: | ---: | ---: | ---: |
| 12, none | 153.5 → 111.4 | 131.2 → 89.6 | 95.1 → 56.0 | 76.1 → 51.5 | 93.6 → 36.9 |
| 6, none | 168.3 → 120.4 | 144.1 → 98.7 | 103.0 → 63.2 | 81.5 → 58.1 | 100.0 → 38.7 |
| 12, two busy threads | 155.9 → 115.2 | 133.3 → 93.2 | 96.8 → 58.7 | 76.8 → 54.6 | 95.0 → 38.6 |
| 6, two busy threads | 159.9 → 123.7 | 137.5 → 101.9 | 96.7 → 65.9 | 76.9 → 60.7 | 93.6 → 40.8 |

The worker sets alone (without the surface fix) take the owner tick from
158.8 to 139.1 ms with 12 workers and hold their gain beside two and four
busy threads; the surface fix alone takes the surface region from 21.4 to
7.7 flow intervals a tick.

In the game, on the same save in the Windows player under Proton on the
test host (900 ticks with 3× selected, each version twice in turn, the five
half-minute windows after the load): the owner tick falls from 177.8 and
176.8 ms to 140.1 and 141.0 ms, and the game advances 0.89 game seconds a
second instead of 0.77 (0.80 to 0.92 against 0.71 to 0.79). The user's PC
ran the same colony about 1.4 times faster than this host.

What is left of the tick is mostly outside the regions: about 34 ms of the
flow stage (conversions between the world and the kernel's arrays, audits,
merging) and 22 ms of the other stages (transfers, heat, ledger and
projection) run on one thread whatever the worker count. That is the next
target; a sampling profile puts it in whole-grid copies and allocations made
every tick.

## What a player may notice

- On the session's second asteroid the brine row that the falling face had
  pinned in place moves again: brine runs to the row's open edge and into the
  shaft below, where a pocket at −24 °C freezes it (about 0.5 kg a second of
  brine ice in the replay, 290 kg in two minutes against 12 kg before). It
  had stood as a step of 1,195 kg beside 4 kg for the whole of the earlier
  replay. The water pool and the puddle keep their masses and temperatures.
- A colony saved by 0.1.8 or 0.1.9 takes about 20 ticks after loading for
  the momentum stored on such faces to clear.

## Verification

- Native tests: 94 of 94 on Linux, 95 of 95 for Windows under Wine (with the
  allocator test of ended threads). New controls: the surface under thin gas
  (fails on 0.1.9: "fell freely"); the planner for budgets 3 to 40; three
  expensive regions beside a cheap one against the single-worker bytes for
  3, 4, 5, 7, 8, 12 and 16 workers, warm, cold, with a region dropped or
  probed, with half the regions asleep, after a failed interval, and over 40
  intervals with the host's timing deciding; a stress of the sleeping pool
  wait and of nested pools.
- ThreadSanitizer: the kernel tests, the block executor tests and 40 ticks of
  the session's replay with three regions on their own workers: no report.
- Scheduling changes nothing: with the scheduler and pool change alone, six
  captures and six generated colonies end on the published binary's final
  hash at 1, 3, 4, 5, 8, 12, 16 and 24 workers (1, 4, 12 and 16 for the
  generated ones). With the surface fix as well, the session's replay ends on
  one hash for 1, 4, 6, 12 and 16 workers over 600 ticks, the hash of the fix
  alone.
- Solver outputs, 0.1.9 against the surface fix, clean builds: 60 of 62
  registered `ma_mac_*` commands identical; `mac_game_kernel` gains the new
  control's line; `mac_production_kernel_oracle` differs in timing fields
  only.
- Long gates, both revisions: `ma_mac_free_surface_gates M2` and `M3` (full
  grids) and `ma_mac_multiphase_gates M5 --run` pass with identical output.
  `ma_mac_riemann_tests` fails identically on both, as since 2026-11-16
  (strong-flow control 1 at N128, density refinement ratio 0.860 against
  0.85): not touched.
- Replays with the surface fix: the session's colony 609 → 561 flow intervals
  a tick; the 32-duplicant industrial base has one such face (a 48-cell
  region, 4.5 → 2.1); the 14-duplicant Spaced Out! colony 52.0 → 50.1 in its
  worst region; the two captures of 2026-11-17 and all generated colonies
  are bit for bit unchanged. No fault; mass and energy errors unchanged.
- In the game: the four runs above restore the session's save, release its
  360 stray cells and run 900 ticks with relative mass and energy errors of
  9e-13 and 2e-14 and no fault, on both versions.
- The quick acceptance tier runs on the release commit; its result is added
  here when it ends.

Not tested: Windows itself.
