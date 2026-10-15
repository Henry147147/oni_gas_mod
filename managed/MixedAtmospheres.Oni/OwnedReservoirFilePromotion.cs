using System;
using System.IO;
using System.Security.Cryptography;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirFilePromotionOutcome
    {
        NotPromoted,
        PromotedAndVerified,
        PromotionOutcomeUnknown
    }

    // Original complete-file data only. This object attests neither the file
    // format nor eligibility to save any live game or actor publication.
    internal sealed class OwnedReservoirFilePromotionImage
    {
        private readonly byte[] bytes;
        private readonly byte[] hash;
        internal string TargetPath { get; }
        internal int ByteLength => bytes.Length;
        internal string Sha256 { get; }

        internal OwnedReservoirFilePromotionImage(string targetPath, ReadOnlySpan<byte> source)
        {
            OwnedReservoirFilePromotion.ValidateCompleteFileLength(source.Length);
            TargetPath = OwnedReservoirFilePromotion.ValidateTargetPath(targetPath);
            bytes = source.ToArray();
            using var algorithm = SHA256.Create();
            hash = algorithm.ComputeHash(bytes);
            Sha256 = BitConverter.ToString(hash).Replace("-", string.Empty).ToLowerInvariant();
        }

        internal void Write(FileStream stream, int start, int count) => stream.Write(bytes, start, count);

        internal void Verify(FileStream stream)
        {
            if (!stream.CanSeek || stream.Length != bytes.Length)
                throw new InvalidDataException("Complete-file readback length differs from the immutable image.");
            stream.Position = 0;
            var buffer = new byte[Math.Min(64 * 1024, bytes.Length)];
            using var algorithm = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            var position = 0;
            while (position < bytes.Length)
            {
                var read = stream.Read(buffer, 0, Math.Min(buffer.Length, bytes.Length - position));
                if (read <= 0) throw new EndOfStreamException("Complete-file readback ended early.");
                for (var i = 0; i < read; ++i)
                    if (buffer[i] != bytes[position + i])
                        throw new InvalidDataException("Complete-file readback differs from the immutable image.");
                algorithm.AppendData(buffer, 0, read);
                position += read;
            }
            if (stream.ReadByte() != -1 || stream.Length != bytes.Length)
                throw new InvalidDataException("Complete-file readback contains trailing data.");
            var observed = algorithm.GetHashAndReset();
            if (observed.Length != hash.Length) throw new InvalidDataException("Complete-file readback hash length differs.");
            for (var i = 0; i < hash.Length; ++i)
                if (observed[i] != hash[i]) throw new InvalidDataException("Complete-file readback SHA256 differs.");
        }
    }

    internal sealed class OwnedReservoirFilePromotionResult
    {
        internal OwnedReservoirFilePromotionOutcome Outcome { get; }
        internal string TargetPath { get; }
        internal int ByteLength { get; }
        internal string Sha256 { get; }
        internal bool? ReplacingExistingTarget { get; }
        internal bool StageWasCreated { get; }
        internal bool StagingReadbackVerified { get; }
        internal bool PromotionAttempted { get; }
        // This invocation's successfully created name. After promotion starts
        // its continued existence/identity is not guaranteed by this result.
        internal string? CreatedStagePath { get; }
        internal Exception? Error { get; }
        internal Exception? StageCleanupError { get; }
        internal string? RetainedStagePath { get; }

        internal OwnedReservoirFilePromotionResult(OwnedReservoirFilePromotionImage image, OwnedReservoirFilePromotionOutcome outcome,
            bool? replacingExistingTarget, bool stageWasCreated, bool stagingReadbackVerified, bool promotionAttempted,
            string? createdStagePath, Exception? error, Exception? stageCleanupError, string? retainedStagePath)
        {
            TargetPath = image.TargetPath; ByteLength = image.ByteLength; Sha256 = image.Sha256;
            Outcome = outcome; ReplacingExistingTarget = replacingExistingTarget; StageWasCreated = stageWasCreated;
            StagingReadbackVerified = stagingReadbackVerified; PromotionAttempted = promotionAttempted;
            CreatedStagePath = createdStagePath;
            Error = error; StageCleanupError = stageCleanupError; RetainedStagePath = retainedStagePath;
        }
    }

#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
    // Closed host-only seam. No fault injection or arbitrary callback is
    // compiled into the ONI assembly.
    internal enum OwnedReservoirFilePromotionFault
    {
        None, StageNameCollision, AfterCreate, AfterPartialWrite, AfterFlush, CorruptStage, TruncateStage,
        AfterReadback, BeforePromotion, AfterPromotion, CorruptPromotedFile, CleanupFailure
    }
#endif

    // Bounded local file promotion only. File.Replace does not establish an
    // uninterrupted target pathname on the tested Windows filesystem, and
    // failed promotion cannot guarantee recovery of the previous target.
    // This helper must remain separate from live/native save permission.
    internal static class OwnedReservoirFilePromotion
    {
        internal const int MaximumCompleteFileBytes = 1024 * 1024 * 1024;
        internal const int MaximumTargetPathChars = 4096;
        internal const int MaximumFileNameChars = 240;
        internal const string StagingPrefix = ".ma-save-";
        internal const string StagingSuffix = ".stage";

        // Pure argument validation and a private byte copy only: no filesystem
        // reads, writes, caller callbacks, Core locks or game calls. The caller
        // must not mutate source while this synchronous copy is being made.
        internal static OwnedReservoirFilePromotionImage Prepare(string absoluteTargetPath, ReadOnlySpan<byte> completeFileBytes)
        {
            return new OwnedReservoirFilePromotionImage(absoluteTargetPath, completeFileBytes);
        }

        internal static OwnedReservoirFilePromotionResult Promote(OwnedReservoirFilePromotionImage image)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
            return PromoteCore(image, OwnedReservoirFilePromotionFault.None);
#else
            return PromoteCore(image);
#endif
        }

#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
        internal static OwnedReservoirFilePromotionResult PromoteForTest(OwnedReservoirFilePromotionImage image, OwnedReservoirFilePromotionFault fault)
        {
            if (image == null) throw new ArgumentNullException(nameof(image));
            if (!Enum.IsDefined(typeof(OwnedReservoirFilePromotionFault), fault)) throw new ArgumentOutOfRangeException(nameof(fault));
            return PromoteCore(image, fault);
        }
#endif

        private static OwnedReservoirFilePromotionResult PromoteCore(OwnedReservoirFilePromotionImage image
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
            , OwnedReservoirFilePromotionFault fault
#endif
            )
        {
            string? stage = null;
            bool? replacing = null;
            var created = false;
            var stageVerified = false;
            var promotionAttempted = false;
            var promotionReturned = false;
            Exception? failure = null;
            Exception? cleanupFailure = null;
            try
            {
                var parent = Path.GetDirectoryName(image.TargetPath)!;
                RequireDirectParentChain(parent);
                replacing = ExistingTarget(image.TargetPath);
                stage = Path.Combine(parent, StagingPrefix + Guid.NewGuid().ToString("N") + StagingSuffix);
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                if (fault == OwnedReservoirFilePromotionFault.StageNameCollision) stage = Path.Combine(parent, ".ma-save-host-collision.stage");
#endif
                using (var stream = new FileStream(stage, FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None, 64 * 1024, FileOptions.SequentialScan))
                {
                    created = true;
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                    Inject(fault, OwnedReservoirFilePromotionFault.AfterCreate);
#endif
                    var first = Math.Max(1, image.ByteLength / 2);
                    image.Write(stream, 0, first);
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                    Inject(fault, OwnedReservoirFilePromotionFault.AfterPartialWrite);
#endif
                    image.Write(stream, first, image.ByteLength - first);
                    stream.Flush(true);
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                    Inject(fault, OwnedReservoirFilePromotionFault.AfterFlush);
                    if (fault == OwnedReservoirFilePromotionFault.CorruptStage)
                    { stream.Position = 0; var original = stream.ReadByte(); stream.Position = 0; stream.WriteByte((byte)(original ^ 0xff)); stream.Flush(true); }
                    if (fault == OwnedReservoirFilePromotionFault.TruncateStage)
                    { stream.SetLength(image.ByteLength - 1); stream.Flush(true); }
#endif
                    image.Verify(stream);
                    stageVerified = true;
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                    Inject(fault, OwnedReservoirFilePromotionFault.AfterReadback);
                    Inject(fault, OwnedReservoirFilePromotionFault.CleanupFailure);
#endif
                }
                // The caller excludes concurrent writers and path redirection.
                // Recheck observable path state, but do not claim this closes
                // an OS-level pathname race or attests the underlying mount.
                RequireDirectParentChain(parent);
                if (ExistingTarget(image.TargetPath) != replacing.Value)
                    throw new IOException("Target existence changed while the complete file was staged.");
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                Inject(fault, OwnedReservoirFilePromotionFault.BeforePromotion);
#endif
                promotionAttempted = true;
                if (replacing.Value) File.Replace(stage, image.TargetPath, null, false);
                else File.Move(stage, image.TargetPath);
                promotionReturned = true;
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                Inject(fault, OwnedReservoirFilePromotionFault.AfterPromotion);
                if (fault == OwnedReservoirFilePromotionFault.CorruptPromotedFile)
                {
                    using var corrupted = new FileStream(image.TargetPath, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
                    var original = corrupted.ReadByte(); corrupted.Position = 0; corrupted.WriteByte((byte)(original ^ 0xff)); corrupted.Flush(true);
                }
#endif
                using (var promoted = new FileStream(image.TargetPath, FileMode.Open, FileAccess.Read, FileShare.Read)) image.Verify(promoted);
                return new OwnedReservoirFilePromotionResult(image, OwnedReservoirFilePromotionOutcome.PromotedAndVerified,
                    replacing, created, stageVerified, promotionAttempted, stage, null, null, null);
            }
            catch (Exception error) { failure = error; }

            // All our stream scopes have exited before cleanup. Never delete
            // the target, a backup, or a stage whose CreateNew did not succeed.
            // Once promotion starts, even a throwing OS operation may have
            // changed pathname identities. Retain all ambiguous artifacts for
            // explicit reconciliation; never delete a target or roll it back.
            if (created && !promotionAttempted)
            {
                try
                {
#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
                    Inject(fault, OwnedReservoirFilePromotionFault.CleanupFailure);
#endif
                    File.Delete(stage!);
                }
                catch (Exception error) { cleanupFailure = error; }
            }
            return new OwnedReservoirFilePromotionResult(image,
                promotionAttempted ? OwnedReservoirFilePromotionOutcome.PromotionOutcomeUnknown : OwnedReservoirFilePromotionOutcome.NotPromoted,
                replacing, created, stageVerified, promotionAttempted, created ? stage : null, failure, cleanupFailure,
                cleanupFailure != null || (created && promotionAttempted && !promotionReturned && File.Exists(stage)) ? stage : null);
        }

        internal static void ValidateCompleteFileLength(long length)
        {
            if (length <= 0 || length > MaximumCompleteFileBytes)
                throw new ArgumentOutOfRangeException(nameof(length), "A nonempty complete file of at most 1 GiB is required.");
        }

        internal static string ValidateTargetPath(string target)
        {
            if (target == null) throw new ArgumentNullException(nameof(target));
            if (target.Length == 0 || target.Length > MaximumTargetPathChars || target.Trim().Length == 0 || !Path.IsPathFullyQualified(target))
                throw new ArgumentException("An explicitly fully qualified bounded target file path is required.", nameof(target));
            var windows = Path.DirectorySeparatorChar == '\\';
            if (windows && (target.StartsWith("\\\\", StringComparison.Ordinal) || target.StartsWith("//", StringComparison.Ordinal)))
                throw new ArgumentException("UNC and device target paths are unsupported.", nameof(target));
            for (var i = 0; i < target.Length; ++i)
                if (char.IsControl(target[i])) throw new ArgumentException("Control characters are unsupported in a save path.", nameof(target));
            if (target[target.Length - 1] == Path.DirectorySeparatorChar || target[target.Length - 1] == Path.AltDirectorySeparatorChar)
                throw new ArgumentException("A target filename is required, not a trailing directory separator.", nameof(target));
            var root = Path.GetPathRoot(target)!;
            var parts = target.Substring(root.Length).Split(new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar }, StringSplitOptions.RemoveEmptyEntries);
            foreach (var part in parts)
            {
                if (part == "." || part == "..") throw new ArgumentException("Dot path components are unsupported.", nameof(target));
                if (windows)
                {
                    if (part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || part.EndsWith(".", StringComparison.Ordinal) || part.EndsWith(" ", StringComparison.Ordinal))
                        throw new ArgumentException("Invalid Windows filename component.", nameof(target));
                    var stem = part.Split('.')[0].TrimEnd(' ').ToUpperInvariant();
                    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$" ||
                        (stem.Length == 4 && (stem.StartsWith("COM", StringComparison.Ordinal) || stem.StartsWith("LPT", StringComparison.Ordinal)) &&
                            ((stem[3] >= '1' && stem[3] <= '9') || stem[3] == '\u00b9' || stem[3] == '\u00b2' || stem[3] == '\u00b3')))
                        throw new ArgumentException("Windows device filenames are unsupported.", nameof(target));
                }
            }
            var full = Path.GetFullPath(target);
            if (full.Length > MaximumTargetPathChars || Path.GetFileName(full).Length == 0 || Path.GetFileName(full).Length > MaximumFileNameChars)
                throw new ArgumentException("The normalized target filename or path exceeds its bound.", nameof(target));
            if (!windows && (UnderRoot(full, "/dev") || UnderRoot(full, "/proc") || UnderRoot(full, "/sys")))
                throw new ArgumentException("Known Unix device and virtual filesystem roots are unsupported.", nameof(target));
            return full;
        }

        private static bool UnderRoot(string path, string root) => path == root || path.StartsWith(root + "/", StringComparison.Ordinal);

        private static void RequireDirectParentChain(string parent)
        {
            for (var current = new DirectoryInfo(parent); current != null; current = current.Parent)
            {
                var attributes = File.GetAttributes(current.FullName);
                if ((attributes & FileAttributes.Directory) == 0) throw new IOException("A save parent is not a directory.");
                if ((attributes & (FileAttributes.ReparsePoint | FileAttributes.Device)) != 0)
                    throw new IOException("Reparse points, symbolic links and devices are unsupported save parents.");
            }
        }

        private static bool ExistingTarget(string path)
        {
            FileAttributes attributes;
            try { attributes = File.GetAttributes(path); }
            catch (FileNotFoundException) { return false; }
            if ((attributes & (FileAttributes.Directory | FileAttributes.ReparsePoint | FileAttributes.Device | FileAttributes.ReadOnly)) != 0)
                throw new IOException("An existing save target must be a writable regular file with no redirection.");
            return true;
        }

#if MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS
        private static void Inject(OwnedReservoirFilePromotionFault requested, OwnedReservoirFilePromotionFault current)
        {
            if (requested == current) throw new IOException("Injected host-only complete-file failure: " + current);
        }
#endif
    }
}
