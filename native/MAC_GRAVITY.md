# MAC gravity coupling

The repaired four-group gravity gate passes on Windows and Linux. The original
donor-cell refinement failure remains recorded below. Constant downward gravity
extends the closed occupied MAC owner. It remains excluded from game
dispatch. Nonzero gravity rejects vertical periodicity because its potential
cannot be periodic. The original default remains zero gravity.

The material pressure step applies the body force to the same retained normal
face inertia as its pressure force. With represented midpoint velocity, its
body work is `W_f = -dt*g*I_f*u_mid`. MAC transport uses its actual constituent
mass flux `F_f`; potential-energy transport across a vertical face is
`D_f = F_f*g*dx`. Their difference `-(W_f+D_f)` is a signed internal-energy
adjustment distributed by adjacent target half-mass shares. It is reported
separately from physical boundary heat, upwind mixing and wall-projection heat.
U and J remain primary quantities; neither is rebuilt from scalar total energy.

Pressure U+K minus body work, transport U+K, stored internal adjustment and
potential transport each have separate 128-epsilon actual-operand checks.
Their bounds sum to the final U+K+PE allowance. Unstored signed U changes are
counted. Entropy change and its stored-U rounding allowance are reported; energy
closure alone is not an entropy certificate. No entropy correction/clamp is
applied. The relationship between gravity work and actual face mass flux is
discussed by [Springel (2010), sections 5.2–5.4](https://academic.oup.com/mnras/article/401/2/791/1147356).
The split, face distribution and tests here are this implementation's choices,
not a reproduction or validation by that paper.

The predeclared controls are two fully exported gas/wet-corner gravity cases,
gas/liquid/layered hydrostatic rest, a stratified small vertical acoustic mode,
and invalid gravity/topology plus whole-interval rollback. Hydrostatic maximum
speed must stay below 1e-7 m/s. The mode uses an isothermal scale height of one,
constant-capacity ideal nitrogen with helium trace, rigid vertical ends and a
0.05-m/s initial velocity. Its linear displacement is proportional to
`exp(y/2)*sin(pi*y)*sin(omega*t)`, with
`omega=c*sqrt(pi*pi+1/4)`. At a quarter period, compare cell-average pressure and
face velocity on N32/64/128 grids. Nominal timesteps number N/4; no retry may
replace that path. Joint pressure/velocity error ratios must be below 0.7,
absolute specific-entropy-change ratios below 0.8, and finest normalized errors
below 0.02 with absolute specific entropy change below 1e-6 J/(kg K).

The saved-data reviewer reconstructs Fraction mass, impulse, body work, gravity
adjustment and U+K+PE accounts. It independently solves the EOS/isentropes in
80-digit arithmetic and integrates the continuous linear stratified mode.
Three altered potential/work/profile records must reject. The prior eight
zero-gravity groups and reviewer are repeated after this solver change.

This smooth-wave/finite-conservation scope does not establish shock accuracy,
nonlinear buoyancy at arbitrary density jumps, general entropy stability,
free surfaces, phase transitions, production performance or colony ownership.
The earlier remap, vacuum and performance failures remain unchanged.

## Reconstructed transport and nested-root repair

The owner now uses [shared limited material reconstruction](MAC_RECONSTRUCTION.md)
for constituent masses, actual U and passive inventories. The actual parcel
masses still drive dual momentum transport and gravitational potential work.
Normal face momentum retains its previous donor-cell transport; this is not
a general second-order nonlinear solver claim.

Fresh R4 strict Windows/MSVC and Linux/GCC15 builds pass all four groups and
the full 16,458-check saved-data review, including three deliberately corrupted
potential/work/profile records. The scientific gravity source file and fixture
initialization are byte-identical to the first failed cohort. Physical inputs,
time/grid sequence, hydrostatic speed, entropy and joint error bounds are
unchanged. Approximate Windows results are:

| Grid | Normalized pressure L1 | Normalized velocity L1 | Specific entropy change (J/kg K) |
| --- | --- | --- | --- |
| 32 | 0.0002468565114 | 0.004793925538 | 8.484297298e-7 |
| 64 | 0.00007272638404 | 0.001201710434 | 2.121502573e-7 |
| 128 | 0.00004637729934 | 0.0002964012499 | 5.304184976e-8 |

Pressure ratios are 0.2946/0.6377, velocity ratios 0.2507/0.2466 and entropy
ratios approximately 0.25/0.25. All pass the original bounds, with 8/16/32
nominal steps and no rejected attempts. Linux normalized pressure/velocity
errors differ by less than 7e-13. Maximum hydrostatic speed is below 3e-11 m/s on both platforms,
within the unchanged 1e-7-m/s bound. U+K+PE accounts pass their original operand
bounds; no arbitrary-flow entropy result follows.

The intermediate R3 parcel cohort passed this full gate on Windows but failed
Linux layered hydrostatic rest before any mode results. Its first negative
capture remains retained. A separate instrumented diagnosis found volume
residual 6.3983330763210483e-14 against the requested 16-epsilon target
3.5527136788005009e-15. The nested isentropic inversion still used its ordinary
2e-13 relative pressure tolerance. R4 explicitly requests epsilon/4 for that
inner inversion; the outer volume and energy bounds and other callers' default
options are unchanged. The reference EOS source is unchanged.

All five reconstruction, seven original transport and eight closed-owner
groups and their separate saved-data reviews pass before the R4 gravity run.
Core 57, retained-node 17 and occupied-fluid 24 regression groups also pass on
each platform. The 24-group suite was supplemental after gravity because the
initial runner selected the 17-group target with a mistaken planned count;
original invocation ordering is retained. No native gravity repeat was used
to change it. Details and terminal review identities are in
[reconstruction evidence](MAC_RECONSTRUCTION.md). Gravity retains its original
60-second runtime bound. The new parcel records use a predeclared 256-KiB pipe
cap, increased from the initial 128 KiB. No Unity, game ownership, production
performance or older failed physical-gate acceptance is implied.

## First-source results: gravity gate failed

Fresh strict Windows/MSVC and Linux/GCC15 builds pass. The first two groups
complete on both platforms: actual-mass work in gas and wet-corner cases, and
gas/liquid/layered hydrostatic rest. Maximum hydrostatic speed stays below
`6.4e-12 m/s`. Their finite U+K+PE accounts close within the declared operand
bounds, and the two exported dynamic cases have positive entropy changes.

The third group fails at N64. The required N128 case and fourth (invalid-domain/
rollback) group are not reached; the full gravity saved-data review is not run.
No gravity refinement, production or performance acceptance is claimed:

| Grid | Normalized pressure L1 | Normalized velocity L1 | Specific entropy change (J/kg K) |
| --- | --- | --- | --- |
| 32 | 0.003442261087 | 0.002145874659 | 8.13659854e-7 |
| 64 | 0.001546636281 | 0.002391723692 | 1.92865762e-7 |

Velocity error grows by a factor of **1.11457**, failing its unchanged ratio
bound of 0.7. Pressure and entropy decrease, but the joint criterion fails.
Both captures exit 1 with normal EOF and no timeout/pipe overflow. Windows/Linux
show the same failure. The timestep, inputs, grid sequence and bounds are not
changed, and that failed source cohort is not run again. The later result above
uses changed transport and pressure algorithms.

A data-only partial reviewer passes 8,159 saved-operand checks per platform for
the two dynamic cases and independently reproduces the failed velocity norm
from exact binary64 fractions. All non-boundary final velocities in both saved
mode profiles are negative. The eight prior zero-gravity groups and their full
4,873-check reviewer still pass on both platforms, including three deliberate
corruptions. The new full gravity review/corruption controls remain unexecuted.

An independent linearized column calculation reproduces the N32/N64 velocity
norms as 0.00214258/0.00238723, within the small nonlinear perturbation error of
the native profiles. Resolving time in that diagnostic model increases the
N32 norm toward 0.00666 and N64 toward 0.00351. This indicates competing spatial
and temporal errors, with partial cancellation on the coarser native grid. It
is diagnostic arithmetic, not a replacement native result or a repair.
The [standalone diagnostic](tests/analyze_mac_gravity_linearization.py) reads
the saved first-source stdout and performs those tridiagonal calculations
without launching the native code or changing its inputs.

The next algorithm work at that checkpoint was consistent spatial reconstruction of transported
constituent, internal-energy and passive inventories, retaining compatible dual
mass fluxes, positivity, trace preservation and constant-p/T contacts. Increasing
the test's starting resolution or reducing its nominal timestep would conceal
that failure and would not satisfy this gate. The existing donor-cell
transport, its tests, and this failed gravity gate remain reference evidence.

Private first-source records are
`local/occupied-staggered-validation/mac-gravity-{windows,linux}-r1`.
Terminal failed-review SHA256 values are
`0cc3915ff23a355618837dfb3974d4c725e6adf5d50b872663201a116857d25d`
(Windows) and
`dfc70f83773c11b7d0b379506dd7ec6a380d834cbf272685a1dcf13080e69675`
(Linux). They bind source freezes, compiler approvals, raw pipes, the negative
native results, partial reviews and passing zero-gravity regressions. Build/
runtime bounds stayed 180/60 seconds with 128 KiB per pipe. No independent agent
review, game launch or previously failed physical-gate replay occurred. The
default game DLL remains unchanged.
