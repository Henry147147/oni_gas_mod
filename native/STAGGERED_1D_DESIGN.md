# Proposed retained staggered 1-D state: review before implementation

This is a proposed next experimental `FluidWorld::step` slice, not implemented
or validated physics. The current gas-front gate remains false. It would keep
the same single connected occupied interval, fixed cross-section, supported
EOS profiles, exact exterior vacuum, typed topology failures and whole-call
rollback. Default closed mode, `GasWorld`, C ABI and game ownership stay outside
the change. No front-speed clamp or pressure/density floor is proposed.

## Why a state extension

The current incremental correction removes unconditional PIC drag, but still
reconstructs node velocities from cell momentum after every step. Its
dt-to-zero centered cell gradient/divergence admits a pressure checkerboard.
It also loses a symmetric isolated-cell expansion velocity moment. Preserving
the nodal dynamics requires storing its momentum and kinetic energy across
steps/remaps, rather than transferring that moment into heat on each call.
This is a mechanics change; successful energy balances cannot substitute for
new Fourier/acoustic and vacuum-front convergence tests.

Staggered compatible force/work construction is established by
[Caramana et al.](https://cnls.lanl.gov/~shashkov/papers/comp.pdf).
Conservative subcell gathering/remapping/scattering is an established ALE
approach described by [Loubere and Shashkov](https://www.math.ntnu.no/conservation/2004/058.pdf).
The need to keep corner/nodal masses consistent when rezoning is emphasized in
[Owen and Shashkov's 2014 remap study](https://laro.lanl.gov/esploro/outputs/journalArticle/Arbitrary-Lagrangian-Eulerian-remap-treatments-consistent/9916370004303761)
(DOI 10.1016/j.jcp.2014.05.023).
Those sources motivate the design. They do not verify the following restricted
1-D algorithm, its proposed corner rebalancing or any convergence claim here.

## Canonical state and energy

Each occupied cell retains constituent masses, internal U (including its
elastic energy), occupied geometry and wall metadata. Each material node stores
momentum J and its validated adjacent half-cell inertia
`I=(Mleft+Mright)/2`; at a free boundary only its one material half is present.
Its velocity is J/I. Empty nodes have I=J=0 and no velocity/heat body. Fixed
wall nodes have exactly zero velocity, with separate reaction accounting.

Cell momentum becomes a derived cache `P_i=M_i*(uL+uR)/2`. The total mechanical
energy must include the retained variance:

```
K_i = P_i^2/(2 M_i) + M_i*(uR-uL)^2/8
sum K_i = sum_nodes J_j^2/(2 I_j)
E = sum U_i + sum_nodes K_j + sum M_i*g*occupied_centroid_i
```

The variance is mechanical energy, not another internal-energy term. Existing
`FluidCell` bulk K access must not silently omit it from occupied-mode totals,
remap, diagnostics, snapshots or initialization. A dedicated occupied-state
energy routine is preferable to changing the legacy `GasCell` definition.

The pressure substep can reuse the existing occupied staggered pressure/work
helper with retained initial node velocities/inertias. Its midpoint nodal work
is then the actual mechanical-energy change. The current incremental force
transfer and its Q are not also applied. Cell pressure forces share the same
nodes as occupied volume motion, without the intervening centered reconstruction.
Gravity retains the half-cell inertia and occupied-centroid identity in this
bounded geometry. Its potential/work balance must be rechecked independently.

## First-order positive dual/subcell remap

The proposed first remapper is deliberately first order. It must be proved and
tested before higher-order reconstruction or limiters are considered.

1. Split each moving occupied material interval at its midpoint into two
   geometric corners. Each has bulk mass M/2. Its velocity is its attached
   node velocity, with momentum m*u and kinetic energy m*u*u/2. The cell U and
   constituent vectors continue to use conservative primal occupied overlaps;
   no chemical constituent is dropped or assigned a fictitious corner floor.
2. Intersect old corners with target occupied half-intervals, using the same
   piecewise-constant material density as the primal remap. Transport bulk
   corner mass, momentum and kinetic energy using those positive intersections.
   Their mass sums must agree with the target primal total masses to a derived
   local arithmetic bound. Intersections of zero duration reproduce the old
   corners exactly. Tiny positive overlaps need an accurate representation or
   a typed failure, including a zero final constituent remainder.
3. An intersection result generally has unequal left/right target corner
   masses. Explicitly rebalance each cell's **bulk** corner masses to M/2 by
   transferring the excess from the larger corner to the smaller. Carry the
   same positive fraction of that corner's momentum and transported kinetic
   energy; keep the primal constituent totals and U unchanged in this internal
   bookkeeping transfer. This is a proposed local mass relocation, not a
   published method claimed verbatim. It is zero on an unchanged valid mesh
   and preserves the uniform-velocity state. Its consistency/diffusion needs
   separate manufactured-transport tests.
4. Gather target corner masses/momenta at each node, recover u=J/I and compare
   transported kinetic energy with actual nodal J^2/(2I). Positive packet
   transfers imply a nonnegative difference by convexity in exact arithmetic.
   Distribute this difference to adjacent cell U using the same corner masses.
   Report represented differences; do not silently clip a material negative
   discrepancy, negative temperature or unsupported EOS state.
5. Preserve gravitational first moments during the primal remap, then include
   the difference between transported potential energy and the target uniform
   occupied-centroid energy, as the current code does. This correction can
   have either sign; positivity and gravitational balance remain independent
   acceptance gates. Do not claim a general entropy proof from the kinetic
   convexity argument when gravity or EOS-domain constraints are present.

The corner rebalancing is necessary for this particular bounded proposal:
simply remapping a dual mass field and resetting its inertia to neighboring
M/2 changes kinetic energy and momentum inconsistently. Keeping unequal corner
masses instead would require a different persistent density/quadrature model
and a new gravity/work derivation. That larger alternative is outside the
smallest proposed slice.

## Initialization, boundaries, mass changes and snapshots

Initialization must be atomic and explicit. A new occupied staggered initializer
accepts all cells and all material-node velocities together, checks topology
and wall velocities, derives inertias/P/variance, and returns a mechanical-energy
ledger. Temperature-preserving initialization explicitly reports the additional
nodal-versus-bulk kinetic energy. A conversion of an already conserved bulk
state must instead deduct it from U and reject if the supported thermal domain
is lost. There is no silent switch or inferred seed node velocity. The old
cell-only setter remains for the current mode; the new mode cannot step with
an incomplete node state. A Fourier regression must use specified compatible
nodes and independently check the derived cell amplitude and initialization
ledger before taking dt toward zero.

A uniform translation must retain u after primal/dual remap and rebalancing,
including unequal cut masses, both directions and coordinate shifts. A moving
free surface retains its one-sided node momentum when the active fixed-cell
index changes; a newly wet node receives actual remapped momentum, not zero or
an extrapolated gas packet. A disappearing zero-inertia node cannot leave
momentum behind. Unsupported separation/cavitation/impact remains typed fatal.

Rigid wall node positions remain fixed at their wall coordinates, and their
velocities remain zero. If a remap contributes
momentum to a wall node, imposing the boundary must record the opposing wall
impulse and convert the removed kinetic energy compatibly to adjacent U; a
fixed wall performs zero work. This operation must be identity when the mesh
does not move. Pressure-step and remap wall reactions must be tested separately
to avoid counting either twice.

Primal mass changes during remap update target corner and nodal inertia through
the positive records above. External injection/extraction is not supported in
the first slice: reject per-cell mass mutation after staggered initialization.
A later explicit transaction would require packet mass/U/momentum/kinetic energy,
compatible corner redistribution and a conserved transfer ledger. Changing
inertia while retaining J or u without that accounting is invalid.

Whole-call rollback must include cells, node momenta/inertias, variance caches,
geometry, time, approximation ledgers and the model identity as one candidate
state. Tests fail after an accepted substep and compare all of it. An explicit
versioned experimental snapshot must serialize nodal state; cell P alone
cannot recover it. Snapshot metadata includes EOS identity, dimensions/axis,
occupied endpoints, wall flags, U/constituents, node J, time and ledgers. Validate
all mass/cache/energy relationships before publication. Existing production
save and C ABI formats must remain unchanged; no automatic old-state migration.

## Files and ordered acceptance gates

Expected implementation files are `fluid_world.hpp/.cpp` for the explicit
momentum-mode selection/state/rollback, new `occupied_staggered.hpp/.cpp` for
state initialization and mechanical identities, and new `staggered_remap.hpp/.cpp`
for 1-D corner gathering/intersection/rebalancing/scattering. Reuse occupied
geometry/EOS/pressure functions; keep the closed path and isolated EOS formulas
unchanged. Add a dedicated staggered test file/target, extend the existing
actual-world rarefaction runner to select the new mode, and add only a separate
experimental snapshot helper if persistence is included. Root CMake registers
the new sources/tests. No managed/ONI bindings are part of this proposal.

First validate retained nodal energy/P, explicit initialization, stationary and
uniform translation, dt-to-zero Fourier amplitude, nonzero alternating-pressure
response and small-amplitude acoustic phase/speed. Next validate the positive
dual remap, changing masses/cut indices, both reflected boundaries, zero-motion
identity and full rollback. Then revalidate all liquid/hydrostatic/gravity and
representability cases, followed by separate timestep/grid refinement of the
ideal-gas fan and its actual front position and speed. Run MSVC, strict GNU and
ASan/UBSan with source/binary manifests. The full gas/vacuum gate remains false
until those measurements pass; no larger timestep/performance or gameplay
claim follows from them.
