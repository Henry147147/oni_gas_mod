# Two-moment pressure integration

`MacPressureIntegration::two_moment` (saved selector 1) and
`two_moment_coupled` (2) are opt-in pressure integrations in the production
kernel. They share the same temporal work and flux equations and provide a
building block for larger calm steps. A separate
[pressure-window component](MAC_PRESSURE_WINDOWS.md) streams prescribed fine
pressure histories with the same local constitutive work. Automatic accuracy
selection and full local subcycling remain open. Existing owners
keep `discrete_gradient_midpoint`, their original schedule and checkpoint bytes.
The benchmark `balanced` profile is unchanged.

## Pressure work and shared sweeps

This uses the polynomial-path/discrete-line-integral idea described by
[Brugnano, Iavernaro and Trigiante](https://arxiv.org/abs/0910.3621).
The following constitutive coefficient and solver reduction are specific to
this implementation; the paper does not establish full-MAC acceptance.

For normalized time `tau`, the material-volume path and pressure trace are

```text
V(tau) = V0 + a*tau + sqrt(3)*b*(tau²-tau)
p(tau) = p0 + X + sqrt(3)*(2*tau-1)*Y
k = sqrt(3)/6
```

Four Gauss nodes evaluate anchored EOS pressure increments. One coefficient
is formed from the anchored endpoint work `dW(a)`, so
`a*X + b*Y = a*dW(a)` before storage rounding. The coefficient with the larger
volume factor is formed this way, avoiding division by a small ratio. No stored
inventory is repaired. `p-p0 = dW(dV)+dV*dW'(dV)` avoids subtraction of absolute
pressures, including the gas series branch.

Each movable face has one mean force `F0` and first moment `F1`, shared by both
cells. Gravity contributes only to `F0`.

```text
J1 = J0 + dt*F0
Jbar = (J0+J1)/2 - k*dt*F1
q = dt*A*Jbar/I
a = divergence(q)
b = k*dt²*A*divergence(F0/I)
```

The same integrated `q` drives both material endpoints. The constitutive work
identity and adjoint pressure/divergence pair reconcile pressure work with
`K(J1)-K(J0)`. Gravity work uses `dt*body_force*Jbar/I`, then the existing
potential-flux adjustment. Fixed faces contribute wall impulse. The interval
retains its original 128-epsilon conservation and entropy checks, retry budget
and atomic publication. Flagged sweeps retain range through parcel formation.
The surrounding remap and split processes retain their order; this is not a
claim of fourth-order accuracy for the full solver.

## Linear solve and continuation

For anchor inverse compliance `C` and positive shared pressure graph `H`, set
`Z=dt²*H*C/12`. The anchor-linearized moment matrix is
`[[I+2Z,-sqrt(3)Z],[sqrt(3)Z,I-Z]]`. Its Schur polynomial `I+Z+Z²` is positive
in the `C`-weighted inner product. Two applications of the same fixed symmetric
geometric V-cycle precondition it through `(I+Z)²`; the scalar polynomial ratio
lies in `[3/4,1]`. Small grids or explicit Jacobi use diagonal preconditioning.
Nonlinear residuals retain the 16-epsilon volume tolerance and bounded line
search. Fixed blocks and reduction order are independent of worker count.
Derived moment buffers are counted in retained workspace, without saved history.

The coupled implementation diagonalizes the same real moment block with
`lambda=(1+i*sqrt(3))/2`, `beta=-sqrt(3)/2+i/2`. For anchor compliance `comp=1/C`
and the already time-scaled graph `H`, it solves

```text
(comp + lambda*H) * z = ra + beta*rb
 da = comp * (Re(z) + sqrt(3)*Im(z))
 db = 2*comp*Im(z)
```

The complex symmetric matrix has positive real part. A geometric V-cycle uses
the existing immutable topology, transpose transfers, complex coarse LU and
reversed .5/1.5 smoothing stages. COCG uses transpose dot products, following
[the preconditioned algorithm of Bertolazzi and Frego](https://arxiv.org/pdf/1405.6297).
Fixed 256-element blocks and ordered double-double block reductions keep the
result independent of worker count. A 512-epsilon dot-product uncertainty
screen covers block accumulation; this does not change the interval's
128-epsilon account. The actual matrix residual certifies convergence; a
residual replacement restarts the direction. An undecidable pivot, failed
preparation or exhausted linear solve requests the retained real Schur method
for the same temporal integration. Explicit Jacobi also uses that real path.
Coefficients refresh once per pressure root; topology is keyed by exact active
geometry. No Krylov history is saved or reused across roots. The complex cache
and its topology are included in retained workspace accounting.

The coupled range path can reconstruct a normal final sweep after flagged
intermediates, provided the actual shared sweeps still meet the original
16-epsilon geometric closure. Otherwise it rejects and refines the interval.
It never repairs an endpoint or changes temporal method.

Both selections require numerical kernel 2. Selector 1 writes checkpoint
version 7; selector 2 writes version 8. The wire layouts share one selector
after the diffusion mode/multiplier, with a canonical value for each version.
Versions 2–7 retain their bytes and continuation; versions 2–6 restore midpoint.
Unknown/noncanonical selectors
reject. A moment owner also rejects a request for an unsupported selector or
range path that would require legacy reference execution: it cannot silently
change its temporal method. Those cases need implementation before adoption
as a general production default.

## Current validation

`ma_mac_kernel_moment_tests` checks five 0.1-second intervals against fine
production-midpoint references; a short fine-step control also checks the strict
stage oracle. Refinement at the smallest and largest candidate steps resolves
the reference within 0.1% of the pressure disturbance. The original bounds stay
0.1 K and 1% for disturbance-relative pressure, O2 partial pressure, constituents
and passives. No rejected attempt may hide smaller candidate steps.

Both implementations pass the first five schedules below; the last is an
additional coupled control. Errors agree at the displayed precision.

| Wave | Candidate dt, s | Reference dt, s | Pressure error / 20 Pa | Maximum dT, K |
| --- | ---: | ---: | ---: | ---: |
| 16×16 | .004 | .00005 | .0073422 | .00012554 |
| 32×2 | .008 | .0001 | .0017038 | .00002913 |
| 64×2 | .02 | .0001 | .0049429 | .00008450 |
| 256×2 | .1 | .0001 | .0027356 | .00004677 |
| 512×2 | .1 | .0001 | .00008250 | .000001411 |
| 500×2, coupled | .1 | .0001 | .00008973 | .000001534 |

Short gravity, room, moving gas/liquid, compressed-liquid and empty-cell controls
retain their original comparison bounds. The 33,389-cell controls retain exact
one/twelve-worker state, audit counts and ordinary continuation. Budget exhaustion
publishes no state or accounts. Physical/100× diffusion with nonuniform composition
and automatic/Jacobi settings retain ordinary continuation. The coupled
4,096-cell subnormal front reaches 256 initially empty cells with 13,480 audited
transfer roundings; version 7 retains its original 14,138 count.

`ma_mac_kernel_moment_multigrid_tests` checks an independent real block equation,
odd/periodic grids, walls, disconnected rooms and 2,113 isolated cells without a
quadratic coarse matrix. It checks cold/refreshed/invalid coefficients,
unconverged and undecidable solves without published corrections, twelve workers,
concurrent heated owners and same-size geometry changes. A flagged intermediate
with a normal final sweep retains the strict endpoint in one attempt, with the
original geometric closure.

All 52 Release CTests pass with the ONI backend and warnings as errors. Twelve
suites pass Debug ASan/UBSan and MinGW 13/Wine 10; eleven pass ThreadSanitizer,
using the moment suites’ twelve-worker checkpoint controls there. The saved endpoint reader
passes 80,709 checks for version 7 and 114,726 for version 8, rejecting fourteen
altered captures for each on Release, ASan/UBSan and MinGW/Wine. It checks EOS,
reference resolution, all endpoint errors and admitted schedules without launching
native code. All 22 saved reviews pass across these routes.

The [current component comparison](MAC_KERNEL.md#validation) retains prior
exits and physical records. Six historical images, sampled version-6 waves,
version-7 images and cross-commit version-7 continuation retain their bytes.
The required reference wave SHA is unchanged.
Version-8 images continue byte-identically in fresh Linux/Wine processes, twice
on each platform in both source directions. Wine is Windows-target evidence;
actual Windows OS runs remain open.

A repeated `taskset -c 0-11` five-interval 500×500 evolving-wave profile at .1 s
spends 210–259 ms in warm pressure solves, with 12 workers and 12–13 complex
cycles per interval. Retained workspace is 433,491,184 bytes (413.4 MiB,
1,734 bytes/cell), including the coupled hierarchy. Host load is 22.47–22.83;
functional validation runs concurrently. These stage profiles exceed the total
62.5 ms target before other stages or adapters; they do not certify real-time
acceptance or whole-process additional memory. Commands, source identity and
captures are under `local/coupled-moment-solver-v1/`.

The original strong-flow inputs still fail collision density refinement with
this method: 0.86176589 against 0.85. Its five completed profiles receive only a
partial saved account review. Strong flow, the vacuum front, free surfaces and
other release physics remain open. Automatic calm-step selection, full fluid
subcycling and in-game adoption are unimplemented.

```sh
cmake --build build-production --target ma_mac_kernel_moment_tests ma_mac_kernel_moment_multigrid_tests
build-production/ma_mac_kernel_moment_tests --coupled > CAPTURE
python3 native/tests/review_mac_pressure_moments.py CAPTURE
```

Omit `--coupled` to check the unchanged version-7 path. `--write-checkpoint PATH`
and `--continue INPUT OUTPUT` supply fresh-process controls. `--trace` and
`--checkpoint-modes` isolate the input controls. A leading `--coupled` selects
version 8 for new fixtures; ordinary continuation uses the saved selection.
