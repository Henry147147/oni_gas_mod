# Experimental native-empty gas reservoir bridge

This is an explicitly activated, private ownership experiment for individual
native GasReservoir instances. It is not enabled by installing the mod or by
loading a prefab. It neither adopts native packets nor owns native world fluids,
pipe flow, construction materials, power, heat exchange or colony persistence.
Windows v23/v24 exercise two actual owned reservoirs; the bounded evidence is
described below. Full-colony acceptance remains open.

`OwnedGasReservoirBridge` keeps every bound `Storage.items` list strictly empty.
Its completed Core container allocation owns the fluid mass, energy, momentum,
disease and radiation metadata. The building's native construction
`PrimaryElement`, native descriptions and ordinary visual components remain
native. There is no positive-mass dominant-material packet or zero-mass packet
used as an authority mirror.

The caller explicitly supplies a Core state and existing full container handles.
That state's complete container table is the session's container ownership
domain, including allocations never successfully bound to a native instance.
Containers must declare gas-only admission, physical volume and the native
reservoir's 1,000-kg gameplay capacity. Capacity is not a pressure or volume EOS.
This bridge creates or retires no Core allocations and imports no native mass.

## Driver sequence

Install the conditional guards with `OwnedGasReservoirBridge.Install(harmony)`.
Installation validates every pinned managed target before activation and verifies
that each prefix is present. Outside explicit ownership the guards preserve
native behavior.

At a known idle post-load boundary, with no native save coroutine already in
progress, call `BeginSession(state, router, explicitPrivateOptIn: true)`.
Initially publish an empty actor-operation batch so there is a completed paired
native/Core/presentation publication before binding a native owner:

1. `session.Prepare(operations, completedFrame, simulationTimeSeconds, model)`
   stages closed commands into the private existing frame coordinator. It
   validates native owners and full session/allocation leases, prepares immutable
   receipts and the presentation, and reserves the custom writer.
2. `session.PrepareSnapshot(batch, stableNativeSource)` deep copies all native
   channels. Actor operations use no world projection and fabricate no legacy
   callback. A malformed source leaves the previous publication and can be
   retried against the same prepared batch.
3. `session.Publish(batch)` commits the paired publication. Bind the runtime to
   `session.Current?.PreparedPresentation`, using its exact prepared object.
4. `session.BeginConsumption(batch)` begins the one delivery attempt.
5. Consume/validate the copied native snapshot without dispatching its copied
   events into `Game` in this private experiment. Call
   `AcknowledgeSnapshotConsumption(batch)` only after its readers have finished.
6. `session.NotifyActors(batch)` emits each affected owner's native storage
   notification channels once. Their meter and logic queries resolve the same
   completed full handle. A source/destination mentioned several times gets one
   before/after receipt for the completed batch.
7. `session.AcknowledgeConsumption(batch)` requires both independent reader and
   actor-notification acknowledgements before releasing the writer.

Then bind each fully spawned, isolated native reservoir with
`BindNativeEmptyReservoir(storage, fullHandle)`. Binding rejects native packets,
foreign/replaced allocations, duplicate owners, conflicting prefab identities,
multiple storage components, incompatible admission/capacity and connected
native input/output ports. It does not infer or adopt the reservoir's contents.

Closed operations are `new OwnedContainerTransferOperation(sourceLease,
destinationLease, massKg)`, `All(sourceLease, destinationLease)` and the explicit
`EvacuateToCell(sourceLease, ownedCoreCell)`. The last emits the existing
`AllContainerToCell` command and validates the destination in the same atomic
transaction. It does not infer a native vent location or modify native cells.
All operations and acknowledgements require the activation thread.

## Fences, cleanup and saving

Owned native Store/Add/consume/drop/remove/transfer paths reject before mutation;
transfer guards cover either an owned source or owned destination. Every pinned
transfer overload takes its destination directly as `Storage`; its `GameObject`
parameter, where present, is the source item. `Store` also checks the source
item's actual `Pickupable.storage` before native reparenting, disease transfer
or absorption. A stale owned source reference rejects even though the bound
reservoir's packet list is empty. Pickupable absorption checks both storage
owners. Native conduit Consume/Dispense calls
are skipped for the exact owned storage, independently of mutable native on/off
flags. No native pipe network becomes custom-owned.

Storage mass, remaining capacity, fullness and emptiness queries resolve the
completed Core container through its full handle. Native `Reservoir` and
`SmartReservoir` continue to drive their actual meter/logic components using
these queries. Native notification delivery revalidates exact ownership between
`Storage.Trigger`, `OnStorageChange` and `SmartReservoir.Sim200ms`; a callback
ending ownership prevents later notification channels.

Normal `UnbindEmpty(storage)` requires an empty inventory including metadata,
acknowledged publication/consumption and no pending operation. A nonempty source
must first evacuate conservatively through the protocol. Native deconstruction
is fenced before its side effects. An unexpected queued destruction, forced
cleanup or session teardown removes the native binding while retaining its Core
allocation and an orphan/recovery record. `OnForcedCleanUp` covers scene loading,
where native `OnCleanUp` can be skipped. Recovery observations retain their
detachment revision and refresh the allocation from the latest recovery snapshot.

Every published Core-cell evacuation sets a sticky in-session
`WorldRecoveryRequired` obligation. Emptying and unbinding the containers does
not make that custom world inventory persistent in a native save. No general
bypass or loss-acknowledgement API clears this obligation.

Native saving is blocked throughout activation and after teardown while any
retained container is nonempty, a world evacuation obligation exists or a
published batch ended without consumption acknowledgement. Guards
cover public `SaveLoader.Save` before autosave deletion, native storage
serialization, `Game.StartDelayedSave` before its UI/dragging callbacks, manual
save completion and both save-first quit paths. This does not implement an
envelope or a recoverable cross-process actor outbox. Private acceptance exports
the exact Core checkpoint and quits its disposable colony without saving.

A callback that changes an actor and throws has an ambiguous external outcome.
Its inventory remains committed and notifications are never automatically
replayed or refunded. Native reader acknowledgement is still required before
`ResolveConsumptionFault` can release the publication. Explicit settlement
attests external recovery; it is not evidence of successful callback delivery.
Session teardown stops the protocol and preserves recovery state, rather than
claiming a native rollback.
If a published native copy has not received its reader acknowledgement, teardown
retains that copy and its private coordinator reservation. After all terminal
readers have returned, `AcknowledgeEndedSessionSnapshotReaders(ticket)` releases
the native memory only. It does not clear the sticky unresolved-consumption
recovery obligation, unblock saving or resume actors. A callback ending the
session therefore cannot free buffers still in use by its outer consumer.

## Verification scope

`tools/OwnedReservoirTests` compiles the production bridge/protocol against the
installed licensed declarations. Its 51 host checks cover conserved mixed
transfers, fractional disease, immutable prepared views/receipts, independent
acknowledgements, callback faults, stale/foreign leases, source retry, capacity
and late-command rejection, forced cleanup during staging/callbacks, ordinary
evacuation/unbind, sticky world/unfinished-consumption obligations, terminal
reader retention and acknowledgement, never-bound recovery,
completed-frame queries, dormant guards and pinned method/prefix signatures.
The source-item regression executes the real managed `Pickupable.storage`
property against the production source guard and verifies the prefix's actual
component-resolution wiring. It also checks both endpoints of all five pinned
transfer signatures before any native body runs. Native item transform/disease
side effects still require the separate Unity acceptance.

The idle eligibility lease pins the exact acknowledged Core publication,
companion, prepared presentation and both allocations. It excludes competing
Core writers and actor mutation while retaining completed readers. Native
owners are revalidated through their exact captured components before and after
Core serialization, with reentry and forced-cleanup guards. Disposal cannot
release a successor reservation. This observation lease supplies no persistent
owner token, native serializer permit or ordinary-save bypass. Its geometry
checks establish distinct valid anchors. The separate copied-footprint validator
below checks every occupied cell and both ports while this lease is held;
captured observations do not extend the lease or authorize native reload.

These tests use synthetic owned native buffers and controlled managed actor
hosts; they do not run the Unity simulator or UI. The shipped Harmony's patch
table uses BinaryFormatter, which .NET 10 cannot execute. Actual Mono/Unity hook
installation, two spawned storage instances, meter/logic events and preserved
native descriptions must therefore be verified in the separate disposable game
run. Passing host tests does not certify full actor coverage or first playable.

Windows v23 and v24 pass the actual Unity bridge sequence using two separately
spawned GasReservoir buildings. Native packet lists remain empty while Core
owns their complete gas inventories. Three paired publications retain one
prepared presentation per completed snapshot, compare the private native copy
against its source, and complete both independent acknowledgements. A malformed
native descriptor rejects before publication and retries without events or loss.

The source starts with 101.94444739357503 kg. A quarter transfer leaves
76.45833554518127 kg and puts 25.486111848393758 kg in the destination, including
chlorine, disease quantity 0.09375 and radiation metadata 0.03125. The destination
panel shows 215.315625 kPa and the chlorine warning at about 0.000197647% by
moles, above its explicit 0.0001% limit. These are fixture values using a declared
10 m3 physical volume, separate from the native 1000 kg mass capacity.
Native mass/fullness queries, actual meter inputs and SmartReservoir logic
follow the completed allocation. Single-precision native visual ratios are
checked within one float ULP; Core and prepared numeric views retain doubles.

The runs verify exactly one native storage event per affected owner, conservative
evacuation into an explicitly owned Core cell, normal empty unbinding, stale
lease rejection, and source-item Store rejection before native transform,
mass, temperature or disease changes. Actual native mutation, port,
deconstruction and save entry points are fenced. Forced destruction retains the
destination allocation and exact orphan record; native saves remain blocked
after teardown because orphan/world recovery is not integrated persistence.
Whole-state totals and exact external ledgers are checked throughout, and the
recovery checkpoint is exported only as private evidence.

Each run includes 28 Unity PNGs. V23's empty-owner live-label check passes, but
PNG28 renders after unbinding and shows native fallback. V24 defers unbinding
until the following phase: its PNG28 visibly retains the owned vacuum reading,
0 kg and the no-breathable-atmosphere warning. Both colonies close cleanly by
computer-use Alt+F4, and the runner restores original mods, preferences,
appearance and generated-world files byte-for-byte. Frozen sources, package
hashes, reports, checkpoints and screenshots are under
`local/colony-runs/presentation-windows-v23` and `presentation-windows-v24`.
Their native DLL is pinned to SHA256
`77671261d559e60a2ea8a80b2a431d01064f416fa6540e27f4cddb7ac6dedf38`;
later dormant solver changes are not part of those game binaries.

Windows v25-lease runs the same bridge sequence with a separate containers-only
Core authority, rather than adopting the earlier world fixture. Explicit input
is 79 kg nitrogen, 21 kg oxygen and 0.0005 kg chlorine, with disease quantity
0.375 and radiation 0.125. A third empty unbound allocation is preserved. The
quarter transfer places 25.000125 kg in the destination at 211.8307064 kPa and
20 C; the selected panel displays the trace chlorine warning. PNG28 retains the
owned vacuum reading before unbinding. The native atmosphere keeps its original
world presentation: synthetic Core identity geometry supplies no world readings.

Both the seeded/empty-destination and two-nonempty-owner states acquire and
revalidate the live eligibility lease. The run checks exact native saved IDs,
prefab, anchor/world/orientation, construction, capacity, volume and component
diagnostics; exports exact canonical Core checkpoints; and rejects nested
acquisition, writers, actor preparation, unbinding and native saving before
mutation. Releasing an old lease cannot release its successor. Evacuation into
a Core cell subsequently makes eligibility reject the world-recovery state.
All 28 PNGs, the two idle checkpoints and final recovery checkpoint remain under
`local/colony-runs/presentation-windows-v25-lease`, with frozen source/package
hashes and bytewise restoration evidence. The native DLL remains the exact
`77671261...` build used by v23/v24. V25 has no persistent owner marker or actual
owned-state native save/load.

Windows v26-markers repeats this sequence and passes with 28 Unity screenshots.
The completed GasReservoir prefab carries one inert, unassigned marker; preview
and construction prefabs carry none. Both live owners remain unassigned.
Seven private primitive fields have a strict interpretation: only complete
empty defaults are unassigned, and partial or unsupported data is invalid.
There is no production assignment, adoption or load activation API.

While each idle lease is held, read-only capture checks all 15 building cells,
world identities, definition and placement caches, component lifecycle, both
utility caches, connection objects and absence of physical gas pipes. A separate
neutral-shape enumeration agrees with the actual native building layer.
Copied foreign occupants and stale utility caches reject without changing Grid
or repairing the owners. Geometry-only validation explicitly reports
`ManifestValidated=false`. V26 predates the subsequent exact global saved-ID
tracker checks; it does not establish that part of future restore eligibility.

The actual game serializer round-trips default, privately assigned and invalid
markers through a local template/mapping on two inactive fixture objects. The
assigned fixture preserves a generation of 2^40 and rejects a stale handle;
both live owners remain unchanged. No global serialization directory is reset.
The report records the assertions and byte counts; raw marker/template bytes
were not archived for independent byte reconstruction.

V26 also captures the actual native `Sim.Save` section in memory for both idle
states. Each section is 8,129,837 bytes, SIMSAVE v15, 738 by 306 cells. A checked
MACOLONY envelope preserves that native payload, exact canonical Core bytes,
catalogue fingerprint and completed frame. Its custom data is a Core checkpoint,
not an owner manifest or full `.sav`. No Storage serialization permit or public
save bypass is granted. An independent review checks the exported bytes and
hashes, 72 frozen source files, package, baseline conservation and restoration
of all 13 original user files. Private evidence is under
`local/colony-runs/presentation-windows-v26-markers`; the native DLL remains
the pinned `77671261...` game build.

The focused `tools/OwnedReservoirMarkerTests` now passes 31 checks on Windows and
against Linux declarations, plus separate netstandard2.1 contract builds. It
covers strict marker data, native serializer wiring, immutable observations,
full footprint and port validation, overlaps and exact manifest/allocation
coverage. Capture now peeks the existing native tracker without initializing
it, pins its exact reference and saved ID, and requires the ID to resolve to
the exact KPrefabID before and after observation. Copied tracker agreement
defaults false and is required by validation. A licensed-method host regression
rejects a different object with the same saved ID even when neither conflict
flag is set, without repairing the mapping. These host checks do not instantiate
a Unity colony or restore owners.

Windows v28 passes the full 28-screenshot sequence with those tracker checks
for both actual owners in both idle states. It also runs a separate normal
netstandard2.1 file-protocol assembly inside ONI's Mono 6.13 runtime. All nine
checks pass, including new/replacement/equal/zero-byte/Unicode streams,
share-delete readers, deny-delete error 32 with retained recovery data, and
strict read-only intent inspection. The driver verifies the assembly SHA256
before loading its bytes. The helper remains excluded from the normal mod and
supplies no native-save permit.

The preceding v27 run remains failed: its longer private fixture paths prevented
stage/backup creation before any native rename. V28 uses the exact same
Oni/Core/native binaries and helper source; only the separate validation
assembly's fixture names and path budget change. Derived paths are at most
239 UTF16 characters. This proves that bounded runtime path only; future
product integration still needs an explicit artifact/path eligibility check.
Both runs close cleanly and restore all 13 original user files byte-for-byte.
Private evidence is under `local/colony-runs/presentation-windows-v27-mono`
and `presentation-windows-v28-mono`. The reproducible source-only validation
tool is documented [here](../tools/OwnedReservoirGameMonoValidation/README.md).

The host suite's 51 checks also pass against licensed Linux declarations in
isolated WSL. This does not draw Linux Unity, establish rendering performance,
route native pipes, transfer native world fluids, provide heat/power gameplay,
or save/reload the owned state. `FirstPlayableColonyPassed` remains false.
