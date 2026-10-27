# Distribution

This page covers how Mixed Atmospheres is packaged for players: the Steam
Workshop release folder, the mod's identity, where it keeps player data, and
what it refuses to run on. How game updates are verified is in
[game-update resilience](game-update-resilience.md). Nothing is published
without the user's authorization, and no licence file is added until the user
chooses one.

## Packages

| Package | Tool | For | Shape |
| --- | --- | --- | --- |
| Release | `tools/package_release.py` | Steam Workshop and manual installs | One folder for Windows x64 and Linux x64, plus a deterministic ZIP; no installer |
| Development probe | `tools/package_probe.py` | Harnesses and private playtests | One platform; carries `development.json` |
| Private Windows ZIP | `tools/package_windows.py` | The user's Windows playtest | A probe package plus `install_windows.ps1` |

### Release folder

```sh
python3 tools/package_release.py \
  --managed-linux build-managed-linux/bin/MixedAtmospheres.Oni/release \
  --managed-windows build-managed-windows/bin/MixedAtmospheres.Oni/release \
  --linux-native build-linux-ubuntu2204/native \
  --linux-build-info build-linux-ubuntu2204/compatibility-build.json \
  --windows-native build-windows \
  --output artifacts/release/MixedAtmospheres --zip artifacts/release/MixedAtmospheres.zip
```

The folder is what the Workshop serves into `mods/Steam/<id>`:

- `MixedAtmospheres.Oni.dll` and `MixedAtmospheres.Core.dll` at the root. ONI
  loads every root DLL as managed code, so nothing else sits there.
- `game-contract.json`: the managed game contract of exactly these DLLs, for
  both platforms ([game-update resilience](game-update-resilience.md)).
- `native/win-x64/` and `native/linux-x64/`: the solver and the game backend
  for each platform. The Linux pair comes from the Ubuntu 22.04 compatibility
  build, whose report must show the glibc 2.34 ceiling
  ([Linux build](linux-native-build.md)). The Windows pair comes from the MinGW
  cross-build ([Windows validation](windows-validation.md)).
- `third-party/`: every native dependency's licence (the union of both
  builds; a licence that differs between them is refused), the source
  pointers, the patch scripts and the patched funchook/diStorm sources as a
  deterministic archive.
- `mod.yaml`, `mod_info.yaml`, a player `README.txt`, an optional
  `preview.png` (`--preview`), `native-build-provenance.json`, and
  `package-manifest.json` with the SHA-256 of every file.

The managed DLLs must be built twice, once against each platform's game
assemblies. The packager requires the two builds to be identical apart from
the build identity that deterministic compilation derives from its inputs: the
COFF timestamp, the module version ID and the debug directory. It ships the
Linux-referenced build. On the current sources the two builds differ in
exactly those 72 bytes.

A release never contains `development.json`, validation requests or reports,
`game-contract-local`, `private-validation`, the coupled reference libraries,
or the installer. The packager refuses uncommitted sources unless
`--allow-dirty` is given, and then records the state in the manifest. The ZIP
holds the folder as `MixedAtmospheres/`, in sorted order, with fixed dates and
modes, so identical inputs give identical bytes. Tests are in
`tools/tests/test_release_packaging.py`.

The Windows build links the MinGW-w64 C runtime, winpthreads and GCC's libgcc
and libstdc++ statically. The packager copies their notices from the
toolchain's packages (`--mingw-root`, default `MA_MINGW_ROOT` or the team's
`build-oni-cross-tools/root/usr`) into `third-party/licenses/mingw-w64-runtime`
and `gcc-runtime`. winpthreads' licence requires its notice to ship with the
binaries.

## Identity

| Item | Value | Effect |
| --- | --- | --- |
| `staticID` | `MixedAtmospheres` (every package) | Harmony owner ID and Mods-screen lookup only |
| Title | Mixed Atmospheres | Mods list |
| Version | the managed project's `<Version>` | DLL version and `mod_info.yaml`; probe packages add `-probe` |
| `minimumSupportedBuild` | 744825 | The earliest verified game build |
| Private Dev folder | `mods/Dev/MixedAtmospheres.IntegrationProbe` | Unchanged, so the user's `mods.json` entry stays enabled |

All of these come from `tools/mod_identity.py`. Inspection of ONI 744825's
`KMod` shows why the final `staticID` changes no player data:

- Saves record the active mods as labels (platform plus folder name or Workshop
  ID) and compare labels on load. A mismatch only logs a warning and lists the
  difference in the Mods screen; the colony still loads. A colony saved with
  the Dev copy therefore loads with the Workshop copy, and the reverse.
- `mods.json` entries are matched by label; the `staticID` stored there is
  replaced by `mod.yaml`'s on every scan.
- Settings and presets live in `mods/config/MixedAtmospheres` in the ONI user
  folder, independent of every mod identity.

The development harnesses keep their Dev folder name and `mods.json` label.
Their stored `staticID` text is cosmetic, and their failure detection matches
the `MixedAtmospheres` prefix.

## Player data

Ordinary play writes nothing into the mod's folder. The player's settings
(`config.json`, `presets/`) and diffusion options are in
`mods/config/MixedAtmospheres`. The integration report goes to `Player.log`
only.

Development packages (`development.json`) and validation runs
(`colony-validation.json`) keep their existing outputs beside the mod, where the
harnesses read them: `integration-probe.json`, the main-menu
`game-contract-local` export and `game-contract-local/colony-validation`. The
private Windows ZIP wraps a development package and keeps that behaviour.

## Refusals

Each refusal is shown in the game in plain words, and is also logged:

- **Platform.** On anything but 64-bit Windows or Linux (macOS, 32-bit, ARM),
  `Mod.OnLoad` returns before any patch or native library is loaded. A
  main-menu dialog, which needs no Harmony patch, says the game is unchanged.
- **Second copy.** ONI loads every enabled copy. The first copy claims the
  process; any other copy installs nothing. The dialog names both folders and
  which one to delete: it keeps a Workshop copy over a Dev or Local one,
  otherwise the copy that loaded first.
- **Gameplay packs.** The release supports the base game and Spaced Out! only
  (user decision 2026-10-27). With the Frosty, Bionic, Prehistoric or Aquatic
  pack, or any unknown non-cosmetic pack, active:
  - the main menu explains this;
  - a new colony keeps ONI's own gases and liquids and shows a notice;
  - a Mixed Atmospheres save is refused before ONI leaves the menu, whether it
    is loaded from the load screen or with the Resume button. A dialog tells
    the player to turn the pack off; the save is not changed.

  Cosmetic packs are allowed.

The decisions are pure functions in `ModAdmission.cs` with unit controls in
`tools/DistributionTests`, because macOS and the other packs are not installed
here.
