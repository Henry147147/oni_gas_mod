# Mixed Atmospheres 1.14.0

1.14.0 is [1.13.9](playtest-1.13.9.md) made to run together with the
Workshop mod Rephysicalized, at the user's request of 2026-11-26: "Please get
my mod working with the other mod: Rephysicalized. Please ensure they can both
run and load at the same time and work well togeather. Please make this
version 1.14.0, give me a test build to test/upload when possible and run
through the whole test suite, fixing issues when found." The solver, its
checkpoint bytes and the save format are those of 1.13.9, and saves made with
it load.

Rephysicalized (Workshop item 3609531831, version 1.0.7 as fetched on
2026-11-26) is a mass-conservation overhaul of the game. It adds six elements
(AshByproduct, CrudByproduct, SiliconRP, PlantFiberRP and two fluids,
MoltenSiliconRP and SiliconGasRP), edits others, gives fuel burners and crops
an oxygen intake of their own and replaces the Polymerizer's pipe intake. Its
RealBreathing option, off by default, also gives critters an oxygen intake.

Measured in the game on Linux, game build 764025, unless noted.

## Before this version

- **Rephysicalized loaded first turned the mod off** at the main menu with the
  "other mods changed game code" notice. 16 game methods the mod uses carry
  its Harmony patches: `ComplexFabricator.CompleteWorkingOrder`,
  `Db.Initialize`, `Deconstructable.OnCompleteWork`,
  `ElementConverter.CanConvertAtAll`,
  `ElementConverter.HasEnoughMassToStartConverting`,
  `GeneratedBuildings.LoadGeneratedBuildings`, `Generator.get_WattageRating`,
  `KMonoBehaviour.OnSpawn`, `KPrefabID.OnSpawn`, `MainMenu.OnSpawn`,
  `OxygenBreather.Sim200ms`, `OxygenBreather.get_ConsumptionRate`,
  `Storage.ConsumeAndGetDisease`, `Storage.OnCleanUp`, `Util.KInstantiate`
  and `Workable.OnCompleteWork`.
- **Loaded after the mod,** the menu passed and a new Spaced Out! colony ran
  600 ticks. The two new fluids join the colony's catalogue (81 materials).
- **A colony saved without Rephysicalized** was refused with Rephysicalized
  on, with a text that blamed the Mixed Atmospheres build.
- **Rephysicalized's oxygen intake** saw only each cell's most massive gas
  (below).
- **Other mods' own pipe intakes got nothing** on the owner's pipes. A mod
  component calling `ConduitFlow.RemoveElement` or `AddElement` itself got
  empty contents or 0 kg, although `GetContents` showed the pipe full. The
  Polymerizer under Rephysicalized kept running only through the game's own
  intake beside Rephysicalized's. In the same window it took 2.05 kg of
  petroleum under the owner against 5.67 kg under ONI's own simulation.

## Changes

- **Another mod's patches are no refusal** (`bfc98cf`). When the only
  differences from the game contract are other mods' Harmony patch marks on
  verified game code, the build is verified and the mod starts, whichever
  order the mod list has. Player.log names each such method and the mods
  that patched it; the main menu shows no notice
  ([how](game-update-resilience.md#other-mods)). The rule holds for every
  mod, not only Rephysicalized: that is the working session's reading of the
  request, recorded in the [plan's amendments](completion-plan.md). Any other
  difference still refuses the build.
- **A colony whose elements changed says what changed** (`968e22b`). The
  refusal compares the colony's saved material records with the game's and
  names what is new, missing and changed. It says that a mod which adds or
  changes elements was usually turned on or off since the save. It says to
  load the colony with the mods it was saved with, or to start a new colony
  ([how](game-update-resilience.md#saves-across-builds)).
- **Rephysicalized's oxygen intake reads the owner's mixture** (`9201b64`).
  Its `DualGasElementConsumer` (every fuel burner, every crop and, with
  RealBreathing, critters) read a cell's shown element and mass. It then
  credited its storage with what it asked for. Under the owner a cell shows
  its most massive gas, so oxygen under carbon dioxide was invisible to it:
  burners stopped and crops wilted in air with oxygen to spare. A cell's mass
  is all its gas, so a cell with less oxygen than its total mass gave the
  storage oxygen that never left the world. While the owner runs, the intake
  now asks each owned cell for the oxidizer gases its composition holds and
  credits the owner's receipt. The search of a suffocating critter for a
  cell with oxygen (RealBreathing) is adapted the same way
  ([how](colony-owner.md#other-mods-rephysicalized)). Version 1.14.0.
- **Other mods' pipe components are served,** in both directions and for
  any mod's own component that calls the game's pipe calls itself. One that
  takes from an owned pipe with `RemoveElement` gets the species the cell
  shows, at most what it asked, and is told exactly what left the pipe. One
  that adds with `AddElement` is accepted up to the cell's room and told
  exactly what entered. Both are booked in the pipes' existing removed and
  supplied accounts, so the pipe save does not change
  ([how](mixed-pipes.md#other-mods-pipe-components)). Rephysicalized's
  Polymerizer intake is one such component.

## A colony cannot gain or lose Rephysicalized

A colony saved without Rephysicalized does not load with it, and a colony
saved with it does not load without it. Both are refused before loading, and
the save is unchanged. Continuing such a colony needs a migration of the
solver's checkpoint to another material model
([checkpoint identity](../native/MAC_CHECKPOINT.md)). That migration is not
built, and it is left to the user as a follow-up decision.

| Colony | Mods at load | Result |
| --- | --- | --- |
| The user's Cycle 130 save, saved without Rephysicalized | Rephysicalized on | refused: "New in this game: Moltten Silicon and Silicon Gas. Changed: Biodiesel, Gunk, Brackene and Liquid Sucrose." Player.log: 2 added, 0 removed, 79 changed, 75 of them only in their place in the element list (Biodiesel: molar mass; Gunk and Liquid Sucrose: byproducts; Brackene: transition targets) |
| A new Spaced Out! colony saved with both mods | Rephysicalized off | refused: "Missing from this game: MoltenSiliconRP and SiliconGasRP. Changed: Biodiesel, Gunk, Brackene and Liquid Sucrose." |
| The same colony | both mods, either order | loads, runs 300 ticks and saves again |

"Moltten" is Rephysicalized's own spelling.

## Left as they are

Found by a read-only survey of Rephysicalized's code, each verified against
the sources. Listed for the user:

- **A battery's water-damage explosion** replaces its cell with the cell's
  shown element at the shown mass. On a mixed cell that removes the other
  gases. It is the same class as the sandbox heat brush in
  [1.13.9](playtest-1.13.9.md#how-they-were-found).
- **Its readers of a cell's shown liquid** (acid burn, corrosive scalding,
  debris dissolving, its environmental preparation) see the most massive
  liquid of a mixed cell. The environmental preparation can also consume
  less than it believes.
- **Its cooking in hot carbon dioxide** (meat, fish, eggs) triggers only
  where carbon dioxide is the most massive gas of the cell.
- **A crop's or burner's low-oxidizer state** can show about one second
  longer when it spawns: the credit arrives with the owner's receipt, a tick
  after the request.
- **An intake request in flight at a save** is withdrawn on load. The gas
  stays in the world.
- **The starting-air estimate** does not count Rephysicalized's oxygen users.

Duplicants breathe at Rephysicalized's changed rate through the game's own
withdrawal, which the owner already runs: nothing to adapt.

## New tests

- **Scenario `rephysicalized`** ([described](colony-owner.md#scenarios)),
  with Rephysicalized loaded first. Four sealed rooms: a Coal Generator in
  air where carbon dioxide outweighs oxygen in every cell, the same generator
  in thin air, a wild Mealwood in the mixed air, and a Polymerizer fed
  petroleum through a pipe. It also runs under ONI's own simulation as the
  reference. `MA_OTHER_MOD_COMPAT=off` leaves Rephysicalized's components
  unadapted for a control run.
- **Release-tier steps** `linux-menu-rephysicalized` and the scenario's
  batch, both with Rephysicalized from `local/other-mods/src/Rephysicalized`.
  `python3 tools/workshop.py fetch-mod 3609531831` puts it there;
  third-party mods stay out of git.
- **The harness runs a release folder at the main menu** with another mod
  loaded first, judged by Player.log. `result.json` lists
  `other_mod_patches`.
- **`linux-resume-refused`** keeps the mod off by another means: its stand-in
  mod now patches two game methods and removes the patches again
  (`MA_OTHER_MOD_REMOVES_PATCHES=1`). Harmony leaves its patch mark on them
  and no patch accounts for it.
- **`game_mono_contract_check.py --other-mod patched|removed`**, both
  controls in the game's own Mono runtime.
- Tools work for 1.15.0, not part of this package: `tools/oni_headless.py run
  --moono PLAYER_DIR` runs the game through Moono.

## Checks

| Check | Result |
| --- | --- |
| Main menu, Rephysicalized first, development package | the mod starts; 16 methods listed with Rephysicalized's patches |
| Main menu, the stand-in mod first | the mod starts; 3 methods listed |
| The stand-in with `MA_OTHER_MOD_REMOVES_PATCHES=1`, the main menu resumes the Cycle 128 save | the mod is off and the save is refused before loading (`passed-resume-refused`) |
| New Spaced Out! colony, Rephysicalized first | starts (64,508 owned cells, 81 materials), runs 600 ticks and saves |
| `rephysicalized`, the owner: oxygen intake in mixed air | carbon dioxide outweighs oxygen in 50 of 50 cells; the room 18.0000007 to 16.7300015 kg oxygen (lost 1.26999924 kg), credited 1.26999924 kg; the burner holds 0.946 kg and burned 10.8 kg of coal |
| The same, thin air | 0.688007 kg asked, 0.589067 kg credited, the room's loss (0.6 to 0.0109326 kg) |
| The same, the crop | buffer 0.2089 kg, not starved, not wilting |
| The same, ledgers | closed (mass 5.3e-16, energy 3.3e-17) |
| `rephysicalized` under ONI's own simulation (layered rooms) | the burner holds 0.946 kg, the room lost 1.282 kg and the burner burned 11.2 kg of coal; thin air 0.6 to 0.022 kg (lost 0.578 kg), stored 0.242 kg; the crop's buffer 0.2178 kg, not starved |
| `rephysicalized` with the pipe fix, `pkg-1140g`: owner (base game) and ONI's own simulation | both pass every check; side by side below |
| Our own pipe scenarios on `pkg-1140g` under the owner | `mixed-pipes`, `mixed-pipe-machines`, `polymer-press` and `pipe-throughput` pass |
| `rephysicalized`, the owner, control with `MA_OTHER_MOD_COMPAT=off` (Rephysicalized's intake unadapted), development package `pkg-1140d` | mixed room: 18.0000007 kg oxygen unchanged (lost 3.6e-15 kg), the burner's oxidizer storage 0 kg, the burner never active, 0 kg of coal burned; the crop's buffer 0 kg, starved and wilting; thin room: lost 0.502193 kg, the storage holds 0.167333 kg (a cell showed 0.0324 kg of gas holding 0.01921 kg of oxygen). The first check fails as a control run should; without the owner-aware ledger the run cannot say how much the thin room's burner was credited and burned |
| Every building, rocket building and living thing under ONI's own simulation, Spaced Out! content, Rephysicalized first: eight games | 8 of 8 passed. Building parts: 101 of 107, 96 of 108, 100 of 107 and 87 of 108 built in the room (384 of 430; the rest are rocket modules and rocket interior buildings, and one building that does not fit the room); rocket pass 45 of 45; critters, plants and geysers 45, 45 and 45 |
| The same eight games under this version's owner | 8 of 8 passed, with the same counts: building parts 101, 96, 100 and 87 built, rocket pass 45 of 45, living things 45, 45 and 45. `artifacts/v1.14.0/compare.py`: 610 subjects compared, 0 differ in outcome, survival or logged signatures. No fault; ledgers closed (at most 2.5e-16 mass, 3.0e-16 energy) |
| Three cycles of a new Spaced Out! colony, Rephysicalized first (9,000 ticks at speed 3, saved) | see [three cycles](#three-cycles) |
| DistributionTests; Python suites | pass |

### The scenario under both simulations

`pkg-1140g`, base game, Rephysicalized first. Under ONI's own simulation a
cell holds one element, so rooms A and C are layered there (carbon dioxide
below, oxygen above) and room B holds oxygen only.

| Part | The owner | ONI's own simulation |
| --- | --- | --- |
| Burner in mixed air | carbon dioxide outweighs oxygen in 50 of 50 cells; the room 18.0000007 to 16.7300015 kg oxygen (lost 1.26999924 kg), credited 1.26999924 kg (1.29298 kg asked); holds 0.946 kg, 0.324 kg burned as oxidizer; 10.8 kg of coal burned | holds 0.946 kg; the room lost 1.282 kg; 11.2 kg of coal burned |
| Burner in thin air | a cell showed 0.0324 kg of gas holding 0.01921 kg of oxygen; the room 0.6 to 0.0109326 kg (lost 0.589067 kg), 0.688007 kg asked, 0.589067 kg credited; stores 0.265066 kg | a cell showed 0.02 kg of oxygen; the room 0.6 to 0.0214662 kg (lost 0.578534 kg); stores 0.242533 kg |
| Mealwood | buffer 0.2089 kg, not starved, not wilting (0.214333 kg credited, 0.008 kg in flight) | buffer 0.2178 kg, not starved, not wilting |
| Polymerizer, 57 conduit ticks | 44.0667 kg of petroleum reached the press (120 to 30 kg in the reservoir, 45.93 kg in the five-segment pipe); it holds 0.866667 kg and made 27 kg of polypropylene (43.2 kg of petroleum); 44.0667 kg against 44.0667 kg taken | 44.0667 kg reached the press; it holds 1.66667 kg and made 26.5 kg of polypropylene (42.4 kg of petroleum); 44.0667 kg against 44.0667 kg taken |
| The press's intake per tick | first intake at frame 34, 1.667 kg in that tick, then 0.8 kg in each; 0.8 kg/s over the second half, what its converter uses; 42.2503 kg through Rephysicalized's own intake and 1.81633 kg through the game's `ConduitConsumer` beside it; 0 removals refused | the same series: first at frame 34, then 0.8 kg a tick; 0.8 kg/s over the second half |
| Ledgers | closed (mass 1.6e-15, energy 6.9e-17) | (ONI's own simulation) |

Both read the press's recipe as built: plastifiable liquid 0.8 kg/s to
polypropylene 0.5, steam 0.15 and carbon dioxide 0.15; its second converter
takes resin (0.8 to polypropylene 0.5, ethanol gas 0.15 and hydrogen 0.15).
The press carries both Rephysicalized's intake and the game's own
`ConduitConsumer`. Before the pipe fix (`pkg-1140e`, a 64-frame window) only
the game's intake was served under the owner: the press took 2.05 kg of
petroleum against 5.67 kg under ONI's own simulation. With the fix both
simulations deliver the same series, and the press runs on its converter's
0.8 kg/s.

**Not yet rerun on `pkg-1140g`** of the pipe scenarios next to the change:
`mixed-pipe-filter`, `-boundary`, `-output`, `-teardown`, `-valves`,
`-gas-vents`, `-pumps`, `-heat-machines`, `-colony`, `-retire`, `-readers`,
`-owned-storage`, `-save-guard`, `mixed-storage-transfer-heat`,
`pipe-schedule`, `pipe-small-heat-save`, `reservoir-mix`, `vent-duty`,
`gas-generator`, `oil-refinery` and the four every-building parts under the
owner. The release tier runs them all.

<a id="three-cycles"></a>
### Three cycles with Rephysicalized

A new Spaced Out! colony with Rephysicalized loaded first, the development
package, 9,000 owner frames at speed 3:

- 64,595 owned cells, 81 materials. Five saves: at the start, three
  autosaves (owner frames 2776, 5776 and 8776) and the end (9002), each
  about 24.05 MB.
- Ledgers closed: mass 2.2e-16, energy 3.8e-16. No fault.
- 1,799.9 game seconds in 606.1 real seconds: 2.97 times real time. Owner
  time per frame 15.7 to 17.7 ms (mean of the 60 performance windows
  16.3 ms). The slowest Unity frame was 2,407 ms, and the slowest owner
  interval about 1.1 s, at the autosaves.
- No exception and no fault line in Player.log. The warnings are the game's
  and Rephysicalized's own at load (unhandled `.psd` assets and a texture in
  its Dev folder, PLib's version check of a non-Steam copy, duplicate and
  missing prefab tags). Mixed Atmospheres logged only its starting-air note
  ("about 0.3 cycles of breathing" for 3 duplicants).

## Staged package

No package of 1.14.0 was staged. The user decided on 2026-11-26 to "just
work on version 1.15.0 and ship when both supported items are confirmed";
the 1.14.0 release chain was stopped during its builds, and 1.14.0 is
neither tested as a release nor uploaded. The development results above
stand as recorded. Everything in 1.14.0 ships in
[1.15.0](playtest-1.15.0.md), with Moono support and the 1.13.10 pipe-save
fix.

