# Experimental native-empty gas reservoir bridge

This is an explicitly activated, private ownership experiment for individual
native `GasReservoir` instances. Installing the mod or loading a prefab does not
enable it. It does not adopt native packets or own native world fluids, pipe
flow, construction materials, power, heat exchange or colony persistence. It
works on managed Core containers and is not connected to the native fluid owner
used by the [sealed native domain](sealed-native-domain.md).
`FirstPlayableColonyPassed` remains false.

The same work also produced pure managed inspectors for ordinary ONI saves and
a dormant boundary that rejects loading saves which declare owned reservoirs.
Neither grants restore, writer or save authority.

## Ownership model

`OwnedGasReservoirBridge` keeps every bound `Storage.items` list strictly
empty. The completed Core container allocation owns the fluid mass, energy,
momentum, disease and radiation metadata. The building's native construction
`PrimaryElement`, native descriptions and ordinary visual components stay
native. No positive-mass dominant-material packet or zero-mass packet mirrors
the authority.

- The caller supplies a Core state and existing full container handles. That
  state's complete container table is the session's ownership domain,
  including allocations never bound to a native instance.
- Containers must declare gas-only admission, a physical volume and the native
  reservoir's 1,000 kg gameplay capacity. Capacity is not a pressure or volume
  EOS.
- The bridge creates and retires no Core allocations and imports no native mass.

## Driver sequence

Install the conditional guards with `OwnedGasReservoirBridge.Install(harmony)`.
Installation validates every pinned managed target before activation and
checks that each prefix is present. Outside explicit ownership the guards keep
native behavior.

At a known idle post-load boundary, with no native save coroutine in progress,
call `BeginSession(state, router, explicitPrivateOptIn: true)`. First publish an
empty actor-operation batch, so a completed paired native/Core/presentation
publication exists before any native owner is bound:

1. `session.Prepare(operations, completedFrame, simulationTimeSeconds, model)`
   stages closed commands into the private frame coordinator. It validates
   native owners and full session/allocation leases, prepares immutable
   receipts and the presentation, and reserves the custom writer.
2. `session.PrepareSnapshot(batch, stableNativeSource)` deep-copies all native
   channels. Actor operations use no world projection and fabricate no legacy
   callback. A malformed source leaves the previous publication in place and
   can be retried against the same prepared batch.
3. `session.Publish(batch)` commits the paired publication. Bind the
   presentation runtime to `session.Current?.PreparedPresentation`, using that
   exact prepared object.
4. `session.BeginConsumption(batch)` begins the single delivery attempt.
5. Consume and validate the copied native snapshot without dispatching its
   copied events into `Game`. Call `AcknowledgeSnapshotConsumption(batch)` only
   after its readers have finished.
6. `session.NotifyActors(batch)` emits each affected owner's native storage
   notification channels once. Meter and logic queries resolve the same
   completed full handle. An owner mentioned several times gets one
   before/after receipt for the batch.
7. `session.AcknowledgeConsumption(batch)` requires both the reader and the
   actor-notification acknowledgements before releasing the writer.

Then bind each fully spawned, isolated reservoir with
`BindNativeEmptyReservoir(storage, fullHandle)`. Binding rejects native packets,
foreign or replaced allocations, duplicate owners, conflicting prefab
identities, multiple storage components, incompatible admission or capacity and
connected native input/output ports. It never infers or adopts the reservoir's
contents.

The closed operations are `new OwnedContainerTransferOperation(sourceLease,
destinationLease, massKg)`, `All(sourceLease, destinationLease)` and
`EvacuateToCell(sourceLease, ownedCoreCell)`. Evacuation emits the existing
`AllContainerToCell` command and validates the destination in the same atomic
transaction; it does not infer a native vent location or modify native cells.
All operations and acknowledgements must run on the activation thread.

## Fences, cleanup and saving

- Owned native Store/Add/consume/drop/remove/transfer paths reject before
  mutation, for an owned source or an owned destination. Every pinned transfer
  overload takes its destination as `Storage`; its `GameObject` parameter,
  where present, is the source item.
- `Store` also checks the source item's actual `Pickupable.storage` before
  native reparenting, disease transfer or absorption, so a stale owned source
  reference rejects even though the bound packet list is empty. Pickupable
  absorption checks both storage owners.
- Native conduit Consume/Dispense calls are skipped for the exact owned
  storage, regardless of mutable native on/off flags. No native pipe network
  becomes custom-owned.
- Storage mass, remaining capacity, fullness and emptiness queries resolve the
  completed Core container through its full handle. Native `Reservoir` and
  `SmartReservoir` drive their real meter and logic components through these
  queries. Notification delivery revalidates exact ownership between
  `Storage.Trigger`, `OnStorageChange` and `SmartReservoir.Sim200ms`; a
  callback that ends ownership prevents later channels.

`UnbindEmpty(storage)` requires an empty inventory including metadata,
acknowledged publication and consumption, and no pending operation. A nonempty
source must first evacuate through the protocol. Native deconstruction is fenced
before its side effects. An unexpected queued destruction, forced cleanup or
session teardown removes the native binding but keeps the Core allocation and
an orphan/recovery record. `OnForcedCleanUp` covers scene loading, where native
`OnCleanUp` can be skipped. Recovery observations keep their detachment
revision and refresh the allocation from the latest recovery snapshot.

Every published Core-cell evacuation sets a sticky in-session
`WorldRecoveryRequired` obligation. Emptying and unbinding the containers does
not make that custom world inventory persistent in a native save, and no bypass
or loss-acknowledgement API clears it.

Native saving is blocked throughout activation, and after teardown while any
retained container is nonempty, a world evacuation obligation exists or a
published batch ended without consumption acknowledgement. Guards cover public
`SaveLoader.Save` before autosave deletion, native storage serialization,
`Game.StartDelayedSave` before its UI and dragging callbacks, manual save
completion and both save-first quit paths. There is no save envelope and no
recoverable cross-process actor outbox. Private acceptance exports the exact
Core checkpoint and quits its disposable colony without saving.

A callback that changes an actor and then throws has an ambiguous external
outcome. Its inventory stays committed, and notifications are never replayed or
refunded automatically. Native reader acknowledgement is still required before
`ResolveConsumptionFault` can release the publication; explicit settlement
attests external recovery, not successful delivery. Teardown stops the protocol
and keeps recovery state rather than claiming a native rollback. If a published
native copy lacks its reader acknowledgement, teardown keeps that copy and its
coordinator reservation. After all terminal readers return,
`AcknowledgeEndedSessionSnapshotReaders(ticket)` releases only the native
memory. It does not clear the unresolved-consumption obligation, unblock saving
or resume actors, so a callback that ends the session cannot free buffers its
outer consumer still uses.

## Idle eligibility lease and inert markers

The idle eligibility lease pins the exact acknowledged Core publication,
companion, prepared presentation and both allocations; see
[publication reservations](container-inventories.md#idle-checkpoints-and-publication-leases).
It excludes competing Core writers and actor mutation while completed readers
continue. Native owners are revalidated through their captured components
before and after Core serialization, with reentry and forced-cleanup guards.
Disposing a lease cannot release its successor. The lease supplies no
persistent owner token, native serializer permit or ordinary-save bypass.

The completed `GasReservoir` prefab carries one inert marker; preview and
construction prefabs carry none. Seven private primitive fields are read
strictly: only complete empty defaults are unassigned, and partial or
unsupported data is invalid. There is no production assignment, adoption or
load-activation API, and live owners stay unassigned.

While a lease is held, a read-only copied-footprint validator checks all 15
building cells, world identities, definition and placement caches, component
lifecycle, both utility caches, connection objects and the absence of physical
gas pipes. A separate neutral-shape enumeration must agree with the native
building layer. Copied foreign occupants and stale utility caches reject
without changing `Grid` or repairing the owners. Capture peeks the existing
native saved-ID tracker without initializing it, pins its exact reference and
saved ID, and requires that ID to resolve to the exact `KPrefabID` before and
after observation. A different object with the same saved ID rejects.
Geometry-only validation reports `ManifestValidated=false`; observations do not
extend the lease or authorize native reload.

For the idle states, the game validation captures the native `Sim.Save`
section in memory and wraps it in a checked `MACOLONY` envelope with exact
canonical Core bytes, the catalogue fingerprint and the completed frame. That
custom data is a Core checkpoint, not an owner manifest or a full `.sav`. The
checkpoint codec is described in
[owned containers](container-inventories.md#idle-checkpoints-and-publication-leases).

## Selected-load rejection boundary

`OwnedReservoirLoadBoundary` is dormant until an explicit
`OwnedReservoirLoadBoundary.Install(harmony)`. Installation pins the inspected
Windows Assembly-CSharp, firstpass, Unity Core and Harmony assemblies. Its host
scope and file list are in [the load tests](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirloadtests-load-rejection-boundary).

- It reads a bounded native header, rejects every recognized owned
  declaration, checks the actual selected reader's header, and keeps fatal state
  through the exact public call and the matching scene teardown.
- Ignored inner or outer failures stay failures. The selected pointer is
  re-cleared, deserialization callbacks cannot continue, and the startup caller
  is fenced immediately after a false load.
- It never produces a valid owned header, assigns an owner, restores a
  checkpoint or grants serialization rights. Full component-table quarantine is
  not implemented.

Completion depends on where the failure happens, and the kind is chosen at the
first failure:

- **Existing `Game`.** The boundary retires only that `Game`'s managed view
  before requesting the frontend. It checks the field-only `SetIsLoading` call,
  then zeroes `Grid.CellCount` alone; dimensions, native arrays and stored
  contents are not repaired. Attempt, thread and current owner are rechecked
  after the call, and a foreign current `Game` blocks the scene request.
  Completion requires that `Game`'s exact `Game.DestroyInstances` body.
  Without the view retirement, `AmbienceManager.LateUpdate` reads the stale
  grid after native shutdown clears its mass pointer.
- **Before any `Game`.** No `Game.DestroyInstances` can complete the attempt.
  The boundary captures the live `SaveLoader`, its backend scene handle and the
  animation updater. It requires the loader's inherited
  `KMonoBehaviour.OnDestroy` and the matching
  `SceneManager.Internal_SceneUnloaded` body to return normally, with no
  outstanding selected-load or `OnSpawn` call. Each body has entry, return and
  normal-path witnesses and a one-use finalizer token. A fresh authority check
  precedes a compare-exchange that clears only the dead loader reference.
  A late `Game`, failed cleanup or foreign/successor reference invalidates
  completion, and the kind never falls back when an existing `Game` vanishes.
- Only the authenticated pending pre-`Game` attempt may skip
  `DoGridProcessing` on its captured updater's zero-sized controller grid. That
  avoids a divide-by-zero in `KBatchedAnimUpdater.CleanUp`. The rest of native
  animation processing continues; controllers, `Grid` and native shutdown are
  untouched.

The private call-counter map must include all four `SIM_Initialize` callers:
three `SaveLoader` bodies and `ProcGenGame.WorldGenSimUtil.DoSettleSim`. See
[the counter preflight](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirloadcountertests-load-counter-preflight).

## Saved-file inspection

These are pure managed parsers over immutable copies of save bytes. They
invoke no native reader, type resolver, saved callback or prefab, and they write
nothing. Native Sim bytes, arbitrary custom components and the terminal tail
stay opaque, and every report denies load, save, restore, writer and
quarantine authority.

### Saved-template and object-section inspection

The section inspector parses saved directory descriptors and KSAV 7.38
object/component records. It reads marker, `KPrefabID`, `Tag` and inherited
`Storage` fields in saved order and walks recognized `Storage` packets. Unknown
component payloads, prefix and tail stay opaque byte ranges.

- The body-prefix inspector walks the inflated body from byte zero. It
  validates the supported saved layouts of `Klei.SaveFileRoot`,
  `Game+Settings` and direct value-type `KMod.Label` rows, then derives the
  sole KSAV start from the bounded inline Sim envelope. It never searches for a
  marker or accepts an observed offset, and every collection length must equal
  the independently walked consumption. Non-null `requiredMods` and the legacy
  streamed `Sim` branch are unsupported.
- `InspectPrefix` walks declared template, member and descriptor counts from a
  supplied start and derives the directory end itself, with a separate
  consumed-byte budget. Physical bounds and count minima are checked before
  dependent allocation. No saved CLR type is resolved and no alternative offset
  is searched.

See [the inspection host](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirsavedinspectiontests-directory-body-prefix-and-ksav-inspection).

### Bounded compressed-fragment inspection

`SavedZlibInflationInspector` receives an immutable image and one exact zlib
range. An original structural walker validates stored, fixed and dynamic
blocks, Huffman descriptions, prior-output distance bounds, the final block and
exact compressed framing before a raw `DeflateStream` rebuilds the bytes.

- Positive partial reads are supported. A positive-capacity zero return, the
  exact structural output count and an independent Adler-32 must all agree.
  Decoder source position is never evidence of framing.
- Streams, work buffers and output state belong to one call. No Ionic codec or
  copied vendor implementation is included.
- Limits cover source, compressed and output bytes, output chunks, decoder
  calls, structural blocks and decoded symbols. Defaults are 1 GiB source,
  64 MiB compressed, 256 MiB output, 64 KiB chunks, 65,536 Read calls and
  blocks, and 67,108,864 structural steps. Callers may only tighten them.
- Output limits are checked before bytes are kept, including a bounded one-byte
  completion probe at the exact cap. A failure returns no partial candidate.

Format references are [RFC 1950](https://www.rfc-editor.org/rfc/rfc1950) and
[RFC 1951](https://www.rfc-editor.org/rfc/rfc1951); the platform reader's
[partial-read contract](https://learn.microsoft.com/en-us/dotnet/api/system.io.compression.deflatestream.read?view=net-10.0)
is handled explicitly. The tests keep regressions for two rejected designs:
an Ionic-based attempt accepted an invalid pre-history distance, and a
stricter successor rejected a legal empty-distance/repeat-18 stream. See
[the inflation tests](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirinflationtests-bounded-zlib-fragment-inspection).

### Complete-source layout inspection

`SavedColonyLayoutInspector` starts from one immutable complete source image.
It derives, in order, the pinned native header, the count-based template
directory, the exact compressed remainder, the body prefix and the KSAV
section. Each report keeps the same source, directory and body identities;
callers cannot inject precomputed ranges or a different decompressed body. The
original header rejection entry points are unchanged, with a separate pure
header-facts API. See [the layout tests](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirlayouttests-complete-source-layout-inspection).

## Verification

| Suite (commands in each README) | Scope | Status |
| --- | --- | --- |
| [`OwnedReservoirTests`](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirtests-bridge-protocol-and-idle-lease) | Bridge and protocol against licensed declarations | 51 checks pass on Windows and Linux declarations |
| [`OwnedReservoirMarkerTests`](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirmarkertests-owner-markers-and-footprints) | Markers, footprints, tracker checks | 31 checks pass on Windows and Linux, plus netstandard2.1 builds |
| [`OwnedReservoirLoadTests`](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirloadtests-load-rejection-boundary) | Load rejection boundary | 72 checks pass on Windows and Linux |
| [`OwnedReservoirSavedInspectionTests`](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirsavedinspectiontests-directory-body-prefix-and-ksav-inspection) | Section, body-prefix and directory inspection | 63 synthetic groups pass on Windows and Linux |
| [`OwnedReservoirInflationTests`](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirinflationtests-bounded-zlib-fragment-inspection) | Bounded inflation | 64 groups pass on Windows and Linux |
| [`OwnedReservoirLayoutTests`](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirlayouttests-complete-source-layout-inspection) | Complete-source layout | 81 groups passed on Linux and under Wine; the suite now has 82 |

Host suites use synthetic native buffers and controlled actor hosts and do not
run Unity. Shipped Harmony's patch table uses BinaryFormatter, which .NET 10
cannot execute, so hook installation, spawned storage, meter and logic events
and native descriptions are verified only in the game.

Actual Windows ONI 744825 disposable colonies pass:

- **Bridge sequence.** Two spawned `GasReservoir` buildings keep empty packet
  lists while Core owns their gas. A malformed native descriptor rejects before
  publication and retries without loss. A quarter transfer moves gas with
  chlorine, fractional disease and radiation, and the destination panel shows
  the chlorine warning. Native mass, fullness, meter and `SmartReservoir` logic
  follow the allocation (visual ratios within one float ULP). The runs also
  check one storage event per owner, evacuation to an owned Core cell, empty
  unbinding, stale-lease and source-item `Store` rejection before any native
  side effect, fenced entry points and orphan retention after forced
  destruction.
- **Live lease.** With a containers-only authority, both idle states acquire
  and revalidate the lease, export canonical checkpoints and reject nested
  acquisition, writers, actor preparation, unbinding and native saving before
  mutation. Native world presentation is unchanged.
- **Markers.** Both 15-cell footprints, ports and tracker checks pass in both
  idle states; the native serializer round-trips default, assigned (generation
  2^40) and invalid markers on inactive fixture objects.
- **Mono file protocol.** Nine checks pass inside ONI's Mono; see
  [Mono validation](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoirgamemonovalidation-mono-file-protocol-validation).
  Derived fixture paths must stay within 239 UTF-16 characters, and product use
  still needs an explicit path eligibility check.
- **Load rejection.** An ordinary unowned reservoir survives native save and
  reload bit-for-bit (12.5 kg oxygen, 310 K, disease count 12345). Existing-Game
  rejection, early `OnSpawn` rejection and an invalid-zlib native load failure
  each release fatal state and reach a stable frontend, with no selected-load
  body, unexpected native call, Unity error or crash marker.
- **Save inspection in Mono.** After an ordinary save, menu and reload, the
  layout inspector derives the complete layout from byte zero and its body
  equals the native body byte-for-byte; the reservoir packet and unassigned
  marker are unchanged.

## Open items

- Owned restore, assigned owners, a native or custom writer, ordinary owned
  saves and save permission. Arbitrary custom-detail descendants, exact
  live-reader exclusion, checkpoint/owner reconciliation and quarantine before
  instantiation are also open.
- Full component-table quarantine in the load boundary.
- Linux Unity behavior for every part of this document; Linux coverage is host
  and declaration checks only.
- Pipe routing, native world-fluid transfer, heat and power gameplay, and a
  connection to the production native fluid owner.
- Cached-world startup in the Windows validation runner still fails before a
  colony starts (observed in `SaveLoader.LoadFromWorldGen`, `SaveLoader.OnSpawn`
  and, most recently, `CustomGameSettings.GetCurrentClusterLayout`; see the
  [2026-10-17 retest](windows-validation.md#windows-game-checks-2026-10-17)).
  Fresh world generation works.
