# Reconstructed momentum in the owning MAC timestep

The occupied-owner controls below pass; the strong-flow gate remains failed.
`MacClosedSettings` now selects limited
linear momentum reconstruction by default and retains an explicit donor-cell
reference. Both the reconstructed-material parcel branch and the donor-material
branch pass that choice into actual [MAC momentum/work transport](MAC_MOMENTUM_RECONSTRUCTION.md).
Pressure forces, midpoint volume sweeps, material reconstruction, gravity work,
the two compression halves and every existing physical bound are unchanged.
The interval aggregates correction/limiter/unresolved counts only after all
candidate stages succeed; failed intervals publish no state or clock.

The shared saved transport format now includes the selected mode, preheat
advected U, full source slopes/Courants/upwind J/Q/K, indexed dual edges and
every paired correction. Its separate reviewer reuses the exact geometric
component audit and the original nonuniform material-parcel audit. The donor
packet identities remain active for donor mode. Corrected mode additionally
checks the same physical J/K/U/species/passive/wall accounts after every work
transfer. The existing closed/gravity scientific source files, their inputs,
oracle calculations and error/energy limits are not changed to pass this mode.

Before the first build, six new owning controls are fixed:

1. Nonuniform wet gas/liquid/U/passive parcels on periodic 6-by-4 cells exercise
   both momentum components. Default owner transport equals explicit component
   replay from its actual pressure endpoint and parcel plan; source is immutable.
2. Nonuniform wet 6-by-5 cells, a solid corner and gravity 9.81 use donor material
   transport with reconstructed momentum. Original pressure/body/potential/
   signed internal-work and wall accounts must pass, with both components active.
3. A two-substep owning interval with compression disabled equals direct
   substep replay. Actual owner inventories/U/canonical J, time and revision are
   saved, not inferred from the requested candidate. Accepted correction counts
   match all saved substeps. Actual endpoint U+K and entropy reports retain their
   original operand-based bounds. Existing compression-owner controls separately
   exercise both compression halves with the new default.
4. Explicit donor momentum equals the original component reference. Changing
   only momentum mode changes actual J while leaving pressure, primal mass,
   all constituent/passive transfers and fixed wall reaction/heat identical.
5. Below-binary64 canonical x momentum on periodic 6-by-3 cells carries full
   volume/mass sweeps and nonzero full correction impulses through the owner.
   Unresolved primary momentum and material heat are reported; helium/passive
   traces remain positive. No projected cutoff or floor is introduced.
6. Invalid mode rejects in both primitive and owner construction. A two-step
   interval with a one-attempt budget fails after its first accepted candidate
   while the original time/revision/state remain unchanged. Reuse is deterministic.

The saved owner reviewer must derive all six actual pressure/transport cases,
nonuniform parcels, geometric corrections, gravity work and committed interval
from operands. It must reject altered geometry, transferred work, positive
trace inventory and committed clock. Original 78 prerequisite native groups
and all saved reviews remain required before one original strong-flow retry.
The strong gate remains failed until that complete check succeeds. No unchanged
failure replay, oracle substitution or error-bound relaxation is permitted.

Initial bounds: configure/build 180 seconds, new native 120 seconds and new
saved review 120 seconds. Existing native/review time bounds stay unchanged.
Expanded owning-step raw pipes are limited to 2 MiB each because they now retain
every full correction, predictor and paired work operand. Saved-review limits
use that cap only when a corrected owning record is present; donor fixtures
retain their previous caps. Geometric component records keep their 1 MiB cap.
First failures, source/compiler identities and raw results must be retained.

This is the occupied MAC owner, including its experimental native sealed-domain
consumer. It does not supply missing vacuum/free surfaces, general phase or
dissolved-gas transport, the full ONI material catalogue, complete gameplay,
persistence or production/performance acceptance. Real-game verification and
the first-playable colony gate remain separate requirements.

The first strict Windows build and all six new native owner groups pass. Its
first saved review rejects an unforwarded `MCONFIG` record in the outer
closed-step parser. That first result is retained. Forwarding the six new
record types to the transport reviewer changes no native operand or bound.

The next run passes the six owner groups and 105,831 saved checks on both
platforms. Windows completes 72 groups, then its weak acoustic and gravity-mode
prerequisites reject the final local derived-energy check. Those first failures
remain retained; Linux does not repeat the known shared failures and no strong
gate is run.

A separate strict diagnostic changes only failure logging. It captures a face
whose incoming transported K is 7,452,672.68 times its post-correction value.
Every original upwind, paired-correction and work-storage account passes at
128 epsilons of its actual operands. The final residual is -1.15519381e-52 J;
checking only the much smaller final magnitudes gives 2.46750124e-57 J, while
the sum of the validated stage bounds is 5.51684372e-50 J. Cancellation must not
erase already incurred roundoff. The diagnostic and exact-fraction review are
retained under `mac-momentum-owner-energy-diagnostic-windows-r1`, terminal SHA256
`6668fb648e2a28521f8dd38d98007995bb14d3a05a26dcfdb80411f6b94a9b4b`.

The repair carries the original upwind bound, each actual nonzero paired
correction's bound and the actual transported-work storage bound into the
final face account. Every term still uses the original 128-epsilon rule, with
no absolute floor. The full composed bound is saved and independently derived
from the same operands. Zero impulses must leave paired state and work exactly
unchanged. No momentum, transported energy, heat or material U is replaced to
force closure. Nonnegative heat/transported K, full-range sign checks and the
original global U+K, entropy and scientific refinement limits remain enforced.

The following Windows run retains identical actual state/work in the six
owner controls and passes 106,835 saved checks. Acoustic N16/N32 now complete;
N64 exposes negative actual transported K after a correction. This first
outcome is retained. Heat alone cannot certify withdrawal from a nearly
cancelled transported-energy sum.

The limiter now optionally borrows both actual transported-energy expansions.
Its coefficient is bounded by outward work capacity as well as heat. The same
64-ULP backoff loop checks both exact remaining work sums before accepting a
pair; no field is projected, clipped or repaired. Standalone heat-only callers
retain their existing reference behavior. Geometric transport always supplies
the work sums, and the saved reviewer derives this additional limit from
actual accumulated incoming energy and preceding work transfers.

A seventh group is added before its first build: a budget 1 - 2^-54 (whose
binary64 estimate is 1) must back off, including its reflected direction;
positive capacity at 0.75 times 2^-1200 must retain a nonzero full coefficient
and impulse; zero capacity must be an exact no-op; negative capacity rejects
immutably. All expansion terms are saved. A fifth corruption control reduces
available work below the recorded outgoing transfer and must reject. The
original 78 prerequisites plus these seven groups make 85 required native
groups before the original strong-flow retry. No scientific source, input,
refinement threshold or global energy/entropy bound changes for this repair.

The first execution of the new seventh group catches a latent backoff bug:
`nextafter(0.5, 0)` is outside the normalized significand interval. The backoff
now doubles that significand and decrements its full exponent when crossing
this boundary. It retains the same representable decrement and 64-step limit.
The failed first work-capacity result remains retained. An earlier preparation
was superseded before native execution after static review found a Python
reference-module name shadowed by a later local topology variable.

## Accepted owning controls and remaining strong-flow failure

All 85 native groups and saved reviews pass on Windows/MSVC and Linux/GCC.
The seven new owner groups pass 107,210 saved checks and reject all five
geometry/work/trace/clock/capacity corruptions per OS. Normal/transverse
advection and the original acoustic/gravity refinement limits pass. Linux
ASan/UBSan additionally passes 21 groups: six limiter, seven owner and eight
closed-step groups, including the original weak acoustic regression. Fatal
findings and leak detection are enabled; all native stderr is empty.
No independent agent review is claimed.

The single changed-source Windows strong-flow retry still fails. Sod completes
N64/128/256. Collision N128/N64 density, pressure and velocity error ratios are
0.88921485, 1.34608842 and 0.65677564; density and pressure fail the original
0.85 limit. A partial saved review passes 59,205 operand/EOS/conservation
checks across five completed profiles. This is not full strong-flow acceptance:
collision N256 and separation do not run, and Linux does not repeat the shared
failure. See [the current failed gate](MAC_RIEMANN.md).

Private immutable cohorts under `local/occupied-staggered-validation`:

- `mac-momentum-owner-windows-r6`, terminal SHA256
  `9b6bbbb251c55db4fc458879bfcb7128703f5bebf027ae0f6820b096b2c08178`.
- `mac-momentum-owner-linux-r6`, terminal SHA256
  `a6a3c9a2081567f3aea6f1f453d2b18e4c288a97f3809071ee34de9eccfa5070`.
- `mac-momentum-owner-linux-asan-r1`, terminal SHA256
  `10343f85d12652bee99ec38e4ca8dc38334922a31633083a093aa76325ac379d`.
- `mac-momentum-owner-strong-windows-r1`, failed terminal SHA256
  `441a3c6eac53c0367f1512e4feb30ffc8647c993941a0cc7ea942908e2cb8cd8`.

The default game DLL is unchanged. These receipts prove the stated occupied
owner controls; they do not certify real-game behavior of this new default,
the complete solver, gameplay, persistence or release readiness.

## Verification after native consumer integration

The momentum owner is merged with the native heat, gas-transfer, actor-clock
and physical-CO2 changes at `8bb8b28`. Comparing with the accepted 85-group
source confirms that its pressure/transport computations, owning timestep,
settings and scientific inputs are unchanged; the closed-world implementation
adds the separate immutable `with_cell_heat` entry point.

Fresh strict MSVC and GCC builds of this merged source each pass 33 native
groups: six limiter, six geometric momentum, seven owning momentum, eight
closed-step, four gravity, one gas-transfer and one native-domain group. All
five saved-review programs pass on each OS, retaining 107,210 owning checks
and the geometry/work/trace/clock/capacity corruption controls. The direct
domain controls cover native heat, gas exchange and physical CO2. A private
CMake include builds the two existing consumer test targets without enabling
the optional game hook backend; it changes no repository build definition.

Private immutable post-merge cohorts:

- `mac-momentum-owner-postmerge-windows-r1`, source-freeze SHA256
  `9ee126bffecc2b7bb596663f22ab8f11eb64487f1d33f0aa2efdff1db1fb6186`,
  terminal SHA256
  `e1ba41b1ee81b8c64ed44e05192d82466f0a969cc772d4d95ca6512594f68553`.
- `mac-momentum-owner-postmerge-linux-r1`, source-freeze SHA256
  `dae0e055f3414d85d93a968d90fde1ed5feb8275ea3ad1c0fdb3a9f8bd61448f`,
  terminal SHA256
  `7cbd53aacb7667cc2774ff48a50c3f2db0622bfbdf2644ed4d1020662098c26a`.

These are direct native integration controls. They provide no new Unity,
mouse-hover, whole-colony, save/reload or performance acceptance. The known
strong-flow failure is not replayed because its numerical path is unchanged.
The default game DLL remains unchanged.
