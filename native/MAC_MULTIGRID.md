# Interval-kernel pressure preconditioner

The occupied ideal-gas [interval kernel](MAC_INTERVAL_KERNEL.md) now uses a
geometric multigrid preconditioner for its existing matrix-free PCG solve.
`automatic` selects it for at least 256 active cells when the maximum ratio of
off-diagonal coupling to compliance is at least two. Explicit `jacobi` keeps
Jacobi. No pressure equation, root tolerance, physical selector or timestep
changes. The default numerical kernel remains `reference`.

## Operator and transfers

The fine operator is `A = diag(h) + B diag(g) Bᵀ`, where the positive
compliances come from the current work-pressure tangent and each nonnegative
conductance belongs to an open physical face. Periodic duplicate faces remain
distinct. Walls and disconnected pores remain disconnected.

Each geometric 2×2 bin aggregates only its connected components. The positive
coarse compliance is their sum. Boundary conductances are summed with the ratio
of fine to coarse centre spacing, including odd boundary-cell widths. This is
a rediscretized coarse preconditioner, not a Galerkin replacement for the fine
physical equations. Cell-centred linear interpolation has nonnegative weights
and preserves constants; restriction is its actual transpose. Missing or
ambiguous pore neighbours use constant interpolation locally.

Each application is one fixed symmetric V-cycle. Two diagonal-relaxation stages
use weights 1/2 and 3/2, reversed after coarse correction. The error polynomial
`p(t) = (1-t/2)(1-3t/2)` satisfies `|p(t)| < 1` on `0 < t <= 2`, the
diagonally scaled positive graph operator's spectral interval. The symmetric
smoothing contribution and positive coarse inverse give a positive symmetric
preconditioner. There is no inner adaptive stopping criterion. The last level
uses Cholesky for at most 24 unknowns, or a diagonal solve for any number of
isolated unknowns. Invalid coefficients or an unresolved coarse pivot select
Jacobi before PCG starts; a nonfinite application declines the interval kernel.

The ordinary fine solve retains its relative residual target of 1e-12 and
512-iteration bound. The outer pressure equation still checks actual geometric
closure against 16 binary64 epsilons. Completed-interval conservation, entropy
and range decisions remain unchanged.

## Cache and deterministic continuation

An immutable topology is built once and shared across unchanged owner geometry.
Heat and fluid transfers keep it; all numerical coefficients are replaced for
every Newton solve. Mutable workspaces belong to one advance and are reused
within its interval. A geometry-changing operation must replace the cache.
Checkpoint restoration rebuilds it. Cache state never changes the continued
physical result, so no derived cache bytes are serialized.

The first downward correction is computed directly from its exactly zero
initial value. Remaining smoothing stages gather each residual and write the
next correction together, retaining row arithmetic order and swapping reusable
buffers. This removes a graph traversal, separate update passes and redundant
zero fills. Fully active grids copy the final vector without an initial clear;
grids with walls retain the zero-filled scatter.

PCG dot products now use fixed 256-cell blocks and four double-double lanes,
then a fixed-order block reduction. Their ordering is independent of future
task scheduling. Operand range flags still request the retained reference
implementation; postponing the exception to the end of a reduction block
cannot publish a partial physical update. Large independent rows now use the
[deterministic interval workers](../docs/interval-parallel-pressure.md), retaining
each row's arithmetic and the fixed dot-product reduction order.

## Reproduction and scope

```sh
cmake --build build-production --target ma_mac_multigrid_tests ma_mac_interval_kernel_tests
build-production/ma_mac_multigrid_tests > multigrid.stdout
python3 native/tests/review_mac_multigrid.py multigrid.stdout
build-production/ma_mac_interval_kernel_tests > interval.stdout
python3 native/tests/review_mac_interval_kernel.py interval.stdout
build-production/ma_mac_interval_kernel_tests --benchmark 500 500 5
build-production/ma_mac_interval_kernel_tests --jacobi-benchmark 500 500 5
```

The component controls exercise odd periodic grids, duplicate periodic faces,
one-dimensional grids, disconnected rooms, isolated pores and coefficient
replacement. A separate saved-data reader reconstructs the hierarchy, transfer
transpose, positive coarse operators and full V-cycle at 100 decimal digits,
and checks the manufactured solution's true fine residual with exact fractions.
Owner controls compare against explicit Jacobi and the strict implementation,
then verify warm/cold checkpoint continuation, failed-budget rollback and
continuation after actual heat/mass edits.

The saved multigrid review passes 40,216 checks and rejects eight altered
captures; the owner review passes 105,931 checks and rejects five. Both suites
and reviews pass on GNU/Linux, fatal ASan/UBSan with leak detection, and static
MinGW Windows binaries under Wine. A multigrid checkpoint from either Linux or
Wine continues byte for byte on both (2,560 inspected numeric fields per
source). This is separate from actual Windows OS evidence and from the open
liquid continuation failure.

All 34 registered Linux tests pass. The 48 retained MAC output streams keep
their previous bytes and exit statuses, including the known strong-flow
collision-refinement failure. No original physical gate is weakened.

The GNU 15.2 Release/IPO 5900X run takes 6,626.66 ms median and 6,738.52 ms maximum
for five fully changing 250,000-cell intervals of 0.1 simulated seconds. It
uses 55 accepted substeps, no retries, and 800 pressure iterations across 100
Newton solves; the topology is reused throughout the measured intervals. The
previous interval implementation took 10,161.31 ms median. Peak standalone RSS
is about 461 MiB, including the owner, topology and numerical buffers.

These are serial donor-cell gas measurements, without game adapters or rendering.
They remain about 106 times over the 62.5 ms budget; neither the p95 colony gate
nor the default strong-flow gate is satisfied. Raw captures and profiles are in
the ignored `local/interval-multigrid/` directory.

## Fused-pass follow-up

After pass fusion, the complete component and owner captures remain byte for
byte identical on GNU/Linux, fatal ASan/UBSan and MinGW/Wine. The same saved-data
reviews therefore cover the changed implementation. Continuation from the
previous multigrid checkpoint remains byte-identical on both Linux and Wine,
in both source directions. The 34 registered Linux tests pass again.

A pinned three-interval comparison on the same CPU measures 7,154.98 ms median
before and 6,460.12 ms after, with identical 480 pressure iterations, 33 accepted
substeps and no retries. Other busy-host samples of the fused implementation
range from 6.36 to 8.67 seconds, so these timings do not certify a percentile or
close the performance gate. The first 100,000-cell multigrid measurement is
2,540.07 ms median per 0.1 s, also far above its 25 ms budget. Raw comparisons,
including the slower samples, are retained in `local/multigrid-fusion/`.
