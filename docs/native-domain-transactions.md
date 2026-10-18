# Native domain boundary transactions

How game requests add, remove or reserve fluid in the
[sealed native domain](sealed-native-domain.md), beyond the base heat-command,
point-transfer and registered-actor ABIs described there: additive cell-mass
edits, radius and rectangular consumption, the accepted frame-input journal
with its completed-frame image, and the live-duplicant breathing control that
uses them together. Object and solid heat exchange is in
[domain physics](native-domain-physics.md). Declarations are in
`native/include/mixed_atmospheres/oni_domain.h`.

Completed-frame audits serialize on demand. Publication pins the immutable
source and completed physical owners with their durable actor records and
accepted inputs; the next publication replaces those pins. Ordinary frames do
not serialize or hash full checkpoints. Snapshot and journal readers materialize
and cache the exact existing formats, including source hashes, under the
publication lock. Pending edits and actor configuration cannot change a pinned
image. Save requests retain their drained-state checks and perform full
serialization. Native controls check exact replay, repeated reads and bounded
retention; this does not establish colony-scale publication performance.

## Rules shared by every transaction

- A batch is prepared as one candidate and reserved atomically. Overlapping
  requests in a batch see earlier reservations. A later invalid request
  publishes none of the batch, and the source buffers stay unchanged.
- Commands name an exact session and expected frame. Physical state changes at
  the next positive native tick. Receipts refer to the same completed frame as
  the copied cells and stay pending until acknowledged after an owned snapshot
  publication.
- A batch that spans native and custom owners rejects before either owner
  changes. Batches entirely outside the owner keep their original native
  dispatch.
- Constituent and U+K(+G) accounts use the 128-epsilon bound over actual
  operation operands. There is no absolute mass or heat floor, no constituent
  cutoff and no minimum extraction.
- Callback handles keep all 32 bits; only -1 means "no callback". Disease and
  implicit pump work or enthalpy are never carried.
- Point messages, radius/rectangle requests and registered actors share one
  phase classifier. Scenarios 4–6 admit gas and water in the same occupied
  cell; gas-only and filled-liquid scenarios keep single-phase admission.

Transfer ABI 2 is a 64-byte `MAOniDomainTransfer`. Kind 1 injects at rest and
kind 2 removes selectively. The additive capabilities below reuse its `flags`
word without changing its size or any reserved field:

| Flag bits | Meaning |
| --- | --- |
| 0 | Quantities are binary32-compatible. Required by radius and rectangle requests |
| 8–15 | Kind-2 radius when bits 16–23 are zero; rectangle width otherwise |
| 16–23 | Nonzero selects a rectangle of this height |

Receipt kinds are 1 inject, 2 consume, 3/4 consumer/emitter samples and 5/6
blocked/unblocked edges.

## Additive cell-mass edits

The owner intercepts ONI's additive `ModifyCell` message, including the
explicit-material `SimMessages.ModifyMass` path that `CO2Manager` uses to
deposit a breath particle. The inspected 744825 packet is 28 bytes on Windows
and Linux.

The adapter preflights the complete batch before reserving anything. An owned
request needs positive finite mass and temperature, a supported constituent in
the cell's current phase, callback -1, no replacement, no vertical displacement
and no disease. Padding is ignored. Mixed ownership, malformed packets,
unsupported edits or more than 256 owned requests reject the batch. Canonical
compatibility writes stay guarded by the projection-write scope.

Each accepted parcel becomes a transfer ABI 2 command bound to the completed
source frame, with the binary32 flag. The whole batch is reserved atomically
and committed at the next positive physical step. The receipt records the
actual constituent mass and material energy. Callback -1 still needs
acknowledgment but produces no game callback. A rejected owned edit faults the
bounded owner, which keeps its native exclusions until shutdown.

Not supported: negative mass edits, replacement, bulk spatial consumption,
callback-bearing edits, disease and production device reservations.
Depositing an independent breath particle is not a conserved metabolism
account; a breathing provider must couple oxygen consumption to its products.

### Particle deposition control

`carbon-particle` (`native-carbon-particle-domain`; Windows
`-NativeDomain carbon-particle`; at least 30 frames) creates a real 0.03125 kg,
310 K particle through `CO2Manager.SpawnBreath`. The game moves, deposits and
retires it normally. Its deposition must produce exactly one physical receipt
without native scalar dispatch. The control checks every constituent, carried
energy, producer retirement, unchanged native compatibility cells, rendering
and outside native heat, with fixed bounds of 1e-10 kg and 1e-6 J. The paired
save is taken after the particle retires. The reviewer requires the actual
producer and once-only receipt; generic transfer evidence cannot pass it. Run it
with the [matrix](native-validation-matrix.md#running-the-matrix) and
`--scenes carbon-particle`.

The retirement witness latches removal in the `CO2Manager.Sim33ms` postfix:
checking pool membership at save time confuses the particle with a later breath
that reuses its pooled component. Receipt ABI size and version belong to the
transfer-status envelope, not to each receipt row.

**Status.** Linux Unity source/save and fresh-process reload pass (maximum
constituent error 2.665e-15 kg, carried-energy residual -3.376e-9 J, 120 exact
continued frames). WindowsPlayer under Proton passes the same source and
reload in the [feature matrix](native-validation-matrix.md#windowsplayer-feature-matrix).
Actual Windows OS is not tested.

## Radius consumption

`GasBreatherFromWorldProvider.ConsumeGas` submits a radius-three
`SimMessages.ConsumeMass`. The adapter keeps the real radius byte and the
original callback. Radial capability 1 encodes the radius in flag bits 8–15;
radius zero keeps the point rule. An older native library rejects the
capability before the adapter changes game state.

The owner allocates from face-connected fluid cells in centre-first
breadth-first order, up to the radius. Solids block the search and the sealed
boundary is impermeable; native fluid outside the owner is never read or
consumed. This is the backend's declared policy, not a claim that vanilla's
search is identical.

One kind-2 receipt and one original game callback carry the total mass,
mass-weighted temperature and complete parcel U/K/momentum. The receipt's cell
is the request centre, not a claim that every parcel came from it.

The total and every parcel lie on one shared binary32 callback lattice, so
their sum is exact. The total never exceeds the requested or available mass.
Finer residual inventory stays in each source cell, including positive traces
below callback resolution. If nothing is representable the callback carries
zero mass; inventory is never rounded up into a parcel.

**Verification.** The `native_oni_radial_consumption` CTest covers the binary32
range, exact combined delivery, retained traces, connected order, solid
barriers, gas and wet owners, overlap, mass-weighted temperature, complete
U+K/species accounts, late atomic rejection and exact checkpoint continuation.
Declaration tests use the actual radius-three packet. The game scene
`native-radius-domain` (`radius`) injects 0.05 kg of oxygen, then requests
0.4 kg at radius three. The receipt must exceed the centre cell's original
oxygen plus the injection, proving several source cells were used, with exactly
one game callback, complete physical accounts and avoided native dispatch.

**Status.** LinuxPlayer and WindowsPlayer (Proton) sources each deliver
0.4000000059604645 kg in one callback, against a centre inventory of
0.2134 kg plus the injection. Both pass local and cross-player two-session
reloads in the [fluid regression matrix](native-validation-matrix.md#fluid-regression-matrix).

## Rectangular consumption

The owner also accepts the width/height overload of `SimMessages.ConsumeMass`.
Rectangular capability 1 puts the height in flag bits 16–23 and the width in
bits 8–15; height zero keeps the point or radius encoding. The adapter checks
the capability before it prepares a source scene.

The whole footprint is reserved before dispatch, including a request whose
anchor is outside the owner but whose rectangle overlaps it. A footprint must
lie inside one owner and cannot wrap a row. The anchor is the lower-left cell;
rows are visited bottom to top and cells left to right, matching the inspected
native loop. Solids in the footprint are skipped. A zero width produces one
zero-mass callback.

Allocation uses the radius lattice and batch rules. One receipt and one
original callback carry combined mass, mass-weighted temperature, material
energy, kinetic energy and momentum. Accepted-input journals keep both
dimensions and replay the complete result exactly.

**Game control.** `native-rectangular-domain` (`rectangular`) injects 0.05 kg
of oxygen, then requests 0.4 kg across a three-by-two footprint. Acceptance
requires one callback delivering more than the anchor's original inventory
plus the injection, complete physical accounts, no original native mass
dispatch, normal rendering and an ordinary paired save. Reloads must pass the
paused frame-zero save and exact continuation.

**Status.** Native gas and wet controls pass on Linux, Windows binaries under
Wine, the Ubuntu 22.04 build and ASan/UBSan. They cover row order, skipped
solids, the binary32 exponent range, overlap, traces, U+K/species accounts,
late rejection and exact journal replay. No actual Unity result is recorded
yet; that control is open.

Pumps, pipe/container transfers, conversion chemistry and whole-colony device
ownership remain open under the [completion plan](completion-plan.md).

## Accepted frame inputs and completed images

A saved fluid image cannot reproduce the next physical frame by itself once a
duplicant or device submits a command after loading. The owner therefore
records every successful external heat and mass reservation in acceptance
order.

Each completed worker frame publishes one checksummed `MAFRIN01` journal with
its physical state. It holds the source session/frame, completed frame,
physical time, actual timestep, domain geometry, SHA-256 of the complete
source snapshot and the ordered heat and transfer batches, including callback
identifiers and radius/rectangle flags. Batch boundaries are kept because one
heat batch makes one physical revision. Failed reservations add nothing.
Limits: 1 MiB, 2,048 batches, and the live per-frame limits of 4,096 heat and
4,096 transfer commands.

Journal staging shares the native frame and state locks with the reservation.
All allocations and checks finish before either candidate is published. The
worker prepares the journal before committing its new physical state; a
failure publishes neither. A new session discards earlier journals. A copy
needs the exact last completed session/frame, even when commands for the next
frame are already queued.

| Entry point | Contract |
| --- | --- |
| `ma_oni_domain_frame_inputs_abi_version()` | Returns 1 |
| `ma_oni_domain_frame_inputs_copy` | Copies the immutable journal of the last completed frame |
| `ma_oni_domain_snapshot_advance_with_inputs` | Validates checksum, framing, source image, model/geometry and clock, applies the captured batches, then runs the ordinary actor and physical advance |
| `ma_oni_domain_completed_snapshot_abi_version()` | Returns 1 |
| `ma_oni_domain_completed_snapshot_copy` | Copies the full physical and durable-actor image published atomically with the journal |

Replay invokes no game callbacks, does not replace the live owner and does not
change saved game identities; callbacks are routed once by the live
publication path. Internal replay uses local epochs and never modifies the
captured commands. On any failure, including insufficient capacity, the output
buffer and required-length field are unchanged.

The completed-frame image can be copied while a duplicant has already reserved
its next request or callbacks await delivery; pending inputs and actor changes
cannot alter it. It needs the exact current session/frame, and actors without
durable identities are unsupported. It is a reader operation only:
`snapshot_copy`, paired capture and ordinary saves keep their drained-input and
callback guards. The Unity continuation verifier uses the completed image and
saving uses the guarded checkpoint, because a live duplicant usually has its
next oxygen request pending.

Durable registered actors advance together with captured inputs. Actor
configuration changes are flagged and their replay is not implemented. Fixtures
that only refresh a binding, with no external commands, keep the exact snapshot
comparison and are reported as actor-refresh frames. External inputs combined
with an actor refresh, and actors without durable identities, refuse replay, so
an omitted actor change cannot pass as a replay.

**Verifiers.** The managed saved-colony verifier checks journal framing, both
SHA-256 bindings, geometry and every command's source epoch, compares each
replayed snapshot with the actual next worker frame, and keeps the initial
source image, every journal and a per-frame digest report. It uses the actual
game timesteps and commands and predicts no AI decisions. An empty journal gives
the same bytes as snapshot-only continuation.
`tools/oni_frame_input_evidence.py --report <native-domain.json>` independently
decodes the saved journals (commands, order, batch boundaries, epochs, geometry,
checksums, callback/radius fields) and checks each against its reported digest,
count and clock, without loading native code. It requires input replay on every
frame and refuses actor-refresh reports. It verifies captured inputs, not
physics.

`MA_VERIFY_FRAME_INPUTS=1` and `MA_VERIFY_COMPLETED_SNAPSHOT=1` add the journal
and completed-image checks to the isolated licensed worker in the
[headless native interface harness](../tools/NativeShadowProbe/README.md).

**Status.** `native_oni_frame_inputs` passes on Linux, Windows binaries under
Wine and ASan/UBSan. It covers gas, water, gravity, wet cells, nonlinear
conduction and viscosity; interleaved heat, injection and radius consumption;
several commands in one heat batch; failed late reservations; empty-journal
identity; source/session/frame mismatch; durable actor timing; and rejection of
unsupported actor changes. Reversing the order changes the physical result, so
the ordering check is meaningful. Truncated and altered journals reject. The
licensed worker passes gas and water heat on Linux and under Wine (61 complete
frames each, one successful two-command batch, late-invalid batch absent).
Actual Unity reloads on both players replay all 120 frames per session from
their journals with no actor-refresh fallback, including the live-breathing
sessions below.

## Live duplicant breathing

The development option `VerifyLiveBreathing` connects radius consumption to an
actual organic duplicant. The source uses the ordinary gas scene for 20–25
completed frames. After projection is established it takes an existing
world-provider breather, stops its old navigation path and places it once
inside the sealed three-by-two room. Normal navigation and
`OxygenBreather.Sim200ms` continue; there are no artificial breathing ticks or
substitute timesteps.

Each accepted oxygen request must carry the real world-provider callback data
for that duplicant, its radius-three flag and its original handle. The native
receipt must match the request's source epoch, cell, constituent and available
mass. A read-only observer around the provider's callback records the actual
handle while the original callback runs, and the duplicant's consumed-gas
event must report the same mass and temperature. Callbacks queued earlier
outside the room stay outside this receipt chain; the observer creates none.

Completion requires a drained, positive chain of requests, receipts and
callbacks, unchanged unconsumed species and traces, and the complete fluid U+K
account at its 128-epsilon bound. Only the tracked duplicant's objects are
admitted; other actors still reject.

Saving keeps the serialized duplicant at its actual location. Loading finds
the unique organic breather already in the room without teleporting it, and
positive time is held until it binds through its normal spawn lifecycle. The
journal replays the actual subsequent oxygen requests and CO2 deposits. A
loaded control requires positive original breathing callbacks, not just reader
calls. Oxygen depletion and exhaled CO2 may make the mixture unbreathable
during a long reload; there is no refill or scrubber. Two sessions submit
different live requests, so the reload comparison uses the
[captured-live-inputs mode](native-domain-persistence.md#comparing-two-sessions).

```sh
python3 tools/oni_headless.py run --package <package> \
  --mode native-domain --frames 20 --save-colony --verify-live-breathing
python3 tools/oni_headless.py run --package <package> \
  --mode native-load-domain --frames 120 --load-save <source.sav> \
  --verify-live-breathing --resave-before-resume --reload-in-process
```

The Proton runner takes the same flag; the Windows runner takes
`-VerifyLiveBreathing` with `-NativeDomain gas` or `load`. Evidence checks
reject mismatched handles, mass, temperature, source epochs, radius flags,
disease and undeclared species changes.

**Status.** Linux and WindowsPlayer (Proton) sources and their local and
cross-player two-session reloads pass in the
[CO2 particle cohort](native-domain-persistence.md#in-flight-co2-particles),
with original positive callbacks, 120 exact input-replayed frames and 120
[native object heat](native-domain-physics.md#native-object-heat) contacts per
session. (That bridge was added when the first reload failed at frame 2: native
element-chunk heat changed a compatibility temperature behind the owner.)

This is not conserved metabolism, and `CompleteMetabolismVerified` stays
false. Vanilla's oxygen-to-CO2 policy can credit requested oxygen before its
callback. Body and food chemistry, suit/tube providers, ventilation and
long-term survival remain open.
