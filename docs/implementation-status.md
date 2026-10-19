# Implementation status

Current state on 2026-10-19. This is a private development milestone: the
overhaul is incomplete and no first-playable or release gate has passed. The
accepted [completion plan](completion-plan.md) is the execution target and
[playtesting readiness](playtesting-readiness.md) tracks the five gates to an
ordinary playtest. Each row below says what exists, where it is verified and
what is open; the linked document owns the contract and reproduction steps.

The production architecture is [native SimDLL hooks](native-backend-integration.md)
(accepted 2026-10-16). The production colony fluid solver is `MacClosedWorld`
(`native/src/mac_*.cpp`), decided by the user on 2026-10-18. ONI's native fluid
simulation is what the mod replaces; it is never used as the solver. Today
`MacClosedWorld` runs only in a bounded development room, and the surrounding
colony keeps native ONI physics.

The [real-time timestep implementation](production-timestep-progress.md) adds
checked IPO, exact arithmetic fast paths, fused pressure thermodynamics,
deferred full-frame audits and disconnected/hydrostatic equilibrium controls.
The next slice retains full-range vertical gravity work and potential flux, with
10,000-step and checkpoint controls. Primary-U rounding remainders still need
persistent storage; these standalone controls do not close the physics gate.
Sparse transport accumulation now allocates full-range accounts only for present
constituents and incoming donors, removing the former 16-million-slot product cap.
A 4,097-species/4,096-cell EOS parcel control passes; primary inventories and
reconstruction fields remain dense ([transport contract](../native/MAC_TRANSPORT.md#sparse-constituent-workspace)).
Compact normal accumulators preserve the original full-range expansion component
for component while reducing their object size from 128 to 72 bytes; all 38
comparable saved MAC captures remain byte-identical. The strong-flow failure remains.
The [large-grid exact-sum follow-up](production-timestep-progress.md#large-grid-exact-sum-capacity)
also removes the 4,096-component accounting limit without dropping terms. A
250,000-cell wave completes one 0.01-second step and exact checkpoint restoration,
but takes about 30 seconds; the sustained real-time gate remains open.
A [pressure-increment restart](../native/MAC_PRESSURE_INCREMENTS.md) now resolves
previously stalled larger implicit steps at the original geometry and accounting
bounds. Actual U/K/G, coupled liquids and checkpoint continuation pass bounded
controls. The larger-step wave accuracy comparison still fails; defaults remain
unchanged.
The heat stage also reuses validated EOS readings with explicit per-pair updates;
72 replay stages and the 27-test Linux suite pass, with the heat/compression saved
outputs unchanged. Coupled pressure now retains full incoming motion and sweeps,
checking actual per-cell work and exposing unresolved U updates; primary remainders
remain unpersisted. Physics and real-time colony acceptance remain open.
The coupled constitutive callback now reuses completed isentropic queries; its
saved work/pressure/owner/range outputs remain exact, and the Linux suite has 29
passing tests. This reduces solver cost without changing a physical selector.
The latest paired 256-cell wave takes about 436 ms with exact baseline continuation,
but a 4,096-cell wave still takes about 6.63 seconds per 0.1 simulated seconds and a
250,000-cell sample timed out at 45 seconds. The accepted 250,000-cell/32-duplicant
real-time target and every first-playable gate remain open.

Conservative [gas diffusion](../native/MAC_DIFFUSION.md) is merged into main as
formulation 2. It supplies species-specific exchange with carried enthalpy,
physical and accelerated modes, completed-frame configuration and versioned
checkpoints (`MACCLS01` version 5 for reference; version 6 for production
midpoint, or 7/8 with opt-in two-moment pressure).
Historical selector-1 version-4/5 images explicitly reject after retirement.

The Maxwell–Stefan driving force includes pressure diffusion, in a symmetric
positive-semidefinite discrete form, so its first-order entropy production
cannot be negative. Faces touching empty cells carry no flux. Correlations
outside their validated ranges are extrapolated, combined with the cell's
Knudsen limit and counted.

These pass:

- binary analytical controls (1.1e-5 of the perturbation);
- independent Maxwell–Stefan ordinary-air comparisons (at most 0.0407%
  mixing-time model error);
- the review's opposing-gradient, empty-cell and 200–600 K, 1e-9 Pa–500 kPa
  controls in both modes;
- Linux address and undefined-behaviour sanitizers.

On 2026-10-18 the native Linux game ran with the default physical mode (ONI
744825, Ubuntu 22.04 compatibility libraries, private display). The six-cell
mixed-air scene and the 7×5 configured gas room each took a paired save. A
fresh-process load resumed each one exactly for 120 frames. The configured
water room, where diffusion is off, matched its earlier 2,562 substeps with no
rejected attempt.

Two known limits remain:

- The retained reference diffusion path rejects a locally introduced gas front
  once positive arrivals leave binary64 range. Production now uses counted
  shared-transfer rounding. Primal advection products also have counted error
  ledgers; game adoption remains open.
- Diffused traces beside a strong pressure jump reach an existing flow-solver
  rejection.

The Windows 9950X benchmark of formulation 1 measured 5.06 ms p95 for certified
uniform inventories and 188.32 ms for active mixing per 0.1 simulated seconds
at 250,000 cells. On a shared Linux host, formulation 2's times overlap
formulation 1's. Active diffusion misses its proposed 6.25 ms allocation; this
is not the required Ryzen 5900X colony/adapter benchmark. The Windows evidence
predates formulation 2: six-cell physical/100× runs, a diffusion-enabled save
and restore, and 120-frame replay. See the
[validation record](gas-diffusion-validation.json).

An opt-in [1-D nodal momentum/kinetic conversion](../native/MAC_NODAL_CONVERSION.md)
now passes the original curved-deformation point oracle with independently checked
work and inventories. It preserves full-range momentum, carries conversion work
with material, and restores exactly from checkpoints. It has not passed strong-flow
acceptance or supplied the general 2-D conversion; production defaults are unchanged.

## Verification scopes

| Term | Meaning |
| --- | --- |
| Linux Unity | Native Linux ONI 744825 (LinuxPlayer) through [`tools/oni_headless.py`](../tools/ONI_HEADLESS.md) |
| Windows | Windows OS with MSVC-built libraries through `tools/run_colony_validation.ps1` ([Windows validation](windows-validation.md)) |
| WindowsPlayer/Proton | The Windows player under official Proton 10 on Linux; real WindowsPlayer execution, not Windows OS evidence |
| Native harness | The licensed SimDLL in an isolated console process ([NativeShadowProbe](../tools/NativeShadowProbe/README.md)), natively or under Wine; no Unity |
| Standalone | CTest, managed and Python suites; no game process |

"Bounded" means the six-cell (3×2) sealed development room of the
[sealed native domain](sealed-native-domain.md) unless stated otherwise. Unit
tests never count as in-game acceptance.

## Game integration and ownership

| Area | Exists and verified | Open |
| --- | --- | --- |
| [Native hook backend](native-backend-integration.md) | Exact-build ONI 744825 profiles for Windows x64 and Linux x86-64 with code fingerprints and assembly/SimDLL hashes checked at load. The opt-in forwarding observer preserves native physics; actual Unity colony forwarding passes on Windows and Linux, plus isolated native comparisons on both | Complete native reader/writer coverage; production takeover |
| [Linux trampoline storage](native-platform-numerics.md#linux-trampoline-storage) | Hooks use verified zero padding in the game's executable page, with bounded capacity and exact byte/permission restoration on teardown | New profiles for any other game build |
| [Sealed native domain](sealed-native-domain.md) | `MacClosedWorld` behind `ma_oni_domain_*` advances once per positive native worker frame inside ONI's schedule. One rectangle of 2–16 cells per side, five species (N2, O2, He, H2O, CO2), seven fixed scenarios (0–6). Mixed-air and compressed-water scenes pass in Linux Unity, Windows and WindowsPlayer/Proton with calibrated GPU checks; gas/liquid coexistence passes in Linux Unity and WindowsPlayer/Proton | Starts only from a development `colony-validation.json`; one owner; no vacuum or free surfaces |
| [Configured domain](native-configured-domain.md) | Scenario 7: explicit physics settings, static insulating interior solids with per-cell hook suppression, rectangles up to 256 per side within 4,096 cells. Native groups pass on Linux, sanitizers and Windows binaries under Wine. A managed adapter and 7×5 gas/water rooms with interior solids pass in Linux Unity (2026-10-18), including [paired saves](native-configured-domain.md#saves-and-reloads), fresh-process loads, natural autosaves and an 8,401-frame long run; the gas save and load also pass under Proton | Actual Windows runs, occupied rooms, fluid/solid heat at interior faces, changing solids; a 64×64 frame takes about 48 s |
| [Initial inventory admission](native-domain-physics.md#initial-inventory-admission) | The room starts from a completed native `GameDataUpdate`, admitting every native gas/water tuple before flow fencing. Linux Unity and WindowsPlayer/Proton pass source, save and exact reload | General world/geometry import, declared reference EOS only |
| Whole-world import | The v15 importer stages all generated worlds read-only. The Windows run staged nine worlds and 50,368 fluid cells, then **rejected** compressed-liquid placement: 28 pore components cannot fit the declared reference volume (minimum deficit 11.87 m³) | Conservative placement without deleting mass or inventing headspace ([interop contract](interop-contract.md)) |
| [Coverage inventory](ownership-coverage.md) | 3,074 entries (materials, native messages/imports/hooks, channels, call sites, systems, prefabs, converters, recipes), each with owner, adapter and acceptance test; validated by the normal build | All entries are `unimplemented`; `gate` exits nonzero until acceptance is complete |
| [Inspected native boundary](interop-contract.md) | Message IDs, unmanaged layouts, phase order and static callers of build 744825; earlier vacuum-shadow experiments are historical controls | Independent reader/writer proof on both OSes |

## Transactions at the owner boundary

All of these run inside the bounded owner on the native worker clock, commit
atomically with receipts and deliver original game callbacks once.

| Area | Exists and verified | Open |
| --- | --- | --- |
| [Gas and water point transfers](sealed-native-domain.md#gas-point-sources-and-game-callbacks) | Injection and selective consumption carry energy, momentum and gravity potential; filled water carries compression energy. Linux Unity passes gas and water; Windows passes the CO2 and gravity transfer scenes; the native harness passes on Linux and Wine | Windows Unity water point transfers; device power/input reservations |
| [Registered point actors](sealed-native-domain.md#registered-gas-actors) | Real `ElementConsumer`/`ElementEmitter` registrations with rate changes, blocked callbacks and removal; owned batches commit atomically while unrelated native batches keep their route. Linux Unity and Windows pass gas and water scenes | Spatial/bulk actors, production devices |
| [Radius consumption](native-domain-transactions.md#radius-consumption) | Radius-three breathing requests allocate breadth-first over connected owned cells with one aggregate binary32 callback. LinuxPlayer and WindowsPlayer sources and local/cross-player reloads pass | Suit/tube providers |
| [Rectangular consumption](native-domain-transactions.md#rectangular-consumption) | Width/height footprints reserve before dispatch; mixed-owner requests reject. Linux native controls pass | Unity source/save/reload controls; pumps and pipes |
| [Additive cell-mass edits](native-domain-transactions.md#additive-cell-mass-edits) | Real `CO2Manager` breath particles deposit into the owner. Linux Unity and WindowsPlayer/Proton pass | Complete metabolism |
| [Breathing reader](native-domain-readers.md#breathing-selection) and [live breathing](native-domain-transactions.md#live-duplicant-breathing) | Duplicant breathing reads completed mixture oxygen under the configured policy (Linux Unity and WindowsPlayer/Proton). One real duplicant breathes in the room, passing two Linux Unity reload sessions | Every duplicant and critter; WindowsPlayer live-breathing reloads; conserved metabolism |
| [Pressure reader](native-domain-readers.md#physical-pressure) | `Grid.Pressure` and its five game callers read physical pressure for owned cells. Linux Unity and WindowsPlayer/Proton pass | Mechanical forces and device pressure semantics |
| Heat edits and [body heat](native-domain-physics.md#native-object-heat) | Game heating messages commit conserved external heat (Linux Unity, Windows). Element-chunk contacts couple duplicants and items to full-EOS cells; Linux Unity and WindowsPlayer/Proton sources and local/cross-player reloads pass | Building heat |
| [Solid-cell heat](native-domain-physics.md#native-solid-cell-heat) | Registered solid faces exchange represented heat through the exact-build temperature hook. Exported temperatures now use a matching completed-frame lease. Linux Unity and WindowsPlayer/Proton source/save and fresh-process reloads pass, including 120 exact resumed frames each | Whole-colony/changing-solid heat and actual Windows OS/MSVC acceptance of the publication repair |
| [Binary32 wire values](native-platform-numerics.md#binary32-wire-values) and [arithmetic scope](native-platform-numerics.md#solver-arithmetic-scope) | Integer-built binary32 decoding preserves subnormal parcels under the game's DAZ/FTZ; `ArithmeticScope` sets the solver's floating-point environment on each executing thread and restores the caller's afterwards | — |

## Physics in the in-game owner

| Area | Exists and verified | Open |
| --- | --- | --- |
| [Gas/liquid coexistence](native-domain-physics.md#occupied-gasliquid-cells) | Gas and liquid constituents share a cell with actual EOS partial volumes, one mechanical pressure and one stored U. Linux Unity and WindowsPlayer/Proton pass source and reload | Liquid interface rendering, free surfaces, phase change |
| [Internal conduction](sealed-native-domain.md#internal-fluid-conduction), [mixed-cell conduction](native-domain-physics.md#mixed-cell-conduction), [stored-energy rounding](../native/MAC_HEAT.md#physical-conduction-and-stored-energy-rounding) | Backward-Euler thermal network with closed energy and entropy checks; mixed cells use the full EOS. Linux Unity, Windows and WindowsPlayer/Proton pass | Variable properties, fluid/solid exchange inside owner, strict cross-player agreement |
| Gravity: [gas](sealed-native-domain.md#gravity-and-carried-potential-energy), [filled water](native-domain-physics.md#filled-liquid-gravity) | Body work and carried potential in the U+K+G account. Linux Unity and Windows pass, including main-menu reloads. Standalone full-range gravity, hydrostatic and stratified refinement controls pass | Persistent primary-U rounding remainders, general hydrostatic certification, free surfaces; native adapter scalar projection limits and new full-range game evidence |
| [Viscosity](native-domain-physics.md#physical-viscosity) | Constant Newtonian shear/bulk surrogates, free-slip walls. Linux Unity and WindowsPlayer/Proton pass source and local/cross-player reloads | Material laws, no-slip and interface geometry |
| [Exact equilibrium](playtesting-readiness.md#implemented-exact-equilibrium) | Certified whole-owner shortcut with no tolerance; MSVC and GNU pass | Sleeping regions inside an active colony |

## Persistence

| Area | Exists and verified | Open |
| --- | --- | --- |
| [Physical checkpoints](../native/MAC_CHECKPOINT.md) and paired colony saves | Lossless physical images predict the next worker frame byte for byte. Paired native/physical capture at one worker barrier through the ordinary `SaveLoader.Save` entry; fresh-process load restores the exact image, autosaves paused at frame zero and matches 120 frames. Linux Unity, Windows and WindowsPlayer/Proton pass | Loading outside the development restore mode; load menu; overwrite save |
| [Live actors](native-domain-persistence.md#durable-point-actors), [in-flight CO2](native-domain-persistence.md#in-flight-co2-particles), [accepted inputs](native-domain-transactions.md#accepted-frame-inputs-and-completed-images) | Saved consumers/emitters keep identity, timing and blocked state; in-flight particles and per-frame input journals make continuation exact. Linux Unity and WindowsPlayer/Proton pass, including cross-player loads | Actor saves on Windows OS; durable pending outbox (saves with pending commands are declined) |
| [Natural day autosave](native-domain-persistence.md#natural-day-autosaves), [menu reload](native-domain-persistence.md#same-process-main-menu-reload), [reader rebinding](native-domain-readers.md#restored-reader-checks) | Scheduled autosaves and same-process return through the main menu resume exactly. Linux Unity, Windows (menu reload) and WindowsPlayer/Proton pass | Asteroid switching with a native domain |
| [Spaced Out! load](native-domain-persistence.md#spaced-out-saves) and [orbital references](native-domain-persistence.md#orbital-reference-admission) | Nine-world source saves and reloads on WindowsPlayer/Proton and native Linux; missing orbital objects reject before advancing | General managed-reference integrity, missing-object recovery |
| Cross-platform continuation | Gas checkpoints agree within 6.74e-12 relative and gas/liquid coexistence within 1.92e-9 | **Water fails** the 1e-8 bound ([arithmetic scope](native-platform-numerics.md#solver-arithmetic-scope)) |
| [Save failures](sealed-native-domain.md#save-failure-recovery-and-native-metadata) | Declined and denied-write saves release resources and restore selection through ONI's error UI (Linux Unity). Windows Save As creation and reload pass | Windows coverage of the failure UI; interrupted-save durability; rejection of mixture saves loaded without the mod |

## Rendering and presentation

| Area | Exists and verified | Open |
| --- | --- | --- |
| Status panel, overlays and shortcuts ([rendering](native-fluid-rendering.md)) | Panel, outline, four overlays and Shift shortcuts in Linux Unity, Windows and WindowsPlayer/Proton | Outline can cross a hover card; panel samples the hovered cell, not the selection |
| [Native fluid rendering](native-fluid-rendering.md), [GPU readback](native-domain-readers.md#gpu-texture-readback) | ONI's shaders draw owned cells from completed presentation; calibrated GPU byte checks bind colour, opacity and hazard to the physical frame. Linux and Windows scenes pass | Other liquids, free surfaces, sustained performance |
| [Mixture presentation](mixture-presentation.md), [containers](container-inventories.md) | Read-only model: mole-weighted colours, trace inspection, breathing assessment, owned container bindings. Fixture gates pass in Linux Unity (2026-10-16) and Windows ([2026-10-17](windows-validation.md#windows-game-checks-2026-10-17)) | Binding to production colony state and containers |
| [Complete tile hover](mixture-presentation.md#tile-hover-acceptance-procedure) | Hover lists every non-trace gas with its mole percentage and keeps hazardous-trace warnings, including the exact cutoff and 125% UI scale; passes as display fixtures on both OSes and bound to completed native frames on Windows | Production four-gas hover in an owned colony |
| [Nitrogen](nitrogen.md) | Namespaced gas/liquid/solid elements registered from mod YAML; item inspection passes in Windows and Linux Unity fixtures, and native save/resume on Windows | Pressure-dependent material model, world generation, starting reserves |

## Solver development (milestone B)

The [production kernel](../native/MAC_KERNEL.md) implements the in-game
physical selectors with interval audits and deterministic workers. Its geometric
[multigrid](../native/MAC_MULTIGRID.md) uses the production pool, with exact
geometry/pressure-active mask keys and fresh coefficients. Selection 2 persists
in checkpoint version 6, including ordinary restore/advance, saved
preconditioning and diffusion. Execution options contain no numerical override.
The old interval solver, owner cache and admission bridge are removed.
Selector-1 owners and version-4/5 selector-1 images explicitly reject as retired;
supported reference images retain their bytes and continuation.

The seven original saved-review scenes now run through production. The reader
retains the strict endpoint inventory, impulse, U+K and entropy bounds, plus
checks of production stage magnitudes and below-range operand counts: 105,985
checks and seven rejected corruptions on Linux and MinGW/Wine. They include
10,000 closed steps, 81 species/82 passives and periodic/disconnected multigrid.
The review exposed wall-reaction cancellation; pressure components now gather
before the common timestep/area product, preserving the small net reaction.
Material-temperature/caloric uncertainty refines through the strict reference,
while exact static states can keep their certificate. Changed geometry/schema,
heat/mass edits, source lifetime, concurrent owners and atomic range fallback
are covered. The strict reference remains the validation oracle.

Production [diffusion](../native/MAC_DIFFUSION.md#counted-transfer-rounding)
now rounds a below-range face transfer once before either endpoint gathers it.
Accepted projection counts and full-range error magnitudes are checked against
half a storage quantum each. Shared antisymmetric transfers preserve the original
128-epsilon inventory/energy bounds. Tiny ternary candidates retry instead of
repairing a cell's total mass; normal-range refinement preserves the consumed
attempt budget. The exact-fraction saved reader and default row/continuation
controls pass. All four direct/composed 8,192-cell physical/100× cases complete 400 frames,
with the original aggregate helium bound and zero measured bulk temperature,
pressure, oxygen-partial-pressure and major-constituent changes. Their saved
reader passes 131,154 checks/three corruptions; default quantum/row controls
pass 2,690 checks/six corruptions on all instrumented/Windows-target routes.
Post-rounding front saves continue exactly in fresh Linux/Wine processes.
Primal advection now counts accepted below-range parcel products and audits
their exact errors against one half-quantum each, without changing parcel values
or endpoint bounds. The saved exact-fraction reader passes 50,825 checks and
rejects seven altered captures. An 8,192-cell wave carrying helium and a passive
trace completes 160 intervals: 44,420 rounded products, helium in 7,028 cells,
and the original aggregate inventory bound. A 33,389-cell control retains exact
one/twelve-worker state and ledger results, ordinary checkpoint continuation
and budget rollback. Pressure momentum additions now have counted half-ulp
charges inside the original interval bound. Midpoint and both moment methods
preserve one/twelve-worker fields, accepted ledgers, rollback and checkpoint
continuation; six endpoint/continuation images and refinement counters match
`fc4dab0`. Their saved reader passes 10,974 checks/eighteen corruptions on
Release, ASan/UBSan, ThreadSanitizer and MinGW/Wine. Pressure
products and other transient momentum/energy rounding ledgers remain open. Flagged sweeps now retain their range through parcel formation, and dual
J/K products use canonical momentum when a derived velocity or kinetic factor
underflows. The former large-inertia fault now advances in one attempt and
matches the strict reference, including a representable tracer arrival.
Signed x/y, zero/one/two-quantum arrivals, twelve-worker/cache/continuation and
shared-budget refinement controls pass; their saved exact-fraction reader
passes 1,318 checks and rejects ten altered captures on Release, ASan/UBSan,
ThreadSanitizer and MinGW/Wine. A cross-block tracer uses the same shared transfer with one/twelve workers.
Normal sweeps divided by very large material volumes now retain their full
share until parcel formation. The range controls preserve the reference
arrival and zero/one/two-quantum transfers. Accepted primal writes blocked by
storage resolution are counted for species, passives and U (48/49 in six cells;
267,113 in the 33,389-cell controls), without changing the conservation bound.
Step f now selects production for new native owners and every live frame,
with twelve requested workers. Accepted physical heat/work diagnostics are
published without rejected-attempt leakage. Supported reference saves retain
idle bytes; the first live frame migrates to version 6 and journal flag 4
records the request. Historical unflagged replay and stateless continuation
retain the saved kernel. Native transition and publication controls pass;
production Linux/Proton game scenes, saves and fresh-process reloads remain
open.

Six historical version-2/3/4/5 images retain every byte of the earlier writer.
The default reference wave checkpoint SHA and sampled version-6 production
checkpoints remain unchanged. All 55 Release CTests pass. Production-kernel,
native-owner and protocol controls pass Debug ASan/UBSan, ThreadSanitizer and
MinGW/Wine; the moment suites use targeted threaded checkpoint modes under
ThreadSanitizer. Post-rebase interface/reconstruction, kernel and owner controls
also pass those instrumented and Windows-target routes.
Against `025a804`, all 68 MAC component executables retain their physical
records and exits: 66 outputs are byte-identical and two differ only in timing.
Fresh-process gas/multigrid and subnormal-tracer continuation agrees exactly between
Linux and Wine in both source directions. The latest
production-owner Ubuntu 22.04 snapshot passes 54 CTests, but sharp-interface
`std::hypot` imports `GLIBC_2.35` in the observer and fails the project 2.34
ceiling. The builder and supplied-provenance package check now enforce 2.34;
the packaging helper tests also run independently.
Its game package is blocked; production game validation remains open.
The preceding 53-test snapshot requires at most 2.34.

The last measured automatic-multigrid baseline fails the real-time targets: two pinned
21-interval evolving-wave runs measure p95 819–865 ms at 100,000 active
cells and 2,210–2,329 ms at 250,000, before adapters, on the shared Ryzen
9 5900X. Every cell changes. Retained workspace is about 1.6 KiB/cell; total
additional memory and colony FPS remain unmeasured. See the
[measured baseline](../native/MAC_KERNEL.md#current-active-cell-baseline).
The pressure-storage ledger's timing impact has not yet been measured.
The opt-in [two-moment pressure method](../native/MAC_PRESSURE_MOMENTS.md)
passes selected larger-step wave schedules against resolved midpoint references:
16×16 at .004 s and 256×2/512×2 at .1 s retain the original 0.1 K/1% bounds.
Its original real Schur selection persists in version 7; the coupled complex
multigrid implementation persists separately in version 8. Older images retain
their method, bytes and continuation. One/twelve-worker state agrees. The coupled
endpoint reader passes 114,726 checks/fourteen altered captures on Release,
ASan/UBSan and MinGW/Wine, including a new 500×2 wave at .1 s. The threaded
controls also pass ThreadSanitizer. Version-8 images continue exactly in fresh
Linux/Wine processes in both source directions. Composed physical/accelerated
diffusion and a spreading subnormal front pass. The original strong-flow inputs
still fail collision density refinement with the coupled method (0.861766
against 0.85); the unchanged defaults retain their original failure.
The [shared-face time primitive](../native/MAC_TIME_MORTAR.md) now supplies
dyadic local time coordinates and one shared momentum/volume path for both
cells, including flagged range products. Its saved review passes 1,057 checks
and fourteen altered captures on Release, ASan/UBSan, ThreadSanitizer and Wine.
A separate [pressure-window component](../native/MAC_PRESSURE_WINDOWS.md)
streams fine-cell pressure roots through a coarse interval with shared face
fluxes and actual primary U updates. Prescribed 1:4/1:8 synthetic gas controls
pass resolved references and the original energy bound; the mixed 1:8 case
uses 88 physical cell intervals instead of 256 uniform-fine intervals.
Its independent endpoint reader passes 3,307 checks/nineteen altered captures.
Wall/body/empty-cell accounts, rejection, cache and twelve-worker controls pass.
Its cached macro matrix now replaces repeated internal coarse-face integration,
and its macro preconditioner includes the occupied fine region. The 250,000-cell
mixed pressure-only diagnostic needs 16 outer iterations instead of 60 and
measures 2,589–2,946 ms per .1 s (host load 26.83–28.90). This resets its source
between samples and is not evolving owner acceptance.
It is pressure-only, with normal-range coefficient admission and no saved
selection. Its interval audits and owner binding remain unfinished. Automatic
calm selection, full local transport/remap, open physics gates and in-game
production-kernel validation remain open.

`MacClosedWorld` composes EOS pressure work, MAC face-momentum transport,
shared material reconstruction, compression/shock heat, conduction, viscosity
and gravity, with atomic intervals and bounded retries. Its components and
defaults are documented in [the closed owner](../native/MAC_SOLVER.md)
and the linked `native/MAC_*.md` notes.

[Prepared remap admission](../native/MAC_TRANSPORT.md#prepared-admission-and-reusable-candidates)
reuses immutable material transport and candidate buffers without changing the
physical result. The full opt-in strong-flow sequence now completes within its
180-second bound on Linux; its separation accuracy still fails. Measurements and
verification are in [timestep progress](production-timestep-progress.md#prepared-remap-admission).

| Gate | Status |
| --- | --- |
| [Strong-flow Riemann gate](../native/MAC_STRONG_FLOW.md) | Sod and collision pass with [compressive work](../native/MAC_PRESSURE.md#coupled-compressive-pressure-work-mode-3). **Separation fails** the finest-grid velocity bound and the central density/pressure bounds |
| [Curved-nodal diagnostic](../native/MAC_STRONG_FLOW.md#curved-deformation-pressure-and-remap-convention) | Localizes a pressure/remap source-velocity convention mismatch. Opt-in selector 5 passes the interior 1-D oracle with original J/K; general 2-D conversion and strong-flow acceptance remain open |
| [Gas/vacuum front](../native/GAS_VACUUM.md) | **Fails**. Standalone MAC [empty-cell support](../native/MAC_VACUUM.md) admits conservative gas arrival and complete zero-inventory checkpoints; its front remains wrong without occupied-volume tracking. The in-game domain still rejects empty cells |
| Shocks, free surfaces, phase interfaces, phase transitions | Not implemented in the owner |

Separate references, excluded from game dispatch: the gas-only `GasWorld`
reference (still behind the public C ABI and `ma_demo`), the
[`FluidWorld` prototype](../native/FLUID_WORLD.md), the
[coupled ABI and colony coordinator](../native/COUPLED_ABI.md) (no game frame
hook), and the [retained material-node](../native/OCCUPIED_STAGGERED.md),
[canonical material](../native/CANONICAL_MATERIAL.md) and
[staggered remap experiments](../native/STAGGERED_EXPERIMENTS.md). The
standalone thermodynamic core provides mixture state, breathing assessment,
reactions, closed-vessel phase helpers and pump/separator process models
([architecture](architecture.md)).

## Managed layer and experiments

| Area | Exists and verified | Open |
| --- | --- | --- |
| Core API and colony state | Accountable injection/extraction, selective transfers, heat, geometry displacement, owned containers with full allocation handles, deterministic checkpoints; standalone on Windows and Linux | Routing of all gameplay actions |
| [Frame commands](fluid-frame-commands.md) and owned snapshots | Unhooked ordered command staging into one transaction with paired publication; native snapshot copies byte-identical in a Windows colony | Production message activation |
| Import staging and element filter | `NativeColonyImport.StageNewColony` and `NativeElementTable.SuppressFluidProduction` ([coverage](ownership-coverage.md)) | Live loader hook; this is historical vacuum-shadow work |
| [Owned reservoir bridge](owned-reservoir-bridge.md) | Explicit native-empty `GasReservoir` ownership with conserved transfers, meter/logic queries, mutation fences and saved-file inspection helpers; Windows Unity | Pipe flow, heat/power, integrated persistence; not connected to the native owner |

## Performance

No active-colony performance gate passes. The release target is **25 ms per
0.1 simulated seconds for 100,000 active cells on a Ryzen 5900X**, with sustained
1× play, bounded memory and no growing publication backlog. Current
`MacClosedWorld` measurements, including the 100,000-cell whole-owner
equilibrium shortcut and the 16×16 pressure-wave cost, are in
[playtesting readiness](playtesting-readiness.md#reproduce-measurements). The
`GasWorld` reference takes about 425 ms for 0.001 s on 100,000 cells and the
[`FluidWorld` prototype](../native/FLUID_WORLD.md) still fails its 25 ms
performance gate; neither is a production solver. Presentation capture costs are in [mixture presentation](mixture-presentation.md).

## Remaining release gates

In dependency order (see the [completion plan](completion-plan.md) for exit
criteria):

1. **Authoritative integration.** Audit and redirect every native mutation,
   snapshot, callback, save and conduit service; prove one owner for every
   simulated quantity in a disposable colony; resolve real generated liquid
   imports without deleting mass or inventing headspace.
2. **Production fluid solver.** Accurate strong flow, vacuum, free surfaces,
   miscible/immiscible liquids, displacement and grid-coupled phase transitions
   in `MacClosedWorld`, preserving mass, momentum, energy and traces.
3. **Thermodynamic catalogue.** Every supported material, with valid-range
   properties and audited machine/biological reactions.
4. **All fluid users.** Pumps, filters, vents, pipes, reservoirs, suits, rockets,
   generators, plants, critters, geysers and fluid-related disease, radiation
   and heat; no silent vanilla conversion.
5. **Playable colony.** Nitrogen and starting supplies, life support, research,
   presentation bound to authoritative state, Windows/Linux screenshot and
   performance acceptance, and the ten-cycle colony gate.
6. **Persistence and compatibility.** All mixture state; rejection of
   unsupported versions and mods; restart, autosave and multi-asteroid transfer.
7. **Release validation.** Long colonies for every supported pack on both OSes,
   measuring CPU, allocations, memory, conservation and stability.

There is no release-packaging option. `tools/package_probe.py` produces only an
integration probe and writes `release_ready: false` in its manifest. The
[Steam Community draft](steam-community-draft.bbcode) describes the intended
project; it is not a release announcement.
