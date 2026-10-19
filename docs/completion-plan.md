# Finish Mixed Atmospheres as a feature-complete ONI overhaul

Accepted execution plan, recorded 2026-10-14. Work proceeds in dependency order;
an implemented helper or standalone test does not satisfy an in-game exit gate.
Each milestone is a separate reviewable change. Regular pulls, commits and
pushes to `main` are authorized by the user. Public release and Workshop upload
still require separate authorization.

Amendments:

- **Architecture, 2026-10-16:** implement the full overhaul through
  [native SimDLL hooks](native-backend-integration.md), retaining verified native
  nonfluid services. Observer and takeover work are the next engineering steps;
  they do not replace the remaining milestones.
- **Execution, 2026-10-16:** implement and verify the full feature set in real
  games before optimizing it. Slow simulation is acceptable for this stage.
  Retain conservation, thermodynamics and numerical acceptance requirements;
  runtime/performance targets must not prevent functional integration. The
  small Linux colony remains the first integration step, followed by the full
  Windows/Linux and DLC scope below.
- **Playtest, 2026-10-17:** investigate conservative performance changes and
  explicit fidelity tradeoffs now, alongside functional integration. Preserve
  the original reference/release criteria, constituent and energy accounts,
  breathing assessment and truthful presentation. [Playtesting readiness](playtesting-readiness.md)
  distinguishes implemented shortcuts, benchmark-only approximations and
  remaining colony blockers.
- **Solver, 2026-10-18 (user decision):** `MacClosedWorld` (`native/src/mac_*.cpp`)
  is the production colony fluid solver. ONI's native SimDLL fluid simulation is
  what the mod replaces and must not be used as the solver.
- **Real-time completion, 2026-10-18 (user decision):** complete the physical
  timestep and optimize the existing model before broad gameplay activation.
  The acceptance colony has 250,000 active fluid cells across all asteroids,
  32 duplicants and sustained 1× speed on the Ryzen 5900X. Brief disturbance
  slowdowns are acceptable. Functional and real-time gates must pass together;
  this supersedes the earlier permission to accept slow first-playable colonies.
  Keep the reference defaults; the repeatedly rejected benchmark `balanced`
  configuration must not become the production default. The first implementation
  slice and its remaining blockers are recorded in
  [production timestep progress](production-timestep-progress.md).
- **Solver contracts, 2026-10-18 (user decision):** relax the per-stage
  numerical contracts to interval-level conservation audits, so the substep
  kernel can be replaced for performance
  ([scaling analysis](solver-scaling-analysis.md)). The production contract is
  in [MAC_SOLVER.md](../native/MAC_SOLVER.md#numerical-contracts). The current
  strict implementation stays buildable and tested as the validation reference.
  Physical model, selectors, conservation of every constituent and energy,
  determinism, checkpoints and the accuracy tolerances below are unchanged.
- **Calm-region steps, 2026-10-18 (user decision):** accept larger
  calm-region steps with local subcycling, one of milestone B's ordered
  approximations, for the production kernel. Fluxes stay shared between
  differently stepped regions, so conservation holds by construction. Accuracy
  is still judged against resolved references with the tolerances below.
- **Kernel consolidation, 2026-10-19 (user decision):** consolidate on the
  production kernel (`MacExecutionPolicy::production`, `native/MAC_KERNEL.md`).
  Port the interval kernel's multigrid preconditioner and saved-data reviewers
  into it, persist the kernel selection in checkpoints so saves continue
  exactly, then retire the interval kernel. The reference implementation stays
  as the validation oracle.
- **Below-range transfers, 2026-10-19 (user decision):** a positive transfer
  too small to store in binary64 may round, with every such rounding counted
  in the interval audit, instead of rejecting the step. This removes the
  trace-front faults of diffusion and transport in large rooms.
- **Gravity reconciliation entropy, 2026-10-19 (user decision):** authorize
  the reversible gravity reconciliation credit implemented in `3fbccfb`.
  Subtract its concavity bound `Σ(dU/T_pre)` from the interval entropy change
  and include its `128 ε Σ(|dU|/T_pre)` rounding allowance. The remaining
  entropy change must pass the original gate; conservation and all other
  contracts remain unchanged. The precise contract is in
  [MAC_SOLVER.md](../native/MAC_SOLVER.md#numerical-contracts).
- **Playtest scope, 2026-10-19 (user decision):** the first ten-cycle playtest
  covers the base game and Spaced Out!. The gameplay packs follow later.
- **Workflow, 2026-10-19 (user decision):** one Codex session works milestone
  by milestone in the execution order below and stops at each milestone exit
  with an evidence report for review. Windows evidence comes from
  WindowsPlayer under Proton for now; actual Windows OS runs remain open.
- **Physics-gate ownership, 2026-10-19 (user decision):** Claude works
  milestone B's physics gates (strong flow, vacuum front, U/volume remainders,
  free surfaces, multiple liquids, phase change, and defining the gravity and
  multiphase gates) and pushes to `main` from its own clone. Codex continues
  calm-region stepping, performance and in-game production adoption.
- **Separation core pressure, 2026-10-19 (user decision):** the strong-flow
  separation case's central pressure criterion is re-baselined against a
  classical method. Textbook Godunov and MUSCL-Hancock schemes with exact
  Riemann fluxes miss the original "within half the exact cell average" bound
  by about 5–26× at N256, so central pressure may now be no worse than a fully
  specified binary64 MUSCL-Hancock reference on the same grid
  ([criteria](../native/MAC_STRONG_FLOW.md#acceptance-criteria)). Every other
  strong-flow criterion, input and bound is unchanged, including central
  density within half its exact cell average.
- **Gravity and multiphase gates, 2026-10-19 (user decision):** the eight
  [gates below](#gravity-and-multiphase-gates) define milestone B's gravity
  and multiphase exit. Gates whose physics is not built yet (two liquids,
  phase change, vacuum) are recorded as open, and the exit stays open until
  they pass. Claude builds vacuum, multiple immiscible liquids, phase change
  and the strong-flow fixes. The gravity reconciliation entropy credit
  ([kernel](../native/MAC_KERNEL.md#gravity-reconciliation-entropy)) stays.
  The dam break compares with the data under the standard 0.175 time-origin
  shift.
- **Strong-flow separation reference, 2026-10-19 (user decision):** separation
  keeps its staggered start, whose centre face starts at rest and so holds a
  one-cell slab at rest. It is judged against a fully specified fine-grid
  MUSCL-Hancock reference of that same start, not against the textbook
  near-vacuum core. A 16× finer solve of the slab start itself misses the
  original velocity and central bounds, and the central-density bound is
  ill-conditioned in a ~3e-4 kg/m³ core
  ([criteria](../native/MAC_STRONG_FLOW.md#acceptance-criteria)).

The current execution order is: production physics and active-cell scaling;
materials, generation and whole-world ownership; transactional life support;
remaining gameplay adapters; progression and presentation; durable colony saves
and Windows/Linux distribution. The milestone labels below retain their earlier
cross-references and feature scope.

## Target

The existing code is a tested foundation, not a nearly finished playable mod.
The first public release targets Windows and Linux, new colonies in the base
game and Spaced Out!, and all official gameplay packs (Frosty, Bionic,
Prehistoric, Aquatic), with cosmetic compatibility. It includes bulk pressure,
buoyancy, viscosity, mixing, immiscible separation, heat transfer, phase changes,
dissolved O2/CO2 and aquatic gas exchange, configurable breathing, nitrogen,
suitable starting resources, and complete progression through endgame.

Microscopic droplets, capillary effects, detailed solution chemistry,
environmental fires and explosions are excluded. Fictional materials require
documented, conservation-consistent approximations.

## Source control

All work is reconciled on the single `main` branch (2026-10-18); there are no
parallel development branches to track. The repository excludes private game
contracts, saves, inspection artifacts, builds, licensed binaries, decompiled
implementations, credentials and generated packages. Original sources, tools,
tests, documentation and CI are committed; milestones use separate commits.

## A. Integrate natively, complete coverage and prove simulation ownership

Create a machine-readable inventory of every material, fluid producer,
consumer, conversion, storage system and relevant callback across all DLC
configurations. Each entry identifies its owner, adapter and acceptance test.

The custom engine owns fluids; ONI retains supported nonfluid services.

- Stage and validate all new-colony world blobs before changing any of them.
- Establish exact-build Windows/Linux profiles; prove forwarding hooks, native
  phase order and worker barriers in isolated runs.
- Import authoritative fluid state and retain scalar compatibility projections;
  the earlier vacuum-shadow importer remains a historical control.
- Replace native fluid readers/writers and route pressure/liquid flow,
  transitions, sublimation, emitters, melting buildings, chunks and backwalls.
- Route fluid mutations and callbacks through the custom engine.
- Publish owned, coherent snapshots for rendering, navigation, pressure queries
  and game logic.
- Advance once per game frame regardless of asteroid-region count.
- Stage custom changes before native operations; publish only completed frames.
- Retry custom candidates before irreversible native work; fault and reload
  after ambiguous native effects rather than claiming unsupported rollback.

Exit: a disposable colony has visible custom mixtures, working native
solids/buildings, correct callbacks and no native fluid ownership on both OSes.
Trace execution and audit readers/writers; projected fluid identities are
allowed and zero-fluid scans alone are insufficient. Every unresolved native
fluid source blocks this milestone.

Status: the [coverage inventory](ownership-coverage.md) exists (3,074 entries,
all unimplemented). Exact-build hooks and a bounded
[sealed native domain](sealed-native-domain.md) run in real Linux and Windows
games; whole-colony ownership is open. See [implementation status](implementation-status.md).

## B. Complete production timestep

Replace the runtime gas-only reference world with a coupled multiphase world.
Keep isolated components as validation references, without mistaking their tests
for evidence of a complete solver.

- Staggered finite-volume transport, implicit pressure/energy coupling and
  conservative momentum transport.
- Preserve every constituent, including traces, through all transfers.
- Explicit liquid volume fractions and interfaces; miscible composition;
  separate immiscible phase identities and interface geometry.
- Gravity, viscosity, diffusion, conduction, vacuum expansion, displacement and
  pressure-driven flow.
- Phase transitions coupled to occupied volume and latent heat.
- Reject invalid timesteps and retry smaller steps without partial changes.

Reference: <https://arxiv.org/abs/1612.08866>. All-Mach pressure methods do not
provide multiphase behavior without additional implementation and validation.

Exit: strong-flow, vacuum, gravity and multiphase numerical gates pass before
broad gameplay integration, with measured active-cell scaling and a credible
path to the real-time budget. Optimize arithmetic, layout, repeated EOS work,
workspaces, diagnostics and parallel independent work before reducing fidelity.
Use pressure-implicit compressible stepping. If approximation remains necessary,
evaluate less frequent conservative slow processes, locally simpler conservative
reconstruction, larger calm-region steps with local subcycling, and only then
calm-region low-Mach treatment. Preserve shared fluxes, tile inventories and
liquid geometry; persist physical settings and continuation history. Never adapt
physics to momentary wall-clock load.

### Gravity and multiphase gates

Each gate is a standalone executable that prints its verdict. Passing gates
also run as CTest regressions; open gates report `OPEN` until their physics
exists. Liquids are water and air at 101,325 Pa and 300 K unless stated, with
g = 9.81 m/s², the production kernel and the default settings; M1–M3 select
sharp interfaces (material selector 3). Conservation means each species and
passive exact to the interval audit, and liquid volume exact. Commands,
measurements and diagnoses are in [MAC_GATES.md](../native/MAC_GATES.md).
Status: G1, G2 and M1–M3 pass; V fails; M4 and M5 are open.

| Gate | Setup | Pass criteria |
| --- | --- | --- |
| G1 hydrostatic gas column | N2 at 300 K, 100 m column in a closed box; 64, 128 and 256 cells; starts from the solver's own discrete hydrostatic state | Over 1,000 intervals of 0.1 s: max \|u\| ≤ 1e-9 m/s, pressure drift ≤ 1e-12 relative. Profile within 1e-6 of the analytic exponential at 64 cells, second-order convergence (successive error ratios ≤ 0.3) |
| G2 gravitational species separation | CO2 and N2 50/50 by mole, 300 K, 1 m column, 64 cells, accelerated diffusion (multiplier 1000) | After at least 10 diffusion times, each partial pressure follows p0·exp(−M g z / RT) within 1% of its exponent (over 1 m the law itself changes pressures by under 2e-4); species exact |
| M1 still water | 1 × 1 m tank, 32 × 32 cells, water 0.5 m deep under air | After 10 s: max \|u\| ≤ 1e-8 m/s, surface drift ≤ 1e-9 m, no liquid more than one cell above the surface; conservation |
| M2 dam break | Martin & Moyce column, width a = 0.05715 m, height 2a, in a 5a-long tank under air; 16, 32 and 64 cells across a; to T = t√(2g/a) = 3 | Finest grid: front Z = x/a within 10% of the published data, compared at simulated T = T_data − 0.175 (≥ 0.5); two finest grids within 3% of each other; interface band (cells 1–99% full, measured across the interface) at most 3 cells wide; liquid volume exact |
| M3 standing wave | Water 0.5 m deep, wavelength 1 m, amplitude 1 mm, periodic; 32, 64 and 128 cells per wavelength | Finest grid: period within 1% of ω² = g k tanh(k h), converging; amplitude change within ±2% per period (loss at most 2%, no growth beyond 2%) |
| M4 two liquids (Rayleigh–Taylor) | Immiscible 1000 and 800 kg/m³ liquids, no surface tension, width 0.1 m, perturbation 1e-4 of the width; 64 cells per wavelength | Growth rate within 10% of √(A g k) while the amplitude is below 5% of the width. Stable (light over heavy) case at rest within 1e-8 m/s. Open: needs immiscible liquids |
| M5 phase change (Stefan vapour film) | Water and steam at 1 atm, wall at T_sat + 10 K, started from the exact solution at t0 | Interface x(t) = 2λ√(α t) within 2% over [t0, 10 t0] on the finest grid, converging; mass and energy including latent heat exact. Open: needs phase change |
| V vacuum front (absolute bound added) | Existing [gas/vacuum gate](../native/GAS_VACUUM.md) inputs | Front within 5% of the exact travel (2.51 m) and recent speed within 10% of 5.019 c0 at N512, both errors decreasing over N128, N256 and N512. Open: needs a void phase |

M2 data are Martin & Moyce (1952), *Phil. Trans. R. Soc. A* 244, 312–324,
for the 2a-tall column (T, Z): (0.41, 1.11), (0.84, 1.23), (1.19, 1.44),
(1.43, 1.67), (1.63, 1.89), (1.82, 2.11), (1.97, 2.33), (2.20, 2.56),
(2.32, 2.78), (2.50, 3.00), (2.64, 3.22), (2.82, 3.44), (2.96, 3.67), as
digitised in Lethe's dam-break validation. The experiment's front lags its
gate release, so it is compared with the simulation 0.175 earlier in T, the
time-origin shift of Lethe's validation (user decision, 2026-10-19).

Additional error against resolved references must stay within 0.1 K and 1% for
pressure, oxygen partial pressure and transported constituents. Pressure-wave
error uses disturbance amplitude. Refine when uncertainty could alter a
breathing, contamination, phase or damage decision. Select the least approximate
configuration that passes performance acceptance.

Status: `MacClosedWorld` is the production solver and already runs in game in
the bounded domain. Its prerequisite native groups and saved reviews pass on
Windows, Linux and Linux ASan/UBSan. With
[compressive pressure work](../native/MAC_PRESSURE.md#coupled-compressive-pressure-work-mode-3), Sod and
collision refinement pass, but the original
[strong-flow separation case](../native/MAC_STRONG_FLOW.md) still fails the
finest-grid velocity bound and the central density/pressure bounds. The
[curved-nodal diagnostic](../native/MAC_STRONG_FLOW.md#curved-deformation-pressure-and-remap-convention) localizes a
pressure/remap source-velocity convention mismatch, reproduced on Windows and
Linux. A [conservative one-dimensional conversion](../native/MAC_NODAL_CONVERSION.md)
now passes the interior curved-point oracle; general 2-D conversion and strong-flow
accuracy remain open. The separate [gas/vacuum front gate](../native/GAS_VACUUM.md)
also fails, and the owner rejects empty cells. Optional modes that did not pass
remain opt-in experiments; original inputs and acceptance bounds are unchanged.
The exit above is open.

## C. Materials, thermodynamics and aquatic chemistry

Versioned definitions cover every supported material and phase, including
density, heat capacity, viscosity, conductivity, phase behavior, chemical
energy and documented validity ranges. Fictional substances have explicit
compositions or surrogates. Audit conversions for material balance, required
work and heat; account separately for external and nuclear sources.

Implement dissolved O2/CO2 transport, temperature-dependent solubility,
surface exchange and accountable outgassing. Aquatic respiration and
photosynthesis consume and produce actual inventory.

Exit: no supported material or conversion silently uses an unaccounted vanilla
transformation.

## D. First playable development colony

Connect the solver to an actual small colony with mixed air, water, duplicants
and basic machines. Support breathing, bulk pumping, selective separation,
ventilation, CO2 removal, fluid-solid heat exchange, construction displacement,
and save/reload.

The first playable gate also requires the shared mixture presentation described
in [mixture presentation](mixture-presentation.md):

- Normal world colours and a subtle, continuous 4-by-4-cell composition pattern;
  colour-only mode and persisted contrast controls, defaulting to 0.04.
- Cell hover, selected-cell details and owned pipe/reservoir/storage/bottle/suit/
  vent inventories showing the same completed revision. Show gas mole
  percentages, temperature, gas density and total gas pressure; expanded details
  show every constituent's mass and partial pressure, with liquid rows identified
  separately.
- Hovering a tile must list **every non-trace gas in that tile**, each with its
  gas mole percentage, directly in the tooltip without selecting the tile or
  opening details. No top-N cap or short mixture name may hide a non-trace gas.
  Use the shared trace classification (default: below 1% of gas moles); retain
  the original percentages without renormalizing after traces are omitted.
  Hazardous trace warnings remain mandatory. Expanded details retain all traces.
  Verify actual Windows and Linux mouse hover on a tile with at least four
  non-trace gases plus benign and hazardous traces, including a gas at the trace
  threshold, and compare the full tooltip against the same completed snapshot.
- Composition, gas-pressure, oxygen-partial-pressure and breathability overlays
  with legends. Explain every configured breathing failure and retain harmful
  traces even when their visual contribution is imperceptible. Native packet
  fallbacks must remain labelled and must never be inferred to be mixtures.
- Actual Windows **and** Linux Unity screenshots for normal rendering, hover,
  full details, each overlay, owned/native container distinctions and the
  colour-only/contrast controls. Exercise ordinary air, thin air, excessive O2,
  CO2 fraction/pressure failures, trace contaminants and gas/liquid coexistence.
  Record source revision, build, settings and fixture/owned-state status beside
  the images; synthetic images and IL checks do not satisfy this gate.
- Presentation performance evidence on both operating systems for 100,000 gas
  cells: initial capture, mostly unchanged and entirely changed revisions,
  repeated sampling/colour allocation counts, and actual game frame times with
  overlays on/off. Repeated worker sampling and colour lookup must allocate
  zero after publication. At sustained 1x there must be no growing publication
  backlog or retained history; cold and entirely changed frames must fit the
  measured publication budget. Report p50/p95 game costs and peak memory.
  This supplements the separate production-solver performance gate.

Exit: duplicants survive and operate for ten cycles with conservation
diagnostics, and the presentation, screenshot and performance criteria above
pass on Windows and Linux. This does not authorize public release.

Status: the four-gas tile-hover check passes as a display fixture (native ONI
physics) on Windows and Linux
([procedure](mixture-presentation.md#tile-hover-acceptance-procedure)),
including the exact trace cutoff, hazardous-trace warning and 125% UI scale.
Windows also binds hover,
selected details and GPU rendering to completed native physical frames in the
bounded domain ([rendering](native-fluid-rendering.md)). Hover against
authoritative production colony state, the 100,000-cell presentation
performance evidence and the ten-cycle colony remain open; see
[playtesting readiness](playtesting-readiness.md).

## E. Every fluid-related gameplay system

Shared transactions reserve inputs, output capacity, power and heat transfers
before consumption. Preserve familiar building purposes with corrected mass
and energy accounting. Blocked output consumes neither input nor work.

- Pumps, filters, fans, vents, valves, bridges, reservoirs, pipes and sensors.
- Generators, electrolysis, refineries, cooling/heating and all conversions.
- Metabolism, excretion, suits, masks, bionic oxygen storage and intake.
- Plants, critters, aquatic ecosystems and all DLC biological conversions.
- Geysers, offgassing, bottles, debris, bubbles, falling liquids, rockets and
  inter-asteroid transport.
- Fluid-related disease, radiation, light absorption, flooding, swimming and
  pressure damage.

Exit: every inventory entry has an implemented adapter and passing in-game test.

## F. Player-facing systems and progression

Register nitrogen and its phases. Generate breathable starting air and finite
nitrogen reserves. Calculate a ten-cycle life-support bridge for the selected
population using new consumption rates. Provide early ventilation, scrubbing
and pressure regulation, mixture overlays, constituent grams, partial
pressures, liquid fractions, dissolved-gas readings and breathing failure
explanations. Add sensors, research, recipes, descriptions and configuration
UI. Use actual liquid geometry for swimming, submersion and atmospheric access.

Defaults: hard mode 15–35% O2; easy mode at least 15%; minimum O2 partial
pressure 15 kPa; CO2 limits 0.5% and 500 Pa.

Exit: representative starts from every pack reach sustainable life support and
endgame without developer intervention.

## G. Persistence and compatibility

Save inventories, geometry, momentum, dissolved gases, pipes, storage, pending
transfers, settings and material-model versions. Store native/custom state in
one atomic versioned container at the same completed frame. Integrate manual
saves, autosaves, resume, previews and cloud/local handling. A mixture save
must not silently load as vanilla without the mod. Validate all worlds before
publishing. Clearly reject unsupported builds, simulation mods and save
versions. Preserve vanilla colonies; do not offer automatic conversion.

Exit: reload, restart, cross-platform transfer and interrupted saves preserve
the complete colony.

## Shared interfaces

Provide a versioned native ABI and managed wrappers for:

| Interface | Responsibility |
| --- | --- |
| SimulationWorld | Coupled simulation, placement and frame advancement |
| FluidInventory | Phase/constituent mass, energy, momentum, dissolved gases, disease |
| FluidTransaction | Atomic transfers, reactions, reservations, work and heat |
| FrameSnapshot / SimulationEvent | Coherent immutable state, lifetimes, callbacks |
| MaterialCatalog | Stable identities, properties, reactions, versions |
| Colony save container | Native/custom state from one completed frame |

Rendering projections are never authoritative for composition or pressure.

## Release acceptance

- Physics: hydrostatic stability, buoyant plumes, gas diffusion, vacuum
  expansion, shocks, liquid settling, miscibility, displacement, phase changes,
  dissolved-gas exchange.
- Conservation: 10,000-step closed systems, relative constituent mass drift
  <= 1e-8 and energy drift <= 1e-6; separate trace checks and source/sink ledgers.
- Gameplay: complete producers/consumers, blocked outputs, depletion,
  demolition, melting, disease, aquatic behavior, suits, bionics and rockets.
- Compatibility: startup, generation, save/load for all 32 gameplay DLC
  combinations on both OSes; cosmetic compatibility separately.
- Persistence: same-platform deterministic continuation, bounded cross-platform
  numerical differences, corruption rejection, safe behavior without the mod.
- Performance: <= 25 ms per 0.1 simulated seconds for 100,000 active cells on
  Ryzen 5900X; at 250,000 active cells and 32 duplicants, p95 solver-plus-adapter
  cost <= 62.5 ms per 0.1 simulated seconds, sustained 1× and at least 30 FPS in
  normal play. Include evolving gas, mixed phases, machines and entirely changed
  presentation frames; uniform equilibrium cannot certify active-cell acceptance.
  Target <= 2 GiB additional mod memory, bounded snapshot retention and no growing
  publication backlog. Worker sampling and colour lookup allocate zero after
  publication. Fault-free 10,000-cell decompression and 1,000-cell flooding/phase
  events must recover to sustained 1× within 30 wall-clock seconds.
- Long runs: 100-cycle regression colonies for each pack and both modes;
  1,000-cycle all-content colonies on both OSes with autosaves and
  inter-asteroid transfers.
- Packaging: reproducible mod-owned-only artifacts, installation and
  compatibility docs, updated Steam Community description.

Create the release package only after every gate passes. Workshop publication
is a separate action.
