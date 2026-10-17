# Separation remap comparisons

These are private Windows component experiments after the completed-step
diagnostic in [bounded carried energy](MAC_BOUNDED_ENERGY.md). They change no
tracked numerical implementation, default, installed game file or scientific
criterion. They do not establish finest-grid, Linux, production or playable
acceptance. The existing 217-group prerequisites and failed strong-flow result
remain the authoritative results for the tracked solver.

All four captures below use byte-identical separation N64 inputs, the original
horizon and nominal timestep, and 46 accepted steps with no timestep retries.
Every process and pipe closes. Saved-only review independently verifies the
same initial material inventories, face momentum and problem definition, and
recomputes the original profile measurements. The table is a coarse comparison;
the absolute 0.06 and central 0.5 gates still apply at the original N256 grid.

| Method | Density L1 | Pressure L1 | Velocity L1 | Central density, left/right (kg/m3) |
| --- | ---: | ---: | ---: | ---: |
| Tracked selector 4 | 0.0554024 | 0.0505242 | 0.185070 | 0.0389251 / 0.0390314 |
| Remove scalar neighbour envelope | 0.0474145 | 0.0412543 | 0.143765 | 0.0364212 / 0.0363848 |
| Carry cell kinetic energy with material | 0.0682200 | 0.135432 | 0.293706 | 0.0439336 / 0.0439336 |
| Local pooled mixing-energy budget | 0.0589832 | 0.0581090 | 0.171550 | 0.0538390 / 0.0319121 |

Comparison evidence is `local/mac-separation-comparisons-r1`, SHA-256
`0344c53f7ea507826803e42e77998487d806ac96483f6e75ad3a9602638cdbac`.
Its partial profile review performs 1,820 checks per capture after parsing;
none is a full strong-flow certification.

## Scalar envelope and energy placement

Removing only the two neighbour-envelope restrictions retains the original
positive-field half-stock limit, donor conservation, parcel/retained EOS
admission and all completed-state guards. The 184 sampled central limiters
change from zero to one. The first local entropy reading below the uniform
initial value now appears at step 3, rather than step 5. Final central specific
entropy changes reach about -610/-642 J/(kg K). This improves coarse L1 errors
but does not justify removing the restriction. Terminal evidence:
`local/mac-bounded-profile-probe-r1`, SHA-256
`4298f97abc798a0de2773a18533663f6c3ead6baecfaa0ae6947a1c83f3e2b7e`.

The energy-placement comparison instead retains the original reconstruction
and momentum law. It partitions canonical face kinetic energy into source
cells by their inertial mass shares, carries that inventory with the actual
material parcel masses, and subtracts final canonical cell kinetic energy.
Independent saved accounts verify all 184 selected source, advected and final
kinetic quantities and actual U updates. The first private build incorrectly
converts an already scaled fallback mass to double; that compile failure is
retained and repaired without changing the method. The completed comparison
warms the central cells to 436.20 K and worsens all three errors. It is rejected
as an accuracy repair. Terminal evidence: `local/mac-primal-kinetic-probe-r2`,
SHA-256 `3946ec4894f0d7b074576df9e4454518fb90d56fe80b8b059ff3fa1b0934c958`.

## Paired existing-energy budget

A further private construction funds a geometric momentum correction only
from the two nodes' existing nonnegative mixing-energy remainders Hl and Hr.
For left-to-right impulse R, the available total remainder after correction is

`Hl + Hr + (ul - ur) R - R^2/(2 Il) - R^2/(2 Ir)`.

Its original-sign quadratic root limits R. Carried energy F must leave both
local remainders nonnegative. In absolute-flux form this requires

`ur R + R^2/(2 Ir) - Hr <= F <= Hl + ul R - R^2/(2 Il)`.

The first attempt rejects before an accepted step: nearest storage of the lower
endpoint lies outside this interval. A read-only diagnostic proves one upward
representable step would enter that interval. Inward stepping then reaches a
different pair whose much narrower interval contains no representable single
flux. Common-velocity product rounding can also reverse its computed bounds.
Both failures and read-only diagnostics are retained:

- First failure `local/mac-pooled-work-probe-r1`, terminal SHA-256
  `ed44ad2e95bf384e91dfbafe087fc98fee780beea64167fb48596e7a6514785e`.
- First operand diagnosis `local/mac-pooled-work-probe-r2`, terminal SHA-256
  `d779411b2766b8b75f1533460f9c19b63f150e4dcfbb49070cb5de04d95e1e5a`.
- Inward-step failure `local/mac-pooled-work-probe-r3`, terminal SHA-256
  `9734e1d89e2807323f69a98e952c403096059b38bb78ddbe0732629f22704198`.
- Second operand diagnosis `local/mac-pooled-work-probe-r4`, terminal SHA-256
  `d4b7e4f0a1357b5089cdce19617ea8b14803d39400e216b9cc20d36564072c87`.

The stable variant chooses left work within `[-Hl, Wtotal + Hr]` and obtains
right work as the conservative remainder. It keeps the small carried-energy
adjustment separate from the large mean-velocity flux. Both components enter
the original per-side energy accounts and actual carried-energy expansions.
All four heat/carried-energy signs remain strictly nonnegative; no stored
thermal energy, physical floor or relaxed tolerance funds the correction.

The completed private capture contains 552 selected events, including 466
separate carried-energy adjustments and 204 restricted impulses. Saved review
independently checks 368 fully sampled pooled pairs, all selected local heat
remainders, actual cell deposition, the original per-side conservation bounds,
and the EOS and donor/target inventories. The reader-builder's first filename
substitution fails before review; correcting that saved-only script needs no
native relaunch. Terminal evidence: `local/mac-pooled-work-probe-r5`, SHA-256
`96d74eab809f0093baee5893458c7c7976290e77ae2b06edd8d26de831bdc234`.
Its temporary diagnostic companion occupies an otherwise unused report array
only in this private build; this is not a public ABI or implementation choice.

This variant still does not solve the separation problem. Its larger central
left/right difference requires investigating update order before promotion.
At step 10, the first below-initial central specific entropy occurs before
signed-work deposition: reconstruction/transport gives -970.35352 J/(kg K),
and the local work and subsequent heat stages change none. The source value
there is -970.16206, versus uniform initial -970.22915. Thus preventing negative
local remap heat alone does not settle scalar entropy transport or accuracy.
No unchanged finest-grid or Linux repeat is launched. Order-independent
momentum/energy transfer and scalar reconstruction remain unresolved, alongside
the original full strong-flow and production gates.

## Update-order controls

Splitting each frozen geometric correction into forward-quarter,
reverse-half, forward-quarter traversals keeps the original simulation dt.
The private N64 run completes all 46 steps and 1,656 sampled work events.
Coarse density/pressure/velocity errors are 0.0566224/0.0552036/0.176857;
central density is 0.0367862/0.0507058 kg/m3. The imbalance decreases but
remains substantial. No sampled central reading falls below the uniform
initial specific entropy in this capture. Saved review checks 1,104 fully
sampled paired budgets. Terminal evidence: `local/mac-pooled-work-probe-r6`,
SHA-256 `fbc743fc2992e630978da7b0b45bf487f4dd85626007b4c0844fea068cb906a8`.

Its first saved reader incorrectly forms the side-account scale from the net
of opposing mean/correction fluxes. Six selected sides then fail that smaller
scale. Exact saved Fractions show every side passes the original native
128-epsilon rule using both actual operands. Restoring those operands repairs
the saved-only reader; no native rerun or multiplier change is involved.
Diagnosis SHA-256:
`59caabf00e13f9d994a586b4f46edaf426f5e144ab975de4e742b9b89036438d`.

A further private trial computes both opposite traversals independently from
one upwind state, averages J and carried K, and adds the nonnegative kinetic
averaging variance `(Jf - Jr)^2 / (8 I)` to the work inventory. This preserves
the two branches' mean total energy under the original operand account.
All 48 sampled averaging records and 384 sampled paired events pass saved
review over eight internally accepted substeps. A subsequent local pooled
precision guard rejects before a complete interval is returned. The capture
exits 1 without timeout; no final Riemann profile or benchmark certification is
available. Selected initial cell bytes and input headers match the baseline;
the complete initial profile is not emitted by the interrupted fixture.
Evidence: `local/mac-symmetric-pooled-probe-r1`, terminal SHA-256
`1d2b27dc6cfcc4d428c42d204379d17bbbecf1b5a31f199970595ba0e868093d`.
The next question is whether explicit local feasibility search can resolve the
remaining finite-precision rejection. No local bound, scientific criterion,
initial state, benchmark timestep or production selection has changed.

## Local admission and reconstruction trial

The private symmetric method then retains its analytic paired upper fraction
and 64 adjacent impulse backoffs. If those do not resolve the strict local
signs, it verifies zero correction and performs at most 53 scaled bisections,
retaining only an admitted pair. The unchanged final per-side and completed
state guards still apply. All 46 N64 steps complete. Saved review checks 2,208
selected paired events, 276 averaging records and the returned canonical
momentum. Six selected local searches record 92 rejected trials; the accepted
signs and recorded brackets are checked independently, but rejected trial
operands are not independently replayed. Evidence:
`local/mac-symmetric-pooled-probe-r2`, terminal SHA-256
`d7167a6fd7c3a1f94b2875cf9e3268a45682a3842256cf4fafa1c0df5db8727e`.

Using one full traversal in each direction instead of two three-pass branches
also completes the coarse case. The relative central mass difference is about
5.5e-16, but density/pressure/velocity L1 errors remain
0.0600926/0.0593387/0.176997. Its terminal evidence is
`local/mac-symmetric-pooled-probe-r3`, SHA-256
`1a77046d3b1abf52e709384c7aaadc8d866190344b6a2bbae251ec4ee995a13d`.
These isolated native timings do not establish game rendering performance.

A separate reconstruction experiment removes the neighbour-density envelope
and admits parcels and retained material against the current donor-stencil
minimum specific entropy. This is motivated by the convex admissible sets in
[Zhang and Shu's perfect-gas FV/DG work](https://math.umd.edu/~tadmor/references/files/Zhang%20%26%20Shu%20Minimum%20entropy%20NM2012.pdf).
Their theorem is not a proof for this deformed implicit mixture method.
The original positive-field half-stock limit, conservative donor subtraction,
EOS checks and reconstruction backtracking remain. The comparison uses the
original 128-epsilon stored-U/T allowance, scaling the reference inventory
homogeneously to the candidate mass. Only reconstruction theta changes;
canonical energy and entropy are not clamped.

Saved review verifies 456 selected reconstructed entropy admissions and all
prior pair, averaging, EOS and material accounts. The coarse errors improve to
0.0493508/0.0476480/0.178860. No sampled central specific entropy falls below
the uniform initial value in this capture. Evidence:
`local/mac-entropy-reconstruction-probe-r1`, terminal SHA-256
`aca3aa9c8eed5d7bd91e4956a2d12de270fe74e84a717df8a9a0bb6002194cb6`.

The next single original N256 separation case still fails. It completes 182
steps with zero timestep retries and preserves the original balances. Its
density/pressure/velocity errors are 0.0136829/0.0141810/0.0869008; velocity
exceeds 0.06. Central density and pressure relative errors are 31.5502 and
131.525, above 0.5. The central cells contain about 0.0106165 kg/m3 at 55.575 K,
versus exact star density 0.00032616 kg/m3 at 13.650 K. The process exits 1 on
the original finest error guard. Full initial profile bytes match the earlier
N256 source. The original 180-second/2-MiB capture limits are fixed before this
single-profile run; it is not a prerequisite, joint-refinement or full strong
gate attempt. Evidence: `local/mac-entropy-reconstruction-fine-r1`, terminal
SHA-256 `807c65bcecc1c47c4a5b777cdad8feacb6776e2dc41e26034e89ee44efb1d23b`.

Saved localization assigns 0.0812167 of velocity error to the expansion fans
and 0.00149103 to the central star region. The adjacent central velocities are
about -35.697/+35.697 m/s where the exact values are zero. Density divided by
grid spacing is about 2.765 at N64 and 2.718 at N256. These are observations,
not a proof of asymptotic order, impossible criteria or inconsistent inputs.
Four distant cells have independently recomputed specific entropy about
1.9e-11 to 2.3e-11 J/(kg K) below the uniform initial value, beyond the single
endpoint allowance. Cumulative stage roundoff is not recoverable from the
final profile, so a global pointwise entropy theorem is not claimed.

The first localization reader incorrectly compares pressure computed from
conserved U with a norm using the printed pressure projection at a much
tighter analysis precision. Using conserved-EOS pressure consistently repairs
that saved-only analysis without changing any scientific threshold or running
native code again. Evidence: `local/mac-entropy-reconstruction-fine-analysis-r2`,
SHA-256 `ae2f68738cba479a42db8718c5b71be6747c490f57309e2fac453925f659299c`.
No method, default or production gate is promoted, and no unchanged Linux
strong repeat is launched. Spatial expansion profiles and their pressure/mass
transport remain unresolved.
