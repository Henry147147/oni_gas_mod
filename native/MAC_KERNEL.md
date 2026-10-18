# Production substep kernel

`mac_kernel*.cpp` is a structure-of-arrays binary64 implementation of the
complete [`MacClosedWorld`](MAC_SOLVER.md) substep under the
[production contract](MAC_SOLVER.md#numerical-contracts). It keeps the owner
API, physical model, selectors, substep order, retry schedule and the
`MACCLS01` checkpoint format. The reference implementation stays the
validation oracle.

Status: phase 1 (single-threaded flux-form kernel, oracle validation,
benchmark option) and the deterministic block-parallel structure are done.
It covers every selector the ONI domain uses. The real-time gate is not met;
see [performance](#performance) and [remaining work](#remaining-work).

## Selection

- `MacExecutionPolicy::production` on `MacClosedWorld::advanced()`: the
  exact-equilibrium certificate first, then the production kernel. This is an
  execution choice only: no setting or checkpoint byte changes.
- `advance_mac_production(world, seconds, budget, options)`: the kernel alone
  (no certificate), with explicit `MacKernelOptions` (thread count).
- `MacNumericalKernel::interval_audited` (the persisted selection added in
  de805b7, [interval kernel](MAC_INTERVAL_KERNEL.md)): when that first port
  declines a configuration it does not cover, the production kernel takes it
  if its own admission passes, before falling back to the reference.

The thread count never changes a result (see [determinism](#determinism)).
`set_mac_kernel_default_options()` sets the process-wide options used by the
policy paths.

## Coverage and admission

| Setting | Production kernel |
| --- | --- |
| Material reconstruction | 0 donor cell, 1 limited linear |
| Momentum reconstruction | 0 donor cell, 1 limited linear |
| Compression | 0 none, 1 quadratic, 2 pressure-sensed acoustic |
| Shock heat | 0, 1, 2, 3 |
| Conduction, gravity, viscosity | yes |
| Cells | gas, filled liquid (single or several liquids), gas/liquid mixtures, exact empty cells, solids |

Swept material reconstruction (2), momentum selectors 2–5 and coupled
compressive pressure (3) are rejected with `std::invalid_argument` on the
explicit production path; the interval-kernel path then uses the reference.
Admission also requires normal-range inputs: no full-range canonical
momentum and every nonzero stored value within `2^-450..2^450`. A flagged
input (or endpoint) runs the whole interval through the reference
implementation, so full-range arithmetic is used only for flagged values.

## Design

**Storage.** Species-major masses for present constituents only, U, one
column per present passive identity, and face J. Owner states are converted in
and out once per interval; the result owner reuses the copied per-cell vectors.

**EOS** (`mac_kernel_eos.cpp`). The reference ideal-gas/zero-expansion
elastic-liquid model in binary64: closed forms for gas-only and single-liquid
cells, safeguarded Newton roots for mixtures and several liquids. Per-cell
statistics (mass, Cv, offsets, moles, nominal liquid volume) and face
inertias are cached until transport changes the masses; between transports a
gas or liquid reading is a few operations (liquid pressure does not depend on
U at fixed volume).

**Pressure** (`mac_kernel_pressure.cpp`). The reference implicit EOS work with
midpoint sweeps, solved for per-cell volume changes in the increment form
`W = p0 + dW(dV)`. Gas increments use a cancellation-free series of
`(1-(1+x)^-k)/(kx)` for `|x| < 1/4`, liquids a closed form in `s = -dV/V1`,
mixtures the reference divided differences. Node pressure differences gather
`(p0_l - p0_r) + (dW_l - dW_r)`, so there is no absolute-pressure roundoff
floor; the root reaches the reference 16-epsilon closure. Each Newton
correction is a matrix-free PCG solve (Jacobi; an aggregation multigrid
option exists but is not tuned) with an inexact-Newton tolerance
(`0.1 * residual`). Canonical J, wall and body impulses and midpoint sweeps
use the reference formulas.

**Transport** (`mac_kernel_transport.cpp`). Limited-linear reconstruction with
the reference stencil, limiter points, positive-field and EOS admission
backtracking. Each donor writes its outgoing face parcels once; each target
gathers retained plus incoming parcels, so every constituent and passive is
moved by one shared face quantity. Dual J/K transport uses the reference
primal-to-dual edges, upwind mixing heat and wall removal, and the
limited-linear corrections with the reference pair limiter. Heat goes to cells
by new half-mass shares; gravity adds the reference body-work/potential-flux
adjustment.

**Regularization and physical stages.** Compression (modes 1/2), shock heat
(modes 1–3), conduction (backward Euler for temperature changes, pairwise
complete-EOS contacts when mixtures are present) and viscosity (rank-one
modes) use the reference pair/mode laws.

**Exact reference order.** Every reference Gauss–Seidel sweep (compression,
shock heat, momentum corrections) chains only along grid rows or columns, and
lines are independent. The kernel runs each line sequentially in the
reference order and lines in parallel. A colour-ordered variant gave up to
2.4 K / 1% differences at a vacuum front where the momentum limiter binds;
line order reproduces the reference to roundoff. Viscosity modes and mixed
conduction contacts couple both directions: viscosity uses a fixed 9-colour
order, mixed conduction runs sequentially.

**Diffusion slot.** `stage_diffusion` is reserved for the gas-diffusion stage
being merged on main (`mac_diffusion*.cpp`). That stage wraps flow intervals
in diffusion half steps at the owner level; once merged, the production
policy keeps the wrapper around the kernel (diffusion stays reference) until a
flux-form diffusion stage fills this slot: per-face antisymmetric species
fluxes on the species-major columns, accumulated into the same interval audit.

## Production contract

- **Conservation by construction.** Primal parcels, dual fluxes, pair
  impulses/heat and conduction fluxes are shared face quantities applied to
  both sides.
- **Interval audit.** Species, passives, both momentum components (with wall
  and body impulses) and U+K+G are reconciled once per interval with the
  rule of `mac_interval_audit.hpp`: double-double sums, an outward error
  envelope, exact `MacEnergyAccount` replay when undecidable. Operands are
  both endpoints, external impulses and every stage's operand magnitudes, so
  the 128-epsilon bound scales with the work actually done. Totals come from
  fixed row-block partials. A failed audit discards the interval and retries
  it with half the maximum step, then faults.
- **Rounding.** Primary storage is binary64; updates that leave a stored value
  unchanged are counted (`unresolved_updates`).
- **Entropy.** The interval rejects a decrease of total stored-state entropy
  beyond `128 eps sum(|U|/T)` over both endpoints.
- **Unchanged.** Atomicity (a failed interval publishes nothing), checkpoint
  continuation, fail-closed admission and the arithmetic environment check on
  every executing thread.

## Determinism

Rows are grouped into fixed blocks (about 1,024 cells each) that depend only
on the grid shape. Each block is processed by one thread; reductions combine
per-block partials in block order; line sweeps assign whole lines to blocks.
The thread pool's exception propagation rethrows the lowest failing block.
Results are therefore bit-identical for any thread count.

## Validation

`ma_mac_kernel_validation_tests` (CTest `mac_production_kernel_oracle`)
advances each scene with the reference policy and with the production kernel
for the same intervals (identical substep schedules) and reports every
completion-plan metric. Tolerances: 0.1 K; 1% for pressure (of the
disturbance amplitude where a disturbance is defined), O2 partial pressure and
every constituent including the 1e-28 helium trace.

| Scene (cells, intervals) | Substeps | max dT (K) | max dp / disturbance | max dp / p | O2 pp | constituents |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Wave 16x16, 20 Pa (256, 2) | 21 | 1.6e-12 | 6.0e-11 | 1.2e-14 | 1.1e-14 | 1.0e-14 |
| Wave 32x32 (1,024, 2) | 21 | 1.5e-12 | 5.8e-11 | 1.2e-14 | 1.2e-14 | 1.1e-14 |
| Uniform translation 1 / 0.5 m/s (256, 2) | 21 | 0 | 0 | 0 | 0 | 0 |
| Hydrostatic gas, gravity 9.81, 5 Pa mode (128, 2) | 21 | 1.3e-12 | 2.5e-10 | 1.3e-14 | 1.4e-14 | 1.0e-14 |
| Conduction, 30 K hot block (256, 2) | 21 | 1.8e-12 | — | 1.2e-14 | 1.2e-14 | 9.9e-15 |
| Gas/water mixture, solid corner, conduction (144, 2) | 21 | 3.4e-12 | — | 0 | 9.2e-16 | 1.9e-14 |
| Filled water, gravity, 200 Pa mode (64, 2) | 21 | 5.2e-12 | 3.6e-8 | 3.2e-11 | — | 3.3e-15 |
| Wave with viscosity 0.5/0.25 Pa s and flow (256, 2) | 21 | 5.2e-10 | 3.0e-8 | 6.0e-12 | 6.0e-12 | 4.3e-12 |
| Room: interior wall, 1 kPa / 10 K step, conduction (384, 2) | 21 | 1.4e-12 | 1.1e-12 | 1.1e-14 | 2.8e-14 | 2.3e-14 |
| Gas into exact empty cells, dt 1/4096 s (128, 1) | 410 | 1.0e-11 | 1.7e-14 | 1.8e-14 | 1.8e-14 | 3.2e-14 |

Every scene passes with at least five orders of margin. The empty-cell scene
uses a fixed small step: at the default step both implementations reject and
halve near the same volume-CFL threshold, and a one-ulp difference can choose
a different retry sequence. The viscosity scene keeps the largest difference
because its modes use a colour order rather than the reference raster order.

`ma_mac_kernel_unit_tests` (CTest `mac_production_kernel_contract`) checks
byte-identical checkpoint continuation (wave, room, liquid, mixture, empty
cells), bit-identical results for 1, 2, 4, 8 and 12 threads (odd grid sizes,
walls, gravity, conduction, viscosity), atomic budget exhaustion, rejection of
swept/coupled selectors, the production policy's equilibrium certificate, the
opt-in interval-admission extension and range fallback, and that every audit
quantity passes. Both suites are run under ASan/UBSan (Debug, `-O1
-D_GLIBCXX_ASSERTIONS`, fatal findings); see the commit messages for the
exact coverage of each run.

The playtest benchmark compares against the reference automatically:
`tools/run_mac_playtest_benchmark.py --kernel production --profile reference
--scene wave --continuation` gave 8.2e-11 of the disturbance after three
consecutive intervals at 4,096 cells and 8.0e-11 at 16,384.

## Performance

Method: `ma_mac_playtest_benchmark` through `tools/run_mac_playtest_benchmark.py
--kernel production --profile reference --scene wave --continuation`, GNU
15.2 Release/IPO, default production selectors (limited-linear material and
momentum, pressure-sensed compression and shock heat), 0.01 s maximum step,
three consecutive 0.1 s samples per run, process pinned to one logical CPU
with `taskset`, three runs per size (two at 250,000). The host was shared:
1-minute load 21–30 during these runs. Process CPU time is recorded beside
wall time; they agreed within 10% here. ns per cell-substep is the median
wall time divided by cells times substeps per sample (10.3 substeps per 0.1 s,
as the reference schedule).

Phase 1, one thread:

| Cells (shape) | ms per 0.1 s (p50, three runs) | ns per cell-substep | Reference ms per 0.1 s |
| ---: | --- | ---: | ---: |
| 4,096 (64x64) | 60, 65, 79 | 1,420–1,870 | 7,560 |
| 16,384 (128x128) | 310, 332, 349 | 1,830–2,060 | 36,231 |
| 65,536 (256x256) | 1,359, 1,411, 1,418 | 2,010–2,090 | 145,569 (2048x32) |
| 250,000 (500x500) | 6,014, 6,187 | 2,330–2,400 | aborted (scaling analysis) |

Reference figures are from the [scaling analysis](../docs/solver-scaling-analysis.md)
(same host, thread CPU time, four pinned cores). The production kernel is
about 100–120 times faster at the same physics, selectors and schedule.

Comparison with the de805b7 interval port on its admitted configuration
(donor-cell material and momentum, no regularization, gas only), one thread,
same host and hour, thread CPU per 0.1 s:

| Cells | de805b7 | Production kernel |
| ---: | --- | --- |
| 4,096 | 97.8 ms, 11 substeps (2,170 ns per cell-substep) | 32.0 ms, 10 substeps (781 ns) |
| 250,000 | 6,547 ms, 11 substeps (2,381 ns) | 2,406 ms, 10 substeps (962 ns) |

The port takes a remainder substep per interval because it restarts its clock
at zero; the production kernel reproduces the reference's absolute-clock
schedule.

Stage breakdown (4,096 cells, default selectors, one thread, ns per
cell-substep): pressure root and PCG 410, momentum transport 275, shock heat
265, reconstruction 225, compression 215, primal gather/heat/gravity 70,
EOS anchors 40, audit 28.

## Relation to the interval kernel (de805b7)

[`MAC_INTERVAL_KERNEL.md`](MAC_INTERVAL_KERNEL.md) describes a first port
selected by the persisted `MacNumericalKernel::interval_audited` setting
(checkpoint version 4) for donor-cell occupied ideal gas without
regularization, gravity, conduction or viscosity. This kernel:

- uses that port's audit decision rule (`mac_interval_audit.hpp`) for its own
  interval audit, computed from parallel block partials;
- covers the production-default selectors and the remaining ONI physics,
  validated against the reference to roundoff;
- reproduces the reference absolute-clock schedule (no remainder substep);
- is reachable through the persisted setting only when
  `MacKernelOptions::extend_interval_admission` is set (default off, so the
  port's fallback behaviour and tests are unchanged).

Whether the persisted setting or the execution policy should select the
production kernel, and whether one implementation should replace the other,
is an open ownership decision.

## Remaining work

- Real-time target: not met. At 250,000 cells one thread takes about 6 s per
  0.1 s against 62.5 ms; deterministic threading, per-stage cost reduction and
  larger calm steps (in progress elsewhere) are all required.
- Larger calm-region steps with local subcycling and a tuned multigrid
  preconditioner are not part of this kernel; the multigrid option here is an
  untuned aggregation V-cycle.
- Gas diffusion (the stage slot above), swept reconstruction, momentum
  selectors 2–5 and coupled compressive pressure are not ported.
- Below-range transients inside an interval round without per-value counting;
  only interval endpoints and inputs are range-checked.
