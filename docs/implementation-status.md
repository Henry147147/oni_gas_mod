# Implementation status

Current state on 2026-10-18. This is a private development milestone: the
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
| [Coverage inventory](ownership-coverage.md) | 3,072 entries (materials, native messages/imports/hooks, channels, call sites, systems, prefabs, converters, recipes), each with owner, adapter and acceptance test; validated by the normal build | All entries are `unimplemented`; `gate` exits nonzero until acceptance is complete |
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
| [Solid-cell heat](native-domain-physics.md#native-solid-cell-heat) | Registered solid faces exchange represented heat through the exact-build temperature hook. Native harness passes on Linux, Wine and the Windows host | **Unity fails**: a published solid endpoint accounts for one temperature step while the fluid journal accounts for two |
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
| [Gas/vacuum front](../native/GAS_VACUUM.md) | **Fails** in the separate `FluidWorld` experiment; the MAC owner and in-game domain reject empty cells |
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
