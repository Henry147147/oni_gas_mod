# Integrated fluid prototype

`FluidWorld` is a separate C++ integration benchmark. `GasWorld`, the C ABI,
managed wrappers, native-save schema and the game probe continue to use the
existing gas reference solver. Milestone B is **not complete**.

A newer, separately excluded [closed MAC owner](MAC_CLOSED_WORLD.md) retains
canonical face momentum and composes EOS pressure work with compatible primal/
dual transport. Its eight Windows/Linux groups and independent saved-data
review pass, including smooth implicit acoustic refinement and compressed
liquid. It does not supply gravity, free surfaces, full multiphase physics or
game dispatch. The older prototype and its unresolved gates below are retained.

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

`FluidWorld` selects automatic pressure preconditioning: systems of at least
256 cells attempt a natural-order, no-fill incomplete Cholesky factorization
(`IC(0)`, represented as unit-lower `L D L^T`). A finite positive diagonal `D`
makes the preconditioner SPD. Unsafe pivots or rows with more than 64 original
lower entries select the existing Jacobi preconditioner instead. No matrix
shift, pressure gauge change, artificial liquid compliance, residual tolerance
change or nonlinear phase-closure change is involved. The original face
operator verifies the final true residual before any solution is accepted.
Explicit Jacobi and IC(0) settings remain available for comparisons. Existing
`PressureSystem` callers retain the Jacobi default, including the thermal path
used by the separate gas reference solver and C ABI.
The factor/application follows the incomplete-factorization approach in
[Templates for the Solution of Linear Systems](https://www.netlib.org/templates/templates.html);
the positive-pivot check preserves the SPD preconditioner requirement for CG,
also documented by [PETSc](https://petsc.org/release/manualpages/KSP/KSP_DIVERGED_INDEFINITE_PC/).

Candidate construction now moves pressure-result inventories into remap and
builds final active cells from their complete remapped inventories. It only
copies untouched wall metadata. Whole-call rollback retains the original cell
vector by a noexcept ownership move at the first validated commit; failures
before that commit leave the original vector in place, and any later exception
restores it. These changes remove redundant deep copies, rather than moving
their allocation cost outside the measured `step()` interval.

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
- Candidate construction still allocates per-cell species inventories and
  temporary arrays. IC(0) is a conservative improvement to this connected
  experimental path, not a finished production memory layout or a demonstrated
  solution to the release performance gate.

## Validation

`ctest` includes `integrated_fluid_world`, with ten cases covering geometric
remap of full liquids, common-weight mass/momentum/energy transport, pressure
driven mixed-fluid transport, gas displacement of a full liquid slug, implicit
pure-gas response, equilibrium contacts, gas/liquid hydrostatics, 10,000-step
closed multiphase conservation, viscous shear heating, successful smaller-step
retry, and atomic rollback/unsupported-state rejection. Some of these checks
share a test case.

`pressure_preconditioning_and_rollback` adds seven groups: variable-coefficient
grid convergence with unchanged true-residual limits and fewer iterations;
irregular triangular/parallel-face graphs with disconnected referenced
components and barriers; unsafe-pivot and high-degree Jacobi fallback; liquid
pressure gauges and gravity; coupled `FluidWorld` agreement with Jacobi and
constituent/energy conservation; allocation exceptions before and after the
first commit; and exact wall-metadata/budget rollback. Windows MSVC Release,
Linux GCC 15 Release, and Linux ASan/UBSan with libstdc++ assertions pass these
groups alongside the original 57 physics/ABI and ten integrated-fluid cases.

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
# Seven identical-state samples: first timed step and steady median.
.\build-solver\ma_fluid_benchmark.exe 400 250 7 auto
.\build-solver\ma_fluid_benchmark.exe 400 250 7 jacobi
```

The default is a 400x250 closed wet-air grid with a 20 Pa smooth pressure
perturbation, 10% liquid volume, and a helium trace in every cell. Initialization
and totals reduction are outside the timed interval; candidate backup, pressure
work and remap are inside it. Diffusion, viscosity and conduction are disabled
in this benchmark. Its JSON reports actual steps, pressure/nonlinear iterations,
conservation and timing; an exit code of zero means the benchmark completed,
**not** that the performance gate passed.

Optional repetitions rebuild the identical initial world before each timed
step. The first timed solve in the process is labelled cold; the steady median
uses subsequent solves. World initialization and totals remain outside the
timer, while initial validation, all candidate/preconditioner construction,
pressure work, remap, final validation and rollback storage stay inside it.
The per-sample JSON also reports verified linear residuals as a fraction of the
unchanged convergence threshold, nonlinear volume residuals, selected
preconditioners and fallback counts.

Before the pressure/copy optimization, this Windows Ryzen 9 9950X ran the
100,000-cell, 0.1-second case in about
**428 ms**, one substep, two nonlinear iterations and 106 pressure iterations.
Compensated global reductions report zero drift to the printed precision for
that one step. The release goal is 25 ms on Ryzen 5900X, so this result **fails**
the gate even on the faster test host. It does not justify integrating this
prototype as the authoritative game runtime. Sparse active-domain storage,
reusable workspace and a true staggered momentum remap
must be addressed together with the missing phase/interface physics.

## Pressure/copy performance checkpoint, 2026-10-15

An isolated before/after comparison used the same 400x250 initial condition,
compiler configurations, host, and timed work. The before operator/integrator
came from commit `2e270d8`; only its benchmark driver was adapted to repeat the
unchanged case. The after code includes the ownership moves above and IC(0).
All executables ran sequentially while ONI was closed and other agent work was
held. Windows used MSVC 19.51 Release; Linux used GCC 15 Release in Ubuntu 26.04
under WSL on the same Ryzen 9 9950X. This is not a Ryzen 5900X measurement.

Each process measured seven independently initialized identical worlds: the
first timed step and the median of the remaining six. These are short,
order-dependent samples, with visible host/runtime variation; the first step
does not mean cold operating-system or disk caches.

| Timed complete 100k-cell step | Windows first / steady median | Linux first / steady median |
| --- | ---: | ---: |
| Before, Jacobi | 404.93 / 373.07 ms | 372.00 / 309.50 ms |
| Ownership moves, explicit Jacobi | 315.31 / 335.60 ms | 341.13 / 301.61 ms |
| Ownership moves, automatic IC(0) | 322.84 / 321.01 ms | 355.54 / 267.69 ms |

The final steady medians improved about 14% in this window, with substantially
different first-step effects. Every sample took one accepted substep, two
nonlinear iterations and no rejected substeps. CG iterations fell from 106 to
34. The final linear residual divided by the **unchanged** acceptance threshold
was approximately `0.8501` before and `0.7049` after; nonlinear volume residuals
were about `1.15e-14` and `1.16e-14 m³`. Compensated constituent and total-energy
drift were zero to the printed precision for this single-step case. This does
not extend the supported physics or replace the long conservation tests.

Separate allocator/timer-instrumented copies measured all allocations inside
`step()`, including those outside named phase scopes:

| Private one-step profile | Windows before → after | Linux before → after |
| --- | ---: | ---: |
| Allocation count | 600,152 → 300,159 | 600,116 → 300,123 |
| Total bytes allocated | 194.79 → 161.17 MB | 193.16 → 159.54 MB |
| CG solve including reductions, matvec and preconditioner | 51.46 → 47.04 ms | 71.35 → 58.54 ms |
| IC(0) setup, two nonlinear solves | 3.96 ms | 6.52 ms |
| IC(0) application, 34 calls | 22.43 ms | 21.83 ms |

The triangular preconditioner applications consume much of the time saved by
fewer CG iterations. Unchanged closure setup still costs roughly 37–39 ms;
material-output checks, initial/remap/final validation, transport and remaining
per-cell allocations dominate much of the complete step. Profile counters are
exclusive nested scopes with some unlabelled destruction/setup work; their
instrumented totals are not substitutes for the uninstrumented timing table.
Bytes are cumulative allocations, not peak resident memory.

The 25 ms performance gate **still fails** on every measured sample. IC(0) is a
bounded improvement in the existing experimental integrator, not a production
multigrid solution or permission to take over the game simulation. Additional
work must address thermodynamic evaluation, reusable storage and conservative
transport alongside the unresolved vacuum, phase and interface physics.

Ignored local evidence is in `local/native-pressure/`: `result.json`, exact
before/after source manifests, executable/library SHA-256 values, compiler and
test logs, the six uninstrumented JSON runs, and four independent profile JSON
runs. `quiet-measurement-window.json` records the serial measurement interval.
Private profile/driver adaptation scripts are kept there; no profiling hooks
or allocator overrides are compiled into the production library.

## Property-only follow-on checkpoint, 2026-10-15

The approved `mixture.cpp` change uses a read-only registry view after the
existing size checks and skips property arithmetic only for exactly zero mass.
Every positive trace retains its term in the original species order. Public
APIs, storage, species formulas, reduction order, every-entry inventory
validation and pressure tolerances remain unchanged.

`mixture_property_compatibility` checks **120,340 bit-exact scalar results and
exception outcomes**, including dense/sparse mixtures, phase reference energies,
signed zeros, minimum positive traces, invalid inputs and direct public-field
mutations. Windows MSVC Release, Linux GCC 15 Release, and Linux ASan/UBSan with
libstdc++ assertions pass this and the existing physics/ABI, fluid and pressure
suites against the frozen integrator.

The comparison freezes the world/pressure/remap code at `0f938f9` and changes
only property calculations. It uses the same 400x250 wet-air, 0.1-second case
with four occupied species out of sixteen. Five independently initialized
identical states run per process: first timed step / median of the remaining
four. Executables run serially with ONI closed, using MSVC 19.51 and GCC 15
under Ubuntu 26.04 WSL on the same Ryzen 9 9950X.

| Complete 100k-cell step | Windows first / steady median | Linux first / steady median |
| --- | ---: | ---: |
| Frozen baseline | 303.94 / 322.15 ms | 343.58 / 253.93 ms |
| Baseline repeated after candidates | 294.56 / 295.78 ms | 299.99 / 252.71 ms |
| Approved property-only change | 209.93 / 224.20 ms | 226.90 / 184.85 ms |

The repeat guard shows visible host/order variation, particularly on Windows;
these short samples are not a Ryzen 5900X or sustained Unity measurement. Every
sample retains 34 CG iterations, two nonlinear iterations, unchanged printed
residuals and zero printed constituent/energy drift. Allocations remain
**300,159 / 161.17 MB** on Windows and **300,123 / 159.54 MB** on Linux per
instrumented step; bytes are cumulative, not peak memory. The **25 ms gate
still fails**. These results do not validate newer interface/phase models.

Ignored evidence, exact source/binary hashes, counter profiles, repeat guards
and test logs are in `local/native-properties/result.json` and its accompanying
manifests.
