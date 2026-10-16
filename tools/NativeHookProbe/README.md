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
Windows runtime evidence uses ABI 1; ABI 3 still needs a Windows runtime rerun.

The same isolated host also accepts `sealed-gas` and `sealed-water` modes with
an enabled backend. They require an original-flow contrast, verified flow
flags, exact nonuniform mass/temperature compatibility tuples for sixty native
frames after priming, conserved custom physics and continuing native solid heat.
They do not validate Unity presentation or save integration; run the actual
headless colony controls for those checks.
