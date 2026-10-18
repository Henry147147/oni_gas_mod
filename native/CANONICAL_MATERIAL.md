# Canonical material owner (dormant 1-D experiment)

This experiment keeps canonical nodal momentum as primary state, with stored
internal energy separate from resolved kinetic energy, on a 1-D material mesh
with a full consistent mass matrix. Its targets are excluded from the default
build and from CTest, and nothing else uses it; the production solver is
`MacClosedWorld` ([SOLVER.md](SOLVER.md)). There is no 2-D transport, vacuum or
phase topology, remap, persistence or colony integration.

| Library | Sources | Content |
| --- | --- | --- |
| `ma_canonical_material` | `canonical_material*.cpp` except `_diagnostics` | Owner, pressure step, gravity, diagnostic bytes |
| `ma_retained_scalar` | `retained_scalar*.cpp` | High/low stored-value arithmetic |
| `ma_canonical_diagnostics` | `canonical_material_diagnostics.cpp` | Failure witness, JSON, arithmetic preflight |

Headers are in `native/include/mixed_atmospheres/experimental/`. Everything
builds with warnings as errors and strict arithmetic (`/fp:strict`;
`-fno-fast-math -ffp-contract=off -frounding-math`).

## Owner and retained state

`canonical_material.hpp` defines a deep immutable owner: seven contiguous
material regions, 8-33 nodes, at most 32 affine reference pieces and 1-4 gas or
liquid profiles. It retains the consistent mass matrix, primary nodal momentum
and reference constituent measures. Velocities, moments, resolved kinetic energy
and EOS caches are derived in one completed snapshot; an owning read view
outlives the inputs and the originating state. Supplied U is kept bit-exact;
temperature initialization records its elastic addition. Resolved kinetic
variance never becomes heat.

Relative positions, origin, nodal momentum, owner U and time are retained
high/low values; scalar U, V and time are explicit projections. Volumes derive
from nodal coordinates (retained difference certificate, then exact area
products), with the signed difference from the scalar projection reported.
Initial momentum comes from complete matrix-product pairs with retained row-sum
remainders. The EOS consumes the scalar projections, so the EOS itself is not
retained-exact.

## Fixed-h pressure advance

`canonical_material_advance_ready()` is true. A call uses complete stored
momentum in the nonlinear force residual, stores free momentum from the
force-impulse pair, solves the consistent system for velocity, and forms
constrained momentum with required impulse `L*delta_u + c0 - c1` (final
compatibility measured, not assumed). Origin, coordinates, balanced isentropic U
and time update as retained values; the prescribed-displacement condition is
verified as an exact zero expression before boundary work. Impulse representation, geometry,
actual-EOS m/U/V, work/entropy comparisons and energy/momentum/moment ledgers
are checked before atomic publication. There is no remap and no retry; failures
keep the input and previous output and seal a witness.

`CanonicalFiniteExpression` stores a bounded signed operand list and its
Neumaier projection. The energy, momentum and moment identities use full stored
changes and exact FMA products and keep the cap-force work defect and force
assembly defect; no discrepancy becomes heat, zero or a retry.

Linear algebra runs in physical node coordinates: diagonally scaled Cholesky
plus two fixed residual-correction passes on full primary momentum; a velocity
cache never supplies the solution. A global even/odd congruence used earlier
mixed distant heavy-liquid and light-gas rows through cancellation. The
gas-only isentropic inverse is analytic
([thermodynamic reference](experimental/README.md#gas-only-small-work-precision)).

**Gravity.** `gravity_down_m_per_s2` is an immutable nonnegative constant toward
negative x (default 0); negative, nonfinite and positive subnormal values fail
before any EOS query. With the consistent mass matrix L:

```text
w_j = sum_k L_jk,   G_j = projection(-g * w_j)
F_j = F_pressure,j + F_exterior,j + G_j
delta_PE  = g * sum_j (w_j * completed_physical_displacement_j)
W_gravity = sum_j (G_j * completed_physical_displacement_j)
D_gravity = W_gravity + delta_PE
```

`G_j` is the force actually used everywhere; the ledger uses potential-energy
changes, never large absolute potentials. Bounds: 256-epsilon arithmetic and
`2e-11` physical energy, with no unit or formation-energy floor.

**Diagnostic bytes.** `canonical_material_diagnostic_bytes` exports tagged,
length-delimited raw binary64 state, including gravity ledgers (schema v4;
readers also accept v3). It makes no EOS query and is **not** a save format.

## Current source validation

All three suites pass on strict Windows/MSVC and WSL Linux/GNU, each checked by
an exact-rational data-only reviewer.

- **Gravity** (`ma_canonical_gravity_tests`, 4 groups): nine-node free fall,
  eight-node water/air column, eleven-node mixed state with fixed and prescribed
  ends, invalid gravity. Free-fall velocity error `2.98e-13` (Windows) and
  `4.69e-13 m/s` (Linux) against `5e-11`. Hydrostatic speeds `4.89e-12` and
  `2.44e-12 m/s` are checked against response bounds (`3.54e-10`,
  `1.72e-10 m/s`) derived from measured EOS imbalance and residuals, not an
  arbitrary floor. Review: 540 identities, 81 stored updates, 12 closures.
- **Pressure step** (`ma_canonical_retained_step_tests`, 4 groups): nine nodes
  (N2/O2/CO2/Cl2, supplied U, free ends), ten nodes (N2/water, moving prescribed
  boundary) and eleven nodes (`h=2^-56 s`, nonuniform velocity), two at time
  `2^40 s`; mass bits unchanged; zero h, aliasing and unsupported rounding
  reject atomically. Review: 566 identities, 83 updates, 12 closures that sum to
  exact zero; force residual at most `4.70e-14` (target `2e-12`).
- **Initialization** (`ma_canonical_retained_initialization_tests`, 4 groups):
  coordinates `-2^-60` and 0 that round together keep a positive interval, low
  momentum bits, supplied U, view lifetime, serialization under upward rounding,
  and a pre-EOS rejection that keeps the previous publication.

Lessons: free fall first failed through a bisection tolerance in the gas
inverse, and the `h=2^-56 s` case through a one-ULP velocity-cache error that
pushed a `3.8e-29 J` increment below U's low-component spacing; both were fixed
without relaxing a check.

**Not established:** cross-platform bitwise determinism. EOS-derived values
differ by one or two ULPs between Windows and Linux (platform-dependent
`long double` in the EOS), so snapshots differ. Physical coverage is 8-11 nodes,
not the full capacity.

## Open: historical translation gate

`ma_canonical_material_tests <case|suite>` is the earlier seven-case target on
the scalar owner. Its `translation` case (uniform translation, nonuniform
dyadic mesh) failed: the relative increment `-2^-65 m` added to `-1.4375 m`
produced no change, a typed `numerical_precision` failure with state unchanged.
It motivated the retained representation and has not been rerun or accepted.
A private actual-EOS consistent-mass prototype (not in this repository) also
rejected a prescribed-boundary case ("Unrepresented canonical impulse") without
keeping its operands, which is why the witness below exists.

## Design background

Exact-rational studies outside this repository showed: corner-mass
representations with fixed positive-definite local mass matrices force one
continuous velocity and so change both boundary caps for localized velocities;
keeping the source velocity kinks in the target partition (at most 32 pieces)
recovers all coarse moments, momenta, owners and caps; and pushing reference
pieces into moving geometry gives compatible pressure and material work. The
owner's affine reference pieces, consistent mass matrix and primary nodal
momentum follow from these.

## Retained increment arithmetic

`retained_scalar.hpp` (`ma_retained_scalar_tests`, 5 groups) holds a normalized
high/low value updated by six TwoSums. It keeps increments below the high part's
ULP, returns a two-component represented increment plus a signed third
remainder, and rejects an erased or reversed nonzero increment. This and the two
operations below reject nonfinite or subnormal arithmetic, overflow and
unsupported floating-point controls, leave inputs unchanged, and return signed
remainders for the caller's ledger (never heat). Their numerical rows match
across platforms.

## Retained scalar multiplication

`multiply_retained_scalar` (`ma_retained_scalar_product_tests`, 5 groups, 136
products) recovers both component product errors by FMA (Algorithm 3.5 of
[Ogita, Rump and Oishi](https://ogilab.w.waseda.jp/ogita/math/doc/2005_OgRuOi.pdf))
and compresses to a normalized pair plus four remainders:
`coefficient * (oldHigh + oldLow) = newHigh + newLow + sum(remainders)`.
Subnormal results reject; a zero FMA error must be proved exact by an integer
significand check; a nonzero product rounded to zero rejects.

## Differences of retained values

`subtract_retained_scalar` (`ma_retained_scalar_difference_tests`, 7 groups, 290
differences) uses fixed-size zero-eliminating Grow-Expansion (Theorem 10 of
[Shewchuk](https://people.eecs.berkeley.edu/~jrs/papers/robustr.pdf)): at most
four nonoverlapping components, the top two stored and the rest returned as two
remainders. A nonzero difference cannot become zero or change sign.

## Failure witness and arithmetic preflight

`CanonicalFailureWitness` (`canonical_material_diagnostics.hpp`) is caller-owned.
A producer copies its operands and seals a typed failure before throwing
`CanonicalFailure`, which derives directly from `std::exception` so a
`std::runtime_error` retry handler cannot catch it. It holds identity token,
step, stage, node/owner indices, predicate sequence and resource counts, plus up
to 248 scalars and eight rows of 33 values (512 slots). Values are classified by
their bits, so signed zero, subnormals and NaN payloads survive FTZ/DAZ.
Capacity failures seal with `capture_valid=false`; a sealed failure is never
overwritten.

The JSON encoder (schema `ma-canonical-failure-witness-v1`) writes at most
32 KiB into a caller buffer without allocation or I/O. Numbers are
`[flags,"16-hex-binary64-bits",finiteDecimalOrNull]` (flags presence=1,
finite=2, negative=4, zero=8, subnormal=16); the hex bits are authoritative and
decimals use integer nearest-even rounding to 17 digits, independent of the FP
controls.

Preflight requires binary64 on x86-64 with nearest rounding, FTZ/DAZ off and all
SSE exception masks set; it reads `fegetround` and MXCSR and rejects anything
else without changing them. This is not proof of compatibility with Unity.

## Build and review

```text
cmake --build build --target ma_canonical_diagnostics_tests ma_retained_scalar_tests ma_retained_scalar_product_tests ma_retained_scalar_difference_tests ma_canonical_retained_initialization_tests ma_canonical_retained_step_tests ma_canonical_gravity_tests ma_canonical_material_tests
build/ma_canonical_diagnostics_tests > diagnostics.stdout
python native/tests/review_canonical_material_diagnostics.py diagnostics.stdout
build/ma_retained_scalar_tests > scalar.stdout
python native/tests/review_retained_scalar.py scalar.stdout
build/ma_retained_scalar_product_tests > product.stdout
python native/tests/review_retained_scalar_product.py product.stdout
build/ma_retained_scalar_difference_tests > difference.stdout
python native/tests/review_retained_scalar_difference.py difference.stdout
mkdir init-out && build/ma_canonical_retained_initialization_tests init-out > init.stdout
python native/tests/review_canonical_retained_initialization.py init.stdout init-out
mkdir step-out && build/ma_canonical_retained_step_tests step-out > step.stdout
python native/tests/review_canonical_retained_step.py step.stdout step-out
mkdir gravity-out && build/ma_canonical_gravity_tests gravity-out > gravity.stdout
python native/tests/review_canonical_gravity.py gravity.stdout gravity-out
```

Owner tests need a fresh existing directory for snapshots. Reviewers run no
native code; they check identities and witnesses exactly, reject deliberately
corrupted copies, and hash rather than decode the remaining legacy fields. The
16 diagnostics groups give identical output on Windows and Linux.
