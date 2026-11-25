# Mixed Atmospheres 1.13.5

1.13.5 continues [1.13.4](playtest-1.13.4.md), which was staged and then held
back by the user on 2026-11-24: nothing of this round is released until every
error found in it is fixed. It carries the user's decisions of that day
(germ counts need not be exact; the gas map's colours may be computed from
bands of about 5 %), the result of looking into the tick's publication, and
repairs to the test harness and its lifecycle fixture.

## The tick frame, from 1.13.3 to 1.13.5

The November 22 colony in the Linux game on the test host (Ryzen 9 5900X,
RTX 5090, game speed 2, about seven ticks a second), each build in the same
harness. The 1.13.3 and 1.13.4 columns are the runs of the
[1.13.4 notes](playtest-1.13.4.md); the 1.13.5 column is the mean of three
runs that evening, which agree within 3 %. A run of 1.13.4 between them
gave 51.4 ms and 38 frames a second, so the two sessions compare.
Milliseconds on the game thread per tick, from 60 s of samples:

| Part | 1.13.3 | 1.13.4 | 1.13.5 |
| --- | ---: | ---: | ---: |
| Actor census | 21.9 | 2.3 | 2.3 |
| Pipes: germ steps | 19.0 | 15.2 | 3.5 |
| Pipes: heat | 10.7 | 5.6 | 5.9 |
| Pipes: other patches | 1.7 | 1.9 | 1.9 |
| Publication of the completed tick | 13.1 | 13.6 | 9.3 |
| Space exposure | 2.9 | 1.8 | 1.9 |
| Gas map colours and hover, spread over the frames | 8.1 | 8.8 | 5.7 |
| **All of the mod's code** | **79.3** | **51.1** | **32.4** |
| The mod's share of the game thread's time | 56 % | 38 % | 23 % |

| Frames, median of each run's 15 s windows | 1.13.3 | 1.13.4 | 1.13.5 |
| --- | ---: | ---: | ---: |
| Frames a second | 26 | 37 | 46 |
| Tick frame, median / 95th percentile | 82.5 / 108.4 ms | 55.5 / 69.3 ms | 44.0 / 63.7 ms |
| The mod's time in a tick frame, median | 58.3 ms | 32.2 ms | 20.2 ms |
| Frames between ticks, median | 16.7 ms | 16.5 ms | 15.6 ms |
| Frames over 100 ms per window | 8 | 1 | 1 |

The frame rows count every window of each run. The 1.13.4 notes gave them
for a selection of windows that is not recorded, a few milliseconds higher
for both builds; the rows above are the ones to compare. The test host's
game thread is slower than the user's and the harness window is small: the
ratios carry over better than the milliseconds.

Measuring 1.13.4 or older needs `MA_COLONY_REPORT_SECONDS=30` (the last
argument of `fpsrun.sh`): their harness writes its report every frame, which
tripled this colony's frame time in two runs here before it was noticed.
1.13.5 spaces the report itself.

## What changed

- **Germ counts are one number** (user decision). A culture whose count is
  not whole followed its expected population in exact arithmetic, and the
  count grew by a word with every step: 17 to 19 binary64 words a culture
  on this colony, precise to 1e-320 of a germ, at 80 µs a step. A step's
  exact result is now rounded to the nearest double and the rounding is
  inside the step's booked change, so the birth and death accounts still
  close exactly ([the rule](mixed-pipes.md)). Germ counts differ from
  1.13.4's from the 16th digit; whole-number cultures keep the game's own
  integer rule. An older save's long counts are rounded at their first step.
- **The gas map keeps a cell's colour inside a band** (user decision). A
  cell's appearance is computed again only when its reading has left the
  band of the reading it was computed from: 2 points of a gas's share,
  2.5 % of the pressure and the gas mass, 2 % of the cell for volumes
  ([the bands](native-fluid-rendering.md#gas-colour-bands)). On this colony
  about 49 of 50 visible cells keep theirs after a tick. Hover, status
  panel, breathing and sensors read exact values as before.
- **Publication** ([what was found](#the-ticks-publication)): the overlay
  pass runs in parts on up to four threads, the radiation field is a plain
  array, and the presentation's readings are copied beside the rest.

## The tick's publication

When the solver has finished a tick, the game thread hands its result to
the game inside ONI's own data request. On this colony that took 13.6 ms a
tick, in six parts (milliseconds a tick on the game thread, 1.13.4 and
1.13.5):

| Part | Before | Now | What it is |
| --- | ---: | ---: | --- |
| Overlay pass | 3.1 | 1.6 | One walk over all 257,000 views of the cluster's rectangle, writing the owned cells into the game's data. Now in parts on up to four threads, with the same writes, events and order. |
| Copy of the game's data | 2.8 | 3.4 | ONI's 47 data channels (13 MB) are copied so the owner's cells can be written over them. Unchanged; it now shares the memory with the readings' copy running beside it. |
| Receipts | 2.5 | 2.4 | Machine and pipe-port settlements (exact accounts) and the debris of phase changes (budgeted at 2 ms). Unchanged. |
| Presentation readings | 2.0 | 0 | A copy of the tick's readings for the hover, the map and the status panel. Now made on a worker beside the other parts (2.6 ms there). |
| Views | 1.6 | 1.6 | A copy of the tick's cell views from the owner. Unchanged. |
| Radiation field | 1.5 | 0.2 | The game's radiation at the owned cells, for the germs it kills. The owner built a hash map of it every tick; it is an array now. |
| **Publication** | **13.6** | **9.3** | |

The three copies that remain are each one pass over memory the size of the
map. Not making them would need the game thread to read the owner's memory
in place (a lease on a published frame through the native interface) and
ONI's data to be overlaid without a copy; both change the interface between
the mod's two halves and are not part of this version.

## What remains in a tick frame

Of the 32 ms a tick that the mod still spends on the game thread of this
colony: the publication 9.3 (above), the pipes' heat 5.9 (every filled pipe
exchanges heat with its tile each tick, in exact arithmetic), the gas map's
colours and the hover 5.7 (now mostly the texture uploads and the cells
that did change), the germ steps 3.5, the census 2.3, the space exposure
check and the other pipe patches 1.9 each. ONI's own code and the engine
take 104 ms of the game thread per tick beside it, and collections of the
managed heap (most of whose allocation is the game's) still make the one
frame over 100 ms in each 15 s.

## Test harness repairs

- **Windows settings steps** (`windows-settings-source`, failed once each in
  the release runs of 1.13.0 and 1.13.3). The tab check compares the fills
  of the unselected tabs, and the pointer was still where the previous
  game's last click had left it: on the Performance tab, in its hover fill.
  The pointer is parked on the window's title before each tab screenshot.
- **Windows ordinary UI step** (`windows-spacedout-ui`, failed in the
  release runs of 1.12.1 and 1.13.0). The stage took the colony's first
  save as proof that the game's new-colony message was gone; the save can
  come first, and every later click then lands on the message. The stage
  now looks at the screen for the message's Begin button and dismisses it.
- **The owner report** is spaced by its own cost by default (a fiftieth of
  the run's time at most), so an ordinary harness game of a large colony is
  no longer slowed by it.
- **The lifecycle fixture and the activity control** (`activity-lifecycle`,
  `MA_COLONY_ACTIVITY_CONTROL=1`; 4 of 16 checks failed and the control
  stopped the owner at its third frame, with 1.13.3's code as well). Both
  had read the game-side view of the owner at the wrong moment since
  overlapped pacing (2026-11-06), under which that view is two frames
  behind the owner: the control compared the view's activity mask with the
  census of the owner's latest frame, and two stages of the fixture ran
  before their subject existed there. The product was right throughout.
  The control now compares the census that the view's own frame committed;
  the stages wait for their subject; no check or bound changed
  ([the account](region-activity-controls.md)). The fixture passes 16 of 16
  without a control, with the activity control (31 mask checks) and with
  the census control, and the control runs 300 ticks of the November 22
  colony (3.9 million cells compared) without a fault.
- **The radiation scenario** (`spaced-out-radiation`, 9 of 10 with 1.13.4's
  code). Its kill-rate check paired a germ count with the radiation read in
  the same frame. Under overlapped pacing the owner kills with the field of
  the frame before, and the lamp's radiation wavers from tick to tick. The
  check takes the nearer of the two; its bound of 6 germs is unchanged.
- **Scenarios in no acceptance tier.** 61 of the 132 in-game scenarios,
  every Spaced Out! one among them, were run by no tier, which is how the
  two above went stale unnoticed. All 61 were played with 1.13.5
  ([the sweep](#the-sweep-of-the-untiered-scenarios)); three more checks
  were wrong, none of them a fault of the mod:
  - `spaced-out-reactor-meltdown`, *the site loses no mass after the comets
    land* (failed in 2 runs of 3, one of them 1.13.3's package). A corium tile
    beside open gas sublimates into nuclear waste at the game's efficiency of
    one half, in the owner's natural sublimation (2026-11-09) as in ONI's;
    the check predates it. The tile is now counted at the half it leaves;
    the bound of 1e-6 is unchanged.
  - `spaced-out-hep`, *the colliding bolts' payload arrived as fallout*
    (with 1.13.3's package as well). The payload condenses to nuclear waste and,
    at the arena's 300 K, freezes into loose solid waste, which the check
    did not count. Bounds unchanged.
  - `dlc-fluid-readers`, the aquatic habitat check, required its cell (two
    fifths liquid) to be shown as gas; since the shown-phase decision of
    2026-11-23 a resting liquid is shown at any volume. It asks for the
    physical liquid fraction and the caller's threshold whatever is shown.
- **The release tier runs them now.** 60 of the 61 in 35 more Linux games
  (`LINUX_EXTRA_BATCHES` in `tools/acceptance_suite.py`), and a test fails
  when a scenario is in no list
  ([the tier](performance-acceptance.md#patch-regression)).

### The sweep of the untiered scenarios

`artifacts/v1.13.5/sweep` (private) holds the script, each game's log and
report. `spaced-out-radiation` had its runs before the sweep (10 of 10 after
its repair). Of the other 60, 43 passed as first run, `activity-lifecycle`
after its repair among them. Of the 17 that did not:

| Scenarios | Why | Now |
| --- | --- | --- |
| `spaced-out-reactor-meltdown`, `spaced-out-hep`, `dlc-fluid-readers` | The checks above | Corrected; pass (the meltdown 3 runs of 3, one of them with a tile sublimating in the last samples) |
| `spaced-out-sublimation-station`, `-centrifuge`, `-sludge-press`, `-vacuum-pocket`, `-intakes`, `-dehydrator` | Run after others in one game: a Spaced Out! start asteroid has room for the arenas of about three scenarios, and the driver refuses an arena that would leave it | Pass in shorter games |
| `spaced-out-rocket-abandon` | Run without the rocket's port step, which fills the cabin with the four fluids the check asks for | Passes 8 of 8 after `spaced-out-rocket`, `-rocket-ports`, `-rocket-flight` |
| `starting-air` | Run fourth in its game: it compares the pod's air with the generated air | Passes first in its game |
| `mixed-pipe-save-guard` | Asks for the refusal that exists only with pipe saves off (`MA_COLONY_PIPE_SAVES=0`) | Passes 7 of 7 with it |
| `owned-storage-active-outlet`, `-drained-save`, `-retired-save` | Save controls of the exact storage writer, which is development-only (user decision 2026-11-04) and needs its switch | Pass with `MA_COLONY_NATIVE_SCHEDULE_CONTROL=1` |
| `pipe-body-rounding` | Reads the native body-heat observer's trace (`MA_ONI_BODY_HEAT_TRACE`) | Passes 5 of 5 with it |
| `mixed-storage-heat` | A strict diagnostic of the exact storage writer; it exceeds its own bound by the 0.703 J the user accepted on 2026-10-26 and needs the Proton runner's paired heat traces | Not run by any tier; recorded as such in `UNTIERED_SCENARIOS` |

## Checks

The radiation field's hand-over is the one native change: a dense array
where the owner built a hash map. Solver arithmetic and checkpoint bytes
are those of 1.13.3. Germ counts change from their 16th digit (the user's
decision) and with them the saved pipe and storage images of cultures that
are not whole numbers.

| Check | Result |
| --- | --- |
| Native tests, production build | 96 of 96 |
| Fixed colony comparisons (`colony_speed.py --regression`) | 12 of 12 fingerprints equal to 1.13.4's |
| `python3 tools/build.py` | passed; 600 Python tests, 93 managed/native integration tests |
| Pipe and storage tests against their independent oracles, with the rounded germ step (one correctly rounded word, per-culture birth and death accounts closing, 10,000 storage cases) | 50 of 50 suites |
| Gas colour bands (`CompactTextureTests`): kept inside each band, computed again outside it; the exact path still equals the full computation in 1,887,839 checks | 9 of 9 |
| Overlay pass in parts against the original loop: the same writes, events and order (`OwnedSnapshotTests`) | 249 of 249 |
| Census tests | 611 checks |
| In the game: every scenario of the release tier's new batches | 60 of 60 scenarios in 35 games ([the sweep](#the-sweep-of-the-untiered-scenarios)); `spaced-out-radiation` 10 of 10, the meltdown 3 runs of 3 |
| In the game: `activity-lifecycle` without a control, with the activity control and with the census control | 16 of 16 each |
| In the game: the November 22 colony, 2,400 ticks at speed 2, three times | no fault; the ledgers' largest relative errors stay those the save arrived with (mass 2.8e-12, energy 5.4e-14, as with 1.13.4) |

The package's own acceptance run is recorded under
[Staged package](#staged-package).

## Staged package

The user uploaded this package (`993c3c3`) to the Workshop on 2026-11-24 at
23:01 UTC, nine minutes after it was staged; `tools/workshop.py status` on
2026-11-25 finds every file as the manifest pins it. Its release chain was
then still running and was stopped on 2026-11-25 at 00:50 UTC at the user's
request, so that [1.13.6](playtest-1.13.6.md) could be built and the
remaining steps run on that version.

| Step, on `993c3c3` | Result |
| --- | --- |
| Linux (Ubuntu 22.04 compatibility), Linux and Windows builds; package; archives | passed |
| Workshop dry run | passed |
| Load matrix: the frozen older save on 744825, 763796 and 764025, Linux and Proton | 6 of 6 |
| Patch tier | 6 of 6 |
| Release tier, 82 steps | the first 18 passed (both builds, `linux-b1` to `linux-b15`, `linux-SandstoneDefault`); `linux-Oceania` was running when the chain was stopped; the other 63 never ran on this version |
