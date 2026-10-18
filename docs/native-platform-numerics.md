# Native platform and numeric boundaries

The native backend runs inside ONI's process, beside code it does not control.
This document covers three boundaries that keep it safe there: where Linux hook
trampolines live, how the solver isolates its floating-point environment from
the game, and how binary32 values from the game are decoded without loss. The
backend itself is described in [native backend integration](native-backend-integration.md).

## Linux trampoline storage

The Linux observer installs every verified SimDLL hook (29 in observer ABI 4)
with trampolines in a bounded buffer inside the loaded module's unused
executable-page padding. This removes the need for a new mapping within the x64
relative-jump range, which Unity can already have filled: before this change a
real game start failed to prepare hook 0 after finding 2,610 regions and no
reachable gap. Windows keeps its own hook allocation and profile.

The pinned Linux image has an RX load segment at RVA `0x2a000` with file and
memory size `0x7a1b9`, so its allocated bytes end at `0xa41b9`. The aligned range
`0xa41c0..0xa5000` holds 3,648 zero bytes outside every load segment's
allocated contents, inside the final privately mapped executable page. That is
room for 50 entries in the pinned Funchook x64 layout. Startup requires capacity
for every profiled hook and checks that each prepared hook used this buffer.

`tools/oni_hook_profiles.py` derives this metadata from the licensed ELF program
headers and padding; the checked-in profile holds only boundaries and hashes.
Before registering the buffer the observer checks the complete native and
managed file hashes, loaded entry fingerprints, the actual loaded program
headers, the padding hash, the system page size and the actual private RX
mapping. Writable segments, BSS, live instructions, overlapping load segments,
nonzero padding and other page layouts reject activation. A new game build
needs a newly inspected profile.

The hash-pinned Funchook extension limits each borrowed page to its buffer.
While a buffer is registered, exhaustion or an unreachable target rejects
preparation; it never silently allocates another page. Writes temporarily make
the page read/write/execute, keeping execute access for native code that shares
it; installed trampolines are read/execute. Destruction restores every padding
byte to zero and the RX permission before releasing the registration. It never
unmaps a game-image page or changes the installed file. A failed restoration
keeps ownership and reports an error, and the process must restart.

For unregistered Funchook users the gap scanner still claims pages with exact
`MAP_FIXED_NOREPLACE` and rejects ignored claims, so it never overwrites
occupied Unity mappings. Both changes ship as source patches with the original
dependency archive and its linking exception.

**Verification.** The detour regression first denies every nearby allocation
and requires ordinary preparation to fail without changing native code. With
borrowed storage it executes and restores short-branch trampolines for 96
complete cycles with no nearby claims or image unmaps. A one-entry buffer
rejects a second target; invalid buffers, active rebinding and a failed final
`mprotect` exercise bounds and cleanup errors; page bytes and RX permissions are
checked after cleanup; loaded-segment and ELF-profile controls reject malformed
and overlapping segments. Linux Unity runs install every hook from this buffer
(startup refuses otherwise); the first cohort passed 28 source/load cases across
14 scenes.

## Solver arithmetic scope

`ArithmeticScope` establishes the solver's floating-point environment on every
executing thread and restores the caller's afterwards, including on failure
paths. It saves the complete floating-point environment and SSE controls,
selects round-to-nearest and gradual underflow (FTZ/DAZ off), and masks
exceptions for the solver's checked operations. Exit restores the original
rounding, exception flags and SSE controls.

GNU x86 builds also select the extended x87 precision of their `long double`,
which the EOS root uses internally; setting the rounding mode alone leaves x87
precision inherited from the game. The precision change is local to the scope,
so ONI's nonfluid code gets its original control word back. MSVC's
`long double` does not use this x87 selector.

This was found from a real difference: a Windows water save reloaded locally
with exact continuation on each player, but controlled replays reproduced each
player's result only when the caller had selected 53-bit x87 precision. The
dedicated `native_oni_arithmetic_environment` CTest continues a nonuniform water
checkpoint for 16 identical frames under both GNU caller precisions, downward
caller rounding and enabled FTZ/DAZ, and checks every caller control and
exception flag on success and rejection. Actual WindowsPlayer and native Linux
reloads of that save with the corrected libraries match their independently
predicted extended-precision images exactly.

**Open.** The two players' extended-precision replays of that water save still
differ (maximum stored-momentum relative difference about 2.1e-7), so the
strict 1e-8 cross-platform water comparison fails. The scope removes dependence
on the caller's precision; it does not make the platforms' math libraries
agree. The same gate fails for the heated mixture
([mixed-cell conduction](native-domain-physics.md#mixed-cell-conduction)).

## Binary32 wire values

ONI's caller can enable denormals-are-zero (DAZ) and flush-to-zero (FTZ). An
ordinary cast of a positive binary32 subnormal can then produce zero, and
projecting an exactly representable binary64 receipt back to binary32 can lose
the same parcel. Managed packet decoding must therefore preserve the wire value
before the native arithmetic scope begins.

`NativeBinary32` builds IEEE-754 representations with integer operations.
Additive cell edits and point/radius/rectangle transfers decode directly into
binary64, where every finite binary32 value is representable. Callback mass
projection requires exact binary32 representability and rejects rounding,
overflow, underflow and nonfinite values. Signed zero is kept and the caller's
floating-point controls are never changed. Parcel inputs, callback projection
and in-flight particle decoding all go through `NativeBinary32.FromBits`, a
non-inlined runtime call: when it was inlined, a freshly JIT-compiled getter
under DAZ supplied zero before the integer conversion ran. For the same reason
native startup checks the smallest positive binary32 round trip with its input
bits read at runtime through `Volatile.Read`, not as a JIT constant.

The declaration suite samples every finite binary32 exponent class, both signs
and 1,024 significands per class against ordinary CLR conversion, malformed
exact projections and real transfer packets. Its caller fixture enables
DAZ/FTZ, shows ordinary casts losing the smallest parcel, checks exact decoding
and callback projection and particle-field round trips in a freshly compiled
caller, then restores the complete original MXCSR. Removing only the
`NoInlining` attribute makes that assertion fail. On a Linux x64 host:

```sh
cc -O2 -shared -fPIC tools/OwnedSnapshotTests/native_binary32_fenv.c \
  -o /tmp/oni-binary32-fenv.so
MA_NATIVE_BINARY32_FENV_FIXTURE=/tmp/oni-binary32-fenv.so \
  dotnet run --project tools/OwnedSnapshotTests \
  -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
```

The C fixture is never linked into or loaded by ONI; without the environment
variable the suite reports that test as skipped.

**Game controls.** `--subnormal-carbon-particle` (Windows
`-SubnormalCarbonParticle`) with `native-carbon-particle-domain` sends the
smallest positive binary32 CO2 parcel, about 1.401298464e-45 kg, through the
original breath factory, movement, deposition and retirement. The reviewer
requires the exact input bits, one matching completed receipt and positive
transported CO2 within a relative 128-epsilon mass account, plus the ordinary
particle bounds. Combined with `--verify-transient-particles` it saves that
parcel in flight ([in-flight particles](native-domain-persistence.md#in-flight-co2-particles)).

**Status.** Both controls pass on LinuxPlayer and WindowsPlayer under Proton,
for both sources and every local and cross-player two-session reload (eight
sessions and 960 exact frames per control). The declaration suite passes on the Windows
and Linux .NET runtimes. Actual Windows OS game runs remain open.
