# Geometric pressure preconditioner

The [production kernel](MAC_KERNEL.md) uses `mac_pressure_multigrid.*` for its
matrix-free PCG pressure solve. Saved `automatic` selects it on pressure grids
of at least 256 cells. Conduction first tries 32 Jacobi-PCG iterations at its
unchanged tolerance; if needed, the geometric solve then starts from zero.
Explicit `jacobi` keeps Jacobi. The former interval solver is
[retired](MAC_INTERVAL_KERNEL.md). Its hierarchy implementation and independent
saved-data reviewer are retained. No physical selector or timestep is changed.
The generic settings default remains the strict reference. The native ONI owner
explicitly selects production; its game validation remains open.

## Operator and transfers

The fine operator is `A = diag(h) + B diag(g) Bᵀ`. Positive compliances come
from the current work-pressure tangent; nonnegative conductances belong to
physical open faces. Periodic duplicate faces stay distinct. Walls and
disconnected pores remain disconnected.

Each geometric 2×2 bin aggregates only its connected components. Positive coarse
compliance is their sum. Boundary conductances include the ratio of fine to
coarse centre spacing, including odd widths. This is a rediscretized
preconditioner; it never replaces the fine physical equation. Cell-centred linear
interpolation has nonnegative weights and preserves constants; restriction is
its actual transpose. Missing or ambiguous neighbours use constant interpolation.
A node takes a coarse neighbour's correction only through one of its own edges
into that neighbour's aggregate, and the diagonal neighbour's only through a
path of two such edges (periodic axes wrap); otherwise it uses constant
interpolation in that direction, as for a missing neighbour.

That rule (2026-11-23) answers a measurement on the November 22 colony's
largest region, 12,374 cells of caves and shafts, a sixth of them liquid. The
Ritz values of the preconditioned operator, taken from the PCG's own
coefficients on sampled solves, ran from 0.08 to 12.2. A convergent V-cycle
cannot give eigenvalues above 2: the coarse correction overshot. The largest
modes lay on 5 to 100 cells at wall corners, where bilinear interpolation gave a
cell a quarter of a coarse node that the coarse graph links to the cell's
aggregate only around the corner; the rediscretized coarse operator does not see
the energy of that correction. With the rule the largest value falls to about 8
and a solve needs 23 instead of 28 iterations. The rest of the overshoot comes
from single liquid cells interpolated from gas aggregates across faces a
thousand times weaker. Galerkin coarse operators (PᵀAP) remove it (values 0.25
to 1.0, 7 iterations), but building their pattern costs about six hierarchy
builds, and hierarchies follow the active cells, so on the colony they cost more
than they saved. The smallest value belongs to a gas pocket reached only through
liquid; deflating even exact eigenvectors saves at most a fifth of the
iterations. The game kernel solves the largest such systems directly instead
([MAC_GAME_KERNEL.md](MAC_GAME_KERNEL.md#sparse-cholesky-pressure-solves-profile-12-2026-11-23)).

One application is a fixed symmetric V-cycle. Two diagonal-relaxation stages use
weights 1/2 and 3/2, reversed after coarse correction. Their error polynomial
`(1-t/2)(1-3t/2)` has magnitude less than one for `0 < t <= 2`, the scaled positive
graph operator's spectral interval. Symmetric smoothing and a positive coarse
inverse give a positive symmetric preconditioner. There is no adaptive inner
stopping rule. The last level uses Cholesky for at most 24 unknowns, or a diagonal
solve for isolated unknowns. Invalid preparation selects Jacobi before PCG;
a failed application rejects the candidate.

Production retains its inexact-Newton linear tolerance and 16-epsilon geometric
closure check. The independent component controls keep their original residual
bounds. Neither equation nor scientific gate is retuned during consolidation.

## Storage and determinism

Production retains immutable topology and numerical arrays in its calling-thread
workspace. Reuse requires identical dimensions, spacing, depth, periodicity and
pressure-active mask. A vacuum arrival rebuilds the mask before use; coarse cells
never connect through solids or empty cells. Empty-side zero-traction terms stay
in the fine diagonal. Every solve replaces all coefficients and starts corrections
from zero. No workspace supplies physical continuation history or checkpoint data.

The PCG around the V-cycle (`solve_linear_selected`) visits only the runs of
active cells in each fixed row block. Every skipped term is an exact zero (the
right-hand side is zero outside the system, which is checked; the V-cycle writes
zero there; the search direction and its product stay zero there), so each block
sums the same terms in the same order as loops over the whole grid.

V-cycles use the production pool through a synchronous block executor. The retained
component executor keeps its original arithmetic order. Row operations have unique
outputs; dot products use fixed 256-cell blocks, four double-double lanes and a
fixed reduction order. The first correction is computed directly from zero;
remaining smoothing passes gather residuals and write reusable correction arrays.
Reported production workspace bytes include topology and multigrid arrays.

Owner controls compare fresh-thread restores, concurrent heated/unheated branches,
changed constituent/passive layouts and caloric models, exact geometry changes and
continued execution after actual heat/mass edits. Numerical coefficients and source
storage cannot leak between these owners.

## Reproduction and evidence

```sh
cmake --build build-production --target ma_mac_multigrid_tests ma_mac_kernel_saved_tests
build-production/ma_mac_multigrid_tests > multigrid.stdout
python3 native/tests/review_mac_multigrid.py multigrid.stdout
build-production/ma_mac_kernel_saved_tests > production.stdout
python3 native/tests/review_mac_kernel.py production.stdout
```

The component reviewer reconstructs transfers, positive coarse operators and the
full V-cycle at 100 decimal digits, then checks the manufactured fine residual
with exact fractions. It checks 40,128 assertions and rejects eight altered
captures (40,216 before interpolation followed fine faces: some prolongation
rows now have fewer terms; the five manufactured cases take the same iteration
counts). The production owner reader keeps all seven original gas scenes and
their strict endpoint bounds, including the 10,000-step and catalogue controls;
it checks 105,985 assertions and rejects seven altered captures. It also checks
the production report's stage operands and below-range operand allowance.

Production controls cover gas, filled liquids, mixtures, gravity, conduction,
viscosity, vacuum masks and ordinary restore into twelve workers. These component
and owner controls do not close the strong-flow, free-surface or colony gates.
Platform verification and remaining work are recorded in [MAC_KERNEL.md](MAC_KERNEL.md#validation).

## Independent IC(0) components

The production midpoint also supports the saved `incomplete_cholesky` selector.
It finds connected components of the actual linear operator and runs each
component's retained `PressureSystem` solve on the production pool. Components
use fixed cell/face order and separate mutable storage. With C components,
local relative tolerance is rtol/2 and absolute tolerance atol/(2 sqrt(C));
their squared residual limits sum to at most half the original global limit
squared. The true residual is checked before publishing each correction.
The outer pressure equation and closure tolerance remain unchanged.

The colony owner selects this route; automatic multigrid and explicit Jacobi
keep their existing pressure choices. The focused disconnected-room control
checks agreement with geometric pressure, closed energy accounts, exact
one/many-worker bytes, cold continuation and atomic budget failure.

Earlier measurements of the removed serial donor-cell port took 6,626.66 ms median
and 6,738.52 ms maximum for five fully changing 250,000-cell intervals of 0.1 s,
with about 461 MiB standalone RSS. Those measurements are historical; current
production active-cell p95, solver-plus-adapter cost and rendering FPS remain open.
