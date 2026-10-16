# Scaled momentum contributions

Four new controls and all prerequisites pass on Windows and Linux. The
[primary-parcel cohort](MAC_PRIMARY_PARCELS.md) reaches an incoming momentum
transfer of about 3.615258007097827e-585 kg·m/s before it fails. Its share still
narrows to binary64 before reaching the receiving dual-volume accumulator.

Retained and incoming normal momentum contributions now use the same unchanged
significand/exponent range arithmetic as the material/energy accounts. An exact
additive expansion accumulates all signed contributions before one explicit
binary64 primary-momentum projection. Its rounding residual must satisfy the
existing 128-epsilon bound, and a nonzero result must retain its sign and remain
nonzero. Canonical face state, provisional/wall momentum, velocity, pressure
impulse and inertia remain binary64. This does not extend canonical storage,
wall reactions or the EOS; a genuinely unresolved accumulated result still
rejects atomically. No small contribution is independently clipped.

The following four native groups are declared before the first build/run:

1. The identified tiny incoming scale on uniform periodic gas, with existing
   positive face J. Contributions below binary64 remain positive until they
   join the target accumulator. Stored uniform state stays identical.
2. Uniform minimum-positive binary64 J with a one-eighth dual mass sweep.
   Both retained and incoming momentum contributions are below the original
   storage range/precision, but their exact sum must preserve stored J.
3. Alternating J = min-denormal and -3 min-denormal with a quarter-volume
   sweep. Signed contributions must cancel exactly on alternating faces and
   produce -2 min-denormal on the others. Actual K, variance heat and U accounts
   remain within the original bounds.
4. A below-range incoming momentum entering a destination with no other J
   must still reject atomically, with the original source intact.

A separate exact-fraction saved-data reviewer must reconstruct the full
contributions from donor J, actual dual fluxes and inertias, verify every stored
J/K/heat/U and constituent account, and reject altered flux, stored J and trace
records. No independent agent review is claimed.

First strict Windows/MSVC and Linux/GCC15 builds/captures are retained.
Predeclared bounds: aggregate build 180 s; new native/review 60 s each; primary
and derived-energy native/review 60 s; range/primitive 30 s; reconstruction
180 s; closed/gravity 60 s; each raw pipe 512 KiB. Only after all prerequisite
native groups and separate reviews pass may Linux repeat its strong-flow gate
once with the unchanged scientific source, physical criteria and 180-s/2-MiB
bounds. The unrelated Windows collision gate is not repeated by this repair.
No strong-flow, free-surface, production-performance, game or shipping acceptance
follows from scoped arithmetic controls. Earlier failures remain evidence.

## Results and canonical storage limit

Both first strict builds pass. Separate saved-data reviewers pass the
following checks per platform and each reject three altered records:

| Control | Native groups | Saved checks |
| --- | --- | --- |
| Momentum contribution range | 4 | 3,391 |
| Primary parcel range | 5 | 7,681 |
| Wide derived-energy integration | 4 | 4,530 |
| Range arithmetic | 7 | 21,019 |
| Original transport | 7 | 3,888 |
| Material reconstruction | 5 | 13,325 |
| Closed pressure/transport | 8 | 6,530 |
| Smooth gravity | 4 | 16,462 |

The tiny identified transfer joins an existing J without changing uniform
stored state. Subnormal retained/incoming contributions recombine exactly.
Signed quarter-volume transport produces exactly zero/-2 minimum-denormals,
retains positive full mixing heat, and explicitly reports unresolved stored-U
updates. An unresolved new primary J still rejects atomically.

After all sixteen prerequisite captures/reviews pass, one Linux strong-flow
retry passes the old incoming-share rejection but fails when storing the
accumulated canonical result. It completes zero Sod profiles. A first-rejection
diagnosis identifies **open x face 45 of 130**, previous J = 0 and inertia
3.0517578125e-05 kg. Its complete accumulated value has one expansion term,
`0.72004705831217664 * 2^-1941`, approximately
**3.615258007097827e-585 kg·m/s**. Its nearest binary64 value is zero.
The unchanged primary storage rule rejects it. This is an open-face canonical
state limit, not a wall-reaction projection. Intermediate range alone cannot
complete this case; preserving the state requires a wider canonical
representation and coherent pressure, velocity, flux and energy treatment.

The diagnostic changes only failure text/site and uses the original Sod N64
inputs once, with 90-s build/20-s native bounds. All captures have normal EOF,
without timeout or pipe overflow. No full/partial profile reviewer is invoked
for the empty profile set. The Windows collision gate is carried forward
without another invocation, and its accuracy failure remains unresolved.
The scientific Riemann/gravity sources and physical criteria stay unchanged.

Private captures are
`local/occupied-staggered-validation/mac-momentum-parcel-{windows,linux}-r1`.
Completed scoped-review SHA256 values are
`f2002d295d58803181354d3a6f6bb386101b3b7677fc9d2e7c4122313762dfd4`
(Windows) and
`96e9d37e93c39e61329785c0b042b3af141557f35b8ea51bfde6728d83adebe3`
(Linux). They bind frozen source, compiler/binary identity, prerequisite
captures/reviews and the Linux open-face storage diagnosis. No independent
agent review, Unity launch, production/performance or shipping acceptance
occurred. The default game DLL is unchanged.
