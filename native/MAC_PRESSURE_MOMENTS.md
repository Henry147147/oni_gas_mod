# Two-moment pressure integration

`MacPressureIntegration::two_moment` is an opt-in temporal scheme in the
production kernel. It supplies a building block for larger calm steps;
automatic accuracy selection and local subcycling remain open. Existing owners
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

The new selection requires numerical kernel 2 and checkpoint version 7. It adds
one versioned selector after the diffusion mode/multiplier; versions 2–6 keep
their bytes and restore the midpoint method. Unknown/noncanonical selectors
reject. A moment owner also rejects a request for an unsupported selector or
range path that would require legacy reference execution: it cannot silently
change its temporal method. Those cases need implementation before adoption
as a general production default.

## Current validation

`ma_mac_kernel_moment_tests` compares five 0.1-second intervals with fine
production-midpoint references. A short fine-step comparison also checks the
strict per-stage oracle. Reference refinement at the smallest and largest-step
controls stays below 0.1% of the pressure disturbance. The original accuracy
bounds remain 0.1 K and 1% for disturbance-relative pressure, O2 partial pressure,
constituents and passives. No retries are allowed to hide smaller candidate steps.

| Wave | Candidate dt, s | Reference dt, s | Pressure error / 20 Pa | Maximum dT, K |
| --- | ---: | ---: | ---: | ---: |
| 16×16 | .004 | .00005 | .0073422 | .00012554 |
| 32×2 | .008 | .0001 | .0017038 | .00002913 |
| 64×2 | .02 | .0001 | .0049429 | .00008450 |
| 256×2 | .1 | .0001 | .0027356 | .00004677 |
| 512×2 | .1 | .0001 | .00008250 | .000001411 |

Short gravity, wall/room, moving gas/liquid, compressed-liquid and empty-cell
controls pass their original comparison bounds. These do not replace the
strong-flow, vacuum-front or free-surface release gates. The 33,389-cell control
retains exact one/twelve-worker state and ordinary version-7 continuation;
budget exhaustion publishes no state or accounts. Saved temporal endpoints
are independently reviewed by `review_mac_pressure_moments.py`, including EOS,
fine-reference resolution, every comparison metric and altered-capture controls.
All 48 Release CTests pass with the ONI backend and warnings as errors;
ten suites pass Debug ASan/UBSan and MinGW 13/Wine 10, and nine pass
ThreadSanitizer. This saved reader passes 80,709 checks and rejects fourteen
altered captures on all four routes. All twenty saved reviews pass.
The 4,096-cell subnormal front reaches 256 previously empty cells with
14,138 audited transfer roundings. Physical/accelerated diffusion with
nonuniform composition and automatic/Jacobi settings retains ordinary
version-7 continuation. New images continue byte-identically in fresh
Linux/Wine processes, twice on each platform in both source directions.
Six historical images and sampled version-6 wave checkpoints keep their bytes;
the reference wave SHA remains unchanged. All 63 preceding MAC executables
retain their physical output and exits (61 byte-identical, two timing-only).
These are Windows-target Wine controls; actual Windows OS evidence remains open.

```sh
cmake --build build-production --target ma_mac_kernel_moment_tests
build-production/ma_mac_kernel_moment_tests > CAPTURE
python3 native/tests/review_mac_pressure_moments.py CAPTURE
```

`--write-checkpoint PATH` and `--continue INPUT OUTPUT` supply fresh-process
controls. `--trace` and `--checkpoint-modes` isolate the added input controls.
No automatic stepping, local subcycling, game or real-time acceptance is claimed.
