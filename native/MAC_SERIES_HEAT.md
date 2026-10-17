# Compression heat through two half-cell resistances

This optional occupied-grid component passes its eight new controls within all
141 strict Windows/Linux groups and 80 selected Linux sanitizer groups. It
retained the existing maximum-speed law and owning/default heat mode 2 in
that component cohort. The subsequent [owning integration](MAC_SERIES_OWNER.md)
exposes mode 3 as an explicit experiment; its failed strong-flow trial prevents
retaining it as the default. The component supplies no production,
game or first-playable acceptance.

## Face construction

The preceding [viscosity-matched law](MAC_MATCHED_HEAT.md) uses
`max(d_left,d_right) * harmonic(Cp_left,Cp_right) / h`. The alternative freezes
the same source Cp, sound tangent, pressure sensor and positive compression
speed `d` in each cell, then assigns the local conductance `G_i = Cp_i*d_i/h`.
Temperature is stored at cell centers. For piecewise constant local conductivity,
continuity of steady heat flux through the two half-cell paths gives

`1/G_face = 1/(2*G_left) + 1/(2*G_right)`.

Thus `G_face = 2*G_left*G_right/(G_left+G_right)`. An inactive cell has infinite
half-cell resistance and supplies zero face flux. This differs from extending
the larger adjacent compression speed over both cells. No fitted coefficient
or failed-benchmark threshold enters the construction.

The local conductivity still follows the Cp/bulk-viscosity relation motivated
by [Cook, equations 6–9](https://www.osti.gov/servlets/purl/1224395). The half-cell
resistance construction is our finite-volume approximation; it is not Cook's
high-order method or proof of accurate shock trajectories. Coefficients remain
frozen per call, and mixed-EOS thermal dependence remains first order.

All products, sums and shares remain scaled, including positive conductance
below binary64 range. The existing pair relaxation retains fixed-volume
capacities, actual-U conservation, monotone temperature, entropy checks and
explicit unresolved-storage flags. Symmetric forward/reverse half sweeps,
walls, constituent/passive inventories and face momentum are unchanged.
`diffusion_speed` reports the equivalent face speed `G_face*h/harmonic(Cp)` for
this law. Existing report values for laws 0/1 retain their previous meanings.

## Validation contract

Eight new native groups cover periodic gas, a wet solid corner, compressed
liquid, inactive rest/shear/contacts/uniform temperature and an isolated
compressed cell, positive subrange heat, unequal-capacity two-body resistance
and semigroup controls on both axes, quadratic weak-wave amplitude, and
immutable invalid requests. The saved-only reviewer reconstructs source EOS,
Cp, sound and sensor fields, each local diffusion speed, reciprocal half-cell
resistances, exact activation/topology and every chronological pair account.
It must reject heat, trace, Cp, tangent, sensor, geometry, equivalent-speed,
activation and law corruptions. No independent-agent review is claimed.

Before owning selection, strict Windows/MSVC and Linux/GCC must pass all 141
groups/reviews (the prior 133 plus eight). Linux ASan/UBSan must pass the prior
72 selected groups plus eight, with fatal findings, leak detection and every
compile command audited for instrumentation. Existing native reference
captures, eighteen checkpoint setting combinations, retained legacy images,
cross-platform bytes and original Fourier/acoustic/gravity/scientific inputs
must remain valid. No unchanged failed strong-flow case is repeated.

Bounds fixed before invocation: each aggregate strict build 180 seconds;
component native capture 120 seconds and 2 MiB per raw pipe; saved component
review 180 seconds. All preceding stage/checkpoint limits remain. Failure
receipts close before any source edits. The default game DLL stays unchanged.

## Component results

Strict MSVC and GCC builds pass all 141 native groups and their saved-only
reviews. The new component review checks 34,507 identities on Windows and
34,511 on Linux, and rejects all nine declared corruptions. Linux ASan/UBSan
passes all 80 selected groups and their reviews with empty native stderr,
fatal findings and leak detection enabled. All 93 generated compile commands
are audited for sanitizer instrumentation; the new Linux component capture is
identical with and without instrumentation.

A separate saved-byte audit compares all 26 preceding native streams on each
OS (52 total) and all three written checkpoint images on each OS (six total).
Every byte is unchanged. All eighteen setting combinations, eight retained
legacy images and three cross-platform image reads per OS pass. The original
Riemann, gravity and Fourier inputs and reviewers are unchanged.

Private evidence is under
`local/occupied-staggered-validation/mac-series-heat-{windows,linux}-r1` and
`mac-series-heat-linux-asan-r1`. Terminal-review SHA256 values are:

- Windows: `0cb6d0affafc0bde9022005f0f1ae91302c9a3e717695b2323ba355f4a7aeb38`.
- Linux: `4afd43e5953726d8276206502ed03d147a51a981fccb251c9d9328ebba3425a8`.
- Linux ASan/UBSan: `5dc7b596fdf50f5345db1ef150a655304859af563682a508d4e3ce8f9e7bdb3e`.

The saved-byte audit SHA256 is
`65cdaefaac9deb66d92df81e51f79b8636a5396385dbdc07e46e5e4772d7f373`.
The original failed strong-flow result remains failed; these controls do not
establish that this alternative will improve it. No owning selection, new
strong-flow retry, game launch or independent-agent review occurred in this
component cohort. The default game DLL is unchanged.
