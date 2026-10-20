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

As of 2026-10-19 all five gates are **open**. The [gate status](#gate-status)
records what exists, what is missing and what closes each one; the
[work plan](#work-plan) orders the remaining items.

The accepted 2026-10-18 [real-time completion amendment](completion-plan.md)
requires functional and real-time acceptance together: 250,000 active fluid
cells across all asteroids, 32 duplicants and sustained 1× on the Ryzen 5900X.
Its timestep/scaling work precedes broad activation. The earlier slow-playtest
proposal below remains historical. Current implementation and measurements are
in [production timestep progress](production-timestep-progress.md).

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
interior pillar and ledge pass in Linux Unity (2026-10-18), including colony
saves, fresh-process loads and natural autosaves
([saves and reloads](native-configured-domain.md#saves-and-reloads)). One
64×64 frame takes about 48 s of wall time.

**Missing.**
- Configured rooms on actual Windows and with occupants (actors, breathing,
  transfers). Their saves pass on Linux and under Proton.
- Fluid/solid heat exchange at interior faces, and solids that melt, fall or
  are dug inside the owner.
- More than one owner, and Spaced Out! worlds.
- Vacuum and free surfaces. `MacClosedWorld` admits exact empty cells
  ([vacuum](../native/MAC_VACUUM.md)), but its vacuum-front accuracy gate fails
  and the in-game owner still rejects empty cells. Opt-in material selector 3
  ([sharp interfaces](../native/MAC_TRANSPORT.md#sharp-gasliquid-interfaces-selector-3))
  keeps liquid in its own cells. In a diagnostic dam break it leaves an eighth of
  limited-linear's partly filled cells and under a tenth of its mist. The production
  kernel runs it. Production free-surface intervals pass their entropy audit
  through the [gravity reconciliation credit](../native/MAC_KERNEL.md#gravity-reconciliation-entropy);
  before it, any moving free surface faulted the production owner. Of the
  [gravity and multiphase gates](../native/MAC_GATES.md), hydrostatic gas, CO2
  settling, still water, the dam break and the standing wave pass. Two liquids
  and phase change are open, and the vacuum front fails.
- The material catalogue: 5 species against 212 inventoried material
  identities.
- Whole-world import. The Windows import staged 50,368 fluid cells, then
  rejected compressed-liquid placement
  ([status](implementation-status.md#game-integration-and-ownership)).
- Production activation without a development configuration.
- Coverage: all 3,074 [inventory entries](ownership-coverage.md) remain
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
  pressure/remap convention mismatch. The opt-in 1-D conversion now passes its
  interior point-velocity oracle; general 2-D conversion and strong-flow acceptance
  remain open ([component evidence](../native/MAC_NODAL_CONVERSION.md)).
- Primary-field remainders. [Persistent primary U](../native/MAC_PRIMARY_REMAINDERS.md)
  now carries a normal or flagged-range correction through both kernels, accounts,
  version-10 checkpoints and owner save framing. The unchanged standalone
  production water-heat control passes (+3.81470e-6 J against 0.00594585 J at
  123 frames). Release, ASan/UBSan and Ubuntu 22.04 each pass 69 CTests;
  ThreadSanitizer, MinGW/Wine and exact cross-platform continuation controls pass.
  Thermal ABI 2 now distinguishes certified idle intervals, preserving an exact
  physical-stage equation and positive heat/entropy progress. Release/Ubuntu each
  pass 69 CTests; affected sanitizer/Wine suites, 198 snapshot scenarios, 301
  Python tests (four skips) and fifteen isolated Linux domain controls pass.
  U-enabled LinuxPlayer and WindowsPlayer/Proton water-heat save/reload pass
  paused frame-zero saving and 120 exact continued frames. Actual Windows OS,
  the remaining current-package game cohort and volume remainders remain open.
  The opt-in ideal-gas constitutive volume component passes 80 responses with
  1,251 independent saved checks/eight corruptions. All 71 Release CTests and
  affected ASan/UBSan, ThreadSanitizer and MinGW/Wine suites pass, with identical
  scientific records. All 81 preceding MAC scientific outputs and the U-era wave
  SHA remain unchanged. World/remap integration, liquid/mixed volume and checkpoint
  persistence remain pending; no owner or saved selection uses it.
- Active-cell performance is open. The `06e88fd`
  [production-kernel baseline](../native/MAC_KERNEL.md#current-active-cell-baseline)
  with automatic multigrid, persisted U and the pressure-storage ledger measures
  p95 516–546 ms at 100,000 active cells and 1,388–1,391 ms at 250,000 per 0.1
  simulated seconds on twelve workers, against 25/62.5 ms before adapters.
  Each shape has two pinned 21-interval evolving-wave captures on the shared
  Ryzen 9 5900X, with loads sampled every five seconds (2.35–8.69). Every cell
  changes; all conservation checks pass and paired checkpoints match at all four
  measured sizes. Standalone peak RSS is 805.6 MiB at 250,000 (3,379 B/cell).
  Additional game/adapter memory, 32 duplicants and colony FPS remain unmeasured.
  The user has relaxed per-stage numerical
  contracts to interval audits (2026-10-18).
  Geometric multigrid now uses the production pool, with cached geometry and
  pressure-active masks.
  Production selection persists in checkpoint version 6; ordinary restore keeps
  the kernel, saved preconditioner and diffusion. The seven original saved-review
  scenes pass production with unchanged strict endpoint bounds (105,985 checks,
  seven corruptions rejected on Linux and MinGW/Wine). They exposed a wall-reaction
  cancellation error, fixed by gathering pressure components before the common
  timestep/area product. Material-boundary uncertainty requests strict-reference
  refinement. The old interval solver/cache/bridge are removed; selector-1 owners
  and saves explicitly reject as retired. Counted production diffusion now rounds
  shared below-range transfers and audits their errors without widening the
  original endpoint conservation bounds. Quantized binary, ternary retry, shared
  budget and row continuation controls pass. All four direct/composed physical/100×
  8,192-cell cases complete 400 frames with the original aggregate helium bound
  and unchanged bulk metrics. The saved reviews pass 2,690/131,154 checks,
  rejecting six/three altered captures. All 55 Release CTests, ten ASan/UBSan,
  ten MinGW/Wine and nine ThreadSanitizer suites pass.
  Accepted below-range primal advection products now have exact error ledgers
  and counts, with unchanged parcel values and endpoint bounds. An 8,192-cell
  advected front completes 160 intervals with 44,420 rounded products and the
  original aggregate tracer bound. The saved reader passes 50,825 checks/seven
  altered captures; a 33,389-cell control preserves exact state/ledger results
  with one/twelve workers, checkpoint continuation and rollback. Pressure-J
  additions now have counted half-ulp charges within the unchanged interval
  bound, with identical endpoint images and refinement counters. The saved
  reader passes 10,974 checks/eighteen corruptions on Release, ASan/UBSan,
  ThreadSanitizer and MinGW/Wine. Pressure products and other
  transient momentum/energy rounding ledgers remain open. Incoming constituent/
  passive gathers now have audited storage-write counts: 70 of 80 independent
  controlled additions, with 1,431 saved checks/fourteen altered captures rejected.
  The original bounds, stored values and six version-6/7/8 endpoint/continuation
  images remain unchanged. Primary U now has persisted corrections and audited
  writes; retained constituent/passive subtractions are outside
  this ledger. Flagged pressure sweeps now
  survive until parcel formation; canonical dual J and normalized kinetic
  products fix the reproduced large-inertia fault. Signed and quantized arrivals,
  twelve-worker cache/continuation, concurrency and shared-budget refinement
  controls pass. The expanded saved reader passes 1,318 checks/ten
  altered captures on Release, ASan/UBSan, ThreadSanitizer and MinGW/Wine, including a normal sweep divided by a very large material
  volume and counted unresolved primal writes. It retains representable and
  zero/one/two-quantum arrivals without changing conservation bounds. A
  cross-block tracer uses the same shared transfer with one/twelve workers.
  Step f now selects production in the native owner, with twelve requested
  workers and committed physical heat/work diagnostics. Supported reference
  saves retain idle bytes; the first live frame migrates to version 6 with
  journal flag 4. Historical unflagged replay and stateless continuation keep
  their saved kernel. Fourteen historical owner images pass native restore,
  continuation and migration checks on Linux and MinGW/Wine. The managed wrapper
  admits production physical version 6 without changing outer payload bytes.
  A Linux production gas source saves, reloads in a fresh process, resaves at
  paused frame zero and matches 120 continued frames. The frozen `73abf47`
  package passes twenty-eight Linux pairs, including both configured rooms,
  and four WindowsPlayer/Proton pairs (solid heat, gas, water and paired
  checkpoints); all reloads verify paused frame-zero saving and 120 exact frames.
  Persisted U fixes the frozen water-heat energy failure; the unchanged standalone
  control passes (+3.81470e-6 J against 0.00594585 J). Thermal ABI 2 distinguishes
  certified idle intervals. The U-enabled Linux water-heat source/save passes at
  frame 122 with zero rejected attempts, zero projected endpoint residual and
  maximum relative energy error 7.12279e-20. Fresh-process reload, paused
  frame-zero saving and 120 exact continued frames pass. The matching
  WindowsPlayer/Proton pair also passes (source frame 121, zero rejected attempts
  and endpoint residual, 120 exact continued frames). The remaining current-package
  game cohort is pending. These
  bounded runs do not certify a colony or actual Windows OS execution. The current
  Ubuntu compatibility build passes 69 tests, and both shared libraries meet the
  glibc 2.34 ceiling. The builder and package provenance check enforce that ceiling;
  their helper tests run independently. The user-authorized `3fbccfb`
  gravity entropy credit is recorded in the central contract. Calm-region/local
  steps and local subcycling remain open. The opt-in
  [two-moment pressure method](../native/MAC_PRESSURE_MOMENTS.md) passes
  selected larger-step resolved waves with the original error bounds and
  persists its real Schur/coupled implementation in version 7/8. Default
  schedules and earlier images retain their method, bytes and continuation.
  The coupled saved review passes 114,726 checks/fourteen corruptions on
  Release, ASan/UBSan and MinGW/Wine; threaded controls pass ThreadSanitizer.
  Version-8 images retain exact fresh Linux/Wine continuation; composed
  diffusion and subnormal-front controls pass. The coupled method still fails
  the original collision density refinement (0.861766 against 0.85). A
  [shared-face time primitive](../native/MAC_TIME_MORTAR.md) passes
  1,057 saved checks/fourteen corruptions on all four native routes. It is not
  connected to the owner. A
  [pressure-window component](../native/MAC_PRESSURE_WINDOWS.md) now streams
  prescribed fine pressure roots with shared face fluxes. Synthetic 1:4/1:8
  controls pass resolved references and the original energy bound; the mixed
  1:8 case uses 88 physical cell intervals against 256 uniform-fine intervals.
  Its independent endpoint review passes 3,307 checks/nineteen corruptions.
  Wall/body/empty-cell accounts, cache, rollback and twelve-worker controls
  pass. A cached macro action and a preconditioner over all occupied cells
  reduce the mixed 250,000-cell diagnostic from 60 to 16 outer iterations.
  Eight fixed double-double chains per Krylov dot block preserve the preceding
  captures and reduce paired warm means by 15–27%. Current mixed pressure-only
  samples are 1,407–2,031 ms per .1 s (sampled loads 17.26–21.88); these
  fixed-source captures do not certify evolving owner speed.
  An unselected full-flow probe passes the fixed 256x2 wave with .025 s mixed
  windows (0.770% wave error); .1 s mixed windows fail at 8.489%, against reference
  steps at most .0005 s. Its local remap fails strict trace accuracy without
  a reconstruction halo; two extra stencil layers pass one front comparison
  (0.330% helium, 0.445% passive), refining most of that small room. The same
  sharp front fails over one second (413.8% maximum helium, 4.55% final passive);
  a smooth control passes at 0.139%. A heuristic roundoff-margin probe passes
  at 0.976%, but its envelope is unproved and unselected. Reference
  resolution and automatic scheduling remain open. The committed component has no owner selection,
  interval audits or local transport. Automatic selection, full local subcycling, open
  physics gates, real-time acceptance and in-game production validation remain open.

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
  endpoint. The solid control now passes Linux Unity source/save and a
  fresh-process reload with matching completed publication leases and 120
  exact resumed frames. Whole-colony solid heat remains open.

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
1e-8 bound. Configured 7×5 rooms with interior solids save and reload with
exact continuation on Linux (gas and water) and under Proton (gas). Saves use
payload version 4, and the loaded grid must reproduce the saved solid mask
([configured saves](native-configured-domain.md#saves-and-reloads)).

**Missing.**
- Loading outside the development restore mode, including from the load
  menu; overwrite save is untested.
- Asteroid switching with a native domain. Only the presentation fixture
  exercises it.
- Rejection of mixture saves loaded without the mod.
- Durability of interrupted saves.
- Actor saves and configured-room saves on an actual Windows installation,
  not only Proton; an in-game load of a tampered configured save.
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
A configured 7×5 gas room passed 8,401 frames, stopping just before the
duplicants died. ONI's natural autosaves in cycles 2 and 3 were admitted and
paired, and a load of the cycle-2 autosave continued exactly.

**Missing.**
- Gates 1–4.
- A colony plan. The harness starts a colony but gives its duplicants no
  orders, and they die in cycle 3–4, so ten cycles need life support from a
  scripted build order or a prepared save.
- The game runs at 1× speed, so ten cycles take at least about 1.7 hours of
  wall time.
- Natural autosaves happen about once per cycle. Two have been paired within
  one run; ten cycles of them have not.
- Milestone D's 100,000-cell presentation performance and frame-time
  evidence.

**Closes when** the [playtest definition](#playtest-definition-proposed)
passes on Linux and Windows.

## Ownership strategy and open decisions

The completion plan gives every fluid to the custom engine and keeps solids
and buildings with native ONI. Today's in-game owner is `MacClosedWorld`
behind `ma_oni_domain_*`.

1. **Decided 2026-10-18:** complete whole-world ownership through the native
   hooks after vacuum, free surfaces and active-cell scaling. Computational
   blocks must share physical fluxes and must not become sealed room walls.
2. **Decided 2026-10-18 by the user:** `MacClosedWorld` (`native/src/mac_*.cpp`)
   is the production colony solver. ONI's native fluid solver is what the mod
   replaces and is not used. The managed `FluidWorld`/coupled path
   ([coupled ABI](../native/COUPLED_ABI.md)), which has no game frame hook, is
   not the production route.
3. **Decided 2026-10-19:** the first ten-cycle playtest covers the base game
   and Spaced Out!. The gameplay packs follow later.

## Work plan

Ordered by dependency. Size: S < M < L < XL. "Game" marks items that need the
licensed game to verify.

| # | Gate | Item | Size | Game |
| --- | --- | --- | --- | --- |
| 1 | 1 | Configured sealed domain: explicit physics settings, interior solid cells, per-cell ownership and a larger bound (native done) | L | Later |
| 2 | 5 | Long-run harness opt-in with bounded telemetry (done; runs in the Linux game) | M | Later |
| 3 | 1 | Managed adoption of item 1 and in-game rooms with interior solids (done on Linux Unity, with saves, reloads and long runs; saves also pass under Proton. Actual Windows and occupied rooms remain) | M | Yes |
| 4 | 1 | Production activation and message routing outside the validation classes | M | Yes |
| 5 | 2 | Pressure/remap convention conversion, strong-flow rerun and default promotion | L | No |
| 6 | 2 | Range-extended gravity and persisted U corrections; volume remainders and game verification remain | M–L | Later |
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
| Certified exact equilibrium | Avoid repeated pressure, reconstruction and heat solves | No physical approximation. Connected inventories must be identical at zero gravity; disconnected regions may differ. Exact discrete hydrostatic force balance is also admitted, with no enabled thermal flux. |
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
Identical connected inventories at equal volume imply identical EOS
pressure/temperature; zero momentum and gravity make the current
mechanical/thermal fluxes zero. Disconnected sealed regions may differ.
Hydrostatic certification instead requires exact discrete pressure/body-force
cancellation at every interior face and equal neighbouring temperature where
conduction is enabled; the body and wall impulse ledgers still advance.
Different passive amounts remain unchanged: this owner has no passive diffusion.
The certificate covers the entire owner. An evolving connected region disables
it, so static regions beside evolving regions are not yet skipped individually.

Only time and revision advance. The report records one certified interval,
`exact_equilibrium_intervals=1`, and zero pressure/thermal stages; no work counts
are invented. Physical settings, checkpoint schema and all state values remain
unchanged. Adding diffusion, phase kinetics or another evolving process requires
extending this certificate first.

`ma_mac_equilibrium_tests` compares complete checkpoint bytes with full solves
for gas/water, walls, conduction, viscosity and reload. It checks wake-up for
heat edits, one-ULP energy/trace changes, normal and below-range momentum, and
gravity; invalid arguments reject before shortcuts. The original controls pass
on MSVC Windows and GNU Linux. New disconnected and hydrostatic controls pass
on GNU Linux; actual Windows evidence for these additions remains open.

## Reproduce measurements

```sh
cmake -S . -B build-playtest -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-playtest --config Release --target ma_mac_playtest_benchmark ma_mac_equilibrium_tests
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/wave-reference --profile reference --scene wave
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/wave-balanced --profile balanced --scene wave
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/still-100k --profile equilibrium --scene uniform --width 400 --height 250 --samples 7 --no-reference
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/evolving-250k --profile equilibrium --scene wave --width 1000 --height 250 --samples 7 --continuation --no-reference --timeout 120
```

For Visual Studio, use the executable under `Release/`. Each output directory
must be new. The runner preserves commands, binary/native-source hashes, raw
stdout/stderr, exit/timeout outcomes and metrics. World construction and
reference review and explicit checkpoint capture are outside advance timing.
Samples use identical initial states by default; `--continuation` advances the
previous completed sample. Neither mode is a sustained colony. Schema 2 records
actual changed-cell counts, physical selectors, disturbance-normalized pressure
error, O2 partial pressure and every positive constituent including traces.
Uniform scenes cannot claim active-cell performance. Every successful runner
capture includes `endpoint.mac`; `--compare-checkpoint` checks exact continuation
against a prior endpoint. The declared test EOS has constant heat capacity
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

Those measurements precede the latest arithmetic and execution changes; see
[current measurements](production-timestep-progress.md). The active-cell targets
remain **25 ms per 0.1 simulated seconds for 100,000 cells** and **p95 62.5 ms
solver-plus-adapter time for 250,000 active cells and 32 duplicants on a Ryzen
5900X**, plus sustained 1×, 30 FPS, bounded memory and no growing publication
backlog. Both remain open.
