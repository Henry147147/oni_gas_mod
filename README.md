# Mixed Atmospheres for Oxygen Not Included

**Development foundation, not a playable overhaul.** This repository contains a
tested native mixture model, a managed API, and an explicitly labelled ONI
integration probe. The default probe leaves colony physics unchanged. The requested
complete gas-and-liquid conversion is not ready for Workshop publication.

The target is new colonies in the base game and Spaced Out!, on Windows and
Linux. Every fluid inventory will retain the mass of every constituent. Oxygen,
nitrogen, carbon dioxide, temperature, pressure, buoyancy, phase changes,
machines, pipes, and metabolism must use the same conserved state.

## How the native integration works

The production architecture uses **native SimDLL hooks** to put the mixture
engine inside ONI's simulation schedule. The custom engine will own all fluid
inventories and physics; verified native services retain solids and buildings.
Native scalar cells become compatibility projections, while machines, heat,
callbacks, rendering and saves use one completed physical frame. The mod loads
its own library and changes verified code in memory; installed SimDLL files
remain intact.

This is the next implementation track, **not an already playable takeover**.
It starts with an opt-in forwarding observer, followed by controlled takeover,
production physics, complete gameplay and persistence. Windows and native Linux
have independent ONI 744825 profiles. Earlier vacuum-shadow experiments remain
historical controls. See the [native integration contract](docs/native-backend-integration.md)
for ownership, interfaces, failures and implementation gates.

## What currently works

- A development [sealed native fluid domain](docs/sealed-native-domain.md)
  connects the closed MAC solver to ONI's native worker clock and publishes
  completed mixture readings. Its Linux compressed-water equilibrium control
  passes actual grid, renderer and save-guard checks. The corrected mixed-air
  control also passes 120 native frames after projection priming. This remains an
  opt-in development experiment with scalar saving disabled.
  Owned-cell heating/cooling messages now commit conserved external heat on the
  native tick; the real-game control verifies both energy and presentation.
  Gas point injection and selective consumption also commit on that clock,
  carrying energy and momentum and delivering actual game callbacks once.
  [Filled-water point transfers](docs/sealed-native-domain.md#filled-liquid-point-sources)
  now carry compression energy too. Licensed Linux and Windows simulation-library
  controls pass; the new Unity save/load scene is awaiting verification.
  Selective point consumers and emitters now retain native registrations while
  transferring custom physical gas; a real Unity consumer, changed rates,
  blocked/unblocked callbacks and removal pass in the Linux colony. General
  production devices and persistence remain unfinished.
  Owned actor configuration batches now commit atomically, and unrelated
  native actor batches keep their normal route; both pass in the Linux game.
  Carbon dioxide is now a physical constituent too: its real-game injection,
  consumption, transport, callbacks and energy/mass accounts pass on Linux.
  Internal fluid conduction now runs on the same native clock, with closed
  energy and entropy checks; a rendered Linux colony verifies the resulting
  heat redistribution. Fluid/solid heat exchange remains unfinished.
  An opt-in gravity scene also passes in the Linux game, retaining physical
  vertical momentum and gravitational energy carried by actual gas transfers.
  The Windows binary also passes gas, water and heat controls under Wine.
  A native MSVC package now also passes the six-cell mixed-air control in an
  actual Windows Unity colony for 120 frames after projection priming. Its
  hover matches the retained physical reading; this two-gas observation does
  not complete the four-gas hover or whole-colony gates.
  Native physical checkpoints now preserve full state and reproduce the next
  actual Linux worker frame byte for byte.
  Paired capture now saves native cells and the custom owner at the same worker
  barrier and validates their combined file in the Linux game. An actor-free
  development colony now also saves through ONI's normal serializer and loads
  in a fresh game process: its managed objects return, its initial physical
  bytes are exact, and all 120 resumed worker frames match independent
  continuation. Native startup events deliver once; grid/rendering and outside
  native heat pass. The ordinary colony save entry now captures a complete pair
  and restores the caller's exact pause count; pending commands reject without
  writing or losing their reservation. Compressed-water save, initial paused
  resave and 120 exact resumed frames also pass in Linux Unity. Owned actor
  persistence and remaining menu/day-rollover triggers remain unfinished. A fresh MSVC
  Windows package also passes compressed water, conduction, CO2, registered
  actors, gravity, and ordinary air/water save/load. Both Windows reloads
  autosave before resuming and match all 120 physical continuation frames.
  These remain six-cell development scenes. See the
  [checkpoint protocol](native/MAC_CHECKPOINT.md#paused-unity-colony-saveload).
  Windows also passes actual Save As menu creation and exact fresh-process
  continuation of that file. The runner loads a separate copy so further menu
  saves stay inside the current evidence directory.

  Save declines now release their resources and restore the prior selection through
  ONI's error UI and deferred-save cleanup; the Linux denied-write control
  resumes and produces a later verified save. Native-owned unassigned disease
  counters remain opaque in preserved world data; custom-fluid disease and
  actor persistence remain separate implementation work.

- Opt-in native SimDLL forwarding hooks with exact-build Windows/Linux profiles,
  code fingerprints, bounded traces and lifecycle fences. Six isolated native
  comparisons pass on Windows and Linux, alongside
  actual Unity colony forwarding checks on both. Windows also passes strict
  MSVC ABI and relocation controls. Registered native emitter/consumer controls
  verify mass transfers, interval resets and removal on Windows. This
  observer preserves native physics.
  See [instructions and evidence](docs/native-backend-integration.md#build-and-observe).

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
- A separate retained material-state reference with constant gravity, full
  consistent-mass momentum and finite work ledgers. Strict Windows/Linux
  gravity and zero-gravity controls pass, including tiny stored changes; earlier
  failures and explicit retests are [documented](native/CANONICAL_MATERIAL.md#current-source-validation).
  This reference is excluded from game dispatch and default builds.
- An explicit retained-coordinate material path preserves short cell widths
  and gravity work. Windows/Linux vacuum-expansion checks show decreasing
  front-position and speed errors on three fine grids, with substantial
  absolute error still remaining. EOS identity caching removes repeated
  profile serialization. These are [solver foundations](native/GAS_VACUUM.md#retained-nodal-replacement-path);
  production vacuum, remap and game ownership remain unfinished.
- A separate 2-D [MAC transport component](native/MAC_TRANSPORT.md) retains face
  momentum and derives its mass transfers from the cell transfers. Seven
  Windows/Linux groups and an independent saved-data checker pass, including
  shear/composition refinement, solid-corner reactions and trace conservation.
  An owning [closed EOS pressure/transport solver](native/MAC_CLOSED_WORLD.md)
  now also passes eight Windows/Linux groups, including elastic liquids,
  interval rollback and pressure/velocity acoustic refinement.
  [Shared material reconstruction](native/MAC_RECONSTRUCTION.md) preserves
  constituents, energy and traces while improving spatial transport. Its five
  controls and the opt-in [smooth gravity gate](native/MAC_GRAVITY.md) now pass
  on both platforms with separate saved-data reviews. Earlier failures remain
  recorded. Shocks, free surfaces, phase interfaces, production performance
  and game integration remain required.
  [Wide derived-energy accounts](native/MAC_ENERGY_TRANSPORT.md) now pass four
  additional Windows/Linux controls. [Scaled primary parcels](native/MAC_PRIMARY_PARCELS.md)
  pass five more, retaining below-range helium/passive contributions until
  accumulation. [Momentum contribution range](native/MAC_MOMENTUM_PARCELS.md)
  passes four more. [Full canonical/flux range](native/MAC_CANONICAL_RANGE.md)
  passes five more. [Bounded large integer exponents](native/MAC_EXPONENT_RANGE.md)
  pass six more and Linux ASan/UBSan checks; that Linux cohort completes Sod
  N64/128/256. [Compatible compression](native/MAC_COMPRESSION.md) now integrates
  with the owning interval. [Symmetric coupling](native/MAC_COMPRESSION_COUPLING.md)
  now passes all 66 native groups and saved reviews on Windows/Linux, including
  five owner groups and actual endpoint entropy. The latest Windows
  [strong-gas gate](native/MAC_RIEMANN.md) passes Sod and collision velocity
  refinement, but fails collision pressure refinement. Linux does not repeat
  that known failure. Earlier results and diagnostics are preserved.
  A separate [compatible momentum limiter](native/MAC_MOMENTUM_LIMITER.md) now
  passes six Windows/Linux groups and Linux ASan/UBSan checks. It preserves
  paired impulse/work accounts and nonnegative upwind remainder heat; its
  scalar-advection refinement ratios are 0.304/0.283. Subsequent
  [geometric momentum transport](native/MAC_MOMENTUM_RECONSTRUCTION.md) and
  [owning integration](native/MAC_MOMENTUM_OWNER.md) pass their Windows/Linux
  controls; the strong-flow gate remains failed. An optional
  [viscosity-matched heat component](native/MAC_MATCHED_HEAT.md) now passes all
  127 groups/reviews on both platforms and 66 Linux sanitizer groups. Its
  [owning integration](native/MAC_MATCHED_OWNER.md) now passes 133 groups/reviews
  on both platforms and 72 Linux sanitizer groups. The new strong-flow retry
  passes collision pressure refinement but still fails density refinement;
  production and game acceptance remain open.
- A versioned [coupled native/managed boundary](native/COUPLED_ABI.md) with exact
  conserved imports, immutable advances and coherent gas/liquid readings.
  Windows/Linux binding controls pass. It exposes the closed reference; game
  frame coordination and the production solver remain unfinished.
- Immutable physical cell edits now retain actual U and momentum and check
  declared external mass, energy, impulse and metadata accounts. Windows/Linux
  boundary and numerical controls pass. This [transaction preparation](native/COUPLED_ABI.md#conserved-physical-mutation-preparation)
  still needs machine process models, power reservations and game routing.
- A closed-reference colony coordinator advances every admitted world before
  publishing one immutable frame. Windows/Linux controls verify later-world
  rollback and shared attempt budgets. Physical internal energy and momentum
  stay in native owners; the read-only colony projection never resets them.
  Physical edits now commit with all-world advances, exact declared boundary
  accounts and presentation. Machine reservations, ONI hooks and persistence
  remain unfinished.
- Completed physical frames can prepare the shared mixture presentation before
  publication, retaining actual U, temperature, pressure and every constituent
  across appearance changes. The current Windows/Linux suites pass 29 coupled,
  89 Core and 41 presentation groups. A Windows game scene verifies four bulk
  gases on hover, an actual reference-owner inventory edit, a changing mixture
  heading and hazardous trace warnings;
  [scope and evidence](docs/mixture-presentation.md#completed-colony-transactions-on-windows)
  remain separate from production colony acceptance.
- Sparse disease counts and passive radiation metadata follow the coupled
  reference's material transfers and appear in its completed snapshots.
  Disease growth and radiation-dose physics remain required.
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
  Tile hover lists every non-trace gas and its mole percentage; hazardous trace
  warnings remain visible. This is an explicit [first-playable requirement](docs/completion-plan.md#d-first-playable-development-colony).
  Windows V54 passes the four-gas native hover fixture, including the trace
  cutoff and 125% UI scale. Linux hover and production colony binding remain
  required; the earlier failed UI runs remain recorded.
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
Additional Cartesian/hybrid remap experiments require the explicit
`MA_BUILD_STAGGERED_EXPERIMENTS=ON` option. Bounded geometry, instantaneous
transport, first-event and passive checks pass on MSVC/GNU/ASan, while the
unchanged strict Cartesian test still fails six of seven groups. They stay
outside the default build, runtime and game DLL; see
[the opt-in evidence contract](native/STAGGERED_EXPERIMENTS.md).
Windows and isolated Linux managed checks pass Core 88, presentation 27,
owned-runtime 97, reservoir protocol 51, marker/footprint 31 and idle checkpoint
codec 19 groups.
The live idle lease pins exact acknowledged owners without permitting native
saving. Windows v26 additionally checks both complete native footprints,
inert marker serialization and exported native-section/Core envelopes;
a separate staged-file helper retains unresolved promotion outcomes.
Neither supplies integrated native save/load. The Linux Python suite passes
113 checks for the coverage inventory, discovery, experimental evidence pipeline
and headless runner; Windows executes 79 checks and skips the Linux-only module.
Linux managed checks
do not establish Linux Unity rendering or game-scale performance.

On 2026-10-15 a clean native Linux (GCC 15) rebuild re-ran every configuration:
Release, ASan/UBSan and the opt-in experiments all match the counts above, and
the two documented failures (strict Cartesian 6/7, vacuum-front gate) reproduce
exactly. Every managed test project passes against the Linux game declarations
with the same counts as Windows, plus a seventh nitrogen registration check.
The shipped Linux library now exports only the 15 `ma_*` entry points and links
libstdc++ statically; built on this host it requires glibc 2.38 or newer, so
release binaries for older distributions must be built on the oldest supported
glibc. The [Ubuntu 22.04 compatibility builder](docs/linux-native-build.md) now
passes all 13 native tests and 89 managed/native ABI checks with both libraries
requiring at most glibc 2.34. It records frozen source and binary provenance;
normal real-game and release gates still apply.

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

A repeatable [native Linux headless playtest runner](tools/ONI_HEADLESS.md) is
now installed on this host. It provides a private rendered virtual desktop,
Steam sign-in, isolated disposable profiles, UI input and captured runtime
evidence. On 2026-10-16, with the private Steam client signed in and ONI
installed through it, the Linux game passed the `menu` gate (six probe checks,
main menu, contract export) and the full `presentation` gate in a disposable
Spaced Out! colony: 34 Unity captures, 18 exact texture-write proofs,
container, nitrogen and owned-reservoir fixtures, with no mod error lines.
Native ONI still owns colony physics there. The runner fails a menu run on any
mod-originated error after the main-menu marker and lets
the game see the running Steam client. The adapter's `OnLoad` removes its own
Harmony patches on any failure, and the nitrogen elements register only while the
adapter supplies their substance table, so a failed load leaves no partial state.
The Windows Steam session cannot supply the Linux game's local client context.

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
