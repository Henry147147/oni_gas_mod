# Game-update resilience

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

Result: a Klei update that leaves `SimDLL` code unchanged and does not touch
any member the mod uses is accepted **without a mod update**. This is the usual
managed-only hotfix. Any other update is refused until it is retargeted.

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
   identities, and whether they already match a supported profile. If they
   match and the managed contract verifies, nothing needs to be done.
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
  --managed-dir DIR [--write-profiles --build N]
```

`retarget_oni.py` compares against the team's 744825 copies by default
(`--reference-windows`, `--reference-linux`). It exits 0 only when nothing needs
review. `--write-profiles --build N` appends carried candidate profiles to
`docs/native-hook-profiles.json`. Review, then rebuild the native library and
run a game check before committing them. The Linux private chunk-heat location
(`CHUNK_HEAT_LOCATIONS` in `tools/oni_hook_profiles.py`) is keyed by image hash,
so a carried Linux image also needs its new hash added there.

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
  - Two real envelopes are accepted byte for byte by a later build: the prepared
    base-game save `local/saves/base-defaults-p5-v1.sav` and a `bfb1168` Spaced
    Out! manual save.
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
