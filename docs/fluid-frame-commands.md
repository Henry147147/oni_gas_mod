# Unhooked owned-fluid frame commands

`ExperimentalFluidFrameCoordinator.PrepareCommands` stages an ordered sequence
of closed immutable `FluidBatchCommand` objects into one `FluidTransaction`.
Native message wrappers and internal transfers share the same staged cells,
geometry, containers and validation. `PrepareBatch(IEnumerable<FluidBatchMessage>, ...)`
wraps its messages through the same path. The coordinator is not connected
to ONI's native message flow; its only game caller is the private
[reservoir bridge](owned-reservoir-bridge.md) actor session.

## Commands

- Factories: `NativeMessage(message)`, `CellToContainer`, `ContainerToCell` and
  `ContainerToContainer`, each with bulk-mass and selected-species overloads.
  `AllCellToContainer`, `AllContainerToCell` and `AllContainerToContainer` also
  move massless disease and radiation metadata.
- Container endpoints take full `FluidContainerHandle` values. There are no
  create/retire commands, caller delegates, actor operations or ID-only lookups.
- Capacity, phase, volume and stale/foreign-handle checks follow the
  [container rules](container-inventories.md). Internal moves keep the
  supplied, removed and heat ledgers and every constituent, energy, momentum,
  disease and radiation quantity. They create no native `Storage` packets.
- At most 4,096 commands per batch, counting internal moves that produce no
  callback record or native intent.
- Enumeration failure, an invalid late command or a retired callback
  registration discards the whole unpublished transaction. Recursive
  preparation rejects, and disposal during enumeration leaves no writer
  reservation behind.

The writer reservation, native-geometry acknowledgement, immutable callback
outbox, paired Core/native publication and explicit consumption
acknowledgement apply unchanged. Native operations are exposed only after the
whole sequence validates, so a late failure exposes no partly staged batch.
After native dispatch begins, ambiguous external effects need explicit native
recovery; there is no rollback claim, and failed callbacks are never replayed
automatically.

## Prepared presentation

`PreparePresentation(batch, model)` optionally prepares the immutable mixture
view before native dispatch. It rejects duplicate preparation and incompatible
catalogues. `MixturePresentationPublication.Prepare(source, model)` captures
the frame before returning an object with exact read-only `Source`, `Model` and
`Frame`. The prepared batch and its published companion expose that object as
`PreparedPresentation`; `Presentation` and `PresentationModel` are derived
getters.

- A capture failure leaves the authority and the old publication intact and
  can be retried.
- A later snapshot allocation or projection failure keeps the prepared
  presentation for the same batch. No fallible recapture follows the Core
  commit.
- Legacy callers may omit presentation. Actor bridges must require
  `PreparedPresentation` and bind its completed source, never independent
  providers or a publisher called after committing custom state.

The protocol runs no actor callback, suppresses no native source, accounts for
no pump work or power and does not integrate ONI saves.

## Binding the runtime

`MixturePresentationRuntime.BindCompletedSource` accepts the prepared companion
through a pure main-thread read callback. Binding needs an initial completed
publication and a clean presentation session.

- Once per Unity frame the runtime reads one immutable companion and pins its
  exact source, model and numeric view. Worker leases never invoke the
  callback.
- Null, foreign-colony, regressed and reentrantly replaced publications fail
  without replacing the previous view.
- Independent publishers and inventory adapters cannot join a bound source.
- A reset, including fixture retirement, invalidates earlier worker leases and
  releases the callback.

Appearance changes can replace only visual options on a bound model.
`IsVisualVariantOf` recognizes models related through `WithVisualOptions`, which
keeps material, palette and breathing definitions. When the authority supplies
a new policy model, the runtime reapplies only the chosen visual options, so
physical quantities and warning policy stay tied to the completed publication.
Numeric capture happens before publication; labels are formatted lazily and
are not allocation-free.

## Verification

`tools/OwnedSnapshotTests` and the presentation suite cover ordered
native/container interleaving, every transfer form, minimum-positive trace and
metadata-only moves, complete late rollback, combined limits, preparation and
snapshot retry, immutable paired views and consumption acknowledgement. They
also cover prepared source/model/frame identity, failed-model retry, old-frame
worker coherence, once-per-frame capture, failed-source retention,
independent-writer rejection, visual-only policy rebasing, reentrant reset,
stale leases and callback release. They pass on Windows against the licensed
declarations. They are synthetic managed checks, not native actor or storage
takeover, performance or first-playable acceptance.
