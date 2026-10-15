# Private Windows Mono file-protocol validation

This source-only tool compiles the normal frozen file helpers into a separate
netstandard2.1 assembly, without either host fault symbol or Core/Unity/game
dependencies. It is an explicit disposable-fixture tool, excluded from the
normal mod. It grants no native-save permission and changes no ordinary save,
autosave, cloud or active-save-pointer behavior.

Its only public API returns JSON:

```csharp
MixedAtmospheres.Validation.GameMonoFileProtocolValidation.Run(
    string absoluteFreshParent);
```

Pass an existing absolute direct local NTFS output directory controlled by the
caller, with no concurrent writers, path redirection or cloud synchronization.
The tool creates a fresh GUID child and retains all fixtures. Nine checks cover
normal runtime/ABI/no-test-seam, new/replacement/equal/zero-byte/Unicode files,
a held share-delete reader, deny-delete error32 with an unresolved outcome and
verified backup, and strict read-only selected-intent inspection. There is no
timed concurrency, cleanup, automatic retry, rollback or backup restoration.

All default-stream bytes are private test data. File writes use CreateNew,
Flush(true) and byte/hash readback. Results include each outcome, actual runtime
and Mono detection, paths, lengths, hashes, full invocation GUID and case/ID
mapping. Returned JSON is also physically written/read-verified; callers must
archive returned failure text if report writing fails. Reading may change
access timestamps. Intent inspection never settles an unknown promotion or
attests native save eligibility.

The harness uses a lossless GUID encoding of 22 URL-safe characters, a prefix
and one-character case directories. It rejects derived artifact paths above
240 UTF16 characters before fixture IO; a caller parent of 161 characters produces
a maximum of 239. This conservative **harness-only** budget does not change the
production helper's path policy or advertise general long-path support.

Actual Windows v27 used the same normal helper sources but longer fixture
names: Mono 6.13 passed ABI and failed all eight IO cases before rename when
artifact paths were 271–292 characters; original seed/report paths fitted
below 260. Outcomes were NotAttempted with no stage/backup/intent created, and
all seven existing seed streams remained unchanged. This supports a legacy
runtime path-length diagnosis, not a P/Invoke failure or a measured exact OS
cutoff. Future product integration needs a reviewed full-artifact/runtime path
preflight; this tool does not supply a live-save eligibility policy.

The initial tracked validator `.cs` is byte-identical to private revision2:

| Input | SHA256 |
| --- | --- |
| Validator source | `a1adb3131bf519318411b0aa865d06787b97dccd5da4162d75cfd092984fb90d` |
| Tested private R2 DLL | `b3d74e815e1daf1e98f7b528b03db4257748efab3f6dc027bad6dfe71f3beafc` |
| Historical image/helper | `d0c2c87db64e211ec06095f80e87b36718836637bfe7e8a20405dea963e1e076` |
| Recoverable helper | `a0810dd4d9b6511755c4c3f2bf4de1b6ac6d25454c9eaf52658f3a7055b7776a` |
| Rename backend | `bd9811953257d321af64cfd1555cdc9a9c175a4faa802c0fa33faec92c40080f` |

The private R2 normal assembly passed nine checks through both .NET 10 path-load
and byte-load preflights. Actual Windows v28 passed 9/9 on Mono 6.13 at a maximum
of 239. Independent archived review verified all 34 retained artifacts, decoded
the intent records, confirmed unresolved native error 32 and both prior images
and stages where required, and checked clean teardown and 13 restored originals.
The review is recorded in the ignored run's `independent-review/verification.json`.
The relocated tracked project also builds with no warnings; its output has not
been executed by that actual Mono claim. Rebuilding may change its DLL hash;
pin/review that output anew.

For an explicit private build, keep artifacts outside this tool directory:

```powershell
dotnet build tools/OwnedReservoirGameMonoValidation -c Release --artifacts-path local/game-mono-validation-build
```

Pass the resulting DLL to `tools/package_probe.py` with
`--private-file-protocol-validator`. The private package retains the DLL hash.
Run `tools/run_colony_validation.ps1` with `-Presentation -IdleReservoirLease
-PrivateFileProtocol -KeepOpen` and fresh package/evidence directories. The
driver independently checks the configured SHA256 before loading the bytes.

An opt-in driver must independently verify the DLL's SHA256, load those exact
bytes with Assembly.Load, resolve the public string Run(string), and archive
its return. In-memory Assembly.Location is empty, so internal assembly hash
is explicitly unmeasured; the caller binds verified package/input bytes to
the actual runtime report. Require Passed, MonoRuntimeDetected, nine checks
and ReportFileWriteVerified for an actual Mono proof.

No full save/load, generic filesystem atomicity, arbitrary-reader support,
metadata/ACL/named-stream preservation, directory or power-loss/crash durability,
live ownership recovery, native-world takeover or Linux Unity proof follows.
Generated DLLs, fixtures, screenshots and reports belong in ignored local
evidence directories, not this source-only tool.
