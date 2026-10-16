# Coupled-world native/managed boundary

The coupled gas/liquid `FluidWorld` now has a versioned C ABI and managed owner.
It is an explicit boundary to the **existing closed-domain reference**, not the
finished production timestep. The default ONI DLL and gas ABI do not select it.
Vacuum/free surfaces, immiscible geometry, phase transitions, dissolved gases,
disease growth/decay, radiation-dose physics, persistence, performance and authoritative
colony publication remain required.

The excluded `mixed_atmospheres_coupled` shared target exports only the
`ma_coupled_*` interface in [coupled_api.h](include/mixed_atmospheres/coupled_api.h).
The excluded `ma_coupled_api_tests` target exercises that binary. Neither is
added to default CTest or enabled by the game probe.

## Ownership and conserved initialization

ABI version 1 explicitly selects `MA_COUPLED_CLOSED_REFERENCE_MODEL`. Its
96-byte configuration includes dimensions, cell size/depth, gravity, diffusion,
viscosity, conduction, timestep/outgoing-fraction limits and preconditioner.
Unknown models, flags, structure sizes and versions reject. The backend uses
the existing standard constant-property registry; definitions expose IDs,
phase, molar mass, Cv, energy offset and condensed density. These are reference
properties, not a complete calibrated production catalogue.

Creation stages all row-major cells and their flat constituent mass array
before returning an owner. It imports supplied internal U and x/y momentum
directly. It neither initializes temperature nor converts through `P/M*M`.
The added `FluidWorld::set_conserved_cell` keeps those momentum bits; the
velocity-based setter remains available for its existing callers. Inputs remain
caller-owned and are copied. Walls must be empty; every other cell must contain
gas or completely filled liquid. Unsupported vacuum and partial pure-liquid
fills reject rather than receiving seed gas or a fitted density.

`ma_coupled_advance` returns a new owning handle. It clones the source, completes
every substep, captures derived data and prepares diagnostics before publication.
Failure destroys the candidate, leaves the source unchanged, returns a null
candidate and preserves the result buffer. Old handles remain independently
readable after successful forks or destruction of a child.

`ma_coupled_copy_snapshot` copies all masses, U, momentum, pressure, temperature,
liquid fraction, wall flags and aggregate energy components from one completed
revision. It checks every capacity before writing any output. Its prepared
native snapshot makes no EOS calls during copy-out. Revision counts are local
to a branch, not globally unique colony frame tickets; a coordinator must bind
world identity/session and its completed frame separately.

Handles permit concurrent reads; callers must keep buffers distinct and valid
and must not race destruction. The independent per-thread error buffer is
bounded to 2,048 bytes. C++ exceptions remain inside the C boundary.

## Managed binding

`MixedAtmospheres.Core.CoupledSimulationWorld` owns handles through `SafeHandle`,
checks ABI layouts, and serializes each owner's calls/disposal. It copies
initialization dictionaries and configuration, and returns immutable material
definitions and managed snapshot objects. Snapshots retain their data after
native owners are disposed. U stays separate from momentum and kinetic energy;
there is no conversion through a rounded total-energy scalar.

`Create` admits a complete world. `Advance` returns `CoupledAdvance`, containing
an independently disposable `World` and its matching diagnostics. It never
changes the source. `CaptureSnapshot` returns one completed state. Cell size,
volume and material definitions belong to the immutable world configuration;
do not combine these readings with a different material catalogue or geometry.
No runtime mutation, save or game takeover API is implied by this binding.

Build and exercise the explicit boundary:

```text
cmake --build build --config Release --target ma_coupled_api_tests
build/Release/ma_coupled_api_tests.exe
dotnet run --project tools/CoupledWorldTests -c Release -- <absolute coupled library path>
```

The sparse metadata controls additionally use the explicit
`ma_passive_transport_tests` target. On Linux the executable/library normally reside directly in the build directory.
Use the actual target paths for the selected generator. The managed test host
loads the supplied absolute library path, pins its import resolver and prints
the library SHA256 before tests. The game probe's resolver is unchanged.

## Verification and preserved failures

The initial boundary's six native groups pass Windows/MSVC and WSL Linux/GNU. Its original six managed groups pass
both platforms under .NET 10.0.402, building the Core binding for .NET Standard
2.1. The controls cover:

- ABI layouts and every standard registry definition.
- Exact m/U/P import, caller-input isolation and retained snapshot ownership.
- A four-cell 2D wet-air advance, constituent/energy conservation and positive
  helium traces at `1e-30 kg` per initial cell.
- Independent ideal-gas headspace pressure and liquid-volume readings.
- A whole-call budget failure after a separately confirmed valid first substep,
  unchanged sources/results and preflight rejection of undersized output.
- Empty walls, unsupported inputs, independent disposal and concurrent managed
  inspection.

The import control uses actual registry water density **997 kg/m3**. Initial
mass bits `40592fde816581af` and momentum bits `3fd999999999999c` give old
`(P/M)*M` bits `3fd999999999999b`; direct conserved import preserves the supplied
momentum. The native driver prints all three operands before its assertions.

Earlier outcomes remain recorded. R1 Windows native compilation passed; Linux
linking lacked the pinned shared-library sysroot flag. Both managed test-host
builds failed on two C++-style numeric literals; both Core assemblies compiled.
R2 corrected those build issues. Its first native drivers passed metadata then
rejected an invalid rounding-loss control; later native groups were unreached.
R3 rejected a replacement operand calculation that mistakenly assumed water
density 1000. R4 corrects only the test operands and records them before checks;
the ABI implementation, physics, managed code and acceptance bounds are
unchanged. The native driver repeats are labelled explicitly.

The six managed groups passed against pinned R2 libraries. The six native groups
pass R4. Source comparison proves the only intervening implementation/test
difference is the native driver; the ABI and all managed sources match those
passing managed runs. There is no repeated managed run or claim of replacement
first outcomes. Linux's shared binary hash is unchanged between R2/R4; Windows
builds retain their separate binary identities.

Private source copies, build/compiler audits, executable/library/assembly hashes,
raw EOF/close records and completed reviews are under
`local/occupied-staggered-validation/coupled-abi-{windows,linux}-r1` through `r4`.
Limits are 180 seconds per build, 30 seconds per runtime invocation and 128 KiB
per raw pipe. All passing captures have exit zero without timeout/cap/capture
errors. The new wrapper/driver use warnings as errors; upstream solver FP
settings are unchanged. Linux's ten dynamic exports are all `ma_coupled_*`.
There is no independent agent implementation review or new game run.

## Sparse disease and radiation metadata

The closed reference now carries sparse passive quantities through actual
remap and gas exchange. Disease identities retain the uint32 bits of their
nonzero signed native hashes. Radiation uses the separate identity
`0x100000000`; this is the existing passive native quantity, not energy or dose.
It contributes no mass, pressure, heat capacity or force. Growth, decay,
sterilization and radiation absorption/dose remain unimplemented.

Advection uses the same retained donor and incoming face weights as material.
Gas exchange carries `exchanged_gas_mass / total_cell_mass` of the cell's
metadata, matching Core's well-mixed bulk-parcel policy. This is not a
phase-specific biological model. Viscosity and heat conduction transfer no
metadata. An independent paired-world check confirms that adding metadata
changes none of the resulting constituent masses, U or x/y momentum.

Closed whole-call totals are checked separately for every identity using
compensated sums and a 256-binary64-epsilon relative bound, without an absolute
or unit floor. Malformed/negative/nonfinite quantities, unknown tags, positive
underflow and metadata on solid/vacuum/occupied-1D cells reject. The existing
whole-call rollback includes these quantities. No quantity is discarded to
make a failed step pass.

The optional passive ABI extension has its own version 1 check. The base ABI
and structure layouts remain version 1. `ma_coupled_create_full` consumes sorted
per-cell sparse rows; `ma_coupled_passive_count` and
`ma_coupled_copy_full_snapshot` export the matching completed rows. All output
capacities are checked before any write. The base copy function refuses a
metadata-bearing handle rather than silently returning an incomplete state.
Limits are 65,536 disease identities plus radiation per cell and 8,000,000
sparse records per ABI import/export. These bounds do not establish production
memory or speed acceptance.

Managed initialization uses `WithPassiveMetadata`; dictionaries are copied.
Managed cell snapshots expose immutable `DiseaseCounts` and `RadiationQuantity`
from the same native revision as masses and physical readings. Signed hashes,
independent forks and snapshots retained after disposal are preserved. Missing
extension support rejects metadata initialization explicitly.

The current first Windows/MSVC and WSL Linux/GNU runs each pass six new native
passive groups, the six previously passing base ABI groups, and eight managed
groups (the original six plus two metadata groups). New checks include analytic
quarter-donor and split/reversed-face transfers, quantities at `1e-60`, a
two-compartment gas-exchange solution, physical noninterference, whole-call
rollback, complete sparse export, output-buffer atomicity, signed hashes and
actual four-cell 2D metadata arrival. The earlier failed driver revisions and
historical failed physics/performance gates are not replayed.

Private evidence is under
`local/occupied-staggered-validation/coupled-passives-{windows,linux}-r1`.
Both first builds and all six runtime invocations pass with frozen source,
pre-invocation compiler/library/assembly audits, exit zero and clean raw EOFs.
The helper, wrapper and control sources use warnings as errors; upstream FP
settings remain unchanged. Linux exports fourteen `ma_coupled_*` functions.
The same 180-second build, 30-second runtime and 128-KiB pipe limits apply.
There is no new game, independent agent or performance acceptance.

## Remaining integration work

The frame coordinator is not connected to this owner. It still needs matching
material/geometry admission, game metadata routing, transactions/mutations,
multi-world atomic publication and presentation binding. Fork/capture allocation
and complete-frame copying also require production performance work. This
boundary does not repair the older remap, vacuum-front or performance failures,
which were not replayed. It establishes no first-playable or release gate.
