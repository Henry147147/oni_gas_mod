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
pressure formulation and the shared geometric multigrid preconditioner
(`mac_pressure_multigrid.*`) are in. Larger calm steps remain open. It covers every selector the ONI domain uses. The
real-time gate is not met; see [performance](#earlier-jacobi-performance), the
[historical comparison](#historical-comparison-with-the-retired-interval-port)
and [remaining work](#remaining-work).

## Selection

- `MacClosedSettings::numerical_kernel = MacNumericalKernel::production`:
  persisted selection 2, checkpoint version 6. It includes the exact-equilibrium
  certificate and the production kernel for each flow interval. The owner's
  diffusion half steps compose with it. Ordinary restore/advance needs no
  caller policy to continue this selection.
- `MacExecutionPolicy::production`: explicitly opts the returned owner into
  that saved selection, including certified equilibrium intervals.
- `advance_mac_production(world, seconds, budget, options)`: flow only, without
  a certificate; also persists selection 2. It rejects enabled diffusion and
  unported selectors. The selected owner route uses an audited reference for
  unported selectors/range flags without changing the physical settings.
- The former interval solver and admission bridge are removed. Selector 1 is
  reserved and rejects; version-4 and version-5 selector-1 images explicitly
  reject as retired. They are never restored under a different kernel.

Numerical preconditioning belongs to the saved `MacClosedSettings`:
`automatic` uses geometric multigrid for grids of at least 256 cells, otherwise
Jacobi; explicit `jacobi` stays Jacobi. Explicit incomplete Cholesky is
unported. `MacKernelOptions` contains thread scheduling and diagnostics, with no
unsaved preconditioner override. Supported legacy reference images keep their
bytes and continuation; selector-1 images explicitly reject. Version 6 rejects every kernel
value except 2. The [wire contract](MAC_CHECKPOINT.md) specifies disabled and
enabled diffusion metadata.

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

Swept material reconstruction (2), momentum selectors 2–5, coupled
compressive pressure (3) and explicit incomplete Cholesky are rejected by the
flow-only helper. A selected production owner uses the audited reference for
these settings, retaining their selectors and selection 2 in its next image.

Range: a nonfinite or overflow-range value (above `2^450`) in the input or
at the interval endpoint runs the whole interval through the reference
implementation, so full-range arithmetic is used only for flagged values.
Uncertain material-temperature or caloric boundaries also request the strict
reference, retaining consumed attempts. A certified static state can advance
unchanged at such a boundary. Below-range values are admitted. Roundoff-level transverse momentum on large
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
correction is a matrix-free PCG solve honoring the saved pressure setting
(automatic geometric multigrid, or explicit Jacobi) with an inexact-Newton tolerance
(`0.1 * residual`). Canonical J, wall and body impulses and midpoint sweeps
use the reference formulas. Wall-reaction pressure components gather in
double-double before the common timestep/area product; rounding each large
opposing wall impulse separately can erase a small net reaction.

The geometric preconditioner uses [the independently reviewed hierarchy](MAC_MULTIGRID.md).
Its topology and mutable workspace are retained on the calling thread, keyed
by exact geometry and the pressure-active mask. Every solve replaces all
coefficients; vacuum arrival changes the mask and rebuilds before use.
Empty-side zero-traction terms stay in the fine diagonal. Production supplies
its fixed-block pool to the V-cycle; no interval workers execute this path.
An invalid preparation selects Jacobi before PCG; a failed application rejects
the candidate. The old aggregation implementation has been removed. Workspace
bytes include retained multigrid topology and numerical arrays.

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
wrapper around the kernel. Persisted production selection opts diffusion into
[counted shared-transfer rounding](MAC_DIFFUSION.md#counted-transfer-rounding);
the strict reference keeps its existing admission. A
diffusion-enabled owner gets the same composition and composed energy
account with either policy; the unit suite compares the two on the room
scene with physical diffusion (roundoff agreement). Counted transfers are
combined into the owner's interval report. `stage_diffusion` stays
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
| Wave 16x16, 20 Pa (256, 2) | 21 | 1.7053e-12 | 5.09317e-11 | 1.01858e-14 | 1.08385e-14 | 1.00957e-14 |
| Wave 32x32 (1,024, 2) | 21 | 1.53477e-12 | 5.96629e-11 | 1.19324e-14 | 1.21248e-14 | 1.04868e-14 |
| Uniform translation 1 / 0.5 m/s (256, 2) | 21 | 0 | 0 | 0 | 0 | 0 |
| Hydrostatic gas, gravity 9.81, 5 Pa mode (128, 2) | 21 | 1.25056e-12 | 2.53203e-10 | 1.26811e-14 | 1.41693e-14 | 1.02873e-14 |
| Conduction, 30 K hot block (256, 2) | 21 | 1.7053e-12 | — | 1.46974e-14 | 1.43296e-14 | 1.06008e-14 |
| Gas/water mixture, solid corner, conduction (144, 2) | 21 | 3.41061e-12 | — | 0 | 9.18564e-16 | 1.91379e-14 |
| Filled water, gravity, 200 Pa mode (64, 2) | 21 | 5.17275e-12 | 3.61794e-08 | 3.22068e-11 | — | 3.29658e-15 |
| Wave with viscosity 0.5/0.25 Pa s and flow (256, 2) | 21 | 5.15286e-10 | 3.0041e-08 | 6.00786e-12 | 6.00781e-12 | 4.29144e-12 |
| Room: interior wall, 1 kPa / 10 K step, conduction (384, 2) | 21 | 1.53477e-12 | 1.1496e-12 | 1.14251e-14 | 3.2265e-14 | 2.67967e-14 |
| Gas into exact empty cells, dt 1/4096 s (128, 1) | 410 | 1.0175e-11 | 1.73168e-14 | 1.75277e-14 | 1.84091e-14 | 3.19285e-14 |

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
persisted production admission and range fallback, and that every audit
quantity passes. Both suites are run under ASan/UBSan (Debug, `-O1
-D_GLIBCXX_ASSERTIONS`, fatal findings) and ThreadSanitizer; see the commit
messages for the exact coverage of each run.

`ma_mac_kernel_multigrid_tests` adds nine explicit geometric-preconditioner
controls: odd periodic waves (including 33,389 cells), an interior wall,
filled liquid with gravity, moving gas/liquid mixtures, hydrostatic gas,
conduction, viscosity and gas arrival into empty cells. It compares the
reference within the original tolerances, checks identical checkpoints with
1/12 workers and a fresh-thread 4-worker restore, rejects exhausted budgets
atomically and compares concurrent heated/unheated branches. The largest
temperature difference is 9.36e-8 K. These controls pass GNU 15.2 Release,
Debug ASan/UBSan, Debug ThreadSanitizer and MinGW 13 under Wine 10.

`ma_mac_kernel_checkpoint_tests` checks fifteen saved-setting continuation
combinations (gas, liquid, mixtures, empty cells; automatic/Jacobi; diffusion
off/physical/100× where admitted), including ordinary restore/advance with
changed worker counts. It checks the equilibrium certificate, resealed wrong
versions/selectors, malformed diffusion settings, every truncation and an
explicit-IC reference fallback. A mid-interval range flag must retain selection
2, consume its discarded attempt, pass exact interval accounts and leave the
source unchanged when its budget is exhausted.

`ma_mac_kernel_saved_tests` (CTest `mac_production_saved_accounts`) carries
all seven original interval-port scientific scenes into production: periodic gas,
walls, a one-dimensional strip, 10,000 steps, 81 species/82 passives, periodic
multigrid and disconnected multigrid. `review_mac_kernel.py` independently
recomputes their original strict endpoint mass, passive, impulse, U+K and entropy
bounds, then checks the production report's stage magnitudes and below-range
operand allowance. It passes 105,985 checks and rejects seven altered captures.
The reviewer exposed wall-reaction cancellation errors; the pressure-component
gather fixes them without changing physical state or gate bounds.

The saved controls also cover changed geometry, heat/mass edits, changed
constituent/passive schemas and caloric models, source lifetime and concurrent
owners. Four formerly declined inputs retain their scientific setup and now
exercise production admission; unported selectors use the audited reference.
Material-boundary uncertainty refines through the reference, while exact static
states can take their certificate. Mid-interval range flags retain consumed
attempts and atomic rollback. The former interval solver/cache is removed;
selector-1 construction and its historical checkpoints explicitly reject.

The Release build enables tests, the ONI backend and warnings as errors; all
45 CTests pass. Nine touched/owner suites pass Debug ASan/UBSan and MinGW 13
under Wine 10. Five production/range suites (checkpoint, contract, multigrid,
saved accounts and diffusion rounding) pass ThreadSanitizer. The
independent production review passes on Release, ASan/UBSan, TSan and Wine.
Against `c23bc36`, all 60 preceding MAC executable exits and physical outputs
agree: 58 output streams are byte-identical and two differ only in timing.
The new diffusion-rounding suite independently checks quantized Heun transfers,
shared-budget rollback, tiny ternary retry and spreading-front continuation.
Its saved reader passes 2,690 checks/six corruptions on all four routes; the
four 400-frame 8,192-cell captures pass 131,154 checks/three corruptions. Bulk
metrics remain unchanged and the strict aggregate helium bound is retained.

The default reference wave checkpoint SHA-256 remains
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
Five-interval 256- and 4,096-cell production wave checkpoints also retain every
version-6 byte against the preceding binary. Fresh-process gas and multigrid
images from Linux and Wine continue byte-identically on both, in both source
directions. These are Wine controls, separate from actual Windows OS evidence.
Six synthetic version-2/3/4/5 images retain every byte of the previous writer;
reference images restore unchanged and selector-1 images explicitly reject.
The original strong-flow, curved-nodal and vacuum-front failures remain open.
The preconditioner-port Ubuntu 22.04 snapshot passed 43 tests with library glibc
requirements at most 2.34; it predates version 6, so the next game package needs
a fresh compatibility build. No new game, actual Windows OS or active-cell
performance acceptance is claimed here.

The playtest benchmark compares against the reference automatically:
`tools/run_mac_playtest_benchmark.py --kernel production --profile reference
--scene wave --continuation` gave 8.2e-11 of the disturbance after three
consecutive intervals at 4,096 cells and 8.0e-11 at 16,384.

## Earlier Jacobi performance

These measurements precede persisted selection 2 and automatic geometric
multigrid. They remain historical diagnostics; current active-cell acceptance
is open.


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

The comparison below predates the geometric preconditioner port and measures the Jacobi default.

Phase 1 stage breakdown (4,096 cells, default selectors, one thread, ns per
cell-substep): pressure root and PCG 410, momentum transport 275, shock heat
265, reconstruction 225, compression 215, primal gather/heat/gravity 70,
EOS anchors 40, audit 28.

## Historical comparison with the retired interval port

The narrower gas port used numerical selection 1 and a separate pool. Production
covers the game's current physical selectors and deterministic fixed-block pool.
The accepted consolidation now includes the same geometric multigrid, version-6
saved selection and independent endpoint reviewers. Selector 1 is retired rather
than silently migrated; the strict reference remains the oracle.

Earlier Jacobi comparisons measured about 1.16 s per 0.1 s at 250,000 cells on
twelve workers. Different physical selectors, pressure settings, host load and
range fallbacks affected the retired port's figures. These historical comparisons
do not certify current automatic-multigrid active-cell performance.

## Remaining work

- Real-time target: open. Earlier Jacobi runs take 1.16 s per 0.1 s at
  250,000 cells on twelve threads and 0.40 s at 100,000 cells, against 62.5/25 ms.
  Current automatic-multigrid active-cell timing is not yet measured.
- Persisted production selection and automatic geometric multigrid are
  implemented; active-cell timing with these defaults is not yet measured.
  The saved reviewers are ported and the narrower interval kernel is retired.
  Larger calm-region steps with local subcycling remain open.
- Thread scaling stops near 3–3.7 times on 12 threads on the shared host.
  Fewer, larger parallel regions (fused sweep stages, a PCG variant with one
  reduction per iteration) and a measurement on an idle host are the next
  steps.
- Diffusion inside the kernel (the stage slot above), swept reconstruction,
  momentum selectors 2–5 and coupled compressive pressure are not ported.
- Below-range transients inside an interval round without per-value counting;
  only interval endpoints and inputs are range-checked.
