# Production substep kernel

`mac_kernel*.cpp` is a structure-of-arrays binary64 implementation of the
complete [`MacClosedWorld`](MAC_SOLVER.md) substep under the
[production contract](MAC_SOLVER.md#numerical-contracts). It keeps the owner
API, physical model, selectors, substep order, retry schedule and the
`MACCLS01` checkpoint format. The reference implementation stays the
validation oracle.

Status: phase 1 (single-threaded flux-form kernel, oracle validation,
benchmark option) and phase 3 (deterministic multithreading: bit-identical
results for 1, 2, 4, 8 and 12 threads) are done. From phase 2 the increment
pressure formulation is in; main's multigrid preconditioner (written for the
interval kernel, `mac_pressure_multigrid.*`) and larger calm steps are not
part of this kernel. It covers every selector the ONI domain uses. The
real-time gate is not met; see [performance](#performance), the
[comparison with the interval kernel](#comparison-with-the-interval-kernel-originmain-66f9d11)
and [remaining work](#remaining-work).

## Selection

- `MacExecutionPolicy::production` on `MacClosedWorld::advanced()`: the
  exact-equilibrium certificate first, then the production kernel for each
  flow interval. With gas diffusion enabled, the owner's
  [diffusion](MAC_DIFFUSION.md) half steps wrap each flow interval exactly as
  for the reference policy. This is an execution choice only: no setting or
  checkpoint byte changes.
- `advance_mac_production(world, seconds, budget, options)`: the kernel alone
  (no certificate), with explicit `MacKernelOptions` (thread count). It
  advances flow only and rejects an owner with diffusion enabled.
- `MacNumericalKernel::interval_audited` (the persisted selection added in
  de805b7, [interval kernel](MAC_INTERVAL_KERNEL.md)): with
  `MacKernelOptions::extend_interval_admission` set (default off), when that
  first port declines a configuration it does not cover, the production
  kernel takes it if its own admission passes, before falling back to the
  reference.

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

Range: a nonfinite or overflow-range value (above `2^450`) in the input or
at the interval endpoint runs the whole interval through the reference
implementation, so full-range arithmetic is used only for flagged values.
Below-range values are admitted. Roundoff-level transverse momentum on large
grids decays far below binary64's normal range within a few intervals;
falling back for it made the 160x160 wave about 100 times slower than
necessary. Full-range canonical momentum is projected, subnormal results
round, and the audit counts up to `2^-1075` of absolute rounding per operand
per substep. A momentum-correction pair whose heat budget is below the
subnormal resolution takes the always-admissible zero correction, and a
compression pair with a closing speed below `2^-500` m/s whose midpoint
rounds away is skipped; both are counted.

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

**Diffusion slot.** Gas diffusion (`mac_diffusion*.cpp`) wraps each flow
interval in half steps at the owner level. The production policy keeps that
wrapper around the kernel (the diffusion implementation is main's), so a
diffusion-enabled owner gets the same composition and composed energy
account with either policy; the unit suite compares the two on the room
scene with physical diffusion (roundoff agreement). `stage_diffusion` stays
reserved for a flux-form diffusion stage inside the kernel: per-face
antisymmetric species fluxes on the species-major columns, accumulated into
the same interval audit.

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
per-block partials in block order; line sweeps assign whole lines to fixed
line blocks. The thread pool's exception propagation rethrows the lowest
failing block. Results are therefore bit-identical for any thread count.

The pool (`mac_kernel_parallel.cpp`) keeps its workers between parallel
regions. Threads claim blocks dynamically from one 64-bit ticket (region
generation and next block), so a descheduled worker delays only the block it
holds; which thread runs a block never changes the result. Idle workers pause
briefly, then yield, then sleep on a condition variable. Before a new region
changes the block count and job, the finished region's ticket is closed, so a
late worker cannot claim a block of the new region twice. The first pool
partitioned blocks statically; on the shared host one preempted thread then
stalled every region: at 65,536 cells 8 threads gained 1.1 times and 12 ran
at 0.56 times the one-thread speed.

Every executing thread checks the arithmetic environment before it claims a
block; a thread that fails the check records the fault on the blocks it would
have run.

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
-D_GLIBCXX_ASSERTIONS`, fatal findings) and ThreadSanitizer; see the commit
messages for the exact coverage of each run.

The playtest benchmark compares against the reference automatically:
`tools/run_mac_playtest_benchmark.py --kernel production --profile reference
--scene wave --continuation` gave 8.2e-11 of the disturbance after three
consecutive intervals at 4,096 cells and 8.0e-11 at 16,384.

## Performance

Method: `ma_mac_playtest_benchmark WIDTH HEIGHT 3 reference wave --continuation
--no-reference --kernel production --threads N` (what
`tools/run_mac_playtest_benchmark.py --kernel production` runs), GNU 15.2
Release/IPO, default production selectors (limited-linear material and
momentum, pressure-sensed compression and shock heat), 0.01 s maximum step,
three consecutive 0.1 s samples per run (10.3 substeps per 0.1 s, the
reference schedule), three runs per size and thread count. ms per 0.1 s is the
median sample of a run; ns per cell-substep divides it by cells times
substeps. The host (Ryzen 9 5900X, 12 cores, 24 logical CPUs) was shared with
a video encoder using about 11 cores and other builds: the 1-minute load was
21-30 for phase 1 and 24-51 for the current sweep. Processes were pinned with
`taskset` (one logical CPU for phase 1; logical CPUs 0-11, one per core, for
the current sweep). Process CPU time is recorded beside wall time.

Phase 1 (commit 08337ed), one thread:

| Cells (shape) | ms per 0.1 s (three runs) | ns per cell-substep | Reference ms per 0.1 s |
| ---: | --- | ---: | ---: |
| 4,096 (64x64) | 60, 65, 79 | 1,420–1,870 | 7,560 |
| 16,384 (128x128) | 310, 332, 349 | 1,830–2,060 | 36,231 |
| 65,536 (256x256) | 1,359, 1,411, 1,418 | 2,010–2,090 | 145,569 (2048x32) |
| 250,000 (500x500) | 6,014, 6,187 | 2,330–2,400 | aborted (scaling analysis) |

Reference figures are from the [scaling analysis](../docs/solver-scaling-analysis.md)
(same host, thread CPU time, four pinned cores). The production kernel is
about 100–120 times faster at the same physics, selectors and schedule.

Current kernel, one thread. Since phase 1: no integer division in face and
neighbour addressing (per-cell face table, column/row-carrying helpers),
stored inverse face inertias, the PCG direction update fused into the
operator application, and parallel load, store, field copies and audit
partials.

| Cells | ms per 0.1 s (three runs) | ns per cell-substep |
| ---: | --- | ---: |
| 4,096 | 61, 70, 88 | 1,450–2,070 |
| 16,384 | 231, 257, 269 | 1,360–1,590 |
| 65,536 | 900, 1,018, 1,064 | 1,330–1,570 |
| 250,000 | 3,563, 3,630, 3,803 | 1,380–1,470 |

Threads (median of three runs, speedup against the one-thread median of the
same sweep; every endpoint checkpoint of a size is byte-identical across
thread counts). The 4,096- and 16,384-cell rows are from a second sweep
(load 43–48) after the thread cap below changed; the 100,000-cell rows from a
third (load 18):

| Cells | Threads used | ms per 0.1 s | ns per cell-substep | Speedup | Process CPU ms per 0.1 s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4,096 | 1 | 52 | 1,233 | 1 | 52 |
| 4,096 | 2 | 47 | 1,118 | 1.10 | 94 |
| 4,096 | 4 (4, 8 or 12 requested) | 36–43 | 850–1,014 | 1.22–1.45 | 135–142 |
| 16,384 | 1 | 276 | 1,632 | 1 | 273 |
| 16,384 | 2 | 200 | 1,181 | 1.38 | 393 |
| 16,384 | 4 | 129 | 760 | 2.15 | 448 |
| 16,384 | 8 | 92 | 543 | 3.01 | 540 |
| 16,384 | 12 | 83 | 492 | 3.31 | 561 |
| 65,536 | 1 | 1,018 | 1,503 | 1 | 1,017 |
| 65,536 | 2 | 664 | 981 | 1.53 | 1,246 |
| 65,536 | 4 | 439 | 649 | 2.32 | 1,581 |
| 65,536 | 8 | 313 | 462 | 3.25 | 1,868 |
| 65,536 | 12 | 277 | 410 | 3.67 | 2,006 |
| 100,000 (400x250) | 1 | 1,477 | 1,430 | 1 | 1,465 |
| 100,000 | 12 | 397 | 384 | 3.72 | 2,989 |
| 250,000 | 1 | 3,630 | 1,405 | 1 | 3,618 |
| 250,000 | 2 | 2,445 | 946 | 1.48 | 4,723 |
| 250,000 | 4 | 1,510 | 584 | 2.40 | 5,546 |
| 250,000 | 8 | 1,226 | 474 | 2.96 | 7,394 |
| 250,000 | 12 | 1,164 (1,106–1,267) | 451 | 3.12 | 7,797 |

The kernel never runs more threads than row blocks
(`MacKernelOptions::minimum_blocks_per_thread`, default 1): idle workers
woken for every parallel region only cost time. At 4,096 cells (four blocks,
load 18) four threads gave 2.0 times one thread and twelve uncapped threads
1.6 times. An earlier build with the static-partition pool collapsed
instead (12 threads on 4,096 cells: 1.9–9.8 s per 0.1 s, 25–150 times slower
than one thread, load 30). Scaling beyond four threads is limited by the number of parallel
regions (two per PCG iteration, several per sweep stage) and by the shared
host; the spin-wait shows as process CPU time above the wall time.

Memory: retained kernel workspace (`MacKernelReport::workspace_bytes`: field
banks and scratch) is 4.1, 16.5, 66.2, 101.0 and 252.5 MiB at 4,096, 16,384,
65,536, 100,000 and 250,000 cells, about 1,060 bytes per cell. The whole
benchmark process (source and result owners, checkpoint, kernel workspace)
peaks at 609–611 MB RSS at 250,000 cells, 246 MB at 100,000 and 163 MB at
65,536, independent of the thread count.

The [comparison with the interval kernel](#comparison-with-the-interval-kernel-originmain-66f9d11)
measures both kernels on a common scene.

Phase 1 stage breakdown (4,096 cells, default selectors, one thread, ns per
cell-substep): pressure root and PCG 410, momentum transport 275, shock heat
265, reconstruction 225, compression 215, primal gather/heat/gravity 70,
EOS anchors 40, audit 28.

## Comparison with the interval kernel (origin/main fe31e7d)

[`MAC_INTERVAL_KERNEL.md`](MAC_INTERVAL_KERNEL.md) describes the port
selected by the persisted `MacNumericalKernel::interval_audited` setting, now
with main's [multigrid preconditioner](MAC_MULTIGRID.md) and
[deterministic pressure workers](../docs/interval-parallel-pressure.md)
(`MA_MAC_WORKERS`, default at most four participants). Both kernels were
measured on 2026-10-18/19 from builds made the same way (GNU 15.2
Release/IPO; the interval kernel from builds of origin/main 66f9d11 and
fe31e7d, the production kernel from this branch) with one scratch harness
through the public API: the fixture wave (20 Pa sinusoid along x, periodic,
300 K, O2 0.22, helium trace and two passives), one warm-up interval, then
three consecutive 0.1 s intervals; median interval of each run, three runs,
`taskset -c 0-11`. The host's video encoder used about 11 cores; the 1-minute
load was 17–31 for the 66f9d11 sweep and 28–70 for the fe31e7d sweep, so the
multi-thread figures in particular are noisy. The interval kernel built from
this branch gives byte-identical endpoints to the origin/main build, and its
endpoints are identical for 1, 4 and 12 workers.

**Coverage.**

| | Interval kernel | Production kernel |
| --- | --- | --- |
| Material / momentum reconstruction | 0 | 0, 1 |
| Compression / shock heat | 0 / 0 | 0, 1, 2 / 0–3 |
| Gravity, conduction, viscosity | no | yes |
| Phases | occupied constant-property ideal gas | gas, filled liquid, mixtures, exact empty cells, solids |
| Range | flags any nonzero value outside `2^-450..2^450` (reference fallback) | admits below-range values (counted rounding); only nonfinite or `>2^450` falls back |
| Gas diffusion | composed by the owner | composed by the owner (production policy) |
| Preconditioner | automatic geometric multigrid (default) or Jacobi | Jacobi; untuned aggregation option |
| Threads | pressure and thermodynamic passes on deterministic workers; transport serial | every stage on the deterministic pool, bit-identical for 1–12 threads |
| Substep schedule | restarts its clock each interval (11 substeps per 0.1 s here) | reference absolute-clock schedule (10) |

With the production-default selectors the interval kernel declines every
interval (reference fallback); only the production kernel runs them.

**Accuracy against the reference oracle** (66f9d11 build; same owner, same
selectors):

| Scene | Kernel | max dT (K) | max dp / 20 Pa | O2 partial pressure | constituents |
| --- | --- | ---: | ---: | ---: | ---: |
| 4,096 cells, donor, 3 intervals, automatic (interval kernel ran 2) | interval | 2.2e-12 | 7.0e-11 | 1.4e-14 | 1.2e-14 |
| | production | 2.6e-12 | 7.9e-11 | 1.6e-14 | 1.4e-14 |
| 4,096 cells, default selectors, 3 intervals | production | 2.2e-12 | 8.0e-11 | 1.7e-14 | 1.5e-14 |
| 65,536 cells, donor, 2 intervals, Jacobi | interval | 6.8e-13 | 3.9e-11 | 8.6e-15 | 6.3e-15 |
| | production | 6.3e-13 | 3.8e-11 | 8.3e-15 | 6.8e-15 |

Both agree with the reference to roundoff where they run.

**Speed, interval kernel's admitted selectors** (donor cell, no
regularization; fe31e7d sweep). ms per 0.1 s (wall, median of three runs),
with ns per cell-substep of process CPU and the kernel's own substep count in
parentheses for one thread:

| Cells | Interval, Jacobi, 1 worker | Interval, Jacobi, 4 / 12 workers | Production, 1 thread | Production, 12 threads |
| ---: | --- | --- | --- | --- |
| 4,096 | 91 (2,021 ns) | 91 / 82 | 22 (547 ns) | 17 (4 threads used) |
| 65,536 | 1,632 (2,252 ns) | 1,303 / 1,843 | 462 (698 ns) | 175 |
| 250,000 | 7,058 (2,540 ns) | 5,432 / 6,257 | 1,938 (769 ns) | 701 |

Process CPU per 0.1 s at 250,000 cells: interval 6,985 (1 worker), 7,760 (4),
8,453 (12); production 1,922 (1 thread), 4,065 (12). The interval kernel's
automatic preconditioner (its default) is not in this table: on this scene it
falls back to the reference for most intervals. At 65,536 cells it took
36,979 ms (66f9d11) and 38,770 ms (fe31e7d, default workers) per 0.1 s, with 2
of 3 measured intervals on the reference; one 250,000-cell interval took
120,752 ms (66f9d11, reference fallback). At 4,096 cells it ran (88 ms; its
multigrid cut PCG iterations per interval from 492 to 128 without reducing the
time).

With automatic preconditioning, multigrid roundoff gives the y-uniform wave
nonzero transverse momentum (smallest nonzero face value 4e-28 after the
first interval at 4,096 cells; with Jacobi it stays exactly zero), and the
interval kernel then declines intervals to the reference. Its range checks
also cover intermediate products (for example PCG dot-product terms below
`2^-450`), which tiny transverse components reach; which check fires was not
instrumented. It is the same class of fallback this kernel removed for itself
in c0cb51d.

**Production-default selectors** (production kernel only):

| Cells | 1 thread | 12 threads |
| ---: | --- | --- |
| 4,096 | 71 (1,719 ns) | 44 wall (4 threads used) |
| 65,536 | 1,134 (1,724 ns) | 285 wall |
| 250,000 | 5,260 (2,091 ns; runs 4,411–6,008) | 1,426 wall |

(The fixture keeps `MacClosedSettings` defaults, compression mode 1; the
playtest benchmark above uses compression mode 2.)

**Best configuration of each at 250,000 cells:** interval kernel with Jacobi
and four workers: 5,432 ms per 0.1 s (fe31e7d; 4,785–20,450 over three runs
at load 30–70); production kernel with 12 threads: 701 ms with the same
selectors (628–728), 1,426 ms with the production-default selectors (66f9d11
sweep).

**Thread scaling** (wall speedup over one thread, fe31e7d sweep). Interval
kernel, Jacobi: 1.0 / 1.1 at 4,096 cells (4 / 12 workers), 1.25 / 0.89 at
65,536, 1.30 / 1.13 at 250,000. Production, donor selectors: 1.3 (4,096,
four threads used), 2.6 (65,536) and 2.8 (250,000) on 12 threads; with the
default selectors 1.6, 4.0 and 3.7 (66f9d11 sweep).

**Memory.** Peak RSS above a setup-only process, per cell: interval kernel
with Jacobi 1,260–1,460 bytes; production 1,570–1,830 bytes (kernel workspace
970 bytes per cell with donor selectors, 1,060 with the default selectors);
any configuration that takes the reference fallback about 4,400 bytes.

**Checkpoint and selection.** The interval kernel is a persisted physical-
contract setting (checkpoint version 4, `numerical_kernel`). The production
kernel is an execution policy (`MacExecutionPolicy::production`), so it adds
no checkpoint bytes, and it reaches the persisted setting only through
`extend_interval_admission`. Both continue checkpoints byte for byte on the
same build. Because both kernels' trajectories differ from the reference at
roundoff, byte continuation of a saved game needs the same kernel after
restore, which argues for a persisted selection.

**What one kernel would take.**

1. Keep the production kernel's coverage, stages, absolute-clock schedule,
   below-range admission and deterministic pool.
2. Wire main's `PressureMultigrid::prepare(compliance, conductances)` /
   `apply` in as this kernel's PCG preconditioner (the operator is the same
   `diag + B diag(w) Bᵀ`) and keep its cached topology across intervals. Its
   smoothing and transfers already run on main's deterministic workers; they
   would move to one executor. The below-range admission of point 1 should
   keep multigrid roundoff in transverse momentum from forcing the reference
   fallback measured above (not measured: the multigrid has not run inside
   this kernel).
3. Persist the selection: a `numerical_kernel` value for this kernel (new
   checkpoint version) or `interval_audited` redefined to it, retiring the
   interval kernel's narrower admission.
4. Carry over the interval kernel's saved-data reviewers (100-digit
   recomputation of inventories, EOS, entropy and U+K) to the production
   selectors, and keep one thread pool. Main's executor parallelizes the
   pressure and thermodynamic passes (1.13–1.30 times at 250,000 cells
   above); this kernel's pool covers every stage (2.8 times with the same
   selectors), so the measured difference reflects coverage as much as the
   pools.

## Remaining work

- Real-time target: not met. At 250,000 cells 12 threads take about 1.16 s
  per 0.1 s (3.6 s on one thread) against 62.5 ms, a factor of about 19; at
  100,000 cells about 0.40 s against 25 ms, a factor of about 16 (wave scene,
  shared host).
- Main's geometric multigrid preconditioner ([MAC_MULTIGRID.md](MAC_MULTIGRID.md),
  `mac_pressure_multigrid.*`, serial) serves the interval kernel and is not
  wired into this kernel, whose default is Jacobi-PCG (about 37 PCG
  iterations per substep in the 250,000-cell wave; the multigrid option here
  is an untuned aggregation V-cycle). Its `prepare(compliance, conductances)` /
  `apply` interface matches this kernel's operator `diag + B diag(w) Bᵀ`.
  Larger calm-region steps with local subcycling are not part of this
  kernel either. Both reduce the substep and iteration counts that every
  number above is proportional to.
- Thread scaling stops near 3–3.7 times on 12 threads on the shared host.
  Fewer, larger parallel regions (fused sweep stages, a PCG variant with one
  reduction per iteration) and a measurement on an idle host are the next
  steps.
- Diffusion inside the kernel (the stage slot above), swept reconstruction,
  momentum selectors 2–5 and coupled compressive pressure are not ported.
- Below-range transients inside an interval round without per-value counting;
  only interval endpoints and inputs are range-checked.
