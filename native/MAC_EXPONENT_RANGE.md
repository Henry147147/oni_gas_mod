# Bounded large integer exponents

Six new groups and all prerequisites pass on Windows and Linux. This work addresses the retained
[Sod N128 scaled-share rejection](MAC_CANONICAL_RANGE.md) requesting exponent
-3049816383. A normalized significand can remain representable while its
separate exponent exceeds a fixed machine word during tiny repeated transfers.

`MacExponent` stores signed 64-bit values inline and larger signed magnitudes
in immutable reference-counted 32-bit limbs. Copies share read-only data;
arithmetic constructs a new result and returns to inline storage when possible.
Significands remain binary64. Normalization, kinetic/variance/share exponents,
separated addition, explicit projections and actual-operand accounts now use
exact exponent addition/subtraction/comparison. A large exponent gap is compared
to 54 before any binary shift; its numerical magnitude is never an allocation
size. No numerical floor or constituent cutoff is introduced.

The declared resource caps are 2,048 magnitude words (65,536 bits) and 20,000
decimal input characters excluding sign. Canonical decimal parsing rejects
leading zeros, negative zero and invalid characters. Integer narrowing must be
exact; capacity/narrowing failures are typed numerical-precision errors and
leave their inputs unchanged. This is bounded variable-precision integer
range, not variable-precision floating significands or EOS computation.

Six groups are declared before the first native build/run:

1. Curated signed32/signed64 boundaries plus deterministic larger integers;
   decimal roundtrips, exact addition/subtraction/doubling and comparisons.
2. Copy/move/normalization and immutable shared ownership exercised concurrently
   by three private test threads, including cancellation back to inline values.
3. The formerly rejected exponent, huge kinetic/variance exponents and share
   normalization whose huge exponents cancel back to ordinary finite range.
4. Error-free additive expansions, separated tiny cancellation and passing/
   failing conservation accounts at the unchanged 128-epsilon operand bound.
5. 256 successive scaled products with independently checked significand
   rounding and exponents that grow beyond fixed machine-word range.
6. Canonical decimal/narrowing/capacity controls, including a maximum-size
   exponent and atomic rejection of the next doubling.

A separate saved-data reviewer uses Python integers and bounded symbolic
significand/exponent clusters. It checks complete additive components, signs,
relative estimates and exact conservation comparisons without constructing
`2**huge_exponent`. Three altered integer/cascade/tiny-component records must
reject. The original range suite's former int32-overflow negative control now
tests strict int32 narrowing; representation beyond int32 is required to work.
All scientific Riemann/gravity inputs, grids, timesteps and physical bounds
remain unchanged.

First strict Windows/MSVC and Linux/GCC15 builds/results are retained.
Predeclared bounds: aggregate build 180 s; new native/review 60 s each; canonical,
momentum, primary and derived-energy native/review 60 s; original range/primitive
30 s; reconstruction 180 s; closed/gravity 60 s; every raw pipe 512 KiB. A
separate Linux ASan/UBSan build/native check will use 180/60 s and the same source,
six controls and pipe bounds. Only after all prerequisites and reviews pass may
Linux repeat strong flow once at the original 180-s/2-MiB bounds. No Windows
failed collision-gate replay follows from an exponent repair.

## Results and remaining collision accuracy

The first Windows build passes. The first Linux build rejects five existing
range-loop copies under `-Werror=range-loop-construct`: two wall-reaction loops
and three fixture export loops now copy a nontrivial exponent object. Changing
those bindings, and the same pattern in three range-test loops, to const
references preserves every arithmetic operation. No native test is invoked
from the failed-build cohort. Fresh strict builds then pass on both platforms.

Each platform passes all 55 native groups and separate saved-data reviews:

| Control | Native groups | Saved checks |
| --- | --- | --- |
| Bounded integer exponents | 6 | 8,560 |
| Full canonical/flux range | 5 | 7,040 |
| Momentum contributions | 4 | 4,565 |
| Primary parcels | 5 | 9,264 |
| Derived-energy integration | 4 | 4,530 |
| Original range arithmetic | 7 | 21,019 |
| Original transport | 7 | 3,888 |
| Material reconstruction | 5 | 13,325 |
| Closed pressure/transport | 8 | 6,538 |
| Ordinary smooth gravity | 4 | 16,958 |

Every full reviewer rejects three corruptions. The new exponent outputs are
identical after line-ending normalization across Windows, Linux and the Linux
ASan/UBSan build. The sanitizer run repeats the six new groups and their 8,560
saved checks with leak detection and halt-on-error enabled; stderr is empty.
These checks do not establish thread-sanitizer or production performance results.

The one Linux strong-flow retry completes Sod N64/128/256 with no rejected
attempts. All successive density/pressure/velocity error ratios satisfy the
original 0.85 bound, and all fine-grid errors satisfy 0.06:

| Grid | Density L1 | Pressure L1 | Velocity L1 |
| --- | --- | --- | --- |
| 64 | 0.0125756323775 | 0.00981171744507 | 0.0170505310651 |
| 128 | 0.00778244337976 | 0.00589579101999 | 0.00996377083148 |
| 256 | 0.00419136756470 | 0.00322154541957 | 0.00532509993547 |

It then fails collision velocity refinement: N64 error 0.043892229516 becomes
0.048944416778 at N128, ratio **1.1151043663**, above 0.85. No collision N256 or
separation case runs. The partial saved-data reviewer checks 59,205 identities
across the five completed profiles, including original EOS, entropy, mass,
momentum and U+face-K accounts. It reproduces the failed norm; no full strong-flow
review or corruption controls follow from this partial review. Windows' earlier
collision failure is retained without another native attempt.

A separate data-only localization retains the original point-velocity metric.
At N64, 98.82% of velocity error lies within four cell widths of an exact shock;
at N128 that fraction is 79.12%, with oscillations behind the shocks. It changes
no scientific criterion and supplies no additional native acceptance. A genuine
accuracy repair is required before either failed strong-flow gate is repeated.

Private results are `local/occupied-staggered-validation/mac-exponent-{windows,linux}-r1`
and sibling `-r2` folders. Completed scoped-review SHA256 values are
`6e98fabe18fbbd7b14e1fc03fda3d6c65fa6c73758a9ab5b6b4aa16857164743`
(Windows) and
`865d43aa8c3352a11beb35a38150774af93c1ef9ae756332fcdce65c4eaaa4dc`
(Linux). They bind frozen sources, strict compiler flags, binaries, first-build
failures, all prerequisite reviews, sanitizer checks and the Linux partial
strong-flow failure. No independent agent, Unity launch, production-performance,
game or shipping acceptance is claimed. The default game DLL is unchanged.
