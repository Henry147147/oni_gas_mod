# Coupled `FluidWorld` C ABI and managed owner

The coupled ABI exposes the closed mode of [`ma::FluidWorld`](FLUID_WORLD.md) to
managed code. It is a **validation reference**; the production solver is
`MacClosedWorld` (2026-10-18 decision, [SOLVER.md](SOLVER.md)). It has no ONI
frame hook: in game only the `coupled-presentation` validation mode loads it, for
a six-cell synthetic scene in an otherwise native colony. Vacuum, free surfaces,
immiscible geometry, phase change, disease growth, radiation
dose, persistence and production performance are unsupported.

`mixed_atmospheres_coupled` (`native/src/coupled_api.cpp`, excluded from the
default build) exports only the `ma_coupled_*` functions of
[coupled_api.h](include/mixed_atmospheres/coupled_api.h). The managed binding is
`MixedAtmospheres.Core.CoupledSimulationWorld` (`CoupledNativeMethods.cs`).

## Ownership and conserved initialization

- **Versioning.** `ma_coupled_abi_version()` = 1, model
  `MA_COUPLED_CLOSED_REFERENCE_MODEL`. The 96-byte configuration holds
  dimensions, cell size and depth, gravity, diffusion, viscosity, conduction,
  timestep and outgoing-fraction limits and the preconditioner. Unknown models,
  flags, sizes and versions reject. Materials come from the standard
  constant-property registry (ID, phase, molar mass, Cv, energy offset,
  condensed density), not a calibrated catalogue.
- **Create** stages every row-major cell and the flat mass array before
  returning an owner. U and x/y momentum are imported directly
  (`FluidWorld::set_conserved_cell`), never via temperature or `(P/M)*M`: with
  water at 997 kg/m3 that round trip changes the momentum's last bit. Walls must
  be empty; other cells must hold gas or be full of liquid.
- **Advance** (`ma_coupled_advance`) clones the source, completes every substep,
  captures readings and diagnostics, then returns a new owning handle. On failure
  the source is unchanged, the handle is null and the result buffer untouched.
  Old handles stay readable after forks or child destruction.
- **Snapshot** (`ma_coupled_copy_snapshot`) copies masses, U, momentum,
  pressure, temperature, liquid fraction, walls and energy components of one
  revision, checks every capacity before writing and makes no EOS calls.
  Revisions are per branch, not global frame tickets.
- **Threads and errors.** Concurrent reads are allowed with distinct buffers;
  callers must not race destruction. Per-thread error text is at most 2,048
  bytes; exceptions never cross the boundary.

`CoupledSimulationWorld` owns handles via `SafeHandle`, checks layouts,
serializes each owner's calls, copies inputs and returns immutable definitions
and snapshots that outlive the owner. `Advance` returns a `CoupledAdvance` with
an independently disposable `World`; `CaptureSnapshot` returns one completed
state. Readings belong to the world's own configuration and catalogue.

## Sparse disease and radiation metadata

Disease identities are the uint32 bits of ONI's nonzero signed disease hashes;
radiation is identity `0x100000000`, ONI's passive per-cell quantity (not energy
or dose) with no mass, pressure or heat capacity. Advection uses the material
face weights; gas exchange carries `exchanged_gas_mass / total_cell_mass` of the
metadata; viscosity and conduction carry none. Metadata never changes the
physical result. Totals are checked per identity within 256 epsilon (no absolute
floor). Malformed, negative, nonfinite, unknown or underflowing quantities and
metadata on solid, vacuum or 1-D cells reject; rollback includes metadata.

The passive extension (`ma_coupled_passive_abi_version()` = 1) adds
`ma_coupled_create_full`, `ma_coupled_passive_count` and
`ma_coupled_copy_full_snapshot`; the base copy refuses a metadata-bearing handle.
Limits are 65,536 disease identities plus radiation per cell and 8,000,000
records per import or export. Managed: `WithPassiveMetadata`, `DiseaseCounts`,
`RadiationQuantity`. Growth, decay and dose are not modelled.

## Closed-reference colony frame coordination

`CoupledColonySimulation` (Core) stages an imported colony against the closed
catalogue and geometry, then keeps one persistent native owner per world.
Unsupported species, vacuum or metadata-only cells and unrelated pressure
entries reject; pure-liquid cells need an explicit mechanical pressure.

`AdvanceFrame` accepts exactly the next frame number, advances every world in
sorted ID order under one colony-wide attempt budget, and prepares all
candidates, captures and the projection before one writer reservation and
commit. Any failure keeps the current frame and reports how many worlds had
completed. At most one retired owner generation is kept; frames hold no native
handles.

`CoupledColonyFrame` binds native snapshots, diagnostics and a read-only Core
projection to one frame. The projection stores scalar total energy, so U derived
from it can round differently; each difference is in
`InternalEnergyProjectionErrorsJoules`, bounded by 64 epsilon times the larger of
|U| and |K|. The projection never reinitializes physics and cannot restart it.

**Presentation.** `ReadMaterialCatalog()` gives the catalogue; a matching
`presentationModel` passed to `CoupledColonySimulation.Create` builds
`CoupledColonyFrame.Presentation` before the commit, so it cannot publish
separately. `ReadThermodynamicCell` uses native U, momentum, mass, T and p
(liquid volume m/rho, gas headspace the remainder). `WithVisualOptions`
rebuilds from the retained `PhysicalSource`. See
[mixture presentation](../docs/mixture-presentation.md).

## Conserved physical mutation preparation

The mutation extension (`ma_coupled_mutation_abi_version()` = 1) adds
`ma_coupled_prepare_mutation`: an immutable child from an exact source revision
and 1-4096 sorted, unique cell edits, each with actual masses, **stored U**,
momentum and metadata. Unedited cells, time, settings and walls are kept; the
revision increments once. Geometry changes and unsupported states reject.

The caller declares signed external changes for every species, disease and
radiation identity, transported material energy (U+K+PE), shaft work, heat and
impulse. Each actual change of the edited cells must match within 128 epsilon of
its own operands (no unit floor), and a nonzero declared change needs a nonzero
actual change of the same sign, so tolerance cannot certify an erased transfer.
Diagnostics keep U, K and PE changes, residuals and bounds.

This is raw preparation: it does **not** validate entropy, pumps or reactions,
or reserve power or material held elsewhere. Core exposes `CoupledCellEdit`,
`CoupledMutationBudget` and
`CoupledSimulationWorld.PrepareMutation(expectedRevision, edits, budget)`,
returning a `CoupledMutation` that owns the child. Stale revisions and later
failures leave the source unchanged.

## All-world frame transactions and boundary accounts

`CoupledColonySimulation.AdvanceFrame(expectedSource, nextFrame, seconds,
mutations, attemptBudget)` requires the current frame. Each
`CoupledWorldMutation` names one world, its edits and budget; unknown or
duplicate worlds and stale frames reject. Each world prepares its mutation,
advances and captures (a mutated world's revision rises by two, others by one);
everything is prepared before one commit, and any failure leaves the previous
publication and accounts. A rejected edit is never counted later.

`CoupledColonyFrame.BoundaryChanges` holds only this frame's
`CoupledBoundaryReceipt`s. `BoundaryLedger` accumulates declared mass, material
energy, shaft work, heat, impulse, disease and radiation, dropping zero-net
identities (65,536 disease identities). `CoupledAccountQuantity.Binary64Units`
is the exact integer sum of declared binary64 operands in units of 2^-1074;
`TryGetRoundedValue` rounds nearest-even and returns false on overflow. These
accounts are bookkeeping only. The companion is marked
`IsReadOnlyPhysicalProjection`, so inventory transactions, legacy ledgers and
Core checkpoints reject for it. There is no native restart.

## Build and verify

```text
cmake --build build --config Release --target ma_coupled_api_tests ma_passive_transport_tests ma_coupled_mutation_tests
build/Release/ma_coupled_api_tests.exe
dotnet run --project tools/CoupledWorldTests -c Release -- <absolute coupled library path>
python native/tests/review_coupled_mutations.py capture.stdout
python native/tests/review_coupled_boundary_ledger.py coupled.stdout
```

With Ninja on Linux the executables and `libmixed_atmospheres_coupled.so` are in
the build directory itself. None of these targets is in CTest. The managed host
pins the given library and prints its SHA-256. The reviewers read saved output
only: exact-rational balances and ideal-gas/caloric checks for mutations, and
exact accounts (subnormals, ties, cancellation, overflow) for the ledger; both
reject altered data.

## Status

- Native: 6 base, 6 passive and 7 mutation groups pass on Windows/MSVC and WSL
  Linux/GNU; Linux exports only `ma_coupled_*`.
- Managed (.NET 10, Core on .NET Standard 2.1): 29 coupled, 89 Core and 41
  presentation groups pass on Windows and WSL Linux.
- Game: one Windows ONI run passes four hover/screenshot cases on the six-cell
  scene (original and changed mixture, a 0.125 kg oxygen inlet with a 4,096 J
  heat account, colour-only recapture); see
  [mixture presentation](../docs/mixture-presentation.md#completed-colony-transactions-on-windows).
  No Linux game run exists.

Missing for any game use: a connection from `ExperimentalFluidFrameCoordinator`,
device/container/power reservations, native dispatch, material and phase
coverage, save/reload, and fork/capture performance. The `FluidWorld` remap,
vacuum and performance gates apply too.
