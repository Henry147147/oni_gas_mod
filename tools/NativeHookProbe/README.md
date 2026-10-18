# Isolated native hook forwarding comparison

`run.py` checks that the optional forwarding observer leaves ONI's native
simulation unchanged. It runs each
[`NativeShadowProbe`](../NativeShadowProbe/README.md) case twice: once against
the original licensed SimDLL and once with the observer installed. It never
attaches to Unity or Steam and never takes over fluids, and summaries keep
`authoritativeFluidOwner: false`.

```sh
python3 tools/build.py --oni-native-observer       # builds libmixed_atmospheres_oni.so
dotnet build tools/NativeShadowProbe -c Release
python3 tools/NativeHookProbe/run.py --game /path/to/OxygenNotIncluded \
  --observer build/libmixed_atmospheres_oni.so --contract /private/game-contract-local \
  --output /private/new-evidence-dir [--mode <mode>] [--native-body-heat]
```

The game must be build 744825, and the contract export is described in
[the environment notes](../ONI_ENVIRONMENT.md#main-menu-probe-and-contract-export).
SimDLL is taken from the game's `OxygenNotIncluded_Data/Plugins`. The `--output`
directory must not exist. Each process has 45 seconds, and failed reports and
logs are kept. The six cases are `baseline`, `multi-region`, `transition`,
`filtered-transition`, `load-shadow` and `fluid-actors`. `--native-body-heat`
(with `--mode baseline`) adds a native body and compares twelve temperature
publications and raw `deltaKJ` bits. The unmodified Windows library's first
`deltaKJ` is NaN: both runs must keep that exact bit pattern, and later values
must be finite.

For the Windows DLLs under Wine, add `--wine`, `--wine-prefix` and
`--windows-dotnet`. The prefix maps the host at `Z:` and holds Windows x64
.NET 10. Summaries record `actualWindowsOS: false` and `unityGameTest: false`.

## Pass criteria

Both runs must pass, and their reports must be identical apart from volatile
fields. That covers physics, events and the save. The observed run must also
show:

- forwarding-only status, no recovery, no active calls at shutdown, a completed
  detach and a rejected live detach;
- equal entry and exit counts on all 29 hooks;
- 12 `update-data` calls, or 13 for `fluid-actors`;
- `initialize` and `pressure` hooks exercised, and at least two message
  batches;
- for actors: 13 consumer calls and 13 emitter calls (canonical plus dispatch
  entry), with registration, modification and removal passing;
- a complete, ordered trace with valid per-thread nesting.

Native `Step` entries and `PrepareGameData` calls are not elapsed-time counts.

The observer's C ABI checks file hashes, entry fingerprints and quiescence
before patching code. A failed install verifies restoration and keeps the
trampolines if recovery is unclear. Never unload the module after a failed
restoration; exit the process instead. Packaging and ABI layouts are in
[native backend integration](../../docs/native-backend-integration.md).

## Status

With observer ABI 4, the Linux library and the Windows binaries under Wine 10
pass all six comparisons and `--native-body-heat`. On Linux, emitter calls go
through GCC's secondary-base dispatch (13 in the actor case), and the canonical
entry is reported as unexercised. Native Windows MSVC runs passed the six
comparisons with an earlier ABI.

None of this validates fluid takeover or performance. The forwarding-only colony
mode waits for ten real native timestep returns and does not count managed
acknowledgements. Windows last ran it with ABI 1. Current hooks run in actual
Windows and Linux colonies through the native-domain controls; see
[Windows validation](../../docs/windows-validation.md#windows-game-checks-2026-10-17)
and [ONI_HEADLESS.md](../ONI_HEADLESS.md).
