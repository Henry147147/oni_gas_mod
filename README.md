# Mixed Atmospheres for Oxygen Not Included

**Development foundation, not a playable overhaul.** This repository contains a
tested native mixture model, a managed API, and an explicitly labelled ONI
integration probe. The default probe leaves colony physics unchanged. The requested
complete gas-and-liquid conversion is not ready for Workshop publication.

The target is new colonies in the base game and Spaced Out!, on Windows and
Linux. Every fluid inventory will retain the mass of every constituent. Oxygen,
nitrogen, carbon dioxide, temperature, pressure, buoyancy, phase changes,
machines, pipes, and metabolism must use the same conserved state.

## What currently works

- Per-species mass and internal-energy inventories, including nitrogen; bulk
  transfers and selective extraction retain trace constituents.
- Hard breathing mode: 15–35% O₂ by mole fraction, inclusive. Easy mode: at
  least 15%. Both also check O₂ partial pressure, CO₂, and contaminants.
- A gas-only finite-volume reference solver: pressure flow, gravity and thermal
  buoyancy, species diffusion, conduction, walls, and accounted escape to space.
- Atom-balanced example reactions with explicit work exchanges, and isolated
  water vapor/liquid and solid/liquid equilibrium calculations.
- Gas compressors, liquid pumps, and selective gas separation with explicit
  shaft work, outlet enthalpy, heat rejection, and entropy checks.
- A sparse pressure-matrix solver with impermeable boundaries, component
  validation, and independently checked convergence. Fluid integration remains
  to be implemented.
- An implicit linear acoustic pressure/velocity step, including gravity,
  incompressible volume constraints, and explicit numerical-dissipation accounting.
- A nonlinear, reversible pressure-work step for moving material volumes. It
  couples gas/liquid heat capacity, pressure, temperature and staggered kinetic
  energy while preserving entropy and accounting for gravity work.
- A shared gas/liquid bulk remap that preserves constituents, momentum and total
  energy; it retains traces, converts mixed-velocity energy to heat, and rejects
  condensed overfill before changing inputs.
- Implicit heat exchange between constant-capacity bodies with equal-and-opposite
  energy transfers, insulation, and entropy/temperature-bound checks.
- Versioned native saves with integrity and species-property checks, a C ABI,
  and a .NET Standard 2.1 managed wrapper.
- An ONI 744825 developer probe for loading the native library, scoped Harmony
  interception of direct native message calls and the managed save entry point,
  and independent mixture save/restore.
- Shared mixture names, complete trace inspection, mole-weighted ONI colours,
  density-aware opacity and composition textures, plus composition, pressure,
  oxygen partial-pressure and breathability overlays. Windows disposable
  fixtures exercise changing mixtures, paused inspection, zoom and UI scaling.
- An explicit two-reservoir ownership experiment with conserved transfers,
  native-empty storage, native meter/logic queries, mutation fences and orphan
  recovery. Windows Unity acceptance passes; native pipe/heat/power gameplay
  and owned-state save/reload remain separate work.

An integrated liquid **flow solver**, game-wide simulation ownership, building/conduit adapters,
nitrogen world generation, production presentation binding and colony save integration are still
required. Constant material properties and reaction energy bookkeeping are
reference models; they do not yet constitute full thermodynamic realism.

The gas reference solver is intentionally an explicit acoustic solver. A local
100,000-cell benchmark took about 425 ms for **0.001 simulated seconds**. This
fails the intended real-time performance gate. It must not be enabled as a
colony-wide replacement in its current form.

## Build and test

Requirements: CMake 3.20+, Ninja, a C++20 compiler, Python 3.10+, and .NET SDK 10.
On Windows, use an x64 Visual Studio developer command environment. The native
library targets 64-bit Windows and Linux; managed tests run on .NET 10, while
the game-facing libraries target .NET Standard 2.1.

```sh
python3 tools/build.py
python3 tools/build.py --native-only --sanitizers --build-dir build-sanitized
./build/ma_demo
./build/ma_demo --benchmark
```

`ma_demo output.csv` writes a small gas-mixing demonstration to CSV. CTest runs
the native physics/ABI suite; the build script also runs managed interop tests.
CI is configured for Windows/Linux standalone builds and Linux sanitizers.
An authored CI workflow is not evidence of a completed hosted CI run.
Reviewed native verification passes 57 physics/ABI scenarios, 10 closed-fluid
groups, 24 occupied-1D groups and bounded gas/vacuum consistency checks on MSVC,
strict GNU and Linux ASan/UBSan. The independent thermodynamic reference passes
26 groups. The full vacuum-front convergence gate still fails; see
[the measured limitation](native/GAS_VACUUM.md).
A separate retained material-node experiment passes 17 groups on MSVC, GNU and
ASan/UBSan. It has no remap, runtime mode or game activation; see
[the retained-state scope](native/OCCUPIED_STAGGERED.md).
Windows and isolated Linux managed checks pass Core 88, presentation 27,
owned-runtime 97, reservoir protocol 51, marker/footprint 31 and idle checkpoint
codec 19 groups.
The live idle lease pins exact acknowledged owners without permitting native
saving. Windows v26 additionally checks both complete native footprints,
inert marker serialization and exported native-section/Core envelopes;
a separate staged-file helper retains unresolved promotion outcomes.
Neither supplies integrated native save/load. Seventeen Python checks
validate the coverage inventory and discovery pipeline. Linux managed checks
do not establish Linux Unity rendering or game-scale performance.

To inspect your licensed game and compile the developer probe:

```sh
python3 tools/inspect_oni.py --game /path/to/OxygenNotIncluded --dump-api --native-exports
python3 tools/build.py --game-dir /path/to/OxygenNotIncluded
python3 tools/package_probe.py --native build/libmixed_atmospheres.so --output artifacts/probe-linux
```

For Windows, package the Windows-built `mixed_atmospheres.dll` instead. Package
into an empty directory. Game-owned DLLs are reference-only and are never
included in the package. The inspected Unity assemblies cause two MSB3277
reference-version warnings for HTTP/compression when compiling the adapter;
the adapter does not call those APIs. Its Windows and Linux Unity loading and scoped
interception checks have also passed in the actual game.

The probe belongs in the game's `mods/Dev` directory and writes
`integration-probe.json` beside its assembly. A successful probe does **not**
certify authoritative simulation replacement. Its report explicitly retains
`AuthoritativeColonySimulationReady: false`.

Steam and ONI have launched through the user's WSL/Windows machine. The user
confirmed agreement to the first-launch license, and the main-menu probe
passed in Unity: native loading, 55 native-message method patches, scoped
frames/snapshots/direct messages, mixture save/restore, and settings persistence.
No colony was loaded. The frozen verified packages use the 49-test native library.
The Windows main-menu probe also exports
private element/disease contract data for the
[isolated native harness](tools/NativeShadowProbe/README.md). That harness
verified on both Windows and Linux extraction of gas/liquid inventories before native Load, with zero
native fluid cells and continuing native solid heat conduction. This is an
import proof, not a complete colony takeover.

The locally acquired licensed installation is outside this repository at
`/mnt/data/share/oni/OxygenNotIncluded`, with the matching Linux depot at
`/mnt/data/share/oni/OxygenNotIncluded-linux`. Linux Unity passed the same six
scoped checks using a virtual display; it exited before the main menu with no
local Steam session, so the native Linux harness still uses Windows-exported
element/disease data. The Windows user's settings were restored byte-for-byte.
Detailed acquisition, Windows toolchain,
and native-boundary evidence are in [the environment report](tools/ONI_ENVIRONMENT.md).

## Implementation records

- [Physics and integration contract](docs/architecture.md)
- [Inspected native boundary and ownership gate](docs/interop-contract.md)
- [Remaining work and release gates](docs/implementation-status.md)
- [Accepted completion plan](docs/completion-plan.md)
- [Ownership inventory and atomic cluster staging](docs/ownership-coverage.md)
- [Steam Community concept draft](docs/steam-community-draft.bbcode)

Native code is under `native/`; managed code is under `managed/`. No game
binaries, decompiled game implementations, Steam credentials, or colony saves
belong in the source repository. No Workshop item has been published.
