using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static string root = "";
    private static readonly List<object> results = new();
    private static int passed, failed, skipped;
    private static object? continuity;
    private static readonly byte[] Old = Bytes(11, 32 * 1024);
    private static readonly byte[] New = Bytes(73, 48 * 1024);
    private static int Main(string[] args)
    {
        var parent = args.Length == 0 ? Path.GetTempPath() : Path.GetFullPath(args[0]);
        Directory.CreateDirectory(parent);
        root = Path.Combine(parent, "recoverable-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
        Console.WriteLine("Recoverable complete-file tests: " + root);
        Run("pure validation and bounded immutable new image", PureValidation);
        Run("absent target creates verified new file and prepared intent", NewFile);
        Run("replacement retains byte-exact immutable previous image", Replacement);
        Run("zero-byte previous file is distinct from absent target", EmptyPrevious);
        Run("equal previous/new images remain observationally ambiguous", SameImages);
        Run("Unicode basename and UTF16 Windows rename ABI", AbiAndUnicode);
        Run("native rename backend rejects an absent stage before mutation", MissingBackendStage);
        Run("native nonreplace rename rejects an occupied name without fallback", NativeNoReplaceCollision);
        Run("absent target failures preserve intent and never roll back", AbsentTargetFaults);
        foreach (var fault in Enum.GetValues<OwnedReservoirRecoverableFault>())
        {
            if (fault == OwnedReservoirRecoverableFault.None || fault == OwnedReservoirRecoverableFault.ChangeTargetBeforeAttempt) continue;
            var captured = fault; Run("closed fault: " + fault, () => Fault(captured));
        }
        Run("external target change is rejected without rename or rollback", ChangedTarget);
        Run("preexisting readonly target is rejected before artifacts", ReadOnlyTarget);
        Run("exclusive target reader fails before capture and preserves original", ExclusiveReader);
        Run("all retained artifact handles close before return", HandlesClosed);
        Run("missing parent and directory target do not create artifacts", BadFilesystemPaths);
        Run("direct parent and target links are rejected", Links);
        if (OperatingSystem.IsWindows()) Run("deny-delete reader yields Unknown with independently verified old image", DenyDeleteReader);
        else Skip("Windows deny-delete reader", "Linux does not implement the Windows share-delete contract.");
        Run("bounded share-delete readers observe complete old/new streams", Readers);
        Run("reconciliation classifies missing/new/old/other without writes", InspectStates);
        Run("read-only inspection never settles a post-rename Unknown", UnknownRemainsUnknown);
        Run("corrupt or missing previous backup yields no recovery image", InspectBackupFailures);
        Run("locked target inspection is unreadable rather than healed", InspectLockedTarget);
        Run("intent identity and cross-parent selection are strict", InspectIdentity);
        Run("hostile checksummed intent grammar is rejected", HostileIntent);
        Run("intent has exact canonical encoding and backend is data", IntentCanonical);
        if (OperatingSystem.IsLinux()) Run("Linux FIFO is rejected without opening or blocking", LinuxFifo);
        else Skip("Linux regular-file statx check", "Windows run does not execute the Linux backend.");
        if (args.Length > 1) Run("normal netstandard assembly has no test faults/deletion/old promoter calls", () => ProductionAssembly(args[1]));
        else Skip("normal netstandard assembly inspection", "Pass the private production DLL as argument two.");
        var evidence = new
        {
            RecordedUtc = DateTime.UtcNow, Passed = failed == 0, ChecksPassed = passed, ChecksFailed = failed, ChecksSkipped = skipped,
            Root = root, OS = RuntimeInformation.OSDescription, Architecture = RuntimeInformation.ProcessArchitecture.ToString(),
            Runtime = RuntimeInformation.FrameworkDescription, Backend = OwnedReservoirLocalRename.Select(root).ToString(),
            Filesystem = DriveInfo.GetDrives().Where(d => root.StartsWith(d.RootDirectory.FullName, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
                .OrderByDescending(d => d.RootDirectory.FullName.Length).Select(d => new { Root = d.RootDirectory.FullName, d.DriveFormat, DriveType = d.DriveType.ToString() }).FirstOrDefault(),
            ContinuityObservation = continuity, Results = results,
            Scope = "Bounded standalone complete-default-stream file helper only; caller excludes concurrent writers, redirection and cloud sync.",
            NativeGameMonoVerified = false, NativeSavePermitted = false, LinuxUnityVerified = false,
            GenericFilesystemAtomicityVerified = false, PowerLossDurabilityVerified = false, MetadataPreservationVerified = false,
            AutomaticRetryOrRollbackImplemented = false
        };
        var path = Path.Combine(root, "result.json");
        File.WriteAllText(path, JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"{passed} passed; {failed} failed; {skipped} skipped."); Console.WriteLine("Evidence: " + path);
        return failed == 0 ? 0 : 1;
    }
    private static void Run(string name, Action test)
    {
        try { test(); passed++; results.Add(new { Name = name, Passed = true }); Console.WriteLine("PASS " + name); }
        catch (Exception error) { failed++; results.Add(new { Name = name, Passed = false, Error = error.ToString() }); Console.WriteLine("FAIL " + name + ": " + error); }
    }
    private static void Skip(string name, string reason) { skipped++; results.Add(new { Name = name, Skipped = true, Reason = reason }); Console.WriteLine("SKIP " + name + ": " + reason); }
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Throws(Action action, string message)
    { try { action(); } catch { return; } throw new Exception("Expected rejection: " + message); }
    private static string DirectoryFor(string name)
    { var path = Path.Combine(root, name); Directory.CreateDirectory(path); return path; }
    private static string Target(string name) => Path.Combine(DirectoryFor(name), "colony.sav");
    private static byte[] Bytes(int seed, int length) { var value = new byte[length]; new Random(seed).NextBytes(value); return value; }
    private static void Equal(byte[] actual, byte[] expected, string message) => Check(actual.AsSpan().SequenceEqual(expected), message);
    private static OwnedReservoirRecoverableResult ReplacementAt(string name, OwnedReservoirRecoverableFault fault = OwnedReservoirRecoverableFault.None)
    {
        var path = Target(name); File.WriteAllBytes(path, Old);
        return OwnedReservoirRecoverableFilePromotion.PromoteForTest(OwnedReservoirRecoverableFilePromotion.Prepare(path, New), fault);
    }
    private static void ValidSuccess(OwnedReservoirRecoverableResult result, bool oldPresent)
    {
        Check(result.Outcome == (oldPresent ? OwnedReservoirRecoverableOutcome.ReplacementVerified : OwnedReservoirRecoverableOutcome.NewFileVerified), result.Error?.ToString() ?? "incorrect success outcome");
        Check(result.NativeRenameAttempted && result.NativeRenameReturned && result.Error == null && result.StageVerified && result.IntentVerified, "missing successful verification flags");
        Check(result.PreviousTargetExisted == oldPresent && result.PreviousImageVerified == oldPresent, "previous-image presence mismatch");
        Check(result.CreatedPaths.Count == (oldPresent ? 3 : 2) && result.CreatedPaths.Contains(result.IntentPath!), "fresh artifact accounting");
        Check(!File.Exists(result.StagePath) && File.Exists(result.IntentPath), "rename did not consume only the stage");
        Equal(File.ReadAllBytes(result.TargetPath), New, "promoted content differs");
    }
    private static void PureValidation()
    {
        var path = Target("pure"); var input = (byte[])New.Clone(); var image = OwnedReservoirRecoverableFilePromotion.Prepare(path, input); input[0] ^= 0xff;
        Check(!File.Exists(path) && Directory.GetFileSystemEntries(Path.GetDirectoryName(path)!).Length == 0, "Prepare performed IO");
        var promoted = OwnedReservoirRecoverableFilePromotion.Promote(image); ValidSuccess(promoted, false);
        Throws(() => OwnedReservoirRecoverableFilePromotion.Prepare(path, ReadOnlySpan<byte>.Empty), "empty new image");
        Throws(() => OwnedReservoirFilePromotion.ValidateCompleteFileLength(1L + OwnedReservoirFilePromotion.MaximumCompleteFileBytes), "oversized new image");
        foreach (var bad in new[] { "", "relative.sav", Path.Combine(root, "..", "outside.sav"), root + Path.DirectorySeparatorChar,
            Path.Combine(root, "bad\n.sav"), Path.Combine(root, new string('n', 241)), Path.Combine(root, "bad\ud800.sav") })
            Throws(() => OwnedReservoirRecoverableFilePromotion.Prepare(bad, New), "malformed path");
        if (OperatingSystem.IsWindows()) foreach (var bad in new[] { @"\\server\share\x.sav", @"\\?\C:\x.sav", Path.Combine(root, "NUL"), Path.Combine(root, "x:sav"), Path.Combine(root, "bad. ") })
            Throws(() => OwnedReservoirRecoverableFilePromotion.Prepare(bad, New), "Windows namespace escape");
        else foreach (var bad in new[] { "/dev/x.sav", "/proc/x.sav", "/sys/x.sav" }) Throws(() => OwnedReservoirRecoverableFilePromotion.Prepare(bad, New), "virtual filesystem root");
        Throws(() => OwnedReservoirRecoverableFilePromotion.Prepare(Path.Combine(root,"colon:name.sav"),New),"protocol-incompatible basename");
        if(OperatingSystem.IsLinux())
        {
            Throws(() => OwnedReservoirRecoverableFilePromotion.Prepare(Path.Combine(root,"slash\\name.sav"),New),"protocol-incompatible basename");
            Throws(()=>OwnedReservoirLocalRename.Select("/proc"),"unsupported filesystem backend");
        }
        Throws(() => OwnedReservoirRecoverableFilePromotion.Promote(null!), "null image");
        Throws(() => OwnedReservoirRecoverableFilePromotion.PromoteForTest(image, (OwnedReservoirRecoverableFault)12345), "unknown host fault");
    }
    private static void NewFile()
    {
        var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(Target("new"), New)); ValidSuccess(result, false);
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Check(inspected.IntentValid && inspected.Target == OwnedReservoirTargetObservation.MatchesNew && inspected.PreviousBackup == OwnedReservoirArtifactObservation.NotApplicable && inspected.PreviousImage == null, "absent previous misreported");
    }
    private static void Replacement()
    {
        var result = ReplacementAt("existing"); ValidSuccess(result, true);
        Equal(File.ReadAllBytes(result.PreviousImagePath!), Old, "prior default stream not retained");
        Equal(result.PreviousImage!.CopyBytes(), Old, "previous image mismatch"); var mutated = result.PreviousImage.CopyBytes(); mutated[0] ^= 0xff;
        Equal(result.PreviousImage.CopyBytes(), Old, "mutable previous image buffer escaped");
        Check(result.PreviousImage.Sha256 == RecoverableFileData.Hash(Old) && result.NewSha256 == RecoverableFileData.Hash(New), "recorded hashes mismatch");
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Equal(inspected.PreviousImage!.CopyBytes(), Old, "restart-independent backup capture failed");
        Throws(() => ((IList<string>)result.CreatedPaths).Add("invented"), "created path list mutable");
    }
    private static void EmptyPrevious()
    {
        var path = Target("zero-old"); File.WriteAllBytes(path, Array.Empty<byte>());
        var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, New)); ValidSuccess(result, true);
        Check(result.PreviousImage!.ByteLength == 0 && result.PreviousImage.Sha256 == RecoverableFileData.Hash(Array.Empty<byte>()) && new FileInfo(result.PreviousImagePath!).Length == 0, "zero-byte prior lost");
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(path, result.IntentPath!);
        Check(inspected.IntentValid && inspected.PreviousTargetExisted == true && inspected.PreviousImage!.CopyBytes().Length == 0, "empty old treated as absence");
    }
    private static void SameImages()
    {
        var path = Target("same"); File.WriteAllBytes(path, New);
        var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, New)); ValidSuccess(result, true);
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(path, result.IntentPath!);
        Check(inspected.Target == OwnedReservoirTargetObservation.MatchesBoth && !inspected.CommitAttested && !inspected.NativeSavePermitted, "same bytes imply commit");
    }
    private static void AbiAndUnicode()
    {
        Check(OwnedReservoirLocalRename.FileRenameInfoEx == 22 && OwnedReservoirLocalRename.WindowsRootDirectoryOffset == 8 &&
            OwnedReservoirLocalRename.WindowsFileNameLengthOffset == 16 && OwnedReservoirLocalRename.WindowsFileNameOffset == 20 && OwnedReservoirLocalRename.WindowsHeaderSize == 24, "x64 ABI layout/class mismatch");
        const string sample = "C:\\bounded\\O₂-\U0001f30d.sav"; var name = Encoding.Unicode.GetBytes(sample);
        foreach (var replace in new[] { false, true })
        {
            var buffer = OwnedReservoirLocalRename.WindowsBufferForTest(sample, replace);
            Check(BitConverter.ToUInt32(buffer, 0) == (replace ? 3U : 0U) && BitConverter.ToInt64(buffer, 8) == 0 && BitConverter.ToUInt32(buffer, 16) == name.Length, "flags/root/UTF16 byte length mismatch");
            Equal(buffer.AsSpan(20, name.Length).ToArray(), name, "UTF16 encoding mismatch");
            Check(buffer[20 + name.Length] == 0 && buffer[21 + name.Length] == 0 && buffer.Length >= 24, "explicit WCHAR NUL missing");
        }
        Throws(() => OwnedReservoirLocalRename.WindowsBufferForTest("C:\\bad\udc00.sav", true), "unpaired surrogate replacement");
        var path = Path.Combine(DirectoryFor("unicode"), "mélange-酸素-\U0001f30d.sav"); File.WriteAllBytes(path, Old);
        ValidSuccess(OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, New)), true);
    }
    private static void MissingBackendStage()
    {
        var target = Target("backend-missing"); File.WriteAllBytes(target, Old); var backend = OwnedReservoirLocalRename.Select(Path.GetDirectoryName(target)!);
        try { OwnedReservoirLocalRename.Rename(backend, target + ".absent", target, true); throw new Exception("missing stage accepted"); }
        catch (OwnedReservoirRenameException error)
        { Check(error.Attempted == !OperatingSystem.IsWindows(), "entrypoint attempted classification differs"); Check(error.NativeError != null, "missing native error"); }
        Equal(File.ReadAllBytes(target), Old, "missing source mutated target");
        Throws(()=>OwnedReservoirLocalRename.Rename(backend,target,Target("backend-other-directory"),true),"cross-directory native rename");
    }
    private static void NativeNoReplaceCollision()
    {
        var target=Target("backend-noreplace"); var stage=target+".stage"; File.WriteAllBytes(target,Old); File.WriteAllBytes(stage,New);
        try { OwnedReservoirLocalRename.Rename(OwnedReservoirLocalRename.Select(Path.GetDirectoryName(target)!),stage,target,false); throw new Exception("occupied target replaced without replace flag"); }
        catch(OwnedReservoirRenameException error) { Check(error.Attempted && error.NativeError!=null,"noreplace failure did not enter native API"); }
        Equal(File.ReadAllBytes(target),Old,"noreplace collision altered old target"); Equal(File.ReadAllBytes(stage),New,"noreplace collision consumed stage");
    }
    private static void AbsentTargetFaults()
    {
        foreach(var fault in new[]{OwnedReservoirRecoverableFault.BeforeNativeRename,OwnedReservoirRecoverableFault.AfterNativeRename,OwnedReservoirRecoverableFault.ChangeTargetBeforeAttempt})
        {
            var target=Target("absent-"+fault); var result=OwnedReservoirRecoverableFilePromotion.PromoteForTest(OwnedReservoirRecoverableFilePromotion.Prepare(target,New),fault);
            var post=fault==OwnedReservoirRecoverableFault.AfterNativeRename;
            Check(result.Outcome==(post?OwnedReservoirRecoverableOutcome.OutcomeUnknown:OwnedReservoirRecoverableOutcome.NotAttempted) && result.PreviousTargetExisted==false && result.PreviousImage==null,"absent target fault semantics");
            Check(File.Exists(target)==(post || fault==OwnedReservoirRecoverableFault.ChangeTargetBeforeAttempt),"absent target created/deleted outside fault/rename");
            var inspection=OwnedReservoirRecoverableFilePromotion.Inspect(target,result.IntentPath!);
            Check(inspection.IntentValid && inspection.PreviousBackup==OwnedReservoirArtifactObservation.NotApplicable && !inspection.CommitAttested,"absent intent lost");
        }
    }
    private static void Fault(OwnedReservoirRecoverableFault fault)
    {
        var result = ReplacementAt("fault-" + fault, fault);
        var post = fault == OwnedReservoirRecoverableFault.InvalidNativeRequest || fault == OwnedReservoirRecoverableFault.AfterNativeRename ||
            fault == OwnedReservoirRecoverableFault.CorruptPromotedTarget || fault == OwnedReservoirRecoverableFault.FailFinalVerification;
        Check(result.Outcome == (post ? OwnedReservoirRecoverableOutcome.OutcomeUnknown : OwnedReservoirRecoverableOutcome.NotAttempted), result.Error?.ToString() ?? "fault outcome mismatch");
        Check(result.NativeRenameAttempted == post && result.Error != null, "fault attempt flags incorrect");
        if (fault == OwnedReservoirRecoverableFault.InvalidNativeRequest)
        {
            Check(result.NativeError != null && result.IntentVerified && !result.NativeRenameReturned, "native failure did not retain intent/error");
            Equal(File.ReadAllBytes(result.TargetPath), Old, "invalid native request overwrote target");
        }
        else if (!post) Equal(File.ReadAllBytes(result.TargetPath), Old, "preattempt failure changed original target");
        else if (fault != OwnedReservoirRecoverableFault.CorruptPromotedTarget) Equal(File.ReadAllBytes(result.TargetPath), New, "post-rename new target missing");
        foreach (var path in result.CreatedPaths)
        {
            var deliberatelyRemoved = (fault == OwnedReservoirRecoverableFault.RemoveBackupBeforeAttempt && path == result.PreviousImagePath) ||
                (fault == OwnedReservoirRecoverableFault.RemoveStageBeforeAttempt && path == result.StagePath) ||
                (fault == OwnedReservoirRecoverableFault.RemoveIntentBeforeAttempt && path == result.IntentPath);
            var consumed = result.NativeRenameReturned && path == result.StagePath;
            Check(File.Exists(path) == !(consumed || deliberatelyRemoved), "helper deleted an artifact or lost accounting");
        }
        if (result.IntentVerified && fault != OwnedReservoirRecoverableFault.CorruptIntentBeforeAttempt && fault != OwnedReservoirRecoverableFault.RemoveIntentBeforeAttempt)
        {
            var inspection = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
            Check(inspection.IntentValid && !inspection.CommitAttested, "prepared intent invalid/committed");
        }
    }
    private static void ChangedTarget()
    {
        var result = ReplacementAt("changed-target", OwnedReservoirRecoverableFault.ChangeTargetBeforeAttempt);
        Check(result.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && !result.NativeRenameAttempted, "changed target was renamed");
        Equal(File.ReadAllBytes(result.TargetPath), new byte[] { 0xe3 }, "helper rolled target back");
        Equal(File.ReadAllBytes(result.PreviousImagePath!), Old, "original recovery backup lost");
    }
    private static void ReadOnlyTarget()
    {
        var path = Target("readonly"); File.WriteAllBytes(path, Old); var attributes = File.GetAttributes(path); File.SetAttributes(path, attributes | FileAttributes.ReadOnly);
        try
        {
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, New));
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && result.CreatedPaths.Count == 0, "readonly target attempted"); Equal(File.ReadAllBytes(path), Old, "readonly target changed");
        }
        finally { File.SetAttributes(path, attributes); }
    }
    private static void ExclusiveReader()
    {
        var path = Target("exclusive"); File.WriteAllBytes(path, Old);
        using (var held = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, New));
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && result.CreatedPaths.Count == 0, "exclusive target was ignored");
        }
        Equal(File.ReadAllBytes(path), Old, "exclusive target changed");
    }
    private static void HandlesClosed()
    {
        var result = ReplacementAt("closed-handles", OwnedReservoirRecoverableFault.BeforeNativeRename);
        foreach (var path in result.CreatedPaths.Append(result.TargetPath))
            using (var opened = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) Check(opened.CanWrite, "owned handle leaked");
    }
    private static void BadFilesystemPaths()
    {
        var missing = Path.Combine(DirectoryFor("missing"), "absent", "file.sav");
        var rejected = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(missing, New));
        Check(rejected.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && rejected.CreatedPaths.Count == 0 && !Directory.Exists(Path.GetDirectoryName(missing)), "parent created implicitly");
        var directory = DirectoryFor("directory-target");
        rejected = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(directory, New));
        Check(rejected.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && rejected.CreatedPaths.Count == 0, "directory target allowed");
    }
    private static void Links()
    {
        var parent = DirectoryFor("links"); var real = Path.Combine(parent, "real"); Directory.CreateDirectory(real); var link = Path.Combine(parent, "link");
        try { Directory.CreateSymbolicLink(link, real); }
        catch (Exception error) when (OperatingSystem.IsWindows() && (error is UnauthorizedAccessException || error is IOException))
        { throw new Exception("Required local symlink test needs developer mode/permission.", error); }
        try
        {
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(Path.Combine(link, "file.sav"), New));
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && result.CreatedPaths.Count == 0, "linked parent allowed");
        }
        finally { Directory.Delete(link); }
        var old = Path.Combine(real, "old.sav"); File.WriteAllBytes(old, Old); var linked = Path.Combine(real, "link.sav"); File.CreateSymbolicLink(linked, old);
        try
        {
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(linked, New));
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.NotAttempted && result.CreatedPaths.Count == 0, "linked target allowed"); Equal(File.ReadAllBytes(old), Old, "link destination mutated");
        }
        finally { File.Delete(linked); }
    }
    private static void DenyDeleteReader()
    {
        var path = Target("deny-delete"); File.WriteAllBytes(path, Old);
        using var held = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, New));
        Check(result.Outcome == OwnedReservoirRecoverableOutcome.OutcomeUnknown && result.NativeRenameAttempted && result.NativeError == 32, result.Error?.ToString() ?? "deny-delete ignored");
        Equal(RecoverableFileData.ReadBounded(held, Old.Length), Old, "old reader changed");
        Equal(File.ReadAllBytes(result.PreviousImagePath!), Old, "previous recovery backup missing");
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(path, result.IntentPath!);
        Check(inspected.IntentValid && inspected.Target == OwnedReservoirTargetObservation.MatchesPrevious && inspected.Stage == OwnedReservoirArtifactObservation.MatchesExpected && !inspected.CommitAttested, "deny-delete reconciliation incorrect");
    }
    private static void Readers()
    {
        var path = Target("continuity"); File.WriteAllBytes(path, Old);
        using var held = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        var stop = 0; long complete = 0, missing = 0, errors = 0, mixed = 0;
        using var ready = new ManualResetEventSlim();
        var reader = Task.Run(() =>
        {
            do
            {
                try
                {
                    using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    var bytes = RecoverableFileData.ReadBounded(stream, New.Length);
                    if (!bytes.AsSpan().SequenceEqual(Old) && !bytes.AsSpan().SequenceEqual(New)) Interlocked.Increment(ref mixed);
                    else Interlocked.Increment(ref complete);
                }
                catch (FileNotFoundException) { Interlocked.Increment(ref missing); }
                catch { Interlocked.Increment(ref errors); }
                ready.Set(); Thread.Yield();
            } while (Volatile.Read(ref stop) == 0);
        });
        ready.Wait(TimeSpan.FromSeconds(10));
        const int promotions = 32; var verified = 0;
        try
        {
            for (var i = 0; i < promotions; ++i)
            {
                var next = i % 2 == 0 ? New : Old;
                var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path, next));
                Check(result.Outcome == OwnedReservoirRecoverableOutcome.ReplacementVerified, result.Error?.ToString() ?? "reader replacement failed"); verified++;
            }
        }
        finally { Volatile.Write(ref stop, 1); Check(reader.Wait(TimeSpan.FromSeconds(10)), "reader did not stop"); }
        Equal(RecoverableFileData.ReadBounded(held, Old.Length), Old, "held old reader changed after replacement");
        continuity = new { Promotions = promotions, Verified = verified, CompleteReads = complete, MissingOpens = missing, OtherErrors = errors, MixedReads = mixed, HeldOldReaderUnchanged = true,
            Scope = "Finite local managed host probe with explicit share-delete readers; not arbitrary reader/filesystem/power-loss proof." };
        Check(complete > 0 && missing == 0 && errors == 0 && mixed == 0 && verified == promotions, JsonSerializer.Serialize(continuity));
    }
    private static Dictionary<string, string> FileSnapshot(string directory) => Directory.GetFiles(directory).OrderBy(p => p, StringComparer.Ordinal).ToDictionary(p => p, p => RecoverableFileData.Hash(File.ReadAllBytes(p)));
    private static void InspectStates()
    {
        var result = ReplacementAt("states", OwnedReservoirRecoverableFault.BeforeNativeRename);
        foreach (var state in new[] { OwnedReservoirTargetObservation.MatchesPrevious, OwnedReservoirTargetObservation.MatchesNew, OwnedReservoirTargetObservation.Other, OwnedReservoirTargetObservation.Missing })
        {
            if (state == OwnedReservoirTargetObservation.MatchesPrevious) File.WriteAllBytes(result.TargetPath, Old);
            else if (state == OwnedReservoirTargetObservation.MatchesNew) File.WriteAllBytes(result.TargetPath, New);
            else if (state == OwnedReservoirTargetObservation.Other) File.WriteAllBytes(result.TargetPath, new byte[] { 9, 8, 7 });
            else File.Delete(result.TargetPath);
            var directory = Path.GetDirectoryName(result.TargetPath)!; var before = FileSnapshot(directory);
            var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
            Check(inspected.IntentValid && inspected.Target == state && inspected.PreviousBackup == OwnedReservoirArtifactObservation.MatchesExpected && !inspected.CommitAttested && !inspected.NativeSavePermitted, "incorrect read-only classification");
            Check(before.OrderBy(p => p.Key).SequenceEqual(FileSnapshot(directory).OrderBy(p => p.Key)), "inspection wrote/deleted a file");
        }
    }
    private static void UnknownRemainsUnknown()
    {
        var result = ReplacementAt("unknown", OwnedReservoirRecoverableFault.AfterNativeRename);
        Check(result.Outcome == OwnedReservoirRecoverableOutcome.OutcomeUnknown && result.NativeRenameReturned, "post-native test not unknown");
        var before = FileSnapshot(Path.GetDirectoryName(result.TargetPath)!);
        for (var i = 0; i < 3; ++i)
        {
            var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
            Check(inspected.IntentValid && inspected.Target == OwnedReservoirTargetObservation.MatchesNew && !inspected.CommitAttested && result.Outcome == OwnedReservoirRecoverableOutcome.OutcomeUnknown, "new bytes reclassified unknown as committed");
            Equal(inspected.PreviousImage!.CopyBytes(), Old, "old image not independently recoverable");
        }
        Check(before.OrderBy(p => p.Key).SequenceEqual(FileSnapshot(Path.GetDirectoryName(result.TargetPath)!).OrderBy(p => p.Key)), "repeated inspection healed unknown");
    }
    private static void InspectBackupFailures()
    {
        var result = ReplacementAt("bad-backup"); File.WriteAllBytes(result.PreviousImagePath!, new byte[] { 1 });
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Check(inspected.IntentValid && inspected.PreviousBackup == OwnedReservoirArtifactObservation.Other && inspected.PreviousImage == null, "corrupt old backup accepted");
        File.Delete(result.PreviousImagePath!); inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Check(inspected.IntentValid && inspected.PreviousBackup == OwnedReservoirArtifactObservation.Missing && inspected.PreviousImage == null, "missing backup manufactured");
        File.WriteAllBytes(result.StagePath!, new byte[] { 2 }); inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Check(inspected.Stage == OwnedReservoirArtifactObservation.Other && File.Exists(result.StagePath), "bad stage healed");
        File.Delete(result.IntentPath!); inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Check(!inspected.IntentValid && inspected.Error != null && !File.Exists(result.IntentPath), "missing intent recreated");
    }
    private static void InspectLockedTarget()
    {
        var result = ReplacementAt("inspect-locked");
        using var held = new FileStream(result.TargetPath, FileMode.Open, FileAccess.Read, FileShare.None);
        var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
        Check(inspected.IntentValid && inspected.Target == OwnedReservoirTargetObservation.Unreadable && !inspected.CommitAttested, "locked target misclassified");
    }
    private static void InspectIdentity()
    {
        var result = ReplacementAt("identity");
        Check(!OwnedReservoirRecoverableFilePromotion.Inspect(Target("different-parent"), result.IntentPath!).IntentValid, "cross-parent intent allowed");
        var changed = Path.Combine(Path.GetDirectoryName(result.TargetPath)!, "other.sav");
        Check(!OwnedReservoirRecoverableFilePromotion.Inspect(changed, result.IntentPath!).IntentValid, "different target basename allowed");
        var wrong = Path.Combine(Path.GetDirectoryName(result.TargetPath)!, ".ma-recover-" + Guid.NewGuid().ToString("N") + ".intent"); File.Copy(result.IntentPath!, wrong);
        Check(!OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, wrong).IntentValid, "foreign operation filename allowed");
        var upper = Path.Combine(Path.GetDirectoryName(result.TargetPath)!, Path.GetFileName(result.TargetPath).ToUpperInvariant());
        Check(!OwnedReservoirRecoverableFilePromotion.Inspect(upper, result.IntentPath!).IntentValid, "case-aliased target identity allowed");
    }
    private static byte[] Recheck(byte[] value)
    {
        using var algorithm = SHA256.Create(); var hash = algorithm.ComputeHash(value, 0, value.Length - 32);
        Array.Copy(hash, 0, value, value.Length - 32, 32); return value;
    }
    private static void HostileIntent()
    {
        var result = ReplacementAt("hostile"); var good = File.ReadAllBytes(result.IntentPath!);
        var targetCount = BitConverter.ToInt32(good, 40); var stageCountOffset = 44 + targetCount; var stageCount = BitConverter.ToInt32(good, stageCountOffset);
        var backupCountOffset = stageCountOffset + 4 + stageCount; var backupCount = BitConverter.ToInt32(good, backupCountOffset);
        var newLengthOffset = backupCountOffset + 4 + backupCount; var oldPresenceOffset = newLengthOffset + 8 + 32; var oldLengthOffset = oldPresenceOffset + 1;
        var attacks = new List<(string, Action<byte[]>)>
        {
            ("magic", b => b[0] ^= 1), ("version", b => Array.Copy(BitConverter.GetBytes(2),0,b,8,4)),
            ("encoded length", b => Array.Copy(BitConverter.GetBytes((long)b.Length-1),0,b,12,8)),
            ("zero operation", b => Array.Clear(b,20,16)), ("unknown backend", b => b[36]=0), ("reserved flags", b => b[37]=1),
            ("huge string length", b => Array.Copy(BitConverter.GetBytes(int.MaxValue),0,b,40,4)),
            ("negative string length", b => Array.Copy(BitConverter.GetBytes(-1),0,b,40,4)),
            ("invalid UTF8", b => b[44]=0xff), ("target separator", b => b[44]=(byte)'/'), ("target colon", b => b[44]=(byte)':'),
            ("stage GUID mismatch", b => b[stageCountOffset+4+12] = b[stageCountOffset+4+12] == (byte)'a' ? (byte)'b' : (byte)'a'),
            ("backup GUID mismatch", b => b[backupCountOffset+4+12] = b[backupCountOffset+4+12] == (byte)'a' ? (byte)'b' : (byte)'a'),
            ("zero new image", b => Array.Clear(b,newLengthOffset,8)),
            ("negative new image", b => Array.Copy(BitConverter.GetBytes(-1L),0,b,newLengthOffset,8)),
            ("oversized new image", b => Array.Copy(BitConverter.GetBytes(1L+OwnedReservoirFilePromotion.MaximumCompleteFileBytes),0,b,newLengthOffset,8)),
            ("unknown presence", b => b[oldPresenceOffset]=2), ("absent previous with nonempty backup", b => b[oldPresenceOffset]=0),
            ("negative old image", b => Array.Copy(BitConverter.GetBytes(-1L),0,b,oldLengthOffset,8)),
            ("oversized old image", b => Array.Copy(BitConverter.GetBytes(1L+OwnedReservoirFilePromotion.MaximumCompleteFileBytes),0,b,oldLengthOffset,8)),
            ("zero old with wrong hash", b => Array.Clear(b,oldLengthOffset,8))
        };
        foreach (var (name, mutate) in attacks)
        {
            var bytes = (byte[])good.Clone(); mutate(bytes); Recheck(bytes); File.WriteAllBytes(result.IntentPath!, bytes);
            var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
            Check(!inspected.IntentValid && inspected.Error != null, "hostile checksummed intent accepted: " + name);
            Equal(File.ReadAllBytes(result.TargetPath), New, "hostile inspection changed target");
        }
        foreach (var size in new[] { 0, 20, good.Length - 1, OwnedReservoirRecoverableFilePromotion.MaximumIntentBytes + 1 })
        {
            File.WriteAllBytes(result.IntentPath!, good.AsSpan(0, Math.Min(size, good.Length)).ToArray());
            if (size > good.Length) using (var writer = new FileStream(result.IntentPath!,FileMode.Open,FileAccess.Write)) writer.SetLength(size);
            Check(!OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath,result.IntentPath!).IntentValid, "invalid complete intent length accepted");
        }
        var badChecksum = (byte[])good.Clone(); badChecksum[^1] ^= 1; File.WriteAllBytes(result.IntentPath!,badChecksum);
        Check(!OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath,result.IntentPath!).IntentValid,"bad checksum accepted");
        var trailing = new byte[good.Length+1]; Array.Copy(good,trailing,good.Length-32); Array.Copy(BitConverter.GetBytes((long)trailing.Length),0,trailing,12,8); Recheck(trailing);
        File.WriteAllBytes(result.IntentPath!,trailing); Check(!OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath,result.IntentPath!).IntentValid,"checksummed trailing body accepted");
    }
    private static void IntentCanonical()
    {
        var result = ReplacementAt("canonical"); var encoded = File.ReadAllBytes(result.IntentPath!); var decoded = RecoverableFileIntent.Decode(encoded);
        Equal(decoded.Encode(),encoded,"intent round trip is not canonical");
        Check(BitConverter.ToInt64(encoded,12)==encoded.Length && decoded.NewHash==RecoverableFileData.Hash(New) && decoded.PreviousHash==RecoverableFileData.Hash(Old),"intent field contract differs");
        var otherBackend = (byte[])encoded.Clone(); otherBackend[36] = OperatingSystem.IsWindows() ? (byte)2 : (byte)1; Recheck(otherBackend); File.WriteAllBytes(result.IntentPath!,otherBackend);
        var before = FileSnapshot(Path.GetDirectoryName(result.TargetPath)!); var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath,result.IntentPath!);
        Check(inspected.IntentValid && !inspected.CommitAttested,"recognized foreign backend data was executed or rejected");
        Check(before.OrderBy(p=>p.Key).SequenceEqual(FileSnapshot(Path.GetDirectoryName(result.TargetPath)!).OrderBy(p=>p.Key)),"foreign backend inspection changed files");
    }
    [DllImport("libc",EntryPoint="mkfifo",SetLastError=true)] private static extern int MkFifo(string path,uint mode);
    private static void LinuxFifo()
    {
        var path=Target("fifo"); Check(MkFifo(path,0x180)==0,"mkfifo failed");
        var result=OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(path,New));
        Check(result.Outcome==OwnedReservoirRecoverableOutcome.NotAttempted && result.CreatedPaths.Count==0 && !result.NativeRenameAttempted,"FIFO entered file capture");
    }
    private static void ProductionAssembly(string path)
    {
        var assembly=Assembly.LoadFile(Path.GetFullPath(path));
        Check(!assembly.GetTypes().Any(t=>t.Name.Contains("Fault",StringComparison.Ordinal)),"host fault enum in production");
        var helper=assembly.GetType("MixedAtmospheres.Oni.OwnedReservoirRecoverableFilePromotion",true)!;
        Check(helper.GetMethod("PromoteForTest",BindingFlags.Static|BindingFlags.NonPublic)==null,"test entrypoint in production");
        var backend=assembly.GetType("MixedAtmospheres.Oni.OwnedReservoirLocalRename",true)!;
        Check(backend.GetMethods(BindingFlags.NonPublic|BindingFlags.Static).Single(m=>m.Name=="Rename").GetParameters().Length==4,"test native parameter in production");
        foreach(var type in assembly.GetTypes().Where(t=>t.Name!="OwnedReservoirFilePromotion"))
        foreach(var method in type.GetMethods(BindingFlags.Static|BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public|BindingFlags.DeclaredOnly))
        foreach(var called in CalledMethods(method))
        {
            Check(!(called.DeclaringType==typeof(File) && called.Name=="Delete"),"production helper deletes a path");
            Check(!(called.DeclaringType?.Name=="OwnedReservoirFilePromotion" && called.Name=="Promote"),"new helper calls historical promoter");
        }
        Check(!assembly.GetReferencedAssemblies().Any(a=>a.Name?.Contains("MixedAtmospheres.Core",StringComparison.Ordinal)==true || a.Name=="Assembly-CSharp"),"live/Core dependency in pure helper");
    }
    private static readonly Dictionary<ushort,OpCode> Opcodes=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Select(f=>(OpCode)f.GetValue(null)!).ToDictionary(o=>unchecked((ushort)o.Value));
    private static IEnumerable<MethodBase> CalledMethods(MethodInfo method)
    {
        var bytes=method.GetMethodBody()?.GetILAsByteArray(); if(bytes==null) yield break;
        for(var offset=0;offset<bytes.Length;)
        {
            ushort value=bytes[offset++]; if(value==0xfe) value=(ushort)(0xfe00|bytes[offset++]); var opcode=Opcodes[value];
            if(opcode.OperandType==OperandType.InlineMethod)
            {
                var called=method.Module.ResolveMethod(BitConverter.ToInt32(bytes,offset),method.DeclaringType?.GetGenericArguments(),method.GetGenericArguments());
                if(called!=null) yield return called;
            }
            offset+=opcode.OperandType switch
            {
                OperandType.InlineNone=>0, OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar=>1,
                OperandType.InlineVar=>2, OperandType.InlineI8 or OperandType.InlineR=>8,
                OperandType.InlineSwitch=>4+4*BitConverter.ToInt32(bytes,offset), _=>4
            };
        }
    }
}
