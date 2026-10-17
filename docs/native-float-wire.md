# Native binary32 parcel inputs

ONI's caller can enable denormals-are-zero (DAZ) and flush-to-zero (FTZ).
Promoting a positive binary32 subnormal with a normal floating-point cast can
then produce zero. Projecting an exactly representable binary64 receipt back
to binary32 can lose the same parcel. Native solver arithmetic already has its
own [floating-point scope](native-floating-point-environment.md); managed packet
decoding must preserve the wire value before that native scope begins.

`NativeBinary32` constructs IEEE-754 representations with integer operations.
Native additive cell edits and point/radius transfers decode directly into
binary64, where every finite binary32 value is representable. Callback mass
projection requires exact binary32 representability and rejects rounding,
overflow, underflow and nonfinite values. The helper preserves signed zero and
does not change the caller's floating-point controls. Native startup also
checks the smallest positive binary32 round trip in the actual managed runtime.

The declaration suite samples every finite binary32 exponent class, both signs
and 1024 significands per class against ordinary CLR conversion in its normal
environment. It also tests malformed exact projections and actual transfer
packet layouts. The test-only caller fixture enables DAZ/FTZ,
demonstrates ordinary casts losing the smallest parcel, then checks exact wire
decoding and callback projection before restoring the complete original MXCSR.

The startup probe reads its smallest-float input bits from memory through
`Volatile.Read`. A fresh Windows host test exposed a false rejection when a cold
JIT compiled `Int32BitsToSingle(1)` with DAZ enabled: its generated code supplied
zero before entering the integer conversion. Dynamic inputs still retained the
exact trace. The same cold-compilation failure reproduces on Linux. Reading the
probe bits at runtime repairs that self-check while retaining both exact-bit
assertions, the conversion algorithms and the caller's floating-point controls.

The exact repaired candidate passes all 172 snapshot, persistence and message
boundary scenarios against the installed Windows ONI declarations, with the
MSVC-built DAZ/FTZ fixture enabled; the adapter also compiles. Fresh and optimized
Windows JIT probes pass, as does the Linux runtime probe using the same captured
managed assembly and a Linux caller fixture. The Linux probe loads no game
declarations and runs no Unity player. Actual smallest-parcel game acceptance
remains separate below.

The original rejected Windows checkpoint remains retained at
`local/windows-solid-particle-r1`, review SHA-256
`28360588d78ef5147bdcaa0fa652adfc13bfed1b9c228ea91d8b09b73b891e8e`.
The repaired declaration/build review is `local/windows-binary32-startup-r1`,
SHA-256 `4a1c82877bbf28d38bfdbd429806bbd7a5348cafe574bfc9d3c779a69fb4f526`.
Its adoption record binds the tracked helper to those exact tested bytes.

A further merged-source preflight compiles the actual `CarbonParticleMass` and
`SavedParticleMass` getters with DAZ enabled. Both originally supplied zero on
their tiny-parcel branch in the Windows .NET runtime. This probe prepares their
code without invoking Unity, the getters or a game factory. `NativeBinary32.FromBits`
now supplies parcel inputs through a non-inlined runtime call; exact callback
projection and in-flight image decoding use that same bit-preserving boundary.

The regression forces a fresh optimized caller to compile after DAZ/FTZ is
enabled, then checks exact particle-field serialization and restoration under
those controls. Removing only `FromBits`' `NoInlining` attribute in a private
copy makes that assertion fail after the ordinary conversion tests pass.
The fixed suite passes all 172 scenarios on Windows and Linux runtimes, using
the installed Windows game declarations on both. Actual compiled getters call
the helper on both runtimes, and direct helper calls retain positive and
negative trace bits. The adapter builds with its two retained reference warnings;
merged Python discovery passes 146 tests with three skips. This adds no Linux
game-declaration or Unity result. The unchanged native source retains its
completed regression without a repeat.

The actual-getter rejection is retained in `local/windows-particle-constants-r1`,
review SHA-256 `7c9ba34642ea7b748b7d788c8c32daed42c0ad900ee9ed91cff7589f08021cb2`.
The fixed cross-runtime and negative-control review is
`local/windows-particle-constants-r2`, SHA-256
`32a1dbedf77b5b9a99646ac6950f809c7de24ff5c6d365427f301addef23ba5c`.

For example, on a Linux x64 test host:

```sh
cc -O2 -shared -fPIC tools/OwnedSnapshotTests/native_binary32_fenv.c \
  -o /tmp/oni-binary32-fenv.so
MA_NATIVE_BINARY32_FENV_FIXTURE=/tmp/oni-binary32-fenv.so \
  dotnet run --project tools/OwnedSnapshotTests \
  -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
```

The C fixture is never linked into or loaded by ONI. Without its explicit test
environment variable the suite reports the caller-control test as skipped.

`--subnormal-carbon-particle` on either Python player runner, or
`-SubnormalCarbonParticle` on the Windows runner, selects an actual game control
with `native-carbon-particle-domain` / `-NativeDomain carbon-particle`. It sends
the smallest positive binary32 CO2 parcel, approximately 1.401298464e-45 kg,
through the original breath factory, movement, deposition and retirement. The
reviewer requires the exact input bits, one matching completed native receipt,
and positive transported CO2 with a relative 128-epsilon mass account. The
ordinary particle bounds also remain in force.

Frozen `c8d6e2d` now passes the actual trace source/save control on both Linux
and WindowsPlayer under official Proton. Both sources then pass local and
cross-player reloads, twice per load process: eight sessions and 960 complete,
individually exact input-replayed frames. Every final CO2 inventory remains
positive and within its original relative mass bound. Source files remain
unchanged, and every session passes the ordinary frame-zero autosave. The
saved-only aggregate review SHA-256 is
`9f63b68d727947f7e046118312095823da63c4742e900f1fc95a72750b2969e8`.
This verifies WindowsPlayer on Linux, not an actual Windows OS.

Combining `--subnormal-carbon-particle` with `--verify-transient-particles`
selects the next control: save the smallest parcel while still in flight,
restore its exact binary32 record, then require one deposition and positive
transported inventory. It uses a gas source with `--save-colony`, or its load
with `--resave-before-resume`. A live breather is excluded from this particular
control so other CO2 production cannot obscure its tiny inventory. Actual
execution of that combined control is pending.
