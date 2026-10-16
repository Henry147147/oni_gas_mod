# Compatible compression viscosity

Six native groups and 6,095 saved-data checks pass on Windows and Linux. It is excluded
from the closed owner, default build, C ABI and game. The unchanged strong-flow
gate is still failed. The purpose is to address oscillations behind shocks in
the retained collision profiles, without changing the accepted point-velocity
oracle, grids, timestep or error limits.

For a cell's two normal faces, let `c = u_left - u_right`. A positive c activates
quadratic compression pressure `q = rho c^2`, corresponding to viscosity length
one grid spacing. Expansion and constant normal velocity produce no force.
Tangential shear produces no force. This is numerical shock regularization;
it is separate from material shear viscosity.

For integrated coefficient `B = dt area rho = dt cell_mass / spacing`, let
`a = 1/I_left + 1/I_right`, omitting a fixed endpoint from a. The nonlinear pair
ODE `dc/dt = -(area rho) a c^2` has the exact relaxation `c_new = c/(1+Bac)`.
The equal/opposite impulse is `d = B c^2/(1+Bac)` and its kinetic loss is
`Q = d (c - a d/2) >= 0`. The implementation computes that positive work from
velocity differences, never by subtracting large kinetic energies. It retains
the impulse even when the denominator rounds to one. Fixed endpoints retain
zero J and report their impulse on the fluid separately.

Forward/reverse half sweeps compose pair solves symmetrically in time. Normal
x and y operators act on different momentum fields. Every positive Q heats
its enclosing cell at fixed masses/volume; no trace, passive or mass is moved.
Every pair checks actual represented J/K and wall/heat accounts at the original
128-epsilon operand bound. The component independently checks represented U+K,
global impulse and EOS entropy, and flags unresolved heat additions. All
arithmetic uses the full scaled canonical fields behind explicit projections.
The EOS, significand precision and conservation bound stay unchanged.

The general compression-only pseudo-viscosity/positive-heating approach is
discussed in [De Vuyst's primary paper](https://arxiv.org/html/1703.00235), whose
entropy result is semi-discrete. The pair ODE, exact relaxation and split
component here are our own derivation; that paper does not certify this method,
its multidimensional behavior or its composition with pressure/remap.

Six groups are declared before the first native build/run:

1. Analytic free/free and fixed/free pair relaxations, unequal inertias, stiff
   relaxation and semigroup checks.
2. Full below-binary64 J, impulse and positive heat, including a denominator
   whose increment cannot be represented in binary64.
3. Constant velocity, expansion, tangential shear and a Galilean pair shift.
4. Closed rigid-wall and periodic mixed gas/liquid grids, exact species/passive
   retention, represented U+K, wall impulse and EOS entropy.
5. Transposed two-dimensional compression and a below-range occupied-grid
   application whose positive heat cannot change stored binary64 U.
6. Invalid coefficients, fixed momentum, shapes, stale projections and timestep
   reject without changing the source.

A separate saved-data checker reconstructs exact-fraction pair ODE/work and
every sequential event, retained constituents/passives, final J/U, both impulse
accounts and EOS entropy. It must reject altered impulse, heat and constituent
records. Bounds fixed before invocation: each platform's aggregate strict build
180 s, native 60 s, saved review 60 s and 512 KiB per raw pipe. No failed
strong-flow gate may be retried from standalone evidence: integration and all
applicable prerequisite controls/reviews must pass first. First failures remain
retained; no production performance, independent agent or game claim follows.

## Standalone results

The first strict builds fail because the new test exporter names the EOS field
`pressure_pa`; the existing type calls it `mechanical_pressure_pa`. Both first
results are retained and no native process runs from that cohort. Correcting
only that exporter field gives passing strict MSVC and GCC15 builds. The
compression implementation and numerical inputs are identical between cohorts.

All six groups pass once on each platform. Every saved review passes 6,095
checks and rejects separately changed impulse, heat and constituent records.
The checker independently reconstructs the pair ODE, event sequence, actual
primary updates, full heat and impulse, mixture EOS and entropy. Rigid walls,
periodicity, a solid corner and gas/liquid cells are covered. Below-range heat
and impulse remain positive in full reports, with unresolved primary increments
explicitly counted. The raw outputs differ across platforms; both independent
physical reviews pass. No bitwise determinism or sanitizer result is claimed.

Private results are `local/occupied-staggered-validation/mac-compression-{windows,linux}-r1`
and sibling `-r2` folders. Completed scoped-review SHA256 values are
`9713355e0a2977d9141e2063a9e81eb12f8a6f3d8e1cd99d13134e0bb431cadb`
(Windows) and
`658ea465d495f941ab260667c6abd140fdef54be9c7f54b3beaa3bb2e35737e0`
(Linux). They bind frozen sources, binary/compiler identity, strict flags,
bounded first builds/captures, the retained exporter failure and the saved
reviews. The default game DLL is unchanged. Closed-owner integration, full
strong-flow acceptance and actual game validation remain required.
