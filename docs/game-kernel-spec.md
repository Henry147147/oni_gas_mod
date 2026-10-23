# Game kernel: a semi-implicit solver spec for playable speed

**Status: accepted design, 2026-10-23.** All seven decisions below were
taken by the user on 2026-10-23; the kernel is not built or measured yet. This is a design for a third numerical kernel
(`numerical_kernel = 3`, "game kernel") that the saved game profile would
select. It changes nothing for the reference oracle, the physics gates, the
production kernel or old saves. The user's decisions are recorded at the
[end](#decisions). Nothing here is built or measured except where a
measurement is cited.

The user's requirement for the kernel, stated when deciding on pressure: it
must **feel real and have no inconsistencies**. No flash equalization, and
nothing a reader can see (pressure, temperature, mass, composition) may
disagree with the cell's own state. Speed comes from the numerics, not from
replacing the physics with region averages.

## Why the current kernel cannot get there

The playtest target is a new Terra colony holding fast speed (2×) for 60
frames under the whole CPU lock: at most 0.1 s of wall time per 0.2 s game
tick, everything included. Vanilla takes 0.108 s per tick in the same harness.
The release gate is harder: 250,000 active cells at 1×, 62.5 ms per 0.1 s of
game time including adapters, about 250 ns per cell per 0.1 s.

Where a Terra tick goes today (profile 19, fast, whole lock, clean run):

| Item | ms per tick |
| --- | ---: |
| Budget, everything | 100 |
| Owner advance, mean / p95 | 169 / 375 |
| of which flow + phase change + settling + conversions | 104 |
| of which heat edits, transfers, terrain, ledger and projection | 38 |
| Observed tick spacing, mean (publication and pacing included) | 260 |

The flow stage alone on a captured frame-50 Terra world (offline, 12 workers)
is 79 ms of wall time but 340 ms of worker time, and it does this per tick:

| Work per 0.2 s tick | Count |
| --- | ---: |
| Substeps, summed over regions | 1,724 |
| Nonlinear pressure roots (Newton iterations) | 5,528 |
| Linear iterations inside them | 49,371 |
| Independent component solves | 9,198 |

At 250,000 cells the same kernel measures 1,390 ms per 0.1 s, 22× over the
release budget, before adapters.

These costs are structural. They come from what the production kernel is,
not from how well it is coded:

- **It is a compressible, acoustic solver.** Every substep solves the full
  implicit EOS pressure root for every cell, because gas sound waves and
  liquid elastic waves are resolved. The step is bounded by fronts (magma
  vapour at 14 m/s into vacuum, 1.8 MPa hydrogen pockets) and by EOS
  admission, so Terra takes 1,724 substeps per tick where the game needs one.
- **It admits by rejection.** A candidate that fails CFL, EOS domain, entropy
  or audit is thrown away and retried smaller. Retry storms are the cause of
  the ten-cycle run's 20 s frames at frame 4390; the work discarded is not
  recoverable by optimization.
- **It is exact.** Interval audits at 128 binary64 epsilons with exact
  expansions, full-range arithmetic for flagged values, per-write rounding
  ledgers and reference-ordered Gauss–Seidel sweeps that must reproduce the
  oracle to roundoff. The sweeps are why 12 workers give 4× (340 ms of worker
  time for 79 ms of wall time). The audit alone is 35 ms of worker time per
  tick.
- **It does nine split stages per substep**, each reading and writing every
  field, plus a per-cell Newton EOS (long double, bisection fallback) for
  every mixed cell at every stage that needs a temperature or pressure.
- **The owner copies the world for every edit** (heat, transfers, phase,
  settling, conversions each publish a new immutable world) and re-evaluates
  the reference EOS on every cell for the presentation projection (30 ms).

Tuning has already taken the owner from 4.4 s to 0.17 s per tick while
keeping all of the above. What is left is the physics kernel itself, and the
measured tradeoff variants (barotropic pressure, fixed-temperature
transitions, donor-cell liquids, slower liquid sound) were slower or within
7% because they kept the substep structure. The gap to the fast clock is
2.6×; the gap to the release gate is about 22×. No remaining tuning item is
estimated above 2×.

## What stays fixed

User decisions that the game kernel keeps, with no exceptions:

- `MacClosedWorld` is the solver. ONI's fluid simulation is never used.
- Every cell keeps its gas and miscible-liquid composition; faces keep
  momentum, so winds, jets, inertia and sloshing exist. Nothing is dropped
  into a bucket model.
- Every constituent, passive quantity, momentum component and energy is
  conserved by construction through shared face fluxes and booked exchanges.
  Nothing is clipped, floored or renormalized to close an account. Traces
  below the saved threshold go to the booked trace sink, as now.
- Physical pressure, ONI's partial-pressure breathing rules, and
  pressure-dependent boiling with latent heat (Clausius–Clapeyron from the
  catalogue).
- Determinism: bit-identical results for the same inputs regardless of worker
  count. Exact checkpoint continuation. Physics is never adapted to wall-clock
  load.
- The reference oracle, the production kernel, the eight physics gates and
  their inputs, grids and bounds are untouched. Old saves keep their kernel.
- Balance stays ONI's, with named balance ledgers for every physical mismatch.

## The game kernel

One pass per game tick over a structure-of-arrays state, no substep retries,
one linear solve for pressure and one for heat, and every stage cell- or
face-local so that twelve workers scale. Every cell keeps its own physical
pressure from its own masses, volume and energy; what changes against the
production kernel is how the pressure is advanced in time, not what it is.

### State

Per cell: present-species masses (sparse, species-major as now), stored
internal energy U, one volume fraction per liquid phase present, passives
(germs), a region id. Per face: canonical normal momentum J. Per region
(connected open component, as the regional controller already finds them): a
sleep counter and the region's totals for the audit. About 200 bytes per
cell: 250,000 cells is about 50 MB, against 1.6 KiB per cell now.

### Time

The tick is the step: 0.2 s of game time per pass. Cells whose advective
Courant number exceeds the limit (fronts, jets) subcycle locally within the
tick; faces between differently stepped cells carry one time-integrated flux,
which the user accepted on 2026-10-18. Sound does not limit the step because
pressure is implicit (below). A pass never rejects: every stage is
positivity-preserving by construction, so admission cannot fail and no work
is ever discarded. The attempt budget, halving schedule and predictive
controller go away for this kernel.

### Pressure: semi-implicit, all speeds (decision 1)

The user rejected a region-average pressure: it flash-equalizes a breached
room and shows a pressure the cell's own gas does not have. The game kernel
instead keeps the production kernel's physics (each cell's pressure is its
own ideal-gas mixture pressure from its masses, volume and U; momentum on
faces is driven by the local pressure gradient and gravity) and changes only
the time integration, in the manner of the ICE family of semi-implicit
compressible methods:

- The pressure at the end of the tick is unknown; the mass and energy
  equations are linearized about the current state, which gives one symmetric
  positive-definite linear system for the new pressure field (a Helmholtz
  equation: compressibility on the diagonal, `dt² / ρ` face coefficients off
  it). One linear solve per tick with the existing geometric multigrid or
  Jacobi PCG on static row-block partitions, about ten iterations. The
  production kernel solves the same physics as a nonlinear root with EOS
  evaluations per cell per Newton step and 49,371 linear iterations per tick;
  the game kernel takes the linearization as its time-integration error.
- Face momentum is then updated by the new pressure gradient and gravity, and
  masses, U and passives move by the resulting face volumes. Pressure work on
  each cell is booked from the actual face sweeps (`p · dV` from the same
  volumes that moved the mass), so U+K+G is conserved by construction and
  there is no separate energy closure to retry.
- Because the pressure is implicit, a sound wave that would cross more than
  one cell in a step is not resolved: it is damped within the step. Mass still
  moves only as fast as the momentum equation accelerates it through the
  opening, so a breached wall equalizes over the time the gas takes to flow,
  with inertia and sloshing, not within one tick. The overlay, hover and every
  reader show the cell's own pressure, so there is nothing inconsistent to see.
- Gravity enters the same solve, so dense gas pools, pressure heads and
  hydrostatic liquid columns are physical.
- There is no entropy admission and no per-cell EOS root. The nonlinearity
  of the ideal gas is handled by the linearization; the measured floor
  (F1, F2) decides whether one Picard correction per tick is needed.

What this drops against the production kernel: resolved acoustics (waves
faster than a cell per tick are smeared, not propagated), the strict 16-epsilon
nonlinear closure of each substep, and the entropy admission gate. The player
keeps real pressure everywhere, gradual equalization, winds and inertia.

### Liquids (decision 2)

Liquids keep the catalogue density `ρ(T)` and carry a **saved stiffness**: a
pressure rise per fractional volume deficit, used in the same implicit
pressure solve. Because the solve is implicit, the stiffness does not limit
the step, only the conditioning of the solve, which the independent component
solves already handle for stiff pockets. A sealed, completely liquid-filled
region that expands on heating therefore stays in place and raises its own
pressure (the user's choice), and ONI's readers see that pressure. The
elastic energy of the compression is booked into U through the same `p · dV`
work, so energy closes. The stiffness is a saved profile setting. The user
chose a **reduced, tunable starting value**, about 1/25 of the catalogue's
bulk modulus (the measured "liquid sound ×0.2" variant), to be tuned with the
vacuum cap at performance evaluation; the real modulus is the upper choice. A
heated sealed tank reaches lower pressures than reality. There is no
liquid sound speed to resolve either way and no 1,000 kg water cell, because
the compression is small at any pressure the game produces.

### Vacuum (decision 3)

A region with no gas is void. A gas cell beside void is accelerated into it
by its own pressure gradient (the void cell joins the region at zero
pressure), so the front is resolved by the momentum equation and its cells
subcycle locally at the advective limit. The number of local subcycles a
front may take per tick is a **saved, tunable cap** (`front_cells_per_tick`):
up to the cap the front advances at its physical rate, beyond it the front's
flux is held at the cap's rate for the rest of the tick. The user chose this
over one cell per tick, keeping as much of the physical front as the cap
allows, with the cap left for later performance evaluation. A large breach
into vacuum costs the cap times the front's cell count in extra local work
that tick; the bulk of the region still takes one step. Free expansion into
the void keeps U and does no work; the entropy it produces is reported in the
ledger.

### Transport

Flux-form, one limited-linear (MUSCL) reconstruction pass per tick, with
Zalesak flux-corrected limiting so every species, passive and liquid fraction
stays nonnegative and bounded without a retry. U is transported with the
material and takes the `p · dV` work of its own face sweeps. Face momentum moves by
limited upwind dual transport with the same shared face volumes. Immiscible
liquid phases layer by density as a buoyant exchange flux inside the same
pass, replacing the separate settling stage and its world copy; miscible
liquids are one phase with a composition. The liquid surface is a volume
fraction with a height-function normal, sharp to one cell; the PLIC interface
and sharp-interface boiling of the production kernel are not used.

### Heat

Conduction between fluid cells and with registered solid faces is one
backward-Euler linear solve per tick on the same partitions (Jacobi PCG,
warm-started). Mixed cells use the cell's total heat capacity; there is no
per-cell EOS relinearization. Shock heat and compression regularization do
not exist in this kernel: unresolved compression is damped by the implicit
pressure step and its work is booked exactly. Viscosity is an explicit face term.

### Phase change

Cell-local saturation relaxation once per tick at fixed U and volume, as the
owner does now (`1 − exp(−dt / 1 s)` of the way to equilibrium, latent heat
booked per pair, Clausius–Clapeyron from the catalogue), but fused into the
pass rather than a separate world copy. The vapour it creates raises the
cell's own pressure before the next tick's solve.

### EOS

Closed forms only. Gas: ideal mixture, T from U with per-species constant or
tabulated `Cv`, with the catalogue's chemical offsets. Liquid: `ρ(T)` and
`c(T)` from the catalogue and the saved stiffness. Mixed cell: gas in the
volume the liquid leaves. No Newton roots, no bisection, no long double; the
linearized pressure solve replaces them. The presentation fields
(temperature, pressure, composition, breathability) fall out of the final
pass as arrays, so the end-of-frame projection that re-evaluates the
reference EOS on every cell is removed.

### Audits

Conservation is by construction; the audit checks it. Once per tick per
region, species, passives, momentum (with wall and body impulse) and U+K+G
are reconciled in double-double against a relative bound of 1e-12 of the
operands. A failing audit is a fault that stops the owner and dumps the
state: it means a bug, and it is never retried or hidden. Entropy is reported
in the ledger as a diagnostic and is not an admission gate. There are no
exact expansions, no full-range arithmetic and no per-write rounding ledgers
in this kernel.

### Parallelism and determinism

Every stage is Jacobi-form (reads the old arrays, writes the new ones) over
static 12-worker row blocks with the fixed-tree reductions the production
kernel already has. There is no sequential sweep and no colouring, so results
are bit-identical for any worker count and the pass should scale close to
memory bandwidth. Rows are partitioned once per topology change.

### Sleep

A region whose largest face speed, temperature difference and phase
disequilibrium are all below saved thresholds for three ticks sleeps: no
pass, no solve, no publication change. Any edit, transfer, terrain change or
neighbour wake restores it. This replaces the exact equilibrium certificate,
which almost never certifies a live colony.

### Owner pipeline

The owner keeps two buffers per region and advances in place: heat edits,
transfers, terrain, phase change, settling and conversions write the next
buffer with receipts, instead of each publishing a new immutable world. The
kernel's final pass writes the presentation arrays; the presentation worker
copies them once. The pacing gap between owner advance and observed tick
spacing (about 50 ms now, of which 30 ms is the projection) should fall to
the one copy. Save framing, ledgers, receipts and the trace and space sinks
are unchanged in meaning; the checkpoint gets a new kernel-3 image.

### Persistence

New checkpoint version and game-profile family 2. A kernel-3 save restores
only under kernel 3. Old saves restore their own kernel and never migrate
implicitly. Checkpoint rules and version assignment stay as in
[MAC_CHECKPOINT.md](../native/MAC_CHECKPOINT.md).

## Expected cost

These are estimates, not measurements. The anchor is the scratch
plain-binary64 pressure-implicit prototype measured at 557 ns per
cell-substep on one core
([scaling analysis](solver-scaling-analysis.md#options)); the game kernel does
one such step per tick plus one heat solve.

| Per cell per 0.2 s tick, one core | ns (estimate) |
| --- | ---: |
| Reconstruction, FCT transport, momentum | 300 |
| Implicit pressure solve, about ten PCG or two V-cycles | 200 |
| Conduction solve | 100 |
| EOS, phase change, region totals, audit | 100 |
| Total | 700 |

| Workload | One core | 12 workers at 4× (estimate) | Budget |
| --- | ---: | ---: | ---: |
| Terra, about 31,000 owned fluid cells | 22 ms per tick | 6 ms per tick | 100 ms per tick with everything |
| Release, 250,000 active cells | 175 ms per tick | 44 ms per tick (22 ms per 0.1 s) | 62.5 ms per 0.1 s |

Against the fast clock on Terra the margin is large even if the estimate is
off by 5×. Against the release gate the margin is about 3× before adapters.
Memory is about 50 MB at 250,000 cells. The vacuum front cap (decision 3) and
a Picard correction (decision 1) add to this where they apply; the prototype
measures both.

## What the player notices

- Every cell shows its own pressure. A breached room equalizes over the
  seconds the gas takes to flow through the opening, with inertia and damped
  sloshing; pressure waves faster than a cell per tick are not shown. Winds,
  drafts and jets from vents and pumps exist and carry momentum.
- Gas reaches vacuum at its physical front speed up to the saved cap of cells
  per tick, then at the cap.
- Liquids keep their catalogue density and compress only slightly under the
  saved stiffness. A sealed full tank that is heated raises its pressure
  instead of leaking volume. There is no 1,000 kg water cell and no liquid
  sound; levelling and layering keep inertia.
- Gases and miscible liquids mix and stay mixed; partial pressures, breathing,
  pressure-dependent boiling and latent heat are as in the current plan.

## Validation

The game kernel is judged by the [base-game floor](base-game-floor.md), not by
the physics gates, which continue to validate the reference:

- F1–F5 with the floor's unchanged setups, metrics and bounds: an error no
  larger than vanilla's. F1 and F2 (equalization timing and split) test the
  implicit pressure's linearization and damping and must be measured first;
  they decide whether a Picard correction per tick is needed.
- Terra60 at fast speed under the whole lock: mean observed tick spacing at
  most 0.1 s, closed ledgers, no faults, owner mean/p95/max reported.
- The ten-cycle unattended run from a fresh prepared base on the new profile.
- Oracle comparison on calm scenes (resting stratified gas, hydrostatic water,
  a 20 Pa wave at long wavelength) within the floor's reported errors, for
  the record.
- Determinism control: one and twelve workers, byte-identical images.
- Conservation control: every ledger closes to 1e-12 on each floor scenario
  and on Terra60.

## Scope and steps

1. **Scratch prototype** (outside the repository, as the floor requires for
   feature variants): the pass on the existing bench dumps (the 98,304-cell
   Terra frame-50 world), measuring ns per cell per tick and worker scaling,
   and F1/F3 on the owner's arena fixture. This decides whether the estimate
   above holds before any repository change.
2. **Kernel 3** behind `numerical_kernel = 3` with its own checkpoint image,
   selected by the game profile; the regional controller's component finding
   reused for regions; sleep; audits; determinism controls.
3. **Owner pipeline**: in-place buffers, fused edits, presentation arrays
   from the pass, publication copy.
4. **Floor and acceptance**: F1–F5, Terra60, ten cycles; record in
   [base-game-floor.md](base-game-floor.md) and [MAC_KERNEL.md](../native/MAC_KERNEL.md).

The production kernel and reference stay buildable and tested throughout; the
gates keep passing on them.

## Alternatives considered

- **Keep tuning kernel 2.** Remaining items (fused stages, parallel sweeps,
  fewer EOS reads, an EOS memo cache) are each estimated below 2× and keep
  the 1,724 substeps and the retry schedule; the release gate is 22× away.
- **GPU.** Closed by the user on 2026-10-19
  ([why](gpu-acceleration-research.md)).
- **A bucket or cellular model** (region-only state, no momentum). Faster
  still, but it removes per-cell composition and momentum, which the user has
  locked.
- **Low-Mach splitting** (one thermodynamic pressure per region plus a
  projection). Cheapest of the momentum-keeping designs, but it flash-
  equalizes a breached room and shows a region pressure the cell's gas does
  not have. Rejected by the user on 2026-10-23 as not feeling real.
- **Only the tradeoff variants measured so far.** All four kept the substep
  structure and were slower or within 7%.

## Decisions

Each is a user decision under the 2026-10-22 floor amendment, which requires
separate adoption for pressure, boiling, sharp interfaces and acoustic detail.

Taken 2026-10-23:

1. **Pressure: semi-implicit, all speeds.** Every cell keeps its own physical
   pressure; one linearized implicit pressure solve per tick; no resolved
   acoustics. The user's requirement: it must feel real, no flash
   equalization and no inconsistencies between what readers show and the
   cell's state. The region-average (low-Mach) design is rejected.
2. **Liquids: incompressible density with a saved stiffness** in the implicit
   solve; a sealed full-liquid region that expands raises its own pressure
   rather than booking volume to the reservoir. The stiffness starts at a
   reduced, tunable value, about 1/25 of the catalogue's bulk modulus.
3. **Vacuum front: physical rate up to a saved, tunable cap of cells per
   tick**, keeping as much of the front as the cap allows; the cap is left
   for later performance evaluation.
4. **Audits: fault-only at 1e-12 relative**, once per tick per region in
   double-double; a failure stops the owner and dumps the state. Entropy is a
   ledger diagnostic, never an admission gate. Game profile only.
5. **Boiling: cell-local saturation relaxation only**, pressure-dependent with
   latent heat; no sharp-interface model in the game profile.
6. **Persistence: a new kernel selector, checkpoint version and profile
   family.** Old saves keep their kernel; there is no migration, and the
   ten-cycle acceptance starts from a fresh prepared base.
7. **Acceptance: the floor** (F1–F5 no worse than vanilla), Terra60 at fast
   speed under the whole lock, and the ten-cycle run. The physics gates
   continue to validate the reference only and are not run against kernel 3.

Tunables left for performance evaluation (saved profile settings, not
physics gates): the vacuum front cap in cells per tick, the liquid stiffness,
the sleep thresholds, and whether the pressure solve takes one Picard
correction per tick.
