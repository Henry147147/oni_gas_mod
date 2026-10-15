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

The separately installed selected-load rejection boundary now passes 56
Windows/Linux declaration checks. It reads a bounded native header, rejects
every recognized owned declaration, verifies the actual selected reader header,
and retains fatal state through the exact public call and successful matching
scene teardown. It forces ignored inner/outer failures to remain failures,
re-clears the selected pointer, stops deserialization callbacks from continuing,
and fences the already-entered startup caller immediately after a false load.
It never produces a valid owned header, assigns an owner, restores a checkpoint
or grants serialization rights. Full component-table quarantine remains absent.

Windows v29 attempted the private game acceptance package. Boundary installation
was reached, but the separate counter installer rejected its incomplete native
initialization-call map: the pinned assembly also calls SIM_Initialize from
`ProcGenGame.WorldGenSimUtil.DoSettleSim`. The process closed before colony
creation or any acceptance case. All 13 original user files were restored
byte-for-byte. Frozen source/package/log evidence remains under
`local/colony-runs/load-rejection-windows-v29`. This is a failed diagnostic
installation, not a passed load test. The corrected private map observes all
four initialization callers, 41 managed targets, 15 native callsite keys and
four managed dispatch antecedents. Declaration preflight does not install
Harmony or invoke native code.

Windows v30 installed that corrected map, but startup from the cached generated
world failed in `SaveLoader.LoadFromWorldGen` before the ordinary control. Its
exact null reference remains unresolved. The error screen was captured with
computer use and closed manually; engine shutdown subsequently logged a crash.
File restoration passed, but clean engine shutdown did not. Evidence is retained
under `local/colony-runs/load-rejection-windows-v30`.

Windows v31 used the same package with fresh world generation. Its ordinary
unowned native reservoir passed a real save, frontend transition and reload:
12.5 kg oxygen, 310 K and disease identity/count 12345 matched bit-for-bit.
Actual original bodies and native dispatch calibrated the complete counter map
with no observation errors. Direct rejection of the copied schema-99 header
then returned false without entering the selected load body or any measured
native initialize/load/start/save call. The selected path cleared and the stored
packet remained exact. All five frozen save-directory files remained unchanged.

V31 nevertheless failed the required stable-frontend gate. During transition,
`AmbienceManager.LateUpdate` read the stale grid view after native shutdown
cleared its mass pointer and before later grid cleanup cleared `CellCount`.
The process exited before complete teardown/stable frontend could be accepted;
the early-OnSpawn and caught-false cases were not reached. No user input closed
that process, and its quit origin was not observed. An independent review
verifies the scoped control and rejection evidence under
`local/colony-runs/load-rejection-windows-v31-fresh-world`. All 13 original user
files were restored exactly in both runs. These results do not establish owned
restore, ordinary owned saves, first-playable acceptance or Linux Unity behavior.
The boundary remains inactive by default.

The subsequent v5b correction retires only the exact failed Game's managed
view before requesting frontend: its field-only `SetIsLoading` call is checked,
then only `Grid.CellCount` is zeroed. Dimensions, native arrays and stored
contents are not repaired. Exact attempt, thread and current owner are checked
again after the call to exclude foreign callback reentry. Missing and foreign
owners are distinguished; a foreign current Game blocks the scene request.
This observation is separate from actual native teardown and grants no
initialization or shutdown inference. All 56 declaration checks pass on both
Windows and Linux; installation still requires the pinned Windows assemblies.

Fresh Windows v32 passes the ordinary native control and the full direct
rejection epoch. It observes the exact owner/view retirement, unchanged packet
and dimensions, zero native initialize/load/start/save dispatch, one frontend
request, actual clean matching `Game.DestroyInstances` completion and a stable
frontend for more than two seconds. It therefore verifies the v31 cleanup
correction for an existing Game.

V32 remains failed overall. In the early-OnSpawn case, SaveLoader rejects the
header before any Game instance exists, so no `Game.DestroyInstances` can
complete that fatal attempt. Native initialize/load/start/save calls remain
zero, but the pending attempt is not released. The partial backend transition
also raises a divide-by-zero in `KBatchedAnimUpdater.CleanUp`; that prevents
accepting this abort as clean. The caught-false case is not reached. Computer
use captures the error screen and explicitly selects Quit to Desktop; private
quit observers preserve failed status and record that UI call stack. All 13
original files restore exactly. Frozen source/package/report/screenshots remain
under `local/colony-runs/load-rejection-windows-v32`. V32 does not establish a
correct pre-Game loader and scene teardown protocol; zero cell count is not
proof of native teardown.

The v6 correction chooses one completion kind at the first failure. An existing
Game requires its exact `Game.DestroyInstances` body. A pre-Game failure instead
captures the live SaveLoader, its backend scene handle and the existing animation
updater. It requires both that loader's actual inherited `KMonoBehaviour.OnDestroy`
and the matching `SceneManager.Internal_SceneUnloaded` body to return normally,
with no outstanding selected-load or OnSpawn invocation. Each body has entry,
return and normal-path postfix witnesses plus a one-use finalizer token. A fresh
authority check precedes a backing-field compare/exchange that clears only the
exact dead loader reference. A late Game, failed cleanup or foreign/successor
reference invalidates completion. The completion kind never falls back when an
existing Game disappears.

Only the authenticated pending pre-Game attempt can skip `DoGridProcessing` on
its captured updater's actual zero-sized controller grid. It returns false from
that query while retaining the rest of native animation processing. It does not
clear/reinitialize controllers, initialize Grid or call native shutdown. The
original Game retirement path remains separate. All 72 Windows and Linux
host/declaration checks pass; those checks do not invoke Unity lifecycle or
install Harmony. Installation also pins the inspected Unity Core and Harmony
assemblies. One older private shutdown diagnostic still says "actual Game
teardown"; the typed fields above determine the actual completion protocol.

Fresh Windows v33 passes the complete private rejection-only gate on actual
ONI 744825/Mono 6.13. The ordinary unmarked native save reconstructs the exact
unowned reservoir and preserves 12.5 kg oxygen, 310 K and native disease
identity/count 12345 bit-for-bit. Its separate paused animation interval executes
and returns from `DoGridProcessing` three times on a positive 35-by-15 grid,
with zero suppressions. The complete 45-method, 15-native-key and four-antecedent
counter maps calibrate against real original bodies/dispatch without observation
errors.

The existing-Game direct schema-99 rejection observes exact view retirement,
unchanged stored packet/dimensions, no selected-load or native initialize/load/
start/save dispatch, one frontend request and clean actual Game teardown with
one natural shutdown. The early OnSpawn rejection performs no native
initialization/shutdown/load/start/save and completes through its exact loader
and backend scene. The ordinary-header/invalid-zlib candidate reaches one native
initialization and the outer loader's own caught-failure shutdown, while inner
world/object/start/save/fallback continuations remain absent. Both pre-Game
epochs observe their exact original bodies, normal-path certificates, released
call holds and cleared dead singleton; the empty-grid guard runs three times
per epoch. Each of the three completed attempts releases fatal state and reaches
the real frontend for more than two seconds.

All frozen test save files remain unchanged. There are no Unity errors or native
crash markers, including the later observed frontend interval and manual native
MainMenu Quit. The runner exits zero and restores all 13 settings/world-generation
files; a separate before/after inventory verifies all 283 personal save files
and membership. Source, package, native call evidence and five actual Unity PNGs
remain private under `local/colony-runs/load-rejection-windows-v33`; the final
rejection report SHA256 is
`8386b595bd4a089b9d3a03ad296cfb0116ee0622b682a3a05e01078fefa11864`.
The exact source manifest is `77d273a430ea7786ab3e5f81dc64083723b3cc32b7070b83acb8057508e5140c`.
This verifies dormant Windows cancellation only. It does not authorize owned
restore, a native/custom writer, ordinary owned saves, Linux Unity behavior or
first-playable acceptance.
