# Canonical material-state work

The production timestep remains unfinished. The rejected consistent-mass
pressure-step experiment is unchanged. Its failing impulse check did not retain
the node and component operands needed to identify the exact numerical cause.
The prospective replacement retains canonical momentum as primary state and
keeps stored internal energy separate from resolved kinetic energy. No new
state owner, pressure step, remap, runtime mode or physical acceptance is supplied
by the diagnostics component alone. The separate experimental owner below has a
known failed translation gate and is not selected by the game.

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

The fixed-h pressure step uses actual stored initial momentum in its nonlinear
residual, stores free momentum from force, solves the full consistent system for
velocity, then computes dependent constrained momentum and measured reactions.
It checks actual impulse representation, geometry, actual-EOS stored m/U/V,
independent work/entropy comparisons and finite energy/momentum/moment ledgers
before an atomic immutable publication. It performs no remap or timestep retry.
Unsupported topology, range and resource failures preserve the complete input
and previous output and seal a separate bounded witness.

The explicit `ma_canonical_material` and `ma_canonical_material_tests` targets
are excluded from default builds and CTest. The library has one-way dependencies
on `ma_core` and diagnostics; the game DLL and world dispatch do not depend on it.
`canonical_material_diagnostic_bytes` exports tagged, length-delimited data for
complete state comparisons, including model/registry and every report field.
It uses raw binary64 bits and no EOS query. It is not a restart/save format.

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
This failed gate is retained without rerun or a relaxed criterion. A storage
representation change is needed before this method can be considered generally
usable. No production 2D/multiphase, Unity, save/load or colony gate follows.
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
no EOS or physical step is invoked, and this component is not yet integrated
into the canonical owner. Its failed translation gate remains negative.

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
Retained positions, owner U/volume, canonical impulses, EOS projections and
their complete physical ledgers still need integration. The failed canonical
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
unchanged. Integrating all three arithmetic operations into canonical state,
EOS projections and complete physical ledgers remains required; the failed
translation gate remains negative.

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
