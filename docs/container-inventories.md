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
override an owned container ID. Binding actual ONI objects to these handles and
staging all device/actor inventories still require production adapters.

Private checkpoint format 3 includes colony identity, allocation high-water,
active handles, definitions and inventories. Restore validates all records,
capacity/phase/volume constraints, catalogue identity and whole-colony ledgers
before returning an authority. Version 2 is strictly rejected; old private
evidence remains separate. The live snapshot companion/callback outbox and
actual ONI manual-save/autosave envelope are not integrated by this format.

Windows correctness evidence passes Core 82 and presentation 24 scenarios.
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
