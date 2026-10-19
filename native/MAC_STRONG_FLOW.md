# Strong-flow accuracy investigation

This is the accuracy gate for [`MacClosedWorld`](MAC_SOLVER.md) on strong ideal-gas
shocks and rarefactions. It is **failed**. With the production defaults, collision
density refinement fails only from N64 to N128 (0.856 against 0.85). Since the
2026-10-19 user decision, separation is judged against a fine
[resting-slab reference](#resting-slab-separation-reference) of the gate's own
discrete start rather than the textbook Riemann core; against it the defaults fail
only the N256 velocity bound (0.0759 against 0.06). The central pressure bound remains
the [classical reference](#acceptance-criteria) error. The opt-in monotone
regularization (compression 4, heat 4) lowers every Sod and collision error and passes
both, with a phase-sensitive collision margin; separation is unchanged, so it fails
the same velocity bound. The latest diagnosis localizes a convention mismatch between
the pressure step and the momentum remap under curved deformation; its share of the
separation error is not yet quantified. Gate 2 in
[playtesting readiness](../docs/playtesting-readiness.md) requires this gate and the
vacuum gate to pass with production defaults.

The tracked test is `native/tests/mac_riemann_tests.cpp`. Its scientific inputs,
grids, timesteps and limits below are fixed; no failed result may be fixed by
changing them. Unless stated otherwise, results are single Windows/MSVC runs; a
method that failed was not repeated on Linux.

The latest GNU Release/IPO 2/4/3/2 sequence also completes all nine profiles on Linux
in 144.96 seconds after [remap preparation reuse](MAC_TRANSPORT.md#prepared-admission-and-reusable-candidates).
Its saved accounts pass independent analysis and its finest separation errors
reproduce the failure below. The production defaults still fail collision density
refinement with byte-identical saved output. See [evidence and limits](../docs/production-timestep-progress.md#prepared-remap-admission).

The opt-in coupled two-moment production pressure method also fails the
original collision density refinement: N64/N128 errors 0.04256384/0.03668006
give 0.86176589 against 0.85. Scientific inputs, schedules and bounds are
unchanged. It stops after five profiles; the saved partial account review
passes 59,205 checks and cannot certify the gate. This does not change the
production-default results below.

## Benchmark

The reference is the exact ideal-gas Euler Riemann solution, with shock jump curves,
isentropic rarefactions and common contact pressure/velocity; see the
[Clawpack Riemann book](https://www.clawpack.org/riemann_book/html/Euler.html).

All cases use nitrogen with a conserved helium mass ratio of 1e-28 and a second
positive passive ratio of 1e-30. Gamma comes from the explicit constant-capacity
material coefficients; it is not rounded to 1.4. Two identical transverse rows are
periodic vertically, with rigid horizontal ends, grid spacing h = 1/N and unit depth.
Cell mass, stored U and normal face J are primary. There is no vacuum cell or phase
interface. The discontinuity starts at x = 0.5:

| Case | Left (density kg/m³, velocity, pressure Pa) | Right | Duration |
| --- | --- | --- | --- |
| Sod | (1, 0, 100000) | (0.125, 0, 10000) | 0.2 / sqrt(100000) s |
| Collision | (1, +1.5 c₀, 100000) | (1, −1.5 c₀, 100000) | 0.06 / sqrt(100000) s |
| Strong separation | (1, −4 c₀, 100000) | (1, +4 c₀, 100000) | 0.03 / sqrt(100000) s |

c₀ is the left initial sound speed. The separating solution keeps strictly positive
pressure and density; it approaches vacuum without testing an exact vacuum boundary.
Sod compares the whole unit interval before waves reach the walls. Collision and
separation compare x in [0.25, 0.75] before wall-generated waves enter that window.
Global mass, impulse, energy and entropy checks cover the whole closed grid.

The grid sequence is N64/128/256. The nominal timestep is `0.25 h / (q c₀)` with
q = 2 (Sod), 2.5 (collision) and 5 (separation).

## Acceptance criteria

- Each profile finishes with no rejected attempts and at most ceil(duration/dt)+2
  accepted steps; source state and clock are unchanged.
- Each successive density, pressure and velocity L1 error is less than 0.85 of its
  predecessor (joint refinement).
- Joint refinement compares every case, separation included, with the exact
  solution.
- At N256 every Sod and collision normalized error is below 0.06.
- Separation at N256 (user decision, 2026-10-19), against the
  [resting-slab reference](#resting-slab-separation-reference) with the same metrics:
  every normalized density, pressure and velocity error is below 0.06; both central
  cells' density errors are below half their reference cell averages; and their
  relative pressure errors are no larger than the classical reference's central
  pressure error below, so the low-pressure core cannot hide inside a small
  domain-averaged error.
- Every nitrogen, helium and passive inventory stays positive. Total entropy may
  decrease only within 128 binary64 epsilons of Σ|U|/T over the initial and final
  states.

**Classical separation reference** (user decision, 2026-10-19). This record supplies
only the central pressure bound value. The original central pressure bound, half the
exact cell average, is beyond textbook methods. With these
inputs and metrics at N256, first-order Godunov misses it by 5.49× and MUSCL-Hancock
by 26.5×, both with exact Riemann fluxes. Both pass collision refinement easily.
The bound is the reference's own central pressure error, currently 26.530350009 at N256.
The reference is binary64 primitive MUSCL-Hancock on one row of the same grid:
- minmod slopes in density, velocity and pressure, with mirrored rigid-wall ghost
  cells;
- a half-step predictor in the primitive quasi-linear form;
- Godunov fluxes from the exact Riemann state at x/t = 0: bisection for the star
  pressure, and a two-rarefaction vacuum state when ur − ul ≥ 2(cl + cr)/(γ − 1);
- the gate's own γ, maximum timestep and duration, with a final partial step.

`mac_riemann_tests` records it as `RREFERENCE`. `review_mac_riemann.py` repeats the
algorithm in the same operation order and requires the recorded value to agree within
1e-9 relative. It also rejects a tampered reference record. The algorithm is shared
with the resting-slab reference in `native/tests/mac_riemann_reference.hpp`.

### Resting-slab separation reference

The gate starts the centre face at J = 0 (the review checks this as "Source face
momentum"). Read with the solver's own dual-cell kinetic energy, that is the Riemann
data plus a slab of one gate cell at rest ([analysis](#initialization-the-resting-centre-face)).
The user decided on 2026-10-19 to keep this start and to judge separation against a
fine solution of it:

- the classical algorithm above (`mac_riemann_reference.hpp`) on R = 8 fine cells per
  gate cell, N = 256, rigid mirrored ends, one row;
- every fine cell at its side's Riemann state, except the R fine cells of the centre
  face's dual, [1/2 − 1/(2N), 1/2 + 1/(2N)], which are at rest (density 1 kg/m³ and
  100000 Pa everywhere);
- fine timestep `dt_gate / R` and the gate's duration, with a final partial step;
- on the gate grid: density and pressure are the means of the R fine-cell values,
  and face velocity is momentum over mass of the two fine cells beside each gate face.

The gate then forms exactly the metrics it uses against the exact solution, with
these values as the expected ones. `mac_riemann_tests` prints every gate-cell
reference value as `RSLAB control N R cell density pressure face-velocity` and the
five separation measures as `RSLABERROR control N density-L1 pressure-L1
velocity-L1 central-density central-pressure`. `review_mac_riemann.py` recomputes the
reference in binary64 Python in the same operation order (each of the 768 values must
agree within 1e-9 relative; on Linux all are bit-equal), recomputes the five measures
from the saved state with its independent EOS, and rejects a tampered `RSLAB` record.
A face state is reused only for a bit-equal left/right pair; the Python reference
takes about 35 s, and a partial review about 40 s.

R = 8 is the finest refinement the review can repeat within its 180 s bound. Its
convergence was measured with `ma_riemann_slab_reference 256 2 4 8 16 32`, as
gate-metric differences between successive refinements:

| Refinements | Density L1 | Pressure L1 | Velocity L1 | Central density | Central pressure |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2 vs 4 | 0.00345 | 0.00449 | 0.01536 | 0.0679 | 0.0039 |
| 4 vs 8 | 0.00231 | 0.00233 | 0.00973 | 0.0226 | 0.0146 |
| 8 vs 16 | 0.00148 | 0.00118 | 0.00537 | 0.0091 | 0.0088 |
| 16 vs 32 | 0.00087 | 0.00060 | 0.00301 | 0.0042 | 0.0049 |
| 8 vs 32 | 0.00233 | 0.00178 | 0.00830 | 0.0132 | 0.0137 |

Differences fall by about 0.55–0.6 per doubling, so the R = 8 values are within about
0.003 (density and pressure L1), 0.012 (velocity L1) and 2–3% (central cells) of the
converged slab solution. Every bound is achievable by the reference itself at coarser
refinement: R = 2 differs from R = 8 by 0.0056 / 0.0068 / 0.0235 in L1 and
0.089 / 0.011 at the centre. The slab's converged central density is
0.0247 kg/m³ (R = 32), about 76 times the Riemann star density.

Density and pressure compare cell averages; velocity compares the normal face
reading. Errors are normalized by the initial left density, 100000 Pa and c₀, and by
the comparison-window length. Native reference averages use eight-point Gauss
quadrature split at every exact wave boundary.

`review_mac_riemann.py` independently brackets the star pressure in 80-digit
arithmetic, integrates the fan power laws analytically and reconstructs the mixture
EOS and entropy from masses, U and volume. It checks exact-fraction constituent,
passive, momentum (`ΔJ = wall impulse on the fluid`) and U+face-K accounts, and must
reject altered trace, wall-momentum and pressure-error records. A run that stops early
can only get a `--partial` review; a partial review cannot certify the gate. Bounds:
180 s per native run or review and 2 MiB per raw pipe.

## Results by method

Selectors are listed as material / momentum / compression / heat
([settings](MAC_SOLVER.md#settings-production-defaults-and-opt-in-selectors)).
Ratios are collision N128/N64 density / pressure / velocity; failing values are in
bold. Sod passed in every row. Separation N256 and central figures in this section
before the [resting-slab results](#separation-against-the-resting-slab-reference) are
against the exact Riemann solution, the criterion before the 2026-10-19 decision.

### Separation against the resting-slab reference

Linux GNU Release; refinement ratios are unchanged (against the exact solution) and
pass in every row. Bounds: L1 below 0.06, central density below 0.5, central pressure
at most 26.530350009 (`RREFERENCE`).

| Configuration | Density L1 | Pressure L1 | Velocity L1 | Central density | Central pressure | Source |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 / 1 / 2 / 2 (defaults) | 0.0119 | 0.0220 | **0.0759** | 0.049 | 0.742 | probe; the gate stops at collision first |
| 1 / 1 / 4 / 4 | 0.0119 | 0.0220 | **0.0759** | 0.049 | 0.742 | gate `--selectors 1,1,4,4`, reviewed |
| 2 / 4 / 3 / 2 | 0.0185 | 0.0209 | **0.0658** | **0.656** | 0.863 | probe |
| 2 / 3 / 3 / 2 | 0.0121 | 0.0204 | **0.0666** | 0.107 | 0.714 | probe |
| 0 / 0 / 2 / 2 | 0.0308 | 0.0518 | **0.1545** | 0.239 | 0.522 | probe |

Against finer references the defaults' velocity error grows slightly (0.0811 at
R = 16, 0.0841 at R = 32; 2 / 4 / 3 / 2: 0.0741 at R = 32), so R = 8 is, if anything,
lenient. For the defaults about 0.029 of the velocity error lies within 0.05 of the
centre, 0.023 between 0.05 and 0.1, 0.021 between 0.1 and 0.18 and 0.003 beyond. Sod
and collision are unchanged by this decision. With 1 / 1 / 4 / 4 the gate passes Sod
and collision and stops at the separation velocity bound; the saved capture passes
`review_mac_riemann.py --partial` and its independent resting-slab check, and every
corruption control (trace, wall, profile, classical reference, resting-slab reference)
rejects when applied to it.

| Configuration | Collision ratios | Outcome |
| --- | --- | --- |
| 1 / 0 / 0 / 0 (first source) | 0.519 / 0.540 / **1.115** | Velocity grows; oscillations behind the shocks |
| 1 / 0 / quadratic after the step (ordering since replaced) | 0.814 / **1.385** / 0.632 | Velocity fixed; pressure error grows near the shocks |
| 1 / 0 / 1 / 0 (symmetric half steps) | 0.818 / **1.381** / 0.634 | Temporal ordering alone does not help |
| 1 / 1 / 1 / 0 | **0.889** / **1.346** / 0.657 | Central entropy excess about 100 J/(kg K) |
| 1 / 1 / 1 / 1 | **0.970** / **1.025** / 0.603 | Central entropy excess falls to about 1.4 J/(kg K); pressure alternates behind fronts |
| 1 / 1 / 2 / 1 | **0.860** / **0.882** / 0.607 | Pressure improves; central entropy excess rises to about 8.7 J/(kg K) |
| **1 / 1 / 2 / 2 (production default)** | **0.856** / 0.796 / 0.611 | Density alone fails |
| 1 / 1 / 2 / 3 | **0.919** / **0.898** / 0.597 | Worse; default kept at heat 2 |
| 2 / 1 / 2 / 2 | **0.857** / 0.795 / 0.611 | Affine remap repair is not the collision cause |
| 2 / 2 / 2 / 2 | **0.857** / 0.795 / 0.611 | No change |
| · / 3 / 2 / 2 (material selector not recorded) | **0.856** / 0.796 / 0.613 | No change |
| 2 / 3 / 3 / 2 | 0.848 / 0.819 / 0.613 | Collision passes; separation fails (below) |
| **2 / 4 / 3 / 2 (best opt-in)** | passes | Separation fails (below) |
| 1 / 1 / 4 / 4 (monotone, Linux) | 0.840 / 0.802 / 0.674 | Sod and collision pass; separation unchanged from the defaults ([below](#monotone-regularization-modes-4)) |
| 2 / 4 / 4 / 4 (Linux) | 0.842 / 0.800 / 0.676 | Sod and collision pass; separation as 2 / 4 / 3 / 2 |

Between the first two rows, Linux runs also stopped on transfers below binary64
range (a 9.12e-436 J kinetic parcel, an 8.59e-325 kg helium parcel, a 3.6e-585 kg m/s
momentum share and an exponent of −3049816383). The no-clipping rule turned each into
a rejection; [range arithmetic](MAC_RANGE.md) removed them.

**Production defaults.** The last run of the defaults (1 / 1 / 2 / 2) stops at the
collision N128 density check, before collision N256 or any separation profile:

| Case | Grid | Density L1 | Pressure L1 | Velocity L1 |
| --- | --- | --- | --- | --- |
| Sod | 64 | 0.0112500740906 | 0.00814291073711 | 0.0185867340515 |
| Sod | 128 | 0.00672611164722 | 0.00465366408982 | 0.0105312985123 |
| Sod | 256 | 0.00351379993016 | 0.00232938014518 | 0.00515552809273 |
| Collision | 64 | 0.0428586118062 | 0.101817988157 | 0.0738480553673 |
| Collision | 128 | 0.0366940677028 | 0.0810854182850 | 0.0451234393987 |

Collision ratios are 0.85616557 / 0.79637616 / 0.61103084 against 0.85. All five
completed profiles pass their individual EOS, inventory, energy, entropy and
momentum checks.

A diagnostic copy of the test, which reports instead of stopping, ran the remaining
default profiles on GNU Release at `5737fb6`:
- **Collision:** N128→N256 refines by 0.412 / 0.388 / 0.480. N256 L1 is
  0.01513 / 0.03144 / 0.02165, so only the first refinement fails.
- **Separation:** refines by 0.437 / 0.506 / 0.759 and 0.549 / 0.596 / 0.707. N256 L1
  is 0.01787 / 0.02327 / **0.07411**. Central relative density / pressure errors are
  **77.52** / **732.98**, against 0.5 and the classical reference's 26.53.

| Measure (separation, N256) | Production defaults | Classical reference | Bound |
| --- | ---: | ---: | ---: |
| Velocity L1 | **0.0741** | 0.1171 | 0.06 |
| Central relative density error | **77.52** | 0.0038 | 0.5 |
| Central relative pressure error | **732.98** | 26.53 | 26.53 |

**Coupled compressive work (2 / 3 / 3 / 2).** All nine profiles complete with zero
timestep retries. Collision ratios are 0.848334 / 0.819035 / 0.613088 (64→128) and
0.414988 / 0.386744 / 0.481236 (128→256). Separation at N256 has density / pressure /
velocity L1 0.017920 / 0.021640 / 0.076338 and central relative density / pressure
errors 81.8118 / 721.5274. Its first attempt stopped on a false entropy rejection,
fixed by measuring relative entropy (see [coupled compressive work](MAC_PRESSURE.md#coupled-compressive-pressure-work-mode-3)).

**Bounded carried energy (2 / 4 / 3 / 2).** This is the best tracked result. All nine
profiles complete with zero timestep retries; Sod and collision pass their gates.
Separation at N256 fails:

| Measure | Value | Limit |
| --- | --- | --- |
| Density / pressure / velocity L1 | 0.0178341 / 0.0193070 / **0.0908767** | 0.06 each |
| Central relative density / pressure error | **25.9256** / **56.9301** | 0.5 each |
| Central cells (left / right) | 0.008384 / 0.008782 kg/m³ at 30.551 / 29.368 K | Exact 0.00032616 kg/m³ at 13.650 K |

The run takes about 110 s on Windows, within the 180 s bound.

### Monotone regularization (modes 4)

Compression 4 and heat 4 add the Christensen/LULESH monotonic limiter to the
pressure-sensed laws ([compression](MAC_PRESSURE.md#monotone-limited-acoustic-compression-mode-4),
[heat](MAC_HEAT.md#face-conductance-laws)). Coefficients are unchanged. Single GNU
Release runs on Linux, `ma_mac_riemann_tests --selectors 1,1,4,4`:

| Case | Grid | Density L1 | Pressure L1 | Velocity L1 | Defaults (same grid) |
| --- | --- | --- | --- | --- | --- |
| Sod | 64 / 128 / 256 | 0.01101 / 0.00647 / 0.00346 | 0.00785 / 0.00448 / 0.00224 | 0.01729 / 0.00993 / 0.00484 | 0.01125 / 0.00673 / 0.00352, 0.00814 / 0.00465 / 0.00233, 0.01859 / 0.01053 / 0.00516 |
| Collision | 64 / 128 / 256 | 0.04178 / 0.03510 / 0.01340 | 0.09096 / 0.07298 / 0.02454 | 0.06267 / 0.04221 / 0.01927 | 0.04286 / 0.03669 / 0.01513, 0.10182 / 0.08109 / 0.03144, 0.07385 / 0.04512 / 0.02165 |

Every Sod and collision error is lower than with the defaults on every grid.
Collision refines by 0.840 / 0.802 / 0.674 and 0.382 / 0.336 / 0.456; Sod and
collision pass. Separation has no compression in its window and reproduces the
defaults, so the gate still stops at the separation N256 velocity bound (against the
exact solution then; 0.0759 against the resting-slab reference now). The saved capture passes
`review_mac_riemann.py --partial` (124,191 checks). The collision margin is small and
phase-dependent (next paragraph); the same law with donor-cell momentum gives 0.866.

Variants tried before choosing this form (probe runs, not tracked), N64→N128
collision density ratio and N64 / N128 / N256 density L1:

| Variant | Ratio | Density L1 |
| --- | --- | --- |
| Limiter × both terms (`1 − φ`), heat 2 | 0.746 | 0.0498 / 0.0372 / 0.0150 |
| Limiter × both terms, matched heat `(1 − φ)(v + a)` | 0.890 | 0.0364 / 0.0324 / 0.0119 |
| Limiter × linear term only, heat 2 | 0.828 | 0.0440 / 0.0364 / 0.0150 |
| Limiter × linear term only, heat linear term limited | 0.849 | 0.0420 / 0.0357 / 0.0144 |
| LULESH form (`1 − φ`, `1 − φ²`), heat 2 | 0.750 | 0.0510 / 0.0383 / 0.0152 |
| **LULESH form, matched heat (modes 4/4)** | **0.840** | **0.0418 / 0.0351 / 0.0134** |
| Quadratic coefficient 0.3, limiter × both, matched heat (coefficient change, not adopted) | 1.102 | 0.0206 / 0.0227 / 0.0111 |

**Collision ratio phase sensitivity.** The collision is self-similar in grid units
(dt ∝ h), so N64, N128 and N256 are the same discrete problem after k = 46, 91 and
182 steps, and the density L1 is `S(k) / (N · window)` with S the summed cell error in
density × cells. A probe that advances N256 in 182 chunks (this perturbs the final
partial steps) shows S oscillating with a period of about 13.6 steps, one cell of
shock travel: for the defaults it swings between about 0.7 and 1.6 per shock with
minima near k = 32, 46, 59, …; k = 46 lies on a minimum and k = 91 on a rising flank.
The N64→N128 ratio is `S(91) / (2 S(46))`, so a ratio below 0.85 needs
`S(91)/S(46) < 1.7` while every tested MAC regularization has a late max/min of 1.9–3.1
(defaults 2.1, 2 / 4 / 3 / 2 2.1, modes 4 2.5, quadratic coefficient 0.3 3.1). Sharper
shocks lower S everywhere but oscillate more, so pass or fail flips with small
perturbations (modes 4: 0.840 in the gate, 0.935 in the chunked run). The classical
MUSCL-Hancock reference has an almost phase-independent S (0.47 / 0.49 / 0.57).
A robust pass needs a shock capture whose cut-cell average tracks the exact one.

### Initialization: the resting centre face

This analysis preceded the 2026-10-19 decision to judge separation against the
[resting-slab reference](#resting-slab-separation-reference). The gate (and `review_mac_riemann.py`, "Source face momentum") fixes the centre face at
J = 0 and every other interior face at its side's velocity. Read with the MAC's own
dual-cell kinetic energy, that state is the Riemann data plus a slab of width h at
rest at the centre, with ½ m (4 c₀)² less kinetic energy than the Riemann data per
cell of slab mass m (4.5 cells' worth of internal energy). An independent
binary64 MUSCL-Hancock solver (minmod, exact Riemann fluxes, the gate's γ, timestep
ratio and metrics) solved both readings 8–16 times finer than each gate grid. On the
gate grid itself it reproduces `RREFERENCE` (0.003848 / 26.53). These readings used a
scratch version of the solver with Riemann and slab starts and pressure from mean
conserved variables; the tracked `ma_riemann_slab_reference` runs only the slab start
with the gate reference's metrics.

| Initial data, N256 metrics | Velocity L1 | Density / pressure L1 | Central density / pressure error |
| --- | ---: | ---: | ---: |
| Riemann data, 16× finer | 0.0160 | 0.0012 / 0.0011 | 1.10 / 22.2 (still converging) |
| Resting slab of width h, 8× finer | 0.1153 | 0.0134 / 0.0098 | 73.8 / 421 |
| Resting slab of width h, 16× finer | 0.1197 | 0.0138 / 0.0098 | 74.5 / 425 |
| Production defaults (MAC) | 0.0741 | 0.0179 / 0.0233 | 77.52 / 732.98 |

The slab's converged central-cell density, 0.02463 kg/m³, matches the defaults'
0.02458 kg/m³ in cell 127 (0.02561 in cell 128, which carries the defaults' small
left/right drift). The slab's N64 / N128 velocity L1 are 0.211 / 0.164. Because the
slab width is h, the problem is the same in grid units on every grid and refinement
cannot remove it. An exact solution of the gate's own initial data therefore fails the
N256 velocity bound and both central bounds; a scheme can pass them only by not
following its initial data. The collision slab error refines normally (density L1
0.0198 / 0.0103 / 0.0051).

The classical reference itself meets the central density bound only on the gated grid:
with the review script's own `reference_central_errors`, its central density / pressure
errors are 0.866 / 34.5 (N64), 0.781 / 3.27 (N128), 0.0038 / 26.53 (N256) and
1.173 / 62.2 (N512). Its N256 core over-empties to about 6.8e-5 kg/m³ near step 100 and
refills by back-flow, crossing the exact 3.26e-4 kg/m³ near step 182 at 377 K. Its
velocity L1 is 0.525 / 0.148 / 0.115 on N64 / N128 / N256, so it fails the 0.06 bound too.

With the discontinuity instead at the centre of cell n/2 (probe `shift=1`, compared with
the shifted exact solution; not a gate input) the MAC core empties: central relative
errors are 0.99–1.00 on all grids and velocity L1 is 0.825 / 0.467 / 0.122 (defaults),
which is the [vacuum-front](MAC_VACUUM.md) problem.

## Diagnosed causes

### Collision shock profile

Saved-profile analysis, without native reruns, located the collision errors:

- With quadratic compression, 99.35% (N64) and 91.43% (N128) of the pressure error
  lies within four cells of an exact shock.
- A logging-only rerun of the 1 / 1 / 1 / 0 configuration split the global entropy
  increase into compression half steps (64–66%), remap before heat (about 15%) and
  transport heat (19–21%); pressure work is at roundoff. The central fixed cell
  overshoots the exact star entropy, consistent with the shock-formation overheating
  studied by [Noh (1987)](https://www.sciencedirect.com/science/article/pii/002199918790074X).
  This motivated the [shock-heat regularization](MAC_HEAT.md#shock-heat-regularization).
- With heat mode 1, the compressed pressure profile alternates around the exact star
  state (cells 59–63 at about 559001, 587884, 554703, 583795 and 567020 Pa against
  571226 Pa). This motivated the pressure-sensed acoustic compression term.
- With the defaults, density error contributions (unshocked / shock-cut / compressed)
  are 0.017450 / 0.000367 / 0.025041 at N64 and 0.014680 / 0.014212 / 0.007803 at N128.
  The compressed region converges; the shock-cut contribution grows from a small
  coarse value. Central density errors are 0.39% / 0.66%.
- Material selector 2 repairs a real moving-centroid error, and the
  [affine remap diagnosis](MAC_MOMENTUM.md#affine-remap-diagnosis) found a real
  dual-geometry bias and an over-restrictive admission rule, repaired by momentum
  selector 3. Neither changed the collision ratios. Only coupling the compression
  stress into the pressure root (compression 3) passed collision.

### Separation

- With the defaults at N64, the two central cells heat from 336.9 K to about 386 K
  by t = T/8 while they expand, where the exact isentropic state is about 100 K.
  They are already 8× too dense; by t = T they are 190× too dense and 2,300× too
  high in pressure. The start-up error stays in the stationary core material, so
  refinement cannot dilute it. The centre face also drifts from exact symmetry,
  to −7.45 m/s at t = T, and the two central cells differ by 6%.
- The staggered initialization contributes. The centre face starts on the
  discontinuity with J = 0, the momentum-conserving average of ±4 c₀. Its dual
  therefore holds one cell of mass at rest, which the exact problem does not have.
  At N256 that mass per unit area is about 850 times the exact core's.
  - A diagnostic copy shifted the discontinuity half a cell so that no face starts
    on it, and compared against the shifted exact solution (not a gate run).
  - The core then over-empties instead: central density reaches about 1e-15 against
    3.3e-4 kg/m³. The faces beside it keep about ±1,050 m/s at t = T/4, where the
    exact flow is at ±6 m/s, because nearly empty cells have almost no EOS pressure
    left to decelerate them.
  - MUSCL-Hancock gets the core within 0.4%. Its centre-face Riemann flux supplies
    the exact low star pressure, which a cell-pressure MAC face does not see.
  - Passing the central density bound needs pressure on strongly expanding,
    nearly empty faces. That is shared with the [vacuum front](MAC_VACUUM.md).
- In the defaults' collision profiles, the shocks sit within 0.03 cells of exact,
  measured by mass. They are about 3–4 cells wide on every grid. The exact shock
  falls 0.62 / 0.23 / 0.46 of the way through its cell at N64 / N128 / N256, and the
  smeared-shock error in cell units follows this (0.67 / 1.00 / 0.77). Post-shock
  ringing of about ±0.04 in density covers about ten cells at N128. Together these
  set the first refinement ratio of 0.856.
- The classical MUSCL-Hancock reference has collision density L1 of
  0.0293 / 0.0154 / 0.0089, against the defaults' 0.0429 / 0.0367 / 0.0151. The
  defaults' wider shocks are a real accuracy gap at N128, not only a refinement
  artefact.
- Selector and coefficient sweeps on collision do not close it. They used a
  diagnostic copy of the test that reports instead of stopping, plus local
  coefficient builds; nothing is tracked.

  | Change from the defaults | N64→N128 ρ / p | Absolute L1 effect |
  | --- | --- | --- |
  | Donor-cell momentum (1 / 0 / 2 / 2) | 0.786 / 0.750 (pass) | Worse at every grid; the larger N64 error makes the ratio |
  | Donor-cell material (0 / 1 / 2 / 2) | 0.740 / 0.694 (pass) | Worse at every grid; same artefact |
  | Coupled compression (1 / 1 / 3 / 2) | 0.848 / 0.820 (pass) | Unchanged within 1%; not in the production kernel |
  | Linear speed c/4, c/8, 0 | 0.869–0.890 / 0.840–0.906 | N64 improves more than N128 |
  | Quadratic coefficient ½ (linear term unchanged) | 0.821 / **1.012** | Density sharpens; pressure error moves to N128 |

  Existing shock-capture knobs trade density against pressure error. Passing
  without loss needs a structurally sharper, ring-free shock capture.
- With momentum selector 3, the central N256 gas reached 119.42 / 118.74 K against
  13.65 K, and most velocity L1 error lay in the expansion fans. An N64 replay showed
  the first remap cancelling every outward momentum correction at the two stationary
  central dual faces: they had no transported kinetic energy to fund the carried
  energy flux. Pressure work cooled the central material from 336.92 to 313.27 K and
  the remap then heated it to 376.39 K. Selector 4 removes this restriction.
- With selector 4, the central cells are still too dense and too hot. Their specific
  entropy is 365–408 J/(kg K) below the uniform initial value while total entropy
  rises. In an N64 replay all 184 sampled central reconstructions have limiter zero
  (flat parcels). At substep 5, signed-work deposition in cell 31 lowers specific
  entropy from −965.92363 to −970.30012 J/(kg K), below the initial −970.22915;
  reconstruction and heat do not contribute there.

### Curved-deformation pressure and remap convention

The pressure step moves material face boundaries with the face velocity `J/I`. The
swept momentum remapper (selectors 3 and 4) reconstructs velocities at dual-volume
centroids. Affine-exact controls make these positions coincide; curved deformation
separates them. [Selector 5's one-dimensional conversion](MAC_NODAL_CONVERSION.md)
now repairs the interior point-velocity oracle while retaining the original J/K
account. It does not supply the general 2-D conversion or pass strong separation;
the production defect remains open.

`ma_mac_curved_nodal_diagnostic` reproduces it in one dimension. For unit density,
moved faces are at `x_f = f + q_f`, source cell widths are `1 + q_(f+1) − q_f`, the
half-cell inertia is `I_f = 1 + (q_(f+1) − q_(f−1))/2`, and the dual-volume centroid is

```text
mean_x_f = f + (q_(f-1) + 2 q_f + q_(f+1))/4.
```

Each case prescribes `u(x) = 1/4 + x/8`. Convention 0 supplies `J_f = I_f u(x_f)`;
convention 1 supplies `I_f u(mean_x_f)`; both are compared with the fixed-grid
reference `J_f = u(f)`. The 16 controls cover both axes, both sweep signs, affine and
curved geometry and both conventions, sampling faces 4–6. Curved sweeps use
`q_f = direction * ((f-1)/64 + (f-1)^2/512)` with fixed outer faces, so
`mean_x_f − x_f = direction/1024` and the point-source error is exactly
`−direction/8192` in velocity. The 128-epsilon actual-momentum account is not
loosened. Windows and Linux agree: 24 of 48 face-value readings fail (all curved),
all 48 dual-mean readings pass, every candidate keeps correction fraction 1 and every
energy guard passes. **The executable and reader exit 1 by design** while the
face-value criterion fails.

A probe of the original N64 separation input (selectors 2 / 4 / 3) showed the same
split on the first pressure step: pressure displacement and remapper centroid differ
by up to 0.0499 cell widths at the interface faces, and the dual-mean momentum of a
piecewise-linear face-velocity interpolant differs from the stored face value by up
to 0.0456 kg m/s. Feeding the alternate dual-mean source changes J and K, so its
passing component readings are not a repair.

## Private remap experiments

Several remap variants were tried in private builds; none is in the tracked code.
Each kept the original inputs, timestep and physical guards. Separation N64 results
(46 steps, no retries; coarse only):

| Variant | Density L1 | Pressure L1 | Velocity L1 | Central density L/R (kg/m³) |
| --- | ---: | ---: | ---: | ---: |
| Tracked selector 4 | 0.0554024 | 0.0505242 | 0.185070 | 0.0389251 / 0.0390314 |
| Remove scalar neighbour envelope | 0.0474145 | 0.0412543 | 0.143765 | 0.0364212 / 0.0363848 |
| Carry cell kinetic energy with material | 0.0682200 | 0.135432 | 0.293706 | 0.0439336 / 0.0439336 |
| Local pooled mixing-energy budget | 0.0589832 | 0.0581090 | 0.171550 | 0.0538390 / 0.0319121 |
| Pooled budget, quarter/half/quarter traversal | 0.0566224 | 0.0552036 | 0.176857 | 0.0367862 / 0.0507058 |
| Symmetric traversal + entropy-admitted reconstruction | 0.0493508 | 0.0476480 | 0.178860 | |
| Quadratic reconstruction, global entropy reference | 0.0481518 | 0.0458518 | 0.167497 | |
| Full source mean / target point conversion | 0.0773135 | 0.0717195 | 0.136296 | |
| Same conversion, dual-carried representation work | 0.0804294 | 0.0734279 | 0.133987 | |

Findings:

- Removing the envelope improves L1 but lets local specific entropy fall below the
  initial value earlier, so it is not accepted. Carrying cell kinetic energy warms the
  core to 436 K and worsens every error.
- The pooled budget funds a correction only from the two nodes' existing mixing
  remainders. Its naive endpoint form fails in binary64 (no representable flux in the
  admissible interval); the stable form chooses left work within `[−Hl, Wtotal + Hr]`
  and takes right work as the conservative remainder. It still leaves a large
  left/right imbalance, and a local entropy drop then enters through scalar
  reconstruction. Averaging two opposite traversals removes the imbalance but not the
  error.
- Entropy-admitted reconstruction (limiting parcels against the donor-stencil minimum
  specific entropy, after [Zhang and Shu](https://math.umd.edu/~tadmor/references/files/Zhang%20%26%20Shu%20Minimum%20entropy%20NM2012.pdf))
  at N256 gives 0.0136829 / 0.0141810 / 0.0869008 and central errors 31.55 / 131.5.
  0.0812 of the velocity error lies in the expansion fans, and the faces beside the
  centre move at about ±35.7 m/s where the exact velocity is zero.
- Quadratic reconstruction at N256 first stopped on a false heat entropy rejection
  ([fixed](MAC_HEAT.md#entropy-measurement)), then gave 0.0121233 / 0.0120709 /
  0.0814362 and central errors 32.64 / 135.97, with the core at 0.0110 kg/m³ and 55.6 K.
- The full source/target conversion interpolates half-cell velocity with each
  source cell's constant density, using pair impulse `m (ur-ul)/8`, then solves the
  target mass-weighted relation for point velocities. Both transformations retain
  original J and K+U through compatible work. Carrying that work with material gives
  N64 central density/pressure errors 150.19 / 1401.46; carrying it with the dual
  packets gives 155.59 / 1294.54. Native and saved-state conservation/entropy checks
  pass, but these coarse results worsen the core and do not establish a correct
  general conversion. No finer-grid or full-gate claim is made. Sources, raw data
  and derived-review provenance are under `local/remap-preparation/consistent-nodal/`
  and `dual-work-nodal/`; neither variant is tracked.

Better scalar reconstruction lowers the density and pressure L1 errors but leaves the
velocity and central criteria failing.

## Next step

1. Define a conservative conversion of momentum and kinetic energy between the
   pressure step's face-value velocity and the remapper's dual-volume-mean
   representation, keeping the original physical source. Make the curved-nodal
   diagnostic pass without loosening its momentum account.
2. Then revisit expansion-fan transport: scalar reconstruction near the separating
   core and the order and entropy of signed-work deposition.
3. Rerun the complete native prerequisite suite, then this benchmark once with
   unchanged criteria. Promote a selector to default only after the full gate (and
   the vacuum gate) passes.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ma_mac_riemann_tests ma_mac_curved_nodal_diagnostic
build/ma_mac_riemann_tests > riemann.stdout 2> riemann.stderr
python3 native/tests/review_mac_riemann.py riemann.stdout            # add --partial if it stopped early (about 40 s with separation N256)
build/ma_mac_curved_nodal_diagnostic > curved.stdout 2> curved.stderr
python3 native/tests/review_mac_curved_nodal.py curved.stdout
# Opt-in selectors (material,momentum,compression,heat); review with --partial if it stops
build/ma_mac_riemann_tests --selectors 1,1,4,4 > monotone.stdout 2> monotone.stderr
# Diagnostics: per-case errors without stopping, profiles, chunked histories
cmake --build build --config Release --target ma_mac_riemann_probe ma_riemann_slab_reference
build/ma_mac_riemann_probe 1 256 comp=4 heat=4 chunks=182   # collision S(k) (SK lines)
build/ma_mac_riemann_probe 2 256 profile=1                  # separation profile
build/ma_mac_riemann_probe 2 256 slab=8                      # separation against the reference
build/ma_riemann_slab_reference 256 2 4 8 16 32               # reference convergence study
```

Visual Studio places executables under `build/Release/`. Without `--selectors` the
tracked Riemann test uses the default `MacClosedSettings`; the option changes only
the four selectors, never inputs, schedules or bounds. The curved-nodal reader exits 2 for
invalid data and reports `evidence_valid` separately from `nodal_gate_passed`. It
checks geometry, reference momenta, stored operands, match flags and completeness,
and rejects five saved-data corruptions; its energy evidence uses the native guard.
