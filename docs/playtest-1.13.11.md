# Version 1.13.11: Oxygen Not Included build 764961

Steam updated Oxygen Not Included to build 764961 (Steam BuildID 26935396)
on 2026-11-26. Version 1.13.10 supports 744825, 763796 and 764025 and
refused the new build at the main menu, as designed
([fail closed](game-update-resilience.md)). The user asked for a working
version as soon as possible. 1.13.11 is 1.13.10 retargeted: one package for
744825, 763796, 764025 and 764961. No solver arithmetic, no checkpoint bytes
and no save format change; colonies saved by 1.13.10 load in it.

## What the game changed

- **The contract: eight differences, one of them real.** Seven are the
  build number, as at every update
  ([why](game-update-resilience.md#build-764025-2026-11-20)): `Game.Save`,
  `SaveGame.GetSaveHeader` and `LoadScreen.DoLoad(string)` as patch
  targets, two of them also by name, and `KleiVersion.ChangeList`. The
  eighth is a code change in `SelectToolHoverTextCard.UpdateHoverElements`,
  the hover card, which the mod's `MixtureCellHoverPatch` transpiles. The
  game moved the backwall block (the "natural backwall" entry) out of the
  cell block to the end of the card, under a flag of its own: it is still
  hidden on unrevealed cells, in a solid dupe-passable cell and where an
  overlay filter hides the cell, is now also hidden in the Oxygen and Gas
  Pipe overlays, and is now shown where a foundation building hides the
  cell block.
- **The rest of the game's C# code.** The decompiled `Assembly-CSharp`
  differs in 41 files; on Linux the other assemblies,
  `Assembly-CSharp-firstpass` included, are the same files. Among the types the mod refers to:
  `GameUtil.ClearCell` now returns `bool` (the mod does not call it),
  `Navigator.OnForcedCleanUp` was removed (the mod does not patch it),
  `ObjectDispenser` skips a destroyed item, `StorageLockerSmart` swapped two
  slider labels, `DetailsScreen` resolves a codex link differently,
  `KCrashReporter` treats missing files as corrupt content, and nine files
  differ only in the build number. None of them is a mod dependency the
  contract does not already cover.
- **Klei now signs its Windows files.** Every DLL of the Windows build
  carries an Authenticode signature (Klei Entertainment Inc., 15,696 bytes
  appended, the certificate directory and the checksum set). Their SHA-256
  therefore all changed, also where the code did not:
  `Assembly-CSharp-firstpass.dll` and the Unity and framework assemblies
  differ from 764025 only in those 7 header bytes and the appended
  certificate, and the IL of `Assembly-CSharp-firstpass` is identical.
- **The simulator library.** Linux: the same `libSimDLL.so` as in 744825 to
  764025. Windows: besides the signature, 41 bytes differ from 764025, the
  stamps and the constant of `AppVersion::GetRevision` (the only changed
  PDB contribution); every hooked function is equivalent and the chunk-heat
  procedure is at the same address. The six `SIM_*` entry points with one
  identical call through a register (18 in `SIM_BeginSave`) were accepted as
  for 763796 and 764025.
- **Element definitions**: unchanged, so every colony save stays valid. The
  object-save version is still 7.39.
- **The pipe biology guard.** Its five game methods are the same IL as in
  764025 once tokens are resolved; their raw IL moved with the metadata
  tokens, so the guard needs new signatures (Linux `B9CA816F…CC43F`,
  Windows `DEB759C7…91CCE`).

## What was changed in the mod

- **The hover card needs no code change.** The transpiler's anchors are all
  still there and unique: one `HoverTextHelper.MassStringsReadOnly` call,
  the `BeginShadowBar(selected)` before it loaded from the same two locals,
  the cell from local 0, the cell block's `EndShadowBar` after it, and one
  final `EndDrawing`. The instruction after the cell block's `EndShadowBar`,
  where the mixture card jumps to when it has drawn the cell, was the
  backwall check in 764025 and is now the branch over the "unknown" block;
  both lead to the backwall block, so the mod's card is followed by the
  game's backwall entry on every build as before, and the containers the mod
  adds are still drawn last. The same source therefore serves all four
  builds; no `#if` or feature constant is needed.
- Build 764961 is registered (`managed/OniBuilds.targets`, both managed
  hashes and the Windows simulator hash).
- A native profile for the Windows `SimDLL.dll` of 764961
  (`docs/native-hook-profiles.json`, carried by `tools/retarget_oni.py`), its
  chunk-heat location, the inventory's native-hook facts, and the pinned
  hash of the shadow probe. The Windows native backend
  (`mixed_atmospheres_oni.dll`) compiles the profiles in, so it must be
  rebuilt from this tree; the Linux one needs no new profile.
- The pipe biology guard accepts the two signatures of 764961.
- Version 1.13.11.

## Checks

- `tools/retarget_oni.py` on development packages built against 764961:
  0 contract differences on each platform, the Linux native image already
  verified, the Windows profile carried and then verified, element
  definitions unchanged.
- `tools/game_mono_contract_check.py` in the game's own Linux Mono runtime,
  764961: 3,020 dependencies, 0 differences; the `--other-mod` control
  attributes all 7 differences to the other mod and names it. The Windows
  assemblies on the Linux Mono stop at `BCrypt.dll` (two patch classes
  cannot resolve their targets), on 764025 as on 764961; not a finding of
  this update.
- `tools/build.py --game-dir <Linux 764961> --oni-native-observer`, all 41
  steps: 96 of 96 native tests, 605 Python tests, every managed test
  project, among them the hover transpiler test ("terminal hover injection
  preserves every vanilla instruction and building block") and the pipe
  scheduling guard.
- The managed adapter compiles against each of the four builds on both
  platforms; the hover and pipe hook tests (`MixtureInspectionTests`,
  `ColonyConduitHookTests`) pass against the Linux 744825, 763796, 764025
  and 764961 games and the Windows 764025 and 764961 assemblies.
- In the game on 764961, the development package of the working tree
  (Linux player): the main menu with the mod active; a new Spaced Out!
  colony, 600 ticks; the user's Cycle 130 save with their seven other mods
  loads, runs 300 ticks and saves. The hover card on screen: over an air
  cell the game's entry (Printing Pod) is followed by the mod's card
  ("OXYGEN MIXTURE (+ TRACES) · Gas pressure 117.5518789 kPa … Breathable ·
  Oxygen 99.9797% by moles"); over solid tiles the game's card alone. The
  old `tile-hover` validation mode (a pixel review that belongs to no
  tier) exceeded its fixed deadline: inconclusive, neither a pass nor a
  failure. A backwall cell and a container were not looked at.

## Staged package

`e09397d` (`origin/release/1.13`) is staged on the private share as
`releases/1.13.11` and `publish/update-1.13.11`, and was uploaded to the
Workshop on 2026-11-27 at 00:11:55 UTC with `tools/workshop.py`
([the procedure](workshop-upload.md#uploading-a-version)), at the user's
request of 2026-11-26 ("ONI updated, please get 1.13.11 pushed out ASAP."),
in an online window of the private Steam client with the game slot held;
the client went back to Offline Mode. The tool takes revisions on
`origin/main` only, so `release/1.13` was first merged into `main`
(`d4fa238`; `main` keeps its version 1.15.0). The published file agrees
with the release folder file for file.

| Step, on `e09397d`, before the upload | Result |
| --- | --- |
| Release builds: Linux for Ubuntu 22.04, the suite's Linux and Windows builds (Windows imports system libraries only) | passed |
| Package for game builds 744825, 763796, 764025 and 764961, archive verification | passed: `MixedAtmospheres-1.13.11.zip`, 32,105,898 bytes, SHA-256 `8b5198a6…3ebc`; Workshop file 32,104,266 bytes, SHA-256 `3adb8189…91d5e` |
| Release folder, Linux game 764961: a new Spaced Out! colony and a new base-game colony, 600 ticks at speed 3, saved | 2 of 2 |
| Five of the user's share saves (Cycles 130, 128, 70, 44, 33) on the release folder with the seven other mods, Linux game 764961 | 5 of 5 |
| The Cycle 130 save with the seven mods, Windows game 764961 under Proton, release folder | loads and runs (387,121 frames; mass error 2.76e-12, energy 7.8e-14) |
| A new Spaced Out! colony, Windows game 764961 under Proton, release folder, 600 ticks | passed |
| Load matrix (the three older game builds, Linux and Windows under Proton) | PENDING(load matrix): runs after the upload |

Not run, at the user's request for speed: the acceptance tiers.

## After this version

The rule that lets the mod run on a game update that changes nothing it
depends on is built as 1.13.12 (separate work) and is in game testing.
