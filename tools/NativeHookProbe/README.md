# Isolated native hook forwarding controls

This host compares the original licensed SimDLL with the exact same simulation
under the optional forwarding observer. It never attaches to a Unity or Steam
process and does not implement fluid takeover. Outputs retain that distinction.

Build with `--oni-native-observer` and build `tools/NativeShadowProbe`. Run
`python3 tools/NativeHookProbe/run.py --help` for paths. The default five cases
cover flow/save, two active regions, native melting, filtered transitions and
the historical load-shadow control. Each unhooked and observed process is bounded
to 45 seconds. Use a fresh evidence directory; failed results are preserved.

The runner requires identical physical/event/save reports, balanced hook
entry/exit counts, a complete bounded control trace with valid per-thread nesting,
twelve native physical updates, explicit live-detach rejection and tested batch
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

Actual Windows x64 MSVC and Linux runs pass all five comparison cases on ONI
744825. Separate real Unity colony tests also pass forwarding and snapshot
checks on both platforms. Windows V60 waits for ten actual native timestep
returns and physical updates; managed acknowledgments alone do not advance
that gate. It retains the failed V59 timing control. Neither runner accepts
these observer results as fluid takeover or performance validation.
