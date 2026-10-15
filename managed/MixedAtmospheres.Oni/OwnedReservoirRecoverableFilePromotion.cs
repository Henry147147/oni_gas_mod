using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirRecoverableOutcome { NewFileVerified, ReplacementVerified, NotAttempted, OutcomeUnknown }
    internal enum OwnedReservoirTargetObservation { NotInspected, Missing, MatchesNew, MatchesPrevious, MatchesBoth, Other, Unreadable }
    internal enum OwnedReservoirArtifactObservation { NotApplicable, Missing, MatchesExpected, Other, Unreadable }

    // Unlike the nonempty candidate image, a previous complete default stream
    // can be empty. No externally mutable array is retained or returned.
    internal sealed class OwnedReservoirPreviousFileImage
    {
        private readonly byte[] bytes;
        internal int ByteLength => bytes.Length;
        internal string Sha256 { get; }
        internal OwnedReservoirPreviousFileImage(ReadOnlySpan<byte> source)
        {
            if (source.Length > OwnedReservoirFilePromotion.MaximumCompleteFileBytes) throw new ArgumentOutOfRangeException(nameof(source));
            bytes = source.ToArray(); Sha256 = RecoverableFileData.Hash(bytes);
        }
        internal byte[] CopyBytes() => (byte[])bytes.Clone();
        internal void Write(FileStream stream, int offset, int length) => stream.Write(bytes, offset, length);
        internal void Verify(FileStream stream) => RecoverableFileData.Verify(stream, bytes);
    }

    internal sealed class OwnedReservoirRecoverableResult
    {
        internal Guid OperationId { get; }
        internal OwnedReservoirRenameBackend? Backend { get; }
        internal OwnedReservoirRecoverableOutcome Outcome { get; }
        internal string TargetPath { get; }
        internal int NewByteLength { get; }
        internal string NewSha256 { get; }
        internal bool? PreviousTargetExisted { get; }
        internal OwnedReservoirPreviousFileImage? PreviousImage { get; }
        internal string? StagePath { get; }
        internal string? PreviousImagePath { get; }
        internal string? IntentPath { get; }
        internal IReadOnlyList<string> CreatedPaths { get; }
        internal bool StageVerified { get; }
        internal bool PreviousImageVerified { get; }
        internal bool IntentVerified { get; }
        internal bool NativeRenameAttempted { get; }
        internal bool NativeRenameReturned { get; }
        internal int? NativeError { get; }
        internal Exception? Error { get; }
        internal OwnedReservoirRecoverableResult(OwnedReservoirFilePromotionImage image, Guid operation,
            OwnedReservoirRenameBackend? backend, OwnedReservoirRecoverableOutcome outcome, bool? previousExisted,
            OwnedReservoirPreviousFileImage? previous, string? stage, string? backup, string? intent, IList<string> created,
            bool stageVerified, bool previousVerified, bool intentVerified, bool attempted, bool returned, int? nativeError, Exception? error)
        {
            OperationId = operation; Backend = backend; Outcome = outcome; TargetPath = image.TargetPath;
            NewByteLength = image.ByteLength; NewSha256 = image.Sha256; PreviousTargetExisted = previousExisted; PreviousImage = previous;
            StagePath = stage; PreviousImagePath = backup; IntentPath = intent;
            CreatedPaths = new ReadOnlyCollection<string>(new List<string>(created));
            StageVerified = stageVerified; PreviousImageVerified = previousVerified; IntentVerified = intentVerified;
            NativeRenameAttempted = attempted; NativeRenameReturned = returned; NativeError = nativeError; Error = error;
        }
    }

    internal sealed class OwnedReservoirRecoveryInspection
    {
        internal bool IntentValid { get; }
        internal Guid? OperationId { get; }
        internal OwnedReservoirRenameBackend? Backend { get; }
        internal OwnedReservoirTargetObservation Target { get; }
        internal OwnedReservoirArtifactObservation Stage { get; }
        internal OwnedReservoirArtifactObservation PreviousBackup { get; }
        internal OwnedReservoirPreviousFileImage? PreviousImage { get; }
        internal int? NewByteLength { get; }
        internal string? NewSha256 { get; }
        internal bool? PreviousTargetExisted { get; }
        internal string? StagePath { get; }
        internal string? PreviousImagePath { get; }
        internal Exception? Error { get; }
        // An intent records PREPARED, never attempted/committed. Observed new
        // bytes cannot retrospectively settle an unknown operation outcome.
        internal bool CommitAttested => false;
        internal bool NativeSavePermitted => false;
        internal OwnedReservoirRecoveryInspection(RecoverableFileIntent? intent, string? stage, string? backup,
            OwnedReservoirTargetObservation target, OwnedReservoirArtifactObservation staged, OwnedReservoirArtifactObservation previous,
            OwnedReservoirPreviousFileImage? previousImage, Exception? error)
        {
            IntentValid = intent != null; OperationId = intent?.Operation; Backend = intent?.Backend;
            Target = target; Stage = staged; PreviousBackup = previous; PreviousImage = previousImage; Error = error;
            NewByteLength = intent?.NewLength; NewSha256 = intent?.NewHash; PreviousTargetExisted = intent?.PreviousExisted;
            StagePath = stage; PreviousImagePath = backup;
        }
    }

#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
    // A closed standalone-host seam; absent from the ONI build. No arbitrary
    // callbacks or live-state delegates are accepted by production code.
    internal enum OwnedReservoirRecoverableFault
    {
        None, AfterOldCapture, AfterBackupCreate, AfterBackupPartialWrite, AfterBackupFlush, AfterBackupReadback,
        AfterStageCreate, AfterStagePartialWrite, AfterStageFlush, AfterStageReadback,
        AfterIntentCreate, AfterIntentPartialWrite, AfterIntentFlush, AfterIntentReadback,
        CorruptBackupBeforeAttempt, RemoveBackupBeforeAttempt, CorruptStageBeforeAttempt, RemoveStageBeforeAttempt,
        CorruptIntentBeforeAttempt, RemoveIntentBeforeAttempt, ChangeTargetBeforeAttempt, BeforeNativeRename,
        InvalidNativeRequest, AfterNativeRename, CorruptPromotedTarget, FailFinalVerification
    }
#endif

    // Original unhooked complete-file protocol. The caller owns a controlled
    // local namespace and excludes writers/redirection. This is neither a
    // live-save eligibility test nor a power-loss/metadata durability promise.
    internal static class OwnedReservoirRecoverableFilePromotion
    {
        internal const string ArtifactPrefix = ".ma-recover-";
        internal const int MaximumIntentBytes = 16 * 1024;

        internal static OwnedReservoirFilePromotionImage Prepare(string absoluteTarget, ReadOnlySpan<byte> completeNewBytes)
        {
            // Both operations are pure; malformed UTF16 must be rejected
            // before any path is passed to an OS API with replacement rules.
            var image = OwnedReservoirFilePromotion.Prepare(absoluteTarget, completeNewBytes);
            _ = RecoverableFileData.StrictUtf8.GetByteCount(image.TargetPath);
            RecoverableFileIntent.ValidateTargetName(Path.GetFileName(image.TargetPath));
            return image;
        }

        internal static OwnedReservoirRecoverableResult Promote(OwnedReservoirFilePromotionImage image)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
            return PromoteCore(image, OwnedReservoirRecoverableFault.None);
#else
            return PromoteCore(image);
#endif
        }

#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
        internal static OwnedReservoirRecoverableResult PromoteForTest(OwnedReservoirFilePromotionImage image, OwnedReservoirRecoverableFault fault)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
            if (!Enum.IsDefined(typeof(OwnedReservoirRecoverableFault), fault)) throw new ArgumentOutOfRangeException(nameof(fault));
            return PromoteCore(image, fault);
        }
#endif

        private static OwnedReservoirRecoverableResult PromoteCore(OwnedReservoirFilePromotionImage image
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
            , OwnedReservoirRecoverableFault fault
#endif
            )
        {
            var operation = Guid.NewGuid();
            OwnedReservoirRenameBackend? backend = null;
            bool? existed = null;
            OwnedReservoirPreviousFileImage? previous = null;
            string? stage = null, backup = null, intentPath = null;
            var created = new List<string>();
            var stageVerified = false; var previousVerified = false; var intentVerified = false;
            var attempted = false; var returned = false;
            int? nativeError = null;
            Exception? failure = null;
            FileStream? oldReader = null;
            try
            {
                // Promote also accepts the historical immutable image type;
                // repeat the strict encoding check before any IO in that case.
                _ = RecoverableFileData.StrictUtf8.GetByteCount(image.TargetPath);
                RecoverableFileIntent.ValidateTargetName(Path.GetFileName(image.TargetPath));
                var parent = Path.GetDirectoryName(image.TargetPath)!;
                RecoverableFileData.RequireParentChain(parent);
                backend = OwnedReservoirLocalRename.Select(parent);
                existed = RecoverableFileData.ExistsRegular(image.TargetPath, writable: true);
                if (existed.Value)
                {
                    oldReader = RecoverableFileData.OpenReader(image.TargetPath);
                    previous = new OwnedReservoirPreviousFileImage(RecoverableFileData.ReadBounded(oldReader, OwnedReservoirFilePromotion.MaximumCompleteFileBytes));
                }
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                Inject(fault, OwnedReservoirRecoverableFault.AfterOldCapture);
#endif
                var stem = ArtifactPrefix + operation.ToString("N");
                stage = Path.Combine(parent, stem + ".new");
                backup = previous == null ? null : Path.Combine(parent, stem + ".old");
                intentPath = Path.Combine(parent, stem + ".intent");
                if (previous != null)
                {
                    using (var writer = RecoverableFileData.Create(backup!))
                    {
                        created.Add(backup!);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                        Inject(fault, OwnedReservoirRecoverableFault.AfterBackupCreate);
#endif
                        var first = previous.ByteLength / 2;
                        previous.Write(writer, 0, first);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                        Inject(fault, OwnedReservoirRecoverableFault.AfterBackupPartialWrite);
#endif
                        previous.Write(writer, first, previous.ByteLength - first); writer.Flush(true);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                        Inject(fault, OwnedReservoirRecoverableFault.AfterBackupFlush);
#endif
                        previous.Verify(writer); previousVerified = true;
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                        Inject(fault, OwnedReservoirRecoverableFault.AfterBackupReadback);
#endif
                    }
                }
                using (var writer = RecoverableFileData.Create(stage))
                {
                    created.Add(stage);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterStageCreate);
#endif
                    var first = Math.Max(1, image.ByteLength / 2); image.Write(writer, 0, first);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterStagePartialWrite);
#endif
                    image.Write(writer, first, image.ByteLength - first); writer.Flush(true);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterStageFlush);
#endif
                    image.Verify(writer); stageVerified = true;
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterStageReadback);
#endif
                }
                var intent = new RecoverableFileIntent(operation, backend.Value, Path.GetFileName(image.TargetPath),
                    Path.GetFileName(stage), backup == null ? "" : Path.GetFileName(backup), image.ByteLength, image.Sha256,
                    previous != null, previous?.ByteLength ?? 0, previous?.Sha256 ?? RecoverableFileData.ZeroHash);
                var encodedIntent = intent.Encode();
                using (var writer = RecoverableFileData.Create(intentPath))
                {
                    created.Add(intentPath);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterIntentCreate);
#endif
                    var first = encodedIntent.Length / 2; writer.Write(encodedIntent, 0, first);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterIntentPartialWrite);
#endif
                    writer.Write(encodedIntent, first, encodedIntent.Length - first); writer.Flush(true);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterIntentFlush);
#endif
                    RecoverableFileData.Verify(writer, encodedIntent); intentVerified = true;
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                    Inject(fault, OwnedReservoirRecoverableFault.AfterIntentReadback);
#endif
                }
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                CorruptBeforeAttempt(fault, image.TargetPath, stage, backup, intentPath);
#endif
                // Close the controlled preattempt window by rechecking all
                // observable bytes/paths. This is not an OS pathname CAS.
                RecoverableFileData.RequireParentChain(parent);
                if (RecoverableFileData.ExistsRegular(image.TargetPath, writable: true) != existed.Value)
                    throw new IOException("Target existence changed before the native rename attempt.");
                if (previous != null)
                {
                    using (var current = RecoverableFileData.OpenReader(image.TargetPath)) previous.Verify(current);
                    using (var saved = RecoverableFileData.OpenReader(backup!)) previous.Verify(saved);
                }
                using (var staged = RecoverableFileData.OpenReader(stage)) image.Verify(staged);
                using (var prepared = RecoverableFileData.OpenReader(intentPath)) RecoverableFileData.Verify(prepared, encodedIntent);
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                Inject(fault, OwnedReservoirRecoverableFault.BeforeNativeRename);
#endif
                try
                {
                    OwnedReservoirLocalRename.Rename(backend.Value, stage, image.TargetPath, existed.Value
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                        , fault == OwnedReservoirRecoverableFault.InvalidNativeRequest
#endif
                        );
                    attempted = true; returned = true;
                }
                catch (OwnedReservoirRenameException error) { attempted = error.Attempted; nativeError = error.NativeError; throw; }
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                Inject(fault, OwnedReservoirRecoverableFault.AfterNativeRename);
                if (fault == OwnedReservoirRecoverableFault.CorruptPromotedTarget) Corrupt(image.TargetPath);
                Inject(fault, OwnedReservoirRecoverableFault.FailFinalVerification);
#endif
                using (var final = RecoverableFileData.OpenReader(image.TargetPath)) image.Verify(final);
                if (previous != null) using (var saved = RecoverableFileData.OpenReader(backup!)) previous.Verify(saved);
                using (var prepared = RecoverableFileData.OpenReader(intentPath)) RecoverableFileData.Verify(prepared, encodedIntent);
            }
            catch (Exception error) { failure = error; }
            finally
            {
                try { oldReader?.Dispose(); }
                catch (Exception error) { failure = failure == null ? error : new AggregateException(failure, error); }
            }
            // No deletion, rollback, replay, automatic cleanup or retry. Once
            // the native mutation entry point ran, every failure is Unknown.
            return new OwnedReservoirRecoverableResult(image, operation, backend,
                failure == null ? (existed!.Value ? OwnedReservoirRecoverableOutcome.ReplacementVerified : OwnedReservoirRecoverableOutcome.NewFileVerified) :
                    attempted ? OwnedReservoirRecoverableOutcome.OutcomeUnknown : OwnedReservoirRecoverableOutcome.NotAttempted,
                existed, previous, stage, backup, intentPath, created, stageVerified, previousVerified, intentVerified, attempted, returned, nativeError, failure);
        }

        internal static OwnedReservoirRecoveryInspection Inspect(string absoluteTarget, string absoluteIntent)
        {
            RecoverableFileIntent? intent = null;
            string? stage = null, backup = null;
            var identityValidated = false;
            try
            {
                var target = OwnedReservoirFilePromotion.ValidateTargetPath(absoluteTarget);
                var intentPath = OwnedReservoirFilePromotion.ValidateTargetPath(absoluteIntent);
                _ = RecoverableFileData.StrictUtf8.GetByteCount(target); _ = RecoverableFileData.StrictUtf8.GetByteCount(intentPath);
                var parent = Path.GetDirectoryName(target)!;
                if (!string.Equals(parent, Path.GetDirectoryName(intentPath), StringComparison.Ordinal))
                    throw new InvalidDataException("The selected intent must be in the exact target directory.");
                RecoverableFileData.RequireParentChain(parent);
                // Select rejects known unsupported mounts; it performs no
                // rename or platform-dependent recovery action.
                _ = OwnedReservoirLocalRename.Select(parent);
                using (var reader = RecoverableFileData.OpenReader(intentPath))
                    intent = RecoverableFileIntent.Decode(RecoverableFileData.ReadBounded(reader, MaximumIntentBytes));
                var expectedIntent = ArtifactPrefix + intent.Operation.ToString("N") + ".intent";
                if (!string.Equals(Path.GetFileName(intentPath), expectedIntent, StringComparison.Ordinal) ||
                    !string.Equals(Path.GetFileName(target), intent.TargetName, StringComparison.Ordinal))
                    throw new InvalidDataException("The selected intent filename or target name differs from its exact recorded identity.");
                identityValidated = true;
                stage = Path.Combine(parent, intent.StageName);
                backup = intent.PreviousExisted ? Path.Combine(parent, intent.BackupName) : null;
                var staged = Observe(stage, intent.NewLength, intent.NewHash, false, out _);
                var prior = backup == null ? OwnedReservoirArtifactObservation.NotApplicable :
                    Observe(backup, intent.PreviousLength, intent.PreviousHash, true, out _);
                OwnedReservoirPreviousFileImage? priorImage = null;
                if (backup != null && prior == OwnedReservoirArtifactObservation.MatchesExpected)
                {
                    // Capture once more and verify before publishing immutable
                    // recovery bytes; no stale status can approve another file.
                    using var reader = RecoverableFileData.OpenReader(backup);
                    var bytes = RecoverableFileData.ReadBounded(reader, OwnedReservoirFilePromotion.MaximumCompleteFileBytes);
                    if (bytes.Length != intent.PreviousLength || RecoverableFileData.Hash(bytes) != intent.PreviousHash)
                        throw new IOException("Previous backup changed during read-only capture.");
                    priorImage = new OwnedReservoirPreviousFileImage(bytes);
                }
                var targetState = ObserveTarget(target, intent);
                return new OwnedReservoirRecoveryInspection(intent, stage, backup, targetState, staged, prior, priorImage, null);
            }
            catch (Exception error)
            {
                // A syntactically decoded record is not valid until its exact
                // caller-selected path/target identity was also checked.
                return new OwnedReservoirRecoveryInspection(identityValidated ? intent : null, stage, backup, OwnedReservoirTargetObservation.NotInspected,
                    OwnedReservoirArtifactObservation.Unreadable, OwnedReservoirArtifactObservation.Unreadable, null, error);
            }
        }

        private static OwnedReservoirArtifactObservation Observe(string path, int length, string hash, bool allowEmpty, out Exception? failure)
        {
            failure = null;
            try
            {
                if (!RecoverableFileData.ExistsRegular(path, writable: false)) return OwnedReservoirArtifactObservation.Missing;
                using var stream = RecoverableFileData.OpenReader(path);
                if (stream.Length != length || (!allowEmpty && length == 0)) return OwnedReservoirArtifactObservation.Other;
                var bytes = RecoverableFileData.ReadBounded(stream, OwnedReservoirFilePromotion.MaximumCompleteFileBytes);
                return RecoverableFileData.Hash(bytes) == hash ? OwnedReservoirArtifactObservation.MatchesExpected : OwnedReservoirArtifactObservation.Other;
            }
            catch (Exception error) { failure = error; return OwnedReservoirArtifactObservation.Unreadable; }
        }

        private static OwnedReservoirTargetObservation ObserveTarget(string path, RecoverableFileIntent intent)
        {
            try
            {
                if (!RecoverableFileData.ExistsRegular(path, writable: false)) return OwnedReservoirTargetObservation.Missing;
                using var reader = RecoverableFileData.OpenReader(path);
                if (reader.Length != intent.NewLength && (!intent.PreviousExisted || reader.Length != intent.PreviousLength))
                    return OwnedReservoirTargetObservation.Other;
                var bytes = RecoverableFileData.ReadBounded(reader, OwnedReservoirFilePromotion.MaximumCompleteFileBytes);
                var hash = RecoverableFileData.Hash(bytes);
                var isNew = bytes.Length == intent.NewLength && hash == intent.NewHash;
                var isPrevious = intent.PreviousExisted && bytes.Length == intent.PreviousLength && hash == intent.PreviousHash;
                return isNew && isPrevious ? OwnedReservoirTargetObservation.MatchesBoth : isNew ? OwnedReservoirTargetObservation.MatchesNew :
                    isPrevious ? OwnedReservoirTargetObservation.MatchesPrevious : OwnedReservoirTargetObservation.Other;
            }
            catch { return OwnedReservoirTargetObservation.Unreadable; }
        }

#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
        private static void Inject(OwnedReservoirRecoverableFault requested, OwnedReservoirRecoverableFault point)
        { if (requested == point) throw new IOException("Injected standalone-host recoverable-file fault: " + point); }
        private static void Corrupt(string path)
        {
            using var writer = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            var first = writer.ReadByte(); writer.Position = 0; writer.WriteByte((byte)(first < 0 ? 0xa5 : first ^ 0xff)); writer.Flush(true);
        }
        private static void CorruptBeforeAttempt(OwnedReservoirRecoverableFault fault, string target, string stage, string? backup, string intent)
        {
            if (fault == OwnedReservoirRecoverableFault.CorruptBackupBeforeAttempt && backup != null) Corrupt(backup);
            if (fault == OwnedReservoirRecoverableFault.RemoveBackupBeforeAttempt && backup != null) File.Delete(backup);
            if (fault == OwnedReservoirRecoverableFault.CorruptStageBeforeAttempt) Corrupt(stage);
            if (fault == OwnedReservoirRecoverableFault.RemoveStageBeforeAttempt) File.Delete(stage);
            if (fault == OwnedReservoirRecoverableFault.CorruptIntentBeforeAttempt) Corrupt(intent);
            if (fault == OwnedReservoirRecoverableFault.RemoveIntentBeforeAttempt) File.Delete(intent);
            if (fault == OwnedReservoirRecoverableFault.ChangeTargetBeforeAttempt)
            { File.Delete(target); using var writer = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None); writer.WriteByte(0xe3); writer.Flush(true); }
        }
#endif
    }

    internal static class RecoverableFileData
    {
        internal static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);
        internal static string ZeroHash => new string('0', 64);
        internal static string Hash(byte[] bytes)
        { using var algorithm = SHA256.Create(); return BitConverter.ToString(algorithm.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }
        internal static FileStream Create(string path) => new FileStream(path, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None, 64 * 1024, FileOptions.SequentialScan);
        internal static FileStream OpenReader(string path)
        {
            if (!ExistsRegular(path, writable: false)) throw new FileNotFoundException("Expected regular complete file is absent.", path);
            return new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete, 64 * 1024, FileOptions.SequentialScan);
        }
        internal static byte[] ReadBounded(FileStream stream, int maximum)
        {
            if (!stream.CanSeek || stream.Length < 0 || stream.Length > maximum) throw new InvalidDataException("Complete-file length is outside its explicit bound.");
            var bytes = new byte[checked((int)stream.Length)]; stream.Position = 0;
            var position = 0;
            while (position < bytes.Length)
            { var count = stream.Read(bytes, position, bytes.Length - position); if (count == 0) throw new EndOfStreamException(); position += count; }
            if (stream.ReadByte() != -1 || stream.Length != bytes.Length) throw new InvalidDataException("Complete-file content changed while reading.");
            return bytes;
        }
        internal static void Verify(FileStream stream, byte[] expected)
        {
            if (!stream.CanSeek || stream.Length != expected.Length) throw new InvalidDataException("Readback length differs from the immutable file image.");
            stream.Position = 0;
            var buffer = new byte[Math.Max(1, Math.Min(64 * 1024, expected.Length))];
            using var observedHash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            var position = 0;
            while (position < expected.Length)
            {
                var count = stream.Read(buffer, 0, Math.Min(buffer.Length, expected.Length - position));
                if (count == 0) throw new EndOfStreamException();
                for (var i = 0; i < count; ++i) if (buffer[i] != expected[position + i]) throw new InvalidDataException("Readback bytes differ from the immutable file image.");
                observedHash.AppendData(buffer, 0, count); position += count;
            }
            if (stream.ReadByte() != -1 || stream.Length != expected.Length) throw new InvalidDataException("Readback has trailing or changed content.");
            var actual = BitConverter.ToString(observedHash.GetHashAndReset()).Replace("-", "").ToLowerInvariant();
            if (actual != Hash(expected)) throw new InvalidDataException("Readback SHA256 differs.");
        }
        internal static void RequireParentChain(string parent)
        {
            for (var current = new DirectoryInfo(parent); current != null; current = current.Parent)
            {
                var attributes = File.GetAttributes(current.FullName);
                if ((attributes & FileAttributes.Directory) == 0 || (attributes & (FileAttributes.ReparsePoint | FileAttributes.Device)) != 0)
                    throw new IOException("Every parent must be an existing direct directory without redirection or devices.");
            }
        }
        internal static bool ExistsRegular(string path, bool writable)
        {
            FileAttributes attributes;
            try { attributes = File.GetAttributes(path); }
            catch (FileNotFoundException) { return false; }
            var forbidden = FileAttributes.Directory | FileAttributes.ReparsePoint | FileAttributes.Device | FileAttributes.Offline;
            if (writable) forbidden |= FileAttributes.ReadOnly;
            if ((attributes & forbidden) != 0) throw new IOException("Expected a direct regular complete file with no unsupported attributes.");
            OwnedReservoirLocalRename.RequireRegularFileType(path);
            return true;
        }
    }

    internal sealed class RecoverableFileIntent
    {
        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MAFSPRM1");
        internal Guid Operation { get; }
        internal OwnedReservoirRenameBackend Backend { get; }
        internal string TargetName { get; }
        internal string StageName { get; }
        internal string BackupName { get; }
        internal int NewLength { get; }
        internal string NewHash { get; }
        internal bool PreviousExisted { get; }
        internal int PreviousLength { get; }
        internal string PreviousHash { get; }
        internal RecoverableFileIntent(Guid operation, OwnedReservoirRenameBackend backend, string target, string stage, string backup,
            int newLength, string newHash, bool previousExisted, int previousLength, string previousHash)
        {
            Operation = operation; Backend = backend; TargetName = target; StageName = stage; BackupName = backup;
            NewLength = newLength; NewHash = newHash; PreviousExisted = previousExisted; PreviousLength = previousLength; PreviousHash = previousHash;
            Validate();
        }
        private void Validate()
        {
            if (Operation == Guid.Empty || (Backend != OwnedReservoirRenameBackend.WindowsNtfsRenameInfoEx && Backend != OwnedReservoirRenameBackend.LinuxRenameAt2))
                throw new InvalidDataException("Unknown operation identity or recorded backend.");
            ValidateName(TargetName, false); ValidateName(StageName, false); ValidateName(BackupName, !PreviousExisted);
            var stem = OwnedReservoirRecoverableFilePromotion.ArtifactPrefix + Operation.ToString("N");
            if (StageName != stem + ".new" || BackupName != (PreviousExisted ? stem + ".old" : ""))
                throw new InvalidDataException("Recorded artifact names do not match the exact operation GUID.");
            OwnedReservoirFilePromotion.ValidateCompleteFileLength(NewLength);
            if (PreviousLength < 0 || PreviousLength > OwnedReservoirFilePromotion.MaximumCompleteFileBytes ||
                (!PreviousExisted && (PreviousLength != 0 || PreviousHash != RecoverableFileData.ZeroHash)))
                throw new InvalidDataException("Previous-file presence and length/hash disagree.");
            _ = ParseHash(NewHash); _ = ParseHash(PreviousHash);
            if (PreviousExisted && PreviousLength == 0 && PreviousHash != RecoverableFileData.Hash(Array.Empty<byte>()))
                throw new InvalidDataException("A present zero-byte previous file must have SHA256(empty).");
        }
        private static void ValidateName(string value, bool emptyAllowed)
        {
            if (emptyAllowed && value.Length == 0) return;
            if (value.Length == 0 || value.Length > OwnedReservoirFilePromotion.MaximumFileNameChars || value == "." || value == ".." ||
                value.IndexOf('/') >= 0 || value.IndexOf('\\') >= 0 || value.IndexOf(':') >= 0)
                throw new InvalidDataException("A bounded direct basename is required.");
            foreach (var ch in value) if (char.IsControl(ch)) throw new InvalidDataException("Control characters cannot occur in a basename.");
            if (RecoverableFileData.StrictUtf8.GetByteCount(value) > 960) throw new InvalidDataException("UTF8 basename exceeds its bound.");
        }
        internal static void ValidateTargetName(string value) => ValidateName(value, false);
        internal byte[] Encode()
        {
            using var stream = new MemoryStream();
            using var writer = new BinaryWriter(stream, RecoverableFileData.StrictUtf8, true);
            writer.Write(Magic); writer.Write(1); writer.Write(0L); writer.Write(Operation.ToByteArray());
            writer.Write((byte)Backend); writer.Write((byte)0); writer.Write((ushort)0); // reserved flags, three bytes
            WriteString(writer, TargetName); WriteString(writer, StageName); WriteString(writer, BackupName);
            writer.Write((long)NewLength); writer.Write(ParseHash(NewHash)); writer.Write(PreviousExisted ? (byte)1 : (byte)0);
            writer.Write((long)PreviousLength); writer.Write(ParseHash(PreviousHash)); writer.Flush();
            var total = checked(stream.Length + 32);
            if (total > OwnedReservoirRecoverableFilePromotion.MaximumIntentBytes) throw new InvalidDataException("Encoded intent exceeds its bound.");
            stream.Position = 12; writer.Write(total); writer.Flush();
            var body = stream.ToArray(); stream.Position = stream.Length; writer.Write(ParseHash(RecoverableFileData.Hash(body))); writer.Flush();
            return stream.ToArray();
        }
        internal static RecoverableFileIntent Decode(byte[] encoded)
        {
            if (encoded == null || encoded.Length < 8 + 4 + 8 + 16 + 4 + 12 + 8 + 32 + 1 + 8 + 32 + 32 ||
                encoded.Length > OwnedReservoirRecoverableFilePromotion.MaximumIntentBytes)
                throw new InvalidDataException("Intent complete length is outside its bound.");
            var body = new byte[encoded.Length - 32]; Array.Copy(encoded, body, body.Length);
            var recordedHash = new byte[32]; Array.Copy(encoded, body.Length, recordedHash, 0, 32);
            if (RecoverableFileData.Hash(body) != Hex(recordedHash)) throw new InvalidDataException("Intent checksum differs.");
            using var stream = new MemoryStream(body, false);
            using var reader = new BinaryReader(stream, RecoverableFileData.StrictUtf8, true);
            var magic = ReadExact(reader, 8);
            for (var i = 0; i < Magic.Length; ++i) if (magic[i] != Magic[i]) throw new InvalidDataException("Unknown intent magic.");
            if (reader.ReadInt32() != 1 || reader.ReadInt64() != encoded.LongLength) throw new InvalidDataException("Unknown version or inexact encoded length.");
            var operation = new Guid(ReadExact(reader, 16)); var backend = (OwnedReservoirRenameBackend)reader.ReadByte();
            if (reader.ReadByte() != 0 || reader.ReadUInt16() != 0) throw new InvalidDataException("Unknown intent flags.");
            var target = ReadString(reader); var stage = ReadString(reader); var backup = ReadString(reader);
            var newLength = reader.ReadInt64(); var newHash = Hex(ReadExact(reader, 32)); var oldPresent = reader.ReadByte();
            var oldLength = reader.ReadInt64(); var oldHash = Hex(ReadExact(reader, 32));
            if (oldPresent > 1 || newLength < 1 || newLength > OwnedReservoirFilePromotion.MaximumCompleteFileBytes ||
                oldLength < 0 || oldLength > OwnedReservoirFilePromotion.MaximumCompleteFileBytes || stream.Position != stream.Length)
                throw new InvalidDataException("Invalid presence flag, bounded file length or trailing intent content.");
            return new RecoverableFileIntent(operation, backend, target, stage, backup, (int)newLength, newHash, oldPresent == 1, (int)oldLength, oldHash);
        }
        private static void WriteString(BinaryWriter writer, string value)
        { var encoded = RecoverableFileData.StrictUtf8.GetBytes(value); writer.Write(encoded.Length); writer.Write(encoded); }
        private static string ReadString(BinaryReader reader)
        {
            var count = reader.ReadInt32();
            if (count < 0 || count > 960 || count > reader.BaseStream.Length - reader.BaseStream.Position) throw new InvalidDataException("Unbounded intent string length.");
            return RecoverableFileData.StrictUtf8.GetString(ReadExact(reader, count));
        }
        private static byte[] ReadExact(BinaryReader reader, int count)
        { var value = reader.ReadBytes(count); if (value.Length != count) throw new EndOfStreamException(); return value; }
        private static string Hex(byte[] value) => BitConverter.ToString(value).Replace("-", "").ToLowerInvariant();
        private static byte[] ParseHash(string value)
        {
            if (value == null || value.Length != 64) throw new InvalidDataException("A canonical SHA256 is required.");
            var bytes = new byte[32];
            for (var i = 0; i < bytes.Length; ++i)
            {
                var high = Digit(value[2 * i]); var low = Digit(value[2 * i + 1]);
                if (high < 0 || low < 0) throw new InvalidDataException("A lowercase hexadecimal SHA256 is required.");
                bytes[i] = (byte)((high << 4) | low);
            }
            return bytes;
        }
        private static int Digit(char ch) => ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
    }
}
