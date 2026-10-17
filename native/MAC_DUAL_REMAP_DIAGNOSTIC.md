# Affine momentum remap diagnosis

The existing momentum predictor has a reproduced geometry error before heat
limiting. The current limiter also rejects a completed state that satisfies
the declared physical constraints in a separate translation fixture. These
are two repair requirements. The original strong-flow gate remains failed;
this diagnostic supplies no replacement operator or colony acceptance.

## Prescribed controls

The diagnostic runs 24 cases: two axes, four controls and momentum selectors
0/1/2. Each uses ten cells along the flow direction, two invariant periodic
transverse rows, fixed end faces, unit grid spacing, constant bulk density,
300 K nitrogen, positive helium at mass ratio 1e-28 and a positive passive at
ratio 1e-30. Source material volumes close exactly against the supplied
signed volume sweeps and the nominal fixed grid. Material parcels use the
existing swept reconstruction. The four controls are:

- A stretched source grid with interior face displacements `(f-1)/64` and
  source velocity `u(x*) = 1/4 + x*/8`.
- Uniform interior translation by 1/8 with the same affine velocity.
- The stretched geometry with constant velocity 1.
- Zero sweep with the affine velocity on the fixed grid.

The selected faces 4/5/6 have equal source spacing on both sides. Their
geometric dual-cell centroids coincide with their nodal coordinates, and
their fixed-grid centroids coincide with the target nodes. Thus those two
interpretations give the same affine expectation in this fixture. End-face
kinetic removal, source/target accounts and every constituent/passive remain
part of the complete saved transport review.

## Geometry result before admission

In the stretched interior, `S = 65/64`, `d = S-1`, and `b = 1/8`. At unit
represented density, the upwind target momentum is

```
J_up = u_fixed - b*d*q_f + b*S*d/2
```

The existing predictor gives the outgoing correction
`R_right = (q_f+d/2) * b*(S-q_f)/2`. The incoming correction uses
`q_left=q_f-d`. Their net change leaves

```
J_unlimited = J_up + R_left - R_right
            = u_fixed - b*d*d/4
            = u_fixed - 1/131072
```

Both corrected selectors reproduce that exact bias on both axes and OSes.
At selected face 5, its magnitude is `7.62939453125e-6` against the original
128-epsilon affine operand allowance `4.97377746628e-14`. Actual admitted
errors are `-1.47976147928e-5` for selector 1 and `-1.45323120435e-5` for
selector 2. The saved reviewer derives the unlimited candidate independently
from the upwind J and each requested edge correction, before its limiter.
Uniform translation, constant velocity and zero sweep pass that unlimited
affine comparison. Admission is inspected separately.

For uniform stretch, the swept dual boundary displacement is `q_f+d/2`.
Its slab centroid differs from the current predictor's point by `-d/4`.
This supplies a specific geometry correction to investigate. General
deformation, unequal density and walls still require a consistent declared
dual geometry and independent controls; this one-dimensional calculation
does not establish them.

## Physical admission result

In the translation fixture, selector 2's unlimited candidate is affine exact;
the admitted result at face 5 differs by `1.84314598897e-5`, with a minimum
touching limiter of 0.997303740725. A saved-only counterfactual applies every
recorded request, derives compatible work and remainder changes with exact
fractions, then rounds canonical J and material U once. It checks the actual
stored U+face-K, wall momentum, EOS and entropy using the original bounds.

The translation counterfactual restores all six selected affine readings.
Two face remainders are negative, with minimum `-0.000023365020751953125 J`.
Transported kinetic energy remains nonnegative, the stored total-energy
residual is exactly zero against a `9.90409117739e-8 J` allowance, and both
momentum accounts pass. Entropy rises `0.000438070242090 J/K` against the
original stored-U allowance `3.30134870956e-10 J/K`. Temperatures remain in
the declared EOS domain, between 299.999999984281 and 300.000081601596 K.
The other axis gives the same conclusion. All eight selector-2
counterfactuals meet their physical checks; the stretched ones retain the
separate geometry bias.

These counterfactuals establish feasibility for the specified fixtures. A
replacement needs a general physical admissibility algorithm. Preserve the
positive upwind-variance account and represent any signed remap-work energy
separately. Require the completed canonical state to satisfy the original
mass/passive, U+face-K, momentum/wall, EOS-domain and entropy conditions.
Admit and publish the candidate atomically. Existing selectors and their
nonnegative-remainder contract retain their behavior. A geometry-only repair
or an unlimited-candidate comparison cannot satisfy the new solver gate.

## Reproduction and evidence

An opt-in replacement now implements both repairs and passes actual stored
affine controls on Windows, Linux and ASan/UBSan. See
[swept physical momentum](MAC_SWEPT_MOMENTUM.md) for the implementation,
completed-state admission, retained failures and remaining acceptance gates.

Build the explicit `ma_mac_dual_affine_diagnostic` target. It emits diagnostic
records and exits after complete collection; exit 0 is not a claim that the
affine criterion passes. Review a bounded saved stream with
`review_mac_dual_affine.py <raw> <fresh-report.json>`. The separate
`review_mac_dual_affine_energy.py <raw> <fresh-report.json>` computes the
counterfactual; it launches no native process. These targets remain outside
the default build and production CTest acceptance.

The counterfactual entry point first validates the complete saved source
stream. Its later validation-only update preserves all eight numerical
results on each platform and rejects a corrupted candidate at that entry
point. No native run is repeated for this reader update.

The first private strict MSVC/GCC captures complete all 24 cases per OS.
Each saved review performs 189,552 checks, confirms 24 geometry witnesses
and rejects all three sweep/candidate/trace corruptions. No numerical source,
coefficient, physical input, norm or original strong benchmark was changed.
Private evidence: `local/mac-dual-affine-diagnostic-r1`; terminal SHA256:
`362af41e36c9219b9838421ddfd66b7b523fba45c91fea9c30b4aea7b5e7baca`.

The published target and standalone readers subsequently complete on Windows,
Linux and Linux ASan/UBSan. All three saved reviews repeat the 189,552 checks,
24 geometry witnesses and three corruption rejections. Normal Windows/Linux
native streams are byte-identical to the first private captures. The sanitizer
build uses `MA_SANITIZERS` alone; inspected compile rules cover the diagnostic
and its linked numerical sources. All native/review stderr is empty. Private
publication evidence: `local/mac-dual-affine-public-r1`; terminal SHA256:
`8ea16f2b62d126cf2f4c0238a08821944cb99c330a09ad894b8f10c52c9d0f8d`.

Completion still requires the implemented replacement to restore the actual
affine controls, pass mixed-material/wall/range/atomicity/checkpoint controls,
and pass the full original prerequisites and strong-flow gate with unchanged
scientific inputs and limits. Production ownership, exact vacuum, game
screenshots, all devices and inventories, performance and first-playable
acceptance remain required by the full overhaul plan.
