using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Oni;

namespace MixedAtmospheres.Validation
{
    // Private disposable-fixture validator, not a mod/save integration API.
    // The production helpers are compiled here without either host fault symbol.
    public static class GameMonoFileProtocolValidation
    {
        public static string Run(string absoluteFreshParent)
        {
            var context = new Context();
            try
            {
                context.Metadata();
                if (absoluteFreshParent == null || !Path.IsPathFullyQualified(absoluteFreshParent))
                    throw new ArgumentException("An existing explicitly absolute caller output directory is required.");
                var parent = Path.GetDirectoryName(OwnedReservoirFilePromotion.ValidateTargetPath(Path.Combine(absoluteFreshParent, "validation-parent")))!;
                RecoverableFileData.RequireParentChain(parent);
                var backend = OwnedReservoirLocalRename.Select(parent);
                if (backend != OwnedReservoirRenameBackend.WindowsNtfsRenameInfoEx)
                    throw new PlatformNotSupportedException("This private Mono validation is scoped to the Windows x64 NTFS backend.");
                var invocation = Guid.NewGuid();
                var directoryId = "f" + Convert.ToBase64String(invocation.ToByteArray()).TrimEnd('=').Replace('+', '-').Replace('/', '_');
                var root = Path.Combine(parent, directoryId);
                // This harness-only budget is deliberately conservative for
                // the tested legacy game Mono. It changes neither helper path
                // policy nor Win32 paths, aliases, settings or fallback rules.
                var longest = Path.Combine(root, "n", ".ma-recover-" + new string('0', 32) + ".intent");
                context.Report["FixtureInvocationId"] = invocation.ToString("D");
                context.Report["FixtureDirectoryId"] = directoryId;
                context.Report["HarnessMaximumPathChars"] = 240;
                context.Report["PredictedMaximumArtifactPathChars"] = longest.Length;
                if (longest.Length > 240) throw new IOException("Private harness derived artifact paths exceed its 240 UTF16 character budget before fixture IO.");
                if (Directory.Exists(root) || File.Exists(root)) throw new IOException("Fresh invocation directory already exists.");
                Directory.CreateDirectory(root); context.Root = root;
                context.Report["FixtureRoot"] = context.Root;
                context.Report["Backend"] = backend.ToString();
                context.Case("normal-production-runtime-and-ABI", () => RuntimeContract(context));
                context.Case("absent-target-and-input-copy", () => Success(context, "new", null, false));
                context.Case("replacement-and-retained-previous-stream", () => Success(context, "replacement", OldBytes(), false));
                context.Case("equal-old-new-reconciliation-is-ambiguous", () => Success(context, "equal", NewBytes(), false));
                context.Case("zero-byte-existing-previous-stream", () => Success(context, "zero-old", Array.Empty<byte>(), false));
                context.Case("Unicode-filenames-and-native-UTF16-buffer", () => Success(context, "unicode", OldBytes(), true));
                context.Case("share-delete-held-old-reader", () => HeldReader(context));
                context.Case("deny-delete-error32-keeps-Unknown-and-prior-backup", () => DenyDeleteReader(context));
                context.Case("strict-selected-intent-read-only-rejection", () => StrictInspection(context));
            }
            catch (Exception error) { context.Failure("setup", error); }
            context.Report["Passed"] = context.Failed == 0;
            context.Report["ChecksPassed"] = context.Passed;
            context.Report["ChecksFailed"] = context.Failed;
            context.Report["Checks"] = context.Checks;
            context.Report["CompletedUtc"] = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture);
            if (context.Root != null)
            {
                try { context.Report["FixtureArtifacts"] = context.Artifacts(); }
                catch (Exception error) { context.Failure("artifact-manifest", error); context.Report["Passed"] = false; context.Report["ChecksFailed"] = context.Failed; }
                var reportPath = Path.Combine(context.Root, "file-protocol-report.json");
                context.Report["ReportPath"] = reportPath;
                context.Report["ReportFileWriteVerified"] = true;
                var proposed = Json(context.Report);
                try { Seed(reportPath, Encoding.UTF8.GetBytes(proposed)); }
                catch (Exception error)
                {
                    // Return failure text to the caller for archival. Do not
                    // retry/truncate an uncertain/partially created report.
                    context.Report["Passed"] = false;
                    context.Report["ReportFileWriteVerified"] = false;
                    context.Report["ReportWriteError"] = error.ToString();
                    context.Failure("report-write", error);
                    context.Report["ChecksFailed"] = context.Failed;
                }
            }
            return Json(context.Report);
        }

        private static void RuntimeContract(Context context)
        {
            Check(IntPtr.Size == 8 && RuntimeInformation.ProcessArchitecture == Architecture.X64, "Wrong native process architecture.");
            Check(OwnedReservoirLocalRename.FileRenameInfoEx == 22 && OwnedReservoirLocalRename.WindowsRootDirectoryOffset == 8 &&
                OwnedReservoirLocalRename.WindowsFileNameLengthOffset == 16 && OwnedReservoirLocalRename.WindowsFileNameOffset == 20 &&
                OwnedReservoirLocalRename.WindowsHeaderSize == 24, "Checked managed FILE_RENAME_INFO ABI differs.");
            var assembly = typeof(GameMonoFileProtocolValidation).Assembly;
            foreach (var type in assembly.GetTypes())
                Check(type.Name.IndexOf("Fault", StringComparison.Ordinal) < 0, "Host fault enum exists in this assembly.");
            Check(typeof(OwnedReservoirRecoverableFilePromotion).GetMethod("PromoteForTest", BindingFlags.Static | BindingFlags.NonPublic) == null,
                "A host-only promoter seam was compiled.");
            Check(typeof(OwnedReservoirLocalRename).GetMethod("WindowsBufferForTest", BindingFlags.Static | BindingFlags.NonPublic) == null,
                "A host-only ABI seam was compiled.");
            var method = typeof(OwnedReservoirLocalRename).GetMethod("Rename", BindingFlags.Static | BindingFlags.NonPublic)!;
            Check(method.GetParameters().Length == 4, "Native rename has a host fault parameter.");
            context.Active!["FileRenameInfoEx"] = 22;
            context.Active["RootOffset"] = 8; context.Active["LengthOffset"] = 16;
            context.Active["NameOffset"] = 20; context.Active["HeaderBytes"] = 24;
            context.Active["NoHostFaultSymbol"] = true;
        }

        private static void Success(Context context, string label, byte[]? previous, bool unicode)
        {
            var directory = context.Directory(label);
            var target = Path.Combine(directory, unicode ? "mélange-酸素-\U0001f30d.sav" : "candidate.sav");
            if (previous != null) Seed(target, previous);
            var source = NewBytes(); var expected = (byte[])source.Clone();
            var image = OwnedReservoirRecoverableFilePromotion.Prepare(target, source); source[0] ^= 0xff;
            var result = OwnedReservoirRecoverableFilePromotion.Promote(image);
            context.Active!["Promotion"] = Describe(result);
            var replacing = previous != null;
            Check(result.Outcome == (replacing ? OwnedReservoirRecoverableOutcome.ReplacementVerified : OwnedReservoirRecoverableOutcome.NewFileVerified), result.Error?.ToString() ?? "Unexpected native promotion outcome.");
            Check(result.Error == null && result.NativeRenameAttempted && result.NativeRenameReturned && result.StageVerified && result.IntentVerified,
                "Successful native operation lacks completed verification.");
            Check(result.PreviousTargetExisted == replacing && result.PreviousImageVerified == replacing, "Previous-file presence or backup verification differs.");
            Check(result.CreatedPaths.Count == (replacing ? 3 : 2) && !File.Exists(result.StagePath), "Wrong retained artifact accounting.");
            Equal(Read(target), expected, "Target did not match the copied immutable candidate.");
            if (previous != null)
            {
                Equal(Read(result.PreviousImagePath!), previous, "Prior default-stream backup differs.");
                Equal(result.PreviousImage!.CopyBytes(), previous, "Immutable previous-image copy differs.");
                var copy = result.PreviousImage.CopyBytes();
                if (copy.Length != 0) copy[0] ^= 0xff;
                Equal(result.PreviousImage.CopyBytes(), previous, "Previous-image mutable data escaped.");
            }
            var both = previous != null && Same(previous, expected);
            var inspected = InspectUnchanged(context, result, both ? OwnedReservoirTargetObservation.MatchesBoth : OwnedReservoirTargetObservation.MatchesNew);
            Check(inspected.PreviousBackup == (replacing ? OwnedReservoirArtifactObservation.MatchesExpected : OwnedReservoirArtifactObservation.NotApplicable),
                "Read-only inspection prior-backup status differs.");
            Check(inspected.Stage == OwnedReservoirArtifactObservation.Missing, "Successful rename left a stage or inspection recreated it.");
            if (previous != null) Equal(inspected.PreviousImage!.CopyBytes(), previous, "Read-only prior bytes differ.");
            else Check(inspected.PreviousImage == null, "Absent previous file was manufactured.");
            context.Active["StrictReadOnlyInspection"] = Describe(inspected);
        }

        private static void HeldReader(Context context)
        {
            var target = Path.Combine(context.Directory("held-reader"), "candidate.sav"); var old = OldBytes(); Seed(target, old);
            using var held = new FileStream(target, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(target, NewBytes()));
            context.Active!["Promotion"] = Describe(result);
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.ReplacementVerified, result.Error?.ToString() ?? "Share-delete replacement failed.");
            Equal(RecoverableFileData.ReadBounded(held, old.Length), old, "Held old handle changed after rename.");
            Equal(Read(target), NewBytes(), "New name did not expose the new complete stream.");
            Equal(Read(result.PreviousImagePath!), old, "Held-reader replacement lost the prior-image backup.");
            context.Active["HeldOldReaderUnchanged"] = true;
            context.Active["StrictReadOnlyInspection"] = Describe(InspectUnchanged(context, result, OwnedReservoirTargetObservation.MatchesNew));
        }

        private static void DenyDeleteReader(Context context)
        {
            var target = Path.Combine(context.Directory("deny-delete-reader"), "candidate.sav"); var old = OldBytes(); Seed(target, old);
            using var held = new FileStream(target, FileMode.Open, FileAccess.Read, FileShare.Read);
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(target, NewBytes()));
            context.Active!["Promotion"] = Describe(result);
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.OutcomeUnknown && result.NativeRenameAttempted && !result.NativeRenameReturned && result.NativeError == 32,
                "Expected deny-delete native error32/Unknown; no fallback or success is allowed.");
            Check(result.StageVerified && result.PreviousImageVerified && result.IntentVerified, "Native rejection occurred without a verified recovery preparation.");
            Equal(Read(target), old, "Deny-delete target bytes changed."); Equal(RecoverableFileData.ReadBounded(held, old.Length), old, "Deny-delete old handle changed.");
            Equal(Read(result.PreviousImagePath!), old, "Deny-delete recovery backup differs."); Equal(Read(result.StagePath!), NewBytes(), "Deny-delete stage lost.");
            var inspected = InspectUnchanged(context, result, OwnedReservoirTargetObservation.MatchesPrevious);
            Check(inspected.Stage == OwnedReservoirArtifactObservation.MatchesExpected && inspected.PreviousBackup == OwnedReservoirArtifactObservation.MatchesExpected,
                "Native-error recovery inspection lost its staged or prior bytes.");
            Equal(inspected.PreviousImage!.CopyBytes(), old, "Recovery did not return the immutable verified prior stream.");
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.OutcomeUnknown, "Read-only reconciliation settled Unknown.");
            context.Active["UnknownRemainedUnknown"] = true;
            context.Active["StrictReadOnlyInspection"] = Describe(inspected);
        }

        private static OwnedReservoirRecoveryInspection InspectUnchanged(Context context, OwnedReservoirRecoverableResult result, OwnedReservoirTargetObservation target)
        {
            var directory = Path.GetDirectoryName(result.TargetPath)!; var before = Snapshot(directory);
            OwnedReservoirRecoveryInspection? inspected = null;
            for (var repeat = 0; repeat < 2; ++repeat)
            {
                inspected = OwnedReservoirRecoverableFilePromotion.Inspect(result.TargetPath, result.IntentPath!);
                Check(inspected.IntentValid && inspected.Error == null && inspected.OperationId == result.OperationId && inspected.Target == target,
                    inspected.Error?.ToString() ?? "Selected-intent identity or target classification differs.");
                Check(!inspected.CommitAttested && !inspected.NativeSavePermitted, "Inspection falsely attests commit/save permission.");
            }
            Check(before == Snapshot(directory), "Read-only intent inspection wrote, deleted or changed a file.");
            context.Active!["InspectionLeftNamesBytesLengthsAndWriteTimesUnchanged"] = true;
            return inspected!;
        }

        private static void StrictInspection(Context context)
        {
            var directory = context.Directory("strict-inspection-source"); var target = Path.Combine(directory, "candidate.sav"); Seed(target, OldBytes());
            var result = OwnedReservoirRecoverableFilePromotion.Promote(OwnedReservoirRecoverableFilePromotion.Prepare(target, NewBytes()));
            context.Active!["Promotion"] = Describe(result);
            Check(result.Outcome == OwnedReservoirRecoverableOutcome.ReplacementVerified, result.Error?.ToString() ?? "Strict inspection setup failed.");
            var encoded = Read(result.IntentPath!);
            var rejected = new List<object?>();
            foreach (var bad in new[] { "checksum", "checksummed-version", "filename-identity", "target-identity", "cross-parent" })
            {
                var folder = context.Directory("strict-" + bad); var badTarget = Path.Combine(folder, "candidate.sav"); Seed(badTarget, NewBytes());
                var bytes = (byte[])encoded.Clone();
                if (bad == "checksum") bytes[bytes.Length - 1] ^= 0xff;
                if (bad == "checksummed-version")
                {
                    bytes[8] = 2;
                    using var algorithm = SHA256.Create(); var checksum = algorithm.ComputeHash(bytes, 0, bytes.Length - 32);
                    Array.Copy(checksum, 0, bytes, bytes.Length - 32, 32);
                }
                var intent = Path.Combine(folder, bad == "filename-identity" ? "foreign.intent" : Path.GetFileName(result.IntentPath!)); Seed(intent, bytes);
                var before = Snapshot(folder);
                var inspected = OwnedReservoirRecoverableFilePromotion.Inspect(
                    bad == "target-identity" ? Path.Combine(folder, "other.sav") : bad == "cross-parent" ? target : badTarget, intent);
                rejected.Add(new Dictionary<string, object?> { ["Kind"] = bad, ["Inspection"] = Describe(inspected) });
                Check(!inspected.IntentValid && inspected.Error != null && !inspected.CommitAttested && !inspected.NativeSavePermitted,
                    "Malformed or mismatched intent was accepted: " + bad);
                Check(before == Snapshot(folder), "Rejected intent caused writes or healing: " + bad);
            }
            context.Active["RejectedIntents"] = rejected;
            context.Active["OriginalValidInspection"] = Describe(InspectUnchanged(context, result, OwnedReservoirTargetObservation.MatchesNew));
        }

        private static Dictionary<string, object?> Describe(OwnedReservoirRecoverableResult value) => new Dictionary<string, object?>
        {
            ["OperationId"] = value.OperationId.ToString("D"), ["Outcome"] = value.Outcome.ToString(), ["Backend"] = value.Backend?.ToString(),
            ["TargetPath"] = value.TargetPath, ["NewByteLength"] = value.NewByteLength, ["NewSha256"] = value.NewSha256,
            ["PreviousTargetExisted"] = value.PreviousTargetExisted, ["PreviousByteLength"] = value.PreviousImage?.ByteLength,
            ["PreviousSha256"] = value.PreviousImage?.Sha256, ["StagePath"] = value.StagePath, ["PreviousImagePath"] = value.PreviousImagePath,
            ["IntentPath"] = value.IntentPath, ["CreatedPaths"] = value.CreatedPaths, ["StageVerified"] = value.StageVerified,
            ["PreviousImageVerified"] = value.PreviousImageVerified, ["IntentVerified"] = value.IntentVerified,
            ["NativeRenameAttempted"] = value.NativeRenameAttempted, ["NativeRenameReturned"] = value.NativeRenameReturned,
            ["NativeError"] = value.NativeError, ["Error"] = value.Error?.ToString(),
        };
        private static Dictionary<string, object?> Describe(OwnedReservoirRecoveryInspection value) => new Dictionary<string, object?>
        {
            ["IntentValid"] = value.IntentValid, ["OperationId"] = value.OperationId?.ToString("D"), ["Target"] = value.Target.ToString(),
            ["Stage"] = value.Stage.ToString(), ["PreviousBackup"] = value.PreviousBackup.ToString(), ["PreviousByteLength"] = value.PreviousImage?.ByteLength,
            ["PreviousSha256"] = value.PreviousImage?.Sha256, ["CommitAttested"] = value.CommitAttested, ["NativeSavePermitted"] = value.NativeSavePermitted,
            ["Error"] = value.Error?.ToString(),
        };
        private static void Seed(string path, byte[] bytes)
        { using var stream = RecoverableFileData.Create(path); stream.Write(bytes, 0, bytes.Length); stream.Flush(true); RecoverableFileData.Verify(stream, bytes); }
        private static byte[] Read(string path)
        { using var stream = RecoverableFileData.OpenReader(path); return RecoverableFileData.ReadBounded(stream, OwnedReservoirFilePromotion.MaximumCompleteFileBytes); }
        private static string Snapshot(string directory)
        {
            var files = Directory.GetFiles(directory); Array.Sort(files, StringComparer.Ordinal); var result = new StringBuilder();
            foreach (var file in files) result.Append(file).Append('|').Append(new FileInfo(file).Length).Append('|')
                .Append(File.GetLastWriteTimeUtc(file).Ticks).Append('|').Append(RecoverableFileData.Hash(Read(file))).Append('\n');
            return result.ToString();
        }
        private static byte[] OldBytes() => Payload(16 * 1024, 13);
        private static byte[] NewBytes() => Payload(24 * 1024, 197);
        private static byte[] Payload(int length, int seed)
        { var value = new byte[length]; for (var i = 0; i < length; ++i) value[i] = (byte)((i * 37 + seed) ^ (i >> 5) ^ (i >> 11)); return value; }
        private static bool Same(byte[] a, byte[] b)
        { if (a.Length != b.Length) return false; for (var i = 0; i < a.Length; ++i) if (a[i] != b[i]) return false; return true; }
        private static void Equal(byte[] a, byte[] b, string message) => Check(Same(a, b), message);
        private static void Check(bool condition, string message) { if (!condition) throw new IOException(message); }

        private sealed class Context
        {
            internal string? Root;
            internal int Passed, Failed;
            internal readonly Dictionary<string, object?> Report = new Dictionary<string, object?>();
            internal readonly List<object?> Checks = new List<object?>();
            internal readonly List<object?> FixtureDirectories = new List<object?>();
            internal Dictionary<string, object?>? Active;
            internal void Metadata()
            {
                var assembly = typeof(GameMonoFileProtocolValidation).Assembly;
                Report["Schema"] = "ma-private-game-mono-file-protocol-v2";
                Report["HarnessRevision"] = 2;
                Report["FixtureDirectories"] = FixtureDirectories;
                Report["StartedUtc"] = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture);
                Report["Runtime"] = RuntimeInformation.FrameworkDescription; Report["EnvironmentVersion"] = Environment.Version.ToString();
                Report["MonoRuntimeDetected"] = Type.GetType("Mono.Runtime") != null;
                Report["OS"] = RuntimeInformation.OSDescription; Report["Architecture"] = RuntimeInformation.ProcessArchitecture.ToString(); Report["PointerBits"] = IntPtr.Size * 8;
                Report["AssemblyPath"] = assembly.Location;
                Report["AssemblyHashSelfMeasured"] = false;
                Report["AssemblySha256"] = null;
                if (!string.IsNullOrEmpty(assembly.Location))
                {
                    using var stream = new FileStream(assembly.Location, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
                    using var hash = SHA256.Create();
                    Report["AssemblySha256"] = BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
                    Report["AssemblyHashSelfMeasured"] = true;
                }
                Report["AssemblyHashBinding"] = string.IsNullOrEmpty(assembly.Location)
                    ? "In-memory load: caller must bind its independently SHA-verified input bytes to this report."
                    : "Measured default stream at Assembly.Location; caller must still bind the actual loaded input/package.";
                Report["CompiledHelperSourceSha256"] = new Dictionary<string, object?>
                {
                    ["OwnedReservoirFilePromotion.cs"] = "d0c2c87db64e211ec06095f80e87b36718836637bfe7e8a20405dea963e1e076",
                    ["OwnedReservoirRecoverableFilePromotion.cs"] = "a0810dd4d9b6511755c4c3f2bf4de1b6ac6d25454c9eaf52658f3a7055b7776a",
                    ["OwnedReservoirLocalRename.cs"] = "bd9811953257d321af64cfd1555cdc9a9c175a4faa802c0fa33faec92c40080f",
                };
                Report["Scope"] = "Private complete-default-stream fixtures on this runtime only; caller must bind returned report and assembly hash to actual ONI invocation.";
                Report["GameContextIndependentlyAttested"] = false; Report["NativeSavePermitted"] = false;
                Report["FullSaveLoadVerified"] = false; Report["PowerLossCrashDurabilityVerified"] = false;
                Report["GenericFilesystemAtomicityVerified"] = false; Report["TimedConcurrencyRun"] = false;
                Report["AutomaticRetryRollbackOrCleanup"] = false; Report["LinuxUnityVerified"] = false;
            }
            internal string Directory(string label)
            {
                string id;
                switch (label)
                {
                    case "new": id = "n"; break;
                    case "replacement": id = "r"; break;
                    case "equal": id = "e"; break;
                    case "zero-old": id = "z"; break;
                    case "unicode": id = "u"; break;
                    case "held-reader": id = "h"; break;
                    case "deny-delete-reader": id = "d"; break;
                    case "strict-inspection-source": id = "s"; break;
                    case "strict-checksum": id = "c"; break;
                    case "strict-checksummed-version": id = "v"; break;
                    case "strict-filename-identity": id = "f"; break;
                    case "strict-target-identity": id = "t"; break;
                    case "strict-cross-parent": id = "p"; break;
                    default: throw new InvalidOperationException("Unknown private fixture label.");
                }
                var path = Path.Combine(Root!, id);
                if (Path.Combine(path, ".ma-recover-" + new string('0', 32) + ".intent").Length > 240)
                    throw new IOException("Private case artifact path exceeds the harness-only budget.");
                if (System.IO.Directory.Exists(path) || File.Exists(path)) throw new IOException("Private fixture name already exists.");
                System.IO.Directory.CreateDirectory(path);
                FixtureDirectories.Add(new Dictionary<string, object?> { ["Id"] = id, ["Label"] = label, ["Path"] = path });
                return path;
            }
            internal void Case(string name, Action action)
            {
                Active = new Dictionary<string, object?> { ["Name"] = name };
                try { action(); Active["Passed"] = true; ++Passed; }
                catch (Exception error) { Active["Passed"] = false; Active["Error"] = error.ToString(); ++Failed; }
                Checks.Add(Active); Active = null;
            }
            internal void Failure(string name, Exception error)
            { ++Failed; Checks.Add(new Dictionary<string, object?> { ["Name"] = name, ["Passed"] = false, ["Error"] = error.ToString() }); }
            internal List<object?> Artifacts()
            {
                var files = System.IO.Directory.GetFiles(Root!, "*", SearchOption.AllDirectories); Array.Sort(files, StringComparer.Ordinal);
                var artifacts = new List<object?>();
                foreach (var path in files) artifacts.Add(new Dictionary<string, object?> { ["Path"] = path, ["ByteLength"] = new FileInfo(path).Length, ["Sha256"] = RecoverableFileData.Hash(Read(path)) });
                return artifacts;
            }
        }

        // Tiny original JSON writer for primitive report data: no serializer,
        // Unity/Core/game dependency or load-time package resolution is needed.
        private static string Json(object? value) { var builder = new StringBuilder(); AppendJson(builder, value); return builder.ToString(); }
        private static void AppendJson(StringBuilder builder, object? value)
        {
            if (value == null) { builder.Append("null"); return; }
            if (value is string text)
            {
                builder.Append('"');
                foreach (var ch in text)
                {
                    if (ch == '"' || ch == '\\') builder.Append('\\').Append(ch);
                    else if (ch < 0x20 || ch >= 0x7f) builder.Append("\\u").Append(((int)ch).ToString("x4", CultureInfo.InvariantCulture));
                    else builder.Append(ch);
                }
                builder.Append('"'); return;
            }
            if (value is bool boolean) { builder.Append(boolean ? "true" : "false"); return; }
            if (value is IDictionary<string, object?> map)
            {
                builder.Append('{'); var first = true;
                foreach (var pair in map) { if (!first) builder.Append(','); first = false; AppendJson(builder, pair.Key); builder.Append(':'); AppendJson(builder, pair.Value); }
                builder.Append('}'); return;
            }
            if (value is IEnumerable sequence)
            {
                builder.Append('['); var first = true;
                foreach (var item in sequence) { if (!first) builder.Append(','); first = false; AppendJson(builder, item); }
                builder.Append(']'); return;
            }
            if (value is int || value is long || value is uint || value is ulong)
            { builder.Append(Convert.ToString(value, CultureInfo.InvariantCulture)); return; }
            throw new InvalidOperationException("Unexpected private JSON report value type: " + value.GetType().FullName);
        }
    }
}
