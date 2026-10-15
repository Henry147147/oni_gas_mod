# Recoverable complete-file promotion (unhooked experiment)

This standalone host project links original file code only. It has no ONI, Unity, Core, coordinator, live eligibility, native-save, cloud, autosave or active-save-pointer dependency. The historical `OwnedReservoirFilePromotion.cs` implementation at `6013e12` is unchanged; the new helper reuses its immutable candidate image and pure validators, never its `Promote` operation. Normal ONI compilation currently excludes the two new experimental product files until separate review/runtime validation.

`OwnedReservoirRecoverableFilePromotion.Prepare(absoluteTarget, ReadOnlySpan<byte>)` performs pure bounded path/encoding checks and copies a nonempty candidate default stream of at most 1 GiB. The caller must not modify the source during this synchronous copy. `Promote(image)` is one caller-controlled attempt, with no callbacks or state locks. A separate invocation is a new attempt; callers must never automatically retry an unknown outcome.

The operation returns exactly one outcome:

| Outcome | Meaning |
| --- | --- |
| `NewFileVerified` | The native rename returned, and the new target and retained prepared intent passed final readback. No target existed at capture. |
| `ReplacementVerified` | The native rename returned, and the new target, byte-exact prior backup and retained intent passed final readback. |
| `NotAttempted` | No native rename mutation entry point ran. Created artifacts are retained; a caller-owned namespace violation may independently have changed the target. |
| `OutcomeUnknown` | A native rename entry point ran and then an error occurred, including an OS error or any final verification/handle-close failure. Even a matching new target cannot settle this outcome. |

Results include the operation GUID, selected backend, native error if available, explicit previous-target presence, immutable prior image, artifact paths and the exact paths this invocation successfully created. Verification flags are historical completed preparation milestones, not assertions that retained files still exist or remain unmodified. No result grants native-save permission.

The prior-image type is distinct from the nonempty candidate type and supports zero-byte existing files. All arrays are privately copied; `CopyBytes()` returns a copy. The prior image covers the file's complete default byte stream only, excluding metadata, ACLs, alternate/named streams and any external state.

## Protocol and failure retention

The caller provides a controlled local directory, excludes concurrent writers, rename/deletion/redirection and cloud synchronization, and controls any eventual explicit artifact disposal. Path checks and read handles do not implement an OS namespace compare-and-swap or protect against an adversarial directory owner. No multiple-hard-link, mount-reconfiguration or full security boundary is claimed. Reads may update filesystem access timestamps.

Every parent must exist and be a direct directory. UNC/device/known Unix virtual roots, dot components, symlinks/reparse points, device/directory/readonly targets, offline files, malformed UTF16 and unsupported filesystems fail closed. The first backend scope is x64 little-endian Windows on a fixed local NTFS volume or x64 Linux on an ext-family mount. Linux uses checked `statx` file type before opening existing files so FIFOs/devices cannot enter blocking complete-file capture. Unsupported APIs/versions/filesystems fail without fallback. Real source/target files remain subject to caller namespace exclusion between checks and opens.

The helper holds an original read handle denying writes and sharing delete through the operation. It captures the old default stream, writes a fresh same-directory backup (if old exists) and new stage, flushes each with `FileStream.Flush(true)`, and verifies full length, every byte, EOF and SHA256 against immutable images. It then creates/flushes/byte-verifies a prepared intent. Every writer closes before rename. Paths, target existence/old bytes, backup, stage and intent are checked again immediately before the single native attempt. On successful rename the final new target, prior backup and exact intent are verified again.

Fresh artifacts are opened with `CreateNew` and `FileShare.None`:

```text
.ma-recover-{operationGuid:N}.new
.ma-recover-{operationGuid:N}.old       # only when a previous file exists, including empty
.ma-recover-{operationGuid:N}.intent
```

Production code never deletes an artifact, target or backup, retries, rolls back, heals, selects a latest intent or updates a pointer. Artifacts remain after preattempt failures too; partial preparation does not imply a valid recoverable intent. Rename consumes only the `.new` name when it succeeds. A verified prior image can be copied out for a future separately reviewed recovery workflow, but this helper never writes it back over the target.

`Flush(true)` plus readback confirms the tested file IO operations completed. It does not flush the directory namespace, prove physical stable storage, power-loss or crash durability, or attest old-target recovery after hardware failure. Finite concurrent-reader observations are scoped evidence, not a generic filesystem atomicity or arbitrary-reader guarantee.

## Prepared intent v1

The original binary format is bounded to 16 KiB. All integer fields use little-endian encoding. Strings use an `int32` byte length and strict UTF8, at most 240 UTF16 characters / 960 UTF8 bytes, with direct basenames only. The exact operation-derived names are enforced. Hashes are 32 raw SHA256 bytes.

| Field, in order | Encoding |
| --- | --- |
| Magic / version / exact total length | ASCII `MAFSPRM1` (8), int32 `1`, int64 including trailing checksum |
| Operation / backend / reserved flags | `.NET Guid.ToByteArray()` (16); byte `1` Windows or `2` Linux; three zero bytes |
| Target / stage / previous backup basenames | Three strict length-prefixed UTF8 strings; backup is empty when absent |
| New length / hash | int64 1..1 GiB; SHA256 (32) |
| Previous-present / previous length / hash | byte 0 or 1; int64 0..1 GiB; SHA256 (32) |
| Checksum | SHA256 of every preceding byte (32), exact EOF |

Absent previous means empty backup name, zero length and an all-zero hash. A present zero-byte previous file means a GUID-derived backup name and SHA256(empty). Unknown flags/version/backend, noncanonical names, invalid encoding/lengths, checksum errors, trailing bytes and absent/present disagreements are rejected. The checksum detects corruption; it is not an authenticity or game-format attestation.

`Inspect(absoluteTarget, absoluteIntent)` reads only the explicitly selected same-directory intent. It requires exact ordinal target basename and operation filename agreement. It reports target `Missing`, `MatchesNew`, `MatchesPrevious`, `MatchesBoth`, `Other` or `Unreadable` using length/hash, and separate stage/backup observations. It exposes an immutable prior image only after the backup matches the intent and is captured again successfully. Recognized other-platform backend values remain data; inspection never executes a rename. It never changes a previous result or reports committed: the record means **prepared**, including when the target matches new or both images. `CommitAttested` and `NativeSavePermitted` are always false.

## Native backend and primary contracts

Windows uses a DELETE-only `CreateFileW` stage handle, `SetFileInformationByHandle` information class **22** (`FileRenameInfoEx`), absolute same-directory target and replacement flags `REPLACE_IF_EXISTS | POSIX_SEMANTICS` (3); absent-target flags are 0. The x64 managed layout checks root offset 8, length offset 16, UTF16 name offset 20 and header size 24. The allocated filename contains an explicit UTF16 NUL; recorded byte length excludes it. Existing share-delete readers can keep the old stream; deny-delete readers can fail with error 32. No read-only/permission override or unverified fallback is used. Nine separate normal-source checks now pass inside actual ONI Mono 6.13 with bounded private paths; see [the runtime tool and limits](../OwnedReservoirGameMonoValidation/README.md). This is not integrated native-save evidence.

Linux uses `libc.renameat2`, flags 0 for replacement and `RENAME_NOREPLACE` (1) for absent-target creation. Entry-point absence is `NotAttempted`; once the syscall entry point runs, any errno is `OutcomeUnknown`. It does not retry with `rename`, copy or delete. `statx` uses the fixed 256-byte UAPI layout, mode offset 28, `STATX_TYPE`, `AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT`; unavailable or unreported type fails closed.

Primary references: [Microsoft FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info), [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle), [Windows FileRenameInformationEx flags](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/4217551b-d2c0-42cb-9dc1-69a716cf6d0c), [NT file rename semantics](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information), [Linux rename(2)](https://man7.org/linux/man-pages/man2/rename.2.html), [Linux statx(2)](https://man7.org/linux/man-pages/man2/statx.2.html). The inspected local Windows SDK is 10.0.26100.0 (`um/WinBase.h`, `um/minwinbase.h`).

## Standalone tests

```powershell
dotnet build tools/OwnedReservoirRecoverableFileTests -c Release --artifacts-path local/recoverable-host
dotnet local/recoverable-host/bin/OwnedReservoirRecoverableFileTests/release/OwnedReservoirRecoverableFileTests.dll ABSOLUTE_PRIVATE_TEST_PARENT OPTIONAL_NORMAL_PRODUCTION_DLL
```

Use a direct NTFS directory on Windows or ext-family directory on Linux. Every invocation creates a fresh GUID directory and retains its own fixtures and JSON report. The host-only `MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS` symbol compiles a closed fault enum and invalid-native-request seam; neither exists in a normal netstandard2.1 assembly. Normal assembly IL inspection rejects calls to `File.Delete` from new production types or to the historical promoter and confirms no Core/game reference.

Tests cover absent/present/zero-byte/equal images; immutable input/output; Unicode/managed native ABI; all preparation stages and native/final faults; strict artifact rechecks and retained files; readonly/locked/link/FIFO/unsupported inputs; native no-replace collision; held old readers, deny-delete behavior and 32 bounded concurrent replacements; strict hostile checksummed intents; selected-intent identity and read-only inspection with unknown outcomes. These are correctness probes, not performance measurements or Linux Unity/rendering proof. Historical failed File.Replace/Atomic-name probes remain excluded from this new backend's positive evidence.
