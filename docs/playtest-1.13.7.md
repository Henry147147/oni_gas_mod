# Mixed Atmospheres 1.13.7

1.13.7 is [1.13.6](playtest-1.13.6.md) with one fix from the user's session
on it (2026-11-25, the day the user uploaded 1.13.6): a colony with a
Fertilizer Synthesizer, or another building that emits a gas the same way,
threw an exception on every frame. The solver and the save format are those
of 1.13.6, and saves made with it load. The user asked for the save they had
tried to be tested, the fault fixed and the fix released as a new version.

## The report

The user played the Cycle 130 autosave of their Spaced Out! colony (three of
its eight duplicants left), saved, and loaded the Cycle 128 autosave, the last
one with all eight. The game then showed, on every frame:

```
NullReferenceException: Object reference not set to an instance of an object
(wrapper dynamic-method) MonoMod.Utils.DynamicMethodDefinition.Game.SimEveryTick_Patch7(Game,single)
(wrapper dynamic-method) MonoMod.Utils.DynamicMethodDefinition.Game.Update_Patch1(Game)
```

Their Player.log holds it 4,793 times in under a minute. Loading that save in
the Linux game with the 1.13.6 package, in a new process, gave the same
exception 3,632 times in 300 ticks: it comes from the save's contents, not
from loading a second colony in one session.

## The cause

The trace names no origin: Harmony throws an exception that a finalizer hands
on anew, which restarts its trace at the patched method, and the mod has two
finalizers on `Game.SimEveryTick`. A build that writes the first trace to the
log gave:

```
at BuildingElementEmitter.UnsafeUpdate (System.Single dt)
at BuildingElementEmitter.Sim200ms (System.Single dt)
...
at StateMachineUpdater.AdvanceOneSimSubTick ()
```

Four of the game's buildings emit a gas through `BuildingElementEmitter`: the
Fertilizer Synthesizer (natural gas), the Blastshot Maker (carbon dioxide)
and two that later game builds added. Every tick the component reads back
what it emitted, from the emitted-mass table of the game's frame data, at
the index in its emitter handle. The colony owner runs every emitter itself
([how](colony-owner.md#element-consumers-and-emitters)), so ONI's own
simulation runs none in a loaded colony and its table is a null address. The
read then fails, inside the game's loop over its 200 ms components, so the
rest of that loop is skipped as well, on every tick. The building need not be
running: in the user's save none emitted anything in 600 ticks. Having built
one was enough, and no in-game scenario had one.

Looking at the same component showed a second fault. ONI states an emitter's
mass per emission interval. `ElementEmitter` (geysers, plants, most
buildings) uses an interval of one second, so its mass is its rate, which is
how the owner read every emitter. `BuildingElementEmitter` sends a fifth of
its rate every 0.2 s: under the mod a Fertilizer Synthesizer gave off 2 g/s
of natural gas instead of 10.

## What changed

- **The emitted-mass table** ([how it works](colony-owner.md#the-emitted-mass-table)).
  Every publication of the owner's frame carries a record for each emitter
  the owner runs: what it put out since the game's previous tick. The
  building reads the amount it emitted, shows its rate, and adds to the daily
  report as it does in the unmodded game. An emitter that the owner took over
  from ONI keeps its handle, and the owner gives no emitter of its own an
  index that one of those uses.
- **An emitter's rate is its mass over its interval.** Unchanged for every
  emitter with a one-second interval (the arithmetic gives the same bits); a
  Fertilizer Synthesizer and a Blastshot Maker now emit at the game's rate,
  five times what they did under earlier versions.
- **Player.log says where an exception began.** When an exception leaves a
  game method through one of the mod's finalizers, the mod writes its first
  trace to the log (the first four different ones of a session), whether the
  failure is the mod's, the game's or another mod's.

Solver arithmetic and checkpoint bytes are unchanged. The game contract
gains three entries, for what the new scenario reads
(`BuildingElementEmitter.AverageEmitRate`, its handle, `Game.simData`).

## Checks

| Check | Result |
| --- | --- |
| The user's Cycle 128 save, 1.13.6 package (`d63861d`), Linux game 764025, new process | the reported exception, 3,632 times in 300 ticks |
| The same save, this version's source, 300 and 600 ticks at speed 3, then a save | loads and runs; no exception; saved at tick 381,423 |
| New scenario `building-emitter` (a Fertilizer Synthesizer with its inputs stored and powered), control without the table | fails as the report did: 1,855 exceptions, "table a null address", rate 0, and the next scenario of that game (`gas-generator`) fails too |
| The same with the table and the earlier rate | no exception, but 0.022 kg of natural gas in 12.8 s (2 g/s): the check for the gas fails |
| The same, this version | passes: +0.112 kg natural gas, the game's data holds the table for handle `0x7e000001`, the building reads 10 g/s |
| `gas-generator`, `life-support`, `wheezewort`, `geyser` in that game | pass; the geyser's steam +39.6 kg as before |
| `tools/OwnedSnapshotTests` with a status-table case (grows to an index, keeps the copied records, zero elsewhere, created when ONI's is a null address) | 250 of 250 |
| `tools/DistributionTests`; Python suites | 75 checks; 601 tests |
| Game contract against the Linux reference at packaging | 2,992 dependencies, 0 differences |

The package's builds and acceptance run are recorded under
[Staged package](#staged-package).

## Staged package

`1de0c78` is staged on the private share as `publish/update-1.13.7` and
`releases/1.13.7` since 2026-11-25 04:33 UTC. Its upload waits for the
user's go-ahead; the Workshop serves 1.13.6.

| Step, on `1de0c78` | Result |
| --- | --- |
| Release builds (Linux for Ubuntu 22.04, Windows), package for game builds 744825, 763796 and 764025, archive verification | passed |
| The user's Cycle 128 save on the release folder (Linux game 764025, 300 ticks at speed 3, then a save) | loads, runs and saves; no exception |
| Workshop dry run | passed; nothing was sent |
| Load matrix (the three game builds, Linux and Windows under Proton) | 6 of 6 |
| Patch tier | 6 of 6; `linux-offline` on its second run ([why](#the-save-file-reader)) |
| Release tier: the 49 steps that [1.13.6's chain](playtest-1.13.6.md#staged-package) had not completed, with the new `linux-x36` | 48 passed, among them `linux-ten-cycle` and all 19 Windows steps; `linux-x25` failed in the harness ([below](#the-control-step)) |

The first 16 release-tier steps passed on `d63861d` and were not repeated:
the two revisions differ in the emitters' table and rate, which the patch
tier and the 48 steps above run with.

### The control step

`linux-x25` runs `mixed-pipe-save-guard`, a control of the development switch
`MA_COLONY_PIPE_SAVES=0`, which no player's package can set. While the
control holds the pipes it refuses every save, the game's own first save
among them; the game logs that refusal as an exception with a mod frame, and
the harness counted it as a mod error. Run on the 1.13.6 package the step
fails in the same way: it had not run in an acceptance chain since it joined
the release tier in 1.13.5. `tools/oni_headless.py` now takes
`--expected-refusal TEXT`, which excuses a log line holding the text and the
stack of an exception whose header holds it, and nothing else; the batch
names its refusal (`1032e94`, tools only). With it the step passes 7 of 7,
by hand on the `1de0c78` package and as `linux-x25` on `1032e94`.

<a id="the-save-file-reader"></a>
## Found while testing: the save-file reader

Not fixed in this version; it waits for the user's decision.

Before a colony loads, the mod reads the save file to learn whether it is a
Mixed Atmospheres colony (`ColonySaveReadback.SimSection`, behind
`ColonyOwnerPersistence.IsMixtureSave` and `LoadRefusal`). The reader
accepts only the game's object-save version 7.38
(`OwnedReservoirLoadHeaderV1.InspectLayoutHeader`, and the KSAV header that
`SavedBodyPrefixInspector` checks after the sim payload). Game builds 763796
and 764025 write 7.39, so for a save written by them the reader returns
nothing:

- With the mod running, nothing a colony needs depends on it: the load
  itself reads the envelope from the game's own stream, and the user's saves
  load.
- With the mod off (another mod's patch, a game build it has no support
  for), the guard that keeps a Mixed Atmospheres colony out of ONI's own
  simulation (`GameBuildCheck`) does not recognise such a save. The
  refusals before loading (an earlier material model, an unsupported
  content pack) are skipped for it as well, and the in-game load makes them
  later.

This has been so since 1.10.1, the first version for build 763796. It
showed when copies of the user's two autosaves stood in `local/saves`:
`tools/DistributionTests` reads every save there and the patch tier's
offline step failed on them. They now stand in a subfolder, and the step
passed on its second run. Accepting 7.39 in both places lets the checks
before loading run on every current save, so the change needs in-game runs
through the load screen, with the mod on (no colony wrongly refused) and off
(the colony refused).
