using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static readonly List<object> Results = new List<object>();
    private static int passed, failed, skipped;
    private static string runRoot = null!;
    private static object? visibilityObservation;

    private static int Main(string[] args)
    {
        if ((args.Length != 1 && args.Length != 2) || !Path.IsPathFullyQualified(args[0]))
        { Console.Error.WriteLine("Usage: OwnedReservoirFilePromotionTests <absolute-private-test-parent> [absolute-production-helper-assembly]"); return 2; }
        var parent = Path.GetFullPath(args[0]);
        Directory.CreateDirectory(parent);
        runRoot = Path.Combine(parent, "file-promotion-tests-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(runRoot);
        Case("production netstandard build excludes faults and game/Core dependencies", f =>
        {
            if (args.Length != 2) throw new Skip("Optional standalone production compilation was not supplied.");
            Require(Path.IsPathFullyQualified(args[1]));
            var assembly = Assembly.LoadFile(Path.GetFullPath(args[1]));
            var helper = assembly.GetType(typeof(OwnedReservoirFilePromotion).FullName!, true)!;
            Require(helper.GetMethod("PromoteForTest", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic) == null);
            Require(assembly.GetType(typeof(OwnedReservoirFilePromotionFault).FullName!) == null);
            Require(assembly.GetReferencedAssemblies().All(reference => reference.Name != "MixedAtmospheres.Core" && reference.Name != "Assembly-CSharp" && !reference.Name!.StartsWith("UnityEngine", StringComparison.Ordinal)));
            f.NoStages();
        });
        Case("opaque complete bytes create absent target", f => SuccessfulWrite(f, false, Bytes(197123, 11)));
        Case("existing target replaced by shorter complete file", f => SuccessfulWrite(f, true, Bytes(65549, 12)));
        Case("existing target replaced by longer complete file", f =>
        {
            File.WriteAllBytes(f.Target, new byte[] { 1 });
            var payload = Bytes(197123, 13);
            var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(f.Target, payload));
            Success(f, result, payload, true);
        });
        Case("source byte array mutation cannot change prepared image", f =>
        {
            var source = Bytes(70001, 14); var expected = (byte[])source.Clone();
            var image = OwnedReservoirFilePromotion.Prepare(f.Target, source);
            Array.Fill(source, (byte)0);
            Require(image.Sha256 == Hash(expected) && image.ByteLength == expected.Length);
            Success(f, OwnedReservoirFilePromotion.Promote(image), expected, false);
        });
        Case("prepare performs no filesystem creation", f =>
        {
            var missing = Path.Combine(f.Root, "missing-parent", "save.bin");
            var image = OwnedReservoirFilePromotion.Prepare(missing, new byte[] { 0, 255 });
            Require(image.TargetPath == missing && !Directory.Exists(Path.GetDirectoryName(missing)));
            Require(Directory.GetFileSystemEntries(f.Root).Length == 1);
        });
        Case("empty payload rejected before filesystem effects", f =>
        {
            Throws<ArgumentOutOfRangeException>(() => OwnedReservoirFilePromotion.Prepare(f.Target, Array.Empty<byte>()));
            Require(!File.Exists(f.Target)); f.NoStages();
        });
        Case("1 GiB length guard checked without allocating a GiB", f =>
        {
            var check = typeof(OwnedReservoirFilePromotion).GetMethod("ValidateCompleteFileLength", BindingFlags.Static | BindingFlags.NonPublic)!;
            foreach (var invalid in new long[] { -1, 0, (long)OwnedReservoirFilePromotion.MaximumCompleteFileBytes + 1, long.MaxValue })
            {
                var error = Throws<TargetInvocationException>(() => check.Invoke(null, new object[] { invalid }));
                Require(error.InnerException is ArgumentOutOfRangeException);
            }
            check.Invoke(null, new object[] { 1L });
            check.Invoke(null, new object[] { (long)OwnedReservoirFilePromotion.MaximumCompleteFileBytes });
            Require(OwnedReservoirFilePromotion.MaximumCompleteFileBytes == 1073741824); f.NoStages();
        });
        Case("malformed and unbounded path inputs rejected", f =>
        {
            string[] paths = { "", " ", "relative.bin", OperatingSystem.IsWindows() ? "\\root-relative.bin" : "relative-again.bin",
                f.Root + Path.DirectorySeparatorChar, Path.Combine(f.Root, ".", "save.bin"), Path.Combine(f.Root, "..", "save.bin"),
                Path.Combine(f.Root, "bad\0name"), Path.Combine(f.Root, "bad\tname"), new string('a', OwnedReservoirFilePromotion.MaximumTargetPathChars + 1),
                Path.Combine(f.Root, new string('x', OwnedReservoirFilePromotion.MaximumFileNameChars + 1)) };
            Throws<ArgumentNullException>(() => OwnedReservoirFilePromotion.Prepare(null!, new byte[] { 1 }));
            foreach (var path in paths) Throws<ArgumentException>(() => OwnedReservoirFilePromotion.Prepare(path, new byte[] { 1 }));
            Require(Directory.GetFileSystemEntries(f.Root).Length == 1);
        });
        Case("platform device and network-style paths rejected", f =>
        {
            var paths = OperatingSystem.IsWindows()
                ? new[] { "\\\\server\\share\\save.bin", "\\\\?\\C:\\save.bin", "\\\\.\\NUL", Path.Combine(f.Root, "NUL.bin"), Path.Combine(f.Root, "CON"), Path.Combine(f.Root, "COM1.sav"), Path.Combine(f.Root, "COM\u00b9.sav"), Path.Combine(f.Root, "CON .txt"), Path.Combine(f.Root, "ads:stream"), Path.Combine(f.Root, "bad?.sav") }
                : new[] { "/dev/null", "/proc/self/mem", "/sys/kernel/save.bin" };
            foreach (var path in paths) Throws<ArgumentException>(() => OwnedReservoirFilePromotion.Prepare(path, new byte[] { 1 }));
            f.NoStages();
        });
        Case("missing parent rejected without creating directories", f =>
        {
            var missing = Path.Combine(f.Root, "not-created", "save.bin");
            var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(missing, Bytes(19, 2)));
            PrePromotionFailure(result, false); Require(!Directory.Exists(Path.GetDirectoryName(missing))); f.NoStages();
        });
        Case("file used as parent rejected without mutation", f =>
        {
            var parentFile = Path.Combine(f.Root, "parent-file"); var original = Bytes(40, 3); File.WriteAllBytes(parentFile, original);
            var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(Path.Combine(parentFile, "save.bin"), Bytes(19, 2)));
            PrePromotionFailure(result, false); Equal(File.ReadAllBytes(parentFile), original); f.NoStages();
        });
        Case("directory target rejected before staging", f =>
        {
            Directory.CreateDirectory(f.Target);
            var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(19, 2)));
            PrePromotionFailure(result, false); Require(Directory.Exists(f.Target)); f.NoStages();
        });
        Case("readonly existing target rejected without truncation", f =>
        {
            var original = Bytes(9001, 4); File.WriteAllBytes(f.Target, original);
            var image = OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(2001, 5));
            File.SetAttributes(f.Target, File.GetAttributes(f.Target) | FileAttributes.ReadOnly);
            try
            {
                var result = OwnedReservoirFilePromotion.Promote(image);
                PrePromotionFailure(result, false); Equal(File.ReadAllBytes(f.Target), original);
                Require((File.GetAttributes(f.Target) & FileAttributes.ReadOnly) != 0); f.NoStages();
            }
            finally { File.SetAttributes(f.Target, File.GetAttributes(f.Target) & ~FileAttributes.ReadOnly); }
        });

        foreach (var existing in new[] { false, true })
        foreach (var fault in new[] { OwnedReservoirFilePromotionFault.AfterCreate, OwnedReservoirFilePromotionFault.AfterPartialWrite, OwnedReservoirFilePromotionFault.AfterFlush,
            OwnedReservoirFilePromotionFault.AfterReadback, OwnedReservoirFilePromotionFault.BeforePromotion, OwnedReservoirFilePromotionFault.CorruptStage, OwnedReservoirFilePromotionFault.TruncateStage })
            Case($"{fault}: {(existing ? "existing target preserved" : "absent target stays absent")}", f =>
            {
                var original = Bytes(991, 6); if (existing) File.WriteAllBytes(f.Target, original);
                var writeTime = existing ? File.GetLastWriteTimeUtc(f.Target) : default;
                var result = OwnedReservoirFilePromotion.PromoteForTest(OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(99991, 7)), fault);
                PrePromotionFailure(result, true);
                if (existing) { Equal(File.ReadAllBytes(f.Target), original); Require(File.GetLastWriteTimeUtc(f.Target) == writeTime); }
                else Require(!File.Exists(f.Target));
                f.NoStages();
            });

        Case("CreateNew collision never deletes preexisting foreign stage", f =>
        {
            var collision = Path.Combine(f.Root, ".ma-save-host-collision.stage"); var foreign = Bytes(56, 8); File.WriteAllBytes(collision, foreign);
            var original = Bytes(71, 9); File.WriteAllBytes(f.Target, original);
            var result = OwnedReservoirFilePromotion.PromoteForTest(OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(503, 10)), OwnedReservoirFilePromotionFault.StageNameCollision);
            PrePromotionFailure(result, false); Equal(File.ReadAllBytes(collision), foreign); Equal(File.ReadAllBytes(f.Target), original);
            File.Delete(collision); f.NoStages();
        });
        Case("cleanup failure retains only this invocation's fresh stage", f =>
        {
            var original = Bytes(33, 11); File.WriteAllBytes(f.Target, original); var image = OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(211, 12));
            var result = OwnedReservoirFilePromotion.PromoteForTest(image, OwnedReservoirFilePromotionFault.CleanupFailure);
            Require(result.Outcome == OwnedReservoirFilePromotionOutcome.NotPromoted && !result.PromotionAttempted && result.StageWasCreated && result.StagingReadbackVerified);
            Require(result.Error != null && result.StageCleanupError != null && result.RetainedStagePath != null);
            Require(Path.GetDirectoryName(result.RetainedStagePath) == f.Root && Path.GetFileName(result.RetainedStagePath!).StartsWith(OwnedReservoirFilePromotion.StagingPrefix, StringComparison.Ordinal));
            Equal(File.ReadAllBytes(f.Target), original);
            // Windows exclusive reopen proves our failed write/verification
            // scopes were disposed before the cleanup attempt and result.
            using (var reopened = new FileStream(result.RetainedStagePath!, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) Require(reopened.Length == image.ByteLength);
            File.Delete(result.RetainedStagePath!); f.NoStages();
        });
        Case("fresh staging identities differ across invocations", f =>
        {
            var image = OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(17, 13));
            var a = OwnedReservoirFilePromotion.PromoteForTest(image, OwnedReservoirFilePromotionFault.CleanupFailure);
            var b = OwnedReservoirFilePromotion.PromoteForTest(image, OwnedReservoirFilePromotionFault.CleanupFailure);
            Require(a.RetainedStagePath != null && b.RetainedStagePath != null && a.RetainedStagePath != b.RetainedStagePath);
            foreach (var stage in new[] { a.RetainedStagePath!, b.RetainedStagePath! })
            {
                var name = Path.GetFileName(stage);
                Require(name.Length == OwnedReservoirFilePromotion.StagingPrefix.Length + 32 + OwnedReservoirFilePromotion.StagingSuffix.Length);
                Require(Guid.TryParseExact(name.Substring(OwnedReservoirFilePromotion.StagingPrefix.Length, 32), "N", out _));
                File.Delete(stage);
            }
            Require(!File.Exists(f.Target)); f.NoStages();
        });
        foreach (var existing in new[] { false, true })
            Case($"post-promotion failure remains unknown despite matching {(existing ? "replaced" : "new")} target", f =>
            {
                var original = Bytes(333, 14); if (existing) File.WriteAllBytes(f.Target, original);
                var payload = Bytes(17777, 15); var image = OwnedReservoirFilePromotion.Prepare(f.Target, payload);
                var result = OwnedReservoirFilePromotion.PromoteForTest(image, OwnedReservoirFilePromotionFault.AfterPromotion);
                Require(result.Outcome == OwnedReservoirFilePromotionOutcome.PromotionOutcomeUnknown && result.PromotionAttempted && result.StagingReadbackVerified && result.Error is IOException);
                Require(result.ReplacingExistingTarget == existing && result.StageCleanupError == null && result.RetainedStagePath == null);
                Equal(File.ReadAllBytes(f.Target), payload); f.NoStages();
            });
        Case("failed final verification never deletes or restores promoted target", f =>
        {
            var original = Bytes(333, 16); File.WriteAllBytes(f.Target, original); var payload = Bytes(17777, 17);
            var result = OwnedReservoirFilePromotion.PromoteForTest(OwnedReservoirFilePromotion.Prepare(f.Target, payload), OwnedReservoirFilePromotionFault.CorruptPromotedFile);
            Require(result.Outcome == OwnedReservoirFilePromotionOutcome.PromotionOutcomeUnknown && result.PromotionAttempted && result.Error is InvalidDataException);
            var observed = File.ReadAllBytes(f.Target); Require(observed.Length == payload.Length && observed[0] == (byte)(payload[0] ^ 0xff));
            Equal(observed.AsSpan(1).ToArray(), payload.AsSpan(1).ToArray()); Require(!observed.SequenceEqual(original)); f.NoStages();
        });
        Case("real Windows sharing-denied Replace preserves original without delete fallback", f =>
        {
            if (!OperatingSystem.IsWindows()) throw new Skip("Windows sharing-denied Replace is not Unix rename semantics.");
            var original = Bytes(999, 18); File.WriteAllBytes(f.Target, original);
            OwnedReservoirFilePromotionResult result;
            using (var locked = new FileStream(f.Target, FileMode.Open, FileAccess.Read, FileShare.Read))
                result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(2001, 19)));
            Require(result.PromotionAttempted && result.StagingReadbackVerified && result.Outcome == OwnedReservoirFilePromotionOutcome.PromotionOutcomeUnknown && result.Error is IOException);
            Require(result.StageCleanupError == null && result.CreatedStagePath != null && result.RetainedStagePath == result.CreatedStagePath);
            Equal(File.ReadAllBytes(f.Target), original); Equal(File.ReadAllBytes(result.RetainedStagePath!), Bytes(2001, 19));
            // The host owns this whole private fixture and has independently
            // inspected both files. Production never performs this deletion
            // or retries the unknown operation automatically.
            File.Delete(result.RetainedStagePath!); f.NoStages();
        });
        Case("real Linux readonly parent produces IO failure before promotion", f =>
        {
            if (!OperatingSystem.IsLinux()) throw new Skip("Unix directory permissions are not Windows readonly directory attributes.");
            var original = Bytes(31, 20); File.WriteAllBytes(f.Target, original);
            var mode = File.GetUnixFileMode(f.Root);
            try
            {
                File.SetUnixFileMode(f.Root, UnixFileMode.UserRead | UnixFileMode.UserExecute);
                var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(51, 21)));
                PrePromotionFailure(result, false); Equal(File.ReadAllBytes(f.Target), original); f.NoStages();
            }
            finally { File.SetUnixFileMode(f.Root, mode); }
        });
        Case("existing symbolic-link target is rejected before staging", f =>
        {
            var pointed = Path.Combine(f.Root, "pointed.bin"); var original = Bytes(37, 22); File.WriteAllBytes(pointed, original);
            TryLink(() => File.CreateSymbolicLink(f.Target, pointed));
            var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(f.Target, Bytes(59, 23)));
            PrePromotionFailure(result, false); Equal(File.ReadAllBytes(pointed), original); Require((File.GetAttributes(f.Target) & FileAttributes.ReparsePoint) != 0); f.NoStages();
        });
        Case("symbolic-link parent is rejected before staging", f =>
        {
            var directory = Path.Combine(f.Root, "actual-parent"); Directory.CreateDirectory(directory);
            var link = Path.Combine(f.Root, "link-parent"); TryLink(() => Directory.CreateSymbolicLink(link, directory));
            var result = OwnedReservoirFilePromotion.Promote(OwnedReservoirFilePromotion.Prepare(Path.Combine(link, "save.bin"), Bytes(61, 24)));
            PrePromotionFailure(result, false); Require(Directory.GetFileSystemEntries(directory).Length == 0); f.NoStages();
        });
        Case("bounded Replace observations verify successful-read contents without promising uninterrupted names", f =>
        {
            var old = Bytes(65539, 25); var next = Bytes(131101, 26); File.WriteAllBytes(f.Target, old);
            var oldImage = OwnedReservoirFilePromotion.Prepare(f.Target, old); var nextImage = OwnedReservoirFilePromotion.Prepare(f.Target, next);
            using var started = new ManualResetEventSlim(); using var stop = new CancellationTokenSource(); var reads = 0; var missing = 0;
            var otherOpenErrors = new Dictionary<string, int>(); var writes = 0;
            var reader = Task.Run(() =>
            {
                while (!stop.IsCancellationRequested)
                {
                    FileStream stream;
                    try { stream = new FileStream(f.Target, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete); }
                    catch (FileNotFoundException) { ++missing; Thread.Yield(); continue; }
                    catch (IOException error)
                    {
                        var key = error.GetType().Name + ":0x" + error.HResult.ToString("x8");
                        otherOpenErrors[key] = otherOpenErrors.TryGetValue(key, out var count) ? count + 1 : 1;
                        Thread.Yield(); continue;
                    }
                    using (stream)
                    {
                    using var memory = new MemoryStream(); stream.CopyTo(memory); var observed = memory.ToArray();
                    Require(observed.SequenceEqual(old) || observed.SequenceEqual(next), "Reader observed partial/mixed content.");
                    Interlocked.Increment(ref reads); started.Set();
                    }
                }
            });
            try
            {
                Require(started.Wait(TimeSpan.FromSeconds(10)), "Reader did not start.");
                for (var i = 0; i < 12; ++i)
                {
                    // Each new attempt follows a known successful result. No
                    // unknown result is retried or inferred safe by this test.
                    var result = OwnedReservoirFilePromotion.Promote((i & 1) == 0 ? nextImage : oldImage);
                    Require(result.Outcome == OwnedReservoirFilePromotionOutcome.PromotedAndVerified);
                    ++writes;
                }
            }
            finally { stop.Cancel(); reader.GetAwaiter().GetResult(); }
            visibilityObservation = new { VerifiedSuccessfulReads = reads, VerifiedPromotions = writes, MissingPathOpens = missing, OtherOpenErrors = otherOpenErrors,
                UninterruptedPathVisibilityAttested = false, Scope = "12 bounded local same-directory replacements; every successful open/read must be exactly one complete variant. Transient open failures are recorded, never hidden by a helper retry." };
            Require(reads > 0); Equal(File.ReadAllBytes(f.Target), old); f.NoStages();
        });
        Case("null image rejected before IO", f => { Throws<ArgumentNullException>(() => OwnedReservoirFilePromotion.Promote(null!)); f.NoStages(); });
        Case("undefined host fault enum is rejected", f =>
        { Throws<ArgumentOutOfRangeException>(() => OwnedReservoirFilePromotion.PromoteForTest(OwnedReservoirFilePromotion.Prepare(f.Target, new byte[] { 1 }), (OwnedReservoirFilePromotionFault)999)); f.NoStages(); });

        var drive = new DriveInfo(Path.GetPathRoot(runRoot)!);
        var report = new
        {
            Kind = "original pure-managed complete-file staging helper checks; no game or Core",
            RecordedUtc = DateTime.UtcNow.ToString("o"), Passed = failed == 0, ChecksPassed = passed, ChecksFailed = failed, ChecksSkipped = skipped,
            Runtime = RuntimeInformation.FrameworkDescription, OS = RuntimeInformation.OSDescription, Architecture = RuntimeInformation.ProcessArchitecture.ToString(),
            TestRoot = runRoot, DriveFormat = drive.DriveFormat, MaximumCompleteFileBytes = OwnedReservoirFilePromotion.MaximumCompleteFileBytes,
            FlushTrueExercised = true, SameDirectoryPromotionExercised = true,
            PowerLossDurabilityVerified = false, GeneralFilesystemAtomicityAttested = false, UninterruptedNamespaceGatePassed = false, OldTargetRecoveryGatePassed = false, LinuxUnityVerified = false,
            SaveFormatOrEligibilityAttested = false, NativeSavePermissionChanged = false, VisibilityObservation = visibilityObservation, Results
        };
        var reportPath = Path.Combine(runRoot, "result.json");
        File.WriteAllText(reportPath, JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }) + "\n");
        Console.WriteLine($"{passed} passed, {failed} failed, {skipped} platform skips. Evidence: {reportPath}");
        return failed == 0 ? 0 : 1;
    }

    private static void SuccessfulWrite(Fixture f, bool existing, byte[] payload)
    {
        if (existing) File.WriteAllBytes(f.Target, Bytes(262144, 91));
        var image = OwnedReservoirFilePromotion.Prepare(f.Target, payload);
        Require(image.ByteLength == payload.Length && image.Sha256 == Hash(payload));
        Success(f, OwnedReservoirFilePromotion.Promote(image), payload, existing);
    }
    private static void Success(Fixture f, OwnedReservoirFilePromotionResult result, byte[] expected, bool existing)
    {
        Require(result.Outcome == OwnedReservoirFilePromotionOutcome.PromotedAndVerified && result.ReplacingExistingTarget == existing);
        Require(result.StageWasCreated && result.StagingReadbackVerified && result.PromotionAttempted && result.Error == null && result.StageCleanupError == null && result.RetainedStagePath == null);
        Require(result.TargetPath == f.Target && result.ByteLength == expected.Length && result.Sha256 == Hash(expected));
        Equal(File.ReadAllBytes(f.Target), expected); f.NoStages();
    }
    private static void PrePromotionFailure(OwnedReservoirFilePromotionResult result, bool created)
    {
        Require(result.Outcome == OwnedReservoirFilePromotionOutcome.NotPromoted && !result.PromotionAttempted && result.Error != null);
        Require(result.StageWasCreated == created && result.StageCleanupError == null && result.RetainedStagePath == null);
    }
    private static void Case(string name, Action<Fixture> body)
    {
        using var fixture = new Fixture(runRoot);
        try { body(fixture); fixture.SentinelUnchanged(); ++passed; Results.Add(new { Name = name, Status = "passed" }); Console.WriteLine("PASS " + name); }
        catch (Skip reason) { ++skipped; Results.Add(new { Name = name, Status = "skipped", Reason = reason.Message }); Console.WriteLine("SKIP " + name + ": " + reason.Message); }
        catch (Exception error) { fixture.Keep = true; ++failed; Results.Add(new { Name = name, Status = "failed", Error = error.ToString(), Directory = fixture.Root }); Console.WriteLine("FAIL " + name + ": " + error); }
    }
    private static void TryLink(Action create)
    {
        try { create(); }
        catch (UnauthorizedAccessException error) { throw new Skip("Symlink creation unavailable to this account: " + error.Message); }
        catch (PlatformNotSupportedException error) { throw new Skip("Symlink creation unavailable on this filesystem: " + error.Message); }
    }
    private static byte[] Bytes(int length, int seed) { var result = new byte[length]; new Random(seed).NextBytes(result); return result; }
    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    private static void Equal(byte[] a, byte[] b) => Require(a.SequenceEqual(b), "File bytes differ.");
    private static void Require(bool condition, string message = "Assertion failed.") { if (!condition) throw new InvalidOperationException(message); }
    private static T Throws<T>(Action action) where T : Exception
    { try { action(); } catch (T error) { return error; } throw new InvalidOperationException("Expected " + typeof(T).Name); }
    private sealed class Skip : Exception { internal Skip(string message) : base(message) { } }
    private sealed class Fixture : IDisposable
    {
        private readonly string parent;
        private readonly byte[] sentinel = Bytes(101, 58);
        internal string Root { get; }
        internal string Target => Path.Combine(Root, "colony-complete-file.bin");
        internal bool Keep { get; set; }
        internal Fixture(string parent)
        {
            this.parent = Path.GetFullPath(parent);
            Root = Path.Combine(this.parent, "case-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Root); File.WriteAllBytes(Path.Combine(Root, "unrelated-sentinel.bin"), sentinel);
        }
        internal void NoStages() => Require(Directory.GetFiles(Root, OwnedReservoirFilePromotion.StagingPrefix + "*" + OwnedReservoirFilePromotion.StagingSuffix).Length == 0);
        internal void SentinelUnchanged() => Equal(File.ReadAllBytes(Path.Combine(Root, "unrelated-sentinel.bin")), sentinel);
        public void Dispose()
        {
            if (Keep) return;
            var full = Path.GetFullPath(Root);
            if (!full.StartsWith(parent + Path.DirectorySeparatorChar, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
                throw new InvalidOperationException("Refusing to delete outside this test invocation's private root.");
            Directory.Delete(full, true);
        }
    }
}
