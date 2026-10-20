# Configured sealed native domain

The configured domain is the first native slice of
[gate 1](playtesting-readiness.md), colony fluid ownership. An additive ABI lets
the [sealed native owner](sealed-native-domain.md) take explicit physics,
interior solids and larger rectangles. Scenarios 0–6, their checkpoint bytes
and every existing entry point are unchanged. A managed adapter and 7×5
Unity rooms with interior solids (gas and water) use it and pass in the native
Linux game ([managed adapter](#managed-adapter-and-unity-scenes)), including
colony saves, fresh-process loads and an 8,401-frame long run
([saves and reloads](#saves-and-reloads)). They are bounded development rooms,
not colony ownership.

## ABI

`ma_oni_domain_configured_abi_version()` returns 1. A configured session uses
`MAOniDomainConfig.scenario` 7 (`MA_ONI_DOMAIN_CONFIGURED_SCENARIO`) with the
existing sealed opt-in flag 1. It also supplies a 40-byte `MAOniDomainPhysics`
record and a row-major solid mask. Only `ma_oni_domain_start_configured`,
`ma_oni_domain_restore_configured` and
`ma_oni_domain_configured_checkpoint_validate` admit scenario 7; the legacy
start, restore and validation entry points reject it.
`ma_oni_domain_configuration_copy` returns the active session's exact geometry,
physics and mask; a null mask with zero capacity queries the count. Copies,
heat, transfers, actors, snapshots, frame inputs, paired capture and
diagnostics use the existing shared calls.

| Flag | Value | Effect |
| --- | ---: | --- |
| `GRAVITY` | 1 | `0 < g <= 100` m/s²; otherwise `g` must be exactly +0 |
| `CONDUCTION` | 2 | `MacClosedSettings.thermal_conduction` |
| `VISCOSITY` | 4 | shear and bulk `0..1000` Pa s, at least one positive; otherwise both exactly +0 |
| `MIXTURE` | 8 | occupied gas/liquid admission, transfer topology and EOS liquid fraction of scenarios 4–6 |
| `LIQUID` | 16 | without `MIXTURE`, single-liquid (water) admission; neither flag is gas-only |
| `VACUUM` | 32 | production void phase and sharp interfaces; empty fluid cells and partly filled water surfaces |
| `UNRESOLVED_GAS` | 64 | requires `VACUUM`; explicitly enables audited unresolved gas expansion, saved in MAC checkpoint version 12 |

`MIXTURE|LIQUID`, unknown bits, nonzero reserved words and other struct sizes
or versions reject. The registry and EOS are those of scenarios 0–2 and 4–6
(`oni-sealed-domain-v2`, the existing five-species catalogue). The maximum
timestep stays 0.01 s. The numerical ranges are admission bounds, not material
laws. A configured start requires a complete `MAOniDomainInitialCell` inventory;
there is no synthetic fixture.

Each pair below is byte-identical when started from the same cells: 0 is
`CONDUCTION`; 1 is `CONDUCTION|LIQUID`; 2 is `GRAVITY` 9.81 with
`CONDUCTION`; 4 is `MIXTURE`; 5 is `MIXTURE|CONDUCTION`; 6 is
`MIXTURE|CONDUCTION|VISCOSITY` with 0.5 / 0.25 Pa s. Scenario 3 has no
equivalent: it shifts water's reference energy into a separate EOS profile
([filled-liquid gravity](native-domain-physics.md#filled-liquid-gravity)).
Configured liquid gravity uses the unshifted v2 reference.

## Solids, targets and copies

The mask byte is 0 for fluid and 1 for solid. It covers `width*height` cells
and keeps at least one fluid cell. Solids become MAC walls, and their initial
records must be all zero, including temperature. Occupied fluid cells need a
positive finite temperature and admitted constituents. `VACUUM` also admits
all-zero masses (finite nonnegative input temperature, no physical temperature)
and liquid-only volumes below one cubic metre. Passive quantities and disease
remain rejected. Disconnected fluid pockets are admitted.

Point heat, point transfers, radius centres, point actors and durable actors
that target a solid reject the complete batch; nothing is queued. A rectangle
names a footprint. Its solids, including a solid anchor, are skipped exactly as
the [inspected native loop](native-domain-transactions.md#rectangular-consumption)
and the existing planner skip them. A footprint with no fluid returns one
zero-mass callback. The regional planner's bound is the configured 256-side,
4096-cell budget; legacy rooms never exceed 256 cells.

Copies publish each solid and each empty fluid cell as an all-zero
`MAOniDomainCell`; the configuration copy is the authoritative ownership mask.
Occupied cells publish positive mass and EOS temperature, using their occupied
volume. With `VACUUM`, `liquid_fraction` is the actual liquid volume. `MAOniDomainStatus.cells` counts
every cell. Its oxygen, pressure and liquid scalars sample the first fluid cell
in row-major order, which is cell 0 in every legacy scenario. The managed
caller must not write native compatibility projections into solid cells, and
its pressure, breathing and hover readers must treat the mask as authoritative:
a solid inside the rectangle is native data. Native item heat in a solid cell is
left to native ONI rather than captured.

## Native ownership per cell

Legacy scenarios exclude every hook family over the whole rectangle. A
configured session owns its fluid cells only. The evidence below is from the
pinned Linux `libSimDLL.so` (SHA-256 `85111cd0…c12d`), as `objdump -d`
addresses. Windows follows the same per-hook rule; its call sites were not
separately disassembled for this slice.

| Hook | Native behaviour for a solid cell | Configured rule |
| --- | --- | --- |
| Pressure | `SimBase::UpdateData`'s gas loop skips a cell whose element state has bit 1 set, liquid or solid (`0x69c6b`). All three `UpdatePressure` calls (`0x6aa66`, `0x6abfa`, `0x6b2be`) test the neighbour the same way (`0x6aa05`, `0x6aba4`, `0x6b261`). | Per cell; no solid reaches it. |
| Liquid | `UpdateLiquid` (`0x6a6cb`) runs only for state 2 with positive mass (`0x6a6ad`–`0x6a6b9`). | Per cell. |
| Displacement | Gas displacement needs a gas neighbour (`0x69f81`); liquid displacement a liquid primary cell (`0x6a7fa`). Any involved owned fluid excludes the call. | Per cell. |
| Gas interaction | The single-cell `DoGasInteraction` (`0x6f490`) has no caller; the pair entry is reached only from it. A pair with one owned endpoint is excluded. | Per cell. |
| Temperature | `SimUpdateTemperatureTask::InternalDoTask` (`0x6b4c0`) calls `UpdateTemperature` for right/up pairs above a temperature-difference threshold, with conductive elements and no insulated flag (`0x6b5c4`–`0x6b61f`). There is no phase test: solid pairs conduct natively. | Per cell. Solid/solid and solid/outside pairs keep native conduction. A fluid/solid pair has one owned endpoint and reaches the solid-heat bridge, which exchanges heat only for registered outside faces. An interior face is adiabatic. |
| Chunk heat | An item exchanges heat with its native cell. | Per cell; an item in a solid keeps native exchange. |
| Post-process | `PostProcessCell` (`0x721f0`) branches on element state (`0x7223d`). A solid returns at once (`0x72580`–`0x72586`) unless its unstable flag is set; then `DoUnstableCheck*` moves falling solids (`0x725a1`, `0x72b80`). Sublimation (`0x7256c`) is reached only from the gas and vacuum branches. | Whole rectangle: an exact no-op for stable solids, and an unstable solid cannot fall into owned fluid. |
| State transition | The temperature task calls `DoStateTransition` for every cell (`0x6b698`–`0x6b6bf`); energy messages (`0x4ae82`) and building heat (`0x5c116`) also call it. It applies its own thresholds, and an unfiltered 400 K native ice control melted ([interop contract](interop-contract.md)). | Whole rectangle: a native melt would create fluid that the owner cannot account. |
| Load transition, sublimation | Load-time transitions; gas/vacuum post-processing. | Whole rectangle, as for legacy scenarios. |

Interior solids therefore never melt, sublimate or fall in this slice, even if
their native temperature would require it. The caller must admit only static,
stable, non-transitioning interior solids with no objects, exactly as it
verifies the border. Native solid temperature continues through solid
conduction, outside cells, buildings and items. A fluid cell on the rectangle
edge still needs the verified impermeable, adiabatic native border outside it.

## Fluid/solid interface

The interface is impermeable and adiabatic. MAC walls carry no mass flux, and
conduction and viscosity treat them as insulated free-slip boundaries. The
[solid-heat bridge](native-domain-physics.md#native-solid-cell-heat) does not
generalize cleanly to every interior face:

1. Registration replaces one set of at most 128 records, and the completed
   endpoint and cell-state observations each describe one solid.
2. A border solid touches one owned cell; an interior solid can touch four.
   Their bridged contacts read and write the same accumulated `updatedCells`
   temperature from row-partitioned native temperature tasks. Their ordering
   across tasks has not been established.
3. The actual Unity border-heat gate still fails.

Registration therefore admits only outside faces, and also requires an owned
fluid cell inside each face. The managed caller must verify interior solids as
insulating, or accept their faces as adiabatic, exactly as it does for an
unregistered border solid.

## Size bound

Configured rectangles are 2–256 cells per side with at most 4096 cells.
Legacy scenarios keep 2–16. In the historical reference measurement, one 0.2 s frame of a 64×64 room with gravity,
conduction and viscosity raised peak RSS from 8.9 to 94.3 MiB, about 21 KiB
per cell; a 1024-cell frame without viscosity measured 13 KiB per cell. The
retained owner, pending candidate, 96-byte publication and roughly 159-byte
snapshot add a few hundred bytes per cell. The 64×64 checkpoint is 650,893
bytes, far below the 256 MiB wrapper and 1 GiB solver limits.

All counts, masks and checkpoint sizes fit uint32. The native index guard keeps
`(offset_y+height+2)*(pitch+2) <= INT_MAX`, and bounds the pitch to
`INT_MAX-2` first. Without that bound, `offset_y+height+2 == pitch+2 == 2^32`
wraps the uint64 product to zero. The legacy validator lacks the pitch bound:
an internal legacy start admits that geometry, while the exported start rejects
the pitch separately. Legacy behaviour is left unchanged.

The bound limits memory and indices; it is not a performance claim. The historical reference
64×64 frame took 48.3 s in a Linux Release build (20 substeps). At `825c56c`, a production
native-suite diagnostic with 256 solids (3,840 active cells) measures 209.851–222.867 ms
per 0.2 s, twenty substeps and peak RSS 9,712–9,724 → 22,076–22,096 KiB (GNU
Release, twelve requested workers, two taskset 0–11 runs, host load 11.07–11.51).
These small-room samples do not certify active-cell acceptance; the production timestep gate remains open.

## Checkpoints

Configured physical checkpoints, completed snapshots, paired physical sections
and frame-input sources use a `MADCFG01` wrapper. Durable actor images keep the
`MAACTR01` format and carry the wrapper as their physical section. Legacy
sessions produce exactly their previous bytes. Integers are little-endian.

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 8 | ASCII `MADCFG01` |
| 8 | 4 | Format version 1 |
| 12 | 4 | Cell count, `width*height` |
| 16 | 40 | Ten `MAOniDomainConfig` uint32 fields, in declaration order |
| 56 | 4 | Physics flags |
| 60 | 4 | Reserved zero |
| 64 | 24 | Gravity, shear and bulk viscosity, binary64 bits |
| 88 | 4 | Enclosed solver image byte count |
| 92 | 4 | Reserved zero |
| 96 | count | Mask bytes, 0 or 1 |
| following | solver bytes | Unchanged `MACCLS01` image |
| final 64 | 64 | Lowercase ASCII SHA-256 of all preceding bytes |

Restore and validation require the caller's exact geometry, bitwise physics and
mask in the wrapper and in any enclosing actor header. The solver image's grid,
walls, clock, gravity, conduction and viscosity must then agree bitwise. Fluid
cells must have admitted phases; `VACUUM` also admits empty cells and occupied
volumes. Actors must sit on admitted fluid cells, including empty ones. The wrapper is never trusted over the solver's walls or settings.
Stateless checkpoint, snapshot and input-replay advances read the configuration
from the wrapper and apply the same checks.

## Verification

```sh
cmake -S . -B build-agent-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-agent-native && ctest --test-dir build-agent-native
cmake -S . -B build-agent-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_SANITIZERS=ON \
  -DMA_BUILD_STAGGERED_EXPERIMENTS=OFF -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-agent-asan && ctest --test-dir build-agent-asan
```

`native_oni_configured_domain` has six groups:

- 16 geometry, 20 physics, 3 mask and 16 initial-record faults, plus legacy
  isolation and exported-wrapper failures, without activation or output change.
- Byte-identical solver checkpoints, publications, receipts and
  diagnostics against scenarios 0, 1, 2, 4, 5 and 6 over a heat batch,
  injection/removal and three frames.
- An L-shaped gas room, a pillared wet mixture and two disconnected water
  pockets, each for ten frames with gravity and conduction (and viscosity in
  the first two). Every hook family is checked per cell; solids copy as zero.
  Species masses and U+K+G close within the 128-epsilon operand bound
  (largest residual 6.7e-5 J against 1.5e-3 J). Frame inputs replay exactly and
  restore continues exactly.
- Solid-targeted heat, point, radius and actor batches reject atomically. A
  rectangle anchored on a solid consumes only its fluid footprint and replays.
- 18 checkpoint and actor-image mismatches reject, including wrappers resealed
  with a different mask or physics. Restore and actor continuation are exact.
- The 64×64 room starts, advances one frame, copies, checkpoints, validates and
  restores; a 256×16 corridor is admitted. Timing is printed, not gated.

**Status.** All CTest groups pass in Linux Release (GCC 15.2) and Linux
ASan/UBSan with fatal diagnostics; the configured group dominates the run time
(about 109 s in Release, 538 s under sanitizers). Standalone timings for the
64×64 room on an idle host: start 39.7 ms, frame 48,308 ms, copy 0.04 ms,
checkpoint 3.7 ms, validation 27.0 ms, restore 75.6 ms. A MinGW-w64 13 cross
build of the native backend and tests compiles without warnings and every test
executable passes under Wine 10; the repository has no committed Windows
toolchain file, so that build used a local toolchain and scratch toolchain
file. Wine is not Windows OS or Unity evidence.

## Managed adapter and Unity scenes

`OniNativeDomain.cs` declares the 40-byte `Physics` record, the flags, size
limits and the five configured entry points. `ConfiguredDomainLayout.cs` is the
engine-free room: mask admission, the complete inventory with all-zero solid
records and the all-zero copy check. Managed tests check the layout; a Python
test compares the constants and entry points with the native header.

Three development modes reuse the sealed-scene builder
(`NativeSealedDomainValidation.cs`) in one 7×5 room
(`ConfiguredDomainLayout.PillaredRoom`): a three-cell Unobtanium pillar on the
floor at x=3 and a ledge at (5,3), 31 fluid and 4 solid cells.

| Mode | Physics | Fluid |
| --- | --- | --- |
| `native-configured-domain` | `GRAVITY\|CONDUCTION`, g = 9.81 | oxygen, 0.6–2.4 kg per column, 280–310 K |
| `native-configured-water-domain` | `LIQUID\|GRAVITY\|CONDUCTION`, g = 9.81 | filled water, 1000–1120 kg, 280–310 K |
| `native-configured-vacuum-domain` | `VACUUM\|UNRESOLVED_GAS\|MIXTURE\|GRAVITY`, g = 9.81 | 500 kg water in each floor fluid cell, vacuum above; 300 K water |

- **Placement.** The arena keeps every active positioned prefab at least four
  cells, and every `Brain` (critter, duplicant) eight cells, away. Burrowed
  critters and entombed items are not always in `Grid.Objects` and otherwise
  fall in when their rock is replaced. Entombed items can also be unspawned
  world-generation records in no object list, so the arena also avoids every
  unspawned record inside the cells the fixture rewrites.
- **Solids.** Interior solids are built and verified exactly like the border,
  before activation and on every frame: solid Unobtanium with zero
  conductivity and heat capacity, no unstable tag, its own high-temperature
  target far above its temperature, no objects. They are native data: no flow
  fence, projection write, owned-copy overlay or physical reading;
  `OwnsPublicCell` excludes them, so pressure, breathing, hover, heat and
  transfer paths stay native, and known native mutations aimed at them stop
  the scene like the border. Their native tuples stay in the unchanged-cell
  check; the GPU check compares mixture texels for fluid cells only.
- **Activation.** After a completed native frame, the fluid cells' native
  mass, temperature and element form the complete inventory (solids all
  zero). `ma_oni_domain_start_configured` admits it; the copy must match with
  solids all zero and status from the first fluid cell, and
  `ma_oni_domain_configuration_copy` must return the exact geometry, physics
  bits and mask. Only then are the fluid cells fenced and projected.
- **Completion.** Besides the existing frame, conservation, projection,
  rendering, save-guard and suppression checks: the configuration copy again,
  all-zero solid copies, readings for fluid cells only, unchanged unowned
  solids, conduction (stages twice the substeps, contacts, redistributed
  energy, positive entropy), gravity (negative body impulse, changed
  potential), fluid motion and no viscosity work.

The runner and `oni_native_domain_evidence.py --mode configured` /
`configured-water` require the `ConfiguredDomain` block, copied configuration,
inventory, interior-solid evidence and thermal and gravity statuses. A
configured report never passes as a legacy scene, nor the reverse. Configured
rooms accept `--save-colony`/`SaveOnComplete`, `--load-save` and `--long-run`
([saves and reloads](#saves-and-reloads)). `tools/configure_visual_demo.py
--scene pillared` (or `pillared-water`) prepares the same room for manual
viewing.

### Vacuum scene

The opt-in vacuum mode first preserves the existing unhooked pressure-gradient
control, then imports a flat half-filled water pool and exact vacuum from a
completed native frame. It checks ten owned resting frames before routing one
actual `EmitMass` packet (0.03125 kg oxygen at 300 K) into the vacuum.
`vacuum-rest.png` and `vacuum-front.png` capture the two stages. The final report
requires gas in another initially empty cell, partial water, completed physical
readings, projection, shader-bound GPU checks and the existing conservation
bounds. Gravity is 9.81 m/s²; conduction, viscosity and phase change stay disabled in
this scene; native controls separately cover body heat beside partial water/vacuum.

Vacuum readings have zero mass, energy, momentum, pressure and temperature and
one cubic metre of void. Partly filled material uses its actual liquid volume
and gas volume inferred from its EOS pressure/temperature; the remaining cell
volume is void. Gas/void readings require an explicit resolved-interface opt-in;
the default common-pressure reading rules are unchanged. Native compatibility
cells keep the initial pool/vacuum tuples behind the flow guards. The published
copy selects vacuum, water or oxygen from the completed physical record.

Saves retain the same configured wrapper and paired payload formats. Existing
version-9/10 exact-geometry saves keep flags 41; new vacuum scenes use flags 105
and require version 12. Flag 64 requires flag 32, and restore compares the saved
subcell policy with the wrapper. Phase change stays off; its version 11 remains
rejected by this game adapter. Restore verifies the exact pool/vacuum
compatibility tuples and loaded solid mask. Configured import and actor classification use the admitted mixture/liquid
flags, so the vacuum scene admits both water and gas. Engine-free controls
pass 207 OwnedSnapshotTests. Point transfers also enforce native phase admission
on empty cells; invalid single requests and batches leave the owner unchanged.
Linux base-game ONI 744825 passes the source scene at frame 121/time
24.2000003606081 s, including ten unchanged resting frames, a real 0.03125 kg
oxygen packet, the front, Grid/GPU readback and ordinary paired save/readback.
Three closure events add 2.59e-17 J/K; maximum relative mass/energy errors are
9.58e-16/7.05e-19. A fresh process restores the same source hash, resaves at frame
zero and completes 60 exact physical frames. Complete restored-reader/menu
reload and the game long run remain open. Reader observations now follow each
published vacuum/water/oxygen identity and permit the physical zero pressure and
mass of void cells; legacy gas/water conditions stay unchanged.

The 48-byte `ma_oni_domain_subcell_status` reports
completed-frame accounts since activation; restore resets those diagnostics.
The harness checks their policy, frame and entropy. The additive
`ma_oni_domain_copy_phase_frame` atomically copies cells, actual material
volumes, receipts and closure accounts under one frame/state fence. The adapter
uses this endpoint so adjacent native frames cannot split the reading. The
separate `ma_oni_domain_copy_occupied_volumes` reader remains available for
inspection; solids and vacuum report zero. Rendering derives
resolved gas headspace and void from that geometry, retaining the EOS gas
volume when liquid subtraction cannot resolve a trace. Geometry must still
satisfy the existing ideal-gas consistency check; no bound changes. The native
151-frame fixture's 31 fluid readings close volume within 5.55e-17 m³.

The owned game snapshot also appends substance-change events for changed
fluid identities: ONI's cached `Grid.Element` needs them separately from its
`elementIdx` array. The room guard compares the delivered publication; the
front capture waits three render frames after publication. Vacuum compatibility
cells exercise blocked liquid, transition and post-processing calls; existing
scenes keep their native temperature-call requirement. Updated complete Linux
game reload/long-run validation remains open. Proton/Wine/Windows validation is deferred by the user's
2026-10-20 decision; previous runs do not certify this revision.

The read-only base-game whole-world retry stages 19,614 fluid cells in a
256×384 world. Direct configured-owner requests with flags 40 and 104 reject their
geometry (height limit 256, total-cell limit 4,096). Separate native single-cell
admission probes admit all 7,370 mapped, metadata-free inputs with either flag set;
without the void flag, 173 reject internal void. This resolves those partial
water admissions, not whole-world ownership. Another 12,138 cells use eleven
unmapped identities and 106 mapped cells carry unsupported disease/radiation
metadata. No whole-world owner activates; original native payloads are unchanged.

### Linux game evidence

Both scenes pass in the native Linux game (ONI 744825, Mesa llvmpipe, private
display). The native libraries come from the Ubuntu 22.04 compatibility build
(glibc at most 2.34):

```sh
python3 tools/build_linux_compat.py --output build-agent-linux-compat --jobs 8
python3 tools/package_probe.py --native build-agent-linux-compat/native/libmixed_atmospheres.so \
  --native-observer build-agent-linux-compat/native/libmixed_atmospheres_oni.so \
  --native-build-info build-agent-linux-compat/compatibility-build.json --output artifacts/configured-probe-linux
python3 tools/oni_headless.py run --game-dir <Steam ONI> --package artifacts/configured-probe-linux \
  --mode native-configured-domain --frames 120 --timeout 3600 --private-display
```

Use `--mode native-configured-water-domain` for water.

| | Gas | Water |
| --- | --- | --- |
| Frames after priming | 120 (24.4 s physical) | 120 (24.4 s) |
| Substeps / rejected attempts | 2,573 / 18 | 2,562 / 0 |
| Max relative mass / energy error | 2.9e-16 / 3.8e-15 | 9.0e-16 / 4.0e-15 |
| Conduction redistributed / entropy | 15.74 J / +1.7e-3 J/K | 99.36 J / +6.6e-3 J/K |
| Gravity potential | 1,138.7 → 1,152.0 J | 818,494 → 825,640 J |
| Wall time per 0.2 s frame | about 0.34 s | about 0.66 s |

Every `ConfiguredDomain` check is true, all 35 rectangle cells keep their
native tuples where native, no mod failure lines appear, and the standalone
checker accepts both reports (the gas report fails as a legacy `gas` scene).
The captures show the Neutronium pillar and ledge inside the sealed walls, 31
fluid readings in the status panel and the stock Neutronium tooltip over the
pillar. The four solids' stock gas texels are transparent zero. Suppressed
pressure and gas counters stay zero, as in the legacy fenced scenes. Wall
times include software Unity rendering; they are not a production timestep or
performance result.

## Saves and reloads

A configured session travels in the ordinary paired colony save. The inner
`MAONID01` section uses payload version 4 for a configured image (`MADCFG01`,
or `MAACTR01` enclosing it) and version 5 when a nonempty `MACO2P01` particle
image follows; an empty particle image keeps version 4. Versions 1–3 are
byte-identical to before and stay limited to scenarios 0–6
([saved domain section](native-domain-persistence.md#saved-domain-section)).

**Save.** The live session's `ma_oni_domain_configuration_copy` must equal the
payload's geometry, bitwise physics and mask, and the image is checked with
`ma_oni_domain_configured_checkpoint_validate` before the pair is written.
`NativeConfiguredDomainImage.cs` reads the wrapper back: SHA-256, geometry and
physics against the native admission rules (disabled values exactly +0), a
binary mask with at least one fluid cell, and an enclosed `MACCLS01`. Legacy
solver versions 2/3 follow the viscosity setting; versions 5 (diffusion),
6 (production), opt-in 9 (void phase) and 10 (carried U) retain the metadata prefix and pass the same
native settings validation. Full native admission requires the saved void selector and occupied-volume
vector to agree with the opt-in flag; phase change remains rejected. The `MAONID01` header geometry must
equal the wrapper's, and saved actors and CO2 origins must be on fluid cells.

**Native-world admission.** Before ONI's loader runs, the mask is recomputed
from the saved cells' phases and must equal the saved mask exactly. Walls and
interior solids must be filled Unobtanium with cleared metadata, and fluid
cells must hold exactly the scene's projection values.

**Load.** Only the pillared 7×5 gas or water scene is admitted, with physics
compared bitwise. At `Sim.Start` the mask is recomputed from the loaded `Grid`
and must equal the saved one; interior solids are re-verified exactly as at
activation, plus the boundary check, before `ma_oni_domain_restore_configured`.
The restored configuration copy must then match. Flags and projections go to
fluid cells only. Any rejection fails the validation report visibly, and ONI's
loader shuts the native simulator down. The report adds `Restored`,
`SavedPayloadFormatVersion`, `RestoredGridVerified`,
`RestoredMaskFromLoadedGrid`, `RestoredInteriorSolids` and a bounded
`SaveHistory` (64 entries). Restored pressure and breathing readers cover the
31 fluid cells; the breathing check compares only the six cells ONI actually
searches (`SearchedCells`).

`OwnedSnapshotTests` pins the SHA-256 of four legacy payloads produced by the
unmodified codec and rejects 13 resealed and 7 framing wrapper faults, a header
that differs from the wrapper, actors on solids, and 15 native-world mask and
tuple faults for each of gas and water.

| Run | Result |
| --- | --- |
| Gas save | 121 frames, 2,552 substeps, mass/energy error 6.3e-16 / 3.0e-15; payload v4, readback verified |
| Gas fresh-process load (`--resave-before-resume --reload-in-process`) | Both sessions 120/120 exact frames with frame-zero resaves and the same final image; loaded mask equals saved mask |
| Water save | 122 frames, 0 rejected attempts, error 1.0e-15 / 3.0e-15; payload v4 |
| Water fresh-process load (resave and in-process reload) | 120/120 exact frames each session, identical final images |
| Gas long run (`--long-run --frames 8400 --timeout 14400`) | 8,401 frames, error 2.3e-15 / 2.0e-13; natural autosaves in cycles 2 and 3 admitted and paired, none declined; one telemetry session of 73 lines ending in `final`; 0.365 s wall per frame |
| Load of the long run's cycle-2 autosave | Source image matches the run's save record; 120 exact frames and a frame-zero resave |
| WindowsPlayer under Proton 10.0-4b (OpenGL Core, MinGW DLLs): gas save, then load | 121 frames, error 2.8e-16 / 9.8e-16, payload v4; then 120 exact frames and a frame-zero resave |

All Linux runs have zero mod failure lines. The long run stopped at 8,400 frames
to finish before the [colony dies](../tools/ONI_HEADLESS.md#long-development-runs);
at its end all three duplicants were alive but suffocating. A load of the
cycle-3 autosave restored, resaved and ran 120 exact frames, but failed the
scene's `NativeSolidsAdvanced` check: by cycle 3 the hot/cold granite pair
outside the room is within 1.3 K and warms with its surroundings, so that
convergence check cannot hold on late saves. Proton is not Windows OS or MSVC
evidence.

## Open work

- Windows OS runs of the configured scenes (`run_colony_validation.ps1` and
  `--verify-natural-autosave` have no configured mode), and occupied rooms
  (actors, breathing, transfers, particles) with interior solids; the payload
  format carries actors and particles, but no game run has exercised them.
- An in-game load of a deliberately tampered configured save; fail-closed
  rejection is covered by unit tests only. Configured mixtures outside the vacuum scene, and viscous configured
  restores, remain rejected.
- `NativeSolidsAdvanced` cannot pass on late saves; it needs a rule that holds
  once the granite pair has nearly equilibrated.
- Interior fluid/solid heat exchange, and melting, sublimation, digging,
  construction and falling solids inside the rectangle.
- The configured vacuum/surface path has native and managed controls and passes
  Linux/Proton activation and rest; vent continuation fails as described above.
  Passive data, disease and further liquids still need adapters.
- Materials beyond the five-species catalogue.
- The production timestep and performance gate.
