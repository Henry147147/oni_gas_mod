# Compatible compression viscosity

The component and owning interval pass all 65 native groups on Windows/Linux,
including 6,095 component and 11,214 owner saved-data checks. It is excluded
from the default build, C ABI and game. The unchanged strong-flow
gate is still failed on pressure refinement. The later
[symmetric coupling](MAC_COMPRESSION_COUPLING.md) now brackets pressure/transport
with half compression steps and reports actual interval endpoint entropy; its
66 native groups pass but pressure refinement still fails. This document retains
the preceding post-only cohort. The purpose is to address oscillations behind shocks in
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
reviews. The default game DLL is unchanged. At that standalone checkpoint,
closed-owner integration, full strong-flow acceptance and game validation
remained required; the integration results follow below.

## Owning interval integration

`MacClosedWorld::advanced` now applies quadratic compression to every accepted
pressure/transport candidate by default, before committing its state and clock.
The primitive `advance_mac_closed_substep` remains the separately inspectable
pressure/transport operator. An explicit `none` setting retains that reference
path. Compression heat, unresolved heat/J updates and event counts have separate
owner fields; wall impulse and energy bounds include all three stages. A failed
later interval attempt publishes none of the compressed candidates.

Four integration groups are fixed before the first build: one gas owner, one
mixed-liquid solid-corner owner, one below-range owner, and disabled-reference/
later-failure/invalid-mode controls. Three complete saved states include both
pressure/transport and compression operands/events; a separate reviewer must
replay the stages, actual U+K, wall impulse, clocks and projections, and reject
three altered heat/wall/constituent records. Existing reference parsers are
generalized only to their supplied case set and reusable compression format;
their scientific error limits remain unchanged. Aggregate build 180 s,
new native/review 60 s and 512 KiB per pipe. All 61 earlier native groups and
their saved reviews must pass before either strong gate repeats at its original
180-s/2-MiB bound. Original Riemann/gravity scientific source bytes stay fixed.

The first integrated builds and all 65 native groups pass on both platforms.
The first owner data review incorrectly calls acoustic-only checks without a
wave fixture, and a parser generalization confuses transport cases with pressure
cases. These first failures are retained. The repaired reviewers reuse every
native capture and unchanged C++ binary. They then expose a physical impulse
failure in the mixed-liquid case: vertical residual 1.0380829226e-15 kg m/s
exceeds the original 7.2682376426e-16 bound by 1.428. Linux does not repeat that
known shared review failure. No strong-flow retry follows either cohort.

The next source accumulates signed boundary pressures before multiplying their
shared timestep and area; gravity reaction remains separate until accumulation.
Full pressure-wall reaction values back the explicit legacy projections. This
removes cancellation of separately rounded large wall products, without
changing pressure impulses on fluid faces, EOS work, initial data or the
128-epsilon criterion. A data-only diagnosis of the original operands predicts
vertical residual 2.1413e-18 with the same bound; fresh native verification is
still required. The old tiny reversal now explicitly selects `none` because its
purpose is exact interval comparison with the two-stage primitive. Its physical
quantities and accounts stay unchanged; the new below-range owner control
separately verifies the default active compression path.

## Integrated results and remaining pressure accuracy

Fresh strict MSVC and GCC15 builds pass after the wall accumulation repair.
All 65 native groups and saved reviews pass once per platform. The new four
owner groups pass 11,214 saved checks; the six compression groups pass 6,095.
Every full reviewer rejects three corruptions. The remaining counts are the
61 prerequisite groups documented above and in [exponent validation](MAC_EXPONENT_RANGE.md).
The mixed-liquid vertical momentum residual is now 0.002947 of the unchanged
bound on Windows and 0.000970 on Linux. The two-stage primitive and new default
owning interval remain explicitly distinguishable in controls and reports.

One Windows strong-flow retry follows all prerequisites. Sod N64/128/256 passes
joint refinement and the original fine-grid limit. Collision then fails on
pressure at N128:

| Grid | Density L1 | Pressure L1 | Velocity L1 |
| --- | --- | --- | --- |
| 64 | 0.0528314817013 | 0.0581114509839 | 0.0757790079357 |
| 128 | 0.0429871612094 | 0.0804701217318 | 0.0479075695783 |

Successive ratios are 0.813666/1.384755/0.632201. Velocity now satisfies 0.85,
while pressure fails. No collision N256 or separation case runs. All five
completed profiles retain their original global mass/trace/passive, actual U+K,
wall momentum, EOS and entropy checks; the partial saved-data review passes
59,205 identities and reproduces the failed norm. This is one passing native
Sod subgroup, no full strong-flow review and no partial-review corruption
controls. Linux does not repeat the known shared failed gate. These are bounded
correctness runs; no speed acceptance follows.

A data-only localization retains the original cell-average pressure metric and
independent EOS operands. Within four cell widths of the exact shocks lie
99.35% of N64 pressure error and 91.43% at N128. A pressure/viscosity coupling
accuracy repair is required before another failed-gate retry; neither the
point/cell-average oracle, original timestep nor 0.85 criterion is changed.

Private integration results are
`local/occupied-staggered-validation/mac-compression-owner-{windows,linux}-r1`,
`-r2` and `-r3`. R1 retains first review mistakes; R2 reuses all native captures
and records the physical impulse and old reference-metadata failures. R3 binds
fresh binaries/captures and the wall repair. Completed scoped-review SHA256s:
`e576660afe5250de7cc7641c026181b47612ae6760b8951d34d05f0070aece2f`
(Windows),
`7a1d6b3de57a10ce4fd34079c02f310ca65c53c5d10b9be148d6fc6b06ea715c`
(Linux). No independent agent, new sanitizer, Unity, production or shipping
acceptance is claimed. The default game DLL remains unchanged.
