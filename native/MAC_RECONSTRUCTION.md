# Conservative material parcel reconstruction

Five standalone controls and the existing closed-pressure and smooth-gravity
controls pass on Windows/MSVC and Linux/GCC15. The closed MAC owner selects
limited linear material reconstruction by default, with
the previous donor-cell path available as an explicit reference setting.
The default gas ABI does not select this owner; later opt-in native sealed-domain
game controls use its existing settings.

A later [swept first-moment path](MAC_SWEPT_RECONSTRUCTION.md) addresses a
reproduced moving-centroid affine error. It is explicit selector 2, while the
method described below retains selector 1 and remains the default. Its component,
checkpoint and selected regression controls pass on Windows and Linux; the
strong-flow and vacuum gates remain open.

For each donor, neighbor inventories are expressed at the donor's material
volume. Centred X/Y differences describe every constituent mass, actual stored
U and sparse passive quantity. A positive thermal-plus-elastic proxy also
participates in limiting. One common coefficient scales all these slopes.
This retains linear relations between constituents and U at constant pressure
and temperature, including composition and gas/liquid volume constraints.
No mass fractions are renormalized and no trace threshold is used.

An outgoing face samples the swept slab centre. Its normal coordinate is
`sign(q)*(1-|q|/V)/2`; its transverse coordinate is minus half of the donor's
mean transverse volume sweep divided by V. Four corners, actual outgoing
coordinates and the retained-volume centroid all constrain the common limiter
to neighboring inventory bounds. Positive fields also retain at least half
their cell-average value at each evaluated point. This is a relative slope
constraint, never an absolute density/contaminant floor. The outflow-volume
fraction is bounded by 0.45, with the owner retaining its 0.4 default.

The reconstructed vector times `|q|/V` becomes an owned parcel. The retained
inventory is the original minus all outgoing parcels. Prototype, actual
normalized-parcel and retained EOS states must be admissible. If necessary,
all slopes are halved together, with exact donor order as the final option.
Unrepresentable stencil gradients flatten the complete donor and are counted;
an unrepresentable positive parcel constituent/passive rejects atomically.
No individual mass, temperature or passive quantity is clipped to zero.

The parcel transport entry point checks every donor's constituent/U/passive
and volume decomposition at 128 binary64 epsilons of its actual operands,
plus target volume, EOS and face geometry. Actual parcel total masses define
the primal fluxes used by the compatible dual transport. Face J/K transport
and positive mixing/projection heat remain the earlier donor-cell method;
this change does not claim a fully second-order nonlinear momentum solver.

Bounded multidimensional reconstruction is discussed by
[Suresh (2000)](https://epubs.siam.org/doi/abs/10.1137/S1064827599360443).
The shared multicomponent limiter, moving-volume slab interpretation and
elastic-EOS backtracking here are this implementation's choices and need their
own checks; that paper does not validate this code or its thermodynamic closure.

Five new controls pass: independent gas-composition/gas-liquid constant-
p/T contacts, cold compressed-liquid EOS limiting, exact zero-flux identity,
eight malformed parcel plans plus invalid selector and unrepresentable positive
passive rejection, and N16/32/64 bounded oblique scalar advection with each L1
ratio below 0.45. Saved parcels, retained inventories and limiters are checked
using exact fractions and independent 80-digit EOS/continuous-wave arithmetic.
The cold control uses 300.001 K against an explicit 300-K liquid profile bound,
with its original formation energy, Cv, density and bulk modulus. An earlier
1.001-K fixture is invalid under the existing specific-energy precision rule;
its first failure is retained and does not justify changing that EOS rule.

The oblique passive control has these cell-average L1 errors on both platforms:

| Grid | Error | Ratio to previous grid |
| --- | --- | --- |
| 16 × 16 | 0.003785217850 | — |
| 32 × 32 | 0.001177162631 | 0.310989 |
| 64 × 64 | 0.000317154091 | 0.269422 |

Both ratios meet the predeclared 0.45 limit. The cold-liquid control exercises
52 EOS slope backtracks. The data-only reviewer passes 13,321 exact-fraction/
80-digit checks and rejects three altered trace/limiter/wave records per
platform. It checks source decomposition, the common limiter, actual parcels,
retained and target inventories, EOS contacts and continuous wave errors.

The seven original MAC transport groups and 3,884-check reviewer also pass,
as do eight closed pressure/transport groups and their expanded 6,526-check
reviewer. After these prerequisites, the unchanged four-group gravity source
passes on both platforms, with 16,458 separate saved-data checks and three
corruptions rejected. Its initialization, timestep, grid sequence and physical
bounds remain unchanged; see [gravity results](MAC_GRAVITY.md). The extra parcel
evidence uses a predeclared 256-KiB pipe cap; gravity retains its original
60-second runtime bound.

No general entropy, arbitrary-interface/shock, vacuum, phase, persistence,
production performance or game acceptance follows from this new path.

The first compiled valid parcel cohort (R3) passed the five new controls, seven
transport controls and eight closed controls with their separate saved-data
reviews on Windows and Linux. Its original gravity gate passed four groups and
16,458 review checks on Windows, but Linux exhausted the shared attempt budget
in layered hydrostatic rest, before reaching any stratified modes. That failure
remains retained. A separate instrumented diagnosis found an outer volume
residual of 6.3983330763210483e-14 against the unchanged 16-epsilon target
3.5527136788005009e-15. The nested isentropic inversion used its ordinary
2e-13 pressure tolerance.

The repair exposes that nested root accuracy explicitly and asks for
epsilon/4 pressure accuracy in the MAC caller. The shared helper's ordinary
default stays unchanged. Geometry, energy, hydrostatic and mode bounds stay
unchanged. Fresh R4 builds and prerequisite reviews pass before the repaired
gravity gate. All 57 core and 17 retained-node regression groups pass on each
platform. The runner initially selected the 17-group retained-node target while
its plan mistakenly named a count of 24; the intended 24-group occupied-fluid
suite was built and passed separately after gravity. Original captures and
their ordering remain retained; gravity was not repeated to change that order.

The earlier R1 compilation failure and R2 invalid cold initialization also
remain recorded. R1 had an extra closing parenthesis in a new test expression;
no native test launched. R2's 1.001-K source failed the existing specific-energy
precision rule before the solver ran. The cold fixture was corrected to the
explicit 300.001/300-K case above without changing the caloric reference, Cv,
compression, density, bulk modulus or EOS precision rule.

Private source freezes, compiler approvals, first raw captures and terminal
reviews are in `local/occupied-staggered-validation/mac-reconstruction-{windows,linux}-r4`.
Completed-review SHA256 values are
`1566cea01498b7bb1c96349b941437b54b13a894e6abb12431f9bf47ae63a252`
(Windows) and
`368e89e3164d161dd723c415a28ee7ae64c9c977b90d3a5348b9d6cf8090f824`
(Linux). They include the supplemental regression and retain all earlier first
outcomes. No independent agent review, Unity launch or performance acceptance
is claimed. The default game DLL is unchanged.
