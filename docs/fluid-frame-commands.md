# Unhooked owned-fluid frame commands

`ExperimentalFluidFrameCoordinator.PrepareCommands` stages an ordered sequence of
closed immutable `FluidBatchCommand` objects into one `FluidTransaction`. Native
message wrappers and internal transfers share the same staged cells, geometry,
containers and validation. `PrepareBatch(IEnumerable<FluidBatchMessage>, ...)`
remains supported and wraps its messages through the same command path. The
4,096-command bound counts every command, including internal moves that produce
no callback record or native intent. Enumeration failure, invalid late commands,
or retired callback registrations discard the complete unpublished transaction.
Recursive preparation is rejected; disposal during enumeration cannot leave a
writer reservation behind.

Factories are `NativeMessage(message)`, `CellToContainer`, `ContainerToCell`, and
`ContainerToContainer`. Transfer factories have bulk-mass and selected-species
overloads; `AllCellToContainer`, `AllContainerToCell` and
`AllContainerToContainer` also move massless disease/radiation metadata. Container
endpoints take full `FluidContainerHandle` values. There are no create/retire
commands, caller delegates, actor operations or ID-only container lookups in this
command type. Capacity, phase, volume and stale/foreign-handle checks use the
existing Core transaction rules. Internal moves preserve supplied/removed/heat
ledgers and retain every constituent, energy, momentum, disease and radiation
quantity under those rules. They do not manufacture native `Storage` packets.

The existing writer reservation, native-geometry acknowledgement, immutable
callback outbox, paired Core/native publication and explicit consumption
acknowledgement apply unchanged. Native operations are exposed only after the
entire command sequence has passed validation. A late failure therefore exposes
no partly staged native batch. After native dispatch begins, ambiguous external
effects still require explicit native recovery rather than a rollback claim.

`PreparePresentation(batch, model)` optionally prepares the immutable mixture view
before native dispatch. It rejects duplicate preparation and incompatible
catalogues. Core's sealed `MixturePresentationPublication.Prepare(source, model)`
captures the frame before returning an object with exact read-only `Source`,
`Model` and `Frame` references. The prepared batch and its published companion
expose that same object as `PreparedPresentation`; `Presentation` and
`PresentationModel` are derived convenience getters. Capture failure leaves the
authority and old publication intact and permits retry. Snapshot allocation or
projection failure retains the successful prepared presentation for the same
batch. No fallible presentation recapture follows the Core commit in this path.

Legacy coordinator callers may omit presentation. Actor bridges must require
`PreparedPresentation` and bind its matching completed source rather than sample
independent providers or call a fallible presentation publisher after committing
custom state. This protocol itself does not execute an actor callback, suppress
native sources, account for pump work or power, or integrate ONI saves. External
callback side effects followed by exceptions remain ambiguous and are never
automatically replayed.

`MixturePresentationRuntime.BindCompletedSource` accepts the completed prepared
companion through a pure main-thread read callback. Binding requires an initial
completed publication and a clean presentation session. Once per Unity frame it
reads one immutable companion and pins its exact source, model and numeric view;
worker leases never invoke the callback. Null, foreign-colony, regressed and
reentrantly replaced publications fail without replacing the previous view.
Independent publishers and inventory adapters cannot join a bound source. A
reset, including explicit fixture retirement, invalidates earlier worker leases
and releases the callback.

User appearance changes can replace only visual options on a bound model.
`IsVisualVariantOf` recognizes models related through `WithVisualOptions`, which
preserves material, palette and breathing definitions. When the authority supplies
a new policy model, the runtime reapplies only the chosen visual options to that
new model. Physical quantities and warning policy therefore remain tied to the
completed authority publication. Numeric capture occurs before publication;
human-readable labels still use lazy formatting and are not allocation-free.

Windows correctness checks pass 88 owned-snapshot/protocol cases and 27 mixture
presentation cases against the exact licensed ONI declarations and the preserved
native binary `91acc96964c40a33a4e60930edafc36ec9f628aecd84e452326bdfef255ca3e5`.
Twelve new command groups cover ordered native/container interleaving, every
transfer form, minimum-positive trace and metadata-only movement, complete late
rollback, combined limits, preparation/snapshot retry, immutable paired views and
consumption acknowledgement. Three Core presentation groups check prepared
source/model/frame identity, failed-model retry and old-frame worker coherence.
These are synthetic managed correctness checks, not actual native actor or
storage takeover, a performance result, or first-playable acceptance.

Six further runtime groups pass against the current Windows native build (94
owned-snapshot/protocol/runtime groups total). They check prepared view identity,
once-per-frame and worker capture, failed-source retention, independent-writer
rejection, visual-only policy rebasing, reentrant reset, stale leases, clean
binding and callback release. Local evidence records the exact native and source
hashes for that run.
