# Gas-to-vacuum front gate (open, false)

The gas-to-vacuum rarefaction gate is **false**. Two dormant 1-D native
experiments are measured against it: the `FluidWorld` occupied 1-D mode and the
retained material-node state. Neither is used by ONI, `GasWorld`, the C ABI or
`MacClosedWorld`. No shock, cavitation, 2-D or performance result follows from
them.

The MAC owner now has separate [empty-cell inventory support](MAC_VACUUM.md).
Its cell-average front diagnostic also fails: positive tails fill the N32 domain,
and finer cases stop on a verified stored-volume entropy violation. It does not
replace or pass this gate.

## Problem

- Domain [0,8] m, cross-section 1 m2, rigid left wall. Gas initially fills
  [0,2] m at density 1 kg/m3, pressure 100000 Pa and zero velocity; the other
  cells are exactly empty.
- Mass fractions: oxygen 0.22, nitrogen 0.78, plus a positive helium trace
  1e-28. Constant catalogue Cv and molar masses give gamma = 1.39844668103111.
  The versioned test EOS has no pressure or density floor and no seed gas.
- The exact ideal-gas fan has head speed `u0-c0` and vacuum-front speed
  `u0+2c0/(gamma-1)`; velocity in vacuum is undefined
  ([Clawpack Euler chapter](https://www.clawpack.org/riemann_book/html/Euler.html#Riemann-problems-with-vacuum)).
  At t = 0.5/c0 the head is at 1.5 m and the front at 4.50974609052378 m; the
  front speed is 5.01949218104756 c0.
- The tests integrate the analytic fan over the fixed cells; the integrals
  recover initial mass and energy and momentum p0*A*t, validating the oracle
  independently of the solver.

## Measures and acceptance

Density and pressure L1 errors are cell integrals normalized by the initial
value and the exact fan width. Velocity error is mass-weighted over complete
cells inside the fan and at least 0.5*c0*t behind the front, normalized by c0.
Front position is the end of the last strictly positive occupied interval (no
density threshold); recent front speed is its displacement between 0.8t and t.
True vacuum gets no velocity.

**Acceptance.** Front position and recent front speed must converge to the
exact values under grid and timestep refinement at fixed Courant number,
together with the interior errors and conservation. The automated checks
(`--refine`, `--front-gate`) test only that both front errors decrease strictly
over the last three doubling grids; no absolute tolerance has been fixed. A
decreasing trend with large remaining error does not pass the gate.

## `FluidWorld` occupied 1-D mode: fails

The mode's mechanics are in [FLUID_WORLD.md](FLUID_WORLD.md#occupied-1-d-mode).
With dx = 8/N and dt_max = 0.025*dx/c0 (Windows):

| N | front position error (m) | recent speed/c0 | density L1 | pressure L1 | velocity L1/c0 |
|---:|---:|---:|---:|---:|---:|
|128|1.464869522|2.876942580|0.01423109|0.01983497|0.10229535|
|256|1.121103256|3.712409507|0.00758895|0.01168756|0.05409996|
|512|0.705741615|4.737909550|0.00484878|0.00685868|0.02897710|
|1024|0.222467408|5.873964601|0.00314806|0.00396263|0.01686020|
|2048|0.313596028|7.063314637|0.00196876|0.00224715|0.00992466|

Interior errors improve, but the front speed overshoots from N1024 on and the
front position worsens at N2048. At fixed N64, halving dt_max from 0.05 to 0.025
to 0.0125 dx/c0 changes the conservative state by 0.00590435 and 0.00321868: the
scheme approaches a fixed-grid limit, which is not shown to be the correct
front.

Known limitation: as dt tends to zero the mechanics reduce to `dV/dt = D R v`
and `M dv/dt = R^T D^T p`, where R mass-weights cell velocities to nodes. With
equal masses this centered operator admits an alternating interior pressure
mode, and an isolated symmetric cell kick turns its expansion kinetic variance
into heat. That this causes the tip overshoot is a hypothesis, not verified.
Retaining nodal momentum (next section) is the proposed fix.

## Retained nodal replacement path

`native/tests/retained_gas_vacuum_tests.cpp` advances the retained material-node
state of [OCCUPIED_STAGGERED.md](OCCUPIED_STAGGERED.md) through the same problem,
EOS, trace, time and Courant number. Fixed-grid mass, linear-velocity and
pressure integrals are read-only samples; they never reset the material mesh.
The driver selects `retain_local_geometry`, so cell volumes and gravity work use
both parts of each local coordinate (see
[the storage contract](OCCUPIED_STAGGERED.md#coordinates-and-numerical-domain)).

Windows/MSVC and WSL Linux/GNU agree to the printed precision:

| Fixed probe grid N | Front error (m) | Recent speed/c0 | Density L1 | Pressure L1 | Velocity L1/c0 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 512 | 1.206040 | 3.015870 | 0.00585337 | 0.00349883 | 0.0192492 |
| 1024 | 1.061948 | 3.248432 | 0.00321100 | 0.00175361 | 0.00976444 |
| 2048 | 0.937363 | 3.450139 | 0.00181847 | 0.00092327 | 0.00496433 |

Front position and recent-speed errors both decrease strictly, but the finest
front is still 0.937 m short and 1.569 c0 too slow (exact 5.019492 c0). Its last
material cell has density about 0.00935 kg/m3 and pressure 145.33 Pa. This is a
bounded trend, **not** a pass; it says nothing about Eulerian remap, 2-D or
general free surfaces. Scalar U and momentum rounding and unresolved-update
counts are reported, not claimed exact.

Pitfalls met on the way: scalar node coordinates lost local volume when added
to large positions (hence paired coordinates), and re-serializing EOS profile
metadata on every identity query made the N2048 run time out (hence the cached
identity).

## Reproduce

Integrated `FluidWorld` mode (`ma_gas_vacuum_tests`, CTest
`gas_vacuum_consistency`):

```sh
ctest --test-dir build -R gas_vacuum_consistency --output-on-failure
build/ma_gas_vacuum_tests --case 64 .025 [profile.csv]   # one case
build/ma_gas_vacuum_tests --refine 2048                  # front gate, N32..N2048
```

The CTest run checks the analytic integrals, bounded N32/64/128 interior
trends, fixed-grid N64 timestep consistency, each constituent and the trace,
total energy, entropy, exact exterior vacuum and wall reaction. It does **not**
test the front. `--refine Nmax` requires decreasing position and recent-speed
errors on its last three doubling grids and returns 2 when that fails, which it
currently does. Each case reports accumulated constituent/energy residuals,
represented-geometry balance errors, scalar-addition and root roundoff,
endpoint-relative nonlinear residual and unresolved update counts.

Retained path (explicit targets, not in CTest; `ma_retained_geometry_tests`
needs `MA_BUILD_STAGGERED_EXPERIMENTS=ON` because it checks that the scalar
remap consumers reject paired coordinates):

```sh
cmake --build build --target ma_retained_gas_vacuum_tests ma_retained_geometry_tests
build/ma_retained_gas_vacuum_tests --case 32 .025 profile.csv > case.stdout
build/ma_retained_gas_vacuum_tests --front-gate > front.stdout
build/ma_retained_geometry_tests > geometry.stdout
python3 native/tests/review_retained_gas_vacuum.py --profile profile.csv --stdout case.stdout --output case-review
python3 native/tests/review_retained_vacuum_front.py --stdout front.stdout --output front-review
python3 native/tests/review_retained_geometry.py --stdout geometry.stdout --output geometry-review
```

The reviewers read saved output only. `review_retained_gas_vacuum.py` checks the
32 coarse analytic samples and closed analytic mass, momentum and energy with an
80-digit reference; `review_retained_vacuum_front.py` checks the three-grid
trend; `review_retained_geometry.py` checks 128 widths, 127 shared-node
identities, a short rebased interval and nine-node gravity work. Corrupted
data and a nonrefining speed record reject. The `--front-gate` run (N512, 1024
and 2048) takes a few minutes.
