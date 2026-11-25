# Mixed Atmospheres 1.13.8

1.13.8 is [1.13.7](playtest-1.13.7.md) (which was staged and never uploaded)
with the fix for the save-file reader that 1.13.7's testing had found. The
user asked on 2026-11-25 for that fix, for every save they had put on the
share to be loaded with the mod and with the other mods those saves were
played with, for whatever that found to be fixed in a new version, and for
that version to be uploaded. The solver and the save format are those of
1.13.6, and saves made with any version from 0.1.8 on load.

## The save-file reader

Before the game loads a colony, the mod reads the save file to learn whether
it is a Mixed Atmospheres colony (`ColonySaveReadback.SimSection`, behind
`ColonyOwnerPersistence.IsMixtureSave` and `LoadRefusal`). The reader walks
the file's own structures up to the simulation section, and it accepted one
version of the game's object save, 7.38. Game builds 763796 and 764025 write
7.39, so since 1.10.1 the reader returned nothing for a save written by a
current game:

- **With the mod off** (another mod's patch, a game build it has no support
  for) the guard on the load screen and on the main menu's Resume button did
  not recognise the colony. The game then handed the colony to its own
  simulation, which cannot read it: "SimDLL found bad data", a
  `NullReferenceException` in `SaveLoader.Load` and the game's crash screen,
  which asks the player to switch mods off. The save file itself was never
  changed.
- **With the mod running** the colony loaded, because the load itself reads
  the game's own stream. Only the refusals before loading (an earlier
  material model, an unsupported content pack) were skipped; the load made
  them later.

The reader now accepts object-save 7.38 and every later minor version of 7.
The number was the only thing that stopped it: every structure it walks
(the save's templates, the root record, the mod list, the length of the
simulation section, the header that follows it) is checked against the save
itself, as before, and a save whose layout really differs is still not read.
The complete layout inspection of the development modes
(`SavedColonyLayoutInspector`) keeps reading the objects of 7.38 only.

## The saves on the share

Fifteen different colony saves stood on the share on 2026-11-25 (one of
them twice): the published session of 2026-11-19 (Cycle 33, game build
744825, save version 7.38), the playtests of 2026-11-20 to 2026-11-22
(Cycles 44, 49, 55 and 70) and the autosaves of Cycles 121 to 130, all the
same Spaced Out! colony. Each save lists the mods it was played with. The
latest ten name seven besides Mixed Atmospheres, in this order:

| Order | Mod | Workshop item | Where the test copy came from |
| --- | --- | --- | --- |
| 1 | Bigger Camera Zoom Out | 1717463209 | the harness's Steam folder |
| 2 | Research Queue | 1893887916 | the harness's Steam folder |
| 3 | Super Duplicant | 3018763783 | the harness's Steam folder |
| 4 | Double Sweeper Range | 1732534140 | the harness's Steam folder |
| 5 | No Critter Overcrowding | 3010968005 (the user runs a local copy) | the harness's Steam folder; same assembly version as in the user's log |
| 6 | Mixed Atmospheres | 3842047795 | the package under test |
| 7 | Airlock Door | 2094698134 | the Workshop's public file, the version the saves record |
| 8 | 200/500 kW Wire Capacity | 3843986345 | the Workshop's public file, the version the saves record |

The earlier saves name a subset. Every run below has all seven installed and
enabled in this order, which is how the user's game loads any of the saves
today. The copies stand in `local/other-mods` (ignored, like the saves in
`local/saves/share-2026-11-25`); `tools/oni_headless.py --extra-mod-first`
and `--extra-mod` install them around the package, and
`tools/oni_proton_headless.py` takes the same two options since this
version.

None of the five mods that load before Mixed Atmospheres patches a game
method it uses: with all seven the mod verifies its 2,992 dependencies and
runs ([why that matters](game-update-resilience.md#other-mods)).

## Checks

| Check | Result |
| --- | --- |
| `tools/DistributionTests` on the fifteen saves (`MA_TEST_MIXTURE_SAVES`) | 1.13.7 reads the one 7.38 save; this version reads all fifteen and parses each envelope |
| New unit tests: the reader on synthetic saves of 7.38, 7.39 and 7.40, compressed and not; 7.37, another major version, a KSAV header that disagrees | `OwnedReservoirLayoutTests` 84 groups, `OwnedReservoirSavedInspectionTests` 64 groups |
| Control, 1.13.7 package, Linux game 764025: a stand-in mod keeps the mod off, the main menu resumes the Cycle 128 save | not refused: the game loads it, "SimDLL found bad data", an exception in `SaveLoader.Load`, the crash screen |
| The same, this version | refused before loading, with the notice for a mod conflict; the game stays at the main menu |
| The same, this version, the Cycle 33 save (7.38) | refused before loading as well |
| The fifteen saves, 1.13.7 release package, seven other mods (the first four run: Cycles 127 to 130) | each loads, runs 300 ticks at speed 3 and saves; no exception |
| This version's source, seven other mods, the Cycle 33, 44, 49, 55, 70 and 128 saves | each loads, runs and saves; the Cycle 128 save loads through the load screen with the checks before loading now reading the save (about one second for this 74 MB colony), no refusal, no exception |

The package's builds and acceptance run are recorded under
[Staged package](#staged-package).

## Found while testing: the ledger figure of a long colony

Not a fault of loading. Decided by the user on 2026-11-25, after the upload:
the Windows runner's bound is 1e-8 from then on
([the decision](completion-plan.md)); the runs recorded on this page were
made and judged under the earlier 1e-12, as described here.

The colony owner keeps, for the whole life of a colony, the largest relative
difference it has seen between the mass it holds and the mass its ledger
books (`MaximumRelativeMassError`; the figure is saved with the colony). The
owner stops a colony when a frame exceeds 1e-8. The in-game harnesses ask
more of their own short test colonies: 1e-12. The user's colony stands at
6.6e-14 at Cycle 33, 8.8e-13 at Cycles 44 to 55 and 2.76e-12 from Cycle 70
on: it is 3,600 times below the owner's bound.

The Windows harness applies its 1e-12 to every colony it loads, so it
reports the saves from Cycle 70 on as `failed-colony-owner-evidence`
although the game loaded the colony, ran it and saved it without an error.
The Linux harness does not apply that bound to a loaded colony. The bound
is an acceptance bound and stays as it is; the runs on the Windows build
below are judged by what the bound is not about (the colony restored, 300
ticks, a save, a clean exit, no exception, no fault, the owner's own 1e-8),
and their harness verdict is given beside it.

## Test tools

- `tools/oni_headless.py run --mode menu --resume-save SAVE` makes the main
  menu resume a colony save by itself (the game's `AutoResumeSaveFile`
  preference, which `MainMenu.OnSpawn` honours). The run passes when the mod
  refused the save before the game loaded it. A release folder may run it.
- `tools/GameContractOtherMod` leaves `LoadScreen.DoLoad` unpatched when
  `MA_OTHER_MOD_LEAVES_LOAD=1`. The mod installs its guard only on load
  entries that no other mod has patched, so the stand-in as it was could keep
  the mod off but could not test the guard.
- The release tier has a step for it, `linux-resume-refused`: the autosave
  that `linux-one-cycle` wrote on the running game build is resumed while the
  stand-in keeps the mod off, and must be refused. No test had loaded a save
  with the mod off since the guard was written, which is how the reader could
  stop reading current saves unnoticed.
- `tools/DistributionTests` no longer expects every real save to record game
  build 744825: a player's save records the build that wrote it.

## Staged package

`6dd5a59` is staged on the private share as `publish/update-1.13.8` and
`releases/1.13.8` since 2026-11-25 14:36 UTC, and was uploaded to the
Workshop at 15:21 UTC with `tools/workshop.py`
([the upload](workshop-upload.md#what-has-been-verified)).

| Step, on `6dd5a59`, before the upload | Result |
| --- | --- |
| Release builds (Linux for Ubuntu 22.04, Windows), package for game builds 744825, 763796 and 764025, archive verification | passed |
| The fifteen saves on the release folder with the seven other mods (Linux game 764025; each through the load screen, 300 ticks at speed 3, then a save) | 15 of 15; no exception in any log, no refusal ([below](#the-fifteen-saves)) |
| The mod off (stand-in mod), the main menu resumes the Cycle 128 save and the Cycle 33 save, release folder | 2 of 2 refused before loading |
| The Cycle 130 and Cycle 33 saves with the seven mods, Windows game 764025 under Proton, release folder | both load, run 300 ticks and save without an error; the harness's own verdict for Cycle 130 is `failed-colony-owner-evidence` ([why](#found-while-testing-the-ledger-figure-of-a-long-colony)) |
| Workshop dry run | passed; nothing was sent |
| Load matrix (the three game builds, Linux and Windows under Proton) | 6 of 6 |
| Patch tier | 6 of 6 |

After the upload, on the same revision: the other thirteen saves on the
Windows build, and the release-tier steps that load or save a colony, with
the new `linux-resume-refused`. Their results are recorded here once they
have run.

### The fifteen saves

On the release folder, Linux game 764025, seven other mods. The Cycle 33
colony crosses a day rollover in its 300 ticks, so it also autosaves.

| Save | Restored at owner frame | Owned cells | Saved at frame | Result |
| --- | --- | --- | --- | --- |
| Cycle 33 (published session, 2026-11-19; game build 744825, save 7.38) | 101,524 | 82,192 | 101,778 | passed |
| Cycle 44 (manual save, 2026-11-21) | 133,995 | 84,595 | 134,297 | passed |
| Cycle 49 | 143,777 | 84,809 | 144,079 | passed |
| Cycle 55 | 161,779 | 86,433 | 162,081 | passed |
| Cycle 70 (manual save, 2026-11-23) | 210,819 | 87,827 | 211,121 | passed |
| Cycle 121 | 359,819 | 88,073 | 360,121 | passed |
| Cycle 122 | 362,819 | 88,073 | 363,121 | passed |
| Cycle 123 | 365,818 | 88,075 | 366,120 | passed |
| Cycle 124 | 368,819 | 88,075 | 369,121 | passed |
| Cycle 125 | 371,818 | 88,076 | 372,120 | passed |
| Cycle 126 | 374,819 | 88,076 | 375,121 | passed |
| Cycle 127 | 377,819 | 88,076 | 378,121 | passed |
| Cycle 128 | 380,821 | 88,076 | 381,123 | passed |
| Cycle 129 | 383,820 | 88,069 | 384,122 | passed |
| Cycle 130 | 386,821 | 88,069 | 387,123 | passed |
