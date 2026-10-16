# Closed-gas Riemann gate

The gate remains failed. The latest Windows reconstructed-momentum owner
completes Sod N64/128/256 and satisfies collision velocity refinement, then
fails collision density and pressure refinement at N128. Linux passes all 85
prerequisite groups and does not repeat that known failure. Earlier
compression, velocity and range failures remain recorded below.
The first-source failures remain recorded below. The passing smooth-wave/gravity controls in
[reconstruction evidence](MAC_RECONSTRUCTION.md) do not establish shock or
strong-expansion behavior. The original gate introduced tests/build declarations;
the later numerical changes and their results are recorded by cohort below.

The reference is the ideal-gas Euler Riemann solution, using shock jump curves,
isentropic rarefactions and a common pressure/velocity across the contact.
See the primary [Clawpack Riemann book](https://www.clawpack.org/riemann_book/html/Euler.html).
The two reference implementations below are separate code; that source does
not certify this numerical method or its entropy behavior.

All cases use nitrogen with a conserved helium mass ratio of 1e-28 and a second
positive passive ratio of 1e-30. Gamma comes from the explicit constant-capacity
material coefficients; it is not rounded to 1.4. Two identical transverse rows
are periodic vertically, with rigid horizontal ends, grid spacing 1/N and unit
depth. Cell mass, actual stored U and normal face J remain primary. There is no
vacuum cell or phase interface in these controls.

The three problems start at x=0.5:

| Case | Left (density kg/m³, velocity, pressure Pa) | Right | Duration |
| --- | --- | --- | --- |
| Sod | (1, 0, 100000) | (0.125, 0, 10000) | 0.2 / sqrt(100000) s |
| Collision | (1, +1.5 c₀, 100000) | (1, −1.5 c₀, 100000) | 0.06 / sqrt(100000) s |
| Strong separation | (1, −4 c₀, 100000) | (1, +4 c₀, 100000) | 0.03 / sqrt(100000) s |

Here c₀ is the left initial sound speed. The separating solution retains
strictly positive pressure/density; it approaches vacuum without testing an
exact vacuum boundary. For Sod, compare the whole unit interval before waves
reach the walls. For collision/separation, compare x in [0.25, 0.75] before
wall-generated waves enter that window. Global mass, impulse, energy and
entropy checks still cover the complete closed grid, including its walls.

The fixed grid sequence is N64/128/256. Each nominal timestep is
`0.25 h / (q c₀)`, with q=2, 2.5 or 5 respectively. A profile must finish with
no rejected attempts and at most ceil(duration/dt)+2 accepted steps. Each
successive density, pressure and velocity L1 error must be less than 0.85 of
its predecessor. At N256 every normalized error must be below 0.06. The
separating case additionally requires both central cells' density and pressure
errors below half their exact cell averages. This prevents the low-pressure
core from disappearing inside a small domain-averaged error.

Density and pressure compare cell averages; velocity compares the normal face
reading. Errors are normalized by initial left density, pressure and sound
speed, and by the comparison-window length. Native reference cell averages use
eight-point Gauss quadrature split at every exact wave boundary. The data-only
reviewer independently brackets the star pressure in 80-digit arithmetic and
integrates the fan power laws analytically. It also reconstructs the ideal
mixture EOS and entropy from masses, U and volume.

Every bulk/trace/passive inventory must stay positive. Exact binary64-fraction
accounts cover each constituent/passive total, both face momentum components
and actual U+face-K. The runtime reports wall impulse on the fluid, so momentum
change must equal that impulse. The native interval's original energy
allowance stays unchanged. Total thermodynamic entropy may decrease only
within 128 binary64 epsilons of the actual stored-U/temperature operands.
The reviewer must reject separately altered trace, wall momentum and reported
pressure-error records.

The first strict Windows/MSVC and Linux/GCC15 build/runtime/review results are
retained, including any failure. Predeclared bounds are aggregate build 180 s,
native/review 180 s each and 2 MiB per raw pipe. These are bounded correctness
runs, not performance measurements. A failed native gate is not retried without
a relevant algorithm repair; partial saved-data review cannot certify the gate.

No multidimensional shock, elastic-liquid impact, exact vacuum/free surface,
immiscible interface, phase, production performance or game acceptance follows
from this gate. Earlier failed physical gates remain separate evidence.

## Current reconstructed-momentum owner: density/pressure refinement failure

The [owning timestep integration](MAC_MOMENTUM_OWNER.md) passes all 85 strict
Windows/Linux native groups and their saved reviews before this one
changed-source Windows retry. Source, timestep, grids and every scientific
bound in the original Riemann and gravity tests remain unchanged.

| Case | Grid | Density L1 | Pressure L1 | Velocity L1 |
| --- | --- | --- | --- | --- |
| Sod | 64 | 0.0106727979090 | 0.00746236246693 | 0.0166702480559 |
| Sod | 128 | 0.00644484914260 | 0.00434731984663 | 0.00976405151286 |
| Sod | 256 | 0.00335375430713 | 0.00219319568397 | 0.00472756956501 |
| Collision | 64 | 0.0447502618538 | 0.0551942800520 | 0.0661799908479 |
| Collision | 128 | 0.0397925973197 | 0.0742963811250 | 0.0434654060056 |

Collision N128/N64 ratios are **0.88921485 / 1.34608842 / 0.65677564**.
Density and pressure exceed the original 0.85 limit; velocity passes. The
native gate stops before collision N256 or any separation profile. The
separate partial review checks 59,205 saved identities across five completed
profiles, including individual traces, actual U+face-K, entropy and both
momentum/wall accounts. It is not the complete three-group review and runs no
full-gate corruption controls. No full strong-flow acceptance is claimed.

Saved-only regional analysis reproduces the failed norms without another
native run. Pressure-error contributions (unshocked / shock-cut / compressed)
are 0.025911 / 0.007177 / 0.022107 at N64 and
0.024955 / 0.026951 / 0.022390 at N128. Central specific entropy exceeds the
exact compressed state by about 100.17 / 96.31 J/(kg K), with central density
about 91.55% / 90.94% of the exact value. These locate unresolved shock-profile
and stagnation errors; they do not justify changing the oracle or bounds.
The CSV/analysis are private under `local/mac-momentum-owner-collision-analysis-r1`.

Private first native/partial records are under
`local/occupied-staggered-validation/mac-momentum-owner-strong-windows-r1`,
failed terminal SHA256
`441a3c6eac53c0367f1512e4feb30ffc8647c993941a0cc7ea942908e2cb8cd8`.
The default game DLL is unchanged. Linux does not repeat this shared failure.
No independent agent, Unity or production/performance acceptance is claimed.

### Logging-only localization of collision entropy

A separate strict Windows diagnostic logs the six states of each accepted
attempt: source, first compression half-step, pressure at actual material
volumes, remap before heat, transport heat, and final compression half-step.
Removing exactly the inserted logging restores the owning implementation,
including arithmetic order, retries and bounds. Its private driver selects
only the existing collision N64/N128 profiles; the repository scientific test
is unchanged. This diagnostic does not rerun or pass the strong-flow gate.

Both final cell records, all face momenta and the full interval reports are
bit-identical to the retained failed run. The saved reviewer uses the original
80-digit oracle, checks the central samples' EOS/entropy independently, and
checks the chronological stage chain, actual aggregate heat operands and
endpoint identities. There are 46 and 91 accepted substeps respectively.

| Contribution to global entropy increase | N64 | N128 |
| --- | ---: | ---: |
| Both compression half-steps | 64.41% | 66.26% |
| Pressure work | Roundoff | Roundoff |
| Material remap before heat | 14.34% | 14.91% |
| Transport heat | 21.24% | 18.82% |

The pressure stage's accumulated entropy change is -2.77e-13 / -1.62e-13
J/K. These closed-domain totals include wall regions, and the transverse
domain height is 2/N; they are not a direct measure of excess shock entropy.
Entropy production is required at a shock, so the table alone does not
identify a faulty dissipative term.

At both grids, the left central fixed-cell sample changes from -970.2291 to
-852.6267 J/(kg K) after one complete substep and -795.8975 after two. The
exact compressed state's specific entropy is -822.3579 J/(kg K). Pressure
preserves the sample's specific entropy, while compression and transport heat
increase it. Fixed-cell readings include advected material and are not parcel
production rates. Together with the persistent final central density deficit,
this suggests investigating heat distribution and shock coupling rather than
changing pressure-root tolerance or the acceptance bounds.

This pattern is consistent with the excess heating on shock formation
investigated by [Noh (1987)](https://www.sciencedirect.com/science/article/pii/002199918790074X).
That paper compares viscosity alone with an additional artificial heat flux;
it does not validate this solver or establish that such a change will satisfy
its gates. Any proposed thermal redistribution still needs compatible energy,
positive-temperature, entropy and stationary-contact controls before a new
strong-flow retry. No numerical coefficient or physical criterion changes in
this diagnostic.

Private cohort `local/occupied-staggered-validation/mac-collision-stage-diagnostic-windows-r1`:
source-freeze SHA256
`1d41ed241dc732e42550a2052f0815033b4f64f3afde31fde017fa19b7fa36ee`,
terminal SHA256
`e29e132eeda4e6109abd7bbd4c9f3aa7bb70863d2e34a71be64381db549fac06`.
The 647,006-byte native stdout closes normally within its original 2 MiB
diagnostic cap; native stderr is empty. The default game DLL is unchanged.
This adds no Linux, Unity, production or performance acceptance.

## Earlier symmetric compression owner: pressure-refinement failure

The subsequent [symmetric coupling](MAC_COMPRESSION_COUPLING.md) passes all 66
native groups and saved reviews on Windows/Linux, including a manufactured
temporal control and actual endpoint entropy. Its one Windows retry completes
Sod but retains collision pressure ratios 0.817665/1.380639/0.633633 for
density/pressure/velocity. The partial reviewer checks 59,205 identities over
five profiles; no collision N256, separation case or full review follows.
Linux does not repeat the known shared failure. The post-only cohort below is
retained; a spatial accuracy repair remains required before another retry.

[Compatible compression](MAC_COMPRESSION.md) passes six component groups and
four new owning-interval groups; all 65 native groups and their saved reviews
pass on Windows/Linux. The default owner adds positive compression heat and wall
impulse before publication. A newly exposed small wall-reaction precision failure
is repaired by cancelling boundary pressures before their common scale factors.
The 128-epsilon bound, EOS, original scientific sources and all error limits
remain unchanged. An old primitive-comparison control explicitly selects the
reference mode; new owner controls verify the active default and rollback.

After all prerequisites, one Windows strong retry completes Sod refinement with
fine errors 0.00414986826735/0.00335348387977/0.00625419909291. Collision
N64/N128 density errors are 0.0528314817013/0.0429871612094, pressure errors
0.0581114509839/0.0804701217318 and velocity errors
0.0757790079357/0.0479075695783. Ratios 0.813666/1.384755/0.632201 now pass
velocity but fail pressure. No collision N256 or separation case runs.

The partial saved-data reviewer checks 59,205 identities over five completed
profiles and reproduces the failed pressure norm. Original mass, traces,
passives, U+K, momentum/wall, EOS and entropy checks pass individually. No full
strong-flow review or partial-review corruptions follow. Data-only localization
places 99.35%/91.43% of pressure error within four cells of an exact shock at
N64/N128. Linux does not repeat the known failed gate. A genuine accuracy repair
is required before another retry; no oracle or criterion changes are accepted.

## Exponent cohort: collision-velocity failure

[Bounded large integer exponents](MAC_EXPONENT_RANGE.md) pass six new groups,
8,560 saved checks and all 55 native prerequisite groups on Windows/Linux,
including Linux ASan/UBSan checks of the new controls. After those pass, one
Linux strong-flow retry completes Sod N64/128/256 with original criteria and no
rejected attempts. Its fine-grid density/pressure/velocity errors are
0.00419136756470/0.00322154541957/0.00532509993547.

Collision N64/N128 then reproduce the Windows failure: velocity L1 grows from
0.043892229516 to 0.048944416778, ratio **1.1151043663** versus the original 0.85
bound. A partial saved-data review checks 59,205 identities across five profiles
and reproduces the failed norm. One full native Sod subgroup passes; no full
strong-flow review, collision N256 or separation case follows. Windows' failed
gate is not repeated. Data-only localization finds oscillations behind the
shocks while retaining the point-velocity metric. Collision accuracy requires
a real numerical repair before either platform repeats this failed gate.

## Canonical-range cohort: exponent-capacity rejection

[Full canonical/flux range](MAC_CANONICAL_RANGE.md) passes five new Windows/Linux
groups, 7,040 saved checks and all prerequisite controls/reviews. The Linux retry
completes Sod N64; a partial data review checks 5,996 identities and reproduces
density/pressure/velocity errors 0.0125756323775/0.00981171744507/0.0170505310651.
Sod N128 then requests exponent -3049816383 in a scaled share, beyond the
representation's signed 32-bit range. The original guard rejects before
overflow. No complete refinement group or full strong-flow review passes.
A first-rejection diagnostic changes no arithmetic, source input or bound.
The later exponent repair above addresses that range failure. The Windows
failed collision gate is not replayed by that repair.

## Momentum-contribution cohort: canonical storage rejection

[Scaled momentum contributions](MAC_MOMENTUM_PARCELS.md) pass four new groups,
3,391 saved checks and all prerequisite reviews on Windows/Linux. The Linux
retry passes incoming sharing but fails when storing about 3.615258007097827e-585
kg·m/s on open x face 45, whose prior J was zero. The unchanged binary64 primary
state cannot represent it. No Sod profile completes, no full/partial profile
review follows, and no Windows collision retry occurs. A coherent wider
canonical representation and a collision-accuracy repair remain required.

## Primary-parcel cohort: incoming momentum rejection

[Scaled primary parcels](MAC_PRIMARY_PARCELS.md) pass five new Windows/Linux
groups, 7,681 saved checks and all transport/closed/gravity prerequisites.
The Linux retry passes the earlier helium-parcel creation and then rejects an
incoming normal momentum contribution of 3.615258007097827e-585 kg·m/s before
any completed Sod profile. Its primary J share still narrows before addition.
The first-rejection diagnosis changes no arithmetic or scientific criteria.
No full/partial profile review follows, and no Windows collision retry occurs.
Full strong-flow acceptance still needs momentum-range and collision-accuracy work.

## Derived-energy cohort: primary transfer rejection

[Wide derived-energy integration](MAC_ENERGY_TRANSPORT.md) passes four new
Windows/Linux controls and repeats all range, transport, parcel, closed-owner
and smooth-gravity prerequisites with separate reviews. After those pass, the
Linux strong-flow retry uses the unchanged scientific source and physical
limits. It no longer rejects a derived K parcel, but stops before any completed
profile at a primary helium transfer of 8.5868194183540713e-325 kg. The existing
positive-transfer check rejects its zero nearest binary64 projection. The
diagnosis retains every input and changes no arithmetic or physical bound.

No full strong-flow review follows that empty profile set, and the Windows
failed collision gate is not repeated. The later primary parcel repair above
addresses this failure mechanism; strong-flow acceptance remains open.

## First-source results: strong-flow gate failed

Fresh strict builds pass on Windows/MSVC and Linux/GCC15. Both first native
captures exit 1 with normal EOF, no timeout and no pipe overflow. The numerical
source remains the accepted reconstruction/gravity source; this new benchmark
does not change it. Scientific inputs, timestep, grids and physical error
bounds were fixed before either run and remain unchanged.

Windows completes all three Sod grids and the first group. Density, pressure
and velocity errors decrease jointly, with finest errors below 0.006. It then
fails at the collision N128 velocity ratio, before reaching collision N256 or
any separating-flow case:

| Case | Grid | Density L1 | Pressure L1 | Velocity L1 |
| --- | --- | --- | --- | --- |
| Sod | 64 | 0.01257563238 | 0.009811717445 | 0.01705053107 |
| Sod | 128 | 0.007782443342 | 0.005895791019 | 0.009963770827 |
| Sod | 256 | 0.004191372723 | 0.003221544983 | 0.005325098892 |
| Collision | 64 | 0.09910923925 | 0.2769363196 | 0.04389222952 |
| Collision | 128 | 0.05141981979 | 0.1495878824 | 0.04894441678 |

Collision velocity error grows by **1.11510437**, failing its unchanged 0.85
bound. Density and pressure decrease, but the joint criterion fails. A
data-only partial review checks 59,205 saved operands/EOS/profile identities
across these five profiles and independently reproduces that failed norm.
Constituent/passive totals, actual U+face-K, entropy and both momentum/wall
accounts pass. It is not the full three-group review; corruption controls are
not run and no cross-platform shock acceptance is claimed.

The first partial reviewer had the wall-report sign reversed and rejected the
Sod momentum balance. Its failed result and the first diagnostic output remain
retained. Correcting the report convention to `delta J = wall impulse on fluid`
permits checking the same saved data; no native run or physical bound changed.

Linux stops before completing Sod N64. A separate instrumented first-rejection
diagnosis identifies the derived dual kinetic-energy parcel:

`7.6902724379882565e-290 * 3.6197301863874292e-151 / 3.0517578125e-5`

Its independently calculated value is **9.1215335539767185e-436 J**, below the
binary64 range. The corresponding positive momentum transfer executes before
the derived-energy rejection. No constituent, passive or momentum is clipped.
The full scientific gate was not repeated. The first diagnostic build had an
unused helper after replacing its main with a single-case diagnostic; it
failed under `-Werror` before any diagnostic native launch. Marking that private
helper unused retains the strict warnings and changes no arithmetic.

The next work is to represent derived kinetic/variance energy across its
required exponent range, without dropping primary mass, traces or momentum,
and to investigate the collision velocity error before changing the transport
operator. Neither failure is fixed by loosening the limits or changing the
initial grid/timestep. The positive smooth-wave/gravity results remain scoped
as before.
The later [derived-energy range component](MAC_ENERGY_RANGE.md) and its
[transport/owner integration](MAC_ENERGY_TRANSPORT.md) address that derived K
mechanism. The original failed source remains retained; the current primary
transfer rejection and unresolved Windows collision still prevent acceptance.

Private first results are
`local/occupied-staggered-validation/mac-riemann-{windows,linux}-r1`.
Failed terminal-review SHA256 values are
`ff46328aa3c6615fe93c841e2faa65db1860b6b9a52ae18d415e463eacea52b1`
(Windows) and
`41c7ae932e795e9060dbfabaa9e66eb1754c025a3509877e20df9188b6375733`
(Linux). They bind frozen sources, compiler approvals, raw results and the
corrected partial/first-rejection diagnostics. No independent agent review,
Unity launch or production/performance acceptance occurred. The default game
DLL is unchanged.
