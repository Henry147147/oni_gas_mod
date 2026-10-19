# Strong-flow accuracy investigation

This is the accuracy gate for [`MacClosedWorld`](MAC_SOLVER.md) on strong ideal-gas
shocks and rarefactions. It is **failed**. With the production defaults, collision
density refinement fails. The best opt-in selector combination passes Sod and
collision, but separation still fails the finest-grid velocity bound and the central
density/pressure bounds. The latest diagnosis localizes a convention mismatch between
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
- At N256 every normalized error is below 0.06.
- Separation at N256: both central cells' density and pressure errors are below half
  their exact cell averages, so the low-pressure core cannot hide inside a small
  domain-averaged error.
- Every nitrogen, helium and passive inventory stays positive. Total entropy may
  decrease only within 128 binary64 epsilons of Σ|U|/T over the initial and final
  states.

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
bold. Sod passed in every row.

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
python3 native/tests/review_mac_riemann.py riemann.stdout            # add --partial if it stopped early
build/ma_mac_curved_nodal_diagnostic > curved.stdout 2> curved.stderr
python3 native/tests/review_mac_curved_nodal.py curved.stdout
```

Visual Studio places executables under `build/Release/`. The tracked Riemann test
uses the default `MacClosedSettings`; to test an opt-in combination, change only the
selector assignments in `run()` in a local build. The curved-nodal reader exits 2 for
invalid data and reports `evidence_valid` separately from `nodal_gate_passed`. It
checks geometry, reference momenta, stored operands, match flags and completeness,
and rejects five saved-data corruptions; its energy evidence uses the native guard.
