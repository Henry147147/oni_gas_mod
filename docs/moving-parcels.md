# Explicit moving parcels

This is the native boundary for full liquid-pipe output and falling particles.
It does not enable ordinary mixed pipes, a new save writer or a numerical
profile. Codex owns solver admission; the parent gameplay worker owns the
native queue, wire and durability; the pipe worker owns particle identities,
split/swap remapping, exact held inventories and component scheduling.

## Coordinates and conserved values

`ma_oni_colony_copy_parcel_context` supplies the saved owner origin, cell
spacing, gravity, flags and current session from one immutable publication.
Carried potential energy uses the same core-local reference as extraction
receipts. Convert global particle poses with that origin exactly once.

A physical mixed parcel is one ordered atomic batch. Unsupported primary
range, invalid equilibrium or inability to report exact grid credit refuses
the candidate without mutation or owner fault. The exact source reservation
remains available for retry or return. Momentum is deposited into the core's
faces; an at-rest mass/temperature injection cannot substitute for this API.
Kinetic mixing heat and potential redistribution are internal energy moves,
not additional incoming energy. No new sink or rounding repair is authorized.

## Canonical request wire

`MAPARC01` contains an 8-byte magic, little-endian uint32 item count, then
ordered entries. Each entry contains:

- uint32 public cell and native element ordinal (not `SimHashes`);
- finite positive binary64 mass in kg;
- five scaled values in order: U, K, G, Jx, Jy;
- uint32 passive count and sorted, unique, positive uint64 identity / finite
  positive binary64 quantity pairs.

Entries strictly increase by `(public cell, native ordinal)`. One scaled
value is a little-endian binary64 significand, uint32 ASCII exponent byte
length and canonical signed decimal exponent. Its value is
`significand * 2^exponent`. Nonzero significands have magnitude at least 0.5
and less than 1; zero is positive zero with exponent `0`. U/G/J may be signed;
K is nonnegative. Exponents are parsed by `MacExponent`, without conversion
to a fixed-width integer or a floating-point exponent. Its existing capacity
bounds apply, including at most 20,000 exponent characters. Inputs exceeding
that supported range reject as invalid wire, not a simulation fault.

The complete wire is at most 16 MiB, with 1–65,536 entries and at most 65,537
passive identities per entry. Truncation, trailing bytes, invalid numbers,
duplicate/unordered identities and noncanonical encodings reject. These are
protocol bounds, not physics gate changes. The parser and native encoder use
the same validation and preserve canonical bytes exactly.

Actual native grid mass, U/K/G/J and passive changes require additive scaled
**component arrays** in receipts. A single significand cannot represent, for
example, `1 - 1e-100`; `.value()` is an estimate and cannot settle an exact
account. Nominal consumed source data and actual grid credits remain separate;
managed settlement retains every measured difference. The durable native queue is described below. Existing `MARECP01/02` and
mass/temperature jobs retain their layouts and behavior.

`MACOLN01` wrapper 9 persists pending moving-parcel jobs after explicit
admission; version 8 output remains unchanged while that facility is unused. No core physical
checkpoint version is claimed by this request wire.

## Exact outcome wire

`MAPOUT01` is the bounded outcome returned by the explicit native capability. Its
little-endian header contains the magic; uint64 sequence, completed frame,
job token and live session; uint32 refusal code; and a uint32 byte length
followed by a refusal reason (at most 4,096 bytes). A uint32 length then
frames the nominal consumed `MAPARC01` request, followed by uint32 grid-credit
count. Refusals consume nothing, have no credits and carry a reason. Successful
outcomes have one grid credit for each consumed input, in matching order.

Each credit contains six additive scaled-component arrays: mass, U, K, G,
Jx and Jy. An array is a uint32 count followed by that many nonzero scaled
values. It then contains sorted uint64 passive identities with component-array
changes, component arrays for kinetic mixing heat and potential work, scalar
wall impulses Jx/Jy, and separate observed passive inventories before and after.
The final six scaled scalars are energy residual/bound, Jx residual/bound and
Jy residual/bound; bounds are nonnegative. No component estimate settles an
account. The complete outcome is at most 64 MiB, each expansion at most 65,536
components. Refusal codes are core codes 1–4, publication range 16 and receipt
capacity 17; success is 0. Live session IDs fence asynchronous work and are
not durable colony identities.

Native controls retain `1 - 1e-100` mass credit, signed formation energy,
mechanical exponents beyond fixed-width integers, fractional passive changes,
work and wall impulses through a byte-exact encode/decode round trip. Every
truncated prefix, malformed identity/status/count/bound and trailing byte is
rejected. These codec controls are separate from the native queue controls below.

## Explicit owner queue and durability

Capability `ma_oni_colony_moving_parcel_version() == 1` admits one physical
mixture through `ma_oni_colony_queue_moving_parcel(session, token, request)`.
The expected live session must match the saved-geometry context. Precise
receipts must be enabled. Moving jobs share the existing contiguous durable
pipe-token sequence; retrying an outstanding token must supply identical
request bytes. A retired token stays retired. Never issue a new token for an
unsettled reservation. Do not use the legacy mass/temperature API to retry it.

The owner keeps the canonical request bytes and a zero-mass marker in the
existing durable input sequence. Legacy transfers execute first, followed by
moving physical batches in their queued order, before flow. Each batch calls
the core once and maps public coordinates/material ordinals once. Unrelated
physical parcels remain independent atomic batches. Complete outcome encoding
and receipt publication storage precede candidate-world installation. Native
reporting-range or receipt-capacity refusal also leaves the candidate unapplied.
The owner's existing audit books actual grid credits; exact source settlement
uses every component in the outcome, retaining all measured source differences.

A committed job has one receipt of kind `MA_ONI_COLONY_MOVING_PARCEL` (7), with
zero legacy mass/temperature. Retrieve its complete `MAPOUT01` via
`ma_oni_colony_copy_moving_outcome`; `MARECP01/02` is not its settlement format.
The receipt/job state and outcome become readable only after the complete
physical frame publishes. Existing sequence acknowledgement retires both job
and outcome. A normal refusal consumes nothing and produces an ordinary
completed outcome without an owner fault or fallback.

After the first explicit moving admission, wrapper9 stores each pending job's
canonical payload (zero length for ordinary jobs). It preserves the token
watermark after acknowledgement. The writer holds one coherent input snapshot
across framing and serialization. Restore validates the complete payload,
marker, token and geometry before installing a session; restored outcomes use
the new live session. Committed outcomes must be settled and acknowledged
before saving. No production managed caller, default ownership activation,
numerical setting or old-save migration is added by this boundary.

Focused owner controls check identical pending-request save round trips,
byte-exact cold physical continuation, stale session/changed retry rejection,
invalid saved payloads without replacing the live colony, undersized output
buffers, acknowledgement retirement, and normal refusal against a no-input
physical continuation. Actual pipe particles, source settlement and full mixed
manual/autosave acceptance remain the managed integration's responsibility.
The optional existing stage log reports batch/item/accepted/refused counts and
queue-work milliseconds; it remains disabled for final timing runs.
