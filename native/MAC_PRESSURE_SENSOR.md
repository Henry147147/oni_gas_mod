# Pressure-sensed acoustic compression

The first unsensed owning candidate fails the original weak-acoustic Fourier
profile check. A constant half-sound-speed term damps the linear wave itself.
The native joint refinement still passes, but that is insufficient: the
independent pointwise limit remains 5e-5 of the wave amplitude. This candidate
restricts the extra acoustic term using source pressure curvature. Its component
and owning prerequisite controls pass; the strong-flow gate still fails. The original pressure/acoustic reviewer and scientific inputs are
unchanged.

For each occupied cell and axis, compute

`nu_i = abs(p_left - 2 p_i + p_right) / (p_left + 2 p_i + p_right)`.

All pressures are nonnegative physical EOS pressures. A zero denominator gives
zero. Boundaries and solids supply the adjacent cell's own pressure (no normal
pressure jump across a wall); the stencil never crosses a solid. Set
`psi_i = max(nu_left, nu_i, nu_right)` and the frozen linear coefficient to
`c_i = (c_sound_i/2) psi_i` for that axis. The compatible pair solver, quadratic
coefficient, time split, full arithmetic and actual-operand energy/wall/entropy
accounts remain unchanged. There is no threshold, coefficient scan or fitted
coefficient.

The normalized curvature and neighboring maximum follow the pressure-sensing
approach discussed in the primary [Swanson and Turkel NASA report, section II,
equations 9-10](https://ntrs.nasa.gov/api/citations/19870012739/downloads/19870012739.pdf).
Our staggered-cell stencil, pair relaxation and compatible heat deposition are
our adaptation; this is not the report's complete second/fourth-difference
scheme or a claim of its stability result.

The sensor lies in [0,1]. It is zero for a zero or affine pressure triple and
decreases with pressure perturbation amplitude in smooth weak flow. Thus the
extra compression force has no first-order weak-wave contribution. The
original discrete Fourier profile must still pass; this argument does not
replace that check. Sharp pressure changes retain a nonzero acoustic term.
Pressure, curvature, effective coefficient and sound speed are frozen from
one actual incoming component state. The operator still uses a first-order
treatment of their thermal dependence.

Numerator/denominator sums, the positive ratio and the effective linear speed
use full scaled arithmetic. Multiplying a large pressure by two does not
project it to binary64. A pressure curvature far below binary64 range remains
positive. No pressure floor, clamp, trace removal or state repair is introduced.
The original unsensed acoustic and quadratic component references remain
explicit; the owner selects the sensed law.

Three new groups are fixed before the first native invocation:

1. Uniform, affine, step, peak, trough, zero and maximum-finite pressure triples;
   exact cancellation retaining a positive sensor below binary64 range.
2. Periodic gas and wet solid-corner pressure jumps, non-square transposition,
   exact agreement with the quadratic solution at uniform pressure, and an
   actual new below-range face J with retained heat/unresolved flags.
3. Negative/nonfinite pressures, invalid timestep and unknown law reject
   without changing a source.

The saved-only review reconstructs exact-fraction pressure sensors and boundary
stencils, actual source pressure/EOS, each event's sensed coefficient, full
pair/grid U+K and wall accounts, entropy, constituent/passive retention and
canonical range. It must reject altered scalar/grid sensors, trace mass and
effective acoustic coefficient. Original pair and physical bounds remain fixed.

New component native/review caps: 120/180 seconds and 2 MiB per raw pipe.
Before a changed-owner strong-flow retry, the [owning gate](MAC_ACOUSTIC_OWNER.md)
now requires all 119 groups and reviews on both platforms, plus 58 Linux
sanitizer groups. These add this component's three groups to the earlier
116/55 declaration. Existing ordinary, owning and strong-flow bounds remain
unchanged. No strong-flow or game acceptance is implied by this candidate.

## First fixture failure

Both strict builds pass. Windows completes all eight scalar triples and four
gas/wet/transpose/uniform-pressure grids. Construction of the final tiny grid
then rejects `from_parts(0,-1941)`: a canonical zero must have exponent zero.
The component is never called on that invalidly constructed grid. Linux
retains its strict build and does not repeat this known fixture rejection.

The fixture now leaves its intended zero face as the default canonical zero;
its other velocities, physical inputs and exponent remain unchanged. No
sensor or solver implementation changes for this correction. A saved-only
partial review checks 13,471 identities across the eight triples and four
completed grids without replaying native code. It runs no corruption controls
and does not pass the full three-group gate.

Private `mac-acoustic-owner-windows-r2` failed terminal SHA256:
`5b401b4570c2d323bee5622fe2339c9cfcefe1a30243bb87b5d0ec3483824120`.
Linux build-only `mac-acoustic-owner-linux-r2` terminal SHA256:
`ec83efdfbbb72726627a3159048baf7566dee3d9ea8e64d5788c8a8e48c591f8`.
The partial review is `local/mac-pressure-fixture-partial-review.json`.

## Accepted component and owning prerequisites

The corrected three-group suite passes strict Windows/MSVC and Linux/GCC15
builds, 14,832 saved checks and all four scalar/grid/trace/coefficient corruptions
on each platform. The actual tiny grid creates a positive or negative nonzero
canonical J on a previously zero open face while its binary64 projection stays
zero, and reports positive below-range heat and unresolved stored-U changes.
No trace or momentum contribution is removed.

All 119 owning prerequisites pass on both platforms, including the original
Fourier profile and gravity controls; all 58 selected Linux sanitizer groups
pass with empty native stderr. The [owning evidence](MAC_ACOUSTIC_OWNER.md)
binds the frozen sources, binaries, raw captures, checkpoint checks and reviews.
Its subsequent strong-flow retry still fails collision density/pressure
refinement. No production or real-game acceptance is claimed.
