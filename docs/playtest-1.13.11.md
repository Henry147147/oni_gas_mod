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
- In the game on 764961 (Linux player, Windows player under Proton), with
  the hover card over a mixed cell, a backwall cell and a container:
  PENDING.
- The release packager and the release steps: PENDING.
