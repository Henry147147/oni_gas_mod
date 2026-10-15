# Bounded nonlinear passive validation gate

This is a test-only gate for the unchanged instantaneous A1 hybrid remapper.
It adds no state, transport algorithm, pressure adapter, physical timestep,
FluidWorld/C ABI selection, save format or game activation. The stronger
all-node temporal-rate claim and whole A2 remain false. The preserved original
Cartesian strict gate remains FAILED6/7 with its unchanged source and criterion.

The new native executable validates actual state/identity/physical balances and
exports data. Its exit0 is **not** a passing nonlinear refinement result. The
separate `tools/check_hybrid_nonlinear_oracle.py` subprocess must also pass.
The first Windows/MSVC native 304-case physical/export run passed, but the full
independent nonlinear refinement gate **FAILED**. No GNU/sanitizer run of
this new gate is claimed. Its criteria remain unchanged.

## Frozen first result: native physical PASS, nonlinear refinement FAIL

The archived strict MSVC executable completed 288 nonlinear histories and 16
uniform controls. After separately frozen oracle arithmetic/diagnostic
corrections, all 37 oracle calibrations and all 304 per-history independent
geometry, actual-state, trace, packet, balance, heat and EOS checks completed.
The single R5 oracle then stopped at the first predeclared spatial comparison
failure. It exited1; its evidence procedure reports `procedure_succeeded=false`.
This is a negative numerical gate, not a passing expected-failure procedure.

The first failing family is unreflected, nonuniform, phase 0, carrier -1, linear
motion, C=1/4, at tau=T/4=1/32. For N32 to N64, the exact velocity supremum
enclosures collapse to these point values:

| Quantity | N32 | N64 |
|---|---:|---:|
| Velocity supremum error | 12565816089467/2^52 | 12124367599963/2^52 |
| Decimal error | 0.002790171669146302 | 0.0026921504136996344 |
| Maximizing coordinate | 31/32 | 63/64 |

The fine/coarse ratio is exactly 12124367599963/12565816089467, approximately
0.964869095141856; the required strict upper bound is 0.8. The failure margin
fine-0.8coarse is about4.6001307838259284e-4. Both maxima are at the right
**inner** cap node. A separate exact polynomial derivative/bracketing analysis
of these two stored profiles confirms the global maxima; this is not an
unresolved supremum enclosure. Physical outer endpoints remain bitwise exact.

The species amount, bulk amount, point-nodal L1 and weighted interpolated L1
comparisons preceding this supremum check passed. Their fine/coarse ratios
were approximately 0.368532/0.411084/0.415044, 0.394644, 0.466307 and 0.481154.
The remaining comparisons after the first failure were not run to completion;
passing integrated/balance diagnostics does not close the failed supremum gate.

The first retirement widens the adjacent transition from h to 7h/4. Its right
corner intersects low/inner old corner packets in lengths 3h/8 and 4h/8. Writing
their velocities as u_L,u_R, the adjacent corner mean is (3u_L+4u_R)/7. If beta
is its rebalanced half mass and alpha the fixed cap half mass, compatible
gather gives

```
u_R_new = u_R - [3 beta / (7 (alpha+beta))] (u_R-u_L).
```

This independently predicts the recorded first-event velocities to ordinary
roundoff. At the sampled time N32 has one material inner-cap velocity loss;
N64 has two, about 0.00142964 and 0.00126251, producing its 0.00269215 bias.
The geometric-dual-versus-point correction is much smaller (about 2.23e-5 and
5.65e-6) and does not remove the error. The measured cause is retirement
projection and its accumulated bias. The known nonuniform inner-cap jump/rate
limit is a related limitation, not a proof of an asymptotic plateau or a
general convergence result. No pressure forces are advanced in this problem.

Private first-result evidence is retained under
`local/occupied-staggered-validation/hybrid-a2-nonlinear/`. The original 2562
native files, first failures and complete source archives remain unchanged.
The R5 source freeze SHA256 is
`0e4586d4e65ff687b74defc01f7ecff7bf26fda34b72efd80d83289381f9c9ed`;
the first failure (embedding both complete sample states/packets) is
`412ac6a73defc4b2b5c60feb89b26edc4aa3b4a0ad86c55dc4bb7035b2e6fe8d`.
Documentation of this result is a later claims update; the pre-execution
source snapshot remains the authority for the run. No native/oracle rerun,
criterion adjustment or algorithm change was made after the failure.

## Fixed mathematical problem and histories

The occupied material interval is q in [0,1], area1, gravity0, free endpoints,
initial temperature300K, and finite protected caps [0,h], [1-h,1]. The fields are

```
rho = 1 + q/4 + q^2/8
u = 1 + q/8 + q^2/16 + q^3/32
Y_B = 1/4 + q/16 + q^2/32
Y_trace = 2^-40 (1 + q/2 + q^2/4)
Y_A = 1 - Y_B - Y_trace
```

The reflected problem evaluates rho/Y at 1-q and u as -u(1-q). Three numerical
ideal-gas surrogate labels use N2/O2/CO2 atom sets and cp1040/920/850. Cv, molar
mass and the caloric offset come from that declared registry. These are
composition-dependent test properties, not calibrated material data.

Each initial species mass is independently integrated and rounded once. The
native test uses bounded integer primitives on the dyadic quarter-grid; the
separate Fraction oracle multiplies/integrates the original polynomials. No
species is used as a bulk remainder. Initialization runs once, with the exact
current mass and supplied nodal velocities used for compatible bulk momentum.
Initial lumped momentum/K quadrature errors and declared temperature-preserving
initialization energy are reported separately. No later field is reset/rebound.

Carrier time T=1/8 does not advance the canonical physical clock. Use
phi=p*h-a*tau, or phi=p*h-a*min(tau,T-tau) for one reversed cycle, dtau=C*h.
Both reflections, a=-1/+1, p=0/1/4/1/2/3/4, and both motions give32 families:

- Spatial: N16/32/64, C1/4.
- Fixed grid: N32, C1/2,1/4,1/8,1/16,1/64,1/256.
- Joint: (N,C)=(16,1/4),(32,1/8),(64,1/16).

There are288 unique nonlinear histories and16 uniform-u controls at N32,
C1/256, both reflections/directions/four phases, linear carrier only. Full
states are compared at T/4,T/2,3T/4,T. Each call uses the genuine preferred-grid
planner and current accepted state, without timestep retry. Count changes and
Cartesian-ID changes, including equal-count changes, identify events.

## Independent checks and predeclared numerical criteria

At each call, actual own-species mass, canonical momentum and stored U plus
canonical J^2/(2I) are summed independently of the remap report. Cumulative
budgets are128eps times each initial species mass,128eps times initial sum|J|,
and256eps times initial/current thermal energy plus initial/current K. There is
no unit, formation-energy or other-species floor. Signed arithmetic ledgers,
actual U gain, requested Q/Qarith and actual K loss remain distinct. Heat is
limited by available initial mechanical variance plus measured arithmetic.
Pure ideal-gas T, pressure and entropy are independently evaluated from actual
m/U/V and the frozen profiles; EOS caches are also checked against actual state.
An all-gas exported gas volume may differ from stored V by finite arithmetic.
The oracle independently encloses the three positive mole divisions and
compensated sum, R/T/p output preimages and the nRT/p operations. One internal
gas volume must fit both its exported conversion interval and V plus its
signed-residual conversion interval. Close subtraction is exact by Sterbenz;
residual consistency uses only the two relevant local output conversions.
Liquid/vacuum volumes and phase flags remain exact. This certificate changes
neither the EOS nor its pressure/root/state/energy acceptance rules.

The oracle measures exact cell-amount errors for every species, bulk amount
error, lumped point-nodal velocity L1, rho-weighted piecewise-linear velocity
L1/supremum, and fixed cap-window L1 on [0,1/8] and [7/8,1]. It also reports
piecewise-constant species/density field L1. Geometric dual mass/mean-velocity
errors are separate from point-node/physical-endpoint errors. Complete profiles
include the first/last six cells; a shrinking cap window cannot hide the layer.

At **each** nonzero sample time, successive spatial and joint refinements must
have strict fine-error-upper <0.8 coarse-error-lower for each species amount,
bulk amount, point-nodal L1, interpolated L1/supremum, both fixed cap windows,
both inner signed-bias magnitudes and both inner TV/T. Heat/T and geometric-dual
diagnostics must strictly decrease. No factor is selected after results.

Fixed-N adjacent-C comparisons use independent node-union rho-weighted PL
integration/supremum and constant-field cell-union integration. Successive
nonzero differences, absolute heat/T changes, signed inner-bias changes and
TV/T changes must strictly decrease. An unresolved enclosure fails; a nonzero
manufactured error is never accepted as an exact-zero control. The native
test retains physical outer q/I/J/K/u and cap species/volume/IDs bitwise; outer
jump/rate/TV are exactly zero. Inner jump/dtau is reported without claiming it
bounded. Inner TV is limited by the original velocity range plus propagated
local packet/division arithmetic. Uniform controls have a fixed128eps|u|
velocity budget independent of the call count and only arithmetic heat.
For those uniform controls only, the diagnostic heat norm is abs(Q)/T and
abs(Q) must fit the existing local K arithmetic bound. Actual Q/Qarith remain
signed everywhere in the physical ledgers and independent packet checks.
Nonuniform heat/refinement norms remain Q/T, with their unchanged criteria.

Variable rho must cause genuine half-mass rebalance. It carries the same
positive fractions of J/H. This material redistribution is distinct from
quadrature-only lambda normalization. The external oracle independently
intersects actual old primal and virtual-corner intervals at the first
insertion/retirement/equal-count event if present and first reversed-leg event.
It derives target mass/J/H, compatible Q and cap/interior heat, then compares
the actual new inventories/nodes. Only a propagated local arithmetic mass
discrepancy can normalize dual quadrature; constituent masses/primal U are
never repaired. Signed deltaM/J/H and Qarith are reported separately.

The oracle treats every exported binary64 as an exact Fraction. Bernstein
convex-hull signs certify absolute-integral regions. Unresolved roots after40
dyadic subdivisions contribute enclosing bounds. A float root suggestion is
only an accelerator after exact sign/interval certification. Supremum uses
exact Bernstein branch/bound with a fixed2^-40 coefficient-scale enclosure.
All comparisons use enclosing endpoints. Local packet checks count bounded
positive share/gather/normalization/rebalance/division operations;64eps share
and256eps gathered/rebalanced envelopes scale with incident mass/J/H,512eps
with incident nodal H, and1024eps with heat-allocation incident H. These
predeclared forward-error envelopes never use formation energy or bulk mass
as a trace floor. They do not widen the unchanged A1 acceptance guards.

## Failure evidence and execution boundary

An additional N8,p1/2,C1/4,maxcells7 fixture accepts a genuine nonidentity
seven-cell rezone, then rejects the real eight-cell insertion and a stale
accepted-revision plan. Full canonical/cache/model/ledger/cap/coverage/snapshot
identity is compared before/after each rejection. Existing A1 invalid-domain,
entropy and positive-share tests remain unchanged and registered.

Any unexpected typed, representability, EOS, entropy, balance, packet, profile
or refinement failure is fatal. The first native failure preserves complete
before/plan/report/after state; the first oracle failure preserves the exact
comparison and complete sample states/packets. There is no automated timestep
retry, criterion adjustment, material floor, seed gas, trace drop, velocity
correction or heat clip. Native output is placed in a fresh numbered run;
the evidence procedure authenticates its fresh marker and checks an explicit
new oracle output directory. A native completion marker alone cannot pass it.

`MA_BUILD_STAGGERED_EXPERIMENTS=OFF` still registers7 tests. ON registers13,
including the new native physical/export gate,12 applicable tests plus the
unchanged known-failed Cartesian gate. The experiment evidence procedure runs
the independent oracle after both its applicable and unfiltered native runs;
it retains actual legacy exits1/8/8. A successful evidence procedure still
reports `strict_cartesian_gate=FAILED`, whole A2 false and no pressure/front
acceptance. Native subprocesses and oracle subprocesses have separate finite
timeouts (default300s and1800s). A timeout is an evidence failure, not a retry.

The first strict MSVC execution requires a complete frozen source/command
inventory. A numerical failure is frozen for root review before any later
platform run. GNU/ASan execution and any later physics work require that review.
