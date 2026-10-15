# Bounded complete-file promotion checks

`OwnedReservoirFilePromotion.cs` is an original pure-managed file helper. It
links no Core, native library, game assembly or Unity code. The standalone host
targets .NET 10; the normal helper also compiles for `netstandard2.1` without
the host fault seam. There are no save hooks or changes to native save
permission, ordinary UI, autosaves, cloud behavior or active save-path pointers.

The contract is **bounded file promotion**. Uninterrupted target-name visibility
and recovery of the previous target after a started promotion are open gates.
This helper must not be interpreted as permission to enable integrated saves.

```powershell
& C:/Users/henry/.dotnet/sdk10/dotnet.exe build tools/OwnedReservoirFilePromotionTests/OwnedReservoirFilePromotionTests.csproj -c Release --artifacts-path local/reservoir-file-promotion-build/windows
& C:/Users/henry/.dotnet/sdk10/dotnet.exe local/reservoir-file-promotion-build/windows/bin/OwnedReservoirFilePromotionTests/release/OwnedReservoirFilePromotionTests.dll C:/Users/henry/Desktop/oni_gas_mod/local/reservoir-file-promotion-build/windows-test-files
```

Each run creates a fresh private test directory and writes its `result.json`.
An optional second absolute argument names a separately compiled normal helper
assembly; the host verifies that it excludes the injected fault API and
Core/game/Unity assembly dependencies. Private production compilation and Linux
commands, copied source hashes, binaries and results are recorded under
`local/reservoir-file-promotion-build`. No licensed references are needed.

`OwnedReservoirFilePromotion.Prepare(absoluteTargetPath, completeFileBytes)`
performs pure argument validation, copies every supplied byte into a private
array, computes SHA256 and returns an immutable image. It performs no filesystem
IO or caller callbacks. The caller must not mutate the input during that
synchronous copy; mutations after return cannot change the image. The nonempty
complete-file size limit is 1 GiB; this is an accepted-input bound, not a promise
that the caller has enough memory for the original and copied arrays. Target
paths are fully qualified, at most 4096 characters, with a final filename of at
most 240 characters. Relative/dot/control-character paths, Windows UNC/device
paths and device names, and known Unix `/dev`, `/proc`, `/sys` roots are rejected.

`Promote(image)` validates the existing parent chain and target before staging.
It creates no directories and rejects symlink/reparse/device parents and
directory, redirected, device or readonly targets. It creates one fresh
same-directory `.ma-save-{guid}.stage` with `CreateNew`, `ReadWrite` and
`FileShare.None`, writes the copied bytes, calls `Flush(true)`, then checks the
entire length, every byte, EOF and SHA256 on that exclusive handle. All owned
handles close before promotion or cleanup. Observed target existence must
remain unchanged before the single promotion call: `File.Move(stage,target)`
for an absent target, or `File.Replace(stage,target,null,false)` for an existing
target. There is no copy, truncate, target-delete, metadata-ignore or overwrite
fallback. The promoted target is read and verified again before success.

| Result | Meaning |
| --- | --- |
| `NotPromoted` | The helper never started the promotion call. It did not modify the original target. It attempts to remove only its successfully `CreateNew`-owned stage; cleanup failure is reported separately. |
| `PromotedAndVerified` | The promotion call returned and the target's complete bytes/hash were verified. This does not attest crash durability or continuous pathname availability. |
| `PromotionOutcomeUnknown` | A promotion started and an exception occurred, including during final verification. The target may be old, new, missing or otherwise unresolved. Neither target nor remaining stage is deleted or rolled back. |

The result includes image identity, whether the target was being replaced,
stage/readback/promotion flags, the original error, cleanup error and created
stage path. `RetainedStagePath` reports an observed remaining stage; its absence
does not prove the OS state safe. The caller must reconcile unknown outcomes
explicitly. It must never automatically retry them merely because the target
now appears to match the image. Reusing an immutable image in another `Promote`
call is a caller-controlled new attempt, not a transaction retry protocol.

Callers must own the local parent/target namespace and exclude other writers
and pathname redirection throughout the call. Parent/reparse checks do not
close OS pathname races, identify every mounted filesystem or defend against a
malicious local filesystem owner. The helper does not parse or attest the save
format, live eligibility, native serialization, publication locks or path
pointer updates. `Flush(true)` does not flush a directory entry, control storage
firmware or demonstrate survival of process termination, system crash or power
loss. No backup of the prior target is made and no old-target recovery is
claimed once promotion starts.

The host uses a closed compile-time-only fault enum: stages after creation,
partial write, flush and readback, immediately before and after promotion,
stage corruption/truncation, final verification failure, cleanup failure and a
`CreateNew` name collision. It verifies absent/existing targets, immutable input
copying, exact successful content, bounded/malformed paths, IO/readonly cases,
own-stage-only cleanup, retained unknown artifacts and real OS failure cases.
Platform-specific sharing/permission cases are explicitly skipped on the
other OS. Every successful read in the bounded concurrent-reader exercise
must equal one complete old/new variant; missing-name and open-error counts
are recorded separately. That exercise is not the failed uninterrupted-name
gate being relaxed into a pass.

The original stricter Windows NTFS test failed when a reader saw
`FileNotFoundException` during `File.Replace`; its Atomic-name report/log are
retained under `local/reservoir-atomic-file-build`. A later bounded private
probe completed 36/36 replacements with 124 missing-name opens, 138 sharing
errors and no mixed successful reads. A .NET 10-only `File.Move(...,true)`
alternative verified only 2/5 attempts before access errors, and that overload
does not compile against `netstandard2.1`. It is not a production replacement.
The old diagnostic artifacts retain their original names and source snapshots.

These observations accord with the documented failure states of
[ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew),
which can leave changed/missing pathnames after an error; its write-through
flag is unsupported. The normal Windows runtime calls that primitive through
[FileSystem.ReplaceFile](https://github.com/dotnet/runtime/blob/v10.0.0/src/libraries/System.Private.CoreLib/src/System/IO/FileSystem.Windows.cs).
The [FileStream documentation](https://learn.microsoft.com/en-us/dotnet/core/api/system.io.filestream)
describes the flush API, rather than a complete save durability protocol.
Windows NTFS and isolated WSL Linux filesystem results establish the scoped
managed helper behavior only. Linux Unity and native ONI persistence are not
tested here.
