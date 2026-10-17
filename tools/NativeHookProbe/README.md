# Isolated native hook forwarding controls

This host compares the original licensed SimDLL with the exact same simulation
under the optional forwarding observer. It never attaches to a Unity or Steam
process and does not implement fluid takeover. Outputs retain that distinction.

Build with `--oni-native-observer` and build `tools/NativeShadowProbe`. Run
`python3 tools/NativeHookProbe/run.py --help` for paths. The default six cases
cover flow/save, two active regions, native melting, filtered transitions,
the historical load-shadow control and registered fluid actors. Each unhooked and observed process is bounded
to 45 seconds. Use a fresh evidence directory; failed results are preserved.

The runner requires identical physical/event/save reports, balanced hook
entry/exit counts, a complete bounded control trace with valid per-thread nesting,
the requested native physical updates, explicit live-detach rejection and tested batch
branches. A two-region message must still produce twelve timesteps. The native
host checks heat evolution in both regions. First ordinary queries normalize
empty native backwall identities, so batch equivalence uses a warmed ordinary
query baseline rather than the distinct Start publication.

The observer's C ABI also checks exact native/managed file hashes, loaded entry
fingerprints and quiescence before changing code. Failed installation attempts
verify restoration and retain trampolines if recovery is ambiguous. Do not
unload its module after a failed restoration; exit the diagnostic process.

See [the core contract](../../docs/native-backend-integration.md) for Unity
packaging, actual evidence, platform limits and remaining implementation gates.

Actual Windows x64 MSVC runs pass all six comparison cases on ONI 744825;
Linux ABI 3 passes all six too. The added fluid-actor case
uses thirteen native timesteps: one for registration, eight active and four
after removal. It checks interval resets, mass ledgers and exact registration
delivery. Emission status slots are distinct from consumed-mass events. Linux's
ABI 2 physical/actor reports matched over thirteen frames, but its full gate
failed because the emitter hook recorded zero calls. ABI 3 profiles the separate
GCC secondary-base dispatch, which executes thirteen times; the complete gate
now passes. The canonical entry remains unexercised on Linux and is reported
that way. Separate real Unity colony tests
also pass forwarding and snapshot
checks on both platforms. Windows V60 waits for ten actual native timestep
returns and physical updates; managed acknowledgments alone do not advance
that gate. It retains the failed V59 timing control. Neither runner accepts
these observer results as fluid takeover or performance validation. The retained
The retained Windows OS evidence uses ABI 1; current ABI 3 still needs an
actual Windows Unity rerun.

ABI 3 also passes all six Windows-binary comparisons under Wine 10 on Linux.
The optional runner arguments `--wine /path/to/wine --wine-prefix /path/to/prefix
--windows-dotnet /path/to/dotnet.exe` select that execution path; supply the
Windows game and observer library. Use an initialized disposable prefix with
the host filesystem mapped at `Z:` and a Windows x64 .NET 10 runtime. Paths are
passed as separate arguments and mapped without a shell. Summaries explicitly
mark Wine, the host/native platforms, `actualWindowsOS: false` and
`unityGameTest: false`. These results verify the actual Windows DLL and hooks;
they do not replace a current Windows Unity run.

MinGW builds now produce the package's expected DLL names and link their
compiler/thread runtimes statically. A clean Windows package passes normal
native bootstrap and all 89 managed/native Core tests under Wine, followed by
the three domain controls. The package contains no compiler runtime DLLs.
Use `package_probe.py --managed-dir /path/to/managed/output` when keeping
Windows and Linux adapter builds in separate output directories.

The same isolated host also accepts `sealed-gas` and `sealed-water` modes with
an enabled backend. They require an original-flow contrast, verified flow
flags, exact nonuniform mass/temperature compatibility tuples for sixty native
frames after priming, conserved custom physics and continuing native solid heat.
They do not validate Unity presentation or save integration; run the actual
headless colony controls for those checks.

Observer ABI 4 adds native body contacts. Both platform binaries pass the six
ordinary forwarding comparisons, plus `--mode baseline --native-body-heat`.
The latter compares twelve original native body temperature publications and
raw deltaKJ bits, with the private contact hook actually exercised. The
unmodified Windows library's first deltaKJ publication is NaN; that exact
original bit pattern is retained in both captures, and later publications
must be finite. The owned body-heat controls require finite original object
temperature and deltaKJ on every accepted contact.

Use `MA_VERIFY_CHUNK_HEAT=1` with a plain sealed gas/water host case to check
represented body heat, full fluid U+K and exact accepted-input replay.
`MA_NATIVE_DOMAIN_SECOND_REGION=1` places that room in the second of two
active regions. `MA_VERIFY_FRAME_INPUTS=1 MA_VERIFY_COMPLETED_SNAPSHOT=1`
instead reserves a future zero-heat command and proves that a completed image
stays identical while save snapshots refuse; stale epochs and short buffers
must also reject without changing output.
