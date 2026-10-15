# Retained material-node experiment, first gate

`ma_occupied_staggered` is a separate static experimental library. It links to
`ma_core` for the unchanged EOS bridge and occupied pressure/work solve;
`ma_core`, the game DLL, `FluidWorld::step`, `GasWorld`, C ABI and production
saves do not link to or select this state. No Eulerian/primal/dual remap or
corner rebalance is implemented here. The full gas-to-vacuum front gate in
`GAS_VACUUM.md` remains false. This first material-coordinate gate does not
validate that front, shocks, arbitrary topology, performance or gameplay.

## State and explicit initialization

`occupied_staggered.hpp` exposes `OccupiedStaggeredState`,
`initialize_occupied_staggered[_into]` and `advance_occupied_staggered`.
Initialization supplies all positive material cells and all N+1 ordered nodes
together, a fixed positive cross-section, nonnegative downward gravity, and
traction-free or rigid endpoints. Empty cells plus empty node arrays represent
exact vacuum. Interior vacuum gaps, cavitation/separation, unsupported EOS
materials and nonpositive represented intervals fail. Supported thermodynamic
profiles are the existing ideal-gas/zero-expansion-elastic-liquid reference;
they are not calibrated real-liquid data. Initialization makes a deep owned
copy of the supplied EOS/registry into an actually const allocation, including
profile vectors and index mappings. A const shared view of a caller's mutable
object would not prevent later assignment through that mutable alias. The
retained immutable copy and complete profile metadata remain part of each
state, independently of replacement/destruction of the supplied model.

Canonical nodal inertia is `I=(Mleft+Mright)/2`; exterior vacuum contributes
exactly zero mass. Canonical momentum is `J`, velocity is `J/I`, and fixed wall
velocity is exactly zero. Each cell's momentum and mechanical variance are
derived read-only views:

```
P_i = M_i*(uL+uR)/2
variance_i = M_i*(uR-uL)^2/8
K = sum_nodes J_j^2/(2 I_j)
  = sum_cells (P_i^2/(2 M_i) + variance_i)  [up to reported arithmetic]
```

Input cell P must agree with the independently supplied node means within
`32*epsilon*(M*(|uL|+|uR|)/2+|P_supplied|)`. There is no unit floor; an external
momentum conflict is rejected. The returned initializer ledger reports the
actual momentum reconciliation and kinetic partition arithmetic. No cell-only
state is silently converted. Two explicit policies are supported:

* `preserve_temperature` keeps supplied internal U and reports the actual
  additional canonical nodal-versus-prior-bulk K as declared added energy.
  This is an initialization energy addition, not conservation.
* `preserve_total_energy` deducts each cell's added mechanical partition from
  U and validates the resulting EOS domain. The ledger compares actual total
  canonical K and represented U, separately reports kinetic partition rounding
  and sub-ULP energy conversions, and rejects material residuals. A rounded
  conversion is not described as exactly conserved.

Both initializers build a complete candidate. `_into` commits only through a
noexcept move after validation. Node/cell arrays, model identity, geometry,
time, caches and ledgers cannot be externally edited through this API.
Species mass aggregation is checked for finite range both before initialization
publishes and whenever totals are requested. Finite per-cell masses and EOS
states do not imply a finite aggregate; overflow is a typed precision failure,
never a published infinity. This adds no physical tolerance or mass cutoff.

## Retained pressure, gravity and residual accounting

Each material call passes the retained nodal velocities and fixed inertias to
the existing implicit isentropic pressure/work solve. It retains the new J
and all nodal K; no whole-velocity PIC averaging, incremental variance heat,
artificial viscosity or correction heat is applied. Mass vectors are copied
unchanged. Endpoint EOS readings are recomputed from actual stored U and
represented material volumes, rather than copied from a different root volume.

Let `delta_i` be the pressure root's requested cell volume change, `p_i^w`
its secant work pressure, `ubar` its midpoint node velocity and
`r_i=delta_i-A*dt*(ubarR-ubarL)` its signed nonlinear residual. In exact
compatible arithmetic:

```
deltaU_i = -p_i^w*delta_i
deltaK = sum_i p_i^w*A*dt*(ubarR-ubarL) + body_work
deltaPE = -body_work             [occupied-centroid half-mass gravity]
delta(U+K+PE) = -sum_i p_i^w*r_i
```

The last term is an actual finite nonlinear-solve error, not floating-point
roundoff, heat or external work. Reports separate that signed residual work,
represented stored-U addition roundoff, canonical J/I/force arithmetic, and
represented gravity/geometry work error. The measured remaining energy
discrepancy is checked against `128*epsilon` times the individual absolute
force/work and old/new K terms. Actual total energy error is also checked
independently against the unchanged `2e-11` local thermal+elastic+mechanical
budget (excluding formation-reference and absolute coordinate floors).
Finite root tolerance therefore permits small energy and entropy errors;
neither is claimed exact or canceled by adding a source.

For the single compressed-liquid regression, Windows measured work is about
8796 J, actual total energy residual `5.08859e-5 J`, signed nonlinear residual
work `5.15909e-5 J`, stored-U rounding `-7.04993e-7 J`, and remaining arithmetic
about `-7.05e-13 J`. Entropy changes about `1.71e-7 J/K`. Independent integrated
liquid `p(V)` and ideal-gas adiabats check velocity, T, p and volume without
using the pressure helper as their oracle. These are finite-error checks, not
proof of exact energy/entropy preservation.

Fixed walls supply the reaction impulse and perform zero work. Momentum's
arithmetic bound uses `64*epsilon` times the sum of old/new absolute J and
each individual `dt*A*|p_left|`, `dt*A*|p_right|` and `dt*I*g` term. Opposing
large wall impulses are not bounded by their tiny net reaction or a unit floor.
Sub-ULP requested nodal impulses, energy/volume updates and relative position
changes are reported explicitly. Material range/precision failures reject the
candidate. No state is partially published and no retry loop hides a typed
topology error.

## Coordinates and bounded numerical domain

Nodes use relative material coordinates and a compensated two-double global
origin. A rigid endpoint is the coordinate anchor if present, otherwise the
left free node is the anchor. Common translation changes the origin without
repeatedly subtracting large absolute positions; rigid endpoints remain fixed
at their actual wall coordinates. Actual origin displacement must match the
requested local displacement within `32*epsilon*|displacement|`.

Relative node motion uses midpoint velocity relative to that anchor. Each
actual positive volume is checked against the pressure root's requested
change and signed residual within `32*epsilon*V_endpoint`, separately from
the scalar root/addition arithmetic diagnostics. This does not support every
possible tiny interval far from the anchor: unresolved material geometry is
typed-fatal, rather than rounded into empty material. There is no density,
pressure, species, donor-fraction or occupied-volume floor. Time remains a
double; requested/represented increments and its addition error are reported,
and a timestep that cannot advance represented time fails atomically.

## Acceptance scope and reproducible tests

The dedicated CTest entry is `retained_occupied_staggered`. It covers explicit
initialization policies and conflicts; canonical K/P/variance identities with
unequal masses; unresolved conversion ledgers; exact vacuum; stationary liquid;
both translations, huge global origins and local scales `2^-300` / `2^300`;
isolated symmetric gas and compressed-liquid expansion with retained K;
independent one-cell adiabats; Fourier dt-to-zero velocity; nonzero alternating
pressure force; independent small-amplitude acoustic profiles/phase with
separate grid and timestep refinement; freefall/gravity; rigid-wall impulse
and reflected single-wall motion; hydrostatics; positive-denormal gas traces;
and invalid configuration/topology/EOS/precision with whole-candidate rollback.
Two independent-review regressions additionally mutate/replace a caller's
same-size caloric EOS and larger registry after initialization, then verify
retained profiles, metadata, caches, totals and the next actual step are
unchanged; and construct 512 finite-EOS dyadic liquid cells whose constituent
mass sum overflows, checking atomic rejection against a finite near-limit
255-cell control. The mass fixture has `m=2^1016`, `rho0=1024`,
`V=area=2^1006`, `Cv=2^-10`, `T=1`, zero velocity/pressure and finite aggregate
U. This is an explicit numerical-range surrogate, not material calibration.

The acoustic standing-wave comparison uses the linear ideal-gas solution
`u=a*sin(pi*x)*cos(c*pi*t)` and
`delta_p=-rho*c*a*cos(pi*x)*sin(c*pi*t)`, amplitude `a/c=1e-7`.
It measures actual node velocity and material cell pressure. At Courant .1,
N=16/32/64 Windows velocity L1 errors are approximately
`2.8202e-4 / 7.0788e-5 / 1.7720e-5`, pressure errors
`4.7962e-4 / 1.1986e-4 / 2.9959e-5`, and inferred sound-speed relative errors
`1.6359e-3 / 4.0939e-4 / 1.0240e-4`. Fixed N=16 timestep refinement is compared
to a finer temporal reference on the same grid, isolating time dispersion.
These establish the bounded material consistency gate, not a vacuum-front or
general free-surface convergence result. Dual/primal remap and its separate
local consistency gates remain the next unimplemented work in
`STAGGERED_1D_DESIGN.md`.

Build in a private directory with `MA_BUILD_TESTS=ON`; build target
`ma_occupied_staggered_tests`, then `ctest -R retained_occupied_staggered`.
The new library and test use strict MSVC `/W4 /WX /permissive-` or GNU/Clang
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`. Existing root
`MA_SANITIZERS=ON` instruments the separate library/test on GNU/Clang too.
Source/binary manifests and compiler/build/CTest logs are frozen under ignored
`local/occupied-staggered-validation/gate1-review2` for the post-review freeze;
the pre-review `gate1` evidence is retained separately.
