# Implementation status

This is a private development milestone. The requested overhaul is incomplete.
The first public release remains gated on complete gas **and** liquid gameplay.

The accepted [completion plan](completion-plan.md) remains the execution target.
Its 2026-10-16 amendment selects [native SimDLL hooks](native-backend-integration.md)
as the core integration mechanism. The native observer starts implementation;
production takeover, complete physics, gameplay and persistence remain open.
Current work adds managed colony transactions/checkpoints, owned native
snapshot copies, a staged fluid-message router, an integrated gas/liquid solver
prototype, and shared mixture presentation. Actual Windows disposable colonies
exercise snapshot copying, presentation fixtures and two explicitly owned
native-empty reservoirs. Native ONI retains the surrounding world physics;
no first-playable or release gate is marked complete.
The 3,071-entry [ownership inventory](ownership-coverage.md) records known work
and remaining acceptance obligations across the supported packs and platforms.

## Implemented and exercised

| Area | Evidence | Limits |
| --- | --- | --- |
| Linux native hook startup | Verified zero RX tail padding holds all 28 hooks; 96 no-near-allocation hook cycles restore every byte and RX permission. Bounded exhaustion and injected cleanup failure pass; all 13 licensed Linux native domain controls pass | Frozen a5aa2de/v8 Unity gravity and gas-actor save/reload pairs pass, including 120 exact continued frames per load; remaining matrix cases are running. Earlier no-gap failures remain retained. Whole-colony ownership remains open. See [storage contract](native-trampoline-storage.md) |
| Same-process main-menu reload | Linux frozen 466bde0 loads the same staged colony twice in one Unity process, observes native shutdown/main menu, advances native session 1 to 2 and matches the same complete frame-120 image. Both paused autosaves/readbacks pass; 101 declaration scenarios pass per platform | Actor-free bounded scope. Windows Unity, Load-screen file selection and general production runtime remain open. See [evidence](native-menu-reload.md) |
| Bounded Unity colony save/load | Linux gas/water reloads and MSVC Windows cohort windows-native-features-r1 restore the original GUID/three duplicants and exact physical image. Both Windows air/water loads run an actual deferred autosave at frame zero, preserve pause depth 1, deliver two startup batches once and match all 120 resumed physical frames; Grid/rendering and outside native heat pass | Drained development domains with no live owned actors. Pending-command saves reject without writing or losing their command. Owned actors, actual menu/day-rollover triggers and whole-colony ownership remain open. See [Windows evidence](sealed-native-domain.md#additional-windows-feature-controls) and [persistence protocol](../native/MAC_CHECKPOINT.md#paused-unity-colony-saveload) |
| Complete colony write readback | Linux matched-heat/deferred gravity autosaves and actual Windows air/water ordinary saves plus deferred autosaves match their complete serialized bodies, exact paired sim sections and GUIDs. Windows body sizes span 10,542,598 to 10,548,893 bytes. All 81 synthetic layout/readback groups pass on actual Windows .NET; earlier Linux and Wine evidence remains retained | Completed-write receipt; ONI replacement/crash durability and actor persistence remain open. See [Windows evidence](sealed-native-domain.md#additional-windows-feature-controls) |
| Initial paused restore scheduling | Linux repaired-water control and actual Windows air/water controls hold three positive latched intervals, preserve native bytes/time and managed accumulators, autosave at frame zero and match all 120 resumed images. All 100 declaration/snapshot scenarios pass against Windows declarations; earlier Linux evidence remains retained | Bounded startup-save hold only; general production ownership remains open. See [Windows controls](sealed-native-domain.md#windows-paused-start-and-menu-controls) |
| Windows Save As menu and source isolation | Actual Escape/Save As/New Save creates a colony file; a fresh process restores its exact physical image at 30.2 s and matches 120 frames to 54.2 s. A second Save As stays inside the new evidence directory and retains the exact 3,507-byte completed physical image. Original input hashes remain unchanged; the runner now loads a separate copy | Save As creation/reload scope only. Overwrite Save was cancelled; Load menu, day-rollover, actor persistence and complete managed-reference restoration remain open. See [menu evidence](sealed-native-domain.md#windows-paused-start-and-menu-controls) |
| Save decline and denied-write recovery | Linux `save-problem-deferred-io-control-v4` declines a pending heat command without writing, then handles a real access error through ONI UI and all three deferred callbacks. Complete physical before/after hashes match, dragging and prior save selection return, time resumes and a later header/body-verified colony save succeeds. Core 91, layout/readback 82 and both-platform declaration 100 groups pass | Actor-free bounded scope. New failure UI still needs Windows Unity coverage; previous Windows air/water save/load passes are retained. Replacement/crash durability remains open |
| Native-owned unassigned disease counters | The actual rejected Linux image has hash zero/count 24 at cell (102,151). Licensed Linux and Windows-under-Wine loaders admit and normalize the same controlled state; strict migration still rejects it. Preserved-world admission keeps source bytes exact, and scoped restore controls reject the same counter in owned cells | Native-owned opaque metadata only; no custom-fluid germ transport, survival or metabolism is implemented |
| Native paired-domain restore | Licensed Linux and Windows-under-Wine gas, compressed-water and gravity controls load both saved sections, restore flow flags at zero time and exactly match twelve subsequent worker images. Invalid metadata, native inventory, geometry, phase, clock and physical integrity reject before native loading | Isolated native reload; the later bounded Unity binding is listed above. Actor persistence and whole-colony ownership remain open. See [restore protocol](../native/MAC_CHECKPOINT.md#native-domain-boundary) |
| Paired native/physical worker capture | Linux `20261016T232719Z-0a1d98` captures both owners at frame 6 / native epoch 541, retains 3,585,197 native and 3,507 physical bytes, and validates their shared envelope. Licensed Linux and Windows-under-Wine paused controls verify zero physical-time advance, one native save, immutable reads and token retirement; all thirteen Release/ASan CTests and six forwarding comparisons per platform pass | This capture cohort was diagnostic; the later bounded Unity binding above adds managed save/load. Full actor/outbox persistence and whole-colony ownership remain open. Raw scalar saves remain guarded. See [capture protocol](../native/MAC_CHECKPOINT.md#paired-native-capture) |
| Lossless native physical checkpoints | Linux `20261016T224334Z-cee2a7` exports/restores 3,503 bytes and exactly predicts the actual next worker frame; all thirteen native Release/ASan CTests pass. Licensed Linux/Windows-native controls pass; cross-platform reads preserve original bytes, including the actual Linux Unity image under Wine | Physical-owner format with drained commands/actors/receipts; the later paired Unity binding is listed above. Full actor/outbox persistence and general production ownership remain open; scalar save guards remain active. See [format and continuation evidence](../native/MAC_CHECKPOINT.md) |
| Atomic native actor configurations | Linux `20261016T223037Z-cf58d3` passes ordered owned rate batches, ordinary outside native batches and the full real-consumer/emitter callback/removal fixture. Native late invalid/stale/removal batch controls preserve earlier configurations; Linux Release/ASan and Windows-under-Wine tests pass | Owned configuration only; mixed-owner batches and owned batch registration/removal require further native admission. General device reservations and persistence remain open. See [registered actor contract](sealed-native-domain.md#registered-gas-actors) |
| Filled-water gravity and energy coordinates | Native scenario 3 retains the original physical reference energy while storing water work/heat separately. The 1200-frame control passes U+K+G and exact reload; licensed Linux and Windows-under-Wine water-gravity save/restore controls pass twelve exact resumed images | Linux Unity source/save passes 121 frames with real water callbacks and U+K+G residual 0.0000661 J under its unchanged bound; fresh-process/menu reloads are running or pending. Six-cell occupied water only; free surfaces, phase change, general material coverage and whole-colony ownership remain open. See [energy and gravity contract](native-liquid-gravity.md) |
| Native-clock gravity and boundary potential | Linux `20261016T222138Z-5dd9d7` passes 120 post-priming frames, real gas callbacks, vertical force/momentum and U+K+G conservation; residual -3.48e-9 J. Potential carried by injection/consumption is 0.147150006 J. Licensed Linux and Windows-under-Wine controls also pass; native regressions cover actors at different elevations and rejected work | Explicit six-cell occupied gas scenario; arbitrary nonlinear buoyancy, free surfaces, whole-colony ownership and persistence remain open. See [gravity contract](sealed-native-domain.md#gravity-and-carried-potential-energy) |
| Native-clock internal fluid conduction | Linux `20261016T220819Z-dbcb0f` passes 120 post-priming frames, 5,124 thermal stages, 1.01283 J internal redistribution and positive entropy; total energy residual -3.03e-9 J. Licensed Linux and Windows-under-Wine native controls, independent pair/time-refinement tests and all twelve Release/ASan native CTests pass | Occupied single-phase constant-capacity cells, insulated boundary and development conductivity rule; fluid/solid exchange, variable properties and phase changes remain open. See [operator and evidence](sealed-native-domain.md#internal-fluid-conduction) |
| Windows native domain and reproducible runners | Actual MSVC/Windows Unity gas, compressed water, conduction, CO2, registered actors, gravity, ordinary air save and fresh-process air/water loads pass. Each load matches 120 exact physical continuation frames and a deferred frame-zero autosave. Earlier two-bulk-gas hover matches completed revision 121. Strict MSVC/GCC 11 builds pass all 13 CTests; Core 89, presentation 41, Windows layout 81 and Linux Python guards 51 pass | Bounded six-cell scenes; four-gas/trace production hover, actor persistence, arbitrary materials/geometry, whole-colony and first-playable acceptance remain open. Both Linux libraries require at most glibc 2.34. See [evidence](sealed-native-domain.md#additional-windows-feature-controls) |
| Physical native-domain carbon dioxide | Linux `20261016T205811Z-f4c02b` passes actual CO2 injection/consumption, complete five-species transport/snapshots, both callbacks, chemical/internal and carried kinetic energy; U+K residual -3.33e-9 J. Domain/transfer ABI 2 rejects old four-species clients | Five explicit development materials; no complete thermodynamic catalogue, metabolism, scrubbing process reservation or aquatic chemistry. See [scope and evidence](sealed-native-domain.md) |
| Registered native-clock gas point actors | Linux `20261016T204654Z-021e13` passes an actual Unity consumer, native emitter registration, 32 consumer callbacks, 40 emission samples, rate changes, blocked/unblocked callbacks and removal; original native transfers stay inactive | Selective, disease-free single-cell boundaries; no spatial/bulk operation, device power/input reservations or persistence. See [actor contract](sealed-native-domain.md#registered-gas-actors) |
| Filled-liquid heat and stored-energy rounding | Same +1500 J licensed old-library control fails at -0.00854 J; conservative neighboring rounding removes that drift without wider bounds. Component controls retain exact U over 2400 small stages, insulation and explicit unavailable/sub-ULP cases. Resolvable Linux/Windows-under-Wine heat controls pass | Linux Unity +1.5 MJ heat/save/reload pair passes 120 exact resumed frames; Windows Unity remains open. Full-range precision, variable properties, solid exchange and phase changes remain open. See [method](../native/MAC_CONDUCTION_ROUNDING.md) |
| Filled-liquid registered point actors | Linux Unity passes 121 frames, 32 actual consumer callbacks, 40 emission samples, density edges, batches and removal. Paired save and fresh-process reload match all 120 physical frames. Linux/Windows-under-Wine native atomic/void regressions pass | Windows Unity remains open for new water actors. Live actor/outbox persistence, void creation and device reservations remain open. See [contract](sealed-native-domain.md#filled-liquid-registered-actors) |
| Filled-liquid point transfers | Linux Unity source scene passes 122 frames, both real water callbacks once, elastic parcel U and carried mechanics, exact native cells and paired save readback. Licensed Linux/Windows-under-Wine controls match 12 checkpoint frames | Fresh-process Linux reload also matches all 120 physical frames after frame-zero deferred autosave. Windows Unity remains open for this feature. Filled single-liquid water only; free surfaces and device work remain open. See [contract](sealed-native-domain.md#filled-liquid-point-sources) |
| Native-clock sealed-domain gas transfers | Linux ONI `20261016T202909Z-bef415` passes 120 post-priming frames, real oxygen injection/consumption and both actual callbacks once; exact completed receipts, source exclusions and U+K/mass accounts pass. Immutable core, C ABI and fault-event replay regressions pass | Bounded actor-free occupied gas scene; no liquid/vacuum transfer, disease, registered actor, power reservation or persistence. See [contract and evidence](sealed-native-domain.md#gas-point-sources-and-game-callbacks) |
| Native SimDLL forwarding observer | Separate exact-build profiles; Linux Release/ASan and actual Windows MSVC ABI/short-branch controls; six identical Windows native comparisons (including live emitter/consumer registration, rate changes and removal), original five on Linux; actual Windows V60 and Linux Unity colony forwarding pass | Observation only; original ONI owns physics. Full native reader/writer coverage and production takeover remain required. The later Ubuntu 22.04 builder and bounded gravity save/reload pass use libraries requiring at most glibc 2.34; no actual Ubuntu 22.04-host Unity run is claimed. Windows V59 timing failure is retained; V60 waits for actual native progress. See [contract and evidence](native-backend-integration.md#current-evidence-and-limits) |
| Native mixture/thermodynamic reference core | 57/57 current GNU Linux scenarios, including tiny/zero-limit trace hazards; earlier 56-scenario Windows/Linux and Linux ASan/UBSan evidence is retained | Constant properties; incomplete material coverage |
| Gas finite-volume baseline | Hydrostatic, buoyancy, diffusion, pressure, vacuum and conservation tests | Gas only; explicit acoustic performance fails target |
| Integrated gas/liquid prototype | 10 closed-mode cases plus 24 occupied-1D groups and bounded gas/vacuum consistency checks pass MSVC, strict GNU and ASan/UBSan; independent thermodynamic reference passes 26 groups | Full vacuum-front convergence fails: N1024 recent speed is 5.874c versus exact 5.019c, and N2048 worsens front position. Dormant outside the C ABI/runtime; 2-D, cavitation, immiscible interfaces and phase transitions remain unproved. See [the failed gate and next design](../native/GAS_VACUUM.md) |
| Retained material-node mechanics | 17 groups and all seven native CTests pass MSVC, strict GNU and ASan/UBSan, with independent gas/liquid adiabats, acoustic/time refinement, immutable EOS ownership and finite aggregate mass | Earlier scalar-coordinate experiment; no Eulerian remap, FluidWorld mode or game activation. Nonlinear work error is reported explicitly. The newer material-only vacuum check is listed below. See [equations and limits](../native/OCCUPIED_STAGGERED.md) |
| Paired local coordinates and retained vacuum expansion | Current Windows/Linux runs pass three new geometry groups, seventeen retained-state groups, twenty-seven independent thermodynamic groups, a coarse case and the N512/1024/2048 joint front-position/speed trend. Exact-rational/high-precision reviewers validate saved operands and reject altered data | Explicit 1-D material-only option; scalar remap consumers reject it. Finest front remains 0.937 m short and 1.569c0 too slow. No production accuracy, paired liquid/remap/2-D/game/performance acceptance. Earlier scalar-coordinate failures and the Windows timeout remain retained. See [results and limits](../native/GAS_VACUUM.md#retained-nodal-replacement-path) |
| Immutable EOS identity lookup | Profile metadata is serialized once after validation; lvalues borrow immutable identity and temporaries return owned copies. All twenty-seven independent reference groups pass Windows/Linux, including copy/move/assignment and temporary lifetime checks. Completed coarse and prior fine physical records remain byte-identical | Removes repeated formatting in reference pressure operations. The full control fits its unchanged runtime bound; this does not satisfy the 100,000-cell production performance gate |
| Retained MAC face-momentum transport | Seven strict Windows/MSVC and Linux/GCC15 groups pass, including a solid corner, variable-density uniform velocity, immutable rejection, joint 2-D shear/passive grid refinement and timestep refinement. A saved-data checker passes 3,888 checks and rejects three corruptions per platform | Closed occupied regular-grid transport with caller-supplied fluxes. No pressure, gravity, free surfaces, phase topology, persistence, world/game activation or performance acceptance. Earlier failed remap/vacuum gates are unchanged. See [equations and evidence](../native/MAC_TRANSPORT.md) |
| Closed MAC EOS pressure/transport owner | Eight Windows/Linux groups pass, including pressure-driven flow, elastic wet corners, constant-p/T contacts, compressed liquid, immutable EOS ownership, adaptive/atomic intervals and joint implicit acoustic pressure/velocity refinement. The current source passes 6,538 saved-data checks and three corruptions; 7 transport, 57 core, 17 retained-node and 24 occupied-fluid regressions pass | Retained canonical face J and physical U with an explicit EOS; closed occupied grids, zero gravity by default. No vacuum/free surfaces, shock/phase/immiscible refinement, persistence, C ABI/game activation or production performance acceptance. The 24-group regression was supplemental after gravity to correct a runner target/count mismatch. See [current and initial evidence](../native/MAC_CLOSED_WORLD.md) |
| Shared MAC material reconstruction | Five strict Windows/Linux groups pass, including gas/liquid constant-p/T contacts, cold compressed-liquid EOS limiting, atomic malformed-plan rejection and bounded oblique advection. Each grid doubling reduces L1 error to 0.311/0.269 of the previous value, below 0.45. A separate reviewer passes 13,325 checks and rejects three trace/limiter/wave corruptions | Actual constituent/U/passive parcels and retained volumes share one limiter; actual masses drive dual transport. Geometric momentum and owning integration have the separate controls below. No arbitrary-interface, general entropy, vacuum, production/game or performance acceptance. R1/R2/R3 first failures remain retained. See [method and evidence](../native/MAC_RECONSTRUCTION.md) |
| MAC gravity and stratified controls | Four Windows/Linux groups pass with reconstructed material transport and explicit nested-EOS precision: gas/wet-corner U+K+PE accounts, gas/liquid/layered rest, joint N32/64/128 pressure/velocity/entropy refinement and atomic invalid-domain controls. The current full reviewer passes 16,958 checks and three corruptions per platform; mode paths have no retries | The original scientific source, inputs, timestep, grids and physical bounds are unchanged. Initial donor-cell refinement failure and intermediate Linux hydrostatic failure remain recorded. Smooth occupied-grid evidence only; general nonlinear buoyancy, shocks, free surfaces, game/production/performance acceptance remain open. Gravity stays opt-in. See [repair, results and first failures](../native/MAC_GRAVITY.md) |
| Closed MAC strong-gas gate | The viscosity-matched heat owner passes Sod N64/128/256, then collision pressure/velocity ratios 0.796376/0.611031 meet 0.85 while density ratio 0.856166 fails. The partial saved review passes 59,205 identities across five complete profiles | **Gate remains failed**. No collision N256 or separation runs. Windows/Linux pass all 133 prerequisite groups/reviews; Linux ASan/UBSan passes 72 selected groups/reviews. Linux does not repeat the shared failure. Original scientific inputs and limits remain unchanged; no full strong-flow, production or game acceptance. See [current and retained failures](../native/MAC_RIEMANN.md) |
| Derived MAC energy range | Seven strict Windows/Linux groups pass for independent exponents, kinetic/variance parcels, exact additive expansions, unchanged relative accounts, explicit projection loss and unsupported arithmetic modes. Each saved-data reviewer passes 21,019 exact-fraction checks and rejects three corruptions | Standalone arithmetic evidence; the later transport integration has separate controls. Excluded from default/game dispatch. Primary inventory/momentum rules remain unchanged and strong-flow acceptance stays open. No production/performance acceptance. See [representation and evidence](../native/MAC_ENERGY_RANGE.md) |
| Wide derived-energy transport/owner | Four strict Windows/Linux groups and 4,530 exact/80-digit saved checks pass, including tiny K/parcels/heat, a warm thin-gas pressure reversal, immutable owner clocks and retained primary-passive rejection. All range/transport/parcel/closed/gravity prerequisites and their data reviews pass; each reviewer rejects three corruptions | Full energy values remain positive/nonzero below binary64 range; legacy projections carry explicit masks/counts. That cohort retains original primary-transfer checks and its Linux helium failure; the later scaled-parcel cohort below passes that creation and reaches incoming momentum range. Windows collision accuracy remains unresolved. No production/performance/game acceptance. See [integration and evidence](../native/MAC_ENERGY_TRANSPORT.md) |
| Scaled primary parcel contributions (first cohort) | Five strict Windows/Linux groups and 7,681 saved exact-fraction checks pass, including the original helium scale, rounded subnormals, sparse passives, four quarter-denormal contributions forming a new stored trace, and atomic unsupported-storage/invalid-scale rejection. All six earlier native/review prerequisites pass; each reviewer rejects three corruptions | Full common-scale prototypes reach donor/target accumulation; primary cell state, bulk fluxes and canonical J remain binary64 with original conservation bounds. The Linux retry passes helium creation but rejects an incoming momentum share near 3.6153e-585 kg�m/s before any completed profile. No strong-flow/game/performance acceptance. See [representation and evidence](../native/MAC_PRIMARY_PARCELS.md) |
| Scaled momentum contributions (first cohort) | Four strict Windows/Linux groups and 3,391 saved exact-fraction checks pass, including the original below-range share, minimum-denormal recombination, signed cancellation with positive mixing heat, and atomic unresolved-primary rejection. All earlier prerequisites and their separate three-corruption reviews pass | Full signed contributions reach one bounded primary projection; canonical/wall state remains binary64. The Linux retry passes sharing then rejects a new open-face J below storage range. A coherent wider canonical representation remains required; no strong-flow/game/performance acceptance. See [results and diagnosis](../native/MAC_MOMENTUM_PARCELS.md) |
| Full canonical/flux range | Five new Windows/Linux groups and 7,040 exact/80-digit saved checks pass, including full J/velocity/sweeps/bulk flux in two completed uniform owner steps, new open-face J, wall reaction/heat, stale-projection and unsupported-gravity rejection. All 49 native groups pass in that cohort. The first independent thin-gas pressure-reference failure is retained; an analytic gas-only reference passes against original captures without native replay | Explicit full fields back validated double projections. Constituent/U cells remain binary64; full vertical-gravity work is unsupported. That cohort's signed 32-bit exponent rejection is addressed by the later integration below. No full strong-flow, production/game/performance acceptance. See [representation and first results](../native/MAC_CANONICAL_RANGE.md) |
| Bounded large integer exponents | Six new strict Windows/Linux groups, 8,560 saved checks and all 55 native prerequisite groups pass; every full reviewer rejects three corruptions. Linux ASan/UBSan passes the six new controls and saved review with empty stderr. Immutable signed magnitudes extend an inline signed64 path while significands remain binary64 | Bounded integer storage only; no numerical floor or cutoff. Linux completes full Sod refinement and reaches the retained collision-accuracy failure. First Linux loop-copy build errors are retained and repaired without arithmetic changes. No independent-agent, full strong-flow, production/performance/game acceptance. See [representation and results](../native/MAC_EXPONENT_RANGE.md) |
| Compatible compression and atomic owner | Six component groups with 6,095 saved checks plus four new owner groups with 11,214 checks pass on Windows/Linux. All 65 native groups and saved reviews pass; every full reviewer rejects three corruptions. Positive compression heat, full wall impulse and unresolved increments commit with the owner clock; disabled reference and later-failure rollback pass | Quadratic normal compression regularization only, separate from physical shear viscosity. A newly exposed small wall-reaction precision failure is repaired at the unchanged 128-epsilon bound. Current Windows collision velocity converges but pressure fails. First review/metadata/physical failures remain retained. Excluded from default/game/C ABI; no new sanitizer, strong-flow, production or performance acceptance. See [method, repairs and results](../native/MAC_COMPRESSION.md) |
| Acoustic and quadratic compression component | Seven new Windows/Linux groups, 12,971 saved checks and four corruption rejections pass. All 110 native groups/reviews pass both platforms; Linux ASan/UBSan passes 49 groups/reviews with empty native stderr. Gas/wet/liquid sound speeds match independent EOS and isentropic perturbations | Explicit separate law with fixed half-sound-speed linear term and exact frozen pair relaxation; full impulse/heat, walls, traces and unresolved updates retain the original bounds. That component cohort retains a quadratic owner. The later pressure-sensed owner below passes its staged controls but still fails the original strong-flow gate. First derived-coefficient range failure remains retained; primary storage limits are unchanged. See [method and evidence](../native/MAC_ACOUSTIC_COMPRESSION.md) |
| Pressure-sensed acoustic component | Three Windows/Linux groups, 14,832 saved checks and four scalar/grid/trace/coefficient corruptions pass. Full pressure curvature retains a positive below-range ratio; gas/wet walls, transpose, uniform-pressure quadratic identity and an actual new tiny canonical J pass | Normalized pressure curvature gates the fixed half-sound-speed coefficient. The original weak Fourier profile is restored; its 5e-5 limit is unchanged. First invalid-zero fixture failure is retained and corrected without solver changes. No game or production acceptance. See [method and evidence](../native/MAC_PRESSURE_SENSOR.md) |
| Pressure-sensed acoustic owner | Six default owner groups and 126,803 saved checks pass both platforms, rejecting clock/chain/trace/heat/tangent/law corruptions. All 119 native groups/reviews pass Windows/Linux; Linux ASan/UBSan passes 58 groups/reviews with empty native stderr. All twelve checkpoint mode combinations, legacy images and cross-platform complete bytes pass | Default compression now uses pressure-sensed acoustic viscosity; explicit quadratic and disabled references remain. Weak Fourier profiles agree within 6.80e-7 of amplitude. Original strong-flow retry passes Sod, then fails collision density/pressure ratios 0.859942/0.881544 against 0.85. First unsensed weak-wave failure remains retained. Actual game, first-playable and production/performance acceptance remain open. See [owning evidence](../native/MAC_ACOUSTIC_OWNER.md) and [failed profiles](../native/MAC_RIEMANN.md) |
| Viscosity-matched compression heat component | Eight new groups and 44,924 saved checks pass Windows/Linux, rejecting heat/trace/capacity/tangent/sensor/geometry corruptions. All 127 native groups/reviews pass per OS; Linux ASan/UBSan passes 66 groups/reviews with empty native stderr and all 92 compile commands audited | Optional heat conductance uses Cp and the shared frozen pressure-sensed acoustic viscosity coefficients. Both prior owner references remain byte-for-byte unchanged; all twelve checkpoint combinations, retained images and cross-platform bytes pass. The separate owning integration below selects heat mode 2 and still fails the original density refinement gate. No game/production acceptance. See [method and evidence](../native/MAC_MATCHED_HEAT.md) |
| Viscosity-matched heat owner | Six new owning groups pass 133,225/133,219 saved checks and eight corruption rejections on Windows/Linux. All 133 groups/reviews, eighteen checkpoint combinations, eight retained images and three cross-platform images pass per OS. Linux ASan/UBSan passes 72 groups/reviews with empty native stderr; every compile command is instrumented | Persisted heat enum 2 selects Cp and pressure-sensed conductance. Legacy heat values 0/1 retain their behavior; fourteen retained raw streams are unchanged. Original Fourier profiles agree within 6.75e-7 of amplitude against 5e-5. The changed-method strong retry fixes collision pressure refinement but density still fails (0.856166 against 0.85). No new Unity, first-playable, production or performance acceptance. See [owning evidence](../native/MAC_MATCHED_OWNER.md) |
| Two-half-cell compression heat component | Eight new groups and 34,507/34,511 saved checks pass Windows/Linux, rejecting nine corruptions. All 141 strict groups/reviews and 80 selected Linux ASan/UBSan groups/reviews pass; all 93 compile commands are audited for instrumentation | Optional reciprocal half-cell conductance retains Cp and pressure-sensed local viscosity. All 52 preceding native streams and six written images are byte-for-byte unchanged; eighteen checkpoint combinations, eight retained images and cross-platform bytes pass. Owning/default heat mode 2 remains selected, and the original strong gate remains failed. No new owner, Unity, first-playable or performance acceptance. See [method and results](../native/MAC_SERIES_HEAT.md) |
| Explicit half-cell heat owner experiment | Trial passes 147 strict groups/reviews per OS, 107,445 new saved identities and eight corruptions, 24 checkpoint combinations, ten retained images and cross-platform reads. Linux ASan/UBSan passes 86 groups/reviews with every compile command instrumented | Original strong-flow retry fails collision density/pressure ratios 0.919310/0.897892 against 0.85; velocity ratio 0.597402 passes. The previous mode-2 default is restored and mode 3 remains explicit. Restored-default Windows/Linux validation also passes all 147 groups/reviews, 24 setting combinations, ten retained images and cross reads; the repeated Linux sanitizer run passes all 86 selected groups. Six legacy owner captures/four legacy images and two explicit-trial captures/six trial images remain byte-for-byte unchanged. These byte-equality receipts precede the later liquid-conduction correction; after merging that fix, all 28 affected groups/reviews, 24 checkpoint combinations, ten retained images and cross-platform reads pass again on Windows/Linux, plus the same 28 selected Linux sanitizer groups. No new game, first-playable or performance acceptance. See [trial and retained failure](../native/MAC_SERIES_OWNER.md) |
| Symmetric compression coupling and endpoint entropy | The existing five owner groups pass 17,126 saved checks and three corruptions with the reconstructed default, including both compression halves, exact manufactured subflow replay and endpoint entropy. Manufactured temporal error ratios remain 0.250120/0.250030 | Compression timing and the owning reconstruction below have separate controls. Collision density/pressure refinement still fails; no full strong-flow or game/production acceptance. See [method and earlier results](../native/MAC_COMPRESSION_COUPLING.md) |
| Compatible momentum correction limiter | Six strict Windows/Linux groups, 420 exact-fraction saved checks and three corruption rejections pass. Linux ASan/UBSan passes the six groups/review with empty stderr. Twenty prerequisite energy/exponent/transport groups and their saved reviews also pass per OS. Scalar momentum-advection error ratios are 0.303600/0.283049 | Pair/batch correction of momentum and pending upwind heat only; no withdrawal from stored U, no floor. The owning integration now also constrains actual transported work; the original strong-flow gate remains failed. Closed owner/game dispatch unchanged; no production/performance acceptance. See [method and evidence](../native/MAC_MOMENTUM_LIMITER.md) |
| Geometric MAC momentum/work transport | Six strict Windows/Linux groups, 3,964 saved checks and geometry/work/trace corruption rejections pass. All 85 native groups and saved reviews pass per OS. Normal/transverse refinement ratios remain 0.303600/0.283049 and 0.358288/0.400436 | Actual dual mass fluxes drive limited prediction before material heat deposition. Local accounts carry validated stage bounds through cancellation; work uses full transported-energy capacity. Global U+K, traces and wall accounts keep their original limits. The owning default below selects this mode; strong-flow still fails. See [component method](../native/MAC_MOMENTUM_RECONSTRUCTION.md) |
| Reconstructed owning pressure timestep | Seven new native groups and 107,210 saved checks pass on Windows/Linux; geometry/work/trace/clock/capacity corruptions reject. All 85 prerequisite groups/reviews pass. Linux ASan/UBSan passes 21 limiter/owner/closed-step groups and reviews with empty native stderr | Accepted substeps commit actual state, clock and correction diagnostics atomically; explicit donor reference remains available. Full work sums protect nearly empty and below-binary64 capacity; backoff renormalizes across powers of two. One strong retry fails as reported above. No new Unity, full-solver, production or release acceptance. See [method and evidence](../native/MAC_MOMENTUM_OWNER.md) |
| Compression-activated heat redistribution | Eight component groups pass strict Windows/Linux builds; saved reviewers pass 13,270/13,252 checks and reject heat/trace/geometry/root corruptions. Linux ASan/UBSan passes eight groups and review with empty stderr. Constant-capacity network error reaches 0.008382 K; admissible mixed-EOS brackets and uniform-pressure contacts pass | Heat directly brackets a valid thermal partition using EOS capacity bounds and an actual U-storage certificate. The initial endpoint-domain failure and separate 2 K source-precision failure remain retained. The later owning integration below enables this component by default; original scientific tests and physical limits remain unchanged. Strong-flow/game acceptance remains open. See [method, repair and evidence](../native/MAC_SHOCK_HEAT.md) |
| Compression-activated heat owner | Six new owner groups and 122,035/122,029 saved checks pass Windows/Linux; all 102 prerequisite groups/reviews pass both platforms. Linux ASan/UBSan passes 35 groups and reviews with empty native stderr. Heat, compression, pressure/transport and optional material conduction commit atomically with the owner clock | The default closed occupied owner selects shock heat. Original strong-flow retry passes Sod, then fails collision density/pressure refinement at N128 (ratios 0.969739/1.025353); central density errors fall below 0.7%. First reviewer failure is retained and corrected using the same native data. No new game, production or performance acceptance. See [integration evidence](../native/MAC_SHOCK_HEAT_OWNER.md) and [failed strong profiles](../native/MAC_RIEMANN.md) |
| Canonical failure diagnostics | 16 explicit Windows/MSVC and Linux/GNU groups pass, with owned first-failure evidence, raw-bit capture, read-only arithmetic preflight and bounded JSON; a separate decoder checks 642 numbers, 640 exact decimal roundings and five corrupt-data rejections | Excluded standalone targets only; no canonical state owner, pressure step, EOS/game activation or physical/performance acceptance. Earlier compilation/formatting failures are retained. See [scope and validation](../native/CANONICAL_MATERIAL.md) |
| Experimental canonical material owner | Current strict Windows/MSVC and WSL Linux/GNU gravity suites pass four groups each; exact reviews check 540 identities, 81 stored updates and twelve accounting closures. The explicitly retried zero-gravity suite passes four groups, including `h=2^-56 s`, with 566 identities and 83 updates. Full primary momentum drives fixed physical-node residual corrections | Separate 1D static library, excluded from game/default dispatch. Physical controls cover 8–11 nodes; production transport/remap, vacuum/phase topology, persistence, performance and colony ownership remain unfinished. Earlier gravity/regression failures remain recorded; their current retests use changed algorithms and unchanged inputs/bounds. Historical failed C7/translation/remap/vacuum/performance gates were not replayed. See [current source evidence](../native/CANONICAL_MATERIAL.md#current-source-validation) |
| Gas-only isentropic work precision | All 27 thermodynamic reference groups pass Windows/MSVC and WSL Linux/GNU. The analytic volume inverse removes tiny-shift root error; new signed `2^-20` through `2^-120 m3` checks compare work, tangent and nonzero balanced energy with independent quadrature | Unchanged gas/liquid and liquid roots; same declared domains and failure semantics. No production timestep, vacuum-front, game or performance acceptance. See [algorithm and evidence](../native/experimental/README.md#gas-only-small-work-precision) |
| Coupled native/managed world boundary | Six base C ABI groups, six passive-metadata groups and the current 29 managed groups pass Windows/MSVC and WSL Linux/GNU. Exact conserved imports, immutable advances, coherent readings, trace/metadata transport, output preflight and whole-call rollback are exercised | Explicit excluded library wraps the closed 2D reference. No production ONI frame-hook connection, vacuum, phase topology, disease growth/radiation-dose physics, persistence or production game/performance acceptance. Earlier compile/driver failures remain recorded. See [interface and evidence](../native/COUPLED_ABI.md) |
| Conserved physical cell mutations | Seven native mutation groups plus six base/six passive native regressions pass Windows/Linux; the current managed suite passes all 29 groups against those pinned libraries. Exact U/P, native revision, signed external accounts, positive traces, late rollback and subsequent physical advance are exercised. A data-only reviewer passes 304 checks per platform and rejects altered trace/heat records | Immutable transaction preparation for the closed reference. No entropy/process/power reservation, other-owner commit, ONI router hookup or production ownership. Unedited state/time is retained; fixed local arithmetic bounds are reported. The prelaunch compiler-audit erratum is preserved. See [contract and evidence](../native/COUPLED_ABI.md#conserved-physical-mutation-preparation) |
| Closed-reference colony frame coordination | Five original coordinator groups within the current 29-group Windows/Linux suite pass: two-world admission, identical native clocks, shared attempt accounting, rejection after one completed world candidate, immutable samples and retirement/disposal | Persistent native owners supply physical U/P; Core stores a read-only scalar-total-energy projection with measured rounding differences. No ONI callback-router integration, physical save/reload or first-playable acceptance. See [publication contract](../native/COUPLED_ABI.md#closed-reference-colony-frame-coordination) |
| All-world physical transactions and boundary accounts | Seven added coupled groups pass within the current 29; all 89 Core and 41 presentation groups pass Windows/Linux. Exact current-source plans, later-world edit/step-budget rollback, physical reading publication, exact declared account accumulation and retirement are exercised; a rational reviewer checks ten rounding/account cases and two corruptions | Closed reference only. The projection now rejects legacy inventory ledgers/writers/checkpoints. Exact boundary bookkeeping is separate from physical residuals and supplies no device/power reservation or process/entropy acceptance. Native compiled source and approved libraries are unchanged. See [frame contract and evidence](../native/COUPLED_ABI.md#all-world-frame-transactions-and-boundary-accounts) |
| Physical completed-frame presentation | Current 29 coupled and 41 presentation groups pass Windows/Linux. Windows ONI V58 passes four hover/Unity-PNG cases: initial/changed mixture, physical oxygen/heat edit and colour-only recapture. The heading changes to nitrogen–oxygen, every bulk row remains visible and the chlorine warning survives | Six synthetic closed-reference cells; surrounding colony physics remains native. Production containers, Linux Unity, physical save/reload and performance gates remain missing. Earlier V55/V56 startup and Linux capture failures are retained. See [scope and evidence](mixture-presentation.md#completed-colony-transactions-on-windows) |
| Retained increment arithmetic | Five Windows/Linux groups pass; exact rational review checks 124 high/low updates, ten explicit rounding remainders, seven typed negatives and corrupted-data rejection | Also used by canonical initialization. Complete pair updates in the pressure operation use separate sum/change certificates and signed remainders. Excluded by default; no failed physical gate was rerun or accepted. See [retained storage](../native/CANONICAL_MATERIAL.md) |
| Retained scalar multiplication | Five Windows/MSVC and WSL Linux/GNU groups pass; exact review checks 136 products, 127 explicit remainder cases, ten typed negatives and corrupted-output rejection. Numerical rows agree across platforms | Also used by retained geometry, momentum, impulse and finite work ledgers. Its standalone arithmetic controls invoke no EOS, physical step, game or failed-gate replay. See [product contract and limits](../native/CANONICAL_MATERIAL.md#retained-scalar-multiplication) |
| Differences of retained values | Seven Windows/MSVC and WSL Linux/GNU groups pass; exact review checks 290 differences, 160 explicit-remainder cases, a 64-update chain, four typed negatives and corrupted-output rejection. Numerical rows agree across platforms | Also used by retained owner initialization and the new pressure-state changes/accounting. The original failed translation gate remains negative; no production/game acceptance follows. See [difference contract](../native/CANONICAL_MATERIAL.md#differences-of-retained-values) |
| Native and managed persistence | Native roundtrip/continuation checks, managed colony checkpoints, and a checksummed native/custom save envelope; isolated Windows-v4 and Linux-v5 native APIs reject the diagnostic envelope after ordinary native-load positive controls | Envelope is not integrated with actual manual saves, autosaves, restart or cross-platform colony loading. Native rejection does not verify Unity's outer-load/fallback behavior. See [native scope](../tools/NativeShadowProbe/README.md) |
| Managed API, settings, native bootstrap, import and colony state | 89 scenarios pass on Windows and isolated WSL Linux, including owned containers, full allocation handles, staged-write failure atomicity, packed mass storage, traces, geometry, v3 checkpoints, exact-publication reservations and physical-projection role guards | Tested transaction helpers do not route all gameplay actions; Core reservation does not pin caller-owned native resources |
| Pump/separator reference processes | Enthalpy, shaft-work, entropy and trace-separation tests | No game power/storage/heat-sink transactions |
| Implicit heat exchange | Analytic pair solution, stiff contacts, energy/entropy and insulation checks | Not yet connected to native solid/building transfers |
| Native v15 fluid importer | All-world staging preserves fluid species, mass, temperature, radiation and disease before native Load | Real generated compressed/overfilled liquid cells reject the current constant-density custom model; no automatic existing-save conversion |
| Sparse pressure operator and linear acoustic reference | Analytic/manufactured solutions, large-step pair mode, hydrostatics, incompressible projection and residual checks | Isolated reference components are not a finished production timestep |
| Nonlinear material pressure work | Seven scenarios check volume/temperature/energy/entropy coupling, acoustic limit, liquid force transmission, gravity, reversibility and rejection | Moving material volumes only; no Eulerian remap, shocks, vacuum or phase changes |
| Gas/liquid bulk remap | Common-weight constituent, momentum and energy transport; coupled to pressure work in the separate integrated prototype | Production interface geometry, immiscible separation, vacuum and phase transitions remain missing |
| Owned snapshots and staged fluid messages | 97 snapshot/envelope/router/presentation-runtime checks pass on Windows and WSL Linux, including closed ordered commands, prepared presentation, paired publication, full handles, retries, session-safe worker leases and containers-only presentation scope; real Windows native snapshots copied byte-for-byte | General native message routing remains opt-in and unhooked; copied snapshots do not establish complete gameplay/callback coverage |
| Native-empty gas reservoir bridge | 51 host checks pass on Windows and against Linux declarations. Windows v23/v24 verify two actual reservoirs, conserved transfer/evacuation, exact shared presentation, native meter/logic, mutation/save fences, stale source rejection and orphan retention; v25/v26 additionally exercise live idle eligibility with a containers-only authority | Explicit private activation; no native packets, pipe/world projection, heat/power or integrated persistence. The base lease observes anchors; v26 checks complete footprints separately while holding it. Linux Unity remains unverified. See [bridge scope](owned-reservoir-bridge.md) |
| Inert reservoir markers and copied footprints | 31 Windows/Linux declaration checks and netstandard2.1 builds pass, including exact global saved-ID tracker reference checks. Windows v26 validates both actual 15-cell footprints and ports, local native marker serialization and two exported native-section/Core envelopes; v28 additionally passes exact tracker checks for both actual owners in both idle states | Live markers remain unassigned. No assigned owner restore, full native save or save permission. Raw fixture marker serialization bytes were not archived. See [the scoped game evidence](owned-reservoir-bridge.md) |
| Idle reservoir checkpoint codec | 19 pure checks pass on Windows and WSL Linux, including canonical Core bytes, exact full handles/owner coverage, metadata, empty rows and hostile input | Data contract only; declared idle state is not live native eligibility and enables no save/load hook |
| Selected-load rejection boundary | 72 Windows/Linux host/declaration checks cover bounded headers, actual-reader identity, held fatal attempts, continuation fences, exact-owner view retirement and typed native lifecycle witnesses. Fresh Windows v33 passes ordinary native reservoir save/reload, normal animation, existing-Game direct rejection, early pre-Game rejection and a caught native-load failure; every rejected epoch returns to a stable frontend without Unity errors | Dormant without explicit installation; pinned Windows game/Unity/Harmony hashes. The pre-Game protocol joins exact loader destruction and backend scene unload before clearing only that dead owner reference. No owned restore or save permission; no Linux Unity acceptance. Earlier failed runs remain preserved. See [game evidence](owned-reservoir-bridge.md) and [host scope](../tools/OwnedReservoirLoadTests/README.md) |
| Saved-template, directory/body-prefix and KSAV inspection | Windows and Linux pass 63 synthetic host groups and 64 each with the exact and derived Windows-v38 capture; complete summaries match and .NET Standard 2.1 builds pass. Count walks derive the directory end and KSAV start without observed endpoints. Fresh Windows-v43 runs the newer count-derived directory, strict inflation and body-prefix/KSAV chain in actual ONI Mono after ordinary native save/menu/reload, matching native boundaries and packet identities | Unhooked immutable byte inspection. Production outer file/header/directory/inflation association, arbitrary custom descendants, native-reader authority, owned restore and writer permission remain unimplemented. PrimaryElement semantics stay opaque. Earlier failed candidates remain preserved. See [scope and fixtures](../tools/OwnedReservoirSavedInspectionTests/README.md) |
| Bounded compressed-fragment inspection | The integrated default host passes 64 groups on Windows/Linux with identical summaries and zero-warning/error host/.NET Standard builds. Earlier private hosts pass 66 groups including two captures. Windows-v43 passes 57 actual Mono groups and recovers the independently observed native body byte-for-byte; normal ONI-project compilation succeeds | Original structural validation plus platform raw DeflateStream, exact output/Adler/EOF and resource caps; no codec source copied. Fragment compatibility only, without production whole-file/native validity, restore or writer authority. Earlier codec, harness and verification-control failures remain preserved. See [runtime evidence and limits](owned-reservoir-bridge.md#bounded-compressed-fragment-inspection) |
| Complete-source layout inspection | Integrated helpers derive header, directory, compressed body, prefix and KSAV from byte zero. Public Windows/Linux hosts pass all 74 original groups and complete typed reports; host/contract builds have zero warnings/errors, and the original 72-group regression passes. Windows-v44 also passes those 74 in actual ONI Mono and matches the independently observed native body | Opaque Sim/custom-component/tail bytes do not establish native validity or owned restore. Earlier wrapper and review-control failures remain preserved. See [scope and evidence](owned-reservoir-bridge.md#complete-source-layout-inspection) |
| Complete-file staging and promotion | 40 pure managed checks pass on Windows NTFS and isolated WSL Linux, with one platform skip each, including immutable bytes, flush/readback, actual IO errors and injected failures | Unhooked helper only. Windows replacement exposes temporary missing-name/open errors; continuous pathname visibility and previous-target recovery gates remain false. No native save permission or crash-durability claim. See [the tested contract](../tools/OwnedReservoirFilePromotionTests/README.md) |
| Recoverable complete-file promotion experiment | 49 host checks pass on Windows NTFS and isolated Linux ext-family storage, with one platform skip each. Windows v28 passes nine normal-source checks in actual ONI Mono 6.13, including native rename/Unicode/readers and retained old images/intents | Separate helper, excluded from normal ONI compilation and default build. V27's longer paths fail before rename; v28 bounds private artifact paths to 239 UTF16 characters. Integrated saves, product path eligibility and power-loss durability remain unverified. Matching target bytes never settle an unknown result. See [protocol and backend scope](../tools/OwnedReservoirRecoverableFileTests/README.md) |
| Shared mixture presentation | 27 scenarios pass on Windows and WSL Linux, including owned containers from one completed snapshot, assessments, tiny/zero-limit traces, immutable frames, thermodynamic parity and lazy thread-safe inspection | Read-only projection; gameplay respiration and a production material catalogue remain missing |
| Supplied thermodynamic readings | Separate immutable input retains stored U, resolved K, canonical molar masses, actual temperature/pressure and compressed phase volumes; fourteen focused controls and the current forty-one-group presentation regression pass on Windows/Linux, including trace arithmetic and warning consistency | The standalone API remains read-only. A closed-reference physical frame now binds it as described above; production thermodynamic world/container ownership and persistence remain missing. See [reading contract](mixture-presentation.md) |
| Cell/container inspection | 27 licensed-declaration checks pass on Windows and WSL Linux; Windows v17 selects six real native objects bound to full owned handles, transfers oxygen, reloads a custom checkpoint, and verifies retired/replacement bindings with 25 Unity captures | Conserved read-only fixtures; native packet flow, equipped suits and production machine transactions remain unproved; Linux declaration tests do not draw Unity UI |
| Complete tile-hover contents | 29 Windows inspection groups pass. Windows V54 passes four native hover/pixel cases: all four non-trace gases, exact/below-cutoff behavior, hazardous-trace warnings and 125% UI scale. All four receipts bind to their completed readings and are accepted | Immutable display fixtures with native ONI physics; Linux four-gas hover, production owner and first-playable acceptance remain missing. V52's deadline failure and V53's sharing violation remain retained. See [evidence and limits](mixture-presentation.md#verification-and-limits) |
| Native nitrogen phases | Namespaced gas/liquid/solid YAML and cloned native substances; six Windows/Linux registration checks and Windows v15/v17 item inspection plus native save/menu/resume | Fixed native phase temperatures and explicit constant-property surrogates; not a complete pressure-dependent material model or world generation |
| World appearance and overlays | Windows V48 passes all 34 original captures plus three actual hover/table captures, four labelled overlays, appearance controls and 18 GPU readbacks from the bound colour/pressure/danger textures. Native scrolling exposes the complete liquid constituent row at the inspected publication | 504 explicit fixture cells in a mostly paused colony; sampled texture bytes do not establish every pixel or shader result. Native liquid ownership, Linux Unity and sustained game-scale performance remain unverified. See [presentation evidence](mixture-presentation.md) |
| Windows compilation | Reviewed full build passes seven native CTests, Core 88, presentation 27, checkpoint 19, file-promotion 39 with two explicit optional/platform skips, owned-runtime 97, reservoir 51, inspection 27, native-description 5 and nitrogen-registration 6 checks. Later targeted marker 31 and game assembly builds pass | Two known Unity dependency warnings; the earlier full build predates the new marker checks and experimental remap. V23 through V28 retain their exact earlier native DLL and game-package hashes |
| Actual ONI references | Compiled against licensed build 744825 | Two Unity dependency-version warnings |
| ONI probe | Actual Windows and Linux Unity reports passed all six checks | Scoped loading/interception only; no colony simulation replacement |

The earlier Linux Release, Linux ASan/UBSan and Windows MSVC runs passed all 56
native scenarios, including the seven nonlinear material-pressure checks.
The current GNU Linux Release and Windows MSVC build pass all 57 plus the ten integrated-fluid
cases. The earlier Windows native56 build is stored separately from the frozen
game-tested packages. The current
82-scenario managed/native suite also passes against the pinned GNU
Linux library. Test binaries resolve that library through the local validation
environment. Test-host resolvers explicitly pin the freshly built native
library and print its SHA256 before correctness tests, preventing stale files
beside test executables from overriding the selected build. Licensed game assemblies stay in the installation or ignored
private evidence.
The frozen `MixedAtmospheresProbe-windows-json49` and corresponding Linux
package passed inside their actual Unity runtimes against the 49-scenario
native builds. Windows DLL SHA256:
`1677c0fce99e096a44cfa1ebdf86f790a2999cbbbf38e13f9389f76c49ff0c1a`.
The probe verifies the inspected managed assembly hash and layouts before
installing any patches.

## Current integration evidence

Earlier Windows main-menu tests used the user's licensed Steam installation
and restored the original settings. Their private evidence remains under
`/mnt/data/share/oni/mod-development/probe-evidence`. These historical scoped
probe runs did not load a colony; the later disposable-colony tests below do.

The expanded verified Unity report records 55 redirected native-message methods,
successful native mixture roundtrip, frame/snapshot/direct-message interception,
save interception, and a settings serialization roundtrip. It reports the expected Cell16, Frame28 and Snapshot496
byte layouts. `AuthoritativeColonySimulationReady` remains false.

The strengthened probe redirects direct native message call sites in `Sim`,
`SimMessages`, `DebugHandler` and `SubworldZoneRenderData` while its own scope
is active. The latter two callers were found by the assembly-wide metadata
audit and verified in the second runtime pass. It also exercises
the managed save entry point into an in-memory buffer. Unsupported scoped
messages/batches fail explicitly. Conduit services, complete callbacks,
third-party call sites, and real colony save/load remain outside its coverage.
The probe checks patch installation before calling methods that would otherwise
reach ONI's native singleton. A capture-only scope exports private element and
disease messages for the isolated native harness, without sending those
messages to the game's simulator. Current ownership research and its explicit
remaining proof obligations are in [the native contract](interop-contract.md).

A Windows new-colony run staged nine generated worlds with 50,466 fluid cells
and published 4,127 owned snapshots while native ONI continued to simulate the
colony. All copied channels were byte-identical: 47 pointer channels and 29
event channels, up to 12,830,560 owned bytes per snapshot. Its overall validation
**failed** because the custom import rejected native liquid volume/pressure
states unsupported by the surrogate constant-density catalogue. The staging
and copy results therefore do not imply an authoritative custom-state roundtrip.
Private evidence: `local/colony-runs/windows-new-world-v4`.

A later read-only diagnostic of that capture uses the explicit nominal
`maxMass`-per-cell liquid reference, rather than spawn mass. It identifies
8,643 issue cells across 103 same-world pore components; 27 components cannot
fit that declared reference, with a minimum aggregate deficit of 11.5257 m³.
Hashes and inventories remain unchanged and publication still rejects. This is
a necessary reference-volume bound, not a physical equation of state or proof
that an aggregate-capacity check permits conservative placement. Private
evidence: `local/colony-placement-v4-diagnostics.json`.

A fresh Windows snapshot-copy v5 run verifies the nominal v2 placement
diagnostic inside ONI before `PublishValidated`. It stages nine worlds with
50,368 fluid cells and 16,205,286.3033 kg, then rejects the first unresolved
compressed-liquid cell (world 0, global cell 2,952). The diagnostic records
8,625 issue cells in 104 same-world pore components; 28 cannot fit the declared
reference volume, with a minimum deficit of 11.8676 m³. This supersedes the
v4-capture-only diagnostic as in-game integration evidence while retaining
both runs. The catalogue fingerprint is
`b31f26d8fc10a2894057ecbe7eb6f26012be898bb068485dce6bf146ca5afc66`.
Native ONI continues to own physics, and no custom-fluid state is published or
source world blob changed. Its 130 owned native snapshot copies across 127
acknowledged frames are byte-identical over 47 pointer/29 event channels, up
to 12,831,032 bytes and 9,477 events per snapshot. The overall result remains
**failed** because custom-state publication/roundtrip is unsupported. Private
evidence: `local/colony-runs/snapshot-windows-v5`.

The frozen Windows presentation-v7 fixture report records twelve actual Unity
screenshots, 504 explicitly registered test cells and six registered fixture
inventory surfaces on actual game prefabs: pipe, reservoir, bottle, suit, vent
and machine. These are conserved test inventories, not spawned production
machines performing custom fluid transactions. V7 additionally records 1,353
actual trace-hover draws and computer-use observations of the chlorine warning,
scrolling through all three full constituent rows, all four overlay icons,
the native F1 legend, and persisted colour-only/strong-contrast callbacks.
Live native `DetailLabel` checks establish warning-row order. The earlier v6
run's zero hover draws and harness-driven controls remain historical evidence;
the failed v8/v9 container-helper runs remain recorded separately.
`FirstPlayableColonyPassed` remains false. Private evidence:
`local/colony-runs/presentation-windows-v7`.

Windows v10 now completes the full private presentation fixture sequence with
18 Unity PNGs: twelve world/cell/overlay/appearance views and six actual spawned,
selected native objects (pipe, reservoir, bottle, suit, vent and machine).
Each live selection report confirms the correct target and panel, complete
labelled fixture composition, warnings before constituents, owned inspection
before the native description, preserved native description, and no errors.
All six container PNGs were also visually inspected across the review. All six
inventories are separate, conserved read-only fixtures: this does not fill a
native network, equip a duplicant or exercise production machine transactions.
The fixture report is passed while `FirstPlayableColonyPassed` remains false.
The archived v10 run also passes native Save And Quit/main menu/same-save resume
in one process. Five teardown observations retire the remaining fixture
world/model/frame without errors; container adapters had already been removed
by their fixture cleanup. Actual post-resume captures record no world provider,
zero inventory providers and zero inspection bindings. The composition legend
explicitly says `No mixture readings available` and native F1 shows its original
oxygen categories; fixture labels are absent. Evidence, PNG dimensions and
binary/file hashes are in `local/colony-runs/presentation-windows-v10`, including
`manual-ui-observations.json` and `presentation-review.json`. The runner exits
successfully and restores the user's settings. Native save/resume is separate
from the still-missing coherent full-owned colony save/load gate.

Windows v11 re-verifies the packed assembly through eighteen automatic PNGs
and all six spawned selected-container checks. All live warnings/composition,
selection and native-description assertions pass; paused-hazard and machine
PNGs were visually reviewed. The actual two shutdown observations clear the
remaining provider/model/frame and bindings without errors. Package hashes
match their manifest and the Core matches the current Windows build. Its
`presentation-review.json` is under
`local/colony-runs/presentation-windows-v11`. V11 performs no interactive or
same-process resume acceptance; those observations remain separate V10 evidence.

Presentation teardown now clears providers, the model, cached/worker frames,
texture scheduling and persistent-prefab inspection bindings before native
shutdown or game instance destruction. Texture recreation alone preserves
providers. Linux declaration tests cover retirement, closure collection and
old-lease rejection; an opt-in private observer records actual shutdown state.
Archived v9 evidence confirms Save And Quit to the main menu and same-process
resume of the same disposable native save. Five teardown observations pass:
the first clears the live model/world/frame and six inventory providers, all
bindings clear, and subsequent shutdown/destroy calls are idempotent without
errors. Manual native UI observations confirm normal gas without fixture
labels, ordinary pipe details without fixture rows, and the native F1 legend.
Evidence is in `local/colony-runs/presentation-windows-v9`, including
`presentation-shutdown.json` under the archived mod, plus
`manual-lifecycle-observations.json` and `lifecycle-review.json` at run root.
The overall v9 fixture status remains **failed** at the spawned-pipe
native-description comparison. This lifecycle pass does not establish custom
colony persistence, production container transactions or long-run memory
behavior. V10 subsequently verifies the no-provider custom-overlay legend as
described above.
Custom overlay
status visibility is explicitly mapped to native oxygen visibility, and the
native F1 mixture legend is scoped to owned cells.

The latest isolated WSL managed run checks current presentation naming and
inspection against the actual Linux game declarations. Its result and exact
source hashes are recorded in `local/linux-presentation/result.json` and
`managed-refresh-manifest.json`. The GNU library was rebuilt for the hazard
fix, all native and managed suites rerun, and every current native input hash
checked. Earlier GNU evidence is retained under `history-pre-trace-fix` with
its original source/binary hashes. This is Linux execution of the model and adapters, not a Linux
Unity presentation run. WSL has no authenticated Linux Steam session, default
network route or DNS configuration. An invisible EGL context reports Mesa
llvmpipe after a DRI3 device error. Linux screenshots and accelerated game
performance remain unverified.

The current full ONI adapter compiles against the Linux declarations with no
errors and the same two Unity dependency-version warnings. Its 43 runtime,
25 inspection and five native-description checks pass. A separate replay of
V9's recorded Windows pipe description passes the corrected comparison against
native text formatting; V9's archived overall failure remains unchanged. The
updated owner-warning priority passes the live Windows v10 six-container
assertions, and post-resume screenshots confirm the no-provider legend/context;
none of this establishes Linux Unity.

## Isolated native ownership experiments

The real Windows SimDLL ran in a separate process with a disposable 64×64
world and the actual exported element/disease tables. Native flow and solid
heat conduction worked. ModifyCell followed by snapshot requests, without a
frame, did **not** clear native fluids; that activation path is rejected.

Two alternatives passed narrow tests. Rebuilding a fresh native world with
fluids captured separately preserved nonfluid cell data and kept native fluid
cells at zero through twelve frames. The preferred path filters the native
v15 load blob before sending it to the original loader. It captured 2 kg of
oxygen and 100 kg of water, preserving temperature, species and coordinates.
The resulting native world contained no fluids before any frame and stayed
fluid-free for twelve frames while solid heat conduction continued. Its
nonfluid cells exactly matched an unmodified native-load control.

A positive-control ice cell melted with normal definitions; suppressing
fluid-producing element definitions kept it solid. This covers one transition
branch, not every source. The native message queue is asynchronous, so the
harness polls snapshots until a submitted frame is acknowledged.

Evidence: `/mnt/data/share/oni/mod-development/shadow-evidence-v4`, `-v5` and
`-v7`. The shared import parser is in `NativeWorldBlob.cs`; the native process
harness is under `tools/NativeShadowProbe`. Those isolated tests did not open a
colony or certify live Grid projection, all callbacks, complete native source
suppression, liquid transport or release readiness.

The matching Linux base depot was downloaded through the authenticated Windows
Steam client into separate depot storage, then copied to
`/mnt/data/share/oni/OxygenNotIncluded-linux`. Its inspected native library has
SHA256 `85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d`.
All five positive isolated-native scenarios also passed on Linux, using the
Windows-exported element/disease tables. The negative sanitation control
reproduced the same two fluid cells and 102 kg before any frame. Initial native
saves were byte-identical between platforms. Linux evidence and the six-result
suite summary are in `shadow-evidence-linux-v2`.

Actual Linux Unity subsequently loaded the mod and passed the same six scoped
checks under Xvfb/Mesa. Its first run exposed a JSON compatibility bug: the
depot omits the Mono machine configuration required by DataContract JSON.
Reports now use the game's existing Newtonsoft assembly by reference only;
standalone settings use a strict fixed-schema codec with locale and malformed
input checks. Evidence is in `probe-evidence/linux-unity-json49`. Linux exited
before the main menu with no local Steam session, so it did not export its own
element tables. Windows main-menu validation of the same fix also passed;
the user's settings were restored exactly and the temporary mod was archived
outside the mods tree.

## Measured performance

The reference benchmark uses 400 × 250 cells, advances 0.001 simulated seconds,
and reports its own timing and conservation errors. An initial local Release
run on the Ryzen 9 5900X took 425.3508 ms and two acoustic substeps. This is not
close to the proposed 25 ms budget per 0.1 simulated seconds for 100,000 active
cells. The benchmark is deliberately retained as a failing baseline; no
real-time claim should be made from the small test scenarios.

The separate integrated wet-air prototype advances 100,000 cells by 0.1
simulated seconds in approximately 428 ms in the recorded Windows Release
sample and 326.5 ms in the current GNU WSL sample. Both fail the 25 ms target; neither
is a production solver. See [the solver limits](../native/SOLVER.md).

The newer packed-storage presentation revision keeps ordinal immutable mass
maps behind the same API and removes hot-loop enumerator allocations. Current
Linux validation passes Core 54/presentation 20/runtime 43/inspection 25/native
description 5, with a successful full adapter compile. Sampling and patterned
colour loops still allocate zero bytes. Cold/all-changed capture allocation
falls from 58.69 MB to 46.69 MB, but single-sample timings vary: Windows/Linux
cold capture is 76.5/165.0 ms and all-changed capture is 109.6/97.3 ms, without
an all-around speedup. Two-changed-cell capture is 17.5/15.4 ms.

Later private Combine and ApplyHeat copy optimizations remain rejected or
unintegrated. V41 passes its exact correctness and 27,770-record presentation
comparison, but its first timed protected Combine control differs by 15,520 B
and stops before any heat-benefit measurement. No cause or speedup is established;
the live Core remains unchanged. A separate same-baseline pair also fails to
reproduce exact right-only Combine allocation equality; this does not reclassify
V41 or establish a cause. See [the preserved negative results](mixture-presentation.md).

Inventory creation/update costs are measured separately. Initialization plus
capture costs 831.9/1,103.0 ms and 406.4/409.3 MB on Windows/Linux; all-changed
update plus capture costs 181.4/231.9 ms and 80.6 MB. Both samples ran with ONI
closed, exclude native solving/Unity rendering and do not establish sustained
publication, backlog or memory acceptance. Exact hashes and the full table are
in [mixture presentation](mixture-presentation.md). Historical V10 benchmarks,
manifests and artifacts remain under
`local/linux-presentation/history-pre-packed-v10`. V10's screenshots belong to
its frozen pre-packed binary hashes, not this newer assembly; packed-build
Windows Unity acceptance is now separately recorded by V11's automated fixtures.
The paused Windows v7 504-cell fixture measured frame p50/p95 of 8.35/10.38 ms,
including UI transitions and screenshot capture with no baseline subtraction.
It does not measure sustained 1x, 100k custom cells, a publication backlog or
production game overhead.
V10's same scoped sequence records 7,151 texture batches, frame p50/p95/p99 of
8.41/10.21/113.08 ms and a maximum texture batch of 3.56 ms, including UI and
screenshots in a mostly paused native colony. These remain fixture timings,
with no native baseline subtraction or sustained simulation claim.
V11 separately records 7,275 batches, frame p50/p95/p99 of
8.24/9.76/110.74 ms and a 3.14-ms maximum batch under the same limited scope.

## Unhooked frame coordinator checkpoint

The managed router now stages an ordered batch into one caller-owned transaction.
A prepared Core frame reserves the custom writer before exposing native intents
and retains that reservation through native acknowledgement, paired custom/native
snapshot publication, and explicit consumption acknowledgement. Snapshot failure
keeps the old publication and retries the same immutable callback outbox. Native
geometry changes require explicit acknowledgement; blocked displacement rejects
the whole batch. Session/sequence tickets and callback registration generations
reject duplicate publication, stale handles and duplicate consumption. An external
callback that changes an actor and throws remains ambiguous and needs explicit
resolution; it is never automatically replayed.

Frozen Windows source passes Core 58, presentation 20 and owned-snapshot 65
scenarios against the pinned native library and actual licensed ONI declarations.
The private `local/frame-coordinator-windows-evidence.json` records source/library
hashes; `local/frame-coordinator-windows-idle.log` records the final idle protocol
tests. These cover late batch failure, ordered emit/consume, immutable paired
publication, competing writers, geometry rollback, trace/energy/disease/radiation
preservation, snapshot retry, callback retirement and native/callback fault paths.

The recorded idle protocol benchmark averages 5.84 ms and 2.84 MB of managed
allocations per frame over 64 frames, with 1,024 messages and 1,024 callback
records per frame. An earlier concurrent-ONI sample measured 7.16 ms and 2.82 MB.
This is a four-cell synthetic protocol test, excluding
native dispatch, actor execution, fluid solving and colony-scale copying; it does
not satisfy the 25 ms engine gate. One publication and one retained emitter
registration remain after the run. Native memory survives acknowledgement until
the next publication or coordinator disposal; acknowledgement attests old readers
have finished. Native source freshness and exclusive native dispatch remain
caller requirements, since `numFramesProcessed` is not a unique source epoch.

The coordinator is opt-in and unhooked. It adds no production message activation,
actor/container conservation boundary, native source suppression, outbox
persistence or full colony save/load integration. Disposal ends the session and
does not claim native rollback or crash recovery. First playable and authoritative
custom-colony readiness remain false.

## Native nitrogen registration and Windows inspection

Three namespaced nitrogen phases now load from original mod YAML, with native
hash/tag/prefab identities, localized names/descriptions, phase links, isolated
gradients and distinct world/UI/conduit colours. The mixture palette reads the
registered gas colour. Native enum values retain their existing tag path;
explicit description keys avoid the installed loader's Turkish-casing bug.
See [nitrogen property scope and validation](nitrogen.md).

Windows private run `presentation-windows-v15` passed 21 automatic Unity
screenshots: the existing 18 mixture/container views plus three selected native
nitrogen items. All six owned mixture fixture surfaces and all three native
phase item inspections passed. Native Save/Quit/MainMenu/Resume then preserved
the three visible item identities, 1000 g masses, consistent displayed
temperatures and descriptions. Four post-resume Unity screenshots retain
those panels and the restored native F1 overlay, with zero mixture providers
and bindings. This proves native pure-material item inspection after resume;
it does not prove mixture persistence or exact post-load binary scalar equality.
Five teardown observations passed and runner settings restored byte for byte.
The six standalone registration checks and Windows actual-game replay passed
on both Windows and Linux. Independent review/hash manifests remain in
`local/colony-runs/presentation-windows-v15/presentation-review.json` and
`local/linux-presentation/nitrogen-v15/result.json`. The run's explicit log
records the matching save/load; its stale ambient `Player-final.log` is excluded.
Future runner invocations skip an ambient log older than their launch.

This small, mostly paused run sampled 8,826 frames (p50 8.30 ms, p95 9.97 ms,
p99 110.08 ms), including screenshots/UI transitions. Maximum measured texture
batch wall time was 142.42 ms. It includes 504 explicit mixture fixture cells,
up to six registered mixture inventories and three native nitrogen items;
it is not a sustained 1x or 100k-cell performance result. Nitrogen resources,
progression, conservative mixture phase changes and Linux Unity acceptance
remain unimplemented/unverified. First playable remains false.

The separate Windows V46 intentional-OnLoad-failure diagnostic remains
**rejected**. Five actual callbacks observe removal of the main Harmony owner,
survival of the foreign witness, reset renderer/marker observers, and exclusion
of all three nitrogen phases despite their definitions appearing in the actual
YAML collection. These are scoped teardown observations before any colony;
populated render-state cleanup is untested. ONI then exits before a menu
screenshot or manual quit. The initiating caller and OS exit code remain
unknown; the later background YAML thread-abort message occurs after shutdown
begins and does not establish the cause. Runner exit 1 is a separate result.
All 283 personal files and 13 settings restore exactly. The preserved negative
and independent review are under
`local/colony-runs/failed-onload-windows-v46` and
`local/failed-onload-windows-v46-managed-completed-negative-review`; review
SHA256 is `6b575db721d51c621949935255693b0588a580688764b0aaedfd54d070c37ba7`.
This supplies no normal-closure, physics or first-playable acceptance.

A separately instrumented Windows V49 run records `App.QuitCode(1)` from
`ReportErrorDialog.OnDestroy`, followed by unsigned Windows process exit 1
on the same authenticated retained handle. The request precedes the final
menu witness, so V49's fixed automatic-quit diagnostic also remains
**rejected**. All five teardown witnesses eventually complete, and all 283
personal files plus 13 settings/backups restore exactly. No screenshot or
manual quit is claimed. A bounded static read finds that this dialog method
calls `App.QuitCode(1)` when `KCrashReporter.terminateOnError` is true; it does
not establish who destroyed the dialog or configured that policy. These new
scoped observations do not assign a caller retrospectively to V46. Evidence
is `local/colony-runs/failed-onload-quit-windows-v49`, with independent review
`9253cae782841d7d8c7a9fbedf62f653126db5028761975a24831515d4f96b8e`
under `local/failed-onload-quit-windows-v49-completed-negative-review`.

Windows V48 independently closes the merged presentation fixture's actual
hover, expanded-table visibility and sampled GPU texture gaps. All original
34 screenshots and three added inspection captures pass; both manual holds
retain the correct completed snapshot. Normal application quit restores all
283 personal files and all 13 settings/backups. The preceding V47 attempt
remains rejected after its second inspection hold expired. These results do
not establish production container flow, owned mixture save/reload, Linux
rendering or performance. See [the precise scope](mixture-presentation.md).

A separate finite reference-pushforward certificate now passes moving fine
geometry, constant consistent inertia and compatible material/pressure work
identities. It uses an artificial potential and manufactured loads. A new
private actual-EOS consistent-mass prototype builds cleanly on Windows, but
its first physical run rejects C7 at the canonical-impulse representation
check after completing C0–C6. The remaining positive cases and all twelve
failure controls were not reached; the complete physical gate remains
unaccepted. The existing remap, vacuum-front and performance failures are
unchanged. See
[the solver experiment evidence](../native/STAGGERED_EXPERIMENTS.md).

## Remaining release gates, in dependency order

1. **Authoritative integration.** Audit and redirect every native mutation,
   snapshot, callback, save, and conduit service. Prove lifetime/ownership of
   native arrays and texture buffers, and prove that a disposable new colony
   has exactly one owner for every simulated quantity. Resolve real generated
   liquid imports without deleting excess mass or inventing headspace. Native
   snapshot-copy and fixture-rendering success are insufficient.
2. **Production fluid solver.** Implement and validate the efficient all-Mach
   coupling of pressure, conservative transport and thermodynamics, plus
   production liquid transport, miscibility/immiscibility, vacuum, free
   surfaces, displacement, and grid-coupled phase transitions. Preserve mass,
   momentum, energy and trace constituents across all paths.
3. **Complete thermodynamic catalogue.** Cover every supported game material,
   including documented fictional surrogates. Add valid-range property tables,
   entropy/free-energy constraints, and all machine/biological reaction audits.
4. **All fluid users.** Replace pumps, filters, vents, pipes, reservoirs, suits,
   rockets, consumers, emitters, generators, plants, critters, geysers, waste,
   and fluid-related disease/radiation/thermal behavior. No silent vanilla
   conversion path may remain.
5. **Playable colony.** Finish nitrogen starting resources and starter
   supplies, ventilation/scrubbing, sensors, research and recipes. Connect the
   implemented read-only presentation to completed authoritative state; finish
   Windows and Linux UI/screenshot and sustained performance acceptance. Apply
   configurable breathing rules to actual duplicants and suitable equipment,
   then prove the ten-cycle development-colony gate.
6. **Colony persistence and compatibility.** Save all mixture state; reject
   unsupported versions and incompatible mods before load. Validate restart,
   autosave, multi-asteroid transfer, and changed display settings.
7. **Release validation.** Run representative long colonies for base game,
   Spaced Out! and every supported official gameplay pack on Windows and Linux;
   measure CPU time, allocations, memory, conservation and stability. Review
   gameplay progression and all release
   gates before creating a Workshop package.

There is no release-packaging option. `tools/package_probe.py` produces only an
explicit integration probe and writes `release_ready: false` in its manifest.
The Steam Community draft describes the intended project and its current
development status; it is not a release announcement.
