# Playtesting readiness and simulation cost

The full overhaul is **not ready for an ordinary colony playtest**. The default
installation remains a development probe. A visible six-cell chamber does not
establish whole-colony mixture ownership.

The next playable gate needs:

1. Colony fluid ownership across walls, vacuum, liquids and every supported
   constituent. The current sealed native owner accepts only 2–16 cells per
   dimension and a small material catalogue.
2. A reliable production timestep. The original strong-flow separation test
   still fails velocity and central density/pressure bounds. Its reproducer
   and limits remain intact; see [the diagnostic](../native/MAC_CURVED_NODAL.md).
3. Complete transactions for breathing, machines, pipes, storage, construction,
   heat and callbacks, using actual inventory and energy reservations.
4. Complete saving of these owners and live objects: manual saves, autosaves,
   reload and asteroid switching on both OSes. Current small-domain tests cover
   only implemented paths.
5. Ten actual colony cycles with life support, coherent rendering, every
   non-trace gas on hover, harmful-trace warnings and bounded simulation and
   publication latency. See [milestone D](completion-plan.md#d-first-playable-development-colony).

As of 2026-10-17 all five gates are **open**. The [gate status](#gate-status)
records what exists, what is missing and what closes each one; the
[work plan](#work-plan) orders the remaining items.

## Playtest definition (proposed)

Proposed 2026-10-17 to make gate 5 reproducible; not yet accepted. It narrows
[milestone D](completion-plan.md#d-first-playable-development-colony) without
relaxing its exit:

- ONI 744825 with the verified adapter profile, on native Linux and 64-bit
  Windows.
- A new base-game colony on Terra, seed 147147 (the verified development
  setup), default settings and the three offered duplicants. Spaced Out!
  follows once gate 4 covers asteroid switching.
- Mixed air and water; duplicant breathing; bulk pumping; selective
  separation; ventilation; CO2 removal; fluid–solid heat exchange;
  construction displacement; manual and automatic save/reload.
- Passing means ten cycles with every duplicant alive, no domain fault,
  conservation diagnostics within the solver's declared tolerances, and the
  presentation criteria of milestone D.
- Latency: slow simulation is acceptable (2026-10-16 amendment), but
  publication must keep pace with completed frames, with no growing backlog,
  and p50/p95/max solver and publication times are recorded. The 25 ms
  active-cell target below is a release target, not a playtest requirement.

## Gate status

### Gate 1: colony fluid ownership

**Exists.** The [sealed native owner](sealed-native-domain.md) runs one
rectangle of 2–16 cells per side. It holds five species: nitrogen, oxygen,
helium, water and carbon dioxide. Seven fixed scenario numbers select gravity,
conduction, viscosity and phase admission. The managed driver uses a fixed
3×2 room inside an insulating Unobtanium/Granite border. Six-cell gas, water
and mixed scenes pass on Linux Unity and on Windows. The domain starts only
from a development `colony-validation.json`.

**Missing.**
- Solid cells inside the owner. The grid has no interior walls, and native
  hook suppression covers the whole bounding rectangle.
- Domains over 16 cells per side, more than one owner, and Spaced Out! worlds.
- Vacuum and free surfaces. Both the in-game owner and the MAC solver reject
  empty cells. The separate `FluidWorld`
  [gas/vacuum front gate](../native/GAS_VACUUM.md) also fails.
- The material catalogue: 5 species against 212 inventoried material
  identities.
- Whole-world import. The Windows v5 import staged 50,368 fluid cells, then
  rejected compressed-liquid placement
  ([evidence](implementation-status.md#current-integration-evidence)).
- Production activation without a development configuration.
- Coverage: all 3,072 [inventory entries](ownership-coverage.md) remain
  `unimplemented`.

**Closes when** milestone A's exit holds for the playtest colony on both
operating systems. That means visible custom mixtures, working native solids
and buildings, correct callbacks, and no native fluid ownership, with traced
reader/writer audits. `python3 tools/coverage_inventory.py gate` covers only
the full release scope; the playtest still needs a defined subset.

### Gate 2: production timestep

**Exists.** The in-game owner advances `MacClosedWorld` with a 0.01 s maximum
step, rejected-attempt retries and certified
[exact equilibrium](#implemented-exact-equilibrium). Sod and collision pass
with [compressive work](../native/MAC_COMPRESSIVE_WORK.md).

**Missing.**
- Accuracy. Strong-flow separation fails the finest-grid velocity bound and
  the central density/pressure bounds. The
  [curved-nodal diagnostic](../native/MAC_CURVED_NODAL.md) localizes a
  pressure/remap convention mismatch; the conservative conversion is unwritten.
- Range-extended vertical gravity work is not implemented in `MacClosedWorld`;
  a step that requires it faults.
- Active-cell performance; see [measurements](#reproduce-measurements).

**Closes when** the strong-flow and vacuum gates pass with production
defaults, and the owner advances the playtest colony for ten cycles without
a fault.

### Gate 3: transactions

**Exists, within the sealed owner:**
- Point, radius and rectangle emit/consume transfers with receipts and
  callbacks ([radial](native-radial-consumption.md),
  [rectangular](native-rectangular-consumption.md)).
- Heat edits, additive [cell-mass edits](native-cell-mass-edits.md), and
  registered consumer/emitter actors.
- [Breathing](native-breathing-reader.md) and
  [pressure](native-pressure-reader.md) readers.
  [Live breathing](native-live-breathing.md) covers one tracked duplicant.
- [Body heat](native-body-heat.md) and one [solid-heat](native-solid-heat.md)
  endpoint.

**Missing.**
- Power and energy reservations.
- Converters such as electrolyzers, scrubbers and diffusers.
- Pumps, vents and conduit contents.
- Storage, bottles and suits. The [reservoir bridge](owned-reservoir-bridge.md)
  is not connected to the native owner.
- Building heat and phase transitions in owned cells.
- Construction and dig displacement. `MacClosedWorld` has no topology-change
  operation, and mutations near the owner fault the session.
- Saves with pending commands, which are currently declined.

**Closes when** each playtest system moves real inventory and energy through
one reserved transaction on both operating systems, and blocked output
consumes neither input nor work.

### Gate 4: saving

**Exists.** Paired native, physical, particle and actor checkpoints. Linux,
MSVC Windows and WindowsPlayer under Proton pass manual save, frame-zero
autosave, natural-day autosave and same-process menu reload
([actors](native-actor-persistence.md),
[day autosave](native-day-autosave.md), [menu reload](native-menu-reload.md)).
Cross-platform gas checkpoints agree within 6.74e-12; water still fails the
1e-8 bound.

**Missing.**
- Loading outside the development restore mode, including from the load
  menu; overwrite save is untested.
- Asteroid switching with a native domain. Only the presentation fixture
  exercises it.
- Rejection of mixture saves loaded without the mod.
- Durability of interrupted saves.
- Actor saves on an actual Windows installation, not only Proton.
- Persistence for the gate 3 owners that do not exist yet.

**Closes when** every playtest owner and live object survives manual saves,
autosaves, load-menu reloads and asteroid switching on both operating
systems, with exact continuation.

### Gate 5: ten colony cycles

**Exists.** Display fixtures verify hover listing every non-trace gas,
hazardous-trace warnings, overlays and colour-only mode on Windows (V54) and
[Linux](linux-tile-hover.md). Windows binds hover and GPU rendering to a
completed native frame ([rendering](native-fluid-rendering.md)). The longest
physical run reaches the first natural-day autosave, about one cycle, in a
six-cell room.

**Missing.** Gates 1–4. The validation harness rejects runs over 10,000
frames or 7,200 s, while ten cycles are about 30,000 frames. Milestone D's
100,000-cell presentation performance and frame-time evidence are also
missing.

**Closes when** the [playtest definition](#playtest-definition-proposed)
passes on Linux and Windows.

## Ownership strategy and open decisions

The completion plan gives every fluid to the custom engine and keeps solids
and buildings with native ONI. Today's in-game owner is `MacClosedWorld`
behind `ma_oni_domain_*`. The managed `FluidWorld`/coupled path
([coupled ABI](../native/COUPLED_ABI.md)) has no game frame hook. Three
decisions are open:

1. Grow ownership from sealed rooms, or move to whole-world ownership at once.
   Rooms need permeable owner boundaries, which the sealed contract forbids.
   Whole-world ownership needs vacuum, free surfaces and the full catalogue
   first.
2. Keep `MacClosedWorld` as the production colony solver, or replace it with
   the coupled `FluidWorld` path.
3. Define the playtest coverage subset by configuration (base game, Spaced
   Out!) or by reachable gameplay systems.

The work plan proceeds with items that hold under either answer.

## Work plan

Ordered by dependency. Size: S < M < L < XL. "Game" marks items that need the
licensed game to verify.

| # | Gate | Item | Size | Game |
| --- | --- | --- | --- | --- |
| 1 | 1 | Configured sealed domain: explicit physics settings, interior solid cells, per-cell ownership and a larger bound (in progress) | L | Later |
| 2 | 5 | Long-run harness opt-in with bounded telemetry (in progress) | M | Later |
| 3 | 1 | Managed adoption of item 1, and an in-game room with interior solids | M | Yes |
| 4 | 1 | Production activation and message routing outside the validation classes | M | Yes |
| 5 | 2 | Pressure/remap convention conversion, strong-flow rerun and default promotion | L | No |
| 6 | 2 | Range-extended vertical gravity | M–L | No |
| 7 | 1, 2 | Vacuum, free surfaces and multiple liquids in the MAC owner | XL | Later |
| 8 | 1 | Catalogue-driven species table and ONI gas/liquid mapping | XL | Yes |
| 9 | 1 | Fluid/solid heat at every owned face | L | Yes |
| 10 | 1 | Whole-world import, resolving the compressed-liquid placement failure | L | Yes |
| 11 | 3 | Breathing for every duplicant and critter | L | Yes |
| 12 | 3 | Power reservations and converter adapters | XL | Yes |
| 13 | 3 | Pumps, vents and conduit contents | XL | Yes |
| 14 | 3 | Storage, bottles and suits through the reservoir bridge | L | Yes |
| 15 | 3 | Construction and dig displacement | L–XL | Yes |
| 16 | 3 | Building heat and phase transitions in owned cells | XL | Yes |
| 17 | 4 | Durable pending outbox; production load path, load menu, overwrite save and missing-mod rejection | M–L | Yes |
| 18 | 4 | Asteroid switching, actual Windows runs, the water cross-platform bound and interrupted saves | M–L | Yes |
| 19 | 2, 5 | Active-cell performance: the "Further work" rows below | XL | No |
| 20 | 5 | Ten-cycle playtest and 100,000-cell presentation performance on both OSes | L | Yes |

Items 5–7 and 19 continue the current production-timestep work.

## Accuracy choices

The 2026-10-17 request authorizes investigating fidelity tradeoffs now.
Conservation, inventories, breathing thresholds and truthful presentation remain
requirements. Original reference and release tests stay available.

| Change | Benefit | Accuracy cost and admission |
| --- | --- | --- |
| Certified exact equilibrium | Avoid repeated pressure, reconstruction and heat solves | No physical approximation. Implemented for identical occupied inventories, exactly zero canonical momentum and zero gravity. |
| Lower-order conservative transport | Less reconstruction | More numerical mixing and smeared fronts. Benchmark-only `balanced` uses donor mass/momentum transport and a 0.1 s maximum step; it is not enabled in the game. |
| Reused topology, EOS identity and workspaces | Less allocation and assembly | Exact invalidation for construction, phase/material changes and load. Further work. |
| Better timestep prediction and growth | Fewer rejected attempts | Persist or deterministically derive history affecting continuation. Keep admission and conservation guards. Further work. |
| Low-Mach pressure treatment for calm gas | Avoid sound-wave timescales | Lose acoustic/shock detail only in admitted slow-flow regions. Retain EOS bulk pressure, source/heating expansion, buoyancy, pressure work and a compressible route for fast flow/vacuum. Further work. |
| Coarser pressure or slower slow-process updates | Less large-domain work | Keep species at tile resolution, conservative interface fluxes, bounded thermal/diffusive error and immediate wake-up for devices or hazardous fronts. Further work. |

Low-Mach projection filters acoustics and can make advective velocity determine
the timestep; see [AMReX-Hydro documentation](https://amrex-fluids.github.io/amrex-hydro/docs_html/Projections.html).
This is a candidate design, not an implemented property of this mod. Clamping
pressure or dropping traces would instead change gameplay physics.

## Implemented exact equilibrium

`MacClosedWorld::advanced` accepts an execution policy. Reference execution
retains the original path. Native worker, checkpoint advance and input-journal
replay consistently select `exact_equilibrium`. It compares every constituent
and internal-energy value and the entire authoritative momentum fields,
including values below binary64 range. Comparisons have no tolerance.
Identical inventories at equal volume imply identical EOS pressure/temperature;
zero momentum and gravity make the current mechanical/thermal fluxes zero.
Different passive amounts remain unchanged: this owner has no passive diffusion.
The certificate covers the entire owner. One differing occupied cell or
nonzero gravity disables it, so it does not yet provide sleeping regions
inside an active colony.

Only time and revision advance. The report records one certified interval,
`exact_equilibrium_intervals=1`, and zero pressure/thermal stages; no work counts
are invented. Physical settings, checkpoint schema and all state values remain
unchanged. Adding diffusion, phase kinetics or another evolving process requires
extending this certificate first.

`ma_mac_equilibrium_tests` compares complete checkpoint bytes with full solves
for gas/water, walls, conduction, viscosity and reload. It checks wake-up for
heat edits, one-ULP energy/trace changes, normal and below-range momentum, and
gravity; invalid arguments reject before shortcuts. MSVC Windows and GNU Linux
pass. The retained first water-test failure incorrectly expected pressure
iteration after heating a zero-expansion liquid. The corrected test checks
conduction and complete full-solver agreement.

## Reproduce measurements

```sh
cmake -S . -B build-playtest -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-playtest --config Release --target ma_mac_playtest_benchmark ma_mac_equilibrium_tests
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/wave-reference --profile reference --scene wave
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/wave-balanced --profile balanced --scene wave
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/still-100k --profile equilibrium --scene uniform --width 400 --height 250 --samples 7 --no-reference
```

For Visual Studio, use the executable under `Release/`. Each output directory
must be new. The runner preserves commands, binary/native-source hashes, raw
stdout/stderr, exit/timeout outcomes and metrics. World construction and
reference review are outside advance timing. Samples use identical initial
states, not a sustained colony. The declared test EOS has constant heat capacity
and elastic water. `--no-reference` explicitly omits the expensive full solve;
comparison fields are null. Conservation still includes positive helium trace.
A passing capture does not mean its performance target passed.

On the Windows Ryzen 9950X, seven 100,000-cell **uniform** samples take
16.52–17.49 ms per 0.1 s, p50 16.79 ms, with zero mass/energy drift and no solves.
That run omits full-reference comparison; small exact-byte controls supply that
comparison. This is whole-owner equilibrium evidence, not active cells,
publication, sustained 1x or 5900X certification.

GNU Linux on the same host also passes all 21 registered native test groups.
Its seven 100,000-cell sleeping samples have p50 2.46 ms and p95 8.47 ms, again
with zero mass/energy drift and no full-reference solve at that size. MSVC
Windows passes the same 21 groups, the separate eight-group closed-owner
control, the 177-scenario managed declaration/FTZ-DAZ suite and the adapter
build (with its two existing assembly-version warnings). These scoped checks
do not establish current Unity rendering or a playable colony.

The managed suite additionally reproduces a Windows hover-review race: a
renamed file can be visible while the native rename DELETE handle is still
open. The reader now shares deletion, but never writing. The old reader rejects
the controlled DELETE handle; the repaired reader reads complete bytes, while
an unfinished exclusive writer still rejects. All four eager-publication tests
pass. This repairs the private review protocol, not the full colony gate.

For a 16×16 periodic 20 Pa pressure wave, five reference samples have p50/p95
1796.81/1840.48 ms. Five benchmark-only balanced samples have p50/p95
1043.78/1199.78 ms, constituent drift below 1.3e-15 and energy drift below 6e-16.
Endpoint differences from the reference are at most 1.17e-5/1.63e-5 relative
density/pressure and 0.00140 K temperature. Pressure differs by about 1.63 Pa,
roughly 8% of the 20 Pa perturbation; normalization only by the 100 kPa background
hides this flow error. Both modes reject 95 attempts over five samples. Larger
steps and lower order alone do not meet the target. Raw captures are in
`local/playtest-performance-r1`.

The active-cell target remains **25 ms per 0.1 simulated seconds for 100,000
cells on a Ryzen 5900X**, plus actual Windows/Linux game frame times, bounded
memory and no growing publication backlog. It remains open.
