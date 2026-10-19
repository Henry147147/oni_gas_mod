# MacClosedWorld solver

`MacClosedWorld` is the project's production colony fluid solver (user decision,
2026-10-18). In owned cells it replaces ONI's native SimDLL fluid simulation; the
SimDLL must not be used as the solver. The ONI native domain
(`native/src/oni_domain.cpp`) runs one owner per session. The code lives in
`native/include/mixed_atmospheres/mac_*.hpp` and `native/src/mac_*.cpp`, in namespace
`ma::experimental` (the name predates the decision).

The solver works on a closed regular two-dimensional MAC grid. Exact empty-cell
inventory and gas arrival are supported, but the [vacuum front](MAC_VACUUM.md)
still lacks occupied-volume tracking and fails its accuracy gate.
Primary state is each cell's constituent masses, stored internal energy U and passive
quantities, plus the canonical normal momentum J on every face. Every derived quantity
(velocity, kinetic energy K, pressure, temperature) is computed from those; nothing
primary is rebuilt from a scalar total energy.

Topic documents:

| Document | Contents |
| --- | --- |
| [Transport and material reconstruction](MAC_TRANSPORT.md) | Primal/dual MAC transport, parcel plans, material selectors 0–2 |
| [Pressure, gravity and compression](MAC_PRESSURE.md) | Implicit EOS pressure work, gravity, compression regularization modes 0–3 |
| [Face momentum reconstruction](MAC_MOMENTUM.md) | Compatible momentum corrections, momentum selectors 0–5 |
| [One-dimensional nodal conversion](MAC_NODAL_CONVERSION.md) | Opt-in point-to-dual momentum/kinetic conversion and its remaining limits |
| [Heat and entropy](MAC_HEAT.md) | Shock-heat regularization modes 0–3, physical conduction, entropy checks |
| [Gas diffusion](MAC_DIFFUSION.md) | Conservative mixture-averaged fluxes, physical/accelerated modes, transport data and validation |
| [Range arithmetic](MAC_RANGE.md) | `MacEnergy`/`MacScaled`/`MacExponent` and where full-range values are used |
| [Production kernel](MAC_KERNEL.md) | Consolidated interval-audited implementation, geometric pressure solve and deterministic workers |
| [Retired interval kernel](MAC_INTERVAL_KERNEL.md) | Selector-1 retirement and explicit rejection of its old checkpoints |
| [Pressure multigrid](MAC_MULTIGRID.md) | Geometric preconditioning in the production kernel and independent component controls |
| [Physical checkpoints](MAC_CHECKPOINT.md) | `MACCLS01` wire format, native-domain checkpoint ABI, paired saves |
| [Strong-flow accuracy](MAC_STRONG_FLOW.md) | The failed Riemann gate, its diagnosis and the next step |
| [Empty cells and vacuum front](MAC_VACUUM.md) | Conservative empty-cell support, saved controls and the failed front/volume gates |

Separate documents cover [physical viscosity](../docs/native-domain-physics.md#physical-viscosity),
[gas/liquid conduction](../docs/native-domain-physics.md#mixed-cell-conduction),
[gas/liquid coexistence and transfers](../docs/native-domain-physics.md#occupied-gasliquid-cells),
[exact equilibrium and benchmarks](../docs/playtesting-readiness.md#implemented-exact-equilibrium)
and the [floating-point environment](../docs/native-platform-numerics.md#solver-arithmetic-scope).

## Owner contract

Declared in `mac_closed_world.hpp`:

- The constructor copies the species registry, holds an immutable shared EOS, and
  validates the state and settings. A caller's later changes to its own model objects
  cannot alter the owner.
- `advanced(seconds, attempt_budget = 1024, policy = reference)` returns a new owner
  and a report. `seconds` must be in (0, 10] and the budget in 1..65536. The
  original owner never changes. A completed interval increments the revision once.
- The first substep tries `maximum_timestep_s`, clipped to the interval end. A
  `std::domain_error` from any stage (CFL, EOS domain, admission) halves the step and
  retries; after an accepted step the next try is `min(maximum, 2 dt)`. Exhausting the
  shared attempt budget throws and publishes no state, clock or counters.
  `FluidFatalError` (topology or precision) propagates unchanged.
- `MacExecutionPolicy::exact_equilibrium` (used by stateless checkpoint
  continuation and historical unflagged journal replay) returns one certified interval with no stages when
  all canonical momentum is exactly zero and connected inventories are identical
  at zero gravity. Disconnected regions may have different inventories. With
  gravity, every discrete interior pressure/body force must cancel exactly and
  enabled conduction must have no temperature difference; gravity and wall
  impulse are still accounted. Nonzero force residuals take the full solver path.
- `MacExecutionPolicy::production` selects and persists numerical kernel 2
  (checkpoint version 6, or 7/8 with opt-in two-moment pressure), including the certificate path. Ordinary restored
  advances select the same kernel without a caller flag. Its saved automatic
  pressure setting uses geometric multigrid on grids of at least 256 cells;
  explicit Jacobi is retained. With midpoint pressure, unported settings take an
  audited reference interval without changing physical selectors or saved kernel.
  New native ONI owners and each live worker frame explicitly select production;
  journal flag 4 records migration from a supported reference save.
- `MacPressureIntegration::two_moment` is an opt-in pressure time scheme for
  numerical kernel 2. It persists in version 7; `two_moment_coupled` uses the
  same temporal equations with a coupled linear solve and version 8. Both preserve prior midpoint
  trajectories and images. Selected larger-step waves pass resolved-reference
  bounds; automatic calm-region selection and local subcycling are still open.
  It refuses unsupported reference/range fallbacks that would change temporal
  method. See [pressure moments](MAC_PRESSURE_MOMENTS.md).
- `with_cell_heat`, `with_gas_transfers` and `with_fluid_transfers` are immutable,
  atomically checked edits (heat batches of 1–4096 edits; point sources and sinks
  with complete parcel accounting). Mixed gas/liquid transfers need the explicit
  `occupied_mixture` topology.
- `checkpoint()` and `from_checkpoint()` write and validate the complete physical
  image, including clock and revision ([format](MAC_CHECKPOINT.md)).

`advance_mac_closed_substep` exposes the pressure/transport stage alone for
component tests; it excludes the regularization, conduction and viscosity stages.

## Substep structure

When gas diffusion is enabled, the owner wraps each interval of at most 0.1 s
with half a diffusion advance before flow and half after. The following flow
substeps retain their maximum 0.01 s timestep. Exact-equilibrium execution must
also certify zero diffusion flux (identical inventories and U across every open
face, or a face touching an empty cell). See [gas diffusion](MAC_DIFFUSION.md).

Each attempted substep of length dt applies these stages to a local candidate, in
this order (`MacClosedWorld::advanced` in `mac_closed_world.cpp`):

1. Physical viscosity, dt/2, when either coefficient is positive.
2. Physical conduction, dt/2, when `thermal_conduction` is set.
3. Shock-heat regularization, dt/2, unless `shock_heat` is `none`.
4. Split compression regularization, dt/2, for compression modes 1 and 2.
5. Pressure and transport (`advance_mac_closed_substep`): implicit EOS pressure work
   and body force on moving material cells (with coupled compressive stress in
   compression mode 3), material reconstruction into parcels, then MAC transport of
   parcels, face momentum and kinetic energy with the selected momentum
   reconstruction.
6. Split compression, dt/2 (modes 1 and 2).
7. Shock-heat regularization, dt/2.
8. Physical conduction, dt/2.
9. Physical viscosity, dt/2.

Each stage freezes its coefficients from its own incoming state; the symmetric
ordering is not a claim of second-order accuracy for the coupled nonlinear system.
Only an accepted candidate adds its state, time, stage energy bounds, heat, wall
impulse and counters to the interval. After the interval, the total U+K(+potential)
change must lie within the sum of the accepted stage bounds, and the report gives the
actual endpoint entropy change and its stored-U allowance.

## Settings: production defaults and opt-in selectors

`MacClosedSettings` defaults are the production settings. Enum values are persisted
in checkpoints and never change meaning; new methods get new values.

| Setting | Default | Other values |
| --- | --- | --- |
| `maximum_timestep_s` | 0.01 s | (0, 1] |
| `maximum_outgoing_volume_fraction` | 0.4 | (0, 0.45] |
| `preconditioner` | `automatic` | `jacobi`, `incomplete_cholesky` |
| `gravity_m_per_s2` | 0 | ≥ 0, constant and downward; requires nonperiodic y |
| `reconstruction` (material) | 1 `limited_linear` | 0 `donor_cell` (reference), 2 `swept_first_moment`, 3 `interface_plic` (sharp gas/liquid interfaces) |
| `momentum_reconstruction` | 1 `limited_linear` | 0 `donor_cell` (reference), 2 `quadratic_limited_linear`, 3 `swept_physical`, 4 `swept_energy_flux`, 5 `swept_nodal_1d` (1-D only) |
| `compression_viscosity` | 2 `acoustic_quadratic` (pressure-sensed) | 0 `none`, 1 `quadratic`, 3 `coupled_pressure` |
| `shock_heat` | 2 `pressure_sensed_acoustic` | 0 `none`, 1 `compression_activated`, 3 `pressure_sensed_series` |
| `thermal_conduction` | false | true |
| `viscosity` (shear, bulk Pa·s) | 0, 0 (disabled) | nonnegative coefficients |
| `numerical_kernel` | 0 `reference` | 2 `production`; unported selections use the audited reference; retired value 1 rejects |
| `diffusion` | disabled for low-level compatibility; new game gas regions explicitly select physical; composes with either kernel | physical 1×, accelerated (initial preference 100×) |

The native domain keeps these physical selectors and explicitly selects numerical
kernel 2.
Legacy scenarios enable conduction except in scenario 4, use 9.81 m/s² in the gravity
scenarios and viscosity 0.5/0.25 Pa·s in scenario 6. Configured sessions take
conduction, viscosity and gravity from their physics flags.

Selectors 2 (material), 2–4 (momentum), 3 (compression) and 3 (heat) are opt-in
experiments. Material 2 with momentum 4 and compression 3 is the most accurate
tracked combination on the strong-flow benchmark, but it does not pass that gate
either and is not the default.

Standalone component functions have their own reference defaults, which differ from
the owner's: `advance_mac_transport` and `advance_mac_parcel_transport` default to
donor-cell momentum, `advance_mac_compression` to the quadratic law and
`advance_mac_shock_heat` to the closing-velocity law.

## Numerical contracts

**Production contract (user decision, 2026-10-18).** The production substep
kernel is checked once per completed interval instead of in every stage:

- **Conservation by construction.** Every constituent mass, passive quantity,
  momentum component and U+K+G moves only through shared face fluxes and
  explicit exchange terms applied antisymmetrically, so what leaves one cell
  enters its neighbour.
- **Interval audit.** At the end of each interval, the species, passive,
  momentum (with wall and body impulse) and U+K+G totals, including external
  heat and work, are reconciled against 128 binary64 epsilons of the interval's
  absolute operands. The audit uses double-double accumulation and escalates
  to exact expansions only when that comparison cannot decide. An interval
  that fails its audit is rejected and retried smaller, or it faults; it is
  never published.
- **Rounding.** Primary storage stays binary64. Updates below a stored value's
  resolution, and transfers below binary64 range, may round. Each rounding is
  counted and included in the audited bound. Fields are still never clipped,
  floored or renormalized to close an account, and a positive constituent is
  never removed by repair. A positive transfer below binary64 range may round
  instead of rejecting the step (user decision, 2026-10-19); only such counted
  rounding can take a positive inventory to zero.
- **Entropy.** The interval rejects a decrease in total stored-state entropy
  beyond `128 ε Σ(|U|/T)` over its endpoints, with the gravity reconciliation
  credit authorized by the user on 2026-10-19 (`3fbccfb`). For that reversible
  reconciliation alone, subtract `Σ(dU/T_pre)` from the interval entropy change
  and add `128 ε Σ(|dU|/T_pre)` to the allowance. `T_pre` is evaluated at the
  same masses and volume before the reconciliation. Concavity makes the credit
  an upper bound on that stage's actual entropy change; the remaining change
  must pass the original interval gate. This applies to the production kernel
  and its audited reference fallback; it does not relax other stages or
  conservation. See [the derivation and controls](MAC_KERNEL.md#gravity-reconciliation-entropy).
  Per-stage entropy admission is no longer required.
- **Range.** Full-range arithmetic is used only where a cheap check flags a
  value outside the normal binary64 range during the interval.
- **Unchanged.** Determinism: results are bit-identical for the same build and
  inputs, independent of thread count and scheduling. Also unchanged: checkpoint
  continuation, atomicity, fail-closed admission and the arithmetic environment.
- **Calm-region steps (user decision, 2026-10-18).** The production kernel may
  take larger steps in calm regions and subcycle active ones locally. Faces
  between differently stepped regions carry one shared, time-integrated flux.
- **Reference oracle.** The current implementation, with the per-stage rules
  below, stays buildable and tested. The production kernel is validated
  against it on shared scenes within the completion plan's accuracy tolerances.

The [production kernel](MAC_KERNEL.md) implements the production physical selectors
and interval contract. The default reference and retained fallback enforce the
reference stage rules; a production fallback also reconciles its interval
accounts. The [first interval port](MAC_INTERVAL_KERNEL.md) is retired.
**Reference rules:**

- **Accounts.** Each stage checks its species, passive, momentum and energy
  balances against 128 binary64 epsilons of the actual operands: the residual is
  compared with 2^-45 times the sum of absolute operands, with no absolute floor.
  Composed bounds are the sum of stage bounds, so cancellation cannot erase
  roundoff already incurred.
- **Entropy.** Stages that admit a candidate by entropy (heat, compression,
  compressive work, swept momentum) reject a decrease in actual stored-state entropy
  beyond `128 ε Σ(|U_initial|/T_initial + |U_final|/T_final)`; the interval reports
  its endpoint change against the same allowance. Changes are measured relative to
  the stored state, not by subtracting rounded absolute entropies
  ([why](MAC_HEAT.md#entropy-measurement)).
- **No repair.** No field is clipped, floored, renormalized or repaired to close an
  account. Traces are never dropped. A positive transfer that cannot be stored
  rejects the candidate; an update too small to change a stored value is counted as
  unresolved.
- **Range.** Primary storage is binary64. Derived energies, parcels, momentum
  contributions, full canonical J and fluxes use significand/exponent range
  arithmetic with explicit projections ([details](MAC_RANGE.md)).
- **Atomicity.** Every operation returns a new candidate or throws; inputs and the
  source owner are never modified.
- **Arithmetic environment.** `require_mac_energy_environment()` checks (without
  changing) x86-64 binary64, round-to-nearest, FTZ/DAZ off and masked SSE
  exceptions on the executing thread.

## Scope and unsupported cases

- Exact empty inventory and conservative gas arrival/depletion are supported in
  the reference. Resolved occupied-volume interfaces, internal liquid voids and
  free-surface dynamics remain open. The native game adapter still rejects empty
  cells; the interval kernel uses the reference for this topology.
- Walls and exterior faces are impermeable. Periodicity is explicit and needs at
  least two cells along the axis. Nonzero gravity rejects vertical periodicity.
- Gravity work and potential flux retain full-range operands. Primary U and
  material volume still round; unresolved U changes are counted, and persistent
  rounding remainders remain unfinished.
- Coupled compression (mode 3) rejects incoming face velocities or midpoint sweeps
  that need range projection.
- There are no phase transitions, no passive diffusion and no topology-change
  operation (construction or digging).
- The test material set uses a zero-expansion elastic water surrogate; it is not a
  calibrated material catalogue.

## Current status

- The most recent complete native suite (237 groups, including every MAC component
  and owner control, and 40 saved-data reviews) passes on Windows/MSVC, Linux/GCC
  and Linux ASan/UBSan.
- Smooth-flow controls pass: owner acoustic wave refinement and pointwise Fourier
  profile, stratified gravity mode and hydrostatic rest, plus component shear
  transport, scalar advection and constant-pressure/temperature contacts (see the
  topic documents).
- The [strong-flow gate](MAC_STRONG_FLOW.md) fails. The defaults fail collision
  density refinement (0.856 against 0.85). The best opt-in combination passes Sod and
  collision but fails the separation finest-grid velocity and central density/pressure
  bounds.
- Production gates and the playtest plan are tracked in
  [playtesting readiness](../docs/playtesting-readiness.md) (gate 2) and the
  [completion plan](../docs/completion-plan.md).

## Building and running the tests

Component tests and diagnostics are standalone targets excluded from the default
build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ma_mac_closed_world_tests
build/ma_mac_closed_world_tests > closed.stdout 2> closed.stderr
python3 native/tests/review_mac_closed_world.py closed.stdout
```

Visual Studio places executables in `build/Release/`. Add `-DMA_SANITIZERS=ON`
(GCC/Clang) for an ASan/UBSan build with fatal findings. Strict test targets build
with warnings as errors and no floating-point contraction.

Each `native/tests/review_mac_*.py` reads a saved stdout capture without running
native code, recomputes the physics with exact fractions or 80–110-digit decimals,
and checks that deliberately altered records are rejected. Each takes the saved
stdout first; most also take a new JSON report path, which must not already exist.
Use a fresh, complete capture; readers enforce a size cap.

With `-DMA_BUILD_ONI_NATIVE_BACKEND=ON`, CTest also runs `mac_gas_boundary_transfers`,
`mac_occupied_conduction`, `mac_mixture_conduction`, `mac_physical_viscosity`,
`mac_native_body_heat`, `mac_physical_checkpoint` and `mac_exact_equilibrium`.

| Target | Flags | Saved-data reviewer | Document |
| --- | --- | --- | --- |
| `ma_mac_transport_tests` | | `review_mac_transport.py` | [Transport](MAC_TRANSPORT.md) |
| `ma_mac_reconstruction_tests` | | `review_mac_reconstruction.py`, `review_mac_swept_controls.py` | [Transport](MAC_TRANSPORT.md) |
| `ma_mac_swept_reconstruction_tests` | `--write-checkpoint`/`--read-checkpoint <file>` | `review_mac_swept_reconstruction.py` | [Transport](MAC_TRANSPORT.md) |
| `ma_mac_closed_world_tests` | | `review_mac_closed_world.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_gravity_tests` | | `review_mac_gravity.py`, `analyze_mac_gravity_linearization.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_gravity_range_tests` | | `review_mac_gravity_range.py` | [Range](MAC_RANGE.md) |
| `ma_mac_compression_tests` | | `review_mac_compression.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_compression_owner_tests` | | `review_mac_compression_owner.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_acoustic_compression_tests` | | `review_mac_acoustic_compression.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_pressure_sensor_tests` | | `review_mac_pressure_sensor.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_compressive_work_tests` | | `review_mac_compressive_work.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_compressive_pressure_tests` | | `review_mac_compressive_pressure.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_compressive_owner_tests` | | `review_mac_compressive_owner.py` | [Pressure](MAC_PRESSURE.md) |
| `ma_mac_momentum_limiter_tests` | | `review_mac_momentum_limiter.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_momentum_reconstruction_tests` | `--quadratic` | `review_mac_momentum_reconstruction.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_momentum_owner_tests` | `--quadratic` | `review_mac_momentum_owner.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_quadratic_limiter_tests` | `--write-checkpoint`/`--read-checkpoint <file>` | `review_mac_quadratic_limiter.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_dual_affine_diagnostic` | | `review_mac_dual_affine.py`, `review_mac_dual_affine_energy.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_swept_momentum_tests` | `--owner`, checkpoint flags | `review_mac_swept_momentum.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_bounded_energy_tests` | `--owner`, checkpoint flags | `review_mac_bounded_energy.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_physical_admission_tests` | `--bounded-energy` | `review_mac_physical_admission.py` | [Momentum](MAC_MOMENTUM.md) |
| `ma_mac_nodal_conversion_tests` | | `review_mac_nodal_conversion.py` | [Nodal conversion](MAC_NODAL_CONVERSION.md) |
| `ma_mac_shock_heat_tests` | `--bracket-regression`, `--source-regression`, `--entropy-regression` | `review_mac_shock_heat.py`, `review_mac_heat_entropy.py` | [Heat](MAC_HEAT.md) |
| `ma_mac_matched_heat_tests` | | `review_mac_matched_heat.py` | [Heat](MAC_HEAT.md) |
| `ma_mac_series_heat_tests` | | `review_mac_series_heat.py` | [Heat](MAC_HEAT.md) |
| `ma_mac_shock_heat_owner_tests` | `--quadratic-compression`, `--legacy-heat`, `--matched-heat`, `--series-heat` | `review_mac_shock_heat_owner.py` (flags differ; see [Heat](MAC_HEAT.md#reproduce)) | [Heat](MAC_HEAT.md) |
| `ma_mac_thermal_tests` | | (CTest only) | [Heat](MAC_HEAT.md#physical-conduction-and-stored-energy-rounding) |
| `ma_mac_mixture_thermal_tests` | | `review_mac_mixture_thermal.py` | [Gas/liquid conduction](../docs/native-domain-physics.md#mixed-cell-conduction) |
| `ma_mac_viscosity_tests` | | `review_mac_viscosity.py` | [Viscosity](../docs/native-domain-physics.md#physical-viscosity) |
| `ma_mac_mixed_transfer_tests` | | `review_mac_mixed_transfers.py` | [Coexistence](../docs/native-domain-physics.md#occupied-gasliquid-cells) |
| `ma_mac_energy_tests` | | `review_mac_energy.py` | [Range](MAC_RANGE.md) |
| `ma_mac_energy_transport_tests` | | `review_mac_energy_transport.py` | [Range](MAC_RANGE.md) |
| `ma_mac_primary_parcel_tests` | | `review_mac_primary_parcel.py` | [Range](MAC_RANGE.md) |
| `ma_mac_momentum_parcel_tests` | | `review_mac_momentum_parcel.py` | [Range](MAC_RANGE.md) |
| `ma_mac_canonical_range_tests` | | `review_mac_canonical_range.py` | [Range](MAC_RANGE.md) |
| `ma_mac_exponent_tests` | | `review_mac_exponent.py` | [Range](MAC_RANGE.md) |
| `ma_mac_checkpoint_tests` | `--write-checkpoint[-no-heat\|-quadratic]`/`--read-checkpoint <file>` | (CTest) | [Checkpoints](MAC_CHECKPOINT.md) |
| `ma_mac_riemann_tests` | | `review_mac_riemann.py [--partial]` | [Strong flow](MAC_STRONG_FLOW.md) |
| `ma_mac_curved_nodal_diagnostic` | | `review_mac_curved_nodal.py` | [Strong flow](MAC_STRONG_FLOW.md) |
| `ma_mac_equilibrium_tests`, `ma_mac_playtest_benchmark` | | (CTest; `tools/run_mac_playtest_benchmark.py`) | [Readiness](../docs/playtesting-readiness.md#reproduce-measurements) |

The checkpoint flags write a fixture image on one platform and read it on another;
the reader must reserialize the original bytes exactly. Continuation must match
on the reading platform and binary; trajectories from different compilers need
not be bit-identical (see [checkpoint controls](MAC_CHECKPOINT.md#verification)).
