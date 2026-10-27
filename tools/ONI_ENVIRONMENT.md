# ONI environment and licensed-game setup

This page covers the private licensed Oxygen Not Included (ONI) installations
the project builds and tests against. It says where they are, which build they
are pinned to, how to inspect them, and how to load a development mod safely.
The Linux test desktop is in [ONI_HEADLESS.md](ONI_HEADLESS.md). The inspected
native boundary is in [the interop contract](../docs/interop-contract.md).

## Licensing and safety rules

- Licensed and game-derived files never enter the repository or a mod package.
  That covers game binaries and assemblies, `SimDLL.pdb`, decompiled sources,
  API or PDB dumps, contract exports (`game-contract-local`, marked
  `DO-NOT-REDISTRIBUTE.txt`), colony saves, and logs or reports from game runs.
  Keep them in the ignored `local/` and `artifacts/` directories or outside the
  tree.
- Projects reference game assemblies in place (`Private=false`). Packages hold
  only mod-owned assemblies, assets and metadata.
- Steam license checks are never bypassed. Use the user's own signed-in Steam
  client. Never extract, copy or pass credentials, session files or tokens, and
  stop at any interactive authentication step.
- Public release and Workshop upload require separate authorization.
- Test only in a fresh game process with disposable new colonies. Never open or
  change the user's colonies or saves. Back up `mods/mods.json` and
  `kplayerprefs.yaml` before changing them, and restore them byte for byte.

## Installations and pinned build

| Copy | Location | Use |
| --- | --- | --- |
| Windows Steam install | `D:\SteamLibrary\steamapps\common\OxygenNotIncluded` (Windows machine) | Windows builds and Unity runs |
| Copy of the Windows install | `/mnt/data/share/oni/OxygenNotIncluded` | Inspecting Windows files from Linux |
| Linux depot copy | `/mnt/data/share/oni/OxygenNotIncluded-linux` | Linux builds; `oni_headless.py` default |
| Linux Steam install | `<state>/steam-home/.local/share/Steam/steamapps/common/OxygenNotIncluded` | Updates with Steam; see [ONI_HEADLESS.md](ONI_HEADLESS.md) |

`/mnt/data/share` is the Linux host's Samba share, `\\192.168.1.24\share` from
Windows. Pass the game root to builds as `ONI_GAME_DIR` or `-p:OniGameDir`.
`tools/build.py --game-dir` passes both.

Game-facing code is pinned to ONI build **744825** (shown in game as U59-744825,
[released 2026-07-28](https://kleiforums.com/game-updates/oni-alpha/744825-r2776/)),
Unity 6000.3.5f2 and Harmony 2.4.2.0. A newer game is rejected until it has been
inspected. Steam BuildID 24423041 is a separate identifier. The adapter targets
.NET Standard 2.1, which Klei's
[March 2026 update](https://kleiforums.com/game-updates/oni-alpha/719533-r2701/)
recommends.

| File (under `OxygenNotIncluded_Data/`) | Windows x64 SHA256 | Linux x64 SHA256 |
| --- | --- | --- |
| Native simulator (`Plugins/x86_64/SimDLL.dll`, `Plugins/libSimDLL.so`) | `031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727` | `85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d` |
| `Managed/Assembly-CSharp.dll` | `a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599` | `6db652990ad1038e86b3493ed43004e12bfd61e62a4be60c787a9a9b9ac52ee1` |
| `Managed/Assembly-CSharp-firstpass.dll` | `72d21f51cda136443bfa1a2f6eadb08167a4c508c3d03f558b8cdb60160f249c` | `117fcc6962eeed82b4929347b9a466fb41e807a01b5a9a7acc8bdc446da276c7` |

The build number is the compiled literal in `BuildWatermark.GetBuildText`. The
Unity version comes from `globalgamemanagers`. Windows ships a matching
`SimDLL.pdb` and bundles Mono. The Linux library is an x86-64 ELF with the same
`SIM_*` exports.

## Machines and toolchains

- **Linux host:** Ubuntu 26.04.1 x64 with .NET SDK 10.0.112. ILSpy CLI
  11.1.0.9782 is installed for inspection only at
  `/home/henry/.local/share/oni-mod-tools/ilspycmd`.
- **Windows machine:** SSH key login as `henry@192.168.1.202` opens WSL2, with
  Windows PowerShell interop. The endpoint has refused connections at times.
  WSL has no compiler, CMake or .NET of its own. Windows has VS BuildTools 18
  (MSVC 19.51) with CMake and Ninja. Its system .NET SDK is 9.0.318, so use
  `C:\Users\henry\.dotnet\sdk10\dotnet.exe` for .NET 10.
- **Windows native builds:** run `VC\Auxiliary\Build\vcvars64.bat`, then
  `python tools/build.py` (or CMake with Ninja, Release). Build from a fresh
  local staging copy whose source hashes match the repository. A new share
  folder with forced copies avoids stale SMB metadata. Never reuse Linux
  outputs. Load managed assemblies from a local copy, because Windows may refuse
  SMB. MSVC `long double` is double precision.

## Inspecting an installation

```sh
python3 tools/inspect_oni.py
python3 tools/inspect_oni.py --game /mnt/data/share/oni/OxygenNotIncluded \
  --scan-root /mnt/data/share/oni --dump-api --native-exports
```

`inspect_oni.py` uses only the standard library, prints JSON and writes nothing.
It never launches Steam or the game. It follows `libraryfolders.vdf`, records
assembly and plugin paths, sizes, hashes, formats and Unity version, skips caches
and directory symlinks, and flags truncated scans. A scan that finds nothing
covers only the searched roots. The options are:

- `--dump-api`: uses ILSpy (found automatically, or set with `--ilspycmd`) to
  extract `Sim`, `SimMessages`, `ConduitTemperatureManager` and any
  `--api-type`, plus the build. The excerpts do not prove interop layout.
- `--native-exports`: lists exports with `nm` or `objdump -p`.
- `--player-log PATH`: extracts only the `U<number>-<build>` markers.

On the acquired install, all three API dumps and all 16 native exports are
found. [`InteropAudit`](InteropAudit/README.md) audits P/Invoke and call sites.

## Native boundary rules for tests

- The native simulator is one process-wide instance: its exports take no
  handle, and the PDB names `gSim` and `gSimData`. `headless` mode is not a
  second simulator. Never initialize a second native world inside a colony; use
  a separate process ([`NativeShadowProbe`](NativeShadowProbe/README.md)) or a
  disposable colony.
- There is no fluids-only stepping switch.
- `Game.UnsafeSim200ms` sends `NewGameFrame` straight to the extern. Most
  `SimMessages` calls also bypass `Sim.HandleMessage`, so patching that wrapper
  alone misses consumption and cell edits.
- Harmony cannot ordinarily patch extern methods
  ([edge cases](https://harmony.pardeike.net/v2/articles/patching-edgecases.html)).
  The community [simSimDLL](https://github.com/R9MX4/simSimDLL) is precedent
  only: it has one species per cell and no declared license.

## Loading a development mod on Windows

- Mods load from `C:\Users\henry\Documents\Klei\OxygenNotIncluded\mods\Dev\<mod>`.
  `KMod.Manager` derives that root from `Documents`, so moving Unity's
  persistent-data directory does not isolate it. The log is
  `C:\Users\henry\AppData\LocalLow\Klei\Oxygen Not Included\Player.log`.
- The loader allows one `KMod.UserMod2` per assembly. Its default
  `OnLoad(Harmony)` runs `PatchAll`.
- Every root `.dll` loads as managed, so the native library lives in
  `native/win-x64/` and is preloaded by absolute path.
- Every package uses the static ID `MixedAtmospheres`; the private installer and
  harnesses keep the Dev folder `MixedAtmospheres.IntegrationProbe`. Keep only
  one copy under `mods`: a second copy refuses to run and names the folder to
  remove ([distribution](../docs/distribution.md)).
- `enabledForDlc` uses the highest active content ID, falling back to `""`.
  `DlcManager.VANILLA_ID` is the empty string, never the text `"VANILLA_ID"`.
  Spaced Out! is `EXPANSION1_ID`.
- `tools/run_colony_validation.ps1` automates disposable colony runs and
  restores mods, preferences and generated-world files afterwards.

The user has accepted ONI's first-launch EULA in the Windows Steam client.

## Main-menu probe and contract export

The development package from `tools/package_probe.py` (it carries
`development.json`) writes `integration-probe.json` beside its assembly. Release
packages write nothing into the mod folder. It passes all six scoped checks in actual Unity on Windows
and on Linux: frame, snapshot, direct-message and scoped save interception, the
native mixture save/restore roundtrip, and settings serialization. It patches 55
native-message methods. It confirms the layouts `Sim.Cell` (16 bytes), frame
message (28 bytes) and `Sim.GameDataUpdate` (496 bytes, 77 fields). It always
reports `AuthoritativeColonySimulationReady=false`, and its
missing-requirements list remains the release gate.

At the main menu, a development package exports `game-contract-local/` without initializing
the native simulator. The export holds `elements.bin` (212 records of 164
bytes), `diseases.bin`, `contract.json` and `DO-NOT-REDISTRIBUTE.txt`. The
private copy the native harnesses use is
`/mnt/data/share/oni/mod-development/game-contract-local/`. The Linux headless
`menu` gate exports the same data.

## Acquiring the Linux depot

The Linux base depot came through the signed-in Windows Steam client, using an
explicit depot instead of changing the installed app's platform:

```text
"C:\Program Files (x86)\Steam\Steam.exe" -console +download_depot 457140 457143 3743790893999086910
```

Steam writes it to
`C:\Program Files (x86)\Steam\steamapps\content\app_457140\depot_457143`. All 935
files (3,461,332,056 bytes) were copied to
`/mnt/data/share/oni/OxygenNotIncluded-linux` on 2026-10-14. Never copy Linux
files into the Windows install or run `app_update` or a forced platform on it.
Do not send Steam console commands while another agent drives the desktop. After
a new acquisition, check the ELF architecture, `SIM_*` exports, NEEDED entries
and file hashes.

| Content | Linux depot | Manifest | Download bytes | Installed bytes |
| --- | --- | --- | --- | --- |
| Base game | 457143 | 3743790893999086910 | 1,033,721,248 | 3,461,332,056 |
| Spaced Out! | 1452493 | 7820242504201654785 | 286,864,128 | 307,638,028 |

The isolated native tests need only the base depot. Native Linux Unity needs a
Linux Steam client signed in as the same OS user, which the Windows session
cannot provide; see [ONI_HEADLESS.md](ONI_HEADLESS.md).
