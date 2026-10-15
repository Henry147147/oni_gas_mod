# Integrated fluid prototype

`FluidWorld` is a separate C++ integration benchmark. `GasWorld`, the C ABI,
managed wrappers, native-save schema and the game probe continue to use the
existing gas reference solver. Milestone B is **not complete**.

## Implemented path

Each accepted substep runs the following operations on a candidate state:

1. Reconstruct interior staggered velocities from conservative cell momentum.
   Face inertia is the arithmetic mean of adjacent cell masses. Exterior and
   solid faces are impermeable.
2. Call `advance_material_pressure` to solve nonlinear pressure work on moving
   material cells. Ideal gases use their actual headspace, heat capacity and
   adiabatic energy response. A full incompressible liquid contributes zero
   pressure compliance. A midpoint projection admits reconstructed velocities
   that have nonzero initial liquid divergence.
3. Allocate each shared face impulse and kinetic-energy change to its two
   adjacent cells. Cell internal energy accounts for the difference between
   staggered mechanical energy and reconstructed cell kinetic energy. Gravity
   work from the material solve is replaced by actual transported potential
   energy in the next operation.
4. Remap material volumes onto the original mesh with the **same** weights for
   every species, both momentum components, and total energy including gravity.
   The remap verifies target volume = material volume - outgoing + incoming
   swept volume. This geometric conservation law bounds liquid volume fractions
   without removing excess fluid. Positive retained mass plus positive incoming
   mass avoids subtractive cancellation of trace constituents.
5. Apply symmetric bounded gas diffusion and viscous momentum exchange.
   Viscous kinetic-energy loss becomes heat. Implicit pairwise conduction
   exchanges equal and opposite energies and checks its maximum principle.
6. Validate temperatures, inventories and occupied volumes, then publish the
   candidate. Numerical or CFL failures retry a smaller timestep. Exhausting
   the caller's budget rolls back **the whole requested interval**, including
   previously accepted substeps. Invalid initial states fail before solving.

Mass reductions use compensated summation, including on MSVC where
`long double` has the same precision as `double`. Pressure-work and long-run
energy checks use sensible-energy scales, so large chemical formation energies
cannot conceal thermodynamic errors.

## Deliberate limits

- Every non-wall cell must contain gas with positive headspace or be completely
  filled with liquid. Vacuum and partially filled pure-liquid free surfaces are
  rejected. No seed gas, removed constituent, or artificial liquid compliance
  stands in for these missing models.
- The common-velocity, common-temperature phase model numerically mixes a gas
  and liquid interface during first-order remap. Species identities and liquid
  volume fractions are retained; reconstructed interface geometry, immiscible
  separation and capillary physics are absent.
- Cell momentum is reconstructed onto staggered faces each substep. This is an
  integrated energy-conserving prototype, not a finished staggered dual-mesh
  momentum discretization. Shock entropy and high-Mach accuracy are unverified.
- There are no coupled phase transitions, dissolved gases, open boundaries,
  persistence, game ownership adapters, or production material catalog here.
- Midpoint pressure dynamics can oscillate at under-resolved acoustic scales.
  Taking an implicit step larger than acoustic CFL proves neither acoustic
  time accuracy nor suitability for gameplay.
- Allocation-heavy candidate construction and Jacobi-preconditioned CG are not
  the required production memory layout and pressure preconditioner.

## Validation

`ctest` includes `integrated_fluid_world`, with ten cases covering geometric
remap of full liquids, common-weight mass/momentum/energy transport, pressure
driven mixed-fluid transport, gas displacement of a full liquid slug, implicit
pure-gas response, equilibrium contacts, gas/liquid hydrostatics, 10,000-step
closed multiphase conservation, viscous shear heating, successful smaller-step
retry, and atomic rollback/unsupported-state rejection. Some of these checks
share a test case.

Windows checks on 2026-10-14:

- MSVC 19.51 Release: existing physics/ABI tests and integrated tests pass.
- GCC 13.1 Debug with `_GLIBCXX_ASSERTIONS`: both suites pass.
- The 10,000-step 3x3 mixed-fluid run enables gravity, gas diffusion, viscosity
  and conduction. Maximum relative constituent drift is `3.78e-13`, including
  an initial `1e-28 kg` helium trace; energy drift relative to initial sensible
  energy is `4.78e-12`. This is a small regression case, not the complete release
  conservation/physics matrix.

Run the benchmark after a Release build:

```powershell
.\build-solver\ma_fluid_benchmark.exe
# Optional width and height, for example:
.\build-solver\ma_fluid_benchmark.exe 80 50
```

The default is a 400x250 closed wet-air grid with a 20 Pa smooth pressure
perturbation, 10% liquid volume, and a helium trace in every cell. Initialization
and totals reduction are outside the timed interval; candidate backup, pressure
work and remap are inside it. Diffusion, viscosity and conduction are disabled
in this benchmark. Its JSON reports actual steps, pressure/nonlinear iterations,
conservation and timing; an exit code of zero means the benchmark completed,
**not** that the performance gate passed.

On this Windows Ryzen 9 9950X, the 100,000-cell, 0.1-second case took about
**428 ms**, one substep, two nonlinear iterations and 106 pressure iterations.
Compensated global reductions report zero drift to the printed precision for
that one step. The release goal is 25 ms on Ryzen 5900X, so this result **fails**
the gate even on the faster test host. It does not justify integrating this
prototype as the authoritative game runtime. Pressure preconditioning, sparse
active-domain storage, reusable workspace and a true staggered momentum remap
must be addressed together with the missing phase/interface physics.
