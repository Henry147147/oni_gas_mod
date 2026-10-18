# Owned container inventory contract

`ColonyFrameSnapshot.Containers` is authoritative custom storage in the same
immutable frame as the world cells. `FluidTransaction` stages both endpoints of
a transfer before one publication. These APIs activate no native Storage,
conduit, actor or message interception. They provide conserved inventory
transactions, not pump work, device reactions, spatial routing or a fluid
timestep.

## Definitions and handles

A `FluidContainerDefinition` is immutable. It declares volume (m³), maximum
admitted mass (kg) and a nonempty gas/liquid phase mask. Mass capacity is a
gameplay admission limit, not a pressure limit. Volume validation uses the
constant-density liquid catalogue and requires positive headspace whenever gas
is present. Vessel strength, compressible liquids and liquid mechanical
pressure are outside this model.

A `FluidContainerHandle` holds the persisted colony ID, a caller-chosen stable
container ID, an ordinal generation and a unique allocation token. Provisional
handles become visible only after a successful publication. An aborted or
stale transaction may let a later one reuse an unpublished ordinal; the token
stops an escaped old handle from matching. Save and restore keep the full
identity for rebinding within the same colony. Consumers resolve the full
handle, never an ID string or a native object index.

## Transfers and retirement

- Containers start empty. Bulk and constituent-selective
  `TransferCellToContainer`, `TransferContainerToCell` and
  `TransferContainerToContainer` validate both endpoints before staging.
  `TransferAll*` also moves massless disease and radiation metadata. Selected
  transfers use the source container's actual volume to rebuild temperature.
- Internal transfers never change the supplied or removed ledgers. `Totals()`
  includes containers; `WorldTotals()` excludes them.
- Capacity, phase and thermodynamic failures happen before any staged write
  and leave the transaction usable. A failure after staged writing begins
  closes the transaction, so no partial transfer can be published. The same
  guard protects cell transfers, displacement, geometry and the external
  source, sink and heat ledgers.
- `RetireContainer` requires empty mass and metadata. `RetireContainerToCell`
  and `RetireContainerToContainer` evacuate everything and retire atomically.
  An overfilled, forbidden-phase, solid or stale destination leaves the source
  and its allocation intact. Changing volume or capacity needs evacuation and a
  new allocation.

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

`CaptureFrame` reads owned containers from the same completed snapshot as the
cells; handle lookup and inspection binding rules are in
[mixture presentation](mixture-presentation.md#inspection-bindings). Staging
native device and actor inventories still needs production adapters.

## Idle checkpoints and publication leases

`ColonyStatePersistence` format 3 stores the colony identity, allocation
high-water mark, active handles, definitions and inventories. Restore validates
every record, capacity/phase/volume constraints, catalogue identity and the
whole-colony ledgers before returning an authority. Other versions reject. The
format has no live snapshot companion, callback outbox or ONI save envelope.

`ReservePublication(expectedPublication)` pins one exact immutable publication
without a new transaction, revision or companion. Until disposal it excludes
Core commits and prepared-writer reservations; readers and optimistic staging
continue. `SaveCheckpoint` serializes the pinned source and revalidates the
lease before and after. Releasing an old lease cannot release its successor.
Companion and native-buffer lifetimes stay the caller's responsibility.

The `OwnedReservoirCheckpointV1` codec keeps canonical Core v3 bytes and exactly
two declared `GasReservoir` owners. Every nonempty allocation needs an exact
owner; empty unbound allocations are kept. It rejects custom world-cell
inventory and validates native and Core identities, catalogue and build
contracts, construction values and the declared idle protocol. That
declaration is not live save eligibility, and the codec enables no save/load
hook. See [the checkpoint checks](../tools/OWNED_RESERVOIR_TESTS.md#ownedreservoircheckpointtests-idle-checkpoint-codec).

## Verification

`managed/MixedAtmospheres.Tests` and `tools/OwnedSnapshotTests` pass on Windows
and Linux hosts. They cover stale-handle reuse, cross-colony rejection,
metadata-only evacuation, minimum-positive traces, fractional disease,
rollback, large selective transfers, immutable earlier frames, exhausted
allocation counters, corrupted checkpoint records and publication
reservations. Internal fault injection after each staged write checks that a
caught exception cannot be followed by publication. These managed tests do not
establish native actor or storage takeover.

Windows ONI presentation fixtures move six world-cell parcels into six owned
containers bound by full handle to real objects; see
[mixture presentation](mixture-presentation.md#display-fixtures-in-the-game).
The [reservoir bridge](owned-reservoir-bridge.md) uses these containers for
native-empty reservoirs.
