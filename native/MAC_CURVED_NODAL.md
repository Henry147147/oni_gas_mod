# Curved-deformation pressure/remap diagnostic

The pressure/remap interface has an unresolved source-velocity convention.
The pressure step uses `J/I` to move material face boundaries. The swept momentum
remapper reconstructs those values at dual-volume centroids. The existing
affine-exact controls make those positions coincide in their sampled interiors;
curved deformation separates them.

`ma_mac_curved_nodal_diagnostic` is an optional diagnostic target, excluded from
the default build. Its current exit is **1**, retaining the failed face-value
criterion. This is a newly localized problem, with no numerical repair or
production selection promoted.

## Independent geometric reference

For the one-dimensional, unit-density controls, moved faces are at
`x_f = f + q_f`. Source cell widths are `1 + q_(f+1) - q_f`, and the adjacent
half-cell inertia is `I_f = 1 + (q_(f+1) - q_(f-1))/2`. The dual-volume centroid is

```text
mean_x_f = f + (q_(f-1) + 2 q_f + q_(f+1))/4.
```

Each case prescribes the physical affine field `u(x) = 1/4 + x/8`. Convention
0 supplies `J_f = I_f u(x_f)`; convention 1 supplies `I_f u(mean_x_f)`. Both are
compared with the same fixed-grid reference `J_f = u(f)`. They are deliberately
distinct source states.

There are 16 controls: both axis orientations, both sweep signs, affine/curved
geometry and both source conventions, with two identical transverse rows.
The interiors sample faces 4 through 6. Curved sweeps use
`q_f = direction * ((f-1)/64 + (f-1)^2/512)`, with fixed outer faces.
There, `mean_x_f - x_f = direction/1024`. The observed point-source error is
exactly the corresponding centroid alias, `-direction/8192` in velocity.
The original 128-epsilon account checks actual canonical momentum; it is not
replaced by a looser diagnostic tolerance.

Strict Windows MSVC and Linux GNU execution agree: 24 of 48 face-value readings
fail, while all 48 dual-mean readings pass. The 24 affine face-value readings
also pass. Every candidate retains correction fraction 1 with zero admission
backtracks, and every reported energy guard passes. Positive helium and passive
traces and source immutability remain required. These are one-dimensional
controls in both orientations, rather than arbitrary deformed 2D geometry.

## Original separation localization

A separate private probe uses the original N64 separation source and its first
prescribed timestep, with material selector 2, momentum selector 4 and coupled
pressure selector 3. All 128 initial state rows and 258 initial face-momentum
rows match the retained original strong-flow transcript byte for byte. The first
shock-heat half-step is verified inactive, and the original source is unchanged.

At the adjacent interface faces, the pressure displacement and remapper centroid
differ by up to `0.04994612721934058` cell widths. A piecewise-linear interpolant
of the produced face velocities has a dual-mean momentum differing from its
stored face value by up to `0.045565683730133096 kg m/s`. The saved reader
recomputes that difference from the actual masses and velocities. Pressure and
full-substep energy accounts pass, with full correction and no backtracking.

This demonstrates that the distinction occurs in the actual pressure/remap
handoff. It does not quantify its contribution to the final strong-flow error.
A repair must define a conservative conversion of momentum and kinetic energy
between these representations while preserving the original physical source.
Supplying the alternate dual-mean source changes J and K, so its successful
component readings do not constitute a repair. The original finest-grid,
refinement, near-vacuum and production-colony gates remain open.

## Reproduction and saved review

```sh
cmake --build <build> --config Release --target ma_mac_curved_nodal_diagnostic
<build>/Release/ma_mac_curved_nodal_diagnostic > curved.stdout 2> curved.stderr
python native/tests/review_mac_curved_nodal.py curved.stdout
```

On GNU builds the executable is directly in `<build>`. The executable and reader
currently both exit 1 for the failed nodal criterion. The reader exits 2 for
invalid data and reports `evidence_valid` separately from `nodal_gate_passed`.
It independently checks the geometry, reference momenta, exact stored operands,
match flags and completeness. Its energy evidence uses the native reported
guard; it does not independently reconstruct every U/K or entropy operand.
Five saved-data corruptions are rejected: stale actual-momentum flags, changed
centroids, changed sweeps, changed affine reference and truncated completion.

Private original geometry/pressure diagnosis:
`local/mac-curved-nodal-r1`, terminal SHA-256
`fd2ca91b48fafdf49a8ea4875aa92d8b761230be12476b915db2b223e9ba3029`.
Its first pressure-probe build retains a strict constant-condition warning;
passing N64 as an argument repairs that fixture without changing its value.

Public diagnostic Windows/Linux review: `local/mac-curved-public-r1`, terminal
SHA-256 `24a24a46b0253eee6ef66ece3c77b3a334ca52969c8cb7315168fc043d4905d8`.
The already verified static mathematics libraries are reused after source and
binary identity checks. The full 239-group/40-review matrix and original strong
suite are not relaunched for this diagnostic-only addition. There is no Unity,
rendering-performance or first-playable acceptance result.
