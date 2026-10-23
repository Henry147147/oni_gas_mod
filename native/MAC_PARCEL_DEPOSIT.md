# Moving material admission

`MacClosedWorld::with_moving_parcels` is an additive boundary operation for a
physical mixed parcel. Existing mass/temperature point sources are unchanged.
The owner queue supplies one ordered atomic batch for one physical mixture.
The core changes no clock; an accepted batch increments revision once. Normal
refusal returns the original world, no receipts, and a reason. It invokes no
fallback or trace sink. The caller retains the exact reservation for retry or
return; a refusal must not become an owner fault.

Each `MacMovingParcel` supplies a local cell and registry species index,
positive binary64 mass, full-range U/K/G/Jx/Jy and every known passive identity.
U, G and J are signed; K is nonnegative and must support the supplied momentum.
G uses the same core-local datum as precise extraction. The game adapter maps
the public cell once and exposes saved geometry/gravity to the managed caller.
There is no assumed temperature or gravity and no conversion from U back to a
source temperature. Only the complete candidate's resulting EOS is validated.

The scalar wire is the existing canonical `MacEnergy` representation: finite
normalized binary64 mantissa and signed decimal `MacExponent`. Zero is +0 with
exponent 0. Exponents are never narrowed to an integer or floating exponent.
The owner owns framing, tokens, durable request bytes and receipt encoding.

The operation adds mass, splits each incoming momentum component between the
cell's two dual faces, and books fixed-face reaction as impulse on the fluid.
It measures actual old/new grid inertia and kinetic energy. Inelastic mixing
and the difference between carried and grid-centre potential enter U, using
full-range accounts. Final EOS, primary representability and U+K+G/J audits
must all succeed before the candidate is returned.

Nominal admission is the whole offered batch. Larger capacity partials require
the caller to offer a common smaller batch. Primary mass-write remainders are
bounded to at most 2^-24 of each offered component (the binary32 relative
rounding scale); a zero or larger unresolved increment refuses the whole batch.
Actual debits and all differences remain explicit. The game kernel accepts
only its saved binary64 primary range; unsupported mechanics refuse instead of
being flattened. Other kernels retain their existing range-capable fields and
existing checkpoint encodings. No new physical format or profile is selected.

## Settlement authority

`receipt.consumed` describes the nominal scaled source portion before primary
rounding. It is **not** a substitute for measured grid credit. Each `grid`
field is a `MacEnergySum`: mass, U/K/G/J and per-ID passive changes are exact
additive changes of represented native operands. Iterate every component;
`value()` is only an estimate. This matters for `new_mass - old_mass` when an
old minority tail is far below the new primary's significand.

With W denoting impulse on the fluid, the sign conventions are:

- Delta J = admitted J + W, within the existing local primary audit.
- K mixing heat = admitted K - Delta grid K.
- Potential work = admitted G - Delta grid G.
- Delta grid U = admitted U + mixing heat + potential work, plus measured
  primary-write rounding differences.

Heat/work fields are additive expansions too. Exact settlement can derive the
source debits from grid U minus heat/work, grid K plus mixing heat, grid G plus
potential work, and grid J minus wall impulse. Retain every source-minus-credit
remainder, including minority mass and passive tails; do not turn it into a
new sink or a physical repair term. Core residual/bound fields are diagnostics,
not permission to omit a measured component. Existing `MacEnergyAccount`
operand bounds remain unchanged.

Focused controls cover reference/production/game admission, positive/negative
formation energy, opposing J, repeated deposits, wall reactions, exact grid
credit expansions, old minority inventory, all known passive IDs, whole-batch
refusal, full-range exponents, checkpoint continuation and game workers1/12.
Native job/save and actual liquid-particle acceptance belong to the owner and
managed integration. They are not established by these core controls.
