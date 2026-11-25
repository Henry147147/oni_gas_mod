# Mixed Atmospheres 1.13.9

1.13.9 is [1.13.8](playtest-1.13.8.md) with the fixes of the bug hunt the
user asked for on 2026-11-25, after the upload of 1.13.8: bring the tests and
harnesses up to date, hunt for bugs by extensive testing with every building
in the game tested, "to not miss another bug like the bug found here" (the
[Fertilizer Synthesizer](playtest-1.13.7.md)), and then release and upload
1.13.9 with every fix. The solver and the save format are those of 1.13.8,
and saves made with it load.

## Fixed

- **A Beeta mines uranium ore again.** A Beeta takes ore from a solid tile
  with a withdrawal of the tile's own element. The owner claimed every
  withdrawal inside its rectangle, so this one never reached ONI: the Beeta
  got nothing and the tile kept its mass. A withdrawal of a solid element is
  now ONI's ([how](colony-owner.md#element-consumers-and-emitters)). In the
  game: ONI takes 10 kg of an 800 kg uranium ore tile; 1.13.8 took 0 kg; this
  version takes 10 kg.
- **A Sanishell cleans its water again.** A cleaning Sanishell removes germs
  through a germ emitter with a negative count, which ONI's own simulation
  runs; the owner holds the world's germs, so ONI found none to remove. The
  owner now applies such an emitter itself, as ONI does
  ([how](colony-owner.md#germ-cleaners)). In the game, 33 s of cleaning: the
  pool goes from 239,988 to 129,399 germs under ONI, stayed at its natural
  die-off (239,976 to 239,525) under 1.13.8, and goes from 239,976 to
  129,390 under this version.

Both have been so in every version of the mod. Solver arithmetic and
checkpoint bytes are unchanged.

## How they were found

Two reviews by a second model (Codex, the user's suggestion), each one
narrow question over the game's decompiled code and the mod's source,
read-only:

1. *Which tables of the game's frame data does game code read that the
   owner's publication could leave null, short or stale?* The Fertilizer
   Synthesizer's table was the only one: the other 76 fields of
   `Sim.GameDataUpdate` are full-size planes, counted lists read up to their
   count, or tables no game code dereferences. Its three remarks on the
   emitted-mass table were followed up: a possible collision of consumer and
   emitter handles at load does not occur (the new
   [count of the game's actors](colony-owner.md#every-building) on the user's
   Cycle 130 colony: 42 consumers and 19 emitters, each owned as what it is,
   no shared handle); one concerns the geothermal vent of content the mod
   does not run with; one needs 16.8 million registrations in a session.
2. *Which messages that game code sends to the simulation are mishandled
   while the owner runs?* A dozen suspicions. Each of the likely ones became
   a check that also runs under ONI's own simulation, so that what ONI does
   is measured and not assumed:

| Suspicion | In the game | Result |
| --- | --- | --- |
| A Beeta's withdrawal from a solid tile never reaches ONI | scenario `message-edges` | **defect, fixed** |
| A Sanishell's negative germ emission removes nothing | scenario `germ-cleaner` | **defect, fixed** |
| The Pitcher Pump's rectangular withdrawal takes from cells outside its rectangle | `message-edges`: 100 kg from the rectangle, 0 kg from a pool two cells to its side | not a defect |
| A dig of the backwall alone opens the tile in front in the owner | `message-edges`: the tile stays solid and closed | not a defect |
| Consumer and emitter handles collide at load | the actor count at the end of every run | not seen |

Left as they are, and listed for the user:

- **A replacement of a mixed cell by its own shown element** (the sandbox
  heat brush) replaces the whole cell: the other gases of that cell are
  removed, booked to the ledger. ONI's cells hold one element, so its tools
  cannot say "keep the rest". Sandbox only.
- **The Vertical Wind Tunnel's gas walls.** While it runs ONI makes the cells
  beside its column impermeable to gas; the owner passes gas there. The
  building works; the air around it mixes with its column.
- **A blocked output that carries germs** loses the germs when the gas waits
  in the displacement reservoir ([known](colony-owner.md#storage-crossings)).

## New tests

- **Every building, rocket module, rocket interior building, critter, plant
  and geyser**, one at a time, against what each logs under ONI's own
  simulation: scenarios `every-building-1` to `-4`, `every-rocket-building`
  and `every-living-1` to `-3` ([how they work](colony-owner.md#every-building)).
  The release tier runs them (the building parts with the base game's
  buildings and with Spaced Out!'s): 13 more games.
- **The actor count** at the end of every harness run: each consumer and
  emitter of the game is owned as what it is; a mismatch fails the run.
- `message-edges` and `germ-cleaner` (above), in the release tier.
- `tools/OwnedReservoirLoadTests` had not compiled since the fault log
  learned to keep a first stack trace (before 1.13.7); no script runs it. It
  builds again and passes 72 of 72 against game build 744825, which it pins.
- The Windows runner accepts a colony's lifetime ledger figures up to 1e-8
  (user decision 2026-11-25, [the plan's amendments](completion-plan.md)).

## Checks

| Check | Result |
| --- | --- |
| `message-edges`, ONI's own simulation (Spaced Out!, game 764025) | 4 of 4: 100 kg from the rectangle and none beside it; 10 kg from the uranium ore tile; the tile behind a backwall dig stays closed |
| The same, 1.13.8 | the solid tile gives 0 kg and keeps its 800 kg; the other checks pass |
| The same, this version | 5 of 5, the tile 800 to 790 kg; `spaced-out-critters` in the same game passes |
| `germ-cleaner`, ONI's own simulation | the Sanishell's pool 239,988 to 129,399 germs (9,216 a cell), the other pool 179,991 to 179,652 |
| The same, 1.13.8 | 239,976 to 239,525: no cleaning |
| The same, this version | 239,976 to 129,390 (9,216 a cell); ledgers closed |
| What ONI's emitter does, cell by cell (a probe scenario, not kept) | once a second the count leaves the emitter's cell and each of its four neighbours; the owner's cells follow the same pattern |
| Every building under ONI's own simulation, Spaced Out! content: 430 buildings | 384 built in the room, 45 left to the rocket pass, 1 taller than the room; 3 log or remove themselves (the baseline) |
| The same, base game content: 352 buildings | 336 built, 15 base-game rocket modules not placed by any pass yet, 1 taller than the room |
| Rocket pass under ONI's own simulation | 45 of 45 built: 8 engines, 23 other modules, 14 interior buildings; nothing logged |
| Critters, plants and geysers under ONI's own simulation | 135 of 135 spawned (78 critters, 27 plants, 30 geysers); 7 log or leave (the baseline) |
| Every building under an earlier form of the test, this version's owner, Spaced Out! parts 1 and 2 (215 buildings) | no building differs from ONI in outcome, log or components; no fault; ledgers closed |
| The actor count on the user's Cycle 130 colony | 42 consumers, 18 emitters and 1 building emitter in the game; 42 and 19 owned; no mismatch |
| A second model's review of the two fixes | the Sanishell fix would have cleaned twice in a colony of the legacy germ mode, and under-applied an interval shorter than a tick: both corrected before this version was cut |
| `tools/build.py` suites; Python suites | pass; 605 tests |

The passes under this version's owner with the final form of the tests, and
the package's builds and acceptance run, are recorded under
[Staged package](#staged-package).

## Not covered yet

The base game's own rocket modules (15 buildings, in a colony without Spaced
Out!) are placed by no pass: they need a base-game rocket stack, which the
room pass does not build. The base game's command capsule is taller than the
room. Both are recorded as such by the scenario and follow after this
version.

## Staged package

The builds, the package for the three game builds, five of the user's share
saves with the seven other mods on the release folder, the mod-off control,
the latest save on the Windows build, the Workshop dry run, the load matrix
and the patch tier with the batches of the two fixes run on this version's
revision before the upload. The passes of the new tests under the owner run
beside them. Results are recorded here once they have run.
