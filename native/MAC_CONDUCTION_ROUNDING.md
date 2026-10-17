# Stored-energy rounding in internal conduction

The occupied MAC conduction operator still solves the same backward-Euler
network with harmonic half-cell resistance. Constant heat capacities, masses,
volumes and canonical momentum retain their existing meaning. This change
addresses conversion of that heat solution into binary64 cell internal energy.

Independent nearest rounding can repeatedly cool a hot cell while every small
incoming update rounds away in its neighbors. The closed system then loses
represented energy. A licensed water control with +2,000 J / -500 J exposes
this in the previous compatibility build v6: after 60 native frames the
independent U+K balance is -0.008544921875 J, exceeding its unchanged
0.005945847213 J endpoint roundoff bound. The failed reports remain retained.

For each connected conductive component, the operator now forms the exact
additive expansion of `old U + solved heat` for each cell and finds its two
neighboring binary64 endpoints. It chooses among those endpoints to preserve
the component's exact original U sum when that sum is representable there.
The endpoint spacings are powers of two: descending spacing gives a greedy
exact-sum selection, with the larger fractional request preferred for equal
spacings and cell index breaking ties. No cell moves outside its rounding
bracket. Independent components are rounded independently, so an insulating
wall cannot carry a rounding correction.

Widely separated cell-energy spacings need not admit an exact closed sum
within those brackets. Such a component retains the original nearest
projections and the existing local/global operand accounts. It does not force
a minimum heat flux or move a small-energy cell farther to compensate for a
large-energy cell's rounding. The component report counts unavailable
rounding components and adjustments explicitly. This is not a general
arbitrary-precision energy representation.

For this constant-capacity, fixed-mass/volume operator, entropy change uses
`Cv * log1p(delta U / (Cv * T))`, with the represented U difference formed as
an additive expansion. Subtracting two rounded EOS temperatures can erase or
misstate the entropy of small liquid heat transfers. The existing entropy
and energy bounds remain unchanged.

`redistributed_energy` and the native ABI's `RedistributedEnergy` now count
represented cell-U changes. Requested redistribution has a separate component
report field. An unresolved sub-ULP request can therefore have positive
requested heat, zero physical redistribution and zero entropy change. It is
counted as unresolved and cannot pass a conduction-progress gate.

Regression coverage includes the independent two-node backward-Euler formula
and time refinement, 2,400 small-water stages with exact component U sums,
separate components behind an insulating wall, and a constructed gas/water
pair whose different energy spacings cannot admit neighboring conservative
rounding. Source state, masses, passives and canonical momentum remain intact.

`sealed-water-small-heat` is a licensed-native precision/accounting control:
it admits +2,000 J / -500 J, requires the conserved balance and explicit
unresolved updates, and records `internalConductionPassed: false`. The
resolvable-gradient control `sealed-water-heat` uses +2 MJ / -0.5 MJ, requires
positive represented redistribution and entropy, and checks warming of both
the source and a conductive neighbor. The rendered
`native-liquid-heat-domain` / Windows `-NativeDomain water-heat` scene uses the
same resolvable input and normal save/reload checks. Gas controls keep their
original +2,000 J / -500 J input and fixed tolerances.

The Linux and Windows-under-Wine simulation-library controls pass the
resolvable gradient with matching 4.375 J represented redistribution,
1.582595547e-5 J/K entropy increase and zero measured U+K drift. Both small
controls conserve energy and report zero represented redistribution/entropy
with 11,536 unresolved updates. All 13 Linux and Windows-under-Wine native
tests pass. The frozen `67e1475` / compatibility v7 rendered Linux heat scene
also passes its ordinary save and fresh-process reload, including all 120
exact continued frames. Windows Unity verification remains open.
This does not certify fluid/solid exchange, variable material
properties, phase changes, full-range heat accuracy or a playable overhaul.
