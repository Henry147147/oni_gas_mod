# Version 1.10.2: Oxygen Not Included build 764025

Steam replaced game build 763796 by 764025 on 2026-11-20, one day after the
update that [1.10.1](playtest-1.10.1.md) answered. On the user's PC 1.10.1
then showed "Mixed Atmospheres is not active: this version does not support
build 764025; 7 parts have changed" at the main menu and left the game
unmodified, as designed ([fail closed](game-update-resilience.md)). The user
asked for the newest game build to be fetched and a working version pushed
out quickly, without long tests. 1.10.2 is 1.10.1 retargeted to build 764025:
no solver arithmetic and no checkpoint bytes change, and colonies saved by
0.1.8 to 1.10.1 load in it.

1.10.2 accepts build 764025 only.

## What the game changed

Nothing the mod uses.

- **The seven differences are the build number.** The game compiles its
  build number into `Game.Save`, `SaveGame.GetSaveHeader` and
  `LoadScreen.DoLoad(string)`, which the mod patches, and
  `KleiVersion.ChangeList` is recorded as a member found by name. Three
  patch targets, their three entries by name and the constant make seven.
  The other 2,841 of the contract's 2,848 dependencies are unchanged on both
  platforms.
- **The rest of the game's C# code.** The decompiled sources of both builds
  differ in 13 files: 11 only in the compiled build number;
  `Movable.OnClickCancel` became public and the Move To button of
  `UserMenuScreen` now cancels a pending move. `Assembly-CSharp-firstpass`
  is the same file.
- **The simulator library.** Linux: the same file as in 744825 and 763796.
  Windows: 30 of 1,176,576 bytes differ from 763796, stamps and the constant
  of `AppVersion::GetRevision`; all hooked functions are equivalent and the
  only changed PDB contribution is that function.
- **Element definitions**: unchanged, so every colony save stays valid.
- **The pipe biology guard**: its five game methods have the same raw IL as
  in 763796 on both platforms, so the accepted signatures still match.

## What was changed in the mod

- The inspected build is 764025 (`tools/mod_identity.py`, the two build
  constants of the managed validation reports, the pinned Windows file
  hashes of the Proton runner and of the shadow probe).
- A native profile for the Windows `SimDLL.dll` of 764025
  (`docs/native-hook-profiles.json`, carried by `tools/retarget_oni.py`
  after the six `SIM_*` entry points with identical calls through a register
  were accepted as for 763796) and its chunk-heat location.
- The contract in the package is generated against build 764025.
- Tools: the retarget report no longer prints "0 contract differences" for a
  platform whose contract the given package does not carry; the harness
  sandbox leaves out an automount point with nothing mounted (below).

Why a game update that changes nothing still needs a new version of the
mod, and what would avoid it, is in
[the retarget record](game-update-resilience.md#build-764025-2026-11-20).
That is a decision for the user and is not part of 1.10.2.

## Verification

Short by request: the offline suites and one load of the user's colony on
each platform. No acceptance tier was run.

- **Offline**, against the new Linux game: `tools/build.py` with all 40
  steps, among them 94 of 94 native tests, the Python tests (557 with the
  new sandbox control) and every
  managed test project (the contract's synthetic changes refused, the
  declaration checks of the hooks). `tools/retarget_oni.py` on the packaged
  mod: no contract difference on either platform, the Windows profile
  carried, the Linux image already verified, element definitions unchanged.
- **In the game, build 764025.** With a package of the working tree, the
  user's save of 2026-11-19 in the Linux player: "Game build 764025
  verified", restored, the 360 stray cells released, 300 ticks at 3×, no
  fault and no warning from the mod. Then the exact packages of the release
  commit `f51be6a`:
  - the Workshop folder in the Linux player: a new base-game colony starts
    and completes the base scenario;
  - the Workshop folder and the private Windows package in the Windows
    player under Proton, each with the user's save: verified, restored at
    frame 101,524, the 360 cells released, 450 ticks at 3× with an autosave
    on the way, relative mass and energy errors of 9e-13 and 2e-14, no
    fault. The owner tick is 138 to 148 ms a window, as 1.10.1 measured on
    763796 (141 to 148).
  No run logs "started with fallbacks" or "is not active".

Not tested: Windows itself; the Windows player ran under Proton only. The
acceptance tier of 1.10.1 was cut off at 17 of 46 steps by a session restart
and then overtaken by this game update; three of its Linux steps had failed
because the suite did not select the base game on the Linux copy, which
includes Spaced Out! since 763796 (the 1.10.1 session's finding; one of the
three was rerun with the base game selected and passed).

### The test host's sandbox and the Windows share

The harness starts each game in a `bwrap` sandbox with the whole root bound
read-only. `bwrap` remounts every mount below such a bind, which triggers
an automount; the host's automount of the user's Windows PC (`/mnt/z`) then
fails while that PC is off, and with it the sandbox ("Unable to apply mount
flags: remount /newroot/mnt/z: No such device"). No game started during
this work until `root_binds` in `tools/oni_headless.py` left such a point
out: an automount point with nothing mounted becomes an empty directory in
the sandbox, and what lies beside it is bound entry by entry. With every
automount mounted, or none present, the sandbox is the same as before.
