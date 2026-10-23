# Mixed gas and liquid pipe contents

The playtest requires an actual composition in each gas/liquid conduit cell,
selective filtering, recipe-specific consumption, conserved heat and germs,
and exact save continuation. Ordinary colonies still send mixture
constituents as separate native packets. The explicit harness fixture below
activates limited internal pipe ownership; production ports and the new save
writer remain off.

## Conserved inventory

`ColonyConduitInventory.cs` keeps ordered constituent masses, internal energy
and every disease identity. Exact floating-point expansions retain small
components when a larger quantity is added. Internal moves subtract and add
the same shared flux; no endpoint mass, heat or passive is clipped to close
an account. A native scalar packet will be a compatibility projection rather
than the source of truth.

The focused `tools/ColonyConduitTests` control checks two materials in one
cell, different heat capacities/formation energies, selective filtering with
blocked output capacity, 1,000 repeated transfers, heat, passives, evacuation,
retention of a 1e-240 kg component and byte-exact cold continuation. Native
scalar dispatch controls retain mass below float resolution, energy that a
float temperature cannot represent, and fractional/multiple germ identities.
Live storage changes, world receipts and all conduit integrations remain to
be validated; these controls do not establish end-to-end game conservation.

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

The `mixed-pipes` and `mixed-pipe-filter` harness scenarios alone activate the current runtime. It
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

This boundary fixture is explicit and does not enable component ports. Full
pumps, vents, native storage/recipe adapters, destruction, pipe phase behavior
and saved physical scheduling remain open. ONI's ordinary conduit save omits
transient movement staging, fairness directions and timers; an inventory-only
round trip does not establish deterministic gameplay continuation.

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
