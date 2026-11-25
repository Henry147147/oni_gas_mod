# Mixed Atmospheres 1.13.6

1.13.6 is [1.13.5](playtest-1.13.5.md) with clearer notices for the cases in
which the mod stays off at the main menu (user request, 2026-11-25). A
running colony, the solver and the save format are those of 1.13.5. The
user uploaded 1.13.5 on 2026-11-24 and asked on 2026-11-25 for 1.13.6 to be
built at once and for the release steps 1.13.5 had not yet run to be run on
it. Its upload waits for the user's go-ahead.

## The report

A player with other mods installed, on game build 764025, was shown:

> This version of Mixed Atmospheres does not support this version of Oxygen
> Not Included (build 764025). It checks every part of the game it depends
> on, and 25 have changed: `SaveLoader::LoadHeader(...)`,
> `SaveLoader::Save(...)`, `CustomGameSettings::SetQualitySetting(...)`,
> and 22 more.

and asked for the mod to be updated. Build 764025 is supported, and the
game's code had not changed: other mods had patched those methods before
Mixed Atmospheres loaded. Harmony marks a patched method as not inlinable,
the check read that mark as a change of the method
([how](game-update-resilience.md#other-mods)), and the one text the mod had
for every failed check spoke of an unsupported game version.

## What changed

- **A patch by another mod is recognised.** The check tells a method that
  only carries Harmony's mark from one whose code differs, and reports it as
  `patched by another mod`. The mod still stays off in that case; what the
  player is told is new. Whether it should run beside such mods is an open
  user decision ([the trade-off](game-update-resilience.md#other-mods)).
- **One notice per cause**, each with its own text for a Mixed Atmospheres
  colony that is then not loaded:

  | Cause | The notice says |
  | --- | --- |
  | The game is a build this copy has no adapter for | Which build the game is and which the copy is for; that an update of the mod is needed and Steam installs it (for an older game: update the game) |
  | Other mods patched game methods the mod uses | That game and mod fit together and neither is out of date; the mods, each with the number of methods it patched; to disable them, or to report the conflict |
  | The game's code or `SimDLL` is not that of the supported build it reports | The first three parts by short name (`SaveLoader.LoadHeader`); that the game is not out of date; to disable other mods and verify the game's files, or to report it |
  | The mod's own files do not belong together | To subscribe again or replace a hand-installed copy |
  | The check itself failed | The error; to disable other mods, or to report it |

  Only the first row speaks of an update. Reports go to the Workshop page or
  `github.com/Henry147147/oni_gas_mod/issues`, with Player.log.
- **Names a player can act on.** A conflicting mod is named by its title in
  the game's mod list (found through the assembly that holds the patch, since
  a mod chooses its own Harmony ID); a game method reads `SaveLoader.Save`.
  Player.log keeps the full signatures and the Harmony IDs.
- **A failed start** (the notice "could not start and has removed all of its
  changes") also says that another mod may be the cause and where to report.

## Checks

| Check | Result |
| --- | --- |
| The refusal texts (`tools/DistributionTests`): every cause, one and several mods, the short names, the text for a refused colony | 75 checks |
| The contract's controls (`GameContract controls`, Linux 764025) | unchanged contract verifies; 15 of 15 synthetic changes still refused as changes |
| The game's own Mono, unpatched (`game_mono_contract_check.py`) | 2,989 dependencies, 0 differences |
| The game's own Mono after a stand-in mod patched three used methods (`--other-mod`) | 7 differences, all reported as another mod's patch; the notice counts 3 methods and names the stand-in |
| Harness tests (`tools/tests/test_oni_headless.py`) | 162, including the order of the mod list |
| The mod compiles against 744825, 763796 and 764025 (Linux) and the Windows game | yes |
| In the game (Linux, 764025, menu): the stand-in mod listed before the package | the mod stays off and patches nothing; Player.log names `CustomGameSettings.SetQualitySetting`, `LoadScreen.DoLoad` and `SaveLoader.Save`, each with "Other Mod Stand-in" and its Harmony IDs; the dialog shows the conflict notice with that title and "3 methods". The dialog was read from a screenshot taken before its text was shortened by one sentence |

The package's own builds and acceptance run are recorded under
[Staged package](#staged-package).

## How common the conflict is

The harness's Steam folder holds 18 other Workshop mods. Reading their
Harmony patch attributes (the DLLs were decompiled, not run) and comparing
with the 1,383 game methods in the contract: **10 of the 18 patch at least
one method Mixed Atmospheres uses**, among them `Db.Initialize`,
`MainMenu.OnSpawn`, `Game.DestroyInstances`, `ModsScreen.BuildDisplay`,
`OverlayMenu.InitializeToggles` and `Sublimates.Sim200ms`. Each of these
turns Mixed Atmospheres off when the game loads it first, and none does when
it loads later. The notice now says so plainly; whether the mod should
instead run beside them is the open decision in
[the resilience notes](game-update-resilience.md#other-mods).

## Staged package

The builds, the package for the three game builds, the Workshop dry run, the
load matrix and the patch tier run on this version's revision. Of the
release tier, it runs the 64 steps that
[1.13.5's chain](playtest-1.13.5.md#staged-package) had not completed
(`linux-Oceania` onwards); the 18 that passed on `993c3c3` are not repeated,
since the two versions differ only in what the mod does before it starts.

The user uploaded this version to the Workshop on 2026-11-25 at 03:09 UTC,
while the chain was running (`tools/workshop.py status`: 1.13.6, `d63861d`,
every file as its manifest pins it). On `d63861d` the builds, the package
verification, the Workshop dry run, the load matrix (6 of 6) and the patch
tier (6 of 6) passed, and so did the first 16 of the 64 release-tier steps
(`linux-Oceania` to `linux-Oasis`, `linux-one-cycle`, `linux-x1` to
`linux-x7`). The chain was stopped there on 2026-11-25 at 04:17 UTC, when
[1.13.7](playtest-1.13.7.md) replaced this version: a colony with a
Fertilizer Synthesizer threw an exception on every frame, in this version as
in every earlier one. The other 48 steps run on 1.13.7.
