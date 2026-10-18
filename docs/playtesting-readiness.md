# Playtesting readiness and simulation cost

The full overhaul is **not ready for an ordinary colony playtest**. The default
installation remains a development probe. A visible development chamber does
not establish whole-colony mixture ownership.

The next playable gate needs:

1. Colony fluid ownership across walls, vacuum, liquids and every supported
   constituent.
2. A reliable production timestep.
3. Complete transactions for breathing, machines, pipes, storage, construction,
   heat and callbacks, using actual inventory and energy reservations.
4. Complete saving of these owners and live objects: manual saves, autosaves,
   reload and asteroid switching on both OSes.
5. Ten actual colony cycles with life support, coherent rendering, every
   non-trace gas on hover, harmful-trace warnings and bounded simulation and
   publication latency. See [milestone D](completion-plan.md#d-first-playable-development-colony).

As of 2026-10-18 all five gates are **open**. The [gate status](#gate-status)
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
3×2 room inside an insulating Unobtanium/Granite border. Six-cell gas and
water scenes pass on Linux Unity and Windows; the gas/liquid mixed scene passes
on Linux Unity and WindowsPlayer under Proton. The domain starts only
from a development `colony-validation.json`.

The additive [configured domain](native-configured-domain.md) (scenario 7)
adds explicit physics settings, interior solid cells with per-cell hook
suppression, and rectangles up to 256 cells per side within 4,096 cells.
Interior solids are impermeable and adiabatic, and must be static and
insulating. Its native groups pass on Linux, under sanitizers and as MinGW
binaries under Wine. A managed adapter and 7×5 gas and water rooms with an
interior pillar and ledge pass in Linux Unity (2026-10-18). One 64×64 frame
takes about 48 s of wall time.

**Missing.**
- Configured rooms in saves, on Windows and with occupants (actors, breathing,
  transfers).
- Fluid/solid heat exchange at interior faces, and solids that melt, fall or
  are dug inside the owner.
- More than one owner, and Spaced Out! worlds.
- Vacuum and free surfaces. Both the in-game owner and the MAC solver reject
  empty cells. The separate `FluidWorld`
  [gas/vacuum front gate](../native/GAS_VACUUM.md) also fails.
- The material catalogue: 5 species against 212 inventoried material
  identities.
- Whole-world import. The Windows import staged 50,368 fluid cells, then
  rejected compressed-liquid placement
  ([status](implementation-status.md#game-integration-and-ownership)).
- Production activation without a development configuration.
- Coverage: all 3,072 [inventory entries](ownership-coverage.md) remain
  `unimplemented`.

**Closes when** milestone A's exit holds for the playtest colony on both
operating systems. That means visible custom mixtures, working native solids
and buildings, correct callbacks, and no native fluid ownership, with traced
reader/writer audits. `python3 tools/coverage_inventory.py gate` covers only
the full release scope; the playtest still needs a defined subset
(decision 3 below).

### Gate 2: production timestep

**Exists.** The in-game owner advances `MacClosedWorld` with a 0.01 s maximum
step, rejected-attempt retries and certified
[exact equilibrium](#implemented-exact-equilibrium). Sod passes with the
production defaults, which still fail collision density refinement (0.856
against 0.85). Opt-in selector combinations with
[compressive work](../native/MAC_PRESSURE.md#coupled-compressive-pressure-work-mode-3)
also pass collision.

**Missing.**
- Accuracy. Strong-flow separation fails the finest-grid velocity bound and
  the central density/pressure bounds ([gate](../native/MAC_STRONG_FLOW.md)). The
  [curved-nodal diagnostic](../native/MAC_STRONG_FLOW.md#curved-deformation-pressure-and-remap-convention) localizes a
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
  callbacks ([radial](native-domain-transactions.md#radius-consumption),
  [rectangular](native-domain-transactions.md#rectangular-consumption)).
- Heat edits, additive [cell-mass edits](native-domain-transactions.md#additive-cell-mass-edits), and
  registered consumer/emitter actors.
- [Breathing](native-domain-readers.md#breathing-selection) and
  [pressure](native-domain-readers.md#physical-pressure) readers.
  [Live breathing](native-domain-transactions.md#live-duplicant-breathing) covers one tracked duplicant.
- [Body heat](native-domain-physics.md#native-object-heat) and one [solid-heat](native-domain-physics.md#native-solid-cell-heat)
  endpoint (native harness only; its Unity control still fails).

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
([actors](native-domain-persistence.md#durable-point-actors),
[day autosave](native-domain-persistence.md#natural-day-autosaves), [menu reload](native-domain-persistence.md#same-process-main-menu-reload)).
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
hazardous-trace warnings, overlays and colour-only mode on Windows and Linux
([hover procedure](mixture-presentation.md#tile-hover-acceptance-procedure)).
Windows binds hover and GPU rendering to a completed native frame
([rendering](native-fluid-rendering.md)), and the
[2026-10-17 Windows checks](windows-validation.md#windows-game-checks-2026-10-17)
repeat the hover and overlay checks.

The validation harness accepts an explicit
[long-run request](../tools/ONI_HEADLESS.md#long-development-runs) of up to
100,000 frames and 48 hours (ten cycles are about 30,000 frames). It appends
flushed telemetry: domain status, conservation error, native wait,
publication and presentation latency, and memory. The longest in-game run,
a six-cell Linux long run, reached 8,552 domain frames before every
duplicant died in cycle 3–4; the harness now fails a long run when the
colony is lost. In that room the native frame wait was below 0.1 ms, while
the harness's own checks and snapshot copy cost about 74 ms p95 per frame.

**Missing.**
- Gates 1–4.
- A colony plan. The harness starts a colony but gives its duplicants no
  orders, and they die in cycle 3–4, so ten cycles need life support from a
  scripted build order or a prepared save.
- The game runs at 1× speed, so ten cycles take at least about 1.7 hours of
  wall time.
- Natural autosaves happen about once per cycle and have never run at this
  scale.
- Milestone D's 100,000-cell presentation performance and frame-time
  evidence.

**Closes when** the [playtest definition](#playtest-definition-proposed)
passes on Linux and Windows.

## Ownership strategy and open decisions

The completion plan gives every fluid to the custom engine and keeps solids
and buildings with native ONI. Today's in-game owner is `MacClosedWorld`
behind `ma_oni_domain_*`.

1. **Open:** grow ownership from sealed rooms, or move to whole-world ownership
   at once. Rooms need permeable owner boundaries, which the sealed contract
   forbids. Whole-world ownership needs vacuum, free surfaces and the full
   catalogue first.
2. **Decided 2026-10-18 by the user:** `MacClosedWorld` (`native/src/mac_*.cpp`)
   is the production colony solver. ONI's native fluid solver is what the mod
   replaces and is not used. The managed `FluidWorld`/coupled path
   ([coupled ABI](../native/COUPLED_ABI.md)), which has no game frame hook, is
   not the production route.
3. **Open:** define the playtest coverage subset by configuration (base game,
   Spaced Out!) or by reachable gameplay systems.

The work plan proceeds with items that hold under either answer to the open
decisions.

## Work plan

Ordered by dependency. Size: S < M < L < XL. "Game" marks items that need the
licensed game to verify.

| # | Gate | Item | Size | Game |
| --- | --- | --- | --- | --- |
| 1 | 1 | Configured sealed domain: explicit physics settings, interior solid cells, per-cell ownership and a larger bound (native done) | L | Later |
| 2 | 5 | Long-run harness opt-in with bounded telemetry (done; not in game) | M | Later |
| 3 | 1 | Managed adoption of item 1 and in-game rooms with interior solids (Linux Unity passes; saves, Windows and occupied rooms in progress) | M | Yes |
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
| 20 | 5 | Scripted playtest colony plan or prepared save for unattended runs | M | Yes |
| 21 | 5 | Ten-cycle playtest and 100,000-cell presentation performance on both OSes | L | Yes |

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
pass.

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

| Measurement | Result |
| --- | --- |
| 100,000-cell **uniform** equilibrium, MSVC Windows on a Ryzen 9950X, 7 samples | 16.52–17.49 ms per 0.1 s, p50 16.79 ms; zero mass/energy drift; no solves |
| Same, GNU Linux on the same host | p50 2.46 ms, p95 8.47 ms; zero mass/energy drift |
| 16×16 periodic 20 Pa pressure wave, reference, 5 samples | p50/p95 1796.81/1840.48 ms per 0.1 s |
| Same, benchmark-only `balanced`, 5 samples | p50/p95 1043.78/1199.78 ms; constituent drift < 1.3e-15, energy drift < 6e-16 |

The 100,000-cell rows omit the full-reference comparison (small exact-byte
controls supply it). They are whole-owner equilibrium evidence, not active
cells, publication, sustained 1× or 5900X certification. The balanced endpoint
differs from the reference by at most 1.17e-5/1.63e-5 relative density/pressure
and 0.00140 K; pressure differs by about 1.63 Pa, roughly 8% of the 20 Pa
perturbation, which normalization by the 100 kPa background would hide. Both
modes reject 95 attempts over five samples. Larger steps and lower order alone
do not meet the target.

The active-cell target remains **25 ms per 0.1 simulated seconds for 100,000
cells on a Ryzen 5900X**, plus actual Windows/Linux game frame times, bounded
memory and no growing publication backlog. It remains open.
