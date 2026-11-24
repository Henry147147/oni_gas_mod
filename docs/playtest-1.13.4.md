# Mixed Atmospheres 1.13.4

1.13.4 is [1.13.3](playtest-1.13.3.md) with less work on the game's own
thread in the frame of each simulation tick. It answers the user's report of
2026-11-24: the frame rate was a little slow in the latest playthrough while
the solver had time to spare.

The solver is untouched: its arithmetic, the save format and every result are
those of 1.13.3. Every change is in the C# mod, and each one computes what
it computed before.

## What slowed the frames

A simulation tick's work lands in one Unity frame: ONI's own 200 ms updates,
and the mod's part around them. On a large colony that frame is several
times as long as the frames between ticks, five times a second at normal
speed and ten at fast speed. The solver's speed does not show it: the solver
runs on its own threads.

The user's session of 2026-11-22 (1.12.2.3 on the Ryzen 9 9950X, frame log
on, 437 windows of 15 s) already shows it. 1.13.0 to 1.13.3 changed none of
the code concerned.

| Session of 2026-11-22, per 15 s window (median of the windows) | |
| --- | ---: |
| Frame time, median | 12.5 ms (80 frames a second) |
| Frame time, 95th percentile | 57.7 ms |
| Frames over 50 ms | 102, about one for every tick |
| The mod's timed part of those frames | 36 ms |
| Collections of the 2.6 GB managed heap | one in two windows, 110 to 150 ms each |

The frame log timed the census and the publication. It did not time the
pipes, whose bookkeeping runs inside ONI's own pipe updates. A sampling
profile of the game thread found them
([how to take one](#profiling-the-game-thread)).

## Where a tick frame's time went, and goes now

The November 22 colony (9,146 actors, 87,615 owned cells), loaded in the
Linux game on the test host (Ryzen 9 5900X, RTX 5090, game speed 2, about
seven ticks a second), 1.13.3's mod code against 1.13.4's in the same
harness. Milliseconds on the game thread per tick, from 60 s of samples each:

| Part | 1.13.3 | 1.13.4 |
| --- | ---: | ---: |
| Actor census | 21.9 | 2.3 |
| Pipes: germ steps | 19.0 | 15.2 |
| Pipes: heat | 10.7 | 5.6 |
| Pipes: other patches | 1.7 | 1.9 |
| Publication of the completed tick | 13.1 | 13.6 |
| Space exposure (the check; its waits are below) | 2.9 | 1.8 |
| Gas map colours and hover, spread over the frames | 8.1 | 8.8 |
| **All of the mod's code** | **79.3** | **51.1** |
| The mod's share of the game thread's time | 56 % | 38 % |

| Frames, median of the 15 s windows | 1.13.3 | 1.13.4 |
| --- | ---: | ---: |
| Frames a second | 26 | 37 |
| Tick frame, median / 95th percentile | 87.8 / 119.7 ms | 57.6 / 73.1 ms |
| The mod's time in a tick frame, median | 61.2 ms | 34.2 ms |
| Frames between ticks, median | 16.9 ms | 16.7 ms |
| Frames over 100 ms per window | 21 | 1 |
| Longest hold of the game thread by a space exposure update | 90.6 ms | 3.2 ms |

(The frame rows above come from a selection of each run's windows that was
not recorded. Over every window of the same runs they are 82.5 / 108.4 ms
and 55.5 / 69.3 ms for a tick frame, 58.3 and 32.2 ms for the mod's part,
8 and 1 frames over 100 ms: the [1.13.5 notes](playtest-1.13.5.md) use
those.)

The test host's game thread is slower than the user's, and the harness
window is small, so the ratios carry over better than the milliseconds. On
the session of 2026-11-22 the same parts were about 20 ms (census) and 13 ms
(publication) of the 36 ms the log timed. The game's speed in this harness
run is the same before and after (1.38 game seconds a second at speed 2):
the frames do not set it there.

## What changed

- **The actor census reads only the actors that can have changed.** It read
  the position and footprint of all 9,146 actors every tick; 8,533 of them
  are fixed buildings (every built tile is one). It now reads the mobile
  ones, those spawned since the last census and 128 fixed ones in turn:
  about 680 a tick ([how](colony-owner.md#main-thread-smoothness)).
- **The pipes no longer recompute what a tick left unchanged.** Heat
  exchange gives every filled pipe a new inventory each tick; its
  constituents' total, heat capacity and energy offset are now kept, and
  amounts already in canonical form are not normalized again.
- **A change of the space-exposed cells no longer waits for the solver on
  the game thread.** It is handed over when the solver is idle, at the
  latest five ticks later.
- **The frame log shows the whole of it.** It times the pipes, reports the
  tick frames on their own, and counts the pipes' germ steps and the length
  of their exact counts ([the setting](settings.md)).

## What remains in a tick frame

In the order of their cost on that colony:

1. **The pipes' germ steps.** Every germ culture in a pipe is stepped each
   tick in exact arithmetic, and an exact count grows longer with every
   step: on this colony about 190 cultures are stepped a tick, their counts
   are 17 to 19 binary64 words long (21 at most, which reaches down to the
   smallest number a double can hold), and a step costs about 80 µs.
   Rounding a culture's count to a fixed length would cut this several
   times over. It changes the pipes' germ counts in their last digits and
   the saved pipe images, so it is a decision for the user and is not part
   of 1.13.4. Reading and writing the words of these sums through pointers
   was tried and made no measurable difference; the cost is the arithmetic.
2. **Publication.** Five copies of the completed tick (the game's data, the
   overlay of the owned cells, views, readings, receipts) are made on the
   game thread, 1 to 3 ms each on a colony of this size. Preparing them on a
   worker when the solver finishes needs a hand-over the game thread only
   swaps; it is a piece of work of its own.
3. **ONI's own tick**, which the unmodified game has as well.
4. **The gas map's colours.** After each tick the mixture's colour is
   computed again for every visible cell (about 9 ms a tick in the small
   harness window, on the game's job threads and the game thread). It grows
   with the area in view, so a large window zoomed out pays more. Keeping a
   cell's colour while its reading is unchanged would save the cells of
   resting regions.
5. **Collections of the managed heap** (110 to 150 ms every 30 s or so in
   the session log). Most of the allocation is the game's; the mod's share
   per tick is the receipts and the lazily built cells.

## Profiling the game thread

`local/perf-tools/mainthread` (private, not in the repository's history)
holds a sampler for the Linux game: `libmonosampler.so`, loaded with
`LD_PRELOAD`, signals the game's main thread every millisecond and records
its managed call stack through Mono's own async-safe walker, while a trigger
file exists. `report.py` gives inclusive and exclusive shares, the mod's
entry points and a call tree; `tick.py` gives the table above. The harness
maps the home folder, so the library and its files must be under `/tmp`.
`artifacts/v1.13.3/fps/fpsrun.sh` is the run that produced these numbers.

## Test harness defects found on the way

They affected measurements, not players.

- The validation driver made six searches of every object of the scene
  once a second, for menu screens, during the whole run, and rewrote its
  report every frame. Together they cost a large colony most of each
  frame: the harness played this colony at 5 frames a second. Both are
  fixed (`2757ce5`); frame rates and game speeds measured in earlier harness
  runs of large colonies were limited by the driver.
- The private `activity-lifecycle` fixture and its control no longer pass,
  with 1.13.3's code as well: the fixture fails 4 of its 16 checks (paused
  refreshes of the actor mask and the staged frame), and
  `MA_COLONY_ACTIVITY_CONTROL=1` stops the owner at the third frame, there
  and on this colony (the same cell and frame with either code). No
  acceptance tier runs them, and they are not repaired here
  ([their state](region-activity-controls.md)). The census comparison of
  1.13.4 has its own switch, `MA_COLONY_CENSUS_CONTROL=1`.

## Checks

Solver arithmetic and checkpoint bytes are those of 1.13.3: no native source
changed.

| Check | Result |
| --- | --- |
| Engine-free census tests, with a partial census against a complete one over 400 random rounds | 611 checks |
| Pipe tests against their independent oracles | 50 of 50 suites: 887,097 amount results and 98,338 comparisons bit for bit, 9,900 inventory temperatures |
| `python3 tools/build.py` | passed; 593 Python tests, 93 managed/native integration tests |
| In the game, every census compared with a complete one (`MA_COLONY_CENSUS_CONTROL=1`): the November 22 colony, 1,500 ticks | 1,503 of 1,503 |
| The same in five gameplay batches (terrain to doors; breathing to germ exposure; germs, critters and plants; skimmer to filter; the mixed-pipe colony batch) | 39,819 of 39,819, and their 27 scenarios pass |
| The November 22 colony, 3,000 ticks at speed 2 with each build of this work | no fault; mass and energy ledgers closed |

The package's own acceptance run is recorded under
[Staged package](#staged-package) once it has run.

## Staged package

To be recorded.
