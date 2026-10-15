# Owned container inventory contract

`ColonyFrameSnapshot.Containers` is authoritative custom storage in the same
immutable frame as world cells. `FluidTransaction` stages both endpoints before
one publication; no native Storage, conduit, actor or message interception is
activated by these APIs. They provide conserved inventory transactions, not pump
work, device reactions, spatial routing or a fluid timestep.

Each `FluidContainerDefinition` explicitly declares volume in cubic metres,
maximum admitted mass in kilograms, and a nonempty gas/liquid phase mask. Mass
capacity is a gameplay admission limit, not a physical pressure limit. Volume
validation uses the current explicit constant-density liquid catalogue and
requires positive headspace whenever gas exists. Vessel strength, compressible
liquids and liquid mechanical pressure remain outside this model.

A `FluidContainerHandle` contains the persisted colony ID, caller-chosen stable
container ID, ordinal generation, and a unique allocation token. Transactions
may return provisional handles, but only a successful publication makes them
visible in an authoritative snapshot. An aborted or stale transaction can reuse
an unpublished ordinal later; its unique token prevents the escaped old handle
from matching that allocation. Independent colonies have different colony IDs.
Save/restore retains the full identity for intentional rebinding within the same
colony. Consumers must resolve the full handle rather than infer identity from an
ID string or native object index.

Creation starts empty. Bulk and constituent-selective `TransferCellToContainer`,
`TransferContainerToCell`, and `TransferContainerToContainer` validate source and
destination before staging changes. Their `TransferAll*` counterparts also move
massless disease/radiation metadata. Internal transfers never change supplied or
removed ledgers. `Totals()` includes world cells and containers; `WorldTotals()`
explicitly excludes containers. Selected transfers use the source container's
actual volume when reconstructing temperature.

Capacity, phase and thermodynamic validation failures occur before staged writes
and leave the transaction recoverable. If allocation/insertion fails after staged
writing begins, the transaction closes and cannot publish a partial internal
transfer. The same guarded writes protect legacy cell transfers, displacement,
geometry and external source/sink/heat ledgers. No authority snapshot changes on
that failure path.

`RetireContainer` requires empty mass and metadata. `RetireContainerToCell` and
`RetireContainerToContainer` explicitly evacuate the complete inventory and
retire it atomically. An overfilled, forbidden-phase, solid or stale destination
leaves the staged source and its allocation intact. Volume/capacity changes
require conservative evacuation and a new allocation; definitions are immutable.

```csharp
using var tx = authority.BeginTransaction();
var buffer = tx.CreateContainer("pump-buffer:42", new FluidContainerDefinition(
    volumeCubicMetres: 0.2, maximumMassKg: 2,
    allowedPhases: FluidContainerPhases.Gas));
tx.TransferCellToContainer(sourceCell, buffer, "oxygen", 0.25);
var completed = tx.CommitFrame(completedFrame, simulationTimeSeconds);

var view = presentationModel.CaptureFrame(completed);
var bufferView = view.SampleContainer(buffer);
```

`CaptureFrame` obtains owned containers directly from that completed snapshot,
with the same revision, model and simulation time as its cells. Handle-based
presentation lookup rejects foreign, retired, replaced and unpublished
allocations. Separate display-only fixture samples remain supported, but cannot
override an owned container ID. `MixtureInspection.RegisterContainerOwner`
binds an ONI object to its full allocation handle without adding a callback
provider. Retired/replaced/foreign bindings explicitly show unavailable; an
ID-only independent binding cannot adopt an owned allocation, and late legacy
or old-handle cleanup cannot erase its replacement. Staging all native
device/actor inventories still requires production adapters.

Private checkpoint format 3 includes colony identity, allocation high-water,
active handles, definitions and inventories. Restore validates all records,
capacity/phase/volume constraints, catalogue identity and whole-colony ledgers
before returning an authority. Version 2 is strictly rejected; old private
evidence remains separate. The live snapshot companion/callback outbox and
actual ONI manual-save/autosave envelope are not integrated by this format.

`ReservePublication(expectedPublication)` pins one exact immutable publication
without creating a fluid transaction, revision or companion. It excludes Core
commits and prepared-writer reservations until disposal; readers and optimistic
staging remain available. `SaveCheckpoint` serializes its pinned source and
revalidates the lease before and after serialization. Releasing an old lease
cannot release a successor. Companion/native-buffer lifetime remains the
caller's responsibility.

The original `OwnedReservoirCheckpointV1` data codec retains canonical Core v3
bytes and exactly two declared GasReservoir owners, including empty owners.
Every nonempty allocation must have an exact owner; empty unbound allocations
are preserved. It rejects custom world-cell inventory and validates native/Core
identities, catalogue/build contracts, construction values and the declared
idle protocol. Its idle declaration does not establish live save eligibility.
No native save/load hook or serialization permit is enabled by this codec.
See the [checkpoint checks](../tools/OwnedReservoirCheckpointTests/README.md)
for the binary contract and remaining persistence work. Windows and isolated
WSL Linux checks pass 88 Core groups and 19 codec groups; six Core groups cover
publication reservations. These Linux checks do not run Unity.

Windows and isolated WSL Linux correctness evidence passes Core 88, presentation
27, owned snapshot/runtime 94 and inspection 27 scenarios.
New cases cover aborted/stale-handle ABA,
cross-colony rejection, metadata-only evacuation, minimum-positive traces,
fractional disease, capacity/phase/volume rollback, large-container selective
transfers, immutable prior frames, versioned continuation, exhausted allocation
counters, checksum-valid record corruption, and failures after individual staged
writes that prevent a caught exception from being followed by publication.
The fault-injection seam is internal, bounded and local to one transaction;
normal pre-write validation remains recoverable. Private source hashes are in
`local/owned-containers-windows-evidence.json`. These are managed protocol tests;
they do not establish native actor/storage takeover or first-playable readiness.

Actual Windows v17/v18 presentation fixtures move six complete world-cell parcels
into six owned containers and bind real pipe/reservoir/bottle/suit/vent/machine
objects using full handles. Both runs verify a selective oxygen transfer,
custom checkpoint, stale allocation unavailability, safe replacement without
stale warnings, complete retirement and whole-colony/external-ledger balances.
V18 also compares nine runtime views directly to the completed Core state.
Independent private checkpoint decoders and actual screenshot reviews pass;
see [mixture presentation](mixture-presentation.md) for scope and evidence paths.
