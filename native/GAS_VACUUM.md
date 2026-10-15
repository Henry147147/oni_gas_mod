# Experimental gas/vacuum consistency and failed front gate

The full gas-to-vacuum rarefaction gate remains **false**. This is a dormant
native `FluidWorld::step` experiment. It does not activate ONI ownership, change
`GasWorld`/C ABI, or establish a performance, shock, cavitation or 2-D result.

## Reproducible physical problem

The domain is [0,8] m, cross-section 1 m2, with a rigid left boundary. Initially
gas occupies [0,2] m at density 1 kg/m3, pressure 100000 Pa and velocity 0; the
remaining cells are exactly empty. Mass fractions are oxygen 0.22, nitrogen 0.78
and an additional positive helium trace 1e-28. Constant catalog Cv/molar masses
give gamma=1.39844668103111. The explicitly versioned test EOS uses each
species' caloric reference and has no pressure/density floor or seed gas.

The exact ideal-gas fan has head speed `u0-c0` and vacuum-front speed
`u0+2c0/(gamma-1)`; velocity in the vacuum is undefined. These relations come
from the [primary Clawpack Euler chapter](https://www.clawpack.org/riemann_book/html/Euler.html#Riemann-problems-with-vacuum).
At t=0.5/c0 the head is 1.5 m and front 4.50974609052378 m; the front speed is
5.01949218104756 c0. The test independently integrates the analytic fan over
fixed cells, including its mass, momentum, pressure and kinetic energy. Its
integrals recover initial total mass/energy and momentum p0*A*t. These checks
validate the oracle separately from the implementation under test.

Density and pressure L1 errors use finite-cell integrals normalized by the
initial density/pressure and exact fan width. Velocity error is exact-mass
weighted over complete cells inside the fan and at least 0.5*c0*t before its
front, then normalized by that sampled mass and c0. Front position uses the
last strictly positive occupied interval, without a density threshold. Recent
front speed uses its displacement between 0.8t and t. Separate tip velocity,
density and pressure are reported; no velocity is assigned to true vacuum.

## Failed baseline and bounded correction

Commit `24dc2c7` reconstructed nodes and overwrote cell momentum with their
complete average on every call. At equal cell masses its dt-to-zero map is
`vnew[i]=(v[i-1]+2v[i]+v[i+1])/4`. A quarter-grid Fourier mode therefore tends
to half amplitude even for arbitrarily small dt. Conservation passes because
the lost kinetic energy becomes heat. The retained failed Windows probe has
source SHA256 `e1846c31d70c69c6dd77098cc00bcf560cc525a8d19b523d8b1ed79754793cc0`
and executable SHA256 `55e16b46e2f8c6dda6c33b880c5fdb6fb8ef2eae3c20d9de09698f38a273bb97`.
Source, executable, logs and source/library manifests are preserved in ignored
`local/fluid-gas-vacuum-validation/baseline-24dc2c7`.

The current bounded correction retains cell momentum and transfers only force
increments, with compatible nonnegative incremental kinetic heat. Its
derivation and endpoint representability policy are in
[FREE_SURFACES.md](FREE_SURFACES.md). The actual-world Fourier amplitudes at
dt=1e-7,1e-9,1e-11 s are 0.999999949456,0.999999999500,0.999999999995. This
fixes that consistency defect. It does not retain a subcell expansion moment:
a symmetric isolated-cell kick converts its expansion kinetic variance to
heat, and its repeated dt-to-zero behavior is not a resolved rarefaction.

With dx=8/N and dt_max=0.025*dx/c0, current Windows results are:

| N | front position error (m) | recent speed/c0 | density L1 | pressure L1 | velocity L1/c0 |
|---:|---:|---:|---:|---:|---:|
|128|1.464869522|2.876942580|0.01423109|0.01983497|0.10229535|
|256|1.121103256|3.712409507|0.00758895|0.01168756|0.05409996|
|512|0.705741615|4.737909550|0.00484878|0.00685868|0.02897710|
|1024|0.222467408|5.873964601|0.00314806|0.00396263|0.01686020|
|2048|0.313596028|7.063314637|0.00196876|0.00224715|0.00992466|

The 2048 probe is frozen separately before the final zero-subnormal-remainder
guard found by independent review. That guard does not take its failing branch
for this probe's ordinary 1e-28 trace. The exact pre-guard source/binary manifest
is retained; final-source validation separately repeats the 1024 front failure.

The speed error increases at 1024, followed by a worsening position error and
larger speed overshoot at 2048, despite improving interior errors. These results
fail a joint front-position/front-speed refinement gate.
They must not be reported as complete gas/vacuum convergence. At fixed N64,
halving dt_max from 0.05 to 0.025 to 0.0125 dx/c0 gives conservative state-distance
changes 0.00590435 and 0.00321868; this demonstrates approach to a fixed-grid
limit, not that this limit is the correct vacuum-front solution.

At dt->0 the corrected mechanics reduces to `dV/dt=D R v` and
`M dv/dt=R^T D^T p`, where R mass-weights cell velocities to nodes. For equal
masses this centered operator admits an alternating interior pressure mode.
This is a discrete algebraic limitation; attributing the observed tip-speed
overshoot to it is a hypothesis, not a verified causal result. The proposed
state extension is reviewed separately in [STAGGERED_1D_DESIGN.md](STAGGERED_1D_DESIGN.md).

## Maintained checks and evidence

`ma_gas_vacuum_tests` / CTest `gas_vacuum_consistency` checks the independent
analytic integrals, bounded N32/64/128 interior trends, fixed-grid N64 timestep
consistency, each constituent/trace, total energy, entropy, exact exterior
vacuum and wall reaction. Its successful exit explicitly does not pass the
full front gate. `--case N Courant [profile.csv]` records a single case;
`--refine Nmax` also requires decreasing position and recent-speed errors on
its last three doubling grids and returns 2 when that observed trend fails.

Each case reports actual accumulated constituent/energy residuals, signed and
absolute sums of represented-geometry balance errors, maximum scalar-addition
and root-arithmetic roundoff, endpoint-relative nonlinear residual, and
unresolved U/volume/position update counts. Nonzero representability errors
are not labeled exact conservation. The 24 free-surface groups recheck the
prior liquid mechanics and rollback cases after this heat change. The
independent 26-group thermodynamic project also checks the equivalent stable
entropy logarithms at ordinary, extreme and denormal-trace scales.

The ignored `local/fluid-gas-vacuum-validation` directory contains source and
binary manifests, compiler/configuration logs, failed baseline evidence,
profiles and cross-compiler/sanitizer results. Test timings there are not
performance claims. No production ownership or persistence format is changed.
