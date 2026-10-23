# Mixed gas and liquid pipe contents

The playtest requires an actual composition in each gas/liquid conduit cell,
selective filtering, recipe-specific consumption, conserved heat and germs,
and exact save continuation. The current live adapter still sends mixture
constituents as separate native packets. The inventory and format controls
below are groundwork; they do not activate mixed conduit ownership.

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

Initial model measurement (Release, high CPU lock, 2026-10-23): median
0.176 us/transfer and 752 allocated bytes with one species; 0.368 us and
1,584 bytes with two species. Each uses five repeats of 20,000 alternating
moves after 50,000 warm-up moves. This measures the pure inventory only;
allocation remains an optimization target after the live correctness checks.
Run with `dotnet run --project tools/ColonyConduitTests -c Release -- --benchmark`.

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
