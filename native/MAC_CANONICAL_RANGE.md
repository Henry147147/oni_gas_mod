# Range-extended canonical momentum and fluxes

Five new groups and all prerequisites pass on Windows and Linux. The later
[bounded integer exponent integration](MAC_EXPONENT_RANGE.md) removes this
cohort's signed-word limit and reaches full Linux Sod refinement; collision
accuracy remains failed. This work addresses
the recorded [open-face canonical storage rejection](MAC_MOMENTUM_PARCELS.md).
The original Riemann/gravity scientific sources, physical inputs, timesteps,
grids and acceptance limits remain unchanged.

Canonical J, transported velocity, volume sweeps, bulk mass fluxes, dual fluxes
and wall reactions now have full significand/exponent values. Existing double
arrays are explicit projections whenever a full array is present. Inputs must
match those projections bit-for-bit; stale projections or wrong shapes reject.
Primary gas/liquid constituent masses and U remain binary64. No constituent or
momentum cutoff is introduced, and primary storage/account bounds remain
128 binary64 epsilons of actual operands. The representation keeps 53-bit
significands and bounded integer exponents; it is not arbitrary precision.

The zero-gravity closed pressure/transport path preserves full J, derives full
midpoint sweeps, retains normalized full-volume parcels and accumulates full
dual momentum/K/heat. Full wall reaction and kinetic removal remain distinct.
Kinetic/variance/share overloads operate on scaled operands with normal
significands. Legacy energy fields retain their existing projection masks.

The existing pressure root receives projected initial velocities. The MAC
wrapper retains raw root inputs/outputs and restores the full initial-velocity
correction to its returned velocity before exposing that physical reading.
Actual pressure impulse, full midpoint sweeps and full K remain independently
accounted at the original bounds. Material volume/U rounding is still explicit;
the EOS and its root tolerances are unchanged. Full canonical state does not
extend every physical state variable or promise arbitrarily small velocities
can change stored U/volume. Range-extended vertical gravity work is explicitly
unsupported and rejects atomically; existing ordinary gravity controls remain
required. Full gravity range, phases, free surfaces, production ownership and
performance still require work.

Five new native groups are declared before the first build/run:

1. A uniform 300-K periodic atmosphere with the rejected canonical J scale,
   below-range velocity/volume/bulk sweeps, one pressure/transport step and one
   atomic owner interval. Full state, source, clock/revision and readings agree.
2. Warm thin gas (mass 1e-200 kg) with J below binary64 range, representable
   velocity/volume sweep and below-range bulk flux. The completed owner retains
   uniform primary state; a projected J must not determine the velocity.
3. The original incoming momentum scale creates a positive full canonical J on
   a face whose previous J was zero.
4. Below-range momentum removed at a rigid wall remains a full reaction and
   positive full wall heat, with unchanged source and exact operand accounts.
5. Stale projections/wrong full-field shapes and unsupported range-extended
   vertical gravity reject without mutating the source.

Saved records include full canonical/flux/wall values, full parcel volumes,
full dual fluxes, actual root inputs/outputs and corrected pressure velocities.
A separate exact-fraction/80-digit reviewer must validate every physical
pressure/transport account, full readings, owner clocks and explicit
projections, then reject altered canonical state, swept volume and wall reaction.

The older primary-parcel bulk-underflow negative control now requires the full
bulk flux to survive; its physical input is unchanged. The older momentum
destination negative control now requires full new canonical J to survive.
Their earlier binary64-storage cohort results remain retained. Constituent
destination and poorly resolved denormal-primary rejection stay required.
Those changes reflect newly represented quantities and preserve the original
conservation/error criteria.

First strict Windows/MSVC and Linux/GCC15 builds and raw results are
retained. Bounds declared before the first run: aggregate build 180 s; new
native/review 60 s each; primary/momentum/derived-energy native/review 60 s;
range/primitive 30 s; reconstruction 180 s; closed/gravity 60 s; each raw pipe
512 KiB. After all prerequisites and reviews pass, Linux may retry its failed
strong-flow gate once at its original 180-s/2-MiB bounds. This representation
change does not justify repeating Windows' separate collision-accuracy failure.
No full strong-flow, game, performance or shipping acceptance follows from
these controls. No independent agent review is claimed.

## Results and remaining exponent capacity

Both first strict builds and all 49 scoped native groups pass. The current
saved-data reviews pass the following checks per platform, with three altered
records rejected by each reviewer:

| Control | Native groups | Saved checks |
| --- | --- | --- |
| Full canonical/flux range | 5 | 7,040 |
| Momentum contributions | 4 | 4,565 |
| Primary parcels | 5 | 9,264 |
| Derived-energy integration | 4 | 4,530 |
| Range arithmetic | 7 | 21,019 |
| Original transport | 7 | 3,888 |
| Material reconstruction | 5 | 13,325 |
| Closed pressure/transport | 8 | 6,538 |
| Ordinary smooth gravity | 4 | 16,958 |

The new full states complete pressure/transport and atomic owner steps with
unchanged uniform warm-gas inventories, source identity, clocks and revisions.
Full velocities/sweeps/bulk fluxes, newly created open-face J and rigid-wall
reaction/heat match the independent accounts. Stale projections and unsupported
range-extended vertical gravity reject atomically.

The new raw captures are not bit-identical across platforms: 46 numeric fields
differ across 536 matching record lines, by at most 3.50 binary64 epsilons
relative to their magnitudes. Both independent physical reviews pass. The
comparison preserves raw hashes; no cross-platform determinism claim follows.

The first Windows data review fails at the independent thin-gas pressure
reference: a bracket starting at 1 Pa with 240 absolute bisections cannot
resolve its pressure near 8.9e-196 Pa. The repaired reference uses the analytic
ideal-gas equation when every constituent is gas; its mixed/liquid bracket and
all physical criteria remain unchanged. The original failure is retained.
Four affected reviewers are rerun against the original Windows/Linux captures,
including the new canonical, reconstruction, closed and gravity records.
No native capture or binary is repeated/rebuilt during this reference repair.
The increased legacy review counts include added full-field/projection and
analytic-reference validation, and the two deliberately extended older controls.

After all prerequisites pass, the single Linux strong-flow retry completes
Sod N64. Its partial saved-data review passes 5,996 checks and reproduces these
normalized L1 errors: density 0.0125756323775, pressure 0.00981171744507 and
velocity 0.0170505310651. This is one completed profile, zero completed full
refinement groups, and no corruption controls/full strong-flow review.

Sod N128 then rejects at the range arithmetic's signed 32-bit exponent limit.
A first-rejection diagnostic changes only failure text/site, runs that N128
input once, and records a scaled share requesting exponent **-3049816383** with
normalized mantissa **0.99960036254833473**. The check rejects before integer
overflow; no quantity is clipped. A wider integer exponent representation is
required before another retry. The 90-s build/20-s native diagnostic and all
captures have normal EOF without timeout or pipe overflow. The Windows
collision-accuracy failure is retained without a failed-gate replay.

Private native captures are
`local/occupied-staggered-validation/mac-canonical-range-{windows,linux}-r1`;
the repaired reviews and Linux strong retry are in the sibling `-r2` folders.
Completed scoped-review SHA256 values are
`600e695d6b504a894967d87e7ddf7f09658d122b95507159ed0c0adf7bf797f3`
(Windows) and
`d1c928f5fbd7140ab03252b17a0a3e6d098afdbd0fd15809176744c29418cbf8`
(Linux). They bind first-source builds/captures, frozen C++/binary identities,
the retained reference failure, repaired saved-data reviews and the Linux
partial profile/exponent diagnosis. No Unity launch or production/performance
acceptance occurred. The default game DLL is unchanged.
