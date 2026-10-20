# `ma::FluidWorld` gas/liquid prototype

`ma::FluidWorld` (`fluid_world.hpp`, `native/src/fluid_world.cpp`, in `ma_core`)
is an integrated gas/liquid prototype with two modes:

- **Closed mode** (2-D, every non-wall cell occupied): the validation reference
  behind the [coupled C ABI](COUPLED_ABI.md).
- **Occupied 1-D mode** (opt-in, one material interval in exact vacuum): a
  dormant free-surface experiment.

It is not the production solver, which is `MacClosedWorld` (see
[SOLVER.md](SOLVER.md)), and it has no ONI frame hook.

## Closed mode

Each substep works on a candidate state:

1. Reconstruct face velocities from cell momentum (face inertia = mean of the
   adjacent cell masses; exterior and solid faces impermeable).
2. `advance_material_pressure` solves nonlinear pressure work on moving
   material cells: ideal gases use their headspace, heat capacity and adiabatic
   response; a full liquid has zero compliance. A midpoint projection admits
   velocities with nonzero initial liquid divergence.
3. Split each face impulse and kinetic-energy change between its cells; cell U
   absorbs the difference between staggered and cell kinetic energy.
4. Remap material volumes onto the fixed mesh with the **same** weights for every
   species, both momentum components and total energy including gravity. Target
   volume must equal material volume minus outgoing plus incoming swept volume,
   which bounds liquid fractions without removing fluid. Retained plus incoming
   positive mass avoids cancelling traces.
5. Apply bounded gas diffusion and viscous exchange (loss becomes heat) and
   implicit pairwise conduction (equal and opposite, maximum principle checked).
6. Validate temperatures, inventories and occupied volumes, then publish.

Numerical or CFL failures retry a smaller step. Exhausting the caller's budget
rolls back **the whole requested interval**. Invalid initial states fail before
solving. Mass reductions are compensated; energy checks use sensible-energy
scales so formation energy cannot hide errors.

**Pressure preconditioning.** Systems of at least 256 cells try natural-order
`IC(0)` (unit-lower `L D L^T`, SPD when D is finite and positive) and fall back
to Jacobi on an unsafe pivot or a row with more than 64 lower entries. The final
true residual is checked on the original operator; no shift, gauge change or
tolerance change is involved. Explicit `jacobi`/`ic0` remain for comparison, and
other `PressureSystem` callers (including `GasWorld`) keep Jacobi. Method:
[Templates](https://www.netlib.org/templates/templates.html); SPD requirement:
[PETSc](https://petsc.org/release/manualpages/KSP/KSP_DIVERGED_INDEFINITE_PC/).

**Limits.** Non-wall cells must hold gas with positive headspace or be full of
liquid; vacuum and partial liquid cells reject, with no seed gas or artificial
compliance. Cells share one velocity and temperature, so first-order remap
mixes a gas/liquid interface numerically (no interface geometry, immiscible
separation or capillarity). Momentum is reconstructed onto faces every substep,
so this is not a finished staggered discretization; shock entropy and high-Mach
accuracy are unverified, and midpoint dynamics can oscillate at under-resolved
acoustic scales. Liquids have constant density. There are no phase transitions,
open boundaries, persistence or game adapter.

**Tests** (pass Windows MSVC Release, Linux GCC 15 Release and ASan/UBSan):

- `integrated_fluid_world` (10 cases): full-liquid remap, common-weight
  transport, pressure-driven mixed transport, gas displacing a liquid slug,
  implicit pure gas, contacts, hydrostatics, 10,000-step conservation (3x3 with
  gravity, diffusion, viscosity, conduction and a `1e-28 kg` helium trace:
  constituent drift `3.78e-13`, energy drift `4.78e-12` of sensible energy),
  shear heating, smaller-step retry and atomic rollback.
- `pressure_preconditioning_and_rollback` (7 groups): IC(0) convergence and
  fallback, irregular graphs, liquid gauges with gravity, Jacobi/IC(0)
  agreement in `FluidWorld`, and allocation and budget rollback.
- `mixture_property_compatibility`: 120,340 bit-exact results for the zero-mass
  property skip in `mixture.cpp`.

### Benchmark and the performance gate

```sh
build/ma_fluid_benchmark                  # 400x250
build/ma_fluid_benchmark 400 250 7 auto   # width height samples auto|jacobi|ic0
```

The case is a closed 400x250 wet-air grid (100,000 cells, 20 Pa perturbation,
10% liquid, helium trace everywhere) advanced 0.1 s with diffusion, viscosity
and conduction off. Repetitions rebuild the same world; the JSON reports the
first step and the median of the rest, with iterations, residuals, the chosen
preconditioner and conservation. Validation, candidate construction, pressure
work, remap and rollback storage are timed. Exit code zero means it ran, **not**
that the gate passed.

**Gate: 25 ms per 0.1 s step for 100,000 active cells on a Ryzen 5900X. It
fails.** The best steady medians, on a faster Ryzen 9 9950X, are about 224 ms
(Windows MSVC) and 185 ms (Linux GCC 15, WSL). IC(0) cuts CG iterations from
106 to 34, but its triangular solves use much of the saving. A step makes about
300,000 allocations (160 MB cumulative); closure setup costs 37-39 ms and
validation, transport and per-cell allocation most of the rest. Passing would
need sparse active-domain storage, reusable workspace and a true staggered
momentum remap.

## Occupied 1-D mode

The opt-in mode uses the same `step` candidate and rollback path for one
connected 1-D slab or column with exact exterior vacuum, static rigid walls,
constant cross-section and a versioned elastic/ideal-gas EOS. Its gas-to-vacuum
front gate is **false** ([GAS_VACUUM.md](GAS_VACUUM.md)).

**Setup.** Construct with `FluidGeometry1D{axis, thermodynamics,
liquid_fill_roundoff}`. The non-axial dimension is one; horizontal worlds need
zero gravity; diffusion, viscosity and conduction must be off.
`set_occupied_cell` takes conserved U, axial velocity and the local occupied
interval. `FluidThermodynamics::initialize_at_temperature` reports the elastic
energy it adds; setting or evaluating an inventory never adds it. Every
constituent needs a matching profile entry (ID, phase, molar mass, Cv, caloric
reference). Test water parameters are surrogates.

**State.** An empty cell holds exactly zero mass, U and momentum and has no
temperature. A wet face has area A, not fraction times A. Endpoints are a
grid-line index plus local offset, so large positions are never subtracted.
Overlap remap can move a whole donor out of its cell; there is no tiny-cell or
constituent cutoff, and the subtraction remainder goes to the largest overlap so
a tiny wet receiver gets a direct positive share.

**Mechanics.** Node inertia I is the sum of adjacent half-cell masses (M/2 at a
free surface, which has zero traction). Wall nodes have zero velocity and a
recorded reaction. The step uses the secant work pressure:

```
I (u1-u0) = dt [ A (pwork_left-pwork_right) + I g ]
q1-q0 = dt (u0+u1)/2
dV_i = A (dq_right-dq_left),  dU_i = -pwork_i dV_i
du = u1-u0,  v1_i = v0_i + (du_left+du_right)/2
Q_i = M_i (du_right-du_left)^2 / 8 >= 0
```

Node velocities start as a mass-weighted difference form that keeps a uniform
velocity exact across unequal cut masses. Cell momentum is retained and only
the force increment is returned; the remaining O(dt^2) kinetic energy Q becomes
heat (`final_projection_heat_j`). All quantities use the same occupied overlap,
potential energy uses occupied centroids, and EOS readings are recomputed after
remap. Pitfall: replacing cell velocity by the reconstructed node average (as at
commit `24dc2c7`) applies `(v[i-1]+2v[i]+v[i+1])/4` even as dt tends to zero and
halves a quarter-grid mode every call while conservation still passes; a test
now requires that amplitude to approach one.

**Liquid-fill roundoff.** `LiquidFillRoundoffPolicy::strict_eos` is the default.
The opt-in `balanced_local_roundoff` lets gasless material use zero work
pressure only while `abs(sum(m_liquid/rho0) - Voccupied) <= 32 epsilon
Voccupied` holds at the entropy anchor and every substituted endpoint. Any gas
trace disables it. Each substitution reports its omitted pressure and
elastic-work bound and is rejected above 64 epsilon of local thermal-plus-elastic
energy; `FluidStepResult` accumulates the bounds.

**Representability.** Represented changes are reported apart from mathematical
ones. The `V0+dV` error, residual-addition bound and stored-geometry balance
must each fit `32 epsilon V1`; positions use 32 epsilon of cell length; a target
union must match its interval to 32 epsilon of its length. Material whose
geometry becomes nonpositive or unresolved fails rather than drying out.

**Failures.** Interior cavitation, separation or merging, internal liquid void,
unsupported wall impact and incompatible topology throw `FluidFatalError`,
bypassing retries. Other failures may retry; any failure restores all cells,
geometry, time and the approximation ledger.

**Tests.** `integrated_fluid_free_surfaces` (24 groups) runs the real `step`:
vacuum, cut liquid, discrete hydrostatics, centroid gravity and wall reaction,
incremental heat, the Fourier limit, elastic release, translated liquid with a
trace at three resolutions, wet/dry activation, sub-ULP motion, tiny receivers,
the roundoff policy and a too-thin interval that must fail with rollback.
