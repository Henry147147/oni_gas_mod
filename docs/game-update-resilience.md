# Game-update resilience

## Supported builds in 1.11.0

One installation serves ONI 744825, 763796 and 764025 on Windows and Linux x64
(1.13.11 adds 764961).
The package root targets the newest build. Every earlier build has a complete
loadable content folder under `archived_versions/<build>/`. ONI selects the
highest eligible `minimumSupportedBuild`. The selected adapter checks its
compiled game build, the build in its platform contract, the running game,
and the existing managed dependencies and native profiles. A build between
two registered numbers is refused. From 1.13.12, a build newer than the last one is
accepted only when nothing the mod uses changed apart from the build number
([tolerated newer builds](#tolerated-newer-builds)). The mod ID,
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
| Managed | Harmony patch targets, including attribute targets, `TargetMethod(s)`, the fields injected through `___field` parameters and manual patches. `NativeMessagePatch` finds native-message callers by scanning `Sim` and `SimMessages`. | mod assembly |
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
   player notice that names the cause ([refusals](#refusals)). The log lists
   every difference. A difference that is only [another mod's patch](#other-mods)
   on verified code is not a failure: the mod starts and the log names the
   patched methods.

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
- **A catalogue change says what changed (1.14.0).** The catalogue hash is
  taken over the 168-byte material records, and the owner image inside the
  save holds those same records (checked on every real save the controls
  read). When the hash differs, `ColonyCatalogueDifference` compares them by
  element hash with the game's current catalogue: elements only in the game
  (new), only in the save (missing) and in both with different properties
  (changed). The player is told up to four names per kind and "and N more",
  then that usually a mod that adds or changes elements was turned on or off
  since the colony was saved, and to load the colony with the mods it was
  saved with or to start a new colony with the current mods; the text does
  not blame the Mixed Atmospheres build. An element that only moved in the
  element list is not named; when that is the whole difference, the text says
  the order of the game's elements changed. Player.log lists every element,
  with what differs (molar mass, heat capacity, conductivity, density,
  transition temperatures, transition targets, byproducts, index). If the
  saved records cannot be read, the text says only what to do. Rephysicalized
  is such a mod: it adds Molten Silicon and Silicon Gas and edits others, so
  a colony saved without it is refused while it is on, and the other way
  round. Both load paths give the same text: the menu check
  (`LoadScreen.DoLoad`, `MainMenu.ResumeGame`, when the game's elements are
  loaded there) and the in-game load. The mixed pipes' own catalogue check
  comes after the owner's and cannot refuse such a colony first.
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
  a hotfix through. The user decided this on 2026-11-26:
  [tolerated newer builds](#tolerated-newer-builds).
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

## Build 764961 (2026-11-26)

The first update after one package began to carry several builds. 1.13.10
(744825, 763796, 764025) refused it; [1.13.11](playtest-1.13.11.md) adds it
as a fourth row of the registry. What it showed:

- **A real change in a patch target.** Besides the seven build-number
  differences, `SelectToolHoverTextCard.UpdateHoverElements` changed: the
  backwall block left the cell block. The mod's transpiler on it checks its
  own anchors and still found each of them exactly once, so the same source
  serves all four builds. A tolerance that masked only the build number
  would still have refused this update: telling a harmless change in a
  patched body from a breaking one needs either the IL read by a person, as
  here, or a check of the patch's own anchors.
- **The pipe biology signature moved although the five bodies did not.**
  764025 kept 763796's raw IL; 764961 shifted the metadata tokens. The
  signature is reproduced from the assemblies on disk with a small
  reflection program (`GetMethodBody().GetILAsByteArray()` over the five
  methods, hashed as `ColonyPipeBiology.SchedulingSignature` does), and the
  resolved IL of the five bodies is compared with `ilspycmd -il` before the
  value is accepted.
- **Klei signs its Windows files from this build on.** Every Windows DLL
  carries an appended Authenticode certificate, so every whole-file hash
  changed, also of files whose code did not (`Assembly-CSharp-firstpass`,
  the Unity and framework assemblies). The registry's whole-file hashes and
  the native image identity (which zeroes stamps and the checksum but
  covers the certificate directory and the appended certificate) therefore
  change with every signed build even where nothing else does.
- **Native.** Linux: the same `libSimDLL.so` again; no profile is needed.
  Windows: 41 bytes besides the signature, the stamps and the constant of
  `AppVersion::GetRevision`; carried with `--accept-indirect` as before, and
  the chunk-heat procedure at the same RVA.
- **Names on the share.** The default folders hold the newest build and the
  previous builds get `-<build>` (`mod_identity.reference_installation`):
  `OxygenNotIncluded-linux` and `OxygenNotIncluded` are 764961,
  `…-linux-764025` and `…-764025` the previous build.

## Tolerated newer builds

User decision, 2026-11-26: "please also make the mod not fail on update if
nothing the mod touches is changed. work on this ASAP and prioritize it over
the new features." Until then every game build turned the published mod off,
because the game compiles its build number into code the mod patches (see
[764025](#build-764025-2026-11-20)) and, on Windows, into its `SimDLL`.

### Which adapter checks a newer build

ONI's `KMod.Mod` collects the root content and every `archived_versions/*`
folder, keeps those whose DLC and `APIVersion` fit, and loads the one with the
highest `minimumSupportedBuild` not above the running build. The same code
runs on Windows and Linux, and every package carries the same archives on
both. The root's `minimumSupportedBuild` is the newest supported build, so a
newer game always loads the root adapter, and a build between two supported
ones loads the archived adapter below it. `ModAdmission.AtPackageRoot` tells
the two apart by the folder (`archived_versions/<build>/` is ONI's own
layout), and `ModAdmission.ChecksNewerBuild` allows the check only for the
root and only for a running build above the contract's. Older and in-between
builds stay refused by their number, as before.

### Managed: the build-number reading

- **What the contract stores.** `tools/GameContract generate` computes every
  entry a second time with the contract's own build number read as a
  placeholder (`GameContractCanon.BuildMark`). The placeholder replaces an int32
  operand of `ldc.i4`/`ldc.i4.s` equal to the build number, an integer constant
  equal to it (enum members and `const` fields, any integer type), and the
  number as a whole word (no digit right before or after) in a string literal or
  string constant. Branch offsets, other constants, locals, clauses, signatures
  and resolved tokens are left as they are. When this reading differs from the
  exact form, that is, when the number occurs in the entry, it is written as the
  entry's `BuildMasked` value (hashed like `Value`). The schema keeps version 1:
  the field is optional, so contracts written before it load and simply forgive
  nothing. On 763796, 764025 and 764961 the same seven entries carry it:
  `Game.Save`, `SaveGame.GetSaveHeader` and `LoadScreen.DoLoad(string)` as patch
  targets, `LoadScreen.DoLoad(string)` as a member, and the named lookups of
  `Game.Save`, `LoadScreen.DoLoad` and `KleiVersion.ChangeList`.
- **The check.** The exact comparison runs first and is unchanged. Only for an
  entry that differs, and only on a newer build checked by the root, the game's
  form is computed again with the running build number read as the placeholder.
  The difference is forgiven when the entry has a `BuildMasked` value and the
  two readings are equal. An entry without one, a changed key, a missing member
  or patch target and a new patch target are never forgiven. This is stricter
  than masking at run time alone: an entry can be forgiven only if the
  contract's own build number occurred in it when it was written. Since the
  contract side masks the contract's build and the game side the running build,
  a game that still holds the old number where the contract had the build
  number differs too. A method patched by another mod is compared without the
  patch mark in both readings, so such a method is still told apart from a
  changed one.
- **Accepted.** Player.log, one entry:

  ```text
  [Mixed Atmospheres] Game build 764025 is newer than the build this version was checked against (763796); every part of the game the mod uses is unchanged (3020 managed dependencies checked in 512 ms); 7 entries differ only by the build number:
    static LoadScreen::DoLoad(System.String)System.Void
    named Game::Save
    ...
  ```

  No main-menu notice, because nothing needs the player's attention.
- **Refused.** The refusal keeps `UnsupportedBuild`'s kind, load text and
  remedy (a Mixed Atmospheres update). Its notice also names the first three
  changed parts by short name (`ModAdmission.NewerBuildChanged`):

  ```text
  Mixed Atmospheres 1.13.10 does not support this version of Oxygen Not Included (the game is build 764961; this copy of the mod is for build 764025). The game update changed one part of the game the mod uses:
    - SelectToolHoverTextCard.UpdateHoverElements

  An update of Mixed Atmospheres is needed. ...
  ```

  Player.log lists every difference and every forgiven entry. From 1.15.0,
  other mods' patch marks are judged as on a supported build
  ([other mods](#other-mods)). If every remaining difference is such a mark,
  the newer build is accepted, and Player.log has the newer-build entry and
  the patched methods. A real change refuses with the update notice, which
  names only the real changes, also when patch marks come with it. (1.13.12
  showed the other-mods notice in that case.) If the managed side is accepted
  but the `SimDLL` is not a
  verified or carried image, the same update notice names the native
  simulator.

### Native: the revision-masked identity

The Windows images differ as follows after the normalisation of
[How a build is verified](#how-a-build-is-verified):

| Images | Differing bytes after normalisation |
| --- | --- |
| 744825 to 763796 | header and toolchain (Rich) stamps at file offsets 60 to 784 (a new linker), and the revision constant |
| 763796 to 764025 | only the revision constant: file offset 0x4ED31 to 0x4ED32 (RVA 0x4F931, the low two bytes of the immediate) |
| 764025 to 764961 | the revision constant (same bytes), the security data-directory entry (file offset 0x1B8, 8 bytes) and the 15,696-byte Authenticode certificate appended at file offset 1,176,576. Klei signs the files since 764961. |

The constant is the immediate of `AppVersion::GetRevision` (PDB public
`?GetRevision@AppVersion@@YA?BIXZ`, procedure of 6 bytes at RVA 0x4F930,
`mov eax, imm32; ret`). It equals the game build: 744825, 763796, 764025 and
764961 (0x000BAC21).

- **What a profile stores.** A Windows profile carries `revision_site` (symbol,
  RVA 0x4F931, 4 bytes, the value) and `revision_masked_identity`.
  `tools/oni_hook_profiles.py revision --windows-game DIR` records them for the
  verified profile of an installation, and `inspect` and `retarget_oni.py`
  write them for new profiles. The site must be in an executable section and
  outside every hook fingerprint, binding and global. Profiles with one masked
  identity must agree in their site, hooks, bindings and globals.
- **The identity.** `revision_masked_identity` in `tools/oni_hook_profiles.py`
  and `revision_masked_image_identity` in `native/src/oni_image_identity.hpp`
  start from the normalised image. They also zero the 4 bytes at the site,
  zero the security directory entry, and cut off the certificate data, which
  must be the last bytes of the file, beyond every section. Every other code,
  data and header byte stays covered. An unsigned image and its signed twin
  therefore compare equal. A one-byte change anywhere else, including the
  `mov` and `ret` around the constant, changes the identity.
- **Selection.** `verify_files` takes a profile whose exact identity matches
  ("verified"). Otherwise it takes the newest profile whose revision-masked
  identity matches ("carried"). Hooks, RVAs, layouts and in-memory fingerprints
  are then those of the verified image. `ma_oni_selected_profile` returns how
  the profile was chosen, and the mod writes it to Player.log, for example:
  `Game SimDLL carried from the verified image of build 764025 (identity
  a8a354e443d5f558; it differs only in the revision constant at RVA 0x4f931, 4
  bytes, and the Authenticode signature; revision-masked identity
  1f266c5b6c9fb87c)`. No build number is compared on the native path, managed
  or native. The build reported by the observer is the selected profile's.
- **Result.** 763796, 764025 and 764961 share the revision-masked identity
  `1f266c5b…`, and 744825 has its own (`0c9fa4e3…`). The Linux `libSimDLL.so`
  is byte for byte the same file in 744825, 763796, 764025 and 764961, so it
  matches its verified profile without any of this. It gets no revision site.

### What else reads the build number

| Place | On a tolerated build |
| --- | --- |
| `GameBuildCheck.CompiledBuild` | Still must equal the contract's build (the package's own consistency). |
| `RequireVerifiedPatches` | Unchanged: the set of patched originals is compared by key, and keys are never masked. |
| `RefusedBuildLoadGuard` | On a refused newer build, its two targets are accepted when equal to the contract apart from the build number, so a mixture save gets the notice instead of reaching the game's loader. |
| Save envelope (`ColonySaveEnvelope.Parse(..., FirstSupportedBuild, RunningBuild, ...)`, `ColonyOwnerPersistence`) | Unchanged. A save records the running build, and saves from 744825 up to the running build load. A save written on the newer build is refused by an older game, as before. |
| `OwnedReservoir*` header pins (`744825` in `OwnedReservoirLoadHeaderV1`, `OwnedReservoirSavedLayout`, `OwnedReservoirLoadBoundary`) | Development and rejection-only paths, unchanged. The load guard's save reader (`InspectLayoutHeader`) accepts any build from 744825. |
| `ColonyPipeBiology` scheduling guard | Its raw-IL signature moves with every rebuilt assembly (764961: new on both platforms), which would turn the all-identity pipe biology off. On a tolerated build it also accepts the token-independent canonical form of the same five bodies (`CanonicalSchedulingSignature`, without the inlining mark). That form is the same on 744825, 763796, 764025 and 764961 on both platforms, and `GameContract verify`/`controls` print it for every installation. |
| `IntegrationProbe`, `ColonyRuntimeValidation`, `NativeContractCapture`, `RuntimeCoverageCapture` | Development reports only. They record the compiled and the running build. |
| `mod_info.yaml` `minimumSupportedBuild` | Unchanged. The root's value is the newest supported build. |

### Controls

- **Managed, real builds** (`tools/tests/test_newer_build_tolerance.py`). The
  current source is compiled against 763796 only, and its contract is written
  against the 763796 Windows and Linux games. Against 764025 it is accepted on
  both platforms with exactly the seven entries above forgiven. Against 764961
  it is refused on both, naming
  `SelectToolHoverTextCard.UpdateHoverElements`. The same results come from
  the game's own Mono (`tools/game_mono_contract_check.py --game <764025 or
  764961 Linux> --mod <that folder>`), which prints the notice. Each control
  skips when its game copy is absent.
- **Managed, synthetic** (`GameContract controls`, any one installation). With
  the contract read as an earlier build's: unchanged, nothing is forgiven;
  every entry with a reading given a wrong exact value, each is forgiven; one
  reading changed too, that entry is refused by name; a changed entry without
  a reading is refused; for the contract's own build, nothing is forgiven.
- **In the game.** A single-build development package for an earlier build
  (`package_probe.py --single-build --game-dir <that build>`) is the newest
  adapter it carries. The harnesses refuse a game newer than a package's
  newest build unless `--newer-game-control` is given
  (`tools/oni_headless.py run`, `tools/oni_proton_headless.py`). With the
  option, ONI loads the root content and the package's own check decides.
- **Unit** (`tools/DistributionTests`). The substitution (whole words, every
  occurrence, exactness, escaping), the root and in-between decision, the
  refusal notice and the Player.log text.
- **Native** (`native_oni_image_identity`, `test_oni_hook_profiles.py`). Shared
  PE fixtures: another revision constant, and a signed twin, give the same
  masked identity, while the exact identity sees both. A code byte elsewhere,
  the instruction around the constant, or a site one byte off change it. A
  site outside executable code or the sections, an empty site, a certificate
  that does not end the file, and an ELF image are refused. With the real
  images, 763796, 764025 and 764961 share the recorded masked identity, and
  764961 with one flipped code byte does not. Every Linux copy matches the
  verified Linux profile.

## Other mods

A player on build 764025, which the installed version supported, was shown
"does not support this version of Oxygen Not Included ... 25 have changed"
(`SaveLoader.LoadHeader`, `SaveLoader.Save`,
`CustomGameSettings.SetQualitySetting`, ...) and took the game to be out of
date (report passed on by the user, 2026-11-24). The game was not: other mods
had patched those methods.

- **Why a patch reads as a change.** Harmony marks every method it patches as
  not inlinable. Under Mono that sets `NoInlining` in the runtime's own method
  record, which is where reflection reads a method's implementation flags, and
  the canonical method form includes them. Measured in the game's Mono: a
  prefix on `SaveLoader.Save` takes the flags from 0 to 8 and leaves the IL
  bytes as they were. The check runs once, in `Mod.OnLoad`, so it sees the
  patches of mods the game loaded earlier and none of those loaded later.
- **What 1.13.6 did.** For a method that Harmony reports as patched, `Verify`
  compares the form without that mark as well; when it then matches, the
  difference is reported as `patched by another mod: ...` instead of
  `changed ...`. In 1.13.6 to 1.13.9 the mod still stayed off when every
  difference was of that kind, with a notice that named the mods and asked
  the player to disable them. That depended on the order of the mod list:
  the same mod loaded after Mixed Atmospheres was never noticed. Rephysicalized
  (Workshop item 3609531831) loaded first turned the mod off through 16
  patched methods (`Db.Initialize`, `OxygenBreather.Sim200ms`,
  `Storage.ConsumeAndGetDisease`, ...); loaded after it, a new colony ran
  normally.
- **What 1.14.0 does (user request 2026-11-26, [the rule](completion-plan.md)).** When every difference is
  another mod's patch mark on verified code, the build is verified and the
  mod starts. The IL of those methods is still verified byte for byte; only
  the mark is set aside. Both orders of the mod list are treated alike, and
  the mod runs with another mod's code on a method it calls, as it always
  did beside a mod loaded later. There is no main-menu notice. Player.log
  says it once, on the verified line (here with Rephysicalized loaded first):

  ```text
  [Mixed Atmospheres] Game build 764025 verified: 3010 managed dependencies match the game contract (written against build 764025) in 450 ms. 16 game methods the mod uses carry other mods' Harmony patches; their code is verified and the mod runs beside the patches:
    ComplexFabricator.CompleteWorkingOrder <- Rephysicalized (Harmony: Rephysicalized)
    ...
  ```

  A mod is named by its title in the game's mod list, found through the
  assembly that holds the patch; a mod's Harmony ID is its own choice and
  is written beside it. The same lines are kept in
  `GameBuildCheck.OtherModPatches` and, in a development package, in
  `integration-probe.json` (`GameContractOtherModPatches`). Any other
  difference refuses the build with the notice about the game's code
  (below), also when it comes together with patch marks.
- **A patch that was removed.** Harmony keeps the mark on a method after its
  patches are removed, and no patch then accounts for it. Such a method
  reads as changed game code and the mod stays off (measured in the game's
  Mono with `--other-mod removed`).
- **How common it is.** Of 18 other Workshop mods at hand, 10 patch at least
  one of the 1,383 game methods in the contract (`Db.Initialize`,
  `MainMenu.OnSpawn`, `Game.DestroyInstances`, `ModsScreen.BuildDisplay`, ...;
  [1.13.6 notes](playtest-1.13.6.md#how-common-the-conflict-is)).
- **Controls.** `tools/game_mono_contract_check.py --other-mod` loads
  `tools/GameContractOtherMod` in the game's Mono before the check; that
  stand-in patches `SaveLoader.Save` (under two Harmony IDs),
  `CustomGameSettings.SetQualitySetting` and `LoadScreen.DoLoad`. Every
  difference must be attributed to it, the build must be verified and the
  verified line must list the three methods with the stand-in and its
  Harmony IDs. `--other-mod removed` runs the stand-in with
  `MA_OTHER_MOD_REMOVES_PATCHES=1`: it patches the first two methods and
  removes its patches again, and the build must then be refused as changed
  game code while both load entries stay verified. In the game,
  `tools/oni_headless.py run --mode menu --extra-mod-first FOLDER` loads the
  built stand-in ahead of the package; with a development package the run
  passes (`passed-scoped-main-menu`) only when the mod is active, and
  `result.json` lists the tolerated methods under `other_mod_patches`.
- **The load guard.** While the mod is off it patches only the two ways a
  colony loads from the menu (`LoadScreen.DoLoad`, `MainMenu.ResumeGame`),
  and only when the contract verifies both as unpatched by anyone else: a
  Mixed Atmospheres colony is then refused before the game loads it, with
  the notice for the cause. Without the guard the game hands the colony to
  its own simulation, which fails on it ("SimDLL found bad data" and the
  crash screen); the save file is not changed either way. The guard reads
  the save file to recognise the colony, and from 1.10.1 to 1.13.7 that
  reader did not read saves of game builds 763796 and 764025
  ([1.13.8](playtest-1.13.8.md#the-save-file-reader)). Its test: the
  stand-in with `MA_OTHER_MOD_REMOVES_PATCHES=1`, which keeps the mod off
  through the mark its removed patches leave and leaves both load entries
  alone, and `tools/oni_headless.py run --mode menu --resume-save SAVE`,
  which makes the main menu resume that save; the release tier runs it as
  `linux-resume-refused` and requires the refusal for changed game code,
  the guard and the refused save.

## Moono

[Moono](https://github.com/tigin-backwards/oni-moono) replaces the game's
Unity player: it runs Klei's own DLLs on CoreCLR (.NET 10) with its own
reimplementation of Unity, TextMeshPro, uGUI and Timeline. A player's
Player.log with 1.13.9 on Moono (2026-11-26) showed the mod refused with
2,730 "changed" parts such as `changed UnityEngine.Color -> UnityEngine.Texture`.
The game was the supported build; the check read the wrong members. Support
was a user request of 2026-11-26 ([amendment](completion-plan.md)).

- **What Moono changes.** Its loader re-emits every mod assembly (a chain of
  Mono.Cecil passes) and loads the image from memory: the file, its
  `Assembly.Location` and the module version ID stay, but the TypeRef,
  TypeSpec and MemberRef rows are renumbered, so a contract token names an
  unrelated member. A Cecil read and write of the mod alone reproduces the
  player's log (2,731 differences against 2,730). The game's
  `Assembly-CSharp` and `Assembly-CSharp-firstpass` are unchanged on disk
  but re-emitted at load: conversions of floats to integers become calls of
  `OniMoono.Host.MonoConversions`, delegates handed to native code are kept
  alive, thread-abort catch blocks get a call at their end, and the optional
  nav-grid and path-grid tweaks rewrite more. `Assembly.Location` still
  reports the file (a Harmony postfix of Moono's host). Methods it does not
  rewrite keep their IL byte for byte. Unity's types come from
  `OniMoono.UnityEngine`; Unity's own modules are never loaded.
- **Detection.** Moono has no mod API. `MoonoHost` reads its markers: Unity's
  `Object` is defined in the assembly `OniMoono.UnityEngine`; the host
  assembly `OniMoono` carries the release as `[AssemblyMetadata("MoonoRelease")]`
  and the source commit in its informational version; Moono's tweaks and host
  fixes are Harmony owners `OniMoono.Host.GameTweaks.<name>` and
  `OniMoono.Host.*`. Player.log at `Mod.OnLoad`:
  `[Mixed Atmospheres] Running on Moono <release> (commit <commit>, .NET <version>): the game contract is checked by member keys and the game's files ...`.
- **What is verified there.** First the game's files: the SHA-256 of the
  files `Assembly-CSharp.dll` and `Assembly-CSharp-firstpass.dll` (the
  contract carries both per platform; `FirstpassSha256` is new in 1.15.0 and
  empty in older contracts) must be the contract's. A different file refuses
  with the usual "game's code differs" notice. Then every contract entry is
  found by its canonical key instead of its token (`GameContractCheck.IndexByKey`:
  the loaded mod's TypeRef, TypeSpec and MemberRef rows, each member under
  every generic context a method of the mod gives it; a type that only an
  attribute names, whose row Cecil's writer drops, by its name). A key not
  found is `missing`. For the pinned game assemblies, presence, signatures
  (the keys), enum literals, constants and struct layouts must match; a
  game method whose body differs is accepted as Moono's rewrite at load and
  counted. Named lookups on game types carry a second form, `Shape`
  (`GameContractScope.NamedShape`): the members the game's assemblies
  declare, with every method body as `body`. Under Moono a named entry must
  match that form. Everything else is Moono's engine (decided by whether the
  assembly is one of the two files the contract pins, not by name): its
  types and members are checked for presence and signature only, because
  Moono's Unity need not have Unity's private layouts, enum members or
  member lists. `UnityEngine.TextClipping`, for one, has no `Ellipsis` there.
  A named lookup Moono's engine lacks (`absent`) still refuses.
- **What is tolerated, measured offline.** Against Moono's own rewritten
  `Assembly-CSharp` of build 764025 (from a run's conversion cache) and the
  engine of the release in `local/moono/release`, with the mod read and
  written by Cecil: 0 differences, 47 game entries whose code Moono
  rewrote (14 called members, 10 named lookups, 23 patch targets; float
  conversions in all, `MonoMarshalledDelegates.Keep` in
  `SaveLoader.LoadFromWorldGen`, `MonoThreadAbort.CatchEnd` in it,
  `GeneratedBuildings.LoadGeneratedBuildings` and `MainMenu.OnSpawn`), and
  41 engine entries checked for presence and signature only (the layouts of
  `RaycastResult`, `Scene` and `NativeArray<T>`, the `TextClipping` enum,
  `Scene.handle`'s type, `SceneManager`'s member list and 35 member lists
  of game types that inherit from Moono's `MonoBehaviour`). Of the mod's
  transpilers only the one on `ConduitFlow.UpdateConduit` meets rewritten
  code, and there each conversion became one call in its place, so its
  pattern and local layout are unchanged. The transpilers and IL witnesses
  already fail loudly where a colony needs them (the mod's start is undone
  with a notice) and softly where it does not (a Player.log warning and the
  game's behaviour).
- **Player.log.** The verified line says `Game build N verified on Moono <release>`
  and adds the count of rewritten and engine entries, the first 20 of each
  (all of them in a development package). Patches of Moono's tweaks on
  methods the mod uses are listed like [other mods'](#other-mods), as
  `<- Moono (Harmony: OniMoono.Host.GameTweaks....)`, and never refuse.
  Every refusal says `on Moono <release>`. When the files match but other
  parts differ (a Moono release that lacks something the mod uses), the
  notice says so and suggests the previous Moono release or the game
  without Moono instead of verifying the game's files.
- **The Mono control.** On the original player the token path stays the
  authority. `GameContractCheck.SelfCheck` (`tools/game_mono_contract_check.py`,
  also with `--other-mod`) also resolves every entry by its key and requires
  the same key and value entry by entry and the same differences; a
  disagreement counts as a difference. The first line ends `key path agrees`.
  Offline, `tools/GameContract verify --by-key [--files-of ROOT] [--mod-assembly DLL]`
  runs Moono's mode on any installation folder.
- **Engine members the mod no longer names.** Moono replaces every
  instruction that names a member its engine lacks by a throw. 1.15.0 names
  none: `Time.captureDeltaTime` left a validation report, `Scene.handle`
  is read by reflection (Unity's returns `SceneHandle`, Moono's an `int`),
  and `SceneManager.Internal_SceneUnloaded` is looked up by a name built at
  run time (the contract then pins `SceneManager`'s whole member list). All
  three were in validation modes only. The owned-load boundary
  (`load-rejection` validation) cannot run on Moono: it needs Unity's own
  engine module by hash and refuses there with that reason.
- **Frame timing.** Moono's delta time is the real time between frames, so
  the V-Sync clock repair ([settings](settings.md), `performance.frameTimingFix`)
  stays off and says so once in Player.log; the performance log keeps its
  timing columns.
- **Saves.** Moono's `background-save` (on by default) leaves only the
  header in a save until the finished `<save>.writing` is moved over it.
  Before the load checks read a save (menu load, resume, the load guard, the
  native-domain validation's readback) they wait up to 45 s for that file
  to go (`PendingSaveWrite`), and otherwise refuse with "still being
  written". Its `fast-save-compression` writes .NET's zlib (zlib-ng) instead
  of Ionic.Zlib; the save reader inflates it at every `CompressionLevel`, with
  sync flushes too (`tools/OwnedReservoirInflationTests`). Moono measured
  its compressed saves about 14% larger, which brings large colonies closer
  to the reader's 64 MiB compressed and 256 MiB body bounds.
- **Limits.** Moono releases change often and are not pinned: a release is
  supported for as long as its files and members meet the rule above, and
  the offline numbers are those of one release and build 764025. Only Linux
  Moono can be run here; Moono on Windows, and the game under Proton with
  Moono, cannot be tested. The native libraries are expected to load as on
  Unity (they are preloaded by absolute path and the mod never sets a
  `DllImport` resolver); only a game run shows it.

### Moono and newer builds

On Moono a game build newer than the newest one a package supports is
refused by its number, with the update notice, which names Moono and both
builds (`ModAdmission.UnsupportedBuild`). The [tolerated newer
builds](#tolerated-newer-builds) rule applies to Unity's player only. The
reason is that Moono's tolerances rest on the game's files being the contract
build's: once `Assembly-CSharp.dll` and `Assembly-CSharp-firstpass.dll` match
the contract's SHA-256, a body that differs can only be Moono's rewrite at
load. A newer build's files cannot match, so a differing body there could be
Moono's rewrite or a change of the game, and nothing tells the two apart
without a canonical form that maps Moono's rewrites back, which the mod does
not have. Accepting it would be weaker than either check on its own.

The key path still applies the build-number reading: on a newer game,
`GameContract verify --by-key` (and the Mono control's key path) forgives the
same entries as the token path, without Moono's tolerances. In the game, a
Moono player on a newer build gets the refusal before any check runs. The
load guard follows the same rule: it accepts a target's build-number reading
only on Unity's player.

## Refusals

Each refusal has its own text (`ModAdmission`, unit controls in
`tools/DistributionTests`). Only the first of the build refusals says that an
update is needed; every notice the player cannot resolve alone says where to
report it.

| Situation | Behaviour |
| --- | --- |
| Not Windows x64 or Linux x64 (macOS, 32-bit) | Nothing patched; main-menu notice; ONI unchanged |
| The game is newer than every build this copy supports, and everything the mod uses is unchanged apart from the build number (Unity's player) | Accepted; one Player.log entry names both builds and lists the forgiven entries; other mods' patch marks are accepted and logged as on a supported build; no notice ([tolerated newer builds](#tolerated-newer-builds)) |
| The game is newer than every build this copy supports, and something the mod uses changed (also beside other mods' patches) | Nothing patched except the load guard (verified, or equal to the verified code apart from the build number); notice names both builds, says a Mixed Atmospheres update is needed and lists the first changed parts by short name |
| On [Moono](#moono-and-newer-builds), the game is newer than every build this copy supports | Nothing patched except the load guard under the same rule; notice names Moono and both builds and says a Mixed Atmospheres update is needed |
| The game is older than this copy supports, or between two supported builds | Nothing patched except the verified load guard; notice names both builds and says a Mixed Atmospheres update is needed (or, for an older game, a game update) |
| Supported build, and the only differences are [other mods' patches](#other-mods) on verified methods | No refusal: the mod starts; Player.log names the patched methods and the mods; no notice |
| Supported build number, but the game's code or `SimDLL` differs (also beside other mods' patches) | Nothing patched except the verified load guard; notice lists the first parts by short name, asks to disable other mods and verify the game's files, or to report it |
| On [Moono](#moono): the game's files are the build's, but parts the mod uses differ under that Moono release | Nothing patched except the verified load guard; notice names the parts and suggests the previous Moono release or the game without Moono |
| A save still being written by Moono's background save after 45 s | The colony is not loaded and the save is unchanged; the text says to wait until the game has finished saving |
| The mod's own files do not belong together (contract missing or of another build, native library of another build) | The same; notice asks to reinstall the mod |
| The check itself failed | The same; notice gives the error and where to report it |
| Second copy of the mod enabled | Second copy patches nothing; notice asks the player to keep one |
| A colony's gases and liquids differ from the game's (a mod that adds or changes elements turned on or off since the save) | The colony is not loaded and the save is unchanged; the text names the new, missing and changed elements and says to load it with the mods it was saved with or start a new colony ([details](#saves-across-builds)) |
| Frosty, Bionic, Prehistoric or Aquatic content active | The mod loads, but the colony owner never takes a colony over; notice; the colony keeps ONI's simulation. A mixture save is not loaded while such a pack is active. This is the release behaviour (user decision 2026-10-27: the first release supports the base game and Spaced Out! only). |

## Commands

```sh
# Written by both packagers; by hand for a built mod folder:
dotnet run --project tools/GameContract -c Release -- generate --mod DIR \
  --game windows-x64=WINDOWS_ROOT --game linux-x64=LINUX_ROOT --output DIR/game-contract.json
dotnet run --project tools/GameContract -c Release -- verify   --mod DIR --game linux-x64=ROOT --contract DIR/game-contract.json
dotnet run --project tools/GameContract -c Release -- controls --mod DIR --game linux-x64=ROOT --contract DIR/game-contract.json
# Moono's mode (docs above): by key, the game assemblies pinned by the files of --files-of
dotnet run --project tools/GameContract -c Release -- verify   --mod DIR --game linux-x64=ROOT --contract DIR/game-contract.json \
  --by-key [--files-of ORIGINAL_ROOT] [--mod-assembly REWRITTEN_DLL]
python3 tools/game_mono_contract_check.py --mod DIR [--managed-game WINDOWS_ROOT --platform windows-x64]
python3 tools/game_mono_contract_check.py --mod DIR --other-mod           # control: a mod that patched first is accepted
python3 tools/game_mono_contract_check.py --mod DIR --other-mod removed   # control: a patch removed again refuses
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
| A stand-in for another mod (Mono control, and a mod folder for the game) | `tools/GameContractOtherMod` |
| Refusal texts, one per cause | `ModAdmission.cs` (`GameRefusal`), chosen in `GameBuildCheck.cs` |
| Normalised identity | `tools/oni_hook_profiles.py`, `native/src/oni_image_identity.hpp` |
| Profile selection | `native/src/oni_backend.cpp` (`NativeProfile`, `verify_files`) |
| Saves across builds | `ColonySaveEnvelope.Parse(..., earliest, latest, ...)`, `ColonyOwnerPersistence.cs` |
| What differs in a colony's gases and liquids | `ColonyCatalogueDifference.cs`, `ModAdmission.CatalogueLoadRefusal`, controls in `tools/DistributionTests` |
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
