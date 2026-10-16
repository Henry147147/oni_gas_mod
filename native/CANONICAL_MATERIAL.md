# Canonical material-state work

The production timestep remains unfinished. The rejected consistent-mass
pressure-step experiment is unchanged. Its failing impulse check did not retain
the node and component operands needed to identify the exact numerical cause.
The prospective replacement retains canonical momentum as primary state and
keeps stored internal energy separate from resolved kinetic energy. No new
state owner, pressure step, remap, runtime mode or physical acceptance is supplied
by the diagnostics component below.

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
