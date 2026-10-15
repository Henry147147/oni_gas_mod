# Implementation status

This is a private development milestone. The requested overhaul is incomplete.
The first public release remains gated on complete gas **and** liquid gameplay.

The accepted [completion plan](completion-plan.md) remains the execution target.
Current work adds managed colony transactions/checkpoints, owned native
snapshot copies, a staged fluid-message router, an integrated gas/liquid solver
prototype, and shared mixture presentation. Actual Windows disposable colonies
exercise snapshot copying, presentation fixtures and two explicitly owned
native-empty reservoirs. Native ONI retains the surrounding world physics;
no first-playable or release gate is marked complete.
The 3,042-entry [ownership inventory](ownership-coverage.md) records known work
and remaining acceptance obligations across the supported packs and platforms.

## Implemented and exercised

| Area | Evidence | Limits |
| --- | --- | --- |
| Native mixture/thermodynamic reference core | 57/57 current GNU Linux scenarios, including tiny/zero-limit trace hazards; earlier 56-scenario Windows/Linux and Linux ASan/UBSan evidence is retained | Constant properties; incomplete material coverage |
| Gas finite-volume baseline | Hydrostatic, buoyancy, diffusion, pressure, vacuum and conservation tests | Gas only; explicit acoustic performance fails target |
| Integrated gas/liquid prototype | 10 closed-mode cases plus 24 occupied-1D groups and bounded gas/vacuum consistency checks pass MSVC, strict GNU and ASan/UBSan; independent thermodynamic reference passes 26 groups | Full vacuum-front convergence fails: N1024 recent speed is 5.874c versus exact 5.019c, and N2048 worsens front position. Dormant outside the C ABI/runtime; 2-D, cavitation, immiscible interfaces and phase transitions remain unproved. See [the failed gate and next design](../native/GAS_VACUUM.md) |
| Native and managed persistence | Native roundtrip/continuation checks, managed colony checkpoints, and a checksummed native/custom save envelope | Envelope is not integrated with actual manual saves, autosaves, restart or cross-platform colony loading |
| Managed API, settings, native bootstrap, import and colony state | 88 scenarios pass on Windows and isolated WSL Linux, including owned containers, full allocation handles, staged-write failure atomicity, packed mass storage, traces, geometry, v3 checkpoints and exact-publication reservations | Tested transaction helpers do not route all gameplay actions; Core reservation does not pin caller-owned native resources |
| Pump/separator reference processes | Enthalpy, shaft-work, entropy and trace-separation tests | No game power/storage/heat-sink transactions |
| Implicit heat exchange | Analytic pair solution, stiff contacts, energy/entropy and insulation checks | Not yet connected to native solid/building transfers |
| Native v15 fluid importer | All-world staging preserves fluid species, mass, temperature, radiation and disease before native Load | Real generated compressed/overfilled liquid cells reject the current constant-density custom model; no automatic existing-save conversion |
| Sparse pressure operator and linear acoustic reference | Analytic/manufactured solutions, large-step pair mode, hydrostatics, incompressible projection and residual checks | Isolated reference components are not a finished production timestep |
| Nonlinear material pressure work | Seven scenarios check volume/temperature/energy/entropy coupling, acoustic limit, liquid force transmission, gravity, reversibility and rejection | Moving material volumes only; no Eulerian remap, shocks, vacuum or phase changes |
| Gas/liquid bulk remap | Common-weight constituent, momentum and energy transport; coupled to pressure work in the separate integrated prototype | Production interface geometry, immiscible separation, vacuum and phase transitions remain missing |
| Owned snapshots and staged fluid messages | 94 snapshot/envelope/router/presentation-runtime checks pass on Windows and WSL Linux, including closed ordered commands, prepared presentation, paired publication, full handles, retries and session-safe worker leases; real Windows native snapshots copied byte-for-byte | General native message routing remains opt-in and unhooked; copied snapshots do not establish complete gameplay/callback coverage |
| Native-empty gas reservoir bridge | 34 host checks pass on Windows and against Linux declarations. Windows v23/v24 verify two actual reservoirs, conserved transfer/evacuation, exact shared presentation, native meter/logic, mutation/save fences, stale source rejection and orphan retention; V24 captures owned vacuum before unbinding | Explicit private activation; no native packets, pipe/world projection, heat/power or integrated persistence. Linux Unity remains unverified. See [bridge scope](owned-reservoir-bridge.md) |
| Idle reservoir checkpoint codec | 19 pure checks pass on Windows and WSL Linux, including canonical Core bytes, exact full handles/owner coverage, metadata, empty rows and hostile input | Data contract only; declared idle state is not live native eligibility and enables no save/load hook |
| Shared mixture presentation | 27 scenarios pass on Windows and WSL Linux, including owned containers from one completed snapshot, assessments, tiny/zero-limit traces, immutable frames, thermodynamic parity and lazy thread-safe inspection | Read-only projection; gameplay respiration and a production material catalogue remain missing |
| Cell/container inspection | 27 licensed-declaration checks pass on Windows and WSL Linux; Windows v17 selects six real native objects bound to full owned handles, transfers oxygen, reloads a custom checkpoint, and verifies retired/replacement bindings with 25 Unity captures | Conserved read-only fixtures; native packet flow, equipped suits and production machine transactions remain unproved; Linux declaration tests do not draw Unity UI |
| Native nitrogen phases | Namespaced gas/liquid/solid YAML and cloned native substances; six Windows/Linux registration checks and Windows v15/v17 item inspection plus native save/menu/resume | Fixed native phase temperatures and explicit constant-property surrogates; not a complete pressure-dependent material model or world generation |
| World appearance and overlays | Windows v10 completes 18 Unity captures plus six interactive captures, six spawned selected-container panels and same-save menu/resume with explicit no-provider composition legend/native F1 restoration; v7 also verifies appearance callbacks | 504 explicit fixture cells in a native colony; Linux Unity and sustained game-scale performance remain unverified |
| Windows compilation | Reviewed full build passes six native CTests, Core 88, presentation 27, checkpoint 19, owned-runtime 94, reservoir 34, inspection 27, native-description 5 and nitrogen-registration 6 checks | Two known Unity dependency warnings; this build alone does not run Unity. V23/V24 retain their exact earlier native DLL and game-package hashes |
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
