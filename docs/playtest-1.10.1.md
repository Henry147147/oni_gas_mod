# Version 1.10.1: Oxygen Not Included build 763796

Steam updated Oxygen Not Included from build 744825 to 763796 on 2026-11-19.
Every earlier package of the mod, the live 0.1.8 as well as 0.1.9 and
[1.10.0](playtest-1.10.0.md), refuses the new build at the main menu and
leaves the game unmodified ([fail closed](game-update-resilience.md)); a
mixture colony then waits unopened. The user asked for the new build to be
fetched, the mod checked against it and a new version made if needed. 1.10.1
is 1.10.0 retargeted to build 763796: no solver arithmetic and no checkpoint
bytes change, and colonies saved by 0.1.8, 0.1.9 and 1.10.0 on the old game
build load in it.

1.10.1 accepts build 763796 only. A player whose game is still on 744825
keeps an earlier version.

## What the game changed

Build 763796 runs on Unity 6000.3.17f1 (6000.3.5f2 before).

- **The simulator library is the same code.** The Linux `libSimDLL.so` is byte
  for byte the 744825 file and moved from `Plugins/` to `Plugins/x86_64/`.
  The Windows `SimDLL.dll` was rebuilt with a newer compiler servicing
  release; 378 of its 1,176,576 bytes differ, all of them stamps except the
  six bytes of `AppVersion::GetRevision`, which now returns 763796. All 27
  hooked functions are byte-identical at the same addresses, every structure
  in the PDB has the same fields, offsets and size, and the world format is
  still `SIMSAVE` v15.
- **The element definitions are unchanged**, so the material catalogue and
  with it every colony save stay valid (the game's own save format went from
  7.38 to 7.39; it reads older saves).
- **The game's C# code changed in many places.** Of the 230 game methods the
  mod patches, 35 have a different body, and four patch targets no longer
  exist in their old form.

## What was repaired

| Game change | Effect on 1.10.0 | Repair |
| --- | --- | --- |
| Linux `libSimDLL.so` in `Plugins/x86_64/` | "Cannot read native image" | The backend and the tools take whichever of the two places exists (`native_image` in `oni_backend.cpp`, `sim_image` in `oni_hook_profiles.py`) |
| Windows `SimDLL.dll` rebuilt | No profile for its identity | Profile for 763796 added after comparing every hook, global, layout and the save writer with 744825 ([record](game-update-resilience.md#build-763796-2026-11-19)) |
| A dismantled rocket interior is now queued as a `PendingRocketInteriorDestruction` with a save blocker, and `ClusterManager.DeleteWorldObjects()` handles the whole queue | The patch on `DeleteWorldObjects(WorldContainer)` could not be applied; the mod refused to start | `ColonyClusterRetirement` keeps the records of the worlds it is retiring out of the game's call, runs the game's own recovery for them at once and finishes them a frame later, after the mixture has left: close the cells, destroy the world objects, free the grid slot, remove the save blocker |
| `PropertyTextures.UpdateGasColour`, `UpdatePressure` and `UpdateDanger` became lambdas held in static fields | Three patch targets missing | The targets are read from the type initialiser: the function stored into the field of that name |
| `KIconToggleMenu.Setup` takes a `List<>` instead of an `IList<>` | Patch target missing | The one-argument `Setup` whose argument is a list of toggles, whichever form |
| The pressure readers of the debug views moved between methods | The fixed list of call sites no longer matched | The call sites are found by scanning the three debug classes for calls of the pressure indexer; five are required, as before |
| The pipe status line is built by a lambda with a new compiler-given name | The patch skipped itself silently: a pipe's status line would have shown ONI's single-element text | The status item's text callback is wrapped once the game's database exists; no compiler-given name is used any more |
| `CO2Manager.OnDestroyCO2` is a static delegate instead of a method | A breath particle inside a rocket interior being retired would have raised a null reference | The delegate is used when present, the method otherwise |
| The pipe biology guard hashes the raw IL of five game methods (`Game.StepTheSim`, `ConduitFlow.Sim200ms`, `GlobalJobManager.Run`, `JobManager.Run`, `ConduitDiseaseManager.Sim200ms`) | Every pipe network started "with fallbacks: all-identity biology unavailable", one warning in `Player.log`; germs of every kind in a mixed pipe were then not carried exactly | The five bodies were compared between the builds with their metadata tokens resolved: identical on both platforms apart from the name of the framework assembly. The two new signatures are accepted |
| `LocText.ParseText` returns its text through an `out` argument; link styling is no longer a static function | Only a development check of the details panel's text called them | The check uses whichever form the game has |

Reviewed and needing no change: the other changed patched methods (saving now
waits while `ClusterManager.CanSave` is false, which the mod's own save
barrier already did for a retiring interior; `Sim.Shutdown` is also called
from `Game.DestroyInstances`; geothermal vents read their emitted mass right
after the sim step; element converters take their rates through a modifier;
`Storage.Transfer` may merge a partial chunk into an existing one; the hover
card gained a pipe conflict line; the rocket self-destruct debris loop), the
transpilers (each refuses to apply when its pattern is absent, and all
applied), and every member the mod finds by name.

The contract that the package carries was generated against build 763796:
2,848 dependencies on each platform (675 types, 1,580 members, 363 named
lookups, 230 patched methods).

## Verification

- **Offline.** 94 of 94 native tests on Linux; 95 of 95 for Windows under
  Wine; the forwarding probe on the new `SimDLL.dll` under Wine in all six
  modes, with reports equal to those on 744825. 556 Python tests (among them
  new controls for the retarget tool on mutated copies of the Windows image).
  All managed test projects of `tools/build.py` against the new Linux game,
  with the contract's 14 synthetic changes refused. The declaration checks
  for the rocket interior removal, the menu button and the pipe status line
  were rewritten for the new game code.
- **In the Linux game, build 763796**, with packages of the working tree
  during the retarget: the 15 scenario batches of the acceptance tier (68
  scenarios) pass, and with Spaced Out! (which the Linux copy now has) the
  three chains of [the 0.1.9 record](playtest-2026-11-19.md): worlds, rocket,
  rocket ports, input flush and rocket retirement; rocket, ports, gas and
  liquid cargo, flight and abandoning a rocket in flight; meteors and
  geysers. Both ways of removing a rocket interior therefore pass on the new
  flow. Comparing the warnings of these runs with those of 1.10.0's runs on
  744825 is what found the pipe biology fallback; after the repair no run
  logs it.
- **The user's save of 2026-11-19** (written by 0.1.8 on build 744825), in
  the Windows player of build 763796 under Proton: restored at frame
  101,524, the 360 stray cells released, 450 ticks at 3× with an autosave
  written on the way, relative mass and energy errors of 9e-13 and 2e-14, no
  fault. The owner tick is 141 to 148 ms a window, as 1.10.0 measured on the
  old build (140 and 141): the game update costs no speed. The same save
  loads and runs in the Linux player; the status panel, the Atmospheres
  button beside Vitals and the colony render as before.
- **The quick acceptance tier on the release commit was not completed.** It
  ended when the session that started it restarted, after 20 of its 46
  steps: both builds, twelve of the fifteen Linux scenario batches and three
  worldgen clusters passed; none of the Windows steps had started. Three
  Linux batches failed (`linux-b2` in building-melt, `linux-b9` in
  rocket-exhaust, `linux-b13` in storage-ledger), and the cause is the
  suite, not the mod: its Linux batch and cluster steps did not select the
  game content, the Linux copy of the game includes Spaced Out! since this
  build, and so the base-game scenarios ran in a Spaced Out! cluster (the
  step's log shows the expansion loaded; rocket-exhaust builds a base-game
  rocket that does not exist there). `linux-b2` passes again from the
  suite's own package with the base game selected; the other two were not
  re-run, because a newer game build replaced 763796 in the copies first.
  The suite now pins the base game for these steps, as the Proton runner
  always did. Whether building-melt and storage-ledger also show something
  real inside a Spaced Out! cluster has not been looked into.
- 1.10.1 was superseded the same night: Steam delivered the hotfix build
  764025, which 1.10.1 refuses like any unverified build
  ([1.10.2](playtest-1.10.2.md)).

Not tested: Windows itself. The Windows player ran under Proton only.

### The Windows player's exit code under Proton

Unity 6000.3.17 calls `UiaDisconnectAllProviders` as its last act of
quitting. Proton 10's Wine does not have that function and aborts the
process there, so every Windows run on the new build ends with exit code 1
after a complete shutdown (the save written, the owner stopped, Unity's last
shutdown line logged). Windows has the function. `oni_proton_headless.py`
now recognises exactly this case (the Proton log names no other
unimplemented function, and nothing follows Unity's last shutdown line in
`Player.log`), records `raw_player_exit_code` and `proton_exit_abort` in the
result and treats the exit as normal. Any other non-zero exit still fails
the run. The acceptance tier's "normal desktop exit" checks for Windows
steps pass through this rule on build 763796.
