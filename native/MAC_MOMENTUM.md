# Face momentum reconstruction

Donor-cell [MAC transport](MAC_TRANSPORT.md) moves normal face momentum J at first
order. `MacClosedSettings::momentum_reconstruction` adds conservative higher-order
corrections between adjacent dual cells, paid for by compatible kinetic/internal
energy exchange. Changing the selector changes J, K and the heat or work deposited in
U; constituent and passive transfers and the 128-epsilon account rule are the same
for every selector.

Code: `mac_momentum_limiter.hpp`/`.cpp`, the momentum paths in `mac_transport.cpp`,
`mac_swept_momentum.hpp`/`.cpp`.

## Selectors

| Value | Name | Status | Summary |
| --- | --- | --- | --- |
| 0 | `donor_cell` | Reference; standalone transport default | Upwind J and K only |
| 1 | `limited_linear` | Owner default | Geometric predictor with the sufficient linear heat limiter |
| 2 | `quadratic_limited_linear` | Opt-in | Same predictor with exact quadratic heat admission |
| 3 | `swept_physical` | Opt-in | Swept dual geometry, signed remap work, completed-state admission |
| 4 | `swept_energy_flux` | Opt-in | Selector 3 with the carried kinetic-energy flux bounded separately |
| 5 | `swept_nodal_1d` | Opt-in, 1-D only | Conservative point-to-dual conversion with compatible kinetic work carried by the material |

Selectors 3 and 4 fix the two defects found by the
[affine remap diagnosis](#affine-remap-diagnosis). Selector 4 with material selector 2
and compression mode 3 is the most accurate tracked combination; it still fails the
[strong-flow gate](MAC_STRONG_FLOW.md), and an open pressure/remap
[convention mismatch](MAC_STRONG_FLOW.md#curved-deformation-pressure-and-remap-convention)
affects selectors 3 and 4.

The [one-dimensional nodal conversion](MAC_NODAL_CONVERSION.md) repairs the
interior curved point-velocity oracle with the original momentum/energy source.
It is selector 5 and retains the original completed-state admission. General 2-D
conversion and the strong-flow gate remain open; its bounded separation probe
improves velocity error but worsens density/pressure error. It is not promoted.

## Compatible pair correction

A correction moves impulse d from the left dual cell to the right. With inertias
I_L, I_R and `u = J/I`, the compatible work is `W = (u_L + u_R) d / 2` and each
side's mixing remainder changes by

```
Q'_L = Q_L + (u_L - u_R) d / 2 - d^2 / (2 I_L)    (and likewise with I_R)
```

so each side obeys `Δ(K + Q) = −W / +W`. Q is the nonnegative upwind mixing remainder
before deposition into material U. No stored internal energy is withdrawn and no
floor is introduced. The need for such a balance in staggered schemes is discussed by
[Herbin, Latché and Nguyen](https://numdam.org/articles/10.1051/m2an/2017055/).

For a fraction a of the requested correction, the heat change is `a A − a² B` with
B ≥ 0. Selector 1 uses the sufficient constraint `Q + a (A − B) ≥ 0`, taking the
minimum of 1 and each positive `Q / (B − A)` in full significand/exponent arithmetic.
An exact zero budget can impose zero correction. Up to 64 significand ULP backoffs
handle a rounded boundary; when a backoff crosses 0.5 the significand doubles and the
full exponent decrements. Unresolved positive updates are reported.

Inside transport the limiter also borrows the actual transported-energy expansions of
both sides, so the coefficient is bounded by outward work capacity as well as heat;
the backoff loop checks both exact remaining work sums before accepting. Standalone
pair and batch callers without a work budget keep the heat-only behaviour.

## Limited linear predictor (selector 1)

Source face velocities are `J/I`. Each free dual face has minmod normal and
transverse slopes; stencils that touch fixed faces are flat in that direction.
Centred Courant numbers are half the sum of the two oriented mass fluxes divided by
source inertia. The face prediction is half the upwind normal slope minus half the
sum of both Courant-times-slope terms, and its change from the donor is bounded
between the donor and target source velocities. No quantity is projected to binary64
to form a slope, predictor or correction.

The oriented correction is mass flux times the bounded velocity change. Each
free/free edge applies the pair limiter to the current upwind J and pending heat.
Fixed face J, wall impulse and wall heat are untouched. Transported kinetic energy
starts from the actual upwind accumulation and receives the paired −W/+W terms.
Reconstructed material parcels supply the actual mass fluxes.

Each face's final energy account carries the upwind bound, the bound of every nonzero
correction and the transported-work storage bound. Cancellation must not erase
roundoff already incurred: in one face the incoming transported K was 7.45e6 times its
final value; a bound from the final magnitudes alone (2.5e-57 J) was far below the
actual residual (1.2e-52 J), which lies within the sum of stage bounds (5.5e-50 J).

## Quadratic admission (selector 2)

The sufficient linear bound can reject admissible corrections. For `I_L = I_R = 1`,
`J_L = 1`, `J_R = 0`, `Q_L = Q_R = 0` and `R = 2` it returns zero, while θ = 1/2 gives
momenta 0 and 1 with both heats zero and work 1/2 moved left to right. Selector 2
solves the actual quadratic constraint `Q + θ A − θ² B ≥ 0` per side, with
`A = R (u_L − u_R)/2` and `B = R² / (2 I)`, capped at one:

```
A <= 0: theta = 2Q / (sqrt(A^2 + 4BQ) - A)
A >  0: theta = (A + sqrt(A^2 + 4BQ)) / (2B)
```

The first form avoids cancellation. With zero heat it admits `A/B` when A > 0 and zero
otherwise. Transported kinetic energy also caps the work. The rounded candidate must
pass exact-sign checks of both heat and both work expansions, with at most 64
downward backoffs. Square roots act on the normalized significand and an exactly
halved exponent (floor division valid for negative odd and out-of-int64 exponents).
Selector 2 does not change the strong-flow collision result.

## Affine remap diagnosis

`ma_mac_dual_affine_diagnostic` runs 24 cases (two axes, four controls, selectors
0/1/2): ten cells along the flow, two periodic transverse rows, fixed end faces, unit
spacing, constant density, 300 K nitrogen with helium at mass ratio 1e-28 and a
passive at 1e-30, and the swept material reconstruction. The controls are a stretched
grid with interior face displacements `(f − 1)/64` and velocity `u(x*) = 1/4 + x*/8`,
uniform translation by 1/8 with the same velocity, the stretched grid at constant
velocity 1, and zero sweep. Faces 4–6 have equal source spacing on both sides, so the
geometric and fixed-grid interpretations agree there.

**Geometry.** With `S = 65/64`, `d = S − 1` and `b = 1/8`, the upwind momentum at unit
density is `J_up = u_fixed − b d q_f + b S d/2`. The predictor's outgoing correction is
`R_right = (q_f + d/2) b (S − q_f)/2` and the incoming one uses `q_left = q_f − d`, so

```
J_unlimited = J_up + R_left - R_right = u_fixed - b d^2/4 = u_fixed - 1/131072
```

That 7.63e-6 bias (allowance 4.97e-14) appears on both axes and OSes for selectors
1 and 2. For uniform stretch the swept dual boundary moves by `q_f + d/2`, whose slab
centroid differs from the predictor point by −d/4.

**Admission.** In the translation fixture, selector 2's unlimited candidate is
affine-exact, but the admitted result differs by 1.84e-5 (limiter 0.997). Applying
every requested correction instead, then rounding J and U once, passes every physical
check: transported K stays nonnegative, the stored total-energy residual is exactly
zero against a 9.9e-8 J allowance, momentum accounts pass, entropy rises, and
temperatures stay within the EOS domain. Two face remainders are negative (minimum
−2.34e-5 J). The nonnegative-remainder rule is therefore stricter than the physics
requires; the replacement must keep positive upwind variance, represent signed remap
work separately and admit the completed state.

The diagnostic exits 0 after complete collection; that is not an affine pass.

## Swept physical momentum (selector 3)

The reconstructed parcel path uses the actual parcel volumes; the raw donor path
derives a volume sweep from donor mass and source volume. Reconstructed parcel mass
never substitutes for an independently supplied volume. The same half-cell topology
gives dual volume sweeps, whose time-centred first moments define source dual
centroids and swept slab centroids. Least-squares gradients use physical centroid
distances with neighbour bounds; true one-dimensional axes have explicit rank-one
stencils. Unresolved geometry keeps a reported first-order fallback. Fixed half dual
cells keep the donor wall treatment and get no reconstructed impulse. These are
discrete swept first moments, not a general polygon-overlap proof.

For each free edge, removing R from left J and adding it to right J has compatible
paired work `W = (u_left + u_right) R / 2` and per-side signed work-energy change
`(u_left − u_right) R / 2 − R² / (2 I_side)`. `MacFaceEnergy.remap_work_energy` stores
it separately from the nonnegative upwind variance and wall heat, and it is deposited
into stored U. An edge limiter keeps transported kinetic energy nonnegative, including
at a stationary face with nothing to transport; the combined variance/work remainder
may be negative.

After both components and the material energy deposition, the candidate must pass the
species/passive, momentum/wall, stored U+face-K, EOS-domain and entropy conditions
(128 epsilons of the actual endpoint operands; chemical reference energy may be
negative). A rejected candidate triggers a common correction-fraction search without
changing material parcels, timestep, grid or coefficients; even the zero-correction
fallback must pass. Reports keep the correction fraction, rejection count, per-edge
limiter, signed work, centroids, gradients and work events.

`ma_mac_physical_admission_tests` exercises an actual intermediate admission. In a
deformed 2-D remap the unrestricted candidate reaches 299.9620683 K and the donor
fallback stays above 300.0009458 K. A fixture-only EOS lower bound of 300 K selects
correction fraction 0.3356288673 (30 rejected candidates on Windows, 28 on Linux).
The result keeps the original 32-epsilon EOS allowance; nothing is clipped or warmed.

## Bounded carried energy (selector 4)

In the separation benchmark, selector 3 cancelled every outward correction at the two
stationary central dual faces: they had zero transported kinetic energy, so the
edge limiter zeroed the impulse ([details](MAC_STRONG_FLOW.md#separation)). Selector 4
keeps the full impulse and limits only the carried energy.

For geometric request R and completed-state fraction θ, let `dJ = θ R`, `ul`, `ur` the
pre-correction velocities and `Il`, `Ir` the final dual inertias. The unrestricted
carried energy is `F* = (ul + ur) dJ / 2`. Only F is restricted to the available
nonnegative transported kinetic energy, using the per-edge limiter and rounding
backoffs. With `D = F* − F`:

- `Wl = (ul − ur) dJ / 2 − dJ² / (2 Il) + D`
- `Wr = (ul − ur) dJ / 2 − dJ² / (2 Ir) − D`

so `ΔKl + Wl + F = 0`, `ΔKr + Wr − F = 0`, pair momentum is conserved and the carried
energy stays nonnegative. In selector 4, `transported_work_limiter` multiplies
carried energy; in selector 3 it limits the impulse.

When F is limited, each side is evaluated directly in one additive expansion,
`Wl = ul dJ − dJ²/(2 Il) − F` and `Wr = −ur dJ − dJ²/(2 Ir) + F`. The velocity-difference
form cancelled tiny work at a cold stationary face (impulse 3.0e-38 kg m/s, kinetic
energy 1.5e-71 J, intermediate terms 7.3e-53 J) and failed the paired-work guard by a
factor of 2^45. Unrestricted events keep the velocity-difference form.

Conservative remap of momentum and carried kinetic energy generally needs an
internal-energy reconciliation; see
[Bailey et al.](https://laro.lanl.gov/esploro/outputs/journalArticle/Reduced-dissipation-remapping-of-velocity-in-staggered/9916363418903761).
The formulation above is this implementation's.

## Verification

- `ma_mac_momentum_limiter_tests` (six groups): analytic pairs and both signs,
  positive/zero budgets, reflection and an exact 1024 m/s Galilean shift,
  below-binary64 impulses, one-period scalar advection, invalid inputs.
- `ma_mac_momentum_reconstruction_tests` (six groups): uniform-velocity identity with
  varying composition, wet parcels with a solid corner, below-binary64 J, invalid
  input rejection, and two refinement controls (both with nonnegative heat, positive
  traces, EOS-admissible U and the endpoint energy account):

| Control | Coarse L1 | Middle L1 | Fine L1 | Ratios (limit) | Fine / donor (limit) |
| --- | --- | --- | --- | --- | --- |
| Normal, N32/64/128, C = 0.2, `u = 1 + 0.2 sin` | 0.0107951145673 | 0.00327739832241 | 0.000927664717293 | 0.304 / 0.283 (0.35) | 0.063 (0.3) |
| Diagonal shear, N8/16/32, dt = 0.1/N | 0.0831404939461 | 0.0297882482862 | 0.0119282835528 | 0.358 / 0.400 (0.45) | 0.220 (0.4) |

- `ma_mac_momentum_owner_tests` (seven groups): owner equals component replay on
  nonuniform wet parcels; donor material with reconstructed momentum, a solid corner
  and gravity; two substeps equal direct replay; donor mode changes only J; below-range
  J; invalid mode and late-budget rollback; transported-work capacity (a budget of
  1 − 2^-54 backs off, a positive 0.75 · 2^-1200 capacity keeps a nonzero impulse, zero
  capacity is an exact no-op).
- `ma_mac_quadratic_limiter_tests` (eight groups): 275 paired corrections, roots and
  signed exponent halves, work budgets, unequal-inertia samples, batches, invalid
  inputs and 72 checkpoint continuations. `--quadratic` runs the reconstruction and
  owner suites with selector 2; their refinement limits are unchanged.
- `ma_mac_swept_momentum_tests` (six groups): 138 actual affine readings over 24 cases
  (both axes and signs, stretch, translation, uniform velocity, zero sweep,
  `q = dt u`, below-binary64 momentum, both one-dimensional axes); `--owner` runs 72
  checkpoint continuations and the EOS boundary controls.
- `ma_mac_bounded_energy_tests` (eight groups): 32 affine cases and 186 readings,
  four interior stagnation controls (selector 3 fails eight outward readings), four
  tiny-transfer controls on both axes and signs (the original libraries fail them);
  `--owner` runs 96 continuations.
- Swept and bounded-energy checkpoint images round-trip across platforms byte for
  byte and continue exactly.

## Reproduce

```sh
cmake --build build --config Release --target ma_mac_momentum_limiter_tests \
  ma_mac_momentum_reconstruction_tests ma_mac_momentum_owner_tests ma_mac_quadratic_limiter_tests \
  ma_mac_dual_affine_diagnostic ma_mac_swept_momentum_tests ma_mac_bounded_energy_tests \
  ma_mac_physical_admission_tests
build/ma_mac_momentum_limiter_tests > limiter.stdout
python3 native/tests/review_mac_momentum_limiter.py limiter.stdout limiter.json
build/ma_mac_dual_affine_diagnostic > affine.stdout
python3 native/tests/review_mac_dual_affine.py affine.stdout affine.json
python3 native/tests/review_mac_dual_affine_energy.py affine.stdout affine-energy.json
build/ma_mac_bounded_energy_tests > bounded.stdout
python3 native/tests/review_mac_bounded_energy.py bounded.stdout bounded.json
build/ma_mac_physical_admission_tests --bounded-energy > admission.stdout
python3 native/tests/review_mac_physical_admission.py admission.stdout admission.json --bounded-energy
```

The other suites pair with `review_mac_momentum_reconstruction.py`,
`review_mac_momentum_owner.py`, `review_mac_quadratic_limiter.py` and
`review_mac_swept_momentum.py`, each taking the stdout and a new report path.
`review_mac_dual_affine.py` derives the unlimited candidate from the upwind J and
each requested correction; `review_mac_dual_affine_energy.py` computes the
full-correction counterfactual. The swept readers rebuild dual first moments, slab
predictors, upwind transport, every compatible-work event, signed material deposition,
EOS and entropy from saved operands.
