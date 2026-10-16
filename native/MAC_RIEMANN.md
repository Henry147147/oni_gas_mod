# Closed-gas Riemann gate

The gate remains failed. Windows collision velocity refinement is unresolved;
the latest Linux retry rejects accumulated canonical momentum below binary64 range.
The first-source failures remain recorded below. The passing smooth-wave/gravity controls in
[reconstruction evidence](MAC_RECONSTRUCTION.md) do not establish shock or
strong-expansion behavior. This gate changes tests and build declarations;
the numerical solver remains the source accepted in that cohort.

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

## Current Linux momentum repair and canonical storage rejection

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
