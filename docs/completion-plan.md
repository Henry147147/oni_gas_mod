# Finish Mixed Atmospheres as a feature-complete ONI overhaul

Accepted execution plan, recorded 2026-10-14. Work proceeds in dependency order;
an implemented helper or standalone test does not satisfy an in-game exit gate.
Each milestone is a separate reviewable change. Regular pulls, commits and
pushes to the shared `main` branch are authorized by the user. Public release
and Workshop upload still require separate authorization.

Execution amendment, 2026-10-16: implement and verify the full feature set in
real games before optimizing it. Slow simulation is acceptable for this stage.
Retain conservation, thermodynamics and numerical acceptance requirements;
runtime/performance targets must not prevent functional integration. Pull and
push regularly while another developer works on `main`. The earlier small
Linux colony priority remains the first integration step, followed by the full
Windows/Linux and DLC scope below.

Architecture amendment accepted 2026-10-16: implement the full overhaul through
[native SimDLL hooks](native-backend-integration.md), retaining verified native
nonfluid services. Observer and takeover work are the next engineering steps;
they do not replace the remaining full-overhaul milestones.

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

## Git checkpoint

1. Initialize a local repository on `main` with the configured Git author.
2. Exclude private game contracts, saves, inspection artifacts and builds.
3. Stage original sources, tools, tests, documentation and CI; exclude licensed
   binaries, decompiled implementations, credentials and generated packages.
4. Review and commit as `Initial mixed-atmospheres core and ONI integration probe`.
5. Record this plan in a second commit; use separate commits for milestones.

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

## B. Complete production timestep

Replace the runtime gas-only reference world with a coupled multiphase world.
Keep isolated components as validation references, without mistaking their tests
for evidence of a complete solver.

The [swept first-moment reconstruction controls](../native/MAC_SWEPT_RECONSTRUCTION.md)
repair a reproduced moving-cell affine error with an opt-in selector. Component,
checkpoint and selected Windows/Linux regressions pass; the strong-flow,
exact-vacuum and production-colony exit gates remain open.

The later [quadratic momentum admission](../native/MAC_QUADRATIC_LIMITER.md)
passes 179 prerequisite groups and 29 saved reviews on each OS and under Linux
sanitizers, then still fails the original collision density refinement bound
(0.856557 versus 0.85). It remains opt-in; this does not satisfy the exit below.

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

Exit: integrated gas/liquid numerical benchmarks pass before broad gameplay
integration. Measure runtime, but defer performance tuning until the feature set
works in real games.

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

Windows V54 passes the scoped four-gas native hover fixture, including the
exact trace cutoff, retained contaminant warning and 125% UI scale; see
[evidence and limits](mixture-presentation.md#verification-and-limits). That
display fixture leaves native ONI in charge of physics. The later
[Linux four-case hover control](linux-tile-hover.md) also passes actual mouse
hover and image review, including the exact trace cutoff and 125% UI scale at
1280 by 720. Its viewport repair preserves every bulk row and hazardous-trace
warning. These close the scoped display-fixture checks on both platforms;
authoritative production colony state remains part of this milestone.
Windows V57 additionally verifies hover against a completed native physical
frame in a six-cell closed-reference scene, including changing composition and
colour-only recapture. It retains the surrounding native colony and does not
complete this production-owner, Linux, save or performance gate.
Windows V58 additionally passes a physical oxygen/heat edit through the all-world
coordinator: the mixture heading and all four bulk rows change with the completed
native state, while hazardous trace warnings remain visible. Colour-only retains
the same physical facts and accounts. Its six-cell reference scene still does
not complete this gate; see [the scoped evidence](mixture-presentation.md#completed-colony-transactions-on-windows).
Later Windows native gas/water controls additionally bind actual hover and
selected details to the same completed physical object and publication as GPU
rendering, including paused gas zoom and two water reload sessions. These
[bounded rendering checks](native-fluid-rendering.md) supplement the required
all-non-trace tile contents; production and Linux hover remain required.

Exit: duplicants survive and operate for ten cycles with conservation
diagnostics, and the presentation, screenshot and performance criteria above
pass on Windows and Linux. This does not authorize public release.

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
  Ryzen 5900X; sustained 1x in representative colonies, bounded memory, no
  growing backlog.
- Long runs: 100-cycle regression colonies for each pack and both modes;
  1,000-cycle all-content colonies on both OSes with autosaves and
  inter-asteroid transfers.
- Packaging: reproducible mod-owned-only artifacts, installation and
  compatibility docs, updated Steam Community description.

Create the release package only after every gate passes. Workshop publication
is a separate action.
