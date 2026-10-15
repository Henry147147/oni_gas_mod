# ONI environment inspection

Verified on 2026-10-14: the user's existing authenticated Windows Steam client
installed ONI into `D:\SteamLibrary\steamapps\common\OxygenNotIncluded`.
The complete Windows installation was copied through the existing authenticated
Samba share `\\192.168.1.24\share` to
`/mnt/data/share/oni/OxygenNotIncluded`. No purchase, credential extraction,
login/authentication change, or system package installation was required.

The remote Windows machine is reachable through existing SSH key authentication
as `henry@192.168.1.202` into WSL2. Explicit Windows PowerShell interop works.
The Linux development host has Ubuntu 26.04.1 x64 and .NET SDK 10.0.112.
ILSpy CLI 11.1.0.9782 was installed only into
`/home/henry/.local/share/oni-mod-tools/ilspycmd`.

Actual acquired-build evidence:

- ONI build **744825**, from `BuildWatermark.GetBuildText`'s compiled uint literal.
- Unity **6000.3.5f2**, from `globalgamemanagers`.
- Harmony assembly **2.4.2.0**, from assembly metadata.
- Steam BuildID **24423041**, which is a separate identifier from the ONI build.
- Windows x64 `OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll`, with a shipped
  matching `SimDLL.pdb`. Mono runtime is bundled in `MonoBleedingEdge`.
- Game references reside in `OxygenNotIncluded_Data/Managed`, including
  `Assembly-CSharp.dll`, `Assembly-CSharp-firstpass.dll`, `0Harmony.dll`, and
  Unity modules. These licensed files stay outside the repository.

This is a Windows installation; acquisition does not establish Linux game
runtime compatibility or full simulation integration.

Run the standard-library-only discovery script:

```sh
python3 tools/inspect_oni.py
python3 tools/inspect_oni.py --game /mnt/data/share/oni/OxygenNotIncluded --scan-root /mnt/data/share/oni --dump-api --native-exports
```

Reports are JSON on stdout. The script reads installation markers and only
selected manifest fields, follows library locations from `libraryfolders.vdf`,
and records assembly/plugin paths, sizes, hashes, binary formats, and Unity
version evidence. It does not launch Steam or the game, authenticate, install
tools, or write files. It skips common cache directories and does not follow
directory symlinks; pass additional `--scan-root` values for installations in
other locations. Each root has a configurable file limit and the report flags
truncated scans. A negative scan is evidence about the searched paths, not proof
that no installation exists anywhere.

The local ILSpy cache is discovered automatically; alternatively
`--ilspycmd /path/to/ilspycmd --dump-api`
extracts declarations from `Sim`, `SimMessages`, and
`ConduitTemperatureManager`. Add repeatable `--api-type` options to choose
other types. `--dump-api` also tries to extract the ONI build from
`BuildWatermark`. The excerpts are heuristic and do not prove interop layout.
`--native-exports` invokes `nm` for ELF or `objdump -p` for PE export names.
Native symbol names alone cannot establish struct packing or callback timing.
`--player-log /path/to/Player.log` extracts only `U<number>-<build>` markers;
it does not include the log's contents. Steam manifest build IDs are different
from ONI build numbers. Inspection of the acquired installation passed: all
three managed API dumps succeeded, the build was extracted, and all 16 native
simulation/conduit/system-info exports were found. The report excluded the
debug PDB from its native-library list.

## Official version references

Klei's [March 2026 update](https://kleiforums.com/game-updates/oni-alpha/719533-r2701/)
records Unity 6000.3.5f2, Harmony 2.4.2, and recommends .NET Standard 2.1 for new
mods. The latest release found in the [Klei update index](https://kleiforums.com/game-updates/oni-alpha/)
during research was [744825, July 28, 2026](https://kleiforums.com/game-updates/oni-alpha/744825-r2776/).
These references alone do not certify installed versions; the acquired binary
evidence above independently confirms this installation. They are not a promise
about future Steam releases.

## Boundary evidence and missing proof

Current managed imports are `Sim.SIM_Initialize(GAME_MessageHandler)`,
`SIM_Shutdown()`, unsafe `SIM_HandleMessage(int, int, byte*)`,
`SIM_HandleMessages(int, int, int, byte*)`, private `SIM_BeginSave(int*, int,
int)`, private `SIM_EndSave()`, `SIM_DebugCrash()`, and system-info functions,
all from `DllImport("SimDLL")`. Native PE exports confirm these and the seven
`ConduitTemperatureManager_*` functions. Decompiled sources and PDB dumps stay
outside the repository in `/tmp/oni-*.cs` and `/tmp/oni-SimDLL-*.txt`.

`Sim.Cell` is sequential, Pack=1, with one ushort element index, mass, and
temperature; `Sim.GameDataUpdate` is sequential, Pack=4 and publishes native
array pointers plus callbacks/events. Current snapshots include backwall arrays
and material texture pointers, which older replacement layouts may omit.
`Sim.Start` and `Game.StepTheSim` assign `Grid` pointers directly from that
native snapshot. Snapshot ownership/lifetime cannot be replaced by merely
editing the managed element cache.

`Game.UnsafeSim200ms` calls `SimMessages.NewGameFrame`, then `StepTheSim`.
`NewGameFrame` sends `SimFrameManager_NewGameFrame` (-775326397) directly to
the extern entrypoint. `StepTheSim` requests `PrepareGameData` (1078620451)
through the managed `Sim.HandleMessage` wrapper, then consumes arrays/events.
Most `SimMessages` operations bypass that wrapper and call the extern directly.
Thus patching only `Sim.HandleMessage` misses consumption and cell mutations.

`AddRemoveSubstance`, `ReplaceElement`, `ReplaceAndDisplaceElement`, and
`ModifyMass` funnel to managed `ModifyCell`; its Pack=4 message includes
cell/callback IDs, temperature/mass, element index, replace type, disease data,
and displacement mode. Energy changes, consumers/emitters, cell properties,
diseases, radiation, buildings, and conduit thermals have separate messages.
Consumption/emission callbacks return one element index and actual mass,
temperature, and disease data; the game releases or retains callback handles
according to success. Matching those semantics is part of the ownership gate.

There is no exposed fluids-only stepping switch in the inspected API:
`DebugProperties` contains only building thermal scales and a debug-edit flag;
`SimSavedOptions` contains only diagonal falling-sand control. The shipped PDB
identifies pressure, liquid, temperature, disease, state-transition,
sublimation, component-update, and frame-management routines. Suppressing native
frame advancement stops shared work; post-step corrections cannot undo emitted
events. Exact native execution order still requires runtime proof.

PDB globals `gSim` and `gSimData`, together with exports lacking instance
handles, establish process-wide native state. `headless` initialization does
not provide an isolated second simulator. Do not initialize a standalone native
world inside an active colony. Use an external process for native-protocol
experiments or a dedicated disposable new colony after the ownership gate is
implemented. A main-menu probe may test the mod's independent native solver
without initializing or modifying ONI's native simulator.

[Harmony's external-method guidance](https://harmony.pardeike.net/v2/articles/patching-edgecases.html)
warns that native methods without an IL body cannot ordinarily accept prefix
or postfix patches. Managed enqueue/snapshot interception is available, but
does not independently solve simulation ownership. A public
[community replacement](https://github.com/R9MX4/simSimDLL) is precedent only:
it still models one species per cell and has no declared license. Prove
authoritative ownership and persistence in-game; successful standalone builds
are not evidence of a complete simulation replacement.

## Windows native build validation

Windows has VS BuildTools 18, MSVC 19.51.36257.0 (toolset directory
14.51.36231), bundled Ninja, MSBuild, and CMake 3.26.3. Windows .NET SDK is
9.0.318, with runtimes 6 through 10 including 10.0.12. WSL itself has no
compiler/CMake/.NET tools in its PATH.

Only `CMakeLists.txt` and `native/` were transferred to the independent
Windows staging/build directory
`C:\Users\henry\AppData\Local\Temp\codex-oni-native-20261014`.
VS18 `VC\Auxiliary\Build\vcvars64.bat` followed by CMake Ninja Release
built the current native milestone and passed **56/56** standalone scenarios,
with zero MSVC warnings. All 30 source hashes were checked on Windows before
compiling and rechecked against the repository afterward. The fresh
`native-source56-4b0e6afd` share folder and forced copies avoid stale SMB metadata.
Linux build artifacts were not reused.

The seven new material-pressure/volume-work scenarios passed on Windows. This
component is a reversible moving-volume substep coupling nonlinear ideal-gas
and incompressible-liquid equations of state, pressure work, staggered kinetic
energy and entropy. It does not implement Eulerian remap or a complete world
replacement. Earlier tests cover thermal coupling, frozen-coefficient linear
acoustics and conservative fixed-flux gas/liquid transport. MSVC `long double`
has double precision.

The native56 DLL and test executable are kept separately at
`/mnt/data/share/oni/mod-development/windows-build-native56/`. Its DLL SHA256 is
`c5c92e4af646254c6f60271b83a6867bf73ee2a84f1a64f87afaf04a480e1696`.
`windows-build-native56/build-evidence.json` records all source hashes, artifact
hashes, commands and results; compiler/test output is
`probe-evidence/native56-build-windows.log`. These are standalone native results,
with no managed or Unity rerun for native56 and no full-colony ownership claim.

The frozen native49 outputs in `windows-build/` remain unchanged. The exact
`NativeLibraryBootstrap` used by the mod passed **12/12** managed/interop/parser
scenarios on Windows .NET 10.0.12 against that native49 DLL, SHA256
`1677c0fce99e096a44cfa1ebdf86f790a2999cbbbf38e13f9389f76c49ff0c1a`.
Four transferred managed artifact hashes were verified; the DLL lived only at
`native-root/native/win-x64/mixed_atmospheres.dll`, separate from the managed
app/working directory. Tests cover absolute `LoadLibraryExW` preload/P/Invoke,
errors, snapshots, breathing/settings, strict settings JSON, disposed handles,
and native world-blob placement/species/disease/radiation preservation and
corruption/guard/backwall rejection. Evidence is
`probe-evidence/managed-json49-12-windows.log`,
`probe-evidence/native49-build-windows.log` and
`windows-build/build-evidence.json`.

The frozen json49 probe packages passed all six scoped runtime checks in actual
Windows and Linux Unity. Windows reached the main menu; Linux completed the mod
probe before exiting because no local Steam client was available. Their detailed
runtime evidence is below. These verified native49 packages were not replaced
by native56. Previous native39 and native44 outputs remain archived under
`windows-build-native39` and `windows-build-native44`.

## Developer mod loading and disposable probes

The actual Windows document directory is `C:\Users\henry\Documents`.
`KMod.Manager.GetDirectory()` uses `Util.RootFolder() + mods`; Windows
`RootFolder()` explicitly derives `Documents\Klei\OxygenNotIncluded`.
Changing Unity's persistent-data directory alone does not isolate that root.
Use `C:\Users\henry\Documents\Klei\OxygenNotIncluded\mods\Dev\<mod>`
for a developer probe. The existing log is
`C:\Users\henry\AppData\LocalLow\Klei\Oxygen Not Included\Player.log`.

The current loader populates `assembly`, `path`, and `mod`, allows one
`KMod.UserMod2` subclass per assembly, then invokes
`public virtual void OnLoad(HarmonyLib.Harmony harmony)`.
Its base implementation calls `harmony.PatchAll(assembly)`.
Stage only mod-owned assemblies/assets plus current metadata; do not package
game dependencies or decompiled game code.

For startup validation, launch a fresh game process to the main menu, run only
the independent probe, and exit without loading a colony. For subsequent
ownership tests, use explicitly disposable new colonies. Preserve existing
mod/profile settings and saves; do not interpret `headless` as profile or native
state isolation.

## Startup probe runtime validation

The corrected Windows package is staged in the owned developer folder
`mods\Dev\MixedAtmospheresProbe-20261014`. Its native DLL is under
`native/win-x64`; the explicit absolute preload is needed because
`KMod.DLLLoader.LoadDLLs` treats root `.dll` files as managed assemblies.

The user confirmed they had read and agreed to ONI's first-launch EULA. Steam
was already open in interactive Session 1. The first Steam run displayed the
EULA; accepting it through the visible Steam UI advanced AppID 457140 through
`CreatingProcess`, `WaitingGameWindow`, and `Completed`. A fresh Steam-launched
ONI process reached the main menu. The EULA is now accepted in Steam.

The developer probe appeared in ONI's Mods screen. For the current Spaced
Out! DLC selection, only `MixedAtmospheres.IntegrationProbe` was temporarily
enabled; the two previously selected DLC mods, `Super Duplicant` and `No
Critter Overcrowding`, were temporarily disabled. The Mods screen confirmed
those three changes and requested a restart. After restart, the game returned
to the main menu; no colony was loaded. The probe log confirms it was loaded
and `OnLoad` completed without an exception:

```
[Mixed Atmospheres] Integration probe completed. Full simulation ownership is NOT certified; this package does not change colony physics.
```

`integration-probe.json` reports `WindowsPlayer`, ONI build 744825, Unity
6000.3.5f2, 64-bit pointers, `Sim.Cell` size 16, frame message size 28,
`GameDataUpdate` size 496 with 77 fields, and 53 native-message methods
patched. Managed frame, snapshot, direct-message, and save interceptions all
passed, and the native mixture save/restore roundtrip passed. The report
correctly keeps `AuthoritativeColonySimulationReady=false`; its missing
requirements list remains the release gate. This is main-menu integration
evidence, not evidence of authoritative colony physics.

Captured evidence is on the existing share at
`/mnt/data/share/oni/mod-development/probe-evidence/`:
`integration-probe-WindowsPlayer-20261014.json`,
`Player-main-menu-probe.log`, `Player-steam-launch.log`, and
`mods.json.probe-enabled`. The during-test mod file has only the probe selected
for Spaced Out! (`enabledForDlc: ["EXPANSION1_ID"]`); every other mod has no
enabled DLC entries. The original
`mods.json` was restored with SHA-256
`E2BE0EEA698BE5C0E7C9AFFE9A41EDA1B50CAF21A4487857838288ABBCACA3EE`, and the
original `kplayerprefs.yaml` with SHA-256
`AA53CCA2204AA9E231BC8EB78D512DC8E9CB48B76AFD8EFD163680538AF1D4`.
Both restored files match their pre-launch backups exactly. The test-owned ONI
process was exited from the main menu; Steam remains open in Session 1. No
existing colony or save was opened or changed.

For future temporary probe enablement, current `DlcManager.VANILLA_ID` is the
empty string `""`; `enabledForDlc` checks the highest active content ID, falling
back to `""`. The literal string `"VANILLA_ID"` is not a valid enablement value.
Keep this distinction separate from deprecated metadata's historical names.

## Native game-table contract capture follow-up

A second isolated WindowsPlayer main-menu run used the private
`MixedAtmospheresProbe-contract` package. The earlier probe folder was moved
outside the mods tree for this run because both packages share the same
`staticID`; the contract package then loaded alone. ONI reached `-- MAIN MENU
--`, the integration probe completed, and the contract capture logged
`Native contract captured without native initialization`. No colony was
loaded and the game-native simulator was not initialized a second time.

The private export is at
`/mnt/data/share/oni/mod-development/game-contract-local/`, with its
`DO-NOT-REDISTRIBUTE.txt` marker. It contains `elements.bin` (69,544 bytes),
`diseases.bin` (31,220 bytes), and `contract.json` (13,925 bytes). The metadata
reports 212 element records of 164 bytes each for ONI build 744825. Keep these
game-derived payloads on the share; they are not repository or package assets.
The run's `integration-probe.json`, Player.log, and enabled-mod snapshot are in
`/mnt/data/share/oni/mod-development/probe-evidence/` under the
`*-contract*` names. The report indicates 55 patched native-message methods
and retains `AuthoritativeColonySimulationReady=false`.

After the game process exited, the immediate pre-second-run settings were
restored byte-for-byte: `mods.json` SHA-256
`E2BE0EEA698BE5C0E7C9AFFE9A41EDA1B50CAF21A4487857838288ABBCACA3EE` and
`kplayerprefs.yaml` SHA-256
`D1259BD87508FD6C8A9864C2DB973562A5D3E1513BFB48D3C74D8C63C4C94814`.
That preference snapshot records 2560×1440. The earlier first-run backup had
3440 width and SHA-256
`AA53CCA2204AA9E231BC8EB78D512DC8E9CB48B76AFD8EFD163680538AF1D4`; the
intervening change's source is unknown, so the later pre-second-run value was
preserved. The first probe folder is back in `mods\Dev`, the contract package
remains staged there, Steam remains open, and only the test-owned ONI process
was closed.

## Verified candidate final main-menu run

The verified-candidate package was staged under the first probe's existing
folder name, with both previous task-owned probe folders moved outside the
mods tree to avoid duplicate `staticID` discovery. The deployed
`native/win-x64/mixed_atmospheres.dll` SHA-256 was
`1677c0fce99e096a44cfa1ebdf86f790a2999cbbbf38e13f9389f76c49ff0c1a`, matching
the candidate manifest. ONI launched once, reached the main menu, and logged
both integration-probe completion and native contract capture without native
initialization. The strict assembly/layout check did not reject startup; the
runtime report records 55 native-message methods patched, with direct native
message, managed frame/save/snapshot interceptions, and native mixture
roundtrip all passing. `AuthoritativeColonySimulationReady` remains false.

Final-candidate integration report, Player.log, enabled-mod snapshot, and the
private `game-contract-local` export are retained under
`/mnt/data/share/oni/mod-development/probe-evidence/` and
`/mnt/data/share/oni/mod-development/game-contract-local-final-candidate/`.
The immediate pre-run configuration backups were restored exactly afterward:
`mods.json` SHA-256
`E2BE0EEA698BE5C0E7C9AFFE9A41EDA1B50CAF21A4487857838288ABBCACA3EE` and
`kplayerprefs.yaml` SHA-256
`D1259BD87508FD6C8A9864C2DB973562A5D3E1513BFB48D3C74D8C63C4C94814`.
Both prior probe folders were restored to `mods\Dev`; ONI exited from the
main menu and Steam remains open. No colony was loaded.

## Task probe cleanup

After the final verification run, the two task-created folders
`MixedAtmospheresProbe-20261014` and `MixedAtmospheresProbe-contract-20261014`
were removed from the scanned mods tree and archived intact at
`C:\Users\henry\Documents\Klei\OxygenNotIncluded\mod-development-archive\20261014-task-probes`.
Their metadata identifies both as this task's integration probe and gives
each the same `MixedAtmospheres.IntegrationProbe` static ID. The archive
contains 20 files (391,235 bytes), including the contract folder's
game-derived files and redistribution marker. No probe folder matching those
names remains below `mods`; the unrelated `NoCritterCram` developer folder
remains in place.

`mods\mods.json` contained no `MixedAtmospheres` entry before cleanup, so it
was left untouched. Its SHA-256 remained
`e2be0eea698be5c0e7c9affe9a41eda1b50caf21a4487857838288abbcaca3ee` before
and after. `kplayerprefs.yaml` was not modified and remained at SHA-256
`d1259bd87508fd6c8a9864c2db973562a5d3e1513bfb48d3c74d8c63c4c94814`.
No ONI relaunch occurred; Steam remains open.

## Windows JSON49 settings-codec verification

The private `MixedAtmospheresProbe-windows-json49` package was staged as the
only task-owned probe under
`mods\Dev\MixedAtmospheresProbe-json49-20261014`. Its deployed
`native/win-x64/mixed_atmospheres.dll` matched SHA-256
`1677c0fce99e096a44cfa1ebdf86f790a2999cbbbf38e13f9389f76c49ff0c1a`. Steam
launched ONI build 744825; `Player.log` records `-- MAIN MENU --` followed by
probe completion and native contract capture without native initialization.
No colony was opened. The WindowsPlayer report confirms all six checks passed,
including `SettingsSerializationRoundtripPassed=true`, with 55 native-message
methods patched and `AuthoritativeColonySimulationReady=false`.

Evidence is at
`/mnt/data/share/oni/mod-development/probe-evidence/`:
`integration-probe-windows-json49.json` (1,204 bytes, SHA-256
`9543effc5dd828c3feacc76192b8eb97677d7080d3e79b9d6a49e95ea4abada7`),
`Player-main-menu-json49.log` (24,689 bytes, SHA-256
`f583e93f7467d3de5a669ae0739611914941ae5e986c809baac6f2905b4602d8`), and
the enabled-mod snapshot `mods.json.json49-enabled`. The private game-derived
payload is in
`/mnt/data/share/oni/mod-development/game-contract-local-windows-json49/`:
`elements.bin` 69,544 bytes, `diseases.bin` 31,220 bytes, `contract.json`
24,162 bytes, and its `DO-NOT-REDISTRIBUTE.txt` marker. These payloads remain
outside the repository and mod package.

After exiting only the test-owned ONI process, the probe folder was archived
intact at
`C:\Users\henry\Documents\Klei\OxygenNotIncluded\mod-development-archive\json49-20261014\MixedAtmospheresProbe-json49-20261014`.
No Mixed Atmospheres folder remains under `mods`. The immediate pre-run
`mods\mods.json` backup was restored byte-for-byte (before and after SHA-256
`e2be0eea698be5c0e7c9affe9a41eda1b50caf21a4487857838288abbcaca3ee`), as was
`kplayerprefs.yaml` (before and after SHA-256
`d1259bd87508fd6c8a9864c2db973562a5d3e1513bfb48d3c74d8c63c4c94814`).
Steam remains open; no ONI process remains.

## Linux depot acquisition route (read-only research)

The logged-in Windows Steam client's cached public app metadata was inspected
without reading account/session credentials or printing product-access tokens.
`C:\Program Files (x86)\Steam\appcache\appinfo.vdf` records ONI public Steam
BuildID **24423041**, matching the acquired Windows reference. Its app entry was
last updated September 25, 2026; this is cached reference metadata, not a claim
that the backend cannot change afterward.

| Content | Linux depot | Cached public manifest | Download bytes | Installed bytes |
| --- | --- | --- | --- | --- |
| Base game | 457143 | 3743790893999086910 | 1,033,721,248 | 3,461,332,056 |
| Spaced Out! | 1452493 | 7820242504201654785 | 286,864,128 | 307,638,028 |

The base depot should supply the Linux native simulation library and managed
assemblies; the DLC content depot is unnecessary for an initial isolated SimDLL
contract test. That file inventory has not yet been acquired or verified.

The installed `steamui.dll` contains help for
`download_depot <appid> <depotid> [<target manifestid>] [<delta manifestid>]`.
The installed `steamclient64.dll` contains the destination format
`%s\steamapps\content\app_%u\depot_%u`. Thus the smallest proposed licensed
acquisition uses the already authenticated desktop Steam client and an explicit
Linux depot, rather than changing the installed app's platform or logging into
another client:

```text
"C:\Program Files (x86)\Steam\Steam.exe" -console +download_depot 457140 457143 3743790893999086910
```

Expected separate output is
`C:\Program Files (x86)\Steam\steamapps\content\app_457140\depot_457143`.
It preserves the Windows game at
`D:\SteamLibrary\steamapps\common\OxygenNotIncluded`; never copy the Linux
files into that directory or run app_update/force-platform against it. The
Linux depot output and its cached manifest did not exist at inspection time.
No download command was issued during this read-only research. Actual account
entitlement and manifest availability still need confirmation from a real
client download; stop at any actual interactive authentication requirement.
Do not dispatch the console command while the launcher agent owns the desktop.

Once the launcher is idle and the acquisition step proceeds, copy the
result to a separate `/mnt/data/share/oni/OxygenNotIncluded-linux` directory.
For the smallest initial transfer, locate `*SimDLL*.so*` and its adjacent native
libraries plus managed assemblies/build metadata; retain the full depot as the
reference if possible. Confirm ELF architecture, exported SIM entrypoints and
NEEDED dependencies with readelf/objdump, confirm the game build and assembly
hashes, then run the original native module in a disposable Linux process.
Windows layout and element-table agreement must be tested, not assumed. No
Unity graphical launch is required for those isolated interface tests, but they
do not establish complete Linux game integration.

The [Klei-hosted community depot guide](https://kleiforums.com/forums/topic/131909-how-to-downgrade-game-in-steaminstruction/)
identifies the same Linux depot. The command syntax and output path above are
supported more directly by the installed Steam client's own static help/path
strings. [ValvePython's appcache reader](https://github.com/ValvePython/steam/blob/master/steam/utils/appcache.py)
is community reverse-engineering context; its documented older cache versions
are not sufficient for this client's version29 string-table format. The local
inspection used an original read-only version29 parser and emitted only ONI
branch/depot metadata.

## Linux base depot acquisition (completed)

On 2026-10-14 the authenticated Steam desktop client downloaded only AppID
457140's Linux base depot 457143 at manifest 3743790893999086910. Steam's
console log records completion at
`C:\Program Files (x86)\Steam\steamapps\content\app_457140\depot_457143`;
the content log records manifest retrieval, CDN authentication, and transfer
progress. The transfer was 1,033,721,248 bytes and staged to 3,461,332,056
bytes. The complete depot (935 files; 3,461,332,056 bytes) was copied to
`/mnt/data/share/oni/OxygenNotIncluded-linux`, a separate share path. The
source and copied target hashes and file sizes matched for both key files.

| Linux depot file | Size | SHA-256 |
| --- | ---: | --- |
| `OxygenNotIncluded_Data/Plugins/libSimDLL.so` | 783,384 | `85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d` |
| `OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll` | 14,072,832 | `6db652990ad1038e86b3493ed43004e12bfd61e62a4be60c787a9a9b9ac52ee1` |

The native library is ELF x86-64, SONAME `libSimDLL.so`, BuildID
`95798440c484c3120f4cc6fbedb173358ffdde0e`, and exports the expected `SIM_*`
entrypoints. The depot's `globalgamemanagers` identifies Unity 6000.3.5f2.
The managed assembly identity is `Assembly-CSharp`, AssemblyVersion 0.0.0.0;
it has no file/product version resource. `Assembly-CSharp-firstpass.dll` SHA-256
is `117fcc6962eeed82b4929347b9a466fb41e807a01b5a9a7acc8bdc446da276c7`.
Steam's cached app BuildID is 24423041 and the Windows UI previously reported
ONI build 059-744825-5; the Linux manifest is pinned above, and its
platform-specific `Assembly-CSharp.dll` hash differs from Windows.

Evidence files are in
`/mnt/data/share/oni/mod-development/probe-evidence/`: Steam console/content
logs and `linux-depot-acquisition.txt`. Steam remains open. No ONI process was
launched for this acquisition, and no app update or overwrite of the Windows
installation at `D:\SteamLibrary\steamapps\common\OxygenNotIncluded` occurred.

## Actual Linux Unity scoped probe (serializer compatibility fix)

The fresh immutable package `MixedAtmospheresProbe-linux-json49` passed its
actual Unity6000.3.5f2 LinuxPlayer scoped probe. The runtime report confirms
Cell16, frame28 and GameDataUpdate496/77fields, **55** patched native-message
methods, and all six flags: managed frame, snapshot, direct message, scoped save,
native mixture roundtrip and settings serialization roundtrip. Full colony
ownership remains explicitly false.

The setup used a locally extracted Xvfb2:21.1.22 with existing Mesa llvmpipe,
-batchmode, a task-owned writable thin executable root, and bubblewrap read-only
host/game data with task-owned XDG config/cache/temp. No system packages or
existing host settings were changed. Null graphics had previously crashed in
native Unity; virtual graphics resolved that startup issue. No login/auth flow
or colony/save loading was attempted.

Steam was absent locally. The game reported 'Steam not initialized in time',
continued through successful probe/mod loading, then exited naturally0 before
MainMenu.OnSpawn. No Linux-generated element/disease contract capture resulted;
the independent native Linux suite still uses the earlier Windows captures.
Both owned game and virtual display are stopped. Evidence is
`/mnt/data/share/oni/mod-development/probe-evidence/linux-unity-json49/`, including
`integration-probe.json`, `Player-xvfb.log` and `runtime-evidence.json`. The earlier
serializer failure is preserved separately under `linux-unity-attempt-v1`.

The same fixed-schema settings codec and parser suite now passes **12/12** on
Windows .NET against the frozen native49 DLL; four transferred artifact hashes
were verified before execution. Native49 was not rebuilt for this managed rerun.
Neither actual Unity scoped success nor the standalone49/12 results certify the
requested full game simulation replacement.

## Reusable native Linux virtual desktop (2026-10-15)

The local Steam-client absence is now resolved by a rootless private
installation under `/home/henry/.local/share/oni-headless`; account sign-in is
still pending. [ONI_HEADLESS.md](ONI_HEADLESS.md) documents the checked-in
`oni_headless.py` setup/start/run/status/input/screenshot/stop workflow and
loopback-only noVNC viewer. Its stop/restart path and 48 Python tests pass.
No system packages or namespace/AppArmor policies were modified.

The current sources were rebuilt against the Linux assemblies, packaged with
the fresh GNU native library and launched inside actual Linux Unity. All six
scoped checks passed, with 55 patched methods and the inspected 744825 layouts.
The Steam API loaded the actual local `linux64/steamclient.so`, then rejected
the unsigned-in global user. ONI exited 2 before the main menu; the runner
correctly returned 3 (`blocked-steam-authentication`). No colony was loaded.
Evidence is `/home/henry/.local/share/oni-headless/runs/20261015T122528Z-bf9a5b`.
The UI inspection screenshot is `/tmp/oni-headless-restarted.png`.
The desktop remains at Steam sign-in for later authentication.

The existing WSL endpoint `henry@192.168.1.202:22` refused connections during
this follow-up. Independently, a remote Windows client cannot provide the
local same-OS-user Steamworks context required for native Linux ONI. This
setup did not transfer credentials or authorize any new account session.
