# MAC gravity coupling

The first-source refinement gate fails on Windows and Linux. Constant downward
gravity extends the closed occupied MAC owner. It remains excluded from game
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
The earlier failed physical gates remain unchanged.

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
changed, and the failed native gate is not rerun.

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

The next algorithm work is a consistent spatial reconstruction of transported
constituent, internal-energy and passive inventories, retaining compatible dual
mass fluxes, positivity, trace preservation and constant-p/T contacts. Increasing
the test's starting resolution or reducing its nominal timestep would conceal
the current failure and does not satisfy this gate. The existing donor-cell
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
