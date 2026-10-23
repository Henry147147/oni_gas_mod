# Mixed gas and liquid pipe contents

The playtest requires an actual composition in each gas/liquid conduit cell,
selective filtering, recipe-specific consumption, conserved heat and germs,
and exact save continuation. Ordinary colonies still send mixture
constituents as separate native packets. Explicit harness controls validate
internal mixing/filtering/readers, precise world receipts and real mixed pump
storage/output. Full production ports and the mixed save writer remain off;
thermal, all-germ biology, remaining component ports and teardown/save
acceptance are still open.

## Conserved inventory

`ColonyConduitInventory.cs` keeps ordered constituent masses, internal energy
and every disease identity. Exact floating-point expansions retain small
components when a larger quantity is added. Internal moves subtract and add
the same shared flux; no endpoint mass, heat or passive is clipped to close
an account. In the explicit controls, the native scalar packet is a
compatibility projection; owned composition remains authoritative.

The focused `tools/ColonyConduitTests` control checks two materials in one
cell, different heat capacities/formation energies, selective filtering with
blocked output capacity, 1,000 repeated transfers, heat, passives, evacuation,
retention of a 1e-240 kg component and byte-exact cold continuation. Native
scalar dispatch controls retain mass below float resolution, energy that a
float temperature cannot represent, and fractional/multiple germ identities.
The precise-receipt and fresh-pump controls below validate live storage and
boundary settlement. They do not establish full production port/save/thermal/
biological integration or complete end-to-end game conservation.

Initial model measurement at `56b35ea` (Release, high CPU lock, 2026-10-23): median
0.176 us/transfer and 752 allocated bytes with one species; 0.368 us and
1,584 bytes with two species. Each uses five repeats of 20,000 alternating
moves after 50,000 warm-up moves. This measures the pure inventory only;
allocation remains an optimization target after the live correctness checks.
Run with `dotnet run --project tools/ColonyConduitTests -c Release -- --benchmark`.

For the new runtime, `PipeMaterial.Id` and constituent IDs are native element
ordinals in the saved catalogue. Compatibility projections map them back to
`SimHashes`; a negative element hash must never be cast to or confused with an
ordinal. The existing catalogue fingerprint covers both `NativeIndex` and
`ElementHash`. Disease IDs inside inventories are native zero-based disease
indices; world/job passive identities are one-based. Job radius is at most
64, an injection reserves exactly its requested native constituent, and each
supplied passive is bounded by the exact per-ID durable reservation.

Schemas 1/2 were dormant groundwork: no live mixed-pipe save or writer has
been enabled. Their encoded numeric bytes remain unchanged. Any previously
activated image with an ambiguous identity convention must be refused, never
reinterpreted. The eventual live schema will tag the identity convention
alongside durable port buffers and physical scheduling state.

## Saved framing

Format IDs **MACOLP02** and **MACPIP01** were claimed immediately after a
fresh fetch/rebase of `b36ed23` on 2026-10-23. These are managed payloads;
they do not change a native MAC checkpoint version or its bytes.

- **MACOLP01:** unchanged magic, int32 configuration length, 56-byte
  configuration, then the existing owner image. An absent pipe image keeps
  exactly these bytes and the existing packet behavior.
- **MACOLP02:** magic, int32 configuration length, 56-byte configuration,
  int32 owner-image length, owner image, int32 pipe-image length, pipe image.
  The existing `MACOLONY` save envelope hashes the entire payload.
- **MACPIP01, version 1:** magic, int32 version, six uint32 geometry fields
  (global width/height, world x/y/width/height), SHA256 catalogue identity,
  ordered pipe cells, initial/supplied/removed inventories, external heat and
  disease exchange accounts, followed by SHA256 over the complete preceding
  image. A pipe cell records its gas/liquid phase, global cell index, ordered
  masses, U and germs. Each conserved amount stores its exact ordered binary64
  atoms. Counts, lengths, phase, geometry, identities and canonical ordering
  are checked before publication; the image is limited to 256 MiB.

The runtime writer continues to emit MACOLP01. Loading MACOLP02 is refused
explicitly until end-to-end mixed ownership is ready. No old save migrates
because an environment variable or a new default changes. The codec controls
check exact legacy bytes, new-image re-encoding/continuation, incompatible
catalogue/geometry, malformed lengths, duplicate cells, corruption and the
outer envelope's coverage of the pipe payload.

**MACPIP01 schema 2** was claimed after fresh fetch/fast-forward
`2129f2b` on 2026-10-23. Schema 1 remains readable and re-encodes with its
original bytes. Schema 2 appends named unreleased-boundary-heat and exact
uint64-identity passive representation-remainder accounts,
four exact mechanical exchange accounts (K, gravitational energy, x/y
momentum), next local-job/native-token watermarks and ordered durable port
jobs. Mechanical accounts use sparse balanced signed base-2^32 digits with
arbitrary-precision decimal bin indices; huge exponents and small residuals
survive exact cancellation and checkpointing.

A job stores its local identity, native token, pipe cell/phase, actual world
request cell, native species, operation/radius, requested mass/temperature,
reserved inventory and every uint64-identity/binary64 passive. Token zero
means prepared and unsubmitted; a positive token means already queued in
the native checkpoint. Only injections (1), gas extractions (3) and liquid
extractions (4) are accepted. Jobs, watermarks, phases, geometry, reservations,
passive ordering and byte/count bounds are validated. Committed outcomes must
be credited before both save images are captured; receipt sequence numbers
are transient and are never restored as credit identities. This codec does
not yet authorize live output, saving or migration.

## Explicit internal-flow fixture

The `mixed-pipes` and `mixed-pipe-filter` harness scenarios activate internal
ownership; separate catalogue, boundary and pump controls are described below. It
reuses ONI's conduit graph, native branch scheduling, bridge/valve rate
decisions and callbacks while moving actual owned mixtures. External scalar
inputs/removals/dumps, cross-world scalar edges and saving are gated while
the boundary/job integrations are unfinished. Ordinary colonies and old saves
retain their existing packet behavior.

The first fixture failed its branching check because both branches ended
without consumers; each bridge's complete mixture remained in one output
cell. The corrected fixture keeps the same requirement of at least two
downstream mixed cells per phase, adds real consuming bridge endpoints and
supplies several packets. On the immutable `pipes-core-a643-v2` bundle
(native precise sidecar `529744c`), all five checks pass after 26 frames:
one-cell gas/liquid composition, bridge/native branching and exact pipe
mass/U/every-germ accounts. Gas reaches two mixed cells (2 kg), liquid three
(30 kg); supplied/held mass is 32 kg, removed mass and U residual are zero.
This fixture exchanges no heat and does not prove live conduction, phase
change, filters, machines, external ports or save continuation.

LOW-lock diagnostic timings with concurrent HIGH work allowed: owner
mean/p95/max 171.766/208.443/233.359 ms (26 samples); observed report-completion
spacing 328.146/287.490/3019.882 ms (25 contiguous samples, 10 ms polling,
including arena/build work). These are fixture diagnostics, not performance
acceptance. World mass/energy errors are 1.126e-16/5.552e-17, trace sink is
0 kg/0 J, no owner fault occurred, and process cleanup was verified. The
colony report does not expose a fallback count. Installed-game contract
controls retain every original native move instruction, reject unknown
layouts and verify cached access and legacy/boundary behavior.

The subsequent `pipes-heat-732-v1` bundle uses rebuilt native metadata/capability
2 and passes all eight checks after 26 frames. Gas mixing gives
303.3540374939621 K and liquid mixing 306 K, exactly matching the analytic
heat-capacity weighted temperatures. Native conduction exchanges
-737.16118018637644 J and native disease 1 changes by -14,591 germs; the
32 kg inventory, U and every germ account still close exactly. World
mass/energy errors are 6.678e-17/4.995e-17, trace sink 0 kg/0 J, no owner
fault, verified process cleanup. Diagnostic owner mean/p95/max is
210.356/260.087/272.580 ms; observed spacing 383.958/400.380/3178.467 ms,
with the same polling, startup/build inclusion and concurrent-HIGH caveats.
Fallback counts remain unavailable. Valves, machine recipes, vents,
destruction, all-disease evolution and complete save continuation remain
separate acceptance work.

The `mixed-pipe-filter` scenario adds actual powered gas/liquid filters,
element sensors and a germ sensor. The first run passed one-cell composition,
unblocked filtering, all sensor cases and inspection, but failed four
partial/blocked-output checks: the native room gate required empty outputs.
The correction preserves its existing power/status/history state machine
while supplying actual selected/nonselected output capacity. The criteria
remain unchanged. Immutable `pipes-filter-732-v4` passes all 17 checks after
33 frames: only the selected species reaches its filtered output; each
output fills independently; blocked remainder stays in the source; minority,
trace and exact-threshold element sensing agree with the world threshold;
the germ sensor sums all identities; inspection shows full composition.

The final gas source retains 0.125 kg oxygen and 0.25 kg carbon dioxide;
the liquid source retains 1 kg water and 4 kg polluted water. Supplied/held
mass is 61.117187362106051 kg, removed mass and exact U residual are zero;
every mass, heat and germ account closes. World mass/energy errors are
1.021e-16/4.839e-17, sink 0 kg/0 J, no owner fault, cleanup verified.
Diagnostic owner mean/p95/max is 225.884/284.354/498.647 ms (33 samples);
observed spacing 373.319/516.006/3271.280 ms (32 contiguous samples), under
the same polling/startup/concurrent-HIGH limits. Fallback count is unavailable.
The model exclusion/capacity and installed-game native staging adversaries,
six pure groups, five hook groups, 37 inspection checks, 135 harness checks
and 83 native tests pass. A one-ulp compatibility correction now preserves
native movable mass, so newly received material is not advanced prematurely.
No production pipe activation or mixed save writer is enabled.

All ordinary inspection is read-only. Heat/germ reconciliation occurs at
deterministic pipe/thermal update boundaries, never because a hover or audit
was opened. A 100-read adversary with changed native temperature leaves the
inventory and reconciliation state unchanged. Scalar sums also normalize
to an order-independent exact expansion; 128 permuted reductions and wide
integer-oracle cases preserve bytes and correctly resolve half-ulp traces.
Previously encoded valid expansion bytes remain unchanged on re-encoding.

## World boundary settlement controls

`ColonyConduitSettlement.cs` computes a complete native job outcome before
changing any pipe account. Injection consumes only the committed mass and
returns undispatched reservation; its native U is authoritative and the
energy difference stays in a named boundary heat account. Actual sequential
passive observations supply the world-side quantity. Rounded-away increments,
erased old low parts and fractional quantities remain in the exact passive
representation account. Extraction retains every committed species, U and
passive, including quantum-cleanup constituents. Mechanical terms remain in
exact sparse accounts rather than becoming heat or overflowing a double.

The seventh pure control group covers full/partial/refused injection,
1e-240 mass and heat remainder, tiny/huge/fractional passive changes,
multiple extraction outcomes, duplicate/missing outcome watermarks and
unchanged replay bytes. A sealed save-boundary validator checks the pending
native-token suffix with at most k+2 queries: every pending token queued,
its predecessor retired and the next watermark unaccepted. Missing first,
middle or last jobs and stale watermarks are rejected. Its caller must first
pause submission, wait and drain/acknowledge committed outcomes; validation
does not create that barrier. These are model controls, not live component
ports or mixed-save acceptance.

The immutable `pipes-boundary-7e6-v1` fixture passes all 12 checks after
29 frames. Four constituent injections deliver 11 kg with simultaneous
fractional/tiny germ identities. Grouped phase extractions return three gas
constituents (1 kg) and two liquid constituents (9.9577839173929856 kg), each
with two germ identities, into actual mixed pipe cells. The gas capacity
boundary retains 1.3593399390905969e-17 kg in its owned port buffer. Zero
extraction also retires its token; the empty pending suffix and next token 8
agree with the native queue. Mass, U and every germ account close exactly,
including -0.055509135519969277 J retained boundary heat, two passive remainder
identities and exact K/G/J accounts. Later pipe conduction exchanges
22.85902061863537 J without changing that closure.

World mass/energy errors are 1.337e-16/3.694e-17, sink 0 kg/0 J, no owner fault,
cleanup verified. Diagnostic owner mean/p95/max is 190.783/236.985/238.318 ms
(29 samples), observed spacing 344.699/448.890/3071.215 ms (28 contiguous
samples); startup/polling/concurrent-HIGH caveats apply. Fallback count is
unavailable. Release83, seven pure groups, five hook groups, 37 inspection,
209 snapshot/backend controls and 135 harness checks pass. Receipts are
credited before legacy acknowledgement; inactive colonies avoid all optional
pipe receipt queries and acknowledgement allocations.

This boundary fixture is explicit and does not enable component ports. The
separate pump control below adds bounded component coverage. Vents, native
storage/recipe adapters, destruction, pipe phase behavior and saved physical
scheduling remain open. ONI's ordinary conduit save omits
transient movement staging, fairness directions and timers; an inventory-only
round trip does not establish deterministic gameplay continuation.

## Catalogue and real pump controls

The explicit `mixed-pipe-catalogue` control routes all 29 registered gases and
49 liquids through actual owned conduit cells, preserving two simultaneous
germ identities and exact inventory/heat accounts. Nineteen gas and 27 liquid
compatibility hashes are negative; authoritative identities remain native
ordinals. Immutable `pipes-catalogue-e652-v1` passes 5/5 checks in nine frames,
retaining 0.000078 kg in two buffers. This checks identity/routing, not every
material's phase or thermal behavior. World mass/energy errors are
5.902e-17/4.637e-17, trace sink zero, no owner fault, cleanup verified.

`mixed-pipe-pumps` owns fresh gas/liquid pump storage and queues their complete
phase extractions as durable jobs inside one ordered native actor batch.
Ordinary A/pump/B requests cannot cross a frame boundary between separate
enqueues. The optional batch capability is required before intake activation;
inactive colonies keep their original final actor batch. Native registrations,
power, rates, output requirements, consumed/stored notifications and
accumulators remain active. The original callback reaches its storage boundary
once, where owned inventory replaces the scalar packet write. Storage readers
and inspection report that actual inventory. Outlet publications and buffer
changes invalidate derived empty/blocked status without moving material.

The 25 fixed checks cover mixed single-cell outputs, both germ identities,
empty scalar storage, native notifications/accumulators, blocked storage
capacity, partial outlet refill, resumed intake, disabled intake with real
room, and exact mass/U/germ/mechanical accounts. Native `RequireOutputs` waits
for an empty outlet; the fixture honors that gate. Its first live v2 run passed
21/25: four setup assumptions incorrectly expected intake through a nonempty
outlet. Corrected stages fill storage with the dispenser off and outlet empty,
then restore an exactly full outlet using explicitly booked fixture supply.
V3 and v5 passed 24/25 and v4 passed 23/25: the remaining failures exposed stale
blocked indicators. A direct installed-component control now checks inactive
full storage, reopened room and emptied buffers without changing inventory.
V6 stopped during setup before the pump checks because native topology was not
ready; bounded readiness now precedes activation and shifts later stages by the
same delay. These failed runs do not establish pump acceptance.

Immutable `pipes-pumps-736-v7` passes all 25 checks in 71 completed frames.
Seventy original stored notifications run; blocked buffers hold exactly 1 kg
gas and 20 kg liquid beside exactly full 1/10 kg outlet cells. Partial outlets
refill without overflow. Queued intake rises 35 to 42 after the outlets empty,
then stays 42 after disabling pumps with actual intake room. Final held mass
is 35.250078 kg, fixture supply 0.20007798807907107 kg, extraction
35.050000011920929 kg; all pipe accounts close exactly. External pipe heat is
185.04549009075419 J, germ-1 exchange -16, two passive remainder identities
and all mechanical accounts retained. World mass/energy errors are
9.591e-16/5.563e-17, trace sink 0 kg/0 J, no owner fault, cleanup verified;
fallback count is unavailable. Receipt control compares 3155 byte-equal entries
in 128 nonempty/64 empty queues, with no prefix growth. Paired native-call means
are 0.002326 ms pinned and 0.487217 ms marshalled over 192 calls.

Diagnostic LOW owner mean/p95/max is 387.155/753.413/784.952 ms (71 samples),
observed 553.488/906.194/4904.638 ms (70 contiguous samples, no gaps). These
include startup, polling and receipt instrumentation; concurrent HIGH work was
permitted. They establish behavior, not a whole-game speed gain. Seven pure
groups, six installed hook groups, 37 inspection, 209 snapshot/backend and 135
harness checks pass. The native batch implementation independently passes the
parent's 83 CTests and concurrent ordered-batch controls.

Completed pump intakes now coalesce into one exact storage inventory before
notifications/discharge; pending reservations retain their independent job
identities. The previous buffer list could discharge an older arrival mixture
instead of the aggregate. A seventh installed hook control combines two
different compositions and two germ identities, checks byte-exact combined
inventory and proportional quarter discharge, and rejects duplicate mixing
without mutation. `pipes-storage-340-v1` passes 27/27 live checks in 71 frames:
the original 25 plus gas/liquid aggregate partial discharge. Gas storage
1 -> 0.75 kg has largest species residual 5.551e-17 kg; liquid 20 -> 17 kg has
zero residual. All per-species/germ/U proportions and exact pipe accounts pass.
Seventy-two native stored notifications still run; final port buffers fall
from 13 in v7 to 7 (including auxiliary fixture buffers). This is a correctness
and bookkeeping change, without a whole-game speed claim.

That run's world mass/energy errors are 2.744e-16/2.482e-13, trace sink zero,
no fault, cleanup verified; fallback count unavailable. Diagnostic LOW owner
mean/p95/max 198.703/376.249/415.441 ms (71 samples), observed
285.590/421.944/3046.178 ms (70 contiguous samples, no gaps), controls off.
Seven pure/seven hook/209 snapshot and 135 harness checks pass. All pipe runs
above use the unchanged legacy owner profile, Config.Flags=1; they do not select
`MA_COLONY_GAME_FLOOR=1` or establish game-floor/profile acceptance.

### Exact inventory cost

`PipeAmount` now uses TwoSum directly for two single atoms and retains an
already-canonical two-atom pair. It preserves exact sums and serialized atoms;
it changes neither fluxes nor saved physics. Private baseline/candidate seven
pure groups and a 9000-state serialized corpus agree (SHA256
`5F28739BF325A89F5D33FE8BFE4B89A4EAD4BC2B5A78C65264EDB4EB484C0F45`).
WHOLE CPU-only model measurements use five sets of 20,000 transfers after
50,000 warm-up moves, with the material delegate cached on both sides:

| Constituents | Before / after, microseconds per transfer | Before / after, bytes allocated |
| --- | --- | --- |
| 1 | 0.548 / 0.310 | 2332 / 1044 |
| 2 | 1.015 / 0.660 | 4652 / 2556 |
| 6 | 2.317 / 1.179 | 10948 / 4752 |

These are .NET 10 model medians and allocations, not Unity/whole-game speed
gains. Main seven pure, seven installed hook and 209 snapshot groups pass.
Immutable `pipes-account-e3f-v1` also passes all 27 live pump checks in 71
frames on the same legacy profile/native pair. Every pipe account remains
exact; world mass/energy errors 6.614e-16/3.478e-17, trace sink zero, no fault,
cleanup verified, fallback count unavailable. Diagnostic LOW owner
mean/p95/max 189.865/272.293/400.121 ms, observed
271.873/383.998/3078.944 ms (71/70 samples, no gaps), instrumentation off.
Concurrent-HIGH/startup caveats remain; this is a behavior regression check.

Full production activation and mixed saves remain off. Finite-body mixed-Cv
equilibrium, deterministic germ growth/death, foreign-phase cleanup returns,
phase teardown and saved component scheduling still need acceptance controls.

The finite-body control `mixed-pipe-thermal` is defined before its first run:
an isolated native radiant gas pipe at arena coordinate (3,3), surrounding
vacuum, no connected source/sink, and its real construction mass/material heat
capacity. At setup stage 8 the body is heated to 400 K; six frames later the
owned cell receives exactly 0.5 kg oxygen plus 0.5 kg hydrogen, both at 300 K.
Topology readiness may delay setup by at most 22 frames, shifting every later
stage equally. After 586 further frames, compare both temperatures with
`(Cbody*Tbody_initial + Cmixture*300)/(Cbody+Cmixture)`, requiring each within
0.1 K. Compare actual body heat loss with mixture U gain; its measurement bound
is two binary32 endpoint ulps of body temperature plus 1e-7 of fluid heat.
Also require both original species masses and exact pipe-account closure.
Report the body's real mass/Cv, both species Cv, temperature endpoints, heat
partition, booked heat and residual. A closed pipe ledger alone is insufficient.

First immutable run `pipes-thermal-340-v1` stops at owner frame 456 before the
final check: an unrelated whole-colony consumer at (103,333) extracts 1e-6 kg
CO2 beside brine/water traces and faults with no positive thermal energy above
reference. The result/Player.log detect the fault even though the last completed
frame's live report has an empty Error field. Cleanup passes; no private dump
was enabled. Actual initial body is 25 kg, Cv 9700.0002861022949 J/K; oxygen and
hydrogen Cv 1004.9999952316284/2400.0000953674316, analytic equilibrium
385.06906383847286 K. Final thermal metrics remain unresolved, with their
criteria unchanged. This run also uses legacy Config.Flags=1. Parent owns an
opt-in transfer-stage capture for a same-profile replay; a scoped arena would
be an explicit fixture change, not removal of this full-colony failure.

The unchanged parent capture bundle `79e0545`, legacy Flags=1, reaches frame
601 without a native fault or either transfer-capture file; the original 456
failure is not erased. Its thermal checks pass 3/5 with the original naive body
capacity: observed body/mixture 353.2601318359375/353.2594036595591 K, body heat
-453376.7345637898 J versus fluid/booked +90674.137143025291 J. Inspection of
the actual registration contract then identifies a fixture error:
`StructureTemperatureComponents.SimRegister` uses
`BuildingDef.MassForTemperatureModification`, not physical construction mass.
The corrected fixture reads that declared mass and reports both. A 5 kg thermal
mass predicts 353.2601236134025 K, but still leaves approximately -1.20977 J
body-plus-fluid residual, above the original two-endpoint-ulp bound. No energy
bound is loosened, and thermal acceptance remains unresolved. Capture-run world
mass/energy errors 1.772e-15/5.731e-17, trace sink zero, cleanup verified.

Opt-in `MA_COLONY_PIPE_THERMAL_LOG=<fresh private path>` now records each
exchange around native `ConduitTemperatureManager.Sim200ms`, before owned
reprojection: native pipe mass/T, shadow T/carrier Cv, registered body thermal
mass and native-float Cv, actual structure-record/callback/backing temperatures,
pending kJ, exact owned U change and inferred carrier work. It allocates/writes
only when selected, refuses an existing file, and is disabled in final timings.
Its `.work` log observes exact arguments to managed
`SimMessages.ModifyBuildingEnergy` and `StructureTemperatureComponents.ProduceEnergy`,
with body/source/thread/context; channels must not be double-counted. `.meta`
records whether both observer prefixes are installed. Whole-body snapshot
changes remain explicitly unattributed outside the proven isolated fixture.

`pipes-thermal-probe-34f-v1` reaches 601 frames and passes 4/5 checks with the
declared 5 kg thermal mass. Body/mixture 353.26016235351563/353.25942167434033 K
agree with analytic 353.26012361340253 K; original energy bound still fails:
body -90675.28770865465 J, fluid/booked +90674.16781319116 J, residual
-1.1198954635 J versus 0.2458838300 J. All 587 exchange rows have exact owned
delta-U equal to inferred rounded-carrier work (maximum residual zero), but
every synchronous structure-temperature delta is zero: body work publishes
later. The backing InternalTemperature stays at 400 K and is not the runtime
temperature. World mass/energy errors 1.746e-15/5.618e-17, trace sink zero,
no native fault/captures, cleanup verified. Diagnostic LOW owner mean/p95/max
180.430/199.774/430.519 ms, observed 231.271/315.350/3002.908 ms
(601/600 contiguous samples, no gaps); no performance claim.

The requested-work preflight `pipes-thermal-work-v2-run` intended 40 frames but
overshot to 151: named scenarios control their own completion and ignore that
frame setting; its external stop came late. This missed bound is recorded,
not called a completed acceptance. Game-group cleanup is verified; interruption
wrote no canonical final result. Both prefixes are installed, yet their work
file contains zero calls for the observed body, including out-of-context calls.
Its 136 hot exchanges book/infer exactly 90484.47283728456 J. The existing native
thermal result exposes temperatures/freeze/melt handles, no shared-energy
array. Exact requested body work or its native queue is the concrete missing
access; no invented representation account or relaxed bound is introduced.
Further probing stops pending that handoff; other pipe integration continues.

### Biological update still to implement

Current native reconciliation evolves only the projected germ identity;
minority identities remain in inventory but do not yet grow or die. Owned pump
buffers also need biological updates. Retention is not complete biological
acceptance.

Proposed mixed-medium rule, not adopted: on the fixed native disease tick,
evaluate each identity independently using ONI's public element-growth and
temperature functions. For a mixture, evaluate each constituent's material
rule against the whole cell's population and mass, then combine material
changes by exact constituent mass fractions; apply the temperature term once
at the mixture temperature. This reduces to the native material rule for a
single constituent. It does not distribute germs among fictitious native
packets. Each identity needs its own saved native rounding accumulator plus
exact sub-integer/overflow representation; every birth/death is booked to
that identity's named biological exchange. Native-compatible single-species
integer populations must retain the exact native count/rounding trajectory.
The extension outside native integer range, fractional populations, storage
tick timing, and mixed-medium truth remain unresolved. No rule is selected
until these limits and single/native versus mixed controls are established.

### Atomic output control

The next explicit `mixed-pipe-output` setup is fixed before measurement: two
isolated source cells hold gas 0.375 kg O2 + 0.625 kg CO2 and liquid 3 kg water
+ 7 kg polluted water, each pair carrying germ-0 quantity 0.25 and germ-1
quantity 1e-240. Move each full mixture into an owned buffer. A deliberately
out-of-rectangle native admission must preserve the exact source object and
both token frontiers; then a valid output atomically queues its two constituent
jobs with all passive slices. The source debits only after strong native batch
admission. After receipt settlement require both constituents of each buffer
to share a physical completed frame, all four exact masses to reach the world,
both germ identities to be booked, contiguous retired tokens and exact
pipe/boundary accounts. Undispatched reservation/remainder stays owned.
Completion-frame instrumentation is allocated only by this explicit control
and is absent from normal runtime. This is an output primitive for vents and
teardown, not completed component integration or save acceptance.

Immutable `pipes-output-79e-v1` passes all 10 checks in 19 frames, using the
SHA-identical parent `79e0545` native pair and legacy Flags=1. Both rejected
batches preserve source identity and unaccepted tokens 1/3. All four accepted
jobs complete in physical frame 13, delivering exactly 11 kg and germ quantities
0.5/2e-240; no mass/germ remainder remains. The named boundary heat account
retains -0.055493040752480738 J and the mechanical G account is retained; every
pipe/boundary account closes exactly. Native token 5 is unaccepted after all
outcomes retire. World mass/energy errors 5.899e-17/4.933e-17, trace sink zero,
no fault, cleanup verified, fallback count unavailable. Seven pure/seven hook,
209 snapshot and 135 harness checks pass. Diagnostic LOW owner mean/p95/max
181.436/261.063/261.063 ms, observed 393.974/3135.041/3135.041 ms (19/18
contiguous samples, no gaps), controls off. No performance acceptance is claimed.

### Conservative teardown control

`mixed-pipe-teardown` is defined before its first measurement. Its 8x6 arena
has a sealed solid region x=2..6, y=1..5, a central blocked spill cell (4,3), and
open spill cells (0,0)/(7,0). Each phase has one pipe in its open cell and one
in the central solid cell. Seed every gas pipe with 0.375 kg O2 + 0.625 kg CO2,
every liquid pipe with 3 kg water + 7 kg polluted water, and each mixture with
germ quantities 0.25/1e-240 at 300 K. Demolish actual native prefabs at stage
10. At stage 18 require all native conduits removed, 11 kg/four species spilled,
11 kg held in two refused spills, complete cell inspection, no repeated retry
within a physical frame and exact accounts. At stage 20 open the central cell;
by stage 32 require all 22 kg released, no pending/root/buffer inventory, hidden
spill inspection, all germ quantities delivered/booked and exact accounts.

The native dump receives its scalar contents by value. After reserving the
complete owned mixture, suppress that scalar emission while its caller retains
normal cleanup. Refused spill buffers remain owned at their source coordinate;
completed returned constituents merge before retry. Retries use completed
owner frames and stable IDs, with no wall-clock load input. Hover/selected-cell
details label held contents "Fluid waiting for space" without replacing native
solid descriptions. Inspection cannot dispatch or alter inventory. This remains
explicit-only; phase damage/returns and saved retry scheduling are separate
acceptances, not established by a warm open-spill check.

Immutable `pipes-teardown-ffd-v1` passes all 10 checks in 33 frames, legacy
Flags=1/SHA-identical `79e0545` native pair. Four real native prefabs disappear;
11 kg spills, 11 kg remains in two sealed spill trackers. The cell reader
reports both phases/all four constituents, 100 reads and repeated same-frame
ticks keep attempts 18 -> 18. After opening the cell, all 22 kg and germ
quantities 1/4e-240 reach the world, with zero held/pending/root state and no
remaining spill inspection. All accounts close exactly; retained boundary
heat is -0.095180099629942561 J and mechanical G remains booked. World
mass/energy errors 1.800e-16/5.583e-17, trace sink zero, no fault, cleanup
verified, fallback count unavailable. Seven pure/seven hook/37 inspection,
209 snapshot and 135 harness checks pass. Diagnostic LOW owner mean/p95/max
175.969/219.020/228.368 ms, observed 311.533/458.943/2982.569 ms
(33/32 contiguous samples, no gaps); no speed claim. Phase-induced pipe damage,
latent/foreign-phase returns, biological evolution and saved retries remain
open, so production activation stays off.

### Valve/shutoff acceptance

The fixed `mixed-pipe-valves` control builds manual valves and powered shutoffs
for both phases, with separate single-cell inputs/outputs and passive sinks.
Each gas input starts 0.375 kg O2 + 0.625 kg CO2; each liquid starts 3 kg water
+ 7 kg polluted water, with fractional/tiny germs at 300 K. Native topology
readiness can shift every stage equally by at most 20 frames. Hold closed for
10 frames, then set manual rates 0.1/1 kg/s and enable native powered shutoffs
for 10 frames. Manual output should be 0.2/2 kg within one native tick
(0.021/0.21 kg); shutoffs transfer full 1/10 kg within 1e-6 kg. Require original
mixture proportions within 1e-10 and zero transfer after closing. Fill manual
outputs to leave 0.01/0.1 kg room and verify full capacity/no overflow within
1e-7 kg. Empty shutoff outputs into owned sinks and refill inputs while still
disabled, proving closure with real output room; reopen and require full mixed
transfer. Final exact mass/U/germ accounts must close. No production setting
or native valve decision is replaced by this control.

The first setup timed out before initialized prefab ports were available. A
second setup creates the pipes after initialization; it reaches flow checks
but fails 9/15 at frame 35. Gas manual flow is 0.200000003 kg, liquid manual
flow 1 kg against the 2 kg expectation, and both shutoffs remain blocked by
their native automation controller. Refilling that still-full input is
correctly rejected. These are unresolved fixture/acceptance failures, not a
valve pass. Native world mass/energy errors are 2.957e-16/3.612e-17, trace sink
zero, no native fault, cleanup verified. Diagnostic LOW owner mean/p95/max
312.90/433.01/462.11 ms, observed 531.64/542.33/5095.39 ms (35/34 samples,
no gaps); no performance claim. The next setup supplies the unconnected
automation value through its initialized native controller and records native
rate/max, ports and elapsed game clock without changing the flow criteria.

The corrected `pipes-valves-e96-v4` control passes **23/23 in 45 frames**.
Both phases observe two native valve calls with dt sum 2 s (game-clock elapsed
1.933303833 s). Manual output is 0.200000003/2 kg and powered shutoffs deliver
1/10 kg, all with the original two-species composition. Closed leakage is zero;
partial outputs are 0.999999999776/9.99999999404 kg, with exact capacity never
exceeded. Disabled shutoffs retain full inputs while their outputs have real
room, then reopen with complete mixtures. All 41.689999997 kg and germ accounts
close exactly, with zero U residual. The change supplies the fixture's native
automation controller after initialization; it changes no production rate or
operational decision. The prior liquid 1 kg cause remains unproven because its
native clock was not recorded; the new observer is explicit-fixture-only.

World mass/energy errors are 5.050e-16/4.767e-17, trace sink zero, no fault,
cleanup verified, fallback count unavailable. Diagnostic LOW owner mean/p95/max
205.538/302.927/369.177 ms, observed 322.030/431.218/3071.469 ms (45/44
contiguous samples, no gaps); no performance claim. This does not activate
production mixed pipes or establish vent, thermal, biology or save acceptance.

### Vent surface-germ preparation (runtime inactive)

A small pure helper prepares the vent's surface exchange separately from
conduit scheduling. For a single integer population it preserves the native
integer-to-binary32, 5% scaling, then integer truncation sequence in each
direction. For a mixture, each supported fluid identity is sampled in stable
identity order using that same rule. Fractions and quantities beyond native
integer range stay in the fluid; total incoming counts are capped against
remaining body integer capacity before any native mutation. Empty fluid never
receives a massless germ population. Mass and heat are unchanged.

The building still has exactly its observed native single germ identity.
After native mutations, each identity's actual body change minus its planned
transfer is recorded as native competition/identity conversion. This differs
from biological growth/death and from fluid transfer; no hidden mixed body is
invented. Pure controls cover native integer rounding, simultaneous identities,
tiny fractions, huge populations, capacity, unsupported identities, arbitrary
observed identity changes and exact replay bytes. They do not establish live
stronger/weaker native competition, vent readiness, all-ID biological evolution
or saved continuation. The helper has no runtime caller yet.

## Precise owner receipts

The optional native adapter exports `ma_oni_colony_enable_precise_receipts`,
`ma_oni_colony_copy_precise_receipt` and `ma_oni_colony_wait`. Start and restore
reset the opt-in. Changing it requires an empty receipt queue; repeating the
current mode is harmless. Size queries and repeated copies leave receipts
intact; the existing acknowledgement retires both legacy and precise data.
The 48-byte legacy receipt, physical trajectory and native checkpoint bytes
remain unchanged. A solid byproduct has no precise fluid sidecar. This API
alone does not enable mixed pipe ownership.

**MARECP01** is an ephemeral, little-endian payload (claimed 2026-10-23 after
fetching `56b35ea`), never a solver checkpoint. The eight-byte magic is followed
by uint64 sequence, uint64 completed frame and uint32 parcel count. Each parcel
stores uint32 native element index, uint32 actual global cell index, uint32
injected flag, then binary64 mass, temperature and signed internal energy.
Four scaled terms follow, in order: resolved kinetic energy, potential energy,
x momentum, y momentum. Each stores its original binary64 mantissa, uint32
ASCII exponent length and exact signed decimal base-two exponent. Finally a
uint32 passive count precedes pairs of uint64 identity and binary64 quantity.
No exponent or conserved quantity is projected through float or through the
binary64 value range. Zero parcels and quantum-cleanup constituents are kept.
A phase extraction's per-species sidecar includes only that species; other
transfers include every original parcel associated with that legacy receipt.
Missing or retired sequences return zero bytes; an undersized buffer receives
no partial write and reports the required size.

The focused native control covers exponents beyond int64 and binary64 range,
trace masses, multiple fractional passives, identity/copy/ack lifetime, and
identical enabled/disabled trajectory, checkpoint and legacy receipt bytes.
Mixed-pipe save integration must drain and credit relevant receipts while the
game submitter is paused, before releasing its save barrier. Waiting for the
owner does not acknowledge or deliver receipts. Exact return injection and
in-flight save/restore remain separate acceptance work.

The managed reader also accepts the agreed **MARECP02** job extension:
after each parcel's passive list, uint32 observation count and sorted
`{ uint64 identity, binary64 before, binary64 after }` entries. These are
sequential per-edit storage observations, so `before + supplied - after`
can be retained exactly in the schema-2 passive remainder account. The
ordinary MARECP01 reader remains unchanged. Reader controls cover huge
exponents, zero parcels, every passive, malformed framing and a fractional
increment rounded away beside a large native count. This is codec evidence;
ports remain disabled until native observations and in-flight job/save
controls establish the complete boundary account.

Full port activation must first require
`ma_oni_colony_precise_receipt_version() >= 2`; an older native bundle or
missing export refuses activation before any job is queued. Ordinary legacy
colonies do not query this optional export.

Native world passive quantities are binary64 per identity: adding a tiny
quantity to a large existing count can round away, and a large incoming
quantity can erase the old low part. Echoed input passives are therefore
insufficient evidence of what the world stored. Durable job requests accept
at most 65,537 sorted positive passives with identities through 0x100000000;
schema-2 jobs and passive remainder accounts enforce these same limits.
Actual sequential MARECP02 observations and a named signed representation
remainder are required for both injection and extraction. No complete
world/pipe germ-conservation or output/save acceptance has passed yet.
