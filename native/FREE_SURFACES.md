# Experimental occupied 1-D FluidWorld: geometry and liquid pressure/remap

This is an explicit opt-in mode of the existing `FluidWorld::step` candidate and
rollback path. It is not game runtime activation. The default closed world,
`GasWorld` and C ABI retain their existing models. This checkpoint covers one
connected 1-D material slab/column, exact exterior vacuum, static rigid walls,
constant cross-section and a versioned zero-expansion elastic/ideal-gas EOS.
Gas/vacuum rarefaction convergence is a separate gate and is not established by
the liquid tests or the two-gas-cell mechanics test. No 2-D, performance,
phase-change or colony-ownership claim follows.

Construct the world with `FluidGeometry1D{axis, shared_const_thermodynamics,
policy}`. The non-axial dimension must be one; horizontal worlds require zero
gravity. Diffusion, viscosity and conduction must be explicitly disabled in
this mode. `set_occupied_cell` accepts conserved U, axial velocity and local
occupied interval endpoints. `FluidThermodynamics::initialize_at_temperature`
returns both the initialized inventory and its explicit added-elastic-energy
ledger. Setting or evaluating an inventory never creates that energy for it.
Each present constituent needs a profile entry with matching stable ID, phase,
molar mass, Cv and caloric reference. Bulk modulus, zero-pressure density,
temperature/compression bounds, version and provenance are explicit data.
The test water parameters are surrogates, not validated real-fluid calibration.

An empty cell contains exactly zero constituent masses, U and momentum. Its
pressure and heat capacity are zero; temperature is absent (the reference's
numeric zero is a sentinel). Occupied EOS volume excludes exterior vacuum.
The occupied interval decides wet faces: a cross-section/interface has area A,
not volume-fraction times A. Endpoints retain grid-line indices and local
offsets, avoiding subtraction of large absolute positions in volume/overlap
calculations. Conservative overlap remap can move an entire donor out of its
old fixed cell. There is no tiny-cell, constituent or outgoing-fraction cutoff.
The subtraction remainder is assigned to the largest overlap so a tiny wet
receiver obtains a directly calculated positive share.

For a material node, inertia is the sum of adjacent half-cell masses. A free
surface has one-sided inertia M/2 and zero exterior pressure traction. Fixed
wall nodes have zero velocity and a recorded reaction impulse. Midpoint nodal
velocity and a compatible volume gradient/divergence pair use the reference's
stable secant work pressure, separately from endpoint EOS pressure:

```
I (u1-u0) = dt [ A (pwork_left-pwork_right) + I g ]
q1-q0 = dt (u0+u1)/2
dV_i = A (dq_right-dq_left), dU_i = -pwork_i dV_i
```

Initial cell-to-node velocities are mass weighted, using a difference form that
preserves an exactly uniform velocity across unequal cut masses. Canonical cell
momentum is retained; only the nodal force increment is transferred back:

```
du = u1-u0
v1_i = v0_i + (du_left+du_right)/2
Q_i = M_i (du_right-du_left)^2 / 8 >= 0
```

The mass-adjoint reconstruction makes the linear nodal/cell kinetic-work terms
equal. Q is their remaining quadratic difference, so the unresolved incremental
kinetic energy becomes heat after the isentropic pressure step. It is O(dt^2).
The diagnostic `initial_projection_heat_j` is now zero in occupied mode;
`final_projection_heat_j` records this incremental Q. There is no instantaneous
wall-velocity projection or its former impulse. Wall reaction during the force
step remains explicit. All constituents,
momentum, internal/kinetic energy and gravitational first moments use the same
occupied overlap. Potential energy uses occupied centroids. EOS pressure and
temperature are recomputed from each remapped conserved inventory. Diagnostics
include the incremental heat and its represented rounding error, wall/body
impulses, pressure-work rounding, nonlinear/linear residuals and represented
U+K+potential balance.

This corrects a failed timestep-consistency baseline at commit `24dc2c7`.
Replacing cell velocity by the complete reconstructed nodal average applied the
filter `(v[i-1]+2v[i]+v[i+1])/4` even as dt approached zero. A quarter-grid
Fourier mode therefore lost half its amplitude on every call while its lost
kinetic energy became heat. The actual-world regression now requires the
velocity amplitude to approach one under fixed-grid timestep refinement;
conservation alone cannot pass it. [GAS_VACUUM.md](GAS_VACUUM.md) records the
separate gas-front validation boundary and remaining mechanics limitation.

## Explicit liquid-fill representability approximation

`LiquidFillRoundoffPolicy::strict_eos` is the default. The separately selected
`balanced_local_roundoff` policy is a numerical experiment for maintaining a
nominal liquid translation in finite precision. It is not a general pressure
floor or evidence of production EOS accuracy.

Any positive gas constituent, including a minimum positive trace, disables the
approximation. For gasless material, both the authenticated entropy anchor and
every substituted trial/represented endpoint must satisfy

```
abs(sum(m_liquid/rho0) - Voccupied) <= 32 epsilon Voccupied.
```

There is no unit-volume floor. Within this symmetric compression/underfill band
only, the mechanics may use zero work pressure. It preserves stored mass, U,
geometry, and true EOS pressure/temperature readings. Outside the band it uses
the full EOS. Each accepted substitution reports its count, maximum omitted
pressure, and the larger absolute elastic-work difference at the requested and
represented volume endpoint. A substitution is rejected if that bound exceeds
64 epsilon times its local thermal-plus-elastic energy; formation energies do
not enlarge the budget. `FluidStepResult` includes per-call and accumulated
omitted-work bounds; the world ledger advances only when the entire call
commits. These are explicitly reported approximation bounds, not an exact
conservation claim. Literature on pressure-equilibrium-preserving advection
motivates consistent transport, but does not validate this particular policy
([primary paper](https://pmc.ncbi.nlm.nih.gov/articles/PMC4122134/)).

Represented endpoint volume/position/work changes are checked and reported
separately from mathematical changes. The nonlinear signed volume residual is
normalized by the trial endpoint occupied volume, not the initial volume.
The compensated scalar `V0+dV` addition error and residual-addition arithmetic
bound are separately reported and must each fit 32 epsilon V1. The arithmetic
bound covers addition of the explicit change and represented midpoint sweeps;
it is not a bound on EOS calibration or all thermodynamic root error.
The stored-geometry balance `Vstored-V0-dV+pressure_residual` is compensated,
reported both as a per-cell maximum and signed/absolute per-call accumulation,
and must fit the same endpoint-local budget. Contraction does not use V0 as a
floor. This changes the floating-point acceptance criterion for rapidly growing
cuts; it does not change EOS/traction pressures or conserved inventories.
Position quantization separately uses 32 epsilon of local cell length.
Positive material whose geometry becomes
nonpositive or materially unresolved fails; it is never made dry by a cutoff.
Overlap transport uses mathematical clipped lengths before conversion to local
in-cell offsets. Tiny internal donor subdivisions are transported, then the
complete target union must match its stored interval to 32 epsilon of its own
occupied length. An arbitrary tiny interval next to the right edge may be
unrepresentable as `begin=dx-tiny,end=dx`; it fails instead of losing material.
Dyadic reflected cuts and translated coordinates are tested independently.
The tests include sub-ULP motion reporting, tiny volumes, tiny wet receivers,
outside-band full EOS, gas-trace exclusion, a cold surrogate where omitted
elastic work is material, and a too-thin translating interval that must fail
with rollback.

## Failure and validation boundary

The mode represents one connected material interval, not interior cavitation
or a newly separated/merged body. Meaningful internal liquid void, unsupported
wall impact, and incompatible topology fail with typed `FluidFatalError`.
They bypass timestep retries. Nonlinear/CFL failures can retry a smaller step;
failure after any provisional substep restores the original cell allocation,
all geometry/masses/U/momentum, time and accumulated approximation ledger.
The displacement CFL is based on grid spacing rather than a cut-cell donor
fraction. It does not prove large-timestep accuracy or remove all cut-cell
stiffness limitations.

`integrated_fluid_free_surfaces` exercises actual `FluidWorld::step`: exact
vacuum, cut liquid, independent discrete hydrostatic balance, occupied-centroid
gravity and wall reaction, compatible incremental kinetic heat, a fixed-grid
Fourier timestep limit, short gasless elastic
release, translated liquid plus conserved trace at three grid/time resolutions,
wet/dry activation/deactivation, representability limits and atomic failure.
Conserved totals are checked against nonformation energy scales. Use root CMake
and CTest; GNU `MA_SANITIZERS=ON` additionally instruments the bridge/reference,
pressure/remap code and test executable. The isolated reference's independent
26-group thermodynamic project remains available in `native/experimental`.
