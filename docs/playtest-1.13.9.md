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
| The same, base game content: 352 buildings | 336 built, 15 base-game rocket modules not placed by this version's form of the test, 1 taller than the room |
| The same with the base game's rocket modules (test change after this version, [below](#after-this-version)) | 351 of 352 built; the ice sculpture melts (the baseline), the command capsule is taller than the room |
| Rocket pass under ONI's own simulation | 45 of 45 built: 8 engines, 23 other modules, 14 interior buildings; nothing logged |
| Critters, plants and geysers under ONI's own simulation | 135 of 135 spawned (78 critters, 27 plants, 30 geysers); 7 log or leave (the baseline) |
| Every building under this version's owner, Spaced Out! content, four parts | 384 built (101, 96, 100, 87), nothing beyond the baseline, no fault, ledgers closed (at most 2.1e-16 mass, 7.4e-16 energy) |
| Rocket pass under this version's owner | 45 of 45 built, nothing logged, no fault, ledgers closed |
| Critters, plants and geysers under this version's owner, three parts | 135 of 135 spawned, nothing beyond the baseline, no fault, ledgers closed |
| The actor count on the user's Cycle 130 colony | 42 consumers, 18 emitters and 1 building emitter in the game; 42 and 19 owned; no mismatch |
| A second model's review of the two fixes | the Sanishell fix would have cleaned twice in a colony of the legacy germ mode, and under-applied an interval shorter than a tick: both corrected before this version was cut |
| `tools/build.py` suites; Python suites | pass; 605 tests |

The passes under this version's owner with the final form of the tests, and
the package's builds and acceptance run, are recorded under
[Staged package](#staged-package).

## After this version

Tests only; the package is unchanged.

The base game's own rocket modules (15 buildings, in a colony without Spaced
Out!) were placed by no pass when this version was cut. The room pass now
builds them as a player stacks them: an engine alone on the floor, any other
module on a steam engine, which is removed after it. Spaced Out!'s modules
stay with `every-rocket-building`.

| Check, game 764025, the package of this version with the changed test | Result |
| --- | --- |
| Base game content under ONI's own simulation, four parts | 351 of 352 built; nothing logged beyond the ice sculpture's baseline entry |
| Base game content under this version's owner, four parts | 351 of 352 built (87, 88, 88, 88), nothing beyond the baseline, no fault, ledgers closed (at most 1.2e-15 mass, 2.2e-16 energy) |
| Spaced Out! content under this version's owner, four parts | 384 built (101, 96, 100, 87) and 45 left to the rocket pass, as before the change; nothing beyond the baseline, no fault, ledgers closed |

The base game's command capsule (`CrewCapsule`, a building the game no longer
offers) is taller than the room and stays recorded as skipped.

## Staged package

`6458df3` is staged on the private share as `publish/update-1.13.9` and
`releases/1.13.9` since 2026-11-25 19:17 UTC, and was uploaded to the
Workshop at 20:02 UTC with `tools/workshop.py`
([the procedure](workshop-upload.md#uploading-a-version)), at the user's
request of that day.

| Step, on `6458df3`, before the upload | Result |
| --- | --- |
| Release builds (Linux for Ubuntu 22.04, Windows), package for game builds 744825, 763796 and 764025, archive verification | passed |
| Five of the user's share saves (Cycles 33, 44, 70, 128, 130) on the release folder with the seven other mods, Linux game 764025: load, 300 ticks at speed 3, save | 5 of 5 |
| The mod off (stand-in mod), the main menu resumes the Cycle 128 save, release folder | refused before loading |
| The Cycle 130 save with the seven mods, Windows game 764025 under Proton, release folder | loads, runs and saves; the runner's verdict is a pass under its 1e-8 bound |
| Workshop dry run | passed; nothing was sent |
| Load matrix (the three game builds, Linux and Windows under Proton) | 6 of 6 |
| Patch tier with the batches of `building-emitter` and of `message-edges,germ-cleaner` | 8 of 8 |

After the upload, on the same revision:

| Step | Result |
| --- | --- |
| The user's other ten share saves (Cycles 49, 55, 121 to 127, 129) on the release folder with the seven other mods, Linux game 764025 | 10 of 10 load, run 300 ticks at speed 3 and save; with the five before the upload, each of the fifteen saves has loaded on this version |
| The base-game building parts under the owner, with the base game's rocket modules | passed ([above](#after-this-version)) |
| The whole release tier (with its 13 new games, `linux-resume-refused` and the steps [1.13.8 did not run](playtest-1.13.8.md#staged-package)), 2026-11-25 21:16 to 2026-11-26 06:14 UTC | 97 of 97 steps passed, none skipped: among them `linux-one-cycle`, `linux-resume-refused` (the autosave of the running game build, refused with the mod off), `linux-ten-cycle`, the every-building, rocket and critter games and all Windows steps under Proton |
