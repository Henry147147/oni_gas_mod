# Configured sealed native domain

This is the first native slice of [gate 1](playtesting-readiness.md), colony
fluid ownership. An additive ABI lets the sealed native owner take explicit
physics, interior solids and larger rectangles. Scenarios 0–6, their
checkpoint bytes and every existing entry point are unchanged. A managed
adapter and two small Unity scenes (a 7x5 room with interior solids, gas or
water) now use it and pass on native Linux; see
[the managed adapter](#managed-adapter-and-unity-scenes). They remain bounded
development rooms, not colony ownership.

## ABI

`ma_oni_domain_configured_abi_version()` returns 1. A configured session uses
`MAOniDomainConfig.scenario` 7 (`MA_ONI_DOMAIN_CONFIGURED_SCENARIO`) with the
existing sealed opt-in flag 1. It also supplies a 40-byte `MAOniDomainPhysics`
record and a row-major solid mask. Only `ma_oni_domain_start_configured`,
`ma_oni_domain_restore_configured` and
`ma_oni_domain_configured_checkpoint_validate` admit scenario 7. The legacy
start, restore and validation entry points still reject it.
`ma_oni_domain_configuration_copy` returns the active session's exact geometry,
physics and mask; a null mask with zero capacity queries the count. Copies,
heat, transfers, actors, snapshots, frame inputs, paired capture and
diagnostics are the existing shared calls.

| Flag | Value | Effect |
| --- | ---: | --- |
| `GRAVITY` | 1 | `0 < g <= 100` m/s²; otherwise `g` must be exactly +0 |
| `CONDUCTION` | 2 | `MacClosedSettings.thermal_conduction` |
| `VISCOSITY` | 4 | shear and bulk `0..1000` Pa s, at least one positive; otherwise both exactly +0 |
| `MIXTURE` | 8 | occupied gas/liquid admission, transfer topology and EOS liquid fraction of scenarios 4–6 |
| `LIQUID` | 16 | without `MIXTURE`, filled single-liquid (water) admission; neither flag is gas-only |

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
equivalent: it shifts water's reference energy into a separate EOS profile.
Configured liquid gravity uses the unshifted v2 reference.

## Solids, targets and copies

The mask byte is 0 for fluid and 1 for solid. It covers `width*height` cells
and keeps at least one fluid cell. Solids become MAC walls, and their initial
records must be all zero, including temperature. Fluid cells must be occupied,
with a positive finite temperature and admitted constituents. Vacuum, passive
quantities and disease remain rejected. Disconnected fluid pockets are admitted.

Point heat, point transfers, radius centres, point actors and durable actors
that target a solid reject the complete batch; nothing is queued. A rectangle
names a footprint. Its solids, including a solid anchor, are skipped exactly as
the [inspected native loop](native-rectangular-consumption.md) and the existing
planner skip them. A footprint with no fluid returns one zero-mass callback.
The regional planner's bound is now the configured 256-side, 4096-cell budget;
legacy rooms never exceed 256 cells.

Copies publish each solid as an all-zero `MAOniDomainCell`. A fluid cell always
has positive mass and EOS temperature, so that record never denotes fluid; the
configuration copy is the authoritative mask. `MAOniDomainStatus.cells` counts
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
| Load transition, sublimation | Load-time transitions; gas/vacuum post-processing. | Whole rectangle, as before. |

Interior solids therefore never melt, sublimate or fall in this slice, even if
their native temperature would require it. The caller must admit only static,
stable, non-transitioning interior solids with no objects, exactly as it
verifies the border. Native solid temperature continues through solid
conduction, outside cells, buildings and items. A fluid cell on the rectangle
edge still needs the verified impermeable, adiabatic native border outside it.

## Fluid/solid interface

The interface is impermeable and adiabatic. MAC walls carry no mass flux, and
conduction and viscosity treat them as insulated free-slip boundaries. The
[solid-heat bridge](native-solid-heat.md) does not generalize cleanly to every
interior face:

1. Registration replaces one set of at most 128 records, and the completed
   endpoint and cell-state observations each describe one solid.
2. A border solid touches one owned cell; an interior solid can touch four.
   Their bridged contacts read and write the same accumulated `updatedCells`
   temperature from row-partitioned native temperature tasks. Their ordering
   across tasks has not been established.
3. The actual Unity border-heat gate is still failing.

Registration therefore still admits only outside faces, and now also requires
an owned fluid cell inside each face. The managed caller must verify interior
solids as insulating, or accept their faces as adiabatic, exactly as it does
for an unregistered border solid today.

## Size bound

Configured rectangles are 2–256 cells per side with at most 4096 cells.
Legacy scenarios keep 2–16. One 0.2 s frame of a 64×64 room with gravity,
conduction and viscosity raised peak RSS from 8.9 to 94.3 MiB, about 21 KiB
per cell. A 1024-cell frame without viscosity measured 13 KiB per cell. The
retained owner, pending candidate, 96-byte publication and roughly 159-byte
snapshot add a few hundred bytes per cell. The 64×64 checkpoint is 650,893
bytes, far below the 256 MiB wrapper and 1 GiB solver limits.

All counts, masks and checkpoint sizes fit uint32. The native index guard keeps
`(offset_y+height+2)*(pitch+2) <= INT_MAX`, and bounds the pitch to
`INT_MAX-2` first. Without that bound, `offset_y+height+2 == pitch+2 == 2^32`
wraps the uint64 product to zero. The legacy validator lacks the pitch bound:
an internal legacy start admits that geometry, while the exported start rejects
the pitch separately. Legacy behaviour is left unchanged.

The bound limits memory and indices; it is not a performance claim. The same
64×64 frame took 48.3 s in a Linux Release build (20 substeps). The production
timestep gate remains open.

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
cells must be occupied with admitted phases, and actors must sit on admitted
fluid cells. The wrapper is never trusted over the solver's walls or settings.
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

Linux Release (GCC 15.2): all 22 CTest groups pass in 136.7 s; the new group
takes 108.9 s. Standalone timings for the largest room on an idle host: start
39.7 ms, frame 48,308 ms, copy 0.04 ms, checkpoint 3.7 ms, validation 27.0 ms,
restore 75.6 ms. Linux ASan/UBSan with fatal diagnostics: all 22 groups pass in
538.4 s (the new group 538.2 s). A MinGW-w64 13 cross build of the native
backend and tests compiles without warnings, using the locally extracted
`build-oni-cross-tools` toolchain and a scratch toolchain file; the repository
has no committed Windows toolchain file. All 22 Windows test executables pass
under the extracted Wine 10 with a scratch prefix (new group 117.3 s). This is
not Windows OS or Unity evidence.

The sanitizer build did not link `ma_mac_equilibrium_tests` on unmodified
`HEAD`: that target was missing from the instrumented list. `CMakeLists.txt`
now instruments it and `ma_mac_playtest_benchmark`.

## Managed adapter and Unity scenes

`OniNativeDomain.cs` declares the 40-byte `Physics` record, the flags, size
limits and the five configured entry points. `ConfiguredDomainLayout.cs` is the
engine-free room: mask admission, the complete inventory with all-zero solid
records and the all-zero copy check. Managed tests check the layout; a Python
test compares the constants and entry points with this header.

Two development scenes reuse the sealed-scene builder
(`NativeSealedDomainValidation.cs`) in one 7x5 room
(`ConfiguredDomainLayout.PillaredRoom`): a three-cell Unobtanium pillar on the
floor at x=3 and a ledge at (5,3), 31 fluid and 4 solid cells.

| Mode | Physics | Fluid |
| --- | --- | --- |
| `native-configured-domain` | `GRAVITY\|CONDUCTION`, g = 9.81 | oxygen, 0.6–2.4 kg per column, 280–310 K |
| `native-configured-water-domain` | `LIQUID\|GRAVITY\|CONDUCTION`, g = 9.81 | filled water, 1000–1120 kg, 280–310 K |

- **Placement.** The arena keeps every active positioned prefab at least four
  cells, and every `Brain` (critter, duplicant) eight cells, away. Burrowed
  critters and entombed items are not always in `Grid.Objects` and otherwise
  fall in when their rock is replaced.
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
rooms reject `--save-colony`/`SaveOnComplete` and `--long-run`: the paired save
payload and restore adapter admit only scenarios 0–6 and the 3x2 room, so an
autosave would be declined. `tools/configure_visual_demo.py --scene pillared`
(or `pillared-water`) prepares the same room for manual viewing.

### Linux game evidence

Both scenes pass on native Linux (Steam build 744825, Mesa llvmpipe, private
display) with native libraries from the Ubuntu 22.04 compatibility build of
`21c8a7c` (22/22 native tests, glibc at most 2.34) and the managed adapter
packaged as `artifacts/configured-probe-linux-v3`:

```sh
python3 tools/build_linux_compat.py --output build-agent-linux-compat --jobs 8
python3 tools/package_probe.py --native build-agent-linux-compat/native/libmixed_atmospheres.so \
  --native-observer build-agent-linux-compat/native/libmixed_atmospheres_oni.so \
  --native-build-info build-agent-linux-compat/compatibility-build.json --output artifacts/configured-probe-linux-v3
python3 tools/oni_headless.py run --game-dir <Steam ONI> --package artifacts/configured-probe-linux-v3 \
  --mode native-configured-domain --frames 120 --timeout 3600 --private-display
```

| | Gas (`configured-gas-linux-v3`) | Water (`configured-water-linux-v1`) |
| --- | --- | --- |
| Frames after priming | 120 (24.4 s physical) | 120 (24.4 s) |
| Substeps / rejected attempts | 2,573 / 18 | 2,562 / 0 |
| Max relative mass / energy error | 2.9e-16 / 3.8e-15 | 9.0e-16 / 4.0e-15 |
| Conduction redistributed / entropy | 15.74 J / +1.7e-3 J/K | 99.36 J / +6.6e-3 J/K |
| Gravity potential | 1,138.7 → 1,152.0 J | 818,494 → 825,640 J |
| Wall time per 0.2 s frame | about 0.34 s | about 0.66 s |

Run directories are under `<state>/runs/`. Every `ConfiguredDomain` check is
true, all 35 rectangle cells keep their native tuples where native, no mod
failure lines appear, and the standalone checker accepts both reports (the gas
report fails as a legacy `gas` scene). The captures show the Neutronium pillar
and ledge inside the sealed walls, 31 fluid readings in the status panel and
the stock Neutronium tooltip over the pillar. The four solids' stock gas texels
are transparent zero. Suppressed pressure and gas counters stay zero, as in the
legacy fenced scenes. Wall times include software Unity rendering; they are not
a production timestep or performance result.

## Open work

- Colony saves, autosaves and reloads for configured rooms: the paired save
  payload and the restore adapter still admit only scenarios 0–6 and the 3x2
  room. Long runs of configured rooms wait for this.
- Windows Unity runs of the configured scenes, and occupied rooms (actors,
  breathing, transfers) with interior solids.
- Interior fluid/solid heat exchange, and melting, sublimation, digging,
  construction and falling solids inside the rectangle.
- Vacuum, free surfaces, passive data and disease; vacuum is separate solver work.
- Materials beyond the five-species catalogue.
- The production timestep and performance gate.
