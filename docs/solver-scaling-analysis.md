# Solver scaling analysis

Measured 2026-10-18 at `4f8e752`, with the 96×96 failure reproduced at
`fa8df8e`. The question: how far is `MacClosedWorld` from the accepted
real-time target, and what could close the gap? The target, from the
[completion plan](completion-plan.md) performance gate: 250,000 active fluid
cells and 32 duplicants at sustained 1× on a Ryzen 9 5900X, with p95
solver-plus-adapter cost at most 62.5 ms per 0.1 simulated seconds; and at
most 25 ms per 0.1 s for 100,000 active cells. Both targets allow about
250 ns of wall time per cell per 0.1 s.

**Conclusion.** The current exact-arithmetic substep cannot reach either
target. Tuning that keeps the existing contracts is estimated at 15–50×. The
measured gap is about 6,500–8,900× on one core, and 540–740× with ideal
scaling across all 12 cores. Separately, grids of about 9,000 cells or more
fail in their first substep (see [below](#exact-sum-capacity-failure)).

**Subsequent implementation:** the [exact-sum capacity follow-up](production-timestep-progress.md#large-grid-exact-sum-capacity)
removes that fixed ceiling without dropping terms or relaxing an account. Both
formerly failing small grids now finish; a 250,000-cell wave finishes one
0.01-second substep in about 30 seconds. The timing analysis below remains
historical evidence of the unresolved real-time gap, not current capacity status.

## Method

- **Benchmark:** `ma_mac_playtest_benchmark`, reference profile, wave scene,
  consecutive intervals, default physical settings.
- **Timing:** per-sample thread CPU time, pinned to four cores, three repeats
  per case.
- **Profiles and counts:** gperftools sampling profiles and exact
  single-step instruction counts.
- **Host load:** `perf` was unavailable, and two other development sessions
  kept the 1-minute load at 21–48 throughout. Thread CPU time stayed within
  9% of wall time, but it ran 6–14% above earlier quiet-host figures. The
  instruction counts below are not affected by load.
- **Files:** raw profiles, captures, scripts and the scratch prototype are
  kept in the ignored `local/profile-scaling/` directory.

## Scaling

| Cells (shape) | CPU ms per 0.1 s, median | Substeps per 0.1 s | Newton iterations per substep | CPU µs per cell-substep | Peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| 256 (16×16) | 464 | 10.2 | 1.96 | 178 | 8.7 MB |
| 1,024 (32×32) | 2,024 | 10.2 | 2.13 | 194 | 19.8 MB |
| 4,096 (64×64) | 7,560 | 10.3 | 2.43 | 179 | 62 MB |
| 16,384 (256×64) | 36,231 | 10.5 | 3.09 | 211 | 250 MB |
| 65,536 (2048×32) | 145,569 | 11.0 | 3.55 | 202 | 860 MB |
| ~250,000 (three shapes) | aborts in the first substeps | — | — | — | 2.0 GB at abort |

- **Linear cost:** cost is essentially linear in cell count.
- **Pressure solve:** the preconditioned conjugate-gradient solve stays at
  about 20 iterations and costs only 0.5% of the time.
- **Instructions:** one substep executes about 1.15 million instructions per
  cell, or 11.7 million per cell per 0.1 s. The budget is about 1,700
  (one core) or 20,000 (12 cores).
- **Memory:** peak memory is about 13 KB per cell. That projects to
  3.1–3.8 GB at 250,000 cells, against the 2 GiB target. Checkpoints are only
  about 190 B per cell; the peak is per-substep transient state.

## Where the time goes

| Stage | Share |
| --- | ---: |
| Parcel transport | 33% |
| Compression regularization | 19% |
| Shock heat | 19% |
| Material reconstruction | 15% |
| Pressure Newton solve (EOS ~6%, linear solve 0.5%) | 7% |
| Audits, setup, momentum sweeps | ~5% |

**By kind of work:**
- **Exact and range-extended arithmetic: 52–54%.** Each cell-substep does
  about 2,400 `MacEnergySum` additions and 200 bound checks; only 0.16% of the
  sums ever leave the normal binary64 range.
- **EOS evaluation: 29–30%.** About 34 `long double` evaluations per
  cell-substep.
- **Repeated validation: 5%.**
- **Allocation: 4%.** About 260 heap allocations per cell-substep.

Re-validation and audit frames hold 35% of the samples. Nothing in the solver
is multithreaded. Shock heat and compression use sequential Gauss–Seidel
sweeps, so naive threading is limited to about 2.3×.

## Timestep limit

The binding limit is the `maximum_timestep_s` setting of 0.01 s. Neither CFL
condition binds: the material CFL number is about 5e-4 against its 0.4
limit, and the acoustic one (about 3.5) is handled implicitly. At 0.02–0.1 s,
however, every attempt is rejected, because the pressure Newton iteration
converges quadratically and then stalls at a roundoff floor:

| Timestep | Stall level |
| ---: | ---: |
| 0.025 s | 1e-14 |
| 0.05 s | 6e-14 |
| 0.1 s | 1.1e-13 |

All three floors are above the 3.55e-15 (16 ε) volume-closure tolerance. A
plain binary64 prototype hits the same floor, so it comes from solving in
absolute pressure. Larger steps therefore need a different pressure
formulation, not just a larger setting.

**Subsequent implementation:** a bounded [pressure-increment restart](../native/MAC_PRESSURE_INCREMENTS.md)
now converges these timesteps at the original closure tolerance, gathering all
four pressure terms before rounding the node force. This removes the measured
cancellation floor. It does not authorize larger production steps: a completed
five-interval wave comparison still fails the disturbance-relative accuracy gate.

The `long double` EOS is load-bearing. With `double`, the floor reaches
7e-15 even at 0.01 s, the substep count doubles and the run is 2.4× slower.
MSVC's `long double` is 64-bit, so Windows builds may behave like the
`double` case; that has not been measured.

## Exact-sum capacity failure

Wave grids of 96×96 cells and larger, 64×128, and every ~250,000-cell shape
tried fail with `MAC derived energy expansion storage capacity exceeded`. The
failure is the diagnostic heat sum in `advance_mac_compression`
(`native/src/mac_compression.cpp`), which exceeds the 4,096-component
`MacEnergySum` limit. Roundoff-level transverse momenta produce compression
heats with exponents down to about -2.8e13, and each one keeps its own
expansion component; a 256×64 grid already reaches 3,510 components.

Dropping components to bound that growth would conflict with the no-drop, full-range contract
([MAC_RANGE.md](../native/MAC_RANGE.md)). The in-game configured limit of
4,096 cells (64×64) is below the failure size, but whole-world ownership is
not.

The follow-up instead retains the complete expansion in growing contiguous
storage and skips only provably unchanged low prefixes. It preserves the original
recurrence and exact conservation decisions. Arbitrary low insertions can still
scan the remaining expansion; complete memory and time scaling remain open.

## Options

| Change | Estimated speedup | Conflict with current contracts |
| --- | --- | --- |
| Bound expansion growth (required for large grids; also removes superlinear audit cost) | needed to run at all | Moderate: below-resolution diagnostics |
| Validate once per interval instead of per stage (removing only the `masses()` EOS recheck gave −9.4% with a byte-identical checkpoint) | 1.2–1.3× | Low |
| Share EOS readings, about 34 to 8 per cell-substep | 1.25–1.35× | Low |
| Double-double accounts that escalate to exact expansions only when a decision is undecidable | 1.6–2.2× | None if decisions stay exact |
| Workspaces, sparse species, compact event records (needed for the memory target) | 1.1–1.25× | None |
| Integer tick clock, removing remainder substeps | 1.02–1.1× | Low |
| 12-thread static partitions; multicolour sweeps | 2–2.3×; 6–9× | Moderate: outputs change, determinism kept |
| Perturbation-pressure solve with 0.05–0.1 s calm steps and local subcycling | 4–8× | High: plan approximation "larger calm-region steps" |
| Newton or tabulated liquid isentropes instead of a 57-step bisection | ~10× on liquid cells | Low |

The first seven rows together give about 15–50× (8–35 s per 0.1 s at 250,000
cells). Adding the calm-step row still leaves the solver 20–100× over the
target. Sleeping regions do not count toward the active-cell gate.

A scratch plain-binary64 prototype of a comparable pressure-implicit MAC
step was also measured. It uses a structure-of-arrays layout, flux-form
conservation and double-double global accounts. It is a different scheme,
not a port. It ran at about 557 ns per cell-substep on one core, roughly
200× fewer instructions than `MacClosedWorld`, or about 1.4 s per 0.1 s at
250,000 cells. Reaching the target from there would still need:
- a multigrid preconditioner, about 3–4×;
- 12 threads, about 6–9× if memory bandwidth allows;
- one or two steps per 0.1 s.

Together those project to about 30–80 ms per 0.1 s. None of this has been
built or validated.

## Path that could reach the target

- **Keep:** `MacClosedWorld`'s owner API, physical model, selectors and
  checkpoint format. The current implementation stays as the validation
  oracle.
- **Replace the substep kernel with:**
  - structure-of-arrays binary64 flux-form updates, conservative by
    construction;
  - double-double interval audits, with range extension only for cells a
    cheap check flags;
  - one EOS evaluation per cell per Newton iteration;
  - fused, multicolour-parallel regularization;
  - a perturbation-pressure, multigrid-preconditioned implicit solve
    admitting 0.05–0.1 s calm steps with local subcycling;
  - static 12-thread partitions.

**Decided 2026-10-18:** the user relaxed the per-stage contracts to
interval-level conservation audits
([production contract](../native/MAC_SOLVER.md#numerical-contracts)), and
accepted larger calm-region steps with local subcycling.

The two decisions this path needed:
- amending the per-stage 128 ε, full-range and 16 ε contracts in
  [MAC_SOLVER.md](../native/MAC_SOLVER.md) and
  [MAC_RANGE.md](../native/MAC_RANGE.md) to interval-level conservation
  audits;
- accepting the completion plan's larger calm-region step approximation.

Both decisions are now recorded in the production contract. The first
[interval-kernel port](../native/MAC_INTERVAL_KERNEL.md) implements the existing
donor-cell gas pressure/transport laws, reusable fields and interval audits. Its
initial 250,000-cell result is about 10.2 s per 0.1 simulated seconds on one core.
The projected multigrid, parallel and larger-step combination remains unvalidated;
no active-colony performance gate passes.
