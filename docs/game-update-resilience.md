# Game-update resilience

## Supported builds in 1.11.0

One installation serves ONI 744825, 763796 and 764025 on Windows and Linux x64.
The package root targets the newest build. Every earlier build has a complete
loadable content folder under `archived_versions/<build>/`. ONI selects the
highest eligible `minimumSupportedBuild`. The selected adapter checks its
compiled game build, the build in its platform contract, the running game,
and the existing managed dependencies and native profiles. A build between
two registered numbers or newer than the last one is refused. The mod ID,
settings directory and save format are shared across the three builds.
Mixture saves can move forward across these builds under the existing
catalogue and format checks. Saves from a newer game still require that
game build or a later supported one.

### Keeping an earlier build working

1. Keep both licensed reference installations for the outgoing build beside
   the latest ones, with `-<build>` appended to each directory name. Keep
   the outgoing entry in [the build registry](../managed/OniBuilds.targets).
2. Inspect the incoming game with the [retarget procedure](#retargeting-a-new-build).
   For initial comparisons, the offline contract generator can inspect new
   installations before a package exists. Review game API, native profile and
   catalogue differences, then add one registry row with the new build number,
   both managed assembly hashes and the Windows simulator hash. Keep the native
   profile for each distinct inspected simulator. If an API needs a different
   implementation, name the feature in the affected rows. The managed build
   selects it by the reference assembly hash. Both rocket-removal paths share
   the same recovery and save-barrier code.
3. Build the current managed adapter against the newest Windows and Linux
   installations and run the normal release packager. It rebuilds every older
   adapter from the same source tree, compares both platforms' managed outputs,
   generates contracts against their own games and checks them before writing
   the manifest. Every runtime payload is covered by that manifest. A private
   probe also includes its platform's archives by default. For development
   with one game installation, use `package_probe.py --single-build --game-dir DIR`.
4. Run `tools/build.py --game-dir DIR --oni-native-observer` and the
   release acceptance tier. To test an earlier game, pass its `--linux-game`
   and `--windows-game` installations to `tools/acceptance_suite.py run REV
   --tier release --keep-going`. Results and single-build test packages for
   earlier games are stored separately under the revision and build number.
   Exercise the distribution through ONI's archive selection with one base-game
   save from the oldest supported build. The same command covers every registry
   row on Linux and in the Windows player under Proton, including a menu reload.

   ```sh
   python3 tools/oni_version_matrix.py --package path/to/MixedAtmospheres \
     --save path/to/oldest-colony.sav --output artifacts/version-reloads \
     --proton build-proton-official10/proton-10.0-4b
   ```

The records below describe the original verification mechanism and the
single-build packages through 1.10.2.

**Status (2026-10-27): implemented.** Packages carry `game-contract.json`, the
native library selects SimDLL profiles by normalised identity, and mixture saves
load across verified builds. The in-game run and remaining gaps are under
[Verification](#verification).

Steam updates Oxygen Not Included automatically. Until now the mod accepted
exactly one game build, 744825, by whole-file SHA-256 of `Assembly-CSharp.dll`,
`Assembly-CSharp-firstpass.dll` and `SimDLL`, and colony saves required that
same build. Any Klei patch therefore disabled the mod and stranded every
mixture colony, even after a mod update. This page sets out how a build is now
verified, what replaces the whole-file hashes, how saves record and accept
builds, and how a new build is retargeted.

The rule is unchanged: **fail closed**. A build is accepted only when every
dependency of the mod is verified. Otherwise the mod installs nothing and says
so, and colonies keep running on ONI's own simulation or wait unopened for a
compatible version.

## What the mod depends on

| Side | Dependency | Where recorded |
| --- | --- | --- |
| Native `SimDLL` | 27 hooked or bound functions (entry address and 32-byte fingerprint), the `gSim`/`gSimData` globals, private layouts (`SimData` header, `CellSOA` vectors, `SimFrameInfo` message vectors, chunk registry), the Linux GCC-specialised chunk-heat ABI, the `SIMSAVE` v15 world format | `native-hook-profiles.json`, `native/src/oni_*` |
| Managed | Harmony patch targets: attribute, `TargetMethod(s)` and manual patches, including the native-message callers that `NativeMessagePatch` finds by scanning `Sim`/`SimMessages` | mod assembly |
| Managed | Game members the mod calls or reads directly (MemberRef table) or finds by name (`AccessTools`, `typeof(T).GetX("name")` with constant arguments) | mod assembly |
| Managed | Layouts of game structs that cross the native boundary or that the mod reads through pointers (`Sim.Cell`, `Sim.GameDataUpdate`, `Sim.Element`, message structs), and enum and `const` values compiled into the mod | game assemblies |
| Data | The gas and liquid catalogue (material model 3) that every colony save is bound to | computed at colony start |

## How a build is verified

`Mod.OnLoad` checks, in order, before installing anything:

1. **Platform**: Windows x64 or Linux x64 Mono. Anything else, such as macOS,
   is refused (see [refusals](#refusals)).
2. **One copy**: a second Mixed Atmospheres copy loading in the same process is
   refused.
3. **Native identity**: the game's `SimDLL` must match a profile in the
   supported table by its *normalised image identity*. That is the SHA-256 of
   the file with only the linker's build stamps zeroed: on PE, the COFF and
   debug-directory timestamps, the checksum and the CodeView GUID, age and path;
   on ELF, the build-id note. Every code and data byte is still covered, so
   all RVAs, private layouts and the `SIMSAVE` writer are those of an inspected
   image. A relink of unchanged code, which is common when Klei patches only C#,
   is accepted. The native library repeats this at attach and checks every
   hook's entry fingerprint in memory, as it does today.
4. **Managed contract**: every recorded managed dependency must resolve in the
   loaded game to the same canonical form:
   - *methods* (patched originals, native-message callers, methods called
     directly or by name): the canonical signature plus a normalised IL hash.
     Metadata tokens are replaced by the canonical name of what they resolve
     to and string tokens by their text, and locals and exception clauses are
     included. The hash does not change when Klei adds members elsewhere,
     which shifts tokens, but does change when the method's own code changes;
   - *fields and properties*: declaring type, name, type and static flag;
   - *value types* referenced by the mod or by any recorded signature: layout
     kind, pack, size, ordered fields and `Marshal.SizeOf`;
   - *enums* referenced by the mod: the complete name/value set; `const`
     fields of referenced types: their values, except `KleiVersion.ChangeList`.

   After `PatchAll` and the manual patches, the set of originals that the mod's
   Harmony ID patches must equal the verified set exactly. An extra target,
   for example from a `TargetMethods()` scan that found a new native-message
   caller, removes every patch.
5. If anything fails, the mod tears down, installs nothing more and shows a
   player notice naming the first differences. The log lists every difference.

### What replaces whole-file hashes

- `Assembly-CSharp.dll`/firstpass hashes are replaced by the **managed
  contract**: member-level and independent of metadata tokens.
- The `SimDLL` hash is replaced by the **normalised image identity** keyed into
  a table of verified native profiles. A profile holds several builds per
  platform, so supported builds can accumulate.

When a hotfix preserves both native identities and the managed dependencies,
its registry entry can reuse the existing native profile and managed implementation.
Each new game build still needs its own contracts, packaged variant and verification.

### Where the contract comes from

The contract is a build product, not committed data. `tools/GameContract`
(.NET 10) loads the built mod assemblies and the reference game's `Managed`
folder into an isolated `AssemblyLoadContext`. It uses reflection only and never
invokes game code. It enumerates dependencies with the same source file the
mod compiles (`GameContractCanon.cs`), so the offline and in-game canonical
forms come from one implementation. It writes `game-contract.json`, with the
mod assembly's MVID, beside the DLLs; the mod refuses a contract written for a
different assembly. The file holds member names, signatures and hashes, never
game code. The native profiles stay committed in `docs/native-hook-profiles.json`.

## Saves across builds

- The `MACOLONY` envelope already records the game build that wrote it. It now
  records the running build (`KleiVersion.ChangeList`) instead of a constant.
- A save is accepted when its envelope format is known, its material model
  equals the running one, its catalogue hash equals the running game's
  catalogue, its native section is `SIMSAVE` v15, and its recorded build lies
  between 744825 and the running build. Game-build equality is no longer
  required, so existing 744825 saves load on any verified later build whose
  catalogue is unchanged.
- A save from a newer build than the running game, or a catalogue change
  (Klei editing a gas or liquid, or the element order), is refused with a clear
  message before ONI leaves the menu. Remapping saved materials to a changed
  catalogue is a future migration; the retarget report calls it out.
- When the mod refuses a build, it installs only a load guard, and only if
  `LoadScreen.DoLoad(string)` itself verifies. A mixture save then shows
  "needs a Mixed Atmospheres version for this game build" instead of reaching
  ONI's native loader. No save is ever written or changed by a refusing mod.

## Retargeting a new build

`tools/retarget_oni.py --windows-game DIR --linux-game DIR` inspects a new
installation and writes a private report under `artifacts/`:

1. **Build and identity**: the `KleiVersion.ChangeList`, the normalised
   identities, and whether they already match a registered profile. A match
   lets the new registry entry reuse that profile. Generate the build-specific
   contracts, package the new variant and run the tests in [Keeping an earlier build working](#keeping-an-earlier-build-working).
2. **Native**: each hook, binding and global is located by symbol (PDB or
   dynamic symbols) and its fingerprint recomputed. The tool compares, against
   the reference build, the normalised disassembly of every hooked function
   and of the functions that witness each private layout, with branch and
   RIP-relative targets symbolised. The candidate profile is marked *carried*
   (all equivalent) or *needs review*, with the exact functions and layouts
   that changed.
3. **Managed**: it builds the contract against the new assemblies and diffs it
   against the reference: missing or changed members by category (patched,
   called, reflected, layout, enum or const), each traced to the mod type that
   uses it.
4. **Data**: element-definition changes that would change the catalogue, and
   therefore need a save migration.

Candidate profiles are written for review. Licensed captures and reports stay
private, under `local/` or `artifacts/`.

## Build 763796 (2026-11-19)

The first real game update since the contract existed: Steam replaced 744825
by 763796 (Unity 6000.3.5f2 to 6000.3.17f1). Every earlier package refused
the new build at the menu, as designed. What the retarget found and what was
changed is in [the release note of 1.10.1](playtest-1.10.1.md); the points
that matter for the next update:

- **Native.** The Linux image is byte for byte the old one in a new folder
  (`Plugins/x86_64/`), so its profile and the build number it reports stay
  744825's (`SUPPORTED_NATIVE_BUILDS` lists both). The Windows image was
  rebuilt: 378 bytes differ, stamps and the constant of
  `AppVersion::GetRevision`. All 27 hooks are byte-identical at their old
  addresses, the PDB's 4,230 structures are identical in fields, offsets and
  size, and the save writer still writes `SIMSAVE` v15. Six `SIM_*` entry
  points contain calls through a register and were carried with
  `--accept-indirect` after reading them. The forwarding probe passes on the
  new DLL under Wine in all six modes with reports equal to 744825's.
- **Managed.** The same mod source gave contracts that differ in 35 changed
  and 6 removed patched methods, 27 changed members, 11 changed types and 67
  changed named lookups. The removed patch targets stopped the mod at
  start-up, one after the other; contract generation and the patch-target
  check now report every missing target at once.
- **What no check reported.** Three things would have gone unnoticed without
  reading the diff and the logs:
  - a patch with `Prepare()` on a compiler-named lambda skipped itself (the
    pipe status line); such targets are no longer used;
  - a method the mod calls by name had become a delegate field of the same
    name (`CO2Manager.OnDestroyCO2`): the named lookup still "exists", the
    call would have failed at run time. A named entry whose kind changes is
    a break;
  - the pipe biology guard compares a hash of the raw IL of five game
    methods, which changes with every rebuilt assembly because metadata
    tokens move. It fell back with one warning in `Player.log` ("Mixed pipes
    started with fallbacks"). The five bodies were compared with resolved
    tokens (identical apart from the framework assembly's name) and the two
    new signatures accepted. After a retarget, compare the warnings of a run
    on the new build with those of a run on the old one.
- **Saves.** The element files are unchanged, so the catalogue hash and every
  colony save stay valid; saves written on 744825 load on 763796.

## Build 764025 (2026-11-20)

A hotfix one day after 763796. Version 1.10.1 refused it with seven contract
differences; [1.10.2](playtest-1.10.2.md) is the retarget. What it showed:

- **Every game build changes the contract, however small.** The game
  compiles its build number into `Game.Save`, `SaveGame.GetSaveHeader` and
  `LoadScreen.DoLoad(string)`, three methods the mod patches, so their
  normalised IL differs in every build, and `KleiVersion.ChangeList` is also
  recorded as a member found by name. Those were the seven differences;
  nothing else among the 2,848 dependencies changed. A package therefore
  accepts exactly the build it was written against, and each hotfix needs a
  new version of the mod even when, as here, the game changed nothing the mod
  uses. Because `LoadScreen.DoLoad` is one of the three, the refusing mod's
  own load guard is never installed on a newer build either; a mixture save
  is then refused by ONI's native loader, as without the mod.
  Treating the build number as a wildcard in the contract (and the constant
  of `AppVersion::GetRevision` in the Windows image identity) would let such
  a hotfix through; that changes what "verified" means and is a user decision.
- **Native.** Linux: the same file again. Windows: 30 bytes differ from
  763796, the stamps and the constant of `AppVersion::GetRevision`; the
  profile was carried with `--accept-indirect` (the same six `SIM_*` entry
  points with identical calls through a register).
- **Managed.** The decompiled sources of both builds differ in 13 files: 11
  only in the compiled build number, and `Movable.OnClickCancel` became
  public so that the Move To button of `UserMenuScreen` cancels a pending
  move. The five bodies of the pipe biology guard have the same raw IL as in
  763796 on both platforms, so its signatures still match.
- **Order of the steps.** `--write-profiles` writes only when the managed
  contract has no differences, so the mod is first built against the new
  game and packaged (`package_probe.py`), and that package is given as
  `--managed-dir`. A package of one platform carries that platform's
  contract only; the report now says "not checked" for the other one
  instead of "0 contract differences".
- **Fetching.** The private Steam client is taken online from its Steam
  menu (friends presence stays offline), updates the Linux install by
  itself, and fetches the Windows files from its console
  (`download_depot 457140 457141`, `download_depot 457140 1452491`); then
  it goes back to Offline Mode. The copies on the share are renamed so that
  the default folders hold the new build and `-763796` the previous one.

## Refusals

| Situation | Behaviour |
| --- | --- |
| Not Windows x64 or Linux x64 (macOS, 32-bit) | Nothing patched; main-menu notice; ONI unchanged |
| Unverified game build | Nothing patched except the verified load guard; notice lists the differences |
| Second copy of the mod enabled | Second copy patches nothing; notice asks the player to keep one |
| Frosty, Bionic, Prehistoric or Aquatic content active | The mod loads, but the colony owner never takes a colony over; notice; the colony keeps ONI's simulation. A mixture save is not loaded while such a pack is active. This is the release behaviour (user decision 2026-10-27: the first release supports the base game and Spaced Out! only). |

## Commands

```sh
# Written by both packagers; by hand for a built mod folder:
dotnet run --project tools/GameContract -c Release -- generate --mod DIR \
  --game windows-x64=WINDOWS_ROOT --game linux-x64=LINUX_ROOT --output DIR/game-contract.json
dotnet run --project tools/GameContract -c Release -- verify   --mod DIR --game linux-x64=ROOT --contract DIR/game-contract.json
dotnet run --project tools/GameContract -c Release -- controls --mod DIR --game linux-x64=ROOT --contract DIR/game-contract.json
python3 tools/game_mono_contract_check.py --mod DIR [--managed-game WINDOWS_ROOT --platform windows-x64]
python3 tools/retarget_oni.py --windows-game NEW_WINDOWS_ROOT --linux-game NEW_LINUX_ROOT \
  --managed-dir DIR [--accept-indirect] [--write-profiles --build N]
```

`retarget_oni.py` compares against the team's copies of the inspected build by
default (`--reference-windows`, `--reference-linux`), and takes as reference
profile the one whose identity the reference installation has. On Windows the
names of call and data targets come from the installation's `SimDLL.pdb`
(objdump prints none for a PE image). A call through a register keeps a hook
in review unless `--accept-indirect` is given after reading it: the hook is
then carried only when everything else is equivalent and the indirect calls
are written identically in both images, and the report lists each acceptance. It exits 0 only when nothing needs
review. `--write-profiles --build N` appends carried candidate profiles to
`docs/native-hook-profiles.json`. Review, then rebuild the native library and
run a game check before committing them. The Linux private chunk-heat location
(`CHUNK_HEAT_LOCATIONS` in `tools/oni_hook_profiles.py`) is keyed by image hash,
so a carried Linux image also needs its new hash added there; the Windows one
is found again from its PDB record and registered the same way.

After the profiles, add the new reference hashes to `managed/OniBuilds.targets`
and update the native-hook facts in `docs/coverage-inventory.json` and the
shadow probe's reference if it changes. Keep the older builds as described
[above](#keeping-an-earlier-build-working).

## Implementation

| Piece | Where |
| --- | --- |
| Canonical forms, scope, patch-target discovery | `managed/MixedAtmospheres.Oni/GameContract.cs` |
| Generation (reflection-call scanning) and verification | `GameContractCheck.cs` |
| Build check in `Mod.OnLoad`, refused-build load guard | `GameBuildCheck.cs` |
| Offline host: generate, verify, controls | `tools/GameContract` (`tools/game_contract.py` wraps it) |
| The same check inside the game's own Mono runtime, without Unity | `tools/game_mono_contract_check.py` and `.c` |
| Normalised identity | `tools/oni_hook_profiles.py`, `native/src/oni_image_identity.hpp` |
| Profile selection | `native/src/oni_backend.cpp` (`NativeProfile`, `verify_files`) |
| Saves across builds | `ColonySaveEnvelope.Parse(..., earliest, latest, ...)`, `ColonyOwnerPersistence.cs` |
| Retargeting | `tools/retarget_oni.py` |

Reflection by name is covered three ways:

- a constant name (`AccessTools.Field(typeof(T), "name")`, `nameof`,
  `FieldRefAccess<T, F>("name")`, the mod's own `(Type, string)` helpers) checks
  every member reachable by that name;
- a computed name on a known type checks every member signature of that type;
- a computed name in a method checks every game type that method touches.

The contract lists the sites it cannot attribute (24 on the current sources):
the contract code's own reflection, development-only scenes, two
`TargetMethod`s whose targets are already patched entries, and four pipe and
germ helpers that take their type at run time (`ColonyConduitsSchedule.Flush`
and `Value`, `ColonyGermEvolutionProbe.Field`, `ColonyMixedPipeEdgePatch.Fields`).
`retarget_oni.py` prints them for review.

## Verification

Measured on the 744825 installations:

- **Contract.** 2,456 entries per platform: 570 types, 1,342 members, 329 named
  lookups and 215 patched originals. The Windows and Linux contracts differ
  only in `App`'s constants. Verification takes 0.55–0.65 s in a warm CoreCLR
  host (2.7 s cold).
- **Game Mono.** `game_mono_contract_check.py` loads the Linux installation's
  `libmonobdwgc-2.0.so` and runs the check there.
  - The Linux contract: 0 differences in 0.65–1.0 s, including Harmony target
    discovery.
  - The Windows assemblies on that runtime: all 2,241 non-patch entries match.
    Their patch-target step needs Windows' `BCrypt.dll`, so those targets are
    checked under CoreCLR.
  - This run found two runtime differences, both now normalised. Mono reports
    the default struct packing as 8 where CoreCLR reports 0. Members inherited
    from framework base classes differ between the runtimes, so they are left
    out. Floating-point constants are compared by their bits for the same
    reason.
  - Harmony's own `PatchAll` cannot run outside Unity: patching runs game static
    initialisers that need the player. The check that every patched original
    is verified therefore runs in the game.
- **Managed controls** (`GameContract controls`, both platforms). The unchanged
  contract verifies; all 14 synthetic changes are refused and name their entry:
  type, member, named and patched values and keys, a `Sim.Cell` layout, the
  `SimHashes` values, a method body, a token resolving to a different member, a
  vanished named member, an unexpected patch target and a vanished one.
- **Native controls.** The C++ and Python identities agree on shared PE and ELF
  fixtures (`native_oni_image_identity`, `test_oni_hook_profiles.py`) and on
  both real SimDLLs. Through the built library's `ma_oni_probe`, the unchanged
  Linux image and a copy with only a different build-id are accepted, and a copy
  with one changed byte is refused (`InstalledImageControls`).
- **Retarget controls** (`test_retarget_oni.py`, plus a manual Windows repeat).
  - The unchanged installation reports nothing to do.
  - A changed byte outside every hooked function gives a *carried* candidate,
    with all 29 Linux and 27 Windows functions equivalent.
  - A changed byte inside `UpdateTemperature` needs review.
  - A changed `gas.yaml` is reported as a save-migration issue.
- **Saves** (`tools/DistributionTests`).
  - Synthetic envelopes saved at 744825 load under 744826.
  - Newer, older-than-supported, catalogue-changed and model-changed envelopes
    are refused with their reason.
  - Material model 4 (0.1.4,
    [molecular masses](material-catalogue.md#molecular-masses-014-2026-11-11)):
    an earlier model is refused with "Start a new colony with this version",
    a newer one with "Update the mod". The model-3 frozen saves
    `base-defaults-p10-v1` and `-p11-v1` are refused this way and the
    model-4 `base-defaults-p12-v1` is accepted.
  - Real envelopes are accepted byte for byte by a later build: every prepared
    base-game save in `local/saves` and a `bfb1168` Spaced Out! manual save.
    That is the envelope only (framing, build, catalogue, material model);
    whether the colony inside is still playable is a separate content decision.
  - Content decisions are pinned against the real saves too. The frozen
    `base-defaults-p10-v1` is accepted. Every base-game save prepared before the
    nitrogen removal (2026-10-27 amendment: `base-core21-d964`,
    `base-defaults-p5-v1`, `base-floor-108/111/116/118/121/125`,
    `base-package-85`, `base-trace-144`) has a valid envelope and is refused
    with "This save contains removed nitrogen content. Start a new colony with
    this R&D build."; the save file is left unchanged.
  - In-game check, 2026-11-03 (package from `2ae0865`, Linux, `--mode
    colony-owner-load`): each of `base-core21-d964`, `base-trace-144`,
    `base-package-85` and `base-floor-108/111/116/118/121/125` was refused at the
    menu ("Colony save refused before loading: This colony was saved by a Mixed
    Atmospheres build that this one cannot continue, so it was not loaded. The
    save has not been changed. This save contains removed nitrogen content.
    Start a new colony with this R&D build."). The game exited normally, no
    owner started, no exception or crash appears in the log, and every save
    file's SHA-256 is unchanged. `base-defaults-p5-v1` was refused the same way.
  - The envelope's format and framing are unchanged. Saves written under 744825
    are byte-identical to before; a later build writes its own build number into
    the existing field.

Open: a Windows game run of the new checks (Proton or the user's PC), and a
save migration for catalogue changes, which is future work.

## Controls

No newer game build is available, so the controls use the 744825 installation:

- Unchanged: the contract and native identity verify, offline and in game.
- Synthetic changes are each refused, with the entry named: an altered IL
  hash, signature, layout, enum value, missing member or extra patch target;
  an `SimDLL` copy with one changed code byte inside or outside a hook. A copy
  with only its timestamps and debug stamps changed is accepted.
- Saves: an envelope recorded at 744825 is accepted by a running build of
  744826 (synthetic). A newer recorded build or a changed catalogue is refused.
- Refusals: platform, duplicate copy and gameplay-pack decisions come from
  pure functions with unit controls, because that content is not installed.
