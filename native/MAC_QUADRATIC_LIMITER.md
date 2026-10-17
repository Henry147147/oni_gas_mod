# Quadratic kinetic-remainder admission

`MacMomentumReconstruction::quadratic_limited_linear` is an opt-in momentum
mode, encoded as value 2. It uses the existing geometric predictor and admits
its paired correction against the actual quadratic heat constraint. The
donor and sufficient-linear modes retain values 0 and 1. The closed owner
still defaults to value 1; this component does not change game dispatch.
Strong-flow and first-playable acceptance remain open.

## Defect and correction

A correction removes `theta * R` from left momentum and adds it to right
momentum. With source velocities `uL = JL/IL` and `uR = JR/IR`, each side's
pending mixing heat after the correction is

```
Q + theta * A - theta^2 * B
A = R * (uL - uR) / 2
B = R^2 / (2 * I)
```

The previous limiter uses the sufficient bound `Q + theta * (A-B) >= 0`.
It can reject an admissible correction. For `IL=IR=1`, `JL=1`, `JR=0`,
`QL=QR=0`, and `R=2`, that bound returns zero. The quadratic constraint
admits `theta=1/2`: momenta become 0 and 1, both heats remain zero, and
compatible work transfers 1/2 unit from left to right. A transported-work
budget of `(1/2, 0)` admits that same correction. The new analytic control
reproduces this difference without changing the existing function's result.

The new limiter finds the positive root for each side, capped at one. For
positive Q it uses

```
A <= 0: theta = 2Q / (sqrt(A^2 + 4BQ) - A)
A >  0: theta = (A + sqrt(A^2 + 4BQ)) / (2B)
```

The first form avoids subtractive cancellation. Zero heat is handled
explicitly: admit `A/B` when A is positive, otherwise zero. An admissible
full request needs no root. The actual transported kinetic-energy expansion
also caps transferred work; a rounded estimate cannot establish admission.

The rounded candidate must pass the existing exact-sign checks for both
heat expansions and both transported-work expansions. At most 64
representable downward backoffs are allowed. Local kinetic/heat/work and
paired momentum accounts retain their original 128-epsilon operand bounds.
No stored material U is withdrawn to fund a correction, and no heat, mass,
trace, pressure, viscosity coefficient, timestep, grid or acceptance norm
is adjusted to obtain admission.

## Range and checkpoint behavior

Square roots operate on the normalized significand and the independent
exponent. Exact floor division and parity work for negative odd exponents
and bounded limb exponents outside int64. Neither roots nor limiters project
tiny positive values into binary64. Unresolved canonical momentum and heat
updates keep their existing explicit diagnostics and precision checks.
Existing exponent allocation limits still reject rather than truncate.

The existing version-2 checkpoint's momentum-enum field stores selector 2.
No prior selector or field is repurposed. Existing readers that do not know
selector 2 reject it; updated readers preserve the complete original bytes.
The default selector and legacy checkpoint behavior remain unchanged.

Build the explicit `ma_mac_quadratic_limiter_tests` target for the new eight
groups. The geometric and owning test executables accept `--quadratic`;
their ordinary invocation retains the legacy controls. Each suite has a
saved-only reviewer under `native/tests`. These experimental targets are
excluded from the default build and do not certify production CTest or
Unity acceptance. Capture fresh bounded outputs and retain first failures.

## Completed component controls

The first component cohort passes 38 groups on Windows/MSVC, Linux/GCC15,
and Linux ASan/UBSan. Eight new groups cover 275 paired corrections, 60
square roots, 15 signed exponent halves, actual additive work budgets,
deterministic unequal-inertia/sign samples, sequential batch conservation,
invalid inputs, and 72 gas/liquid/material/heat/compression checkpoint
continuations. Each platform's saved-only reviewer performs 16,935 checks
with exact fractions and independent 110-digit decimal roots, and rejects
four corrupted limiter, heat, root-exponent and work-expansion controls.

Twelve selected legacy output streams across Windows and Linux are
byte-identical to their preceding accepted captures. Both platforms read
and re-emit the other's new-mode checkpoint exactly. The fixture script's
additional assumption that independently advanced Windows and Linux
producer images must be identical failed; both required cross reads had
already passed. That failed script and both producer images are retained.
No native replay was needed to correct that interpretation. The first
closing script also miscounted the selected swept suite as nine groups;
it contains four. Correcting the count closes the same native captures.

Private evidence: `local/mac-quadratic-limiter-r1`. Terminal-review SHA256:
`8414f752c5bd32b6ff28c479771dc3aa24ef56b88ee2328dd6fd6d3ac48321c7`.

## Completed integrated prerequisites and failed strong gate

Windows, Linux and Linux ASan/UBSan pass 179 groups each: all 149 original
strong-flow prerequisite groups plus 30 new-method controls. All 29 saved
reviews per platform also pass, for 87 reviews. The new geometric suite retains the existing
normal refinement limits of 0.35/0.35 and transverse limits of 0.45/0.45;
its measured ratios are approximately 0.303718/0.283261 and
0.358088/0.400527. Its 3,964 saved checks reject all three geometry/work/
trace corruptions. The new owning suite's 107,210 saved checks reject all
five geometry/work/trace/clock/capacity corruptions.

The first extended reader misclassified older donor records with an absent
mode field. Their native streams match the previous accepted bytes. A
separately frozen correction recognizes only explicit selectors 1 and 2 as
reconstructed; all 29 reviews above use it. No native replay or physical
bound change was made for this reader repair.

The instrumented new geometric suite first hit its 180-second component
budget near the end of the N32 transverse case, with empty stderr. The
legacy suite subsequently hit its 170-second guest timeout under the same
180-second outer budget. Both host captures have complete EOF/closed pipes,
and exact-executable guest probes find no remaining process. Each completes
under an explicitly amended 360-second instrumented budget: 217.55 seconds
for the new mode and 212.11 for the legacy mode. Both saved reviews pass.
Both first timeouts are retained; the strong-flow budget and every numerical
criterion remain unchanged. These timings are execution evidence, not a
performance acceptance result.

After all prerequisites pass, a fresh Windows run of the original strong
benchmark selects only swept material mode 2 and quadratic momentum mode 2.
It passes all three Sod grids, then fails collision density refinement at
N128. Density errors are 0.0428499380630 and 0.0367034003902; the independently
computed density/pressure/velocity refinement ratios are
**0.8565566731 / 0.7951588820 / 0.6110159609** against the unchanged 0.85 limit.
The native run exits 1 in 61.28 seconds, with complete pipes and no timeout.

The saved-only partial review performs 59,205 checks. All five completed
profiles pass their individual species/passive, energy, momentum, entropy
and nominal-timestep criteria. Their joint collision criterion fails.
Collision N256 and all separating grids remain unexecuted, and Linux does
not repeat the shared failed method. The new limiter repairs its demonstrated
over-limiting behavior but does not resolve the collision failure. It stays
opt-in and the defaults remain unchanged.

Private integrated evidence: `local/mac-quadratic-owner-r1`. Readiness SHA256:
`112f8bfdb152b438af4b79f4e050ac3f3e8b9c034a03e47c24f162a507ecf8f2`.
Failed strong terminal-review SHA256:
`300eb23c1b74566e8d60c23c01c8080a32e331bea28cf9539226c9ee991b856a`.
No production integration, game screenshot, performance, exact-vacuum or
shipping acceptance follows from these standalone controls.
