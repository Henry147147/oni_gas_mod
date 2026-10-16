# Scaled primary parcel contributions

This records the original binary64-bulk-flux cohort. The subsequent
[full canonical/flux representation](MAC_CANONICAL_RANGE.md) retains the
formerly rejected below-range bulk flux; the updated suite now requires that
positive result. Unresolved constituent cell storage still rejects atomically.

Five new native groups and all prerequisites pass on Windows and Linux.
This work addresses the retained
[Linux helium-parcel rejection](MAC_ENERGY_TRANSPORT.md), without changing the
Riemann inputs, timestep, refinement or error limits. It does not repair the
separate Windows collision-accuracy failure.

An extensive transfer need not fit binary64 before it reaches an accumulator.
When any field's explicit projection would round in the subnormal range or
become zero, the complete parcel retains a normalized prototype and a common
reference/actual volume pair. Constituents, signed U and sparse passive fields
all use that same scale. An exactly projectable parcel keeps the original
actual-amount representation. No constituent is dropped or independently
limited. The unit-neutral `MacScaled` aliases reuse the unchanged tested
significand/exponent arithmetic introduced for derived energy.

Donor accounts use full contributions at the existing 128-epsilon bound on
actual operands. Target accumulation retains full contributions until one
explicit cell-storage projection, with an exact additive rounding account at
the same bound. Every nonzero result must retain its sign and remain nonzero.
Primary cell inventories, bulk mass fluxes, canonical momentum and EOS state
remain binary64. A destination trace that cannot be stored accurately still
rejects atomically; so does a poorly resolved donor denormal or unrepresentable
bulk mass flux. This is wider intermediate range, not arbitrary-precision
physical state or a trace cutoff. Normal roundoff into existing positive
inventories is accounted for against the actual operands.

The full rounded parcel is normalized back to the donor volume for EOS
admissibility. Validation checks the original prototype as well. Existing
shared spatial limiting, positivity constraints, EOS references, pressure
work, timestep and conservation bounds remain unchanged.

Five new native groups are declared before the first build/run:

1. The original helium scale (8.5868194183540713e-325 kg), plus two larger
   rounded-subnormal cases, on a uniform periodic gas grid. Full positive
   contributions must survive and uniform stored state must stay identical.
2. A positive sparse passive near 1e-350 per parcel must survive accumulation
   into its existing inventory, retaining its identity and source immutability.
3. Four exact quarter-denormal helium contributions into a cell initially
   lacking helium must accumulate into one exactly representable minimum
   positive binary64 helium inventory. Neither source nor plan may change.
4. An unresolved new destination trace, the original denormal-passive donor
   and an unrepresentable bulk flux must still reject atomically.
5. Negative, nonfinite and inconsistent reference volumes must reject without
   changing source or plan.

Saved records carry the prototype, scale and actual significand/exponent
values. A separate data-only exact-fraction review checks full contributions,
all constituent/passive donor and target balances, shared reconstruction,
dual fluxes/J/K/heat, stored U and ideal-gas pressure/temperature. It must
reject an altered full constituent, reference volume and new target trace.

Fresh strict Windows/MSVC and Linux/GCC15 correctness builds retain their
first results, including failures. Predeclared bounds: aggregate build 180 s;
new native/review 60 s each; range and primitive native/review 30 s each;
derived-energy transport 60 s; reconstruction 180 s; closed/gravity 60 s;
512 KiB per raw pipe. After all scoped native controls and separate reviews
pass, one Linux strong-flow retry may use its original 180-s/2-MiB bounds.
No Windows failed collision-gate replay is justified by this range change.
These runs provide no production-performance, free-surface, phase, game or
shipping acceptance. All earlier failed scientific controls remain evidence.

## Results and next momentum rejection

Both first builds pass with the existing strict compiler flags. The following
native groups and separate saved-data checks pass on each platform; each
reviewer rejects three altered records:

| Control | Native groups | Saved checks |
| --- | --- | --- |
| Scaled primary parcels | 5 | 7,681 |
| Wide derived-energy integration | 4 | 4,530 |
| Range arithmetic | 7 | 21,019 |
| Original transport | 7 | 3,888 |
| Material reconstruction | 5 | 13,325 |
| Closed pressure/transport | 8 | 6,530 |
| Smooth gravity | 4 | 16,462 |

The original helium contribution remains positive in the actual parcel, even
though its explicitly flagged binary64 projection is zero. All five saved
fixture profiles retain constituent/passive identities, donor/target accounts,
actual U, pressure and temperature. Four exact quarter-denormal contributions
produce a new stored helium inventory of 2^-1074 kg. An unresolved destination,
poorly resolved donor denormal and below-range bulk flux still reject atomically.

After all fourteen prerequisite native/review captures pass, the single Linux
strong-flow retry gets past the original helium-parcel creation but still
fails before any completed Sod profile. A private instrumented first-rejection
diagnosis changes only error text/site. It records incoming normal momentum
from source J = 2.5697272365554806e-293 kg·m/s, mass flux
4.2934097091770358e-297 kg and donor inertia 3.0517578125e-05 kg. Exact arithmetic
on those binary64 operands gives **3.615258007097827e-585 kg·m/s**. The existing
strict momentum share projects it to zero and rejects it before accumulation.
Canonical momentum storage and its checks have not changed in this cohort.

The strong-flow gate remains failed. No full or partial strong-profile review
is invoked for its empty profile set, and the Windows collision gate is not
repeated. The original scientific Riemann/gravity sources and physical criteria
remain unchanged. The diagnostic has separate 90-s build/20-s native bounds;
all captures close with EOF, without timeout or pipe overflow.

Private captures are
`local/occupied-staggered-validation/mac-primary-parcel-{windows,linux}-r1`.
Completed scoped-review SHA256 values are
`0df5b43c6309563367b07325f249877a0907a11edcc5cf474ab76b822fade430`
(Windows) and
`c78c0e34e38dd675be2b1eb252e8bff4dcc9121fb6c9830f8c74a31b42fdd034`
(Linux). They bind frozen source, compiler/binary identity, all prerequisite
captures/reviews and the retained Linux momentum diagnosis. No independent
agent review or Unity launch occurred. The default game DLL is unchanged.

The subsequent [momentum contribution cohort](MAC_MOMENTUM_PARCELS.md) passes
four new groups and all these prerequisites on both platforms. Its Linux retry
passes incoming sharing, then rejects an accumulated open-face canonical J
below binary64 range. Wider intermediate transfers alone do not extend stored
canonical state or establish strong-flow/game acceptance.
