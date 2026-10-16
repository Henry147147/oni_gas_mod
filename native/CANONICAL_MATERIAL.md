# Canonical material-state work

The production timestep remains unfinished. The rejected consistent-mass
pressure-step experiment is unchanged. Its failing impulse check did not retain
the node and component operands needed to identify the exact numerical cause.
The prospective replacement retains canonical momentum as primary state and
keeps stored internal energy separate from resolved kinetic energy. No new
state owner, pressure step, remap, runtime mode or physical acceptance is supplied
by the diagnostics component alone. The separate experimental owner below has a
historical failed scalar-translation gate and is not selected by the game. The
current retained operator supports initialization, inspection and a complete
fixed-h pressure advance, with the bounded new controls below. Production 2D
transport, vacuum/phase topology, remap, persistence and colony integration are
still unfinished.

## Experimental material owner

`experimental/canonical_material.hpp` adds a deep immutable owner for seven
contiguous material regions, 8–33 retained nodes, at most 32 affine reference
pieces and 1–4 supplied gas/liquid profiles. It retains the full consistent mass
matrix, primary nodal momentum and reference constituent measures. Derived
velocities, fine/owner moments, resolved kinetic energy and actual EOS caches
share one completed snapshot. An owning read view survives retirement of both
caller inputs and its originating state. Supplied stored internal energy is
preserved; temperature initialization records its separate elastic addition.
Resolved kinetic variance is never converted to heat.

The retained fixed-h pressure step uses complete stored initial momentum in its nonlinear
residual, stores free momentum from force, solves the full consistent system for
velocity, then computes dependent constrained momentum and measured reactions.
It checks actual impulse representation, geometry, actual-EOS stored m/U/V,
independent work/entropy comparisons and finite energy/momentum/moment ledgers
before an atomic immutable publication. It performs no remap or timestep retry.
Unsupported topology, range and resource failures preserve the complete input
and previous output and seal a separate bounded witness.

The explicit `ma_canonical_material`, `ma_canonical_material_tests`,
`ma_canonical_retained_initialization_tests`, `ma_canonical_retained_step_tests` and
`ma_canonical_gravity_tests` targets
are excluded from default builds and CTest. The library has one-way dependencies
on `ma_core` and retained arithmetic; the game DLL and world dispatch do not depend on it.
`canonical_material_diagnostic_bytes` exports tagged, length-delimited data for
complete state comparisons, including model/registry and every report field.
Its v4 schema includes the immutable gravity parameter, complete primary high/low values, geometry certificates,
scalar projections, initialization product/remainder ledgers and the complete
retained-step diagnostic section, including gravity force/work/potential ledgers. It uses raw
binary64 bits and no EOS query. It is not a restart/save format.

## Retained initialization and inspection

The actual immutable owner now retains high/low relative positions, origin,
canonical nodal momentum, stored owner internal energy and completed time.
Compatibility vectors and scalar U/V/time fields are explicitly derived
projections. Every owner and fine-piece volume derives from the complete nodal
coordinates: a retained difference certificate followed by exact area-scaled
product terms. The signed difference between those terms and the scalar volume
projection is explicit. No second independently mutable volume is introduced.

Initialization constructs canonical momentum from complete matrix-product pairs
and retains each row-sum remainder. Velocity decoding consumes the high/low
momentum and keeps its existing compatibility bound. Supplied U preserves its
exact input bits; temperature mode records the retained caloric/elastic addition.
The existing EOS still accepts scalar U/V projections. Independent closure checks
include retained U and complete geometric-volume terms, with unchanged accuracy
bounds; this is not an exact retained-arithmetic EOS solver. A child arithmetic
failure is copied into the parent's model/stage/node/owner witness.

At commit `62ee096`, four initialization groups pass on their first strict Windows/MSVC and WSL
Linux/GNU invocations. Two eight-node nitrogen inputs include adjacent absolute
coordinates `-2^-60` and zero which become the same rounded relative coordinate
with a 1 m origin. Their retained interval remains positive; the two areas give
volumes `1.1275702593849246e-18` and `5.421010862427522e-19` m3. Other controls check
all eight nonzero low momentum components in the moving input, supplied-U bits,
separate kinetic variance, owning-view lifetime, raw serialization under upward
rounding, the closed advance guard, and a pre-EOS reference-product rejection
which preserves the previous publication and the parent's failure context.

The independent data-only checker decodes each 35,242-byte snapshot's 4,663-byte
primary/owner-geometry prefix and hashes the whole snapshot. Exact `Fraction`
checks cover coordinates, geometric differences/volumes/projection residuals,
128 momentum products and their stored-sum remainders. It checks scalar gas EOS
readings using the existing 2e-10 accuracy, verifies 30 copied witness numbers,
and rejects two copies with corrupted primary low bits. It does not semantically
decode every remaining diagnostic field or launch a native process.

The cross-platform all-row bitwise comparison **fails**: 14 of 160 recorded rows
differ. Retained positions/momentum/origin/time, owner geometry certificates,
supplied-U inputs and their stored values match. Temperature-initialized U differs
by at most two ULPs; cached pressure and temperature differ by at most one ULP.
The existing EOS uses platform-dependent `long double` intermediates; no exact
cause is claimed for every last bit. Both independent EOS accuracy checks pass
without changing bounds. Complete snapshot hashes differ, and bitwise snapshot
determinism is not established. The failed comparison is preserved without a
native rerun.

Private evidence is under
`local/occupied-staggered-validation/retained-owner-init-windows-r1` and
`retained-owner-init-linux-r1`. Both first native exits are zero, both raw pipes
reach EOF and close without capture errors, and complete source/executable hashes
are retained. The 180-second aggregate build, 30-second invocation, 128-KiB pipe
and 2-MiB snapshot bounds are unchanged. EOS initialization runs; no pressure
advance, old failed physical gate, game, performance or first-playable gate runs.

That initialization stage deliberately closed public advance. The current
retained pressure implementation below replaces that guard; the initialization
control's rejection check now uses zero h and still enters no EOS/advance physics.
The v2 evidence and its failed cross-platform bitwise comparison remain intact.
The current data checker accepts the v3 and v4 diagnostic schemas.

The updated initialization/zero-step target also passes four groups on its first
v3 Windows/MSVC and WSL Linux/GNU invocations. Its independent decoder checks
128 exact momentum products, 32 witness numbers, both 35,975-byte snapshots and
two corrupted copies per platform. Evidence and original source copies are under
`retained-owner-init-v3-windows-r1` and `retained-owner-init-v3-linux-r1` in the
same private evidence directory. A scope-count audit mistakenly matched the
diagnostics translation unit as well; the wrapper continued before resolving
that assertion. All eleven matched GNU units were then verified to use the
required strict flags. That failed audit is retained, with no native rerun.
Historical v2 captures use the checker from `62ee096`; the current command below
accepts v3/v4 captures. No first-stage failure is replaced by the current evidence.

```text
python native/tests/review_canonical_retained_initialization.py capture.stdout snapshot-directory
```

## Complete retained pressure operation

The public standalone advance now consumes full primary momentum in the
force residual and stores free momentum from the complete force-impulse pair.
Each stored update retains its full before/increment/sum/change certificates;
omitted sum components remain explicit signed remainders. The erasure/sign test
applies to the complete physical increment. It does not require every smallest
rounding component to survive independently. Constrained endpoint momentum is
constructed from full row-product pairs. Its required impulse includes both
initial and final compatibility, `L*delta_u + c0 - c1`; final compatibility is
measured rather than assigned zero.

Recentered origin/relative coordinates, balanced isentropic U and completed time
all use retained updates. Physical displacement and owner volume changes come
from complete completed-state certificates. Prescribed displacement is checked
as an exact zero expression before boundary work, using a fixed bounded
Grow-Expansion classifier. It supplies no replacement state. The existing EOS
continues to consume explicit scalar U/V projections, with unchanged independent
closure, work-pressure, entropy and arithmetic bounds.

`CanonicalFiniteExpression` stores a bounded signed operand list and its scalar
Neumaier projection; its exact projection residual is that operand sum minus the
stored projection. It is diagnostic data, not additional primary state or an
arbitrary-precision fallback. The finite energy/momentum/moment identities use
full stored changes and exact FMA products before projection. They retain the
cap-force work defect and the difference between actual assembled force and
its individual components. Energy includes displacement times that assembly
defect; momentum includes its impulse. No discrepancy is silently turned into
heat, a theoretical zero or a timestep retry.

The first strict Windows/MSVC and WSL Linux/GNU builds and native invocations
pass four new groups. They advance three original inputs: nine nodes with
nitrogen/oxygen/CO2/chlorine, supplied U and free zero-exterior-pressure ends;
ten nodes with nitrogen/water and a moving prescribed right boundary; and eleven
nodes with a fixed left origin, nonuniform interior velocity and `h=2^-56` s.
The first two use `h=2^-17` s. The third retains nonzero relative movement behind
unchanged scalar coordinates. Two inputs retain h at completed time `2^40` s,
whose ordinary scalar projection does not change. All constituent mass bits,
including chlorine at reference density `3e-6` kg/m3, remain unchanged. Three
additional typed negatives cover zero h, aliasing and unsupported rounding,
with unchanged input/previous output and zero constitutive charges/factorizations.

The independent data-only decoder reconstructs 494 expression identities and
83 stored-update certificates per platform. It checks constrained product/sum
records, full geometric volumes, per-node compatibility and actual impulses,
cap/assembly work, physical energy/momentum, stable first moments and the full
bilinear change. All twelve complete accounting remainder expressions sum to
exact zero as real `Fraction` sums. Their finite Neumaier projections can still
be tiny nonzero values; those are retained and checked against their unchanged
bounds. Physical energy error is measured separately, not forced to zero. An
independent exact force residual is at most `4.70e-14`, below the existing
`2e-12` target. Three copies with altered normal primary low bits reject.

Six complete v3 snapshots per platform remain below 191 kB each. The decoder
reads their primary/owner-geometry prefixes and retained-step suffixes and
hashes the opaque intervening diagnostic fields; it does not claim a semantic
decode of every cache/model field or provide a restart reader. The raw numerical
outputs are not bit-identical: 70 of 440 rows differ between platforms. Fixed
input and matrix rows match; each platform independently satisfies all exact
accounting and existing EOS/force checks. Whole-snapshot determinism and
cross-platform colony saves are not established.

Evidence is private under
`local/occupied-staggered-validation/retained-pressure-windows-r1` and
`retained-pressure-linux-r1`. Both native exits are zero, with both raw pipes
reaching EOF and closing without timeout/cap/capture errors. Complete original
source copies, source freezes, executable hashes, strict build flags, inputs,
snapshots and exact data reviews are retained. The build/invocation/pipe/snapshot
bounds remain 180 s, 30 s, 128 KiB per pipe and 2 MiB per snapshot. There is no
performance, game or first-playable acceptance in these controls, nor an
independent agent implementation review. No old failed physical gate was rerun.

`canonical_material_advance_ready()` now reports this standalone implementation
available. Neither the C ABI, default DLL nor world dispatch selects it. Interface
capacity remains 8–33 nodes/seven owners/1–4 supported profiles; the new physical
controls cover 9/10/11-node inputs, not arbitrary inputs within those limits.
The historical seven-case target is retained as old evidence, not the current
verification target. Production transport/remap, broader physical validation,
game ownership, performance, persistence and surviving colonies remain required.

```text
python native/tests/review_canonical_retained_step.py capture.stdout snapshot-directory
```

## Constant gravity and complete physical-node solves

`gravity_down_m_per_s2` is an immutable, nonnegative constant directed toward
negative material x. It defaults to zero. Negative, nonfinite and positive
subnormal inputs fail before any constitutive query or factorization, preserving
the caller's input and previous output. This remains a one-dimensional model.

The nodal body load uses the same consistent mass matrix as momentum:

```text
w_j = sum_k L_jk
G_j = projection(-g * w_j)
F_j = F_pressure,j + F_exterior,j + G_j
delta_PE = g * sum_j (w_j * completed_physical_displacement_j)
W_gravity = sum_j (G_j * completed_physical_displacement_j)
D_gravity = W_gravity + delta_PE
```

The weight, products and completed displacement use complete finite operand
lists and explicit FMA errors. `G_j` is the actual finite force used in pressure
trials, momentum updates, constrained reactions and residuals. The energy ledger
includes stable potential *changes*, rather than subtracting absolute potentials
at large coordinates. Its signed gravity projection defect remains explicit.
Momentum includes the actual gravity impulse. The existing 256-epsilon arithmetic
and `2e-11` physical energy bounds remain unchanged; their scales include actual
gravity work and potential change without a new unit or formation-energy floor.

Linear algebra now operates in physical node coordinates. A diagonally scaled
Cholesky factorization replaces the global even/odd congruence, whose cancellation
mixed distant heavy-liquid and light-gas rows. Row, factor and triangular
products retain their explicit FMA errors. Two fixed residual-correction passes
reuse the same factorization. Decoding residuals consume full primary high/low
momentum and prescribed contributions before scalar RHS projection. A velocity
cache never supplies the solution. The original 256-epsilon physical-row bound,
factorization budget, Newton stopping criterion and physical timestep remain
unchanged. No failure selects a fallback or a new precision or timestep.

The gas-only isentropic inverse also uses its analytic logarithmic volume
relation; see the [thermodynamic reference](experimental/README.md#gas-only-small-work-precision).
Mixed gas/liquid and liquid roots retain their existing algorithms.

### Preserved first failures and diagnostic reproduction

The first gravity controls on Windows/MSVC and WSL Linux/GNU both failed the
uniform free-fall velocity check. The first driver checked that physical result
before writing its returned candidate. An explicitly labelled Windows diagnostic
reproduction corrected only that observation order and repeated the same
failure: maximum velocity error `2.52505e-10 m/s` against `5e-11 m/s`. Its returned
candidate passes 175 exact finite identities, which does not make its physical
accuracy pass. A separate 90-digit ideal-gas calculation from that candidate's
actual profiles/volumes found work-pressure errors up to `5.29809e-9 Pa`.

After the analytic gas inverse, both R3 free-fall cases passed the unchanged
bound. The newly reached supported liquid/gas column then failed: Windows on a
parity row product, Linux on the solved physical row. Both preserve complete
input/previous-output bytes and owned witnesses containing 52/51 numeric slots.
The mixed-boundary case and invalid-gravity controls were not reached in R3.

The physical-node R4 solver passed all four gravity groups on both platforms.
Exact review checks 540 expression identities, 81 stored updates, three corrupted
normal low-bit snapshots and 21 witness numbers per platform. The subsequent
explicit zero-gravity regression passed its first two cases, then rejected the
original `h=2^-56 s` case on both platforms. A `3.78866e-29 J` energy increment
would disappear below the existing U-low-component spacing, `2.64698e-23 J`.
That erasure predicate correctly stopped publication. An independent exact solve
of the actual mass matrix and primary momentum identifies a one-ULP error in
node 9's initial velocity cache: its nearest solution is `0.0625 m/s`.
The complete-RHS correction passes above address this arithmetic error; they do
not relax the erasure check. These original negative captures remain retained.

Private evidence is under `local/occupied-staggered-validation/`:
`canonical-gravity-{windows,linux}-r1`,
`canonical-gravity-windows-r2-diagnostic`,
`canonical-gravity-{windows,linux}-r3`,
`canonical-gravity-{windows,linux}-r4`, and
`retained-pressure-physical-basis-{windows,linux}-r1`.
The first Linux R3 readiness audit incorrectly included linked dependency sources
in its fourteen-source strict canonical whitelist. It stopped before native
invocation; the corrected audit records dependency sources separately. A
zero-gravity plan's copied negative-case labels were corrected in a separately
hashed metadata erratum before native invocation; its driver stayed unchanged.

The current data readers accept v3/v4 prefixes and complete retained suffixes.
They explicitly report that intervening legacy metadata is not semantically
decoded. They cannot establish native execution/retry provenance themselves;
the separate captured invocation/source/executable records supply that evidence.
These snapshots remain diagnostic exports, not restart files.

```text
python native/tests/review_canonical_gravity.py capture.stdout snapshot-directory
```

### Current source validation

With the complete-RHS corrections, the explicit gravity retest passes all four
groups on both strict Windows/MSVC and WSL Linux/GNU builds. The same three
original fixtures cover nine-node uniform free fall, an eight-node supported
water/air column and an eleven-node mixed gas/liquid state with fixed/prescribed
ends. Each platform's exact review checks 540 expression identities, 81 stored
updates, all twelve finite accounting closures, three corrupted normal low-bit
snapshots and 21 invalid-gravity witness numbers.

Free-fall maximum velocity errors are `2.98116e-13 m/s` (Windows) and
`4.69298e-13 m/s` (Linux), below the original `5e-11 m/s` bound. Hydrostatic
speeds are `4.88694e-12` and `2.43740e-12 m/s`. These are not asserted to be
exactly zero: each is checked against an independent inverse-consistent-mass
response bound from measured initial EOS force imbalance, actual nonlinear and
momentum residuals, assembly error and pressure monotonicity work. The resulting
bounds are `3.54267e-10` and `1.72209e-10 m/s`. There is no arbitrary stationary
velocity floor. Initial liquid-pressure deviations, up to `3.39264e-7` and
`1.43861e-7 Pa`, are retained and checked using density-to-pressure conditioning.

The explicitly retried original zero-gravity pressure suite also passes all
four groups on both platforms, including the unchanged `h=2^-56 s` case and
zero-step, alias and unsupported-rounding atomic rejections. Its v4 data reviews
check 566 expression identities (including zero-gravity ledgers), 83 stored
updates, twelve finite accounting closures, three corrupted low-bit snapshots
and 14 witness numbers per platform. The earlier failed regression remains a
negative outcome of its older algorithm.

Evidence is private under `canonical-gravity-{windows,linux}-r5` and
`retained-pressure-physical-basis-{windows,linux}-r2` in the validation directory.
Each source-frozen build has an actual strict compiler audit before invocation;
every native exit is zero, with both raw pipes reaching EOF and closing without
timeout/cap/capture errors. Inputs, complete diagnostic snapshots, executable
hashes and exact reviews are retained under the original 180-s build, 30-s
invocation, 128-KiB pipe and 2-MiB snapshot limits. Execution provenance explicitly
records the failed-gravity and failed-zero-gravity retests. No historical C7,
scalar-translation, remap, vacuum-front or performance gate was replayed or
retroactively accepted. Platform snapshots are not claimed to be bit-identical.

The default game DLL is unchanged. These controls establish bounded standalone
constant-gravity mechanics and arithmetic, not a production grid, remap,
save/reload, game ownership, screenshots, rendering speed or a playable colony.
The first-playable gate still requires its complete Windows/Linux colony and
presentation evidence, including every non-trace gas on tile hover.

## Historical scalar-owner validation

Strict Windows/MSVC and Linux/GNU builds passed. The first GNU build rejected
six loop/following-statement lines as misleading indentation; only line breaks
changed before its next compile-only build. No material case ran on Linux.
The first new seven-case Windows invocation
passed six cases: owner/read-view lifetime and independent reference/row checks,
supplied-U import, five invalid-initialization controls, invalid-step/alias
controls, a moving prescribed boundary, and free gas with zero exterior pressure.
The two advancing cases reached force residual ratios 5.56e-14 and 5.03e-14;
their finite energy-accounting remainders were 1.02e-16 J and 2.74e-14 J. These
are bounded correctness observations, not performance or full-world acceptance.

The seventh case, uniform translation on a nonuniform dyadic mesh, **failed**.
At node 0 the relative requested increment was
`-2.7105054312137611e-20` m (`bbe0000000000000`); adding it to stored relative
position `-1.4375` m produced zero represented change. The typed
`numerical_precision` witness was valid, at geometry stage 10, step 0, with
21 numbers. Complete diagnostic bytes confirmed both input and previous output
unchanged. The retained native exit was 1; raw stdout SHA-256 is
`991cbef942702d747325526cb5a4b878d9ec5b4d3de4052f7facb3b51cfb703b` and
stderr SHA-256 is
`f8290ddc4777966f89246a4b9bda59d55eeb0ffb7e43a55646b87c4e161dad76`.
Both streams reached EOF and closed without timeout, cap or capture errors.
This failed gate is retained without rerun or a relaxed criterion. It motivated
the retained representation above; the new controls do not retroactively accept
this old input. No production 2D/multiphase, Unity, save/load or colony gate
follows from these historical checks.
The updated diagnostic component's 16 groups still pass on both platforms,
with identical normalized output and the separate 642-number decimal review.

## Retained increment arithmetic

`experimental/retained_scalar.hpp` supplies an immutable normalized high/low
stored value and a bounded six-TwoSum update. The result retains increments
smaller than the high component's ULP, reports a two-component represented
increment and returns its signed third-component rounding remainder explicitly.
It rejects a completely erased or reversed nonzero increment, nonfinite or
subnormal arithmetic, overflow and unsupported floating-point controls. The
input remains unchanged on rejection. The remainder is available for accounting;
it is not hidden heat or a claim of arbitrary precision.

The excluded `ma_retained_scalar` and `ma_retained_scalar_tests` targets link
diagnostics only. Five groups pass on Windows/MSVC and Linux/GNU: 124 raw-bit
updates across five exponent scales, cancellation and repeated small increments,
plus seven typed rejections. A separate `Fraction` checker establishes the exact
stored-value/increment/remainder identities for every row, confirms ten nonzero
explicit remainders and the full 64-update chain, and rejects a corrupted stored
low bit. Numerical rows match between platforms. These are arithmetic controls;
no EOS or physical step is invoked by those standalone controls. The component
now also supports the owner's initialization above. Complete pressure updates
use the separately recorded pair sum/change certificates. Production integration
is unfinished, and the failed translation gate remains negative.

```text
python native/tests/review_retained_scalar.py capture.stdout
```

## Retained scalar multiplication

`multiply_retained_scalar` scales the complete high/low stored value by a
binary64 coefficient. Both component products use an explicit FMA to recover
their rounding errors, following Algorithm 3.5 of [Ogita, Rump and Oishi](https://ogilab.w.waseda.jp/ogita/math/doc/2005_OgRuOi.pdf).
Four fixed five-TwoSum compressions produce a normalized stored pair and four
separate signed rounding operands. In the admitted range, the exact identity is
`coefficient * (oldHigh + oldLow) = newHigh + newLow + sum(remainders)`.
The return value exposes all four product terms as well as those remainders.
They are an obligation for the physical caller's ledger, not implicit heat.

Normal product alone cannot prove an error-free FMA transform: its error may
underflow. Nonzero subnormal results reject. When the FMA returns zero, a
bounded integer significand-width/trailing-zero predicate must prove that the
product was exactly representable. The integer calculation only classifies
bits; it does not supply an alternative physical result. A nonzero component
product rounded to zero also rejects. The final stored total retains its sign
and nonzero value. Diagnostics copy known input components, coefficient,
current operands/result, any evaluated FMA addend and up to 44 tape values.
No library operation changes floating-point controls or overwrites an existing
sealed first failure.

The new excluded `ma_retained_scalar_product_tests` target passes five groups
on strict Windows/MSVC and WSL Linux/GNU. A separate exact-rational decoder
checks all 136 captured products: 124 have nonzero high-product errors, 126
have nonzero low-product errors, and 127 expose nonzero rounding remainders.
It verifies both component products, the complete finite accounting identity,
normalization and sign. Ten typed negatives include overflow, component
underflow, hidden errors below and exactly at half the minimum subnormal,
nonfinite coefficients and unsupported rounding. It checks 71 copied witness
numbers and rejects an altered result bit. All 136 numerical rows agree across
platforms; environment diagnostics remain platform-specific.

The fresh build and first invocation captures retain both raw pipe EOFs, source
closures, executable hashes, a 180-second aggregate build limit, 30-second
invocation limit and 128-KiB limit per pipe. Private evidence is under
`local/occupied-staggered-validation/retained-product-windows-r1` and
`retained-product-linux-r1`. There is no EOS, material step, old physical corpus,
game activation, performance or first-playable acceptance in these checks.
Retained positions, owner U/volume and initial momentum now support owner
initialization above. Stored pressure impulses and their complete finite
ledgers are now integrated in the isolated retained operator above. Production
transport/remap and game ownership remain unfinished. The failed canonical
translation gate remains negative and was not rerun.

```text
python native/tests/review_retained_scalar_product.py capture.stdout
```

## Differences of retained values

`subtract_retained_scalar` measures the change between two complete high/low
values. It starts with the left pair, then adds the two negated right components
using fixed-size zero-eliminating Grow-Expansion, based on Theorem 10 of
[Shewchuk's arithmetic paper](https://people.eecs.berkeley.edu/~jrs/papers/robustr.pdf).
The certificate has at most four nonoverlapping components in increasing
magnitude. The highest two are normalized into the stored result; any lower
components are returned as two separate signed rounding operands. Thus the
exact difference equals both the certificate's sum and the stored result plus
its explicit remainders. A nonzero total cannot become zero or change sign.
This is a bounded operation certificate, not an adaptive precision state.

The difference path performs at most six TwoSums and retains up to 12 tape
numbers on failure. It copies both original input pairs and known arithmetic
operands/results. It rejects nonfinite/subnormal arithmetic, overflow and
unsupported controls, and preserves an already sealed first failure. The
library does not change floating-point controls or mutate either input.

The separate excluded `ma_retained_scalar_difference_tests` target passes seven
groups on strict Windows/MSVC and WSL Linux/GNU. Its first invocation records
290 differences: equal-high cancellation, low-component cancellation, widely
separated signed values, a chain of 64 completed small updates, and zero/alias/
identity edges. The native driver directly checks input preservation. The
independent exact-rational decoder verifies all difference and remainder
identities, normalization, sign, nonoverlapping bit ranges and the complete
update chain. There are 24 exact zeros, 106 one-component differences and 160
four-component certificates with explicit remainders. Four typed negatives
cover overflow, subnormal high/low differences and unsupported rounding; 35
copied witness numbers check, and an altered result bit rejects. All 290
numerical rows match across platforms.

Fresh source/build/first-invocation evidence is private under
`local/occupied-staggered-validation/retained-difference-windows-r1` and
`retained-difference-linux-r1`, using the same 180-second build, 30-second native
and 128-KiB raw-pipe bounds as the product controls. Both streams reach EOF and
close without capture errors. No EOS, mechanics, failed physical corpus, game
or performance gate runs. The earlier addition/product implementations are
unchanged. All three operations now support retained owner initialization and
its explicit EOS projections. The isolated pressure operation above also uses
them for completed state changes and complete finite ledgers. Production
integration remains required; the failed translation gate remains negative.

```text
python native/tests/review_retained_scalar_difference.py capture.stdout
```

## Bounded failure evidence

`experimental/canonical_material_diagnostics.hpp` provides a caller-owned
`CanonicalFailureWitness`. A producer prepares one stage, copies its operands,
and seals a typed failure before throwing `CanonicalFailure`. It must check
every capture result. The exception derives directly from `std::exception`,
so it cannot enter a legacy `std::runtime_error` catch which retries a timestep.
The exception stores bounded text; the C++ exception runtime still manages the
exception object's storage.

The witness records a model identity token, completed-step index, stage,
node/owner indices, the producer's operation-local predicate sequence (zero means
unavailable), and resource counts. The future invocation capture must
associate that token with complete model/snapshot bytes; this helper does not
authenticate them. Fixed enum fields hold at most 248 indexed scalars and eight
rows of at most 33 values each: 512 numeric slots in total. It supports matrix
coefficients/operands/products, old/new momentum and velocity, compatibility,
requested/represented impulses, pressure/exterior/reaction components, local
scales/bounds, Neumaier head/correction and owner thermodynamic quantities.
It cannot reconstruct operands missing from an earlier failed run.

Classification uses copied binary64 integer bits. Signed zero, subnormals,
infinities and NaN payloads remain distinguishable even under flush-to-zero or
denormals-are-zero controls. No floating comparison determines their presence.
Capacity or invalid metadata failures seal explicitly with `capture_valid=false`;
they are never reported as successfully truncated evidence. Once sealed,
producer calls cannot change the first failure. Only an explicit caller reset
permits reuse for another operation. Caller arrays and strings are copied.

The JSON encoder writes to a caller buffer of at most 32 KiB and allocates no
storage. It performs no I/O, callbacks, EOS query or mechanical step. It rejects
output overlapping the witness before writing. Small buffers return a separate
encoding failure with zero committed length; discard any partial output. This
does not alter the sealed failure. The format is diagnostic evidence, not a
simulation save/checkpoint protocol.

The schema is `ma-canonical-failure-witness-v1`. Enum IDs match the public header;
missing node/owner/entry indices are `null`. Scalars are `[field,index,number]`,
rows are `[field,index,[number,...]]`, and each number is
`[flags,"16-hex-binary64-bits",finiteDecimalOrNull]`. Flags are presence=1,
finite=2, negative=4, zero=8, subnormal=16. Nonfinite decimals are `null`; the
hexadecimal bits remain authoritative. Reasons/identity tokens accept 1–256
printable ASCII bytes and receive JSON escaping without truncation.
Finite decimal conversion uses a fixed-size integer coefficient and nearest,
ties-even rounding to 17 significant digits, so it does not depend on the
current floating-point controls. This avoids the floating classification used
by some library formatters, including the [GCC implementation](https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/libstdc%2B%2B-v3/src/c%2B%2B17/floating_to_chars.cc).

## Arithmetic environment

The prospective first method supports binary64 on x86-64 with nearest rounding,
FTZ and DAZ disabled, and all SSE exception masks enabled. Preflight reads
`fegetround` and MXCSR, records the observations, and rejects other settings.
It never changes controls or removes a trace. This is a restricted experimental
arithmetic contract, not proof of compatibility with Unity's current settings.
The separate diagnostic controls temporarily change their own standalone test
process and restore it; the library does not do so.

## Explicit targets and scope

`ma_canonical_diagnostics` and `ma_canonical_diagnostics_tests` are excluded from
the default build and are not registered with CTest. Neither target links EOS,
world state, `ma_core`, or the game DLL. Build the test target explicitly from a
fresh CMake build directory and execute it directly. Canonical sources use
warnings as errors and strict arithmetic (`/fp:strict` on MSVC;
`-fno-fast-math -ffp-contract=off -frounding-math` on GNU).

These controls cover evidence ownership, bounds, encoding, exception ancestry
and floating-point preflight. They do not replay the failed physical corpus,
prove force-primary dynamics, advance an atmosphere, measure rendering
performance, or satisfy a first-playable colony gate.

The 16 groups pass with strict MSVC on Windows x64 and GNU 15 on Linux x64.
The additional decimal control covers 125 boundary patterns, including exact
halfway rounding and both signed zeros, under upward rounding with FTZ/DAZ.
Capture the executable's stdout, then run the separate checker:

```text
python native/tests/review_canonical_material_diagnostics.py capture.stdout
```

It launches no native code. It decodes six witnesses, verifies 642 raw numbers,
round-trips 640 finite values and independently checks their 17-digit decimal
rounding using exact decimal arithmetic. The maximal witness occupies 30,097
bytes. Five data-only rejection controls also reject signed-zero loss, a finite
decimal on a nonfinite value, duplicate JSON keys, oversized stdout and incorrect
halfway rounding which would still round-trip to the same binary64 value.
Windows/Linux outputs match after newline normalization.

The first Windows declaration-order compilation failure and the first GNU
subnormal-formatting control failure are retained in the private development
evidence. The corrections change declaration order and decimal implementation;
the original 15 control criteria remain intact. No physical gate was rerun or
promoted by these diagnostics checks.
