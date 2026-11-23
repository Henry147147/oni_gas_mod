# Faster fluid regions: what the literature offers (2026-11-23)

A survey of published techniques for making grid fluid solvers faster, read
against this solver's code and the measurements of the
[speed study of 2026-11-23](speed-study-2026-11-23.md). It is research only:
nothing here was built or measured. Three labels separate the kinds of claim:

- **Shown**: a cited source demonstrates it (on its own problems, which are
  never exactly this one).
- **Code**: read in this worktree (file and function named).
- **Inferred**: my reasoning from the two; an estimate, not a measurement.

Sources are listed at the end with the URL that was opened. Sources that could
not be opened are listed separately and are only cited for what their
abstracts or citing papers say.

## Summary

**The budget first.** 1.13.1 as saved runs an owner tick of 137 ms: 90 ms of
regional run plus 47 ms of everything else (speed study). 3× needs 41.7 ms.
The 47 ms outside the regions alone exceed that, so 3× at full fluid detail
needs both halves cut: roughly 20 ms for the largest region and 20 ms for the
rest. Nothing in the literature turns the main region's 81 ms into 20 ms
without parallelism inside the region; what it does offer is a well-tested way
to make the pressure solve three to five times cheaper, a plausible factor on
the local stages from data layout, and an exact way to use several workers
inside one region. The owner's own 47 ms is bookkeeping specific to this mod
and is outside what the literature covers; it is a precondition, not a topic
of this note.

The ideas most worth the developer's time, in order of value for effort:

| # | Idea | Stage | Expected gain (main region, 81 ms) | Results | Effort, risk |
| --- | --- | --- | --- | --- | --- |
| 1 | Make the multigrid robust to the gas/liquid coefficient jump | `PressureMultigrid` (`smooth`, `cycle`, `coarsen`, `prepare`) | 26 → about 10-14 PCG iterations a solve; 10-15 ms | Same equation and tolerance; trajectory differs at tolerance level, as when IC(0) gave way to multigrid | Medium (line smoother, K-cycle) to high (operator-dependent interpolation); low risk |
| 2 | Make one V-cycle cheap: structured fine-level stencil, fused passes, fewer pool dispatches | `PressureMultigrid`, `solve_linear_selected` | 0.47 → about 0.15-0.25 ms an iteration; 7-12 ms | Can be bit-identical on the fine level | Medium, low risk |
| 3 | Measure, then deflate or recycle the slow modes of the preconditioned operator | `solve_linear_selected` | Removes most of the 20-iteration tail if it comes from a few modes | Same tolerance; trajectory at tolerance level | Low to medium; the measurement decides |
| 4 | Sparse direct Cholesky of each region's pressure system, as benchmark and fallback | `solve_components_cached` | A few ms a solve for 12k unknowns in 2-D (estimate) | Exact to rounding, deterministic single-threaded | Medium; licence care (below) |
| 5 | Region-local dense species-major layout for the local stages; band-limited sharpening setup | `PatchTransport::advance`, `sharpen`, `layer` | Unknown; plausibly 1.5-2× on the 31.5 ms of transport, sharpening and layering | Can be bit-identical | Medium to high; needs a profile first |
| 6 | Owner-computes subdomains inside a region (one fork per stage, halos) instead of fork-join per loop | `Tick::advance_ordered` and its stages | Lets 4-8 workers pay; the only exact route to ≤ 20 ms | Exact if the serial orderings are kept or redefined | High |
| 7 | Conservative velocity deadband so that still faces are exactly still and local stages skip them | `PressureSolver::advance` (drag terms) | Depends on how much of a region is quiescent: measure first | Small, booked physics change (momentum to wall load, kinetic energy to heat) | Low effort; user's "feel real" test |

1. **Robust multigrid (pressure, 30 ms).** The production preconditioner is,
   in the literature's terms, a cell-centred multigrid whose coarse operators
   are arithmetic averages of the fine coefficients (Khalil and Wesseling;
   Molenaar 1996), with bilinear transfers, a two-stage point Jacobi smoother
   and a V-cycle (**Code**). Molenaar shows that exactly this coarse operator
   fails with point Gauss-Seidel on hard interface problems and needs a line
   (ILLU) smoother, and W-cycles for grid-independent rates (**Shown**).
   MacLachlan, Tang and Vuik show for bubbly-flow pressure (density ratio
   10⁻³) that a geometric MG-CG needs 132 iterations where black-box
   multigrid (operator-dependent interpolation, Galerkin coarse operators)
   needs 12, at any contrast from 10⁻¹ to 10⁻⁵, and is also the fastest in
   time (**Shown**, 3-D). Here liquid faces conduct about 10⁻³ as much as air
   faces (**Inferred** from `PressureSolver::advance`'s coefficients), the
   same regime. The cheap test is a symmetric alternating line smoother and a
   K-cycle (Notay) on the existing hierarchy; the thorough fix is BoxMG-style
   interpolation. Expected 26 → 10-14 iterations (**Inferred**).
2. **Cheap V-cycles.** The hierarchy is stored as a graph (16-byte neighbour
   entries, indirect coefficient reads), the PCG and V-cycle dispatch about
   ten pool jobs an iteration on 12k cells, and coarse levels below 8,192
   cells already run serially (**Code**). Loops shorter than a few thousand
   iterations lose to barrier overhead (Hager, **Shown** in general); miniGMG
   gets its multicore speed from fusing passes and avoiding synchronisation
   (**Shown**). A 5-point stencil in dense arrays for the finest level, fused
   smoothing, residual and restriction, and one dispatch per V-cycle half
   should bring an iteration to a third or less (**Inferred**). Keeping each
   row's summation order (compliance term, then left, right, bottom, top, as
   the edge list orders them now) and skipping missing neighbours rather than
   multiplying them by zero can keep every bit.
3. **Deflation or recycling.** "Fast for five iterations, slow for twenty"
   is the signature of a few small eigenvalues the preconditioner misses, or
   of an effective condition number near 28 (a CG rate of 0.68). Which one
   decides the cure. The Ritz values come free from the PCG's own α and β
   (Meurant and Tichý). If a few outliers: deflate them with indicator vectors
   of liquid bodies and gas pockets (Tang and Vuik's bubbly-flow deflation)
   or recycle the previous interval's Ritz vectors (de Sturler and Parks;
   Gosselet et al.). The study found the previous *solution* useless as a
   start, which is expected: the right-hand sides are unrelated, but the
   *operator* is nearly the same from one interval to the next.
4. **Direct solve.** 2-D is where sparse direct methods are strong: nested
   dissection on an n×n grid costs O(N^1.5) flops and O(N log N) fill
   (Bindel's notes, **Shown**). For 12,374 cells that is single-digit
   megaflops a factorisation and a few milliseconds (**Inferred**, constant
   uncertain), exact, and independent of the coefficient jump. CHOLMOD's
   supernodal module is GPL (LGPL for the simplicial one); an own
   nested-dissection Cholesky on the region's grid is feasible. Its main use
   is as the yardstick ideas 1-3 must beat.
5. **Layout of the local stages.** The remap and the sharpening copy each
   cell's sparse inventory into a `std::vector<std::pair<uint32_t,double>>`,
   and the remap merges up to five such lists per updated cell (**Code**). The production
   kernel of the earlier phases used species-major columns of the present
   constituents (`MAC_KERNEL.md`, Design). For a region holding a few dozen
   species, dense species-major arrays over the region's box make the
   donor-cell remap a vectorisable stencil, and adding exact zeros in the
   same order reproduces the sparse sums bit for bit (**Inferred**). The
   sharpening pass builds phase geometry for every non-wall cell; building it
   only for liquid cells and their stencil neighbours is exact (**Code**,
   **Inferred**). Size of the gain: needs a profile of where `sharpen` and
   `PatchTransport::advance` spend their time.
6. **Parallelism inside a region.** The region does not scale past four
   workers because each stage is many short fork-join loops with serial
   parts between (study; **Code**). The standard remedy is owner-computes
   subdomains: each worker owns a fixed piece of the region for a whole stage,
   synchronises at stage boundaries and exchanges halos, while the pressure
   stays one solve. That gives the same discrete answer as today. The study's
   alternative (pieces stepped independently, gas exchanged across cut faces
   afterwards) is a lagged interface coupling; the analysis of such schemes
   (Connors, Howell and Layton) shows that lagging is weakest exactly where
   coupling is strongest, and a cut through open space is the strongest
   coupling there is. If cuts are pursued, the coupled interface solve the
   study mentions is the version the literature supports (Schur complement,
   as in Liu et al. 2016).
7. **Quiescence inside a connected region.** Rigid-body engines never put
   part of a coupled island to sleep (Box2D: "shapes begin to overlap and
   joints begin to dislodge"); the fluid analogue is that part of a pressure
   system cannot simply be skipped. What can be made cheap is the local work
   of still cells. Today a backward-Euler solve with drag leaves every face
   with some tiny non-zero swept volume, so every cell is "affected" in the
   remap (**Inferred**). A stick threshold booked exactly like the existing
   thin-gas drag (momentum to the wall load, kinetic energy to local heat)
   would make still faces exactly still; `evaluate_changed` already skips
   bitwise-unchanged cells (**Code**). Whether it pays depends on the share of
   faces moving below, say, 10⁻⁴ m/s: one replay can count it.

**Combined, as an estimate.** Ideas 1 and 2 take the main region's pressure
from 30 ms to about 8-12 ms; idea 5 might take 10-15 ms off the 31.5 ms of
local stages; trimming diagnostics (entropy, readings) a few more. That is
roughly 81 → 40-50 ms with results that are exact or differ only at the
pressure tolerance, using no more parallelism than today, and → 15-20 ms
only with idea 6 (or the study's approximate cuts).
These are inferences with wide error bars; experiment list at the end.

## 1. Pressure solves with strongly jumping coefficients

### What the solver does (Code)

`PressureSolver::advance` (`pressure_be.hpp`) assembles one linear
backward-Euler system per flow interval: `diag = compliance` (gas
`V_gas/((1+nR/C_v) p)`, plus the liquid compliance), off-diagonal face weights
`dt² A² / (I (1+γ))`, with I the face inertia (half the two cells' mass) and γ
the implicit drags; faces to inactive cells (vacuum, fixed pressure, a partly
filled gas-free surface) join the diagonal as Dirichlet couplings. A single
connected component of at least 2,000 cells goes to `solve_geometric_checked`
(MG-PCG to the absolute bound `0.5 * max(1e-14, rtol ||rhs||)`, then a true
residual check in long double); smaller or split systems use IC(0) per
component.

From these formulas (**Inferred**, dt = 0.1 s, 1 m cells): an air face has
`I ≈ 1.2 kg`, weight ≈ 8·10⁻³; a water face `I ≈ 10³ kg` (liquid drag adds a
tenth), weight ≈ 10⁻⁵; a thin-gas face below 1 g gets `I(1+γ) ≈ (dt/τ)·1 g =
0.01 kg`, weight ≈ 1. Air's compliance is about 7·10⁻⁶ m³/Pa, so
compliance/weight ≈ 10⁻³ = 1/(c dt/h)²: the diagonal term does not regularise
gas; the operator is a variable-coefficient Poisson operator with a contrast of
about 10⁻³ at every gas/liquid boundary and up to 10⁻⁵ between thin gas and
liquid. Liquid bodies are anchored only through free-surface Dirichlet cells,
weak faces to gas, and a tiny compliance.

The preconditioner (`mac_pressure_multigrid.cpp`): `coarsen` aggregates each
2×2 bin's connected cells; `prepare` sums compliances into the coarse cell and
gives each coarse face the sum of its fine faces' weights times
`old_span/new_span` (½ inside); prolongation is cell-centred bilinear,
restriction its transpose; `smooth` is two Jacobi stages with weights ½ and
3/2, reversed on the way up; one pre- and one post-smoothing; dense Cholesky at
≤ 24 unknowns. Levels with fewer than 32 blocks of 256 cells run serially
(`prepare_multigrid`).

### What the literature says

**Coefficient-averaged cell-centred multigrid.** Molenaar (1996) derives the
coarse operator from piecewise-constant prolongation, which in the variable
case makes "the arithmetic average of the corresponding finest grid
coefficients" the coarse coefficient, with the second-order term "multiplied
by a factor 1/2". That is this solver's `edge_scale`. His results (**Shown**,
2-D): on the inhomogeneous staircase problem "the algorithm fails to converge
with the Gauss-Seidel smoother", so he switches to incomplete line LU (ILLU)
smoothing and gets factors 0.15-0.34; on a four-quadrant interface problem
the V-cycle rate "seems to be h-dependent", while with the W-cycle "the
algorithm converges fast and in a grid independent way". He builds on Khalil
and Wesseling (1992), whose cell-centred method is described in abstracts as
cheaper to set up than vertex-centred multigrid, which needs matrix-dependent
transfers (not opened).

**Operator-dependent interpolation and Galerkin coarse operators.** Alcouffe,
Brandt, Dendy and Painter (1981) and Dendy's black-box multigrid (1982)
introduced interpolation weights computed from the matrix rows, with Galerkin
coarse operators RAP (primary sources not opened; described in Hemker et al.
1983, who list the same matrix-dependent prolongations "developed for cases
where a_ij is discontinuous", and in MacLachlan et al.). MacLachlan, Tang and
Vuik (2008) compare solvers on bubbly-flow pressure systems (**Shown**, 3-D,
64³, contrast ε between phases):

| Method | ε = 10⁻¹ | ε = 10⁻³ | ε = 10⁻⁵ |
| --- | --- | --- | --- |
| IC-CG | 131 it., 3.1 s | 244 it., 5.8 s | 289 it., 6.9 s |
| Deflated IC-CG | 35, 1.1 s | 54, 1.7 s | 59, 1.9 s |
| Geometric MG-CG | 33, 1.0 s | 132, 3.9 s | 267, 8.0 s |
| Galerkin coarse, fixed interpolation (CCMG-CG) | 10 | 18 | 37 |
| BoxMG-CG | 12, 0.8 s | 12, 0.8 s | 12, 0.8 s |
| AMG-CG | 9, 8.0 s | 14, 8.9 s | 16, 9.2 s |

Their geometric MG-CG preconditions with the *constant-coefficient* operator,
which is weaker than this solver's rediscretised variable-coefficient
hierarchy, so the 132 is a worst case for this solver; the direction is the
point. Their explanation: with jumping coefficients "the errors left after
relaxation are not smooth", and operator-induced interpolation "approximately
preserves the continuity of the normal flux" across the interface. Galerkin
coarse operators with *fixed* interpolation help, but their performance
"still degrades as the density contrast increases". AMG matches BoxMG's iterations but cost "a
factor of 10 to 15 times" more time, mostly setup and unstructured data
structures.
BoxMG is not universal: Dendy and Moulton (2012 abstract) report a modified
checkerboard problem where red-black point relaxation converges poorly.
hypre's structured solvers make the same trade explicitly: PFMG with "simple
pointwise smoothing" is "not as robust as SMG", which uses line and plane
smoothing, "but is much more efficient per V-cycle".

**Algebraic multigrid and its setup.** Smoothed aggregation (Vaněk, Mandel,
Brezina 1996) smooths a piecewise-constant tentative prolongation and forms
coarse operators variationally; energy-minimising interpolation (Olson,
Schroder, Tuminaro 2011) computes interpolation by Krylov minimisation with
flexible sparsity (both: abstracts only). Notay's AGMG uses unsmoothed
pairwise aggregation, which keeps setup cheap, and notes that a
piecewise-constant prolongation "just substitut[ing] the classical
prolongation in a standard multigrid scheme" is "not efficient"; it needs
Krylov acceleration at every level (the K-cycle). His table (**Shown**,
stand-alone iterations to 10⁻¹², two-grid factor 0.5): V-cycle 188 (7 levels)
and > 999 (14 levels), W 37 and 50, K 20 and 20. Setup reuse when only values
change: AMGCL keeps P and R and recomputes the coarse matrices with
`rebuild()`; Demidov (2021) reports setup reduced by "40% to 200%" (his
wording) and total time by up to 20% on a two-fluid dam break.

**MG-preconditioned CG for fluids.** McAdams, Sifakis and Teran (2010)
(**Shown**, constant coefficients, irregular voxelised domains, 64³-512³):
plain V-cycles need 30-40 boundary smoothing sweeps to be stable and can
stagnate on irregular domains because "coarse modes may not be accurately
represented", but the same cheap V-cycle as a CG preconditioner reduces the
residual "by one order of magnitude every 2 iterations": 9-13 iterations to
10⁻⁴, 15-21 to 10⁻⁸, against 36-278 for IC-PCG. This is the design this
solver follows; its 26 iterations to a tolerance of 10⁻⁶ are about twice what
McAdams gets without coefficient jumps.

**Deflation and recycling.** Tang and Vuik (2007) on bubbly flows (**Shown**):
small eigenvalues from density ratios and "many bubbles" are "not cured by
classical preconditioning"; deflating them (DICCG, subdomain indicator
vectors) makes convergence nearly insensitive to the contrast, with an optimum
number of vectors for time. Tang, MacLachlan, Nabben and Vuik show deflation,
domain-decomposition and two-grid preconditioners are one family and that
with comparable work per iteration they converge alike. Kahl and Rittich: the
effective condition number grows about linearly with the error in the
deflation space, and the coarse projection must be computed accurately. For
sequences of systems, recycling approximate invariant subspaces from earlier
solves is established (de Sturler and Parks; Gosselet, Rey and Pebrel), and
the subspace of the smallest eigenvalues is often not the best one to
recycle, particularly for nonsymmetric problems (de Sturler and Parks,
abstract). Meurant and Tichý give a cheap
running estimate of the extreme Ritz values from the CG coefficients.

**Incomplete factorisations.** IC(0) is what the solver used before multigrid:
134 iterations against 15 on the industrial base (`MAC_GAME_KERNEL.md`), 244
against 12 in MacLachlan's table; parallel variants (level scheduling,
multicolour orderings) lose further. Not worth revisiting except as a smoother
(ILLU in Molenaar).

**Cut cells and variational discretisations** (Batty, Bertails and Bridson
2007, not opened) improve accuracy for sub-cell solid boundaries. Tiles here
are whole walls or whole fluid: not relevant.

**Direct factorisation.** Bindel's lecture notes derive the nested-dissection
cost on an n×n grid: about (5/3)n³ flops to factor the separators, O(N^1.5)
in total, O(N log N) fill, "why sparse direct methods are attractive for
solving 2D PDEs". CHOLMOD's licence file: Check, Cholesky, Utility and
Partition are LGPL; Supernodal, MatrixOps and Modify are GPL.

### How it maps onto this solver

- The stalled tail fits both failure modes the literature names: averaged
  coarse coefficients with a weak point smoother (Molenaar), and geometric
  interpolation across a 10⁻³ jump (MacLachlan). The red-black Gauss-Seidel
  result (32 % more iterations, speed study) fits Molenaar: a better point
  smoother does not fix a coarse grid that misrepresents the interface.
- **Cheapest robust step (Inferred):** keep `coarsen` and `prepare`, replace
  `smooth` by symmetric alternating zebra-line relaxation (tridiagonal solves
  along row and column segments between walls; segments are independent, so
  the parallel result is deterministic), and replace the recursion in `cycle`
  on the serial coarse levels by a K-cycle (two flexible-CG steps a coarse
  level). Both change only the preconditioner; the checked residual bound in
  `solve_geometric_checked` guarantees the same tolerance. A K-cycle's extra
  work lies on the serial coarse levels, a quarter of the fine level's size
  each.
- **Thorough step (Inferred):** BoxMG-style interpolation and Galerkin RAP on
  the structured 2-D grid (9-point coarse stencils), with walls as decoupled
  identity rows. Setup is a few fine-level passes a solve; with two solves a
  tick on nearly the same operator, keeping P between the two intervals
  (AMGCL's rebuild) halves it. Determinism holds if RAP is computed row by
  row in fixed order.
- **AMG libraries** (hypre BoomerAMG, AMGCL, AGMG): good for an offline
  experiment on dumped systems, to learn what a near-ideal hierarchy buys.
  Setup at every solve on 12k unknowns would eat most of the gain, and the
  libraries' parallel reductions are not fixed-order, so a product version
  would need to run them single-threaded or own the code.
- **Deflation:** the deflation vectors suggest themselves: indicator vectors
  of each liquid body (connected cells at least half liquid; the main region
  has 896 such cells) and of each gas pocket bounded by liquid. The coarse
  matrix `E = Zᵀ A Z` costs one operator pass a solve, each iteration one
  extra pass and a tiny dense solve; the reductions run in the existing fixed
  blocks.

### Verdict

Relevant now. This is the largest well-documented lever in the region's
budget, it changes no equation, tolerance or saved state, and it is local to
two source files. Start with the measurement in the experiment list; it
decides between deflation (few outliers) and a better hierarchy (a spread).

## 2. Domain decomposition for one connected region

### What the literature says

- **Exact and approximate methods.** Schur-complement substructuring,
  FETI-DP and BDDC, and overlapping Schwarz used as preconditioners inside a
  Krylov method all converge to the monolithic discrete solution; they are
  exact to the solver tolerance. Schwarz needs a coarse space to scale with
  the number of subdomains (Dolean, Jolivet, Nataf and Tournier, who single
  out robust coarse spaces such as GenEO). Optimised Schwarz
  (Gander 2006) improves the transmission conditions and gains most when the
  overlap is about one cell. Approximation enters only when the subdomains
  are stepped with lagged or one-shot interface data.
- **Lagged interface coupling.** Connors, Howell and Layton analyse two heat
  equations coupled across an interface (**Shown**): the IMEX scheme that
  lags the interface term is stable only under a condition, with the time-step
  restriction growing like κ⁻² for strong coupling κ; a data-passing
  partitioned scheme is stable for large κ. Dawson, Du and Dupont (1991,
  abstract only) compute interface values explicitly with a wider stencil and
  the interiors implicitly; later papers report it conditionally stable.
- **Fluids practice.** Liu, Mitchell, Aanjaneya and Sifakis (2016) solve the
  pressure Poisson equation by a Schur-complement decomposition into
  subdomains solved concurrently, assembled into a global solver while
  avoiding heavy communication and dense linear algebra, and used as a CG
  preconditioner, at up to about 10⁹ unknowns (project page). In distributed CFD the explicit transport is partitioned with
  halo (ghost-cell) exchanges, which reproduces the monolithic explicit update
  exactly, and the pressure stays one global solve with a parallel solver.
- **Cost of an interface solve.** With a few hundred interface unknowns
  (165-277 cut faces in the study's main-region cuts), each iteration of an
  iterative Schur solve costs one Dirichlet solve per piece, in parallel;
  forming the Schur complement explicitly needs one piece solve per interface
  unknown and does not pay unless the pieces are factored directly
  (**Inferred**).

### How it maps onto this solver

- The working copy of `regional_tick.hpp` holds an uncommitted study
  prototype (`MA_X_CUT_CELLS`, `cut_pieces`, `exchange_across_cuts`):
  straight cuts, pieces stepped as regions with cut faces as walls, then
  either pairwise equalisation across each cut face or a "lumped" variant
  that solves a least-squares problem on the piece graph. The lumped variant
  is a one-unknown-per-piece coarse correction (Nicolaides' coarse space in
  domain-decomposition terms, not opened); it levels the pieces' mean
  pressures but not the gradients near the cuts. Gas crosses with its
  internal energy; liquid, germs, momentum and conducted heat do not (comment
  in the code). Both variants are
  lagged couplings: per Connors et al., the error is largest where coupling
  is strong, i.e. at wide openings, which is where the study's cuts lie.
- The exact alternative keeps one pressure solve per region (made cheap by
  section 1, and parallel through its own fine-level passes or a
  Schur/Schwarz preconditioner) and partitions the local stages by
  owner-computes with halos. Most stages are already Jacobi-like (layering
  exchanges, sharpening clusters, per-cell EOS). Three parts are serial by
  construction and would need parallel definitions that the controls then
  pin: the lightest-first heaps of `limit_thin_work` and `share_thin_heat`,
  the per-microtick sort of affected cells in `PatchTransport::advance`, and
  the ordered cluster walk in `sharpen`. The transport's dyadic microticks
  need a halo exchange per microtick, which is cheap while the levels stay
  low.
- Liquid across cuts, which the study flags as an obstacle, disappears in the
  exact version: nothing is cut physically.

### Verdict

Exact subdomain execution: relevant after a restructuring of the region's
tick; it is what makes four to eight workers pay. Approximate cuts with
lagged exchange: relevant only with the coupled interface solve; the plain
exchange is the variant the literature warns about.

## 3. Time integration for stiff low-Mach flow

### What the literature says

- **Semi-implicit pressure.** Kwatra's thesis (and Kwatra, Su, Grétarsson,
  Fedkiw 2009, not opened) split the compressible flux into explicit
  advection and implicit acoustics; the implicit part is a Poisson-like
  equation with an identity term that becomes the incompressible Poisson
  equation as the sound speed grows, and the time step is set by the fluid
  velocity rather than the sound speed (thesis abstract). This solver is in that class: a backward-Euler Helmholtz
  pressure step and an explicit conservative remap limited by the 0.4-cell
  swept volume.
- **Local time stepping and multirate.** Osher and Sanders (1983, abstract
  only) prove convergence of conservative explicit schemes with fine and
  coarse time steps in different regions under a local CFL condition; Tan,
  Zhang, Huang and Tang (2004) build a conservative version by matching the
  fluxes into and out of the fine region; Constantinescu and Sandu give
  second-order multirate Runge-Kutta schemes that keep conservation and
  stability under local CFL conditions (report abstract).

### How it maps onto this solver

- The transport already steps locally: `PatchTransport::advance` gives each
  cell a dyadic clock level from its outgoing volume and runs fast faces more
  often (**Code**), an Osher-Sanders-type scheme. The pressure interval is not
  local: one cell over the 0.4-cell net-volume bound makes `Tick` re-solve the
  whole region at a shorter interval, and every later interval of the tick
  covers the whole region too (**Code**, `advance_ordered`).
- That matters little for the main region (2.0 intervals a tick, no excess)
  but explains the surface region at default settings: 60 ms at 7.6
  intervals a tick because of a few dozen cells of water film. The fast
  settings remove it by deleting the plume (trace threshold), an accuracy
  setting. A local pressure refinement would keep the plume: solve the region
  at the full interval, then re-solve only a patch around the flagged cells
  for the substeps, with the coarse interval's face fluxes imposed on the
  patch boundary and the fine fluxes summed back to them (refluxing). Mass,
  U+K+G and momentum stay exact because the boundary fluxes are shared
  (**Inferred**; I found no paper that does this for an implicit compressible
  pressure step with frozen compliance).
- Higher-order or less dissipative integrators (Crank-Nicolson, IMEX
  Runge-Kutta) would trade away the damping the solver relies on; not a speed
  lever.

### Verdict

Local pressure refinement: relevant after restructuring, for regions whose
cost is excess intervals (the surface region at default settings). Not a
lever for the main region. Step-size control by swept volume is already done
(profile 7, sized intervals).

## 4. Sparse and adaptive representations, sleeping, equilibrium

### What the literature and practice say

- **Sparse volumes.** VDB (Museth 2013) and SPGrid (Setaluri et al. 2014)
  store sparse 3-D grids with O(1) access; SPGrid streams paged blocks "with
  sequential and stencil access bandwidth comparable to dense uniform grids"
  and supports an adaptive MGPCG. For a 2-D world of 250,000 cells memory is
  no issue, and regions are already compact boxes: not relevant as data
  structures. The lesson that carries over is SPGrid's: dense blocks, not
  per-cell lists, are what make stencils fast (idea 5).
- **Islands and sleep.** Box2D (Catto 2023) puts whole islands to sleep
  when every body's sleep timer passes a threshold; partial sleep is refused
  because the solver couples the island ("shapes begin to overlap and joints
  begin to dislodge"); islands persist across steps, merge immediately by
  serial union-find, which keeps results deterministic, and split lazily,
  about ten times faster than rebuilding them each step. This
  solver's regions and activity tiers already follow this pattern.
- **Cellular-automaton games.** Noita (80.lv on Purho's GDC 2019 talk):
  64×64 chunks, each with a dirty update region; threads update chunks in
  four checkerboard passes, each pixel moving at most 32 pixels into a
  neighbour, so no two threads touch the same pixel. Minecraft water is
  "stationary" until a block update makes it flow and spreads 1 block every
  5 ticks. Dwarf Fortress moves water by gravity, level averaging and an
  explicit pressure search for the nearest free tile. No primary description
  of the base game's own fluid code was found; the wiki pages opened describe
  only its visible behaviour.

### How it maps onto this solver

- Regions with activity tiers are the island-and-sleep pattern, done. Inside
  an awake connected region there is no published way to skip parts of an
  implicit pressure solve without changing the answer, consistent with Box2D's
  refusal of partial island sleep.
- What can be skipped is local work on cells whose state does not change.
  The code already exploits bitwise-unchanged cells (`evaluate_changed`,
  `unchanged_cell`); the dirty-chunk analogue exists at the owner level
  (`sparse_cached`, chunk serials). The missing piece is cells that change by
  rounding-level amounts every interval: a deadband (idea 7) turns them into
  bitwise-unchanged cells.
- Noita's checkerboard passes are a recipe for exact, deterministic
  subdomain execution of local stages (idea 6): with a fixed phase order the
  result does not depend on which thread runs a tile.

### Verdict

Sparse grid libraries: not relevant. Activity inside a connected region:
relevant now, as a measurement (share of still faces) and then a deadband.

## 5. Multi-species transport and interfaces at low cost

### What the literature says

- **Many tracers.** Moving every constituent with one shared mass flux, as
  the remap does ("each donor writes its outgoing face parcels once"), is
  standard practice; the cost is then the data layout of the tracers.
- **VOF interfaces.** So, Hu and Adams (2012) sharpen by solving an
  anti-diffusion equation on the volume fraction after each step and update
  phase mass, momentum and energy conservatively from the sharpened field,
  without reconstruction or normals (abstract); this is the family the solver's `sharpen` belongs to. Geometric
  donor-acceptor fluxes (Hirt and Nichols 1981, not opened; Fluent's manual)
  pick a horizontal or vertical interface orientation from the
  volume-fraction gradient and limit the face flux by the donor's filled and
  the acceptor's free volume, so the advection itself stays sharp. Algebraic
  schemes put the sharpening into the flux: THINC (Xiao, Honma, Kono 2005,
  not opened) uses a tanh profile; OpenFOAM adds a counter-gradient
  compression flux with MULES limiting to keep α in [0, 1] (CFD Direct).
  Comparative studies (Aniszewski, Ménard, Marek 2014) mostly report how much
  the methods differ in complexity.

### How it maps onto this solver

- The layered vertical remap (profile 10, `layer_donor`) is already a
  donor-acceptor scheme on vertical faces: the top face draws gas first, the
  bottom face the heaviest liquid. Extending it to horizontal faces with the
  dominant-axis orientation the sharpening already computes would make the
  remap itself keep surfaces sharp and leave the separate pass less to do
  (**Inferred**). That changes how liquid surfaces move, so it needs the
  surface and pool controls and the user's "feel real" test.
- Cheaper without changing results: `sharpen` copies every cell's inventory
  and calls `geometry_into` for every non-wall cell before any face is
  examined (**Code**); faces are processed only between two liquid cells
  above `detail_fraction`. Building geometry only for liquid cells and their
  3×3 neighbours (the height-normal stencil) is exact if the skipped cells'
  reads return what they return now. The main region has 6,073 cells with
  some liquid but 896 with at least half a cell: with the default detail
  fraction of zero, liquid traces keep the band wide, which is consistent
  with the trace threshold's 81 → 69 ms.
- Species layout: see section 6.

### Verdict

Band-limited sharpening setup: relevant now, exact. Donor-acceptor
horizontal faces or a THINC-type flux in place of the separate pass: relevant
after a restructuring, with visible-behaviour risk.

## 6. Data layout and parallelism

### What the literature and practice say

- **Short loops and synchronisation.** Hager (blog post, measured on one
  older six-core socket): barrier overhead dominates loops below "a couple of
  1000s" iterations, depends on where the threads sit in the machine, and
  differed about 20× between two compilers' OpenMP runtimes.
  miniGMG (Williams et al. 2012) gets its multicore speed in geometric
  multigrid from SIMD, fusing the residual with restriction, communication-
  avoiding (wavefront) smoothing and dynamic threading decisions per level;
  the coarse levels' overheads limit the overall speed-up.
- **Mixed precision.** McCormick, Benzaken and Tamstorf (2020) analyse
  multigrid with three precisions per level ("progressive precision"); lower
  precision in the preconditioner costs accuracy only through κ^½ terms.
- **Persistent structures.** Box2D's persistent islands and data-oriented
  layout, SPGrid's paged blocks and VDB's tiles all keep state in place
  between steps and update incrementally.

### How it maps onto this solver (Code, then Inferred)

- **Region pressure.** `solve_linear_selected` runs about ten pool jobs per
  PCG iteration (operator, fused update with a dot product, the dot after
  preconditioning; in the V-cycle the copy-in, the first correction and
  smoothing pass, the residual, the prolongation, two post-smoothing passes,
  the copy-out), each over 12k cells, while levels below 8,192 cells run on
  the caller. The fine level uses adjacency lists of `{node, edge}`
  (`std::size_t` pairs) and gathers coefficients by edge index. A 5-point
  stencil in five coefficient arrays over the region box, with a mask for
  walls, needs about a third of the bytes per cell and vectorises. Fusing the
  two post-smoothing stages, residual and restriction, and running the whole
  V-cycle as one job per worker over fixed row blocks with barriers inside,
  replaces ten dispatches by two or three. At 12k unknowns the whole solve
  may be fastest on one or two threads; the regional scheduler can then give
  the region's other workers to other stages.
- **Local stages.** The remap and the sharpening copy the sparse arrays into
  per-cell `std::vector` lists and pack them back (`PatchTransport::advance`'s
  `cells_storage`, `sharpen`'s `scratch.cells`); the region `extract` and the
  regional rebuild copy each stepped region's sparse state every tick.
  Capacities are reused, so allocation is not the cost (the allocator tuning found
  nothing), but the 5-way list merge per updated cell, with branches per
  entry, is. A region-local species-major array over the box for the
  species present in the region (a few dozen at most) lets the remap compute
  `new[s][i] = Σ_a fraction_a · m[s][donor_a]` as a stencil; skipped absent
  entries become added exact zeros, which leave non-negative sums bit for bit
  unchanged if the order of the five contributions is kept. The production
  kernel used such columns before the game kernel (`MAC_KERNEL.md`, Design),
  so the conversion code and its controls are a model.
- **Determinism.** Fixed blocks and ordered reductions already give
  thread-count-independent results; reproducible-summation libraries are not
  needed. Any new parallel stage must keep a fixed block partition that
  depends on geometry only.
- **Mixed precision.** A single-precision fine-level smoother inside the
  double-precision PCG would halve its memory traffic; the coarsest Cholesky
  must stay double (coefficients span 10⁵). Worth it only after the stencil
  rewrite.

### Verdict

Relevant now: stencil storage and fused passes in the multigrid (exact),
layout of the local stages (exact, larger change). Mixed precision: after the
rewrite, small.

## 7. Other large factors, and negative results

- **Direct solves in 2-D** (section 1): the one technique that removes the
  coefficient problem entirely. Worth an offline timing on dumped systems:
  if a nested-dissection Cholesky of the main region takes 3-5 ms, it is
  competitive with the best iterative plan and simpler to trust.
- **Skipping diagnostics.** The game kernel reports entropy as a
  diagnostic, without admission retries (`MAC_GAME_KERNEL.md`), yet `Tick`
  computes `total_entropy` over the region twice per tick (**Code**). If no
  gate depends on it (check whether its EOS errors act as a domain check),
  computing it on sampled ticks only is free accuracy-wise. `TickResult`
  already measures `entropy_ms`.
- **The bounding box.** Each region's `Tick` runs on its bounding rectangle
  with non-members as walls (`RegionalTick`, `Region::box`); every N-sized
  pass (EOS readings, pruning scans, audits, PCG vectors, packing) covers the
  box. A cave network of 12,374 cells may have a box twice that size. Knowing
  the ratio tells whether compacting the cell index (or iterating member rows
  only) pays.
- **GPU:** closed by the user ([why](gpu-acceleration-research.md)); the
  sizes here (a few thousand to tens of thousands of unknowns per solve, two
  solves per tick) are also too small to hide transfer and launch latency.
- **Tall cells, adaptive refinement, particle methods:** change the
  discretisation the game's tiles fix; not relevant.

## Already tried or ruled out, read against the literature

| Tried (study or earlier notes) | Result | What the literature says |
| --- | --- | --- |
| Start each pressure solve from the previous solution | No gain | Expected: the increments' right-hand sides are unrelated. What carries over is the operator; recycle its slow eigenvectors instead (de Sturler and Parks; Gosselet et al.). |
| Looser pressure tolerance | 14-26 % fewer iterations, noisy liquid pressures | A pressure error is a residual divided by a compliance; in stiff liquid a small residual is a large pressure error. With the tail removed (section 1), tolerance stops being the lever. |
| More workers inside a region | No gain past four | Barrier overhead dominates loops below a few thousand iterations (Hager); the remedy is fewer, longer parallel sections (miniGMG), i.e. idea 6. |
| Shared executor for regions of 4,096+ cells (earlier note, `MAC_GAME_KERNEL.md`) | 34 % slower | Same cause. |
| Red-black Gauss-Seidel smoother | 32 % more iterations | Point smoothers do not cure an averaged coarse operator at an interface (Molenaar: Gauss-Seidel fails, ILLU and W-cycles needed); MacLachlan: better relaxation with fixed interpolation still degrades with contrast. |
| IC(0) → geometric MG-PCG (2026-11-11) | 134 → 15 iterations on the industrial base | Consistent with McAdams (IC-PCG 36-278 vs MGPCG 9-13 iterations). |
| Exempting near-vacuum from the swept-volume bound | Mixed | No literature; the trace threshold or local pressure refinement (section 3) address the same cost. |
| Allocator tuning | No gain | Capacities are already reused; the cost is the per-entry list merging, not allocation. |
| Larger steps in true game time beyond 2 ticks | No gain | Semi-implicit schemes are limited by material CFL (Kwatra); work scales with fluid time, only per-update overhead is saved. |
| Regions on alternate ticks | Loses the gain | The run is bounded by the largest region; balancing needs smaller units of work (idea 6). |
| Stepping busy regions once a second | Source cells reach 9,570 K | Operator splitting of point sources: multirate schemes keep fast components (here, source spreading) on the fine clock. |
| GPU | Closed | See section 7. |
| Cutting regions at one-tile openings | 12,041 of 12,374 cells stay together | Partitioners cut for balanced size with small separators, whatever the geometry; the study's recursive straight cuts are a simple version of the nested-dissection separators that also order a direct solver's elimination (section 1). |

## Small experiments that would settle the open questions

Each runs on dumped systems or the existing replay, game-free:

1. **Ritz values of the preconditioned main-region operator.** Store α and
   β of 20 PCG solves of the main region, build the Lanczos tridiagonal
   matrix, list its eigenvalues. A handful of outliers far below the rest:
   deflation or recycling (idea 3) will remove the tail. A spread down to
   about 1/28 of the largest: the hierarchy must improve (idea 1).
2. **Smoother and cycle swap.** On the same dumped systems: alternating
   zebra-line relaxation in place of `smooth`, and a K-cycle on the serial
   levels. Iterations and time per solve against the present 26 and 0.47 ms.
3. **Upper bound of a better hierarchy.** Solve the dumped systems with an
   AMG library (BoomerAMG or AMGCL, single-threaded) as preconditioner and
   count iterations only. If even AMG needs 15+, the problem is not the
   hierarchy.
4. **Direct solve timing.** Factor the dumped systems with a nested-dissection
   Cholesky (CHOLMOD simplicial or Eigen, offline): time per factorisation
   and per triangular solve.
5. **V-cycle microbenchmark.** The present fine-level sweep against a 5-point
   dense-array sweep with the same summation order on one region system:
   time per pass and bitwise equality.
6. **Where the local stages spend time.** A sampling profile of
   `PatchTransport::advance`, `sharpen` and `layer` on the main region: share
   in list copying and merging versus arithmetic, and how many cells
   `geometry_into` builds against how many take part in a sharpened face.
7. **Still faces.** Distribution of |J/I| over the main region's open faces
   after each interval: the share below 10⁻³, 10⁻⁴, 10⁻⁵ m/s decides idea 7.
8. **Box against membership.** Box size and cell count of the three costly
   regions; `entropy_ms` per region.

## Where this note's reading of the code differs from the study

- The study says of cutting regions "nothing was built"; the working copy of
  `native/src/mac_game/regional_tick.hpp` (shown modified, uncommitted)
  contains a cut prototype behind `MA_X_CUT_CELLS`, with pairwise and
  "lumped" exchange laws (`MA_X_CUT_LAW`). Either the study predates it or
  its text should mention it.
- The study gives the main region 81 ms a tick in its cost table and 92 ms
  "with four [workers] (the present limit)" in the scaling sentence; the two
  are presumably different runs or hosts, worth one clarifying line.
- "Does not get faster with more workers" holds past four: 106 ms with two,
  92 with four, 93 with eight.
- The 47 ms outside the regional run are not all single-threaded: 1.13.1
  moved the space sink scan, the carried-germ scan, clock checks, merge, list
  rebuild, the projection's join and the next world's copies onto the
  workers in fixed blocks.

## Sources

### Opened

1. A. McAdams, E. Sifakis, J. Teran, "A parallel multigrid Poisson solver for fluids simulation on large grids", SCA 2010. https://www.math.ucdavis.edu/~jteran/papers/MST10.pdf
2. J. M. Tang, C. Vuik, "Efficient deflation methods applied to 3-D bubbly flow problems", ETNA 26 (2007) 330-349. https://etna.ricam.oeaw.ac.at/vol.26.2007/pp330-349.dir/pp330-349.pdf
3. S. P. MacLachlan, J. M. Tang, C. Vuik, "Fast and robust solvers for pressure-correction in bubbly flow problems", Elsevier preprint dated 31 July 2008 (journal details not checked). https://www.math.mun.ca/~smaclachlan/research/bubbles.pdf
4. P. W. Hemker, R. Kettler, P. Wesseling, P. M. de Zeeuw, "Multigrid methods: development of fast solvers", Appl. Math. Comput. 13 (1983) 311-326. https://ir.cwi.nl/pub/1879/1879D.pdf
5. J. Molenaar, "A simple cell-centered multigrid method for 3D interface problems", Comput. Math. Appl. 31(9) (1996) 25-33. https://ir.cwi.nl/pub/2629/2629D.pdf
6. J. E. Dendy, J. D. Moulton, "Black box multigrid for checkerboard problems", Copper Mountain 2012 abstract. https://grandmaster.colorado.edu/copper/2012/abstract/dendy093967/
7. Y. Notay, "Aggregation-based algebraic multigrid: from theory to fast solvers", CEMRACS 2012 slides. https://groupe-calcul.cnrs.fr/attachments/spip/Documents/Ecoles/CEMRACS2012/marseille12_notay.pdf
8. D. Demidov, "Partial reuse AMG setup cost amortization strategy for the solution of non-steady state problems", arXiv:2108.02054 (2021). https://arxiv.org/abs/2108.02054
9. AMGCL documentation, preconditioners (`allow_rebuild`, `rebuild()`). https://amgcl.readthedocs.io/en/latest/components/preconditioners.html
10. J. M. Tang, S. P. MacLachlan, R. Nabben, C. Vuik, "A comparison of two-level preconditioners based on multigrid and deflation" (preprint; journal details not checked). https://www.math.mun.ca/~smaclachlan/research/defMG.pdf
11. K. Kahl, H. Rittich, "The deflated conjugate gradient method: convergence, perturbation and accuracy", arXiv:1209.1963. https://arxiv.org/abs/1209.1963
12. E. de Sturler, M. L. Parks, "Analysis of Krylov subspace recycling for sequences of linear systems", SAND2005-2794C (record and abstract). https://www.osti.gov/biblio/970200
13. P. Gosselet, C. Rey, J. Pebrel, "Total and selective reuse of Krylov subspaces for the resolution of sequences of nonlinear structural problems", Int. J. Numer. Meth. Eng. (2013), arXiv:1301.7530. https://arxiv.org/abs/1301.7530
14. G. Meurant, P. Tichý, "Approximating the extreme Ritz values and upper bounds for the A-norm of the error in CG", arXiv:1810.02127. https://arxiv.org/abs/1810.02127
15. N. Kwatra, "Practical methods for simulation of compressible flow and structure interactions", PhD thesis, Stanford 2011 (abstract). https://purl.stanford.edu/hn238xx1131
16. H. Liu, N. Mitchell, M. Aanjaneya, E. Sifakis, "A scalable Schur-complement fluids solver for heterogeneous compute platforms", ACM TOG 35(6) 2016 (project page). https://pages.cs.wisc.edu/~aanjneya/papers/SCFSHCP/paper.html
17. J. M. Connors, J. S. Howell, W. J. Layton, "Partitioned timestepping for a parabolic two domain problem" (preprint; journal details not checked). https://www.mathematics.pitt.edu/sites/default/files/research-pdfs/ddecomp1012.pdf
18. M. J. Gander, "Optimized Schwarz methods", SIAM J. Numer. Anal. 44(2) (2006) 699-731 (record). https://archive-ouverte.unige.ch/unige:6275
19. V. Dolean, P. Jolivet, F. Nataf, P.-H. Tournier, "A guided tour of modern domain decomposition", arXiv:2605.24982 (abstract). https://arxiv.org/abs/2605.24982
20. E. M. Constantinescu, A. Sandu, "Multirate timestepping methods for hyperbolic conservation laws", TR-06-15 (record; J. Sci. Comput. 2007). https://vtechworks.lib.vt.edu/items/cc86930c-4aa1-4b04-b56a-6f28ef7d0e60/full
21. Z. Tan, Z. Zhang, Y. Huang, T. Tang, "Moving mesh methods with locally varying time steps", J. Comput. Phys. 200 (2004) 347-367. https://www.math.hkbu.edu.hk/~ttang/Papers/localstep.pdf
22. 80.lv, "Noita: a game based on falling sand simulation" (on P. Purho's GDC 2019 talk). https://80.lv/articles/noita-a-game-based-on-falling-sand-simulation/
23. E. Catto, "Simulation islands", Box2D blog, 2023. https://box2d.org/posts/2023/10/simulation-islands/
24. Oxygen Not Included wiki, "Atmospheric Pressure". https://oxygennotincluded.wiki.gg/wiki/Atmospheric_Pressure
25. Dwarf Fortress wiki, "Pressure". https://dwarffortresswiki.org/index.php/DF2014:Pressure
26. Minecraft wiki, "Water". https://minecraft.wiki/w/Water
27. R. Setaluri, M. Aanjaneya, S. Bauer, E. Sifakis, "SPGrid: a sparse paged grid structure applied to adaptive smoke simulation", ACM TOG 33(6) 2014 (project page). https://pages.cs.wisc.edu/~sifakis/project_pages/SPGrid.html
28. K. Museth, "VDB: high-resolution sparse volumes with dynamic topology", ACM TOG 32(3) 2013 (SIGGRAPH record). https://history.siggraph.org/?p=108935
29. K. K. So, X. Y. Hu, N. A. Adams, "Anti-diffusion interface sharpening technique for two-phase compressible flow simulations", J. Comput. Phys. 231(11) (2012) 4304-4323 (record and abstract). https://portal.fis.tum.de/en/publications/anti-diffusion-interface-sharpening-technique-for-two-phase-compr/
30. W. Aniszewski, T. Ménard, M. Marek, "Volume of fluid (VOF) type advection methods in two-phase flow: a comparative study", Comput. Fluids 97 (2014), arXiv:1405.5140. https://arxiv.org/abs/1405.5140
31. CFD Direct, "Multiphase interface capturing" (OpenFOAM). https://cfd.direct/openfoam/free-software/multiphase-interface-capturing/
32. ANSYS Fluent theory guide, VOF interpolation near the interface (donor-acceptor, geometric reconstruction). https://www.afs.enea.it/project/neptunius/docs/fluent/html/th/node299.htm
33. S. Williams et al., "Implementation and optimization of miniGMG, a compact geometric multigrid benchmark", LBNL report 2012. https://www.osti.gov/servlets/purl/1136783
34. S. F. McCormick, J. Benzaken, R. Tamstorf, "Algebraic error analysis for mixed-precision multigrid solvers", arXiv:2007.06614. https://arxiv.org/abs/2007.06614
35. D. Bindel, CS 6210 lecture notes 2019-09-30 (sparse direct factorisation, nested dissection). https://www.cs.cornell.edu/courses/cs6210/2019fa/lec/2019-09-30.pdf
36. SuiteSparse CHOLMOD licence file. https://raw.githubusercontent.com/DrTimothyAldenDavis/SuiteSparse/dev/CHOLMOD/Doc/License.txt
37. hypre documentation, SMG and PFMG. https://hypre.readthedocs.io/en/latest/solvers-smg-pfmg.html
38. J. Schroder et al., energy-minimising AMG interpolation, Copper Mountain 2010 abstract (precursor of Olson, Schroder, Tuminaro, SIAM J. Sci. Comput. 33 (2011)). https://grandmaster.colorado.edu/copper/2010/abstract/schro109276/schro109276.html
39. G. Hager, blog post on OpenMP barrier overhead (undated in what was read). https://blogs.fau.de/hager/?p=6883
40. P. Vaněk, J. Mandel, M. Brezina, "Algebraic multigrid by smoothed aggregation for second and fourth order elliptic problems", Copper Mountain 1996 (record and abstract; Computing 56 (1996)). https://ntrs.nasa.gov/citations/19970006617

### Cited from abstracts or citing papers only (could not open)

- J. E. Dendy, "Black box multigrid", J. Comput. Phys. 48 (1982) 366-386 (scanned report without text layer).
- R. E. Alcouffe, A. Brandt, J. E. Dendy, J. W. Painter, "The multigrid method for the diffusion equation with strongly discontinuous coefficients", SIAM J. Sci. Stat. Comput. 2 (1981) 430-454.
- M. Khalil, P. Wesseling, "Vertex-centered and cell-centered multigrid for interface problems", J. Comput. Phys. 98 (1992) 1-10.
- Y. Notay, "An aggregation-based algebraic multigrid method", ETNA 37 (2010) 123-146 (server unavailable).
- Y. Saad, M. Yeung, J. Erhel, F. Guyomarc'h, "A deflated version of the conjugate gradient algorithm", SIAM J. Sci. Comput. 21 (2000) 1909-1926 (access refused).
- M. L. Parks, E. de Sturler, G. Mackey, D. D. Johnson, S. Maiti, "Recycling Krylov subspaces for sequences of linear systems", SIAM J. Sci. Comput. 28 (2006) 1651-1674.
- N. Kwatra, J. Su, J. T. Grétarsson, R. Fedkiw, "A method for avoiding the acoustic time step restriction in compressible flow", J. Comput. Phys. 228 (2009).
- S. Osher, R. Sanders, "Numerical approximations to nonlinear conservation laws with locally varying time and space grids", Math. Comp. 41 (1983) 321-336 (access refused).
- C. N. Dawson, Q. Du, T. F. Dupont, "A finite difference domain decomposition algorithm for numerical solution of the heat equation", Math. Comp. 57 (1991) 63-71 (access refused).
- F. Xiao, Y. Honma, T. Kono, "A simple algebraic interface capturing scheme using hyperbolic tangent function", Int. J. Numer. Meth. Fluids 48 (2005) 1023-1040.
- C. W. Hirt, B. D. Nichols, "Volume of fluid (VOF) method for the dynamics of free boundaries", J. Comput. Phys. 39 (1981) 201-225.
- Y. Chen, T. A. Davis, W. W. Hager, S. Rajamanickam, "Algorithm 887: CHOLMOD", ACM TOMS 35(3) (2008) (connection refused).
- C. Batty, F. Bertails, R. Bridson, "A fast variational framework for accurate solid-fluid coupling", SIGGRAPH 2007 (not looked up).
- R. A. Nicolaides, "Deflation of conjugate gradients with applications to boundary value problems", SIAM J. Numer. Anal. 24 (1987) (not looked up).
