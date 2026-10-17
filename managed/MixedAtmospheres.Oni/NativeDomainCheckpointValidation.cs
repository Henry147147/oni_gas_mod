using System;
using System.IO;
using System.Linq;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static partial class NativeSealedDomainValidation
    {
        private static bool CheckpointMode => ColonyRuntimeValidation.Settings?.Mode == "native-checkpoint-domain" || PairedCheckpointMode;
        private static byte[]? checkpointSource, checkpointExpected, checkpointActual;
        private static ulong checkpointSourceFrame, checkpointResumedFrame;
        private static double checkpointSourceTime, checkpointResumedTime;
        private static int checkpointAttempts;
        private static bool checkpointVerified;

        private static bool TryCapturePhysicalCheckpoint(out byte[] image)
        {
            image = Array.Empty<byte>();
            var code = CopyDomainSnapshot(latest.Session, latest.CompletedFrames, null, 0, out var size);
            if (code == 3) return false;
            Check(code); Require(size > 32 && size <= 1024U * 1024 * 1024, "Physical checkpoint length is invalid.");
            var next = new byte[checked((int)size)];
            code = CopyDomainSnapshot(latest.Session, latest.CompletedFrames, next, size, out var copied);
            if (code == 3) return false;
            Check(code); Require(copied == size, "Physical checkpoint size changed inside one completed frame.");
            image = next; return true;
        }
        private static byte[] ResumePhysicalCheckpoint(byte[] source, double seconds)
        {
            Check(AdvanceDomainSnapshot(source, (uint)source.Length, seconds, null, 0, out var size));
            Require(size > 32 && size <= 1024U * 1024 * 1024, "Resumed physical checkpoint length is invalid.");
            var result = new byte[checked((int)size)];
            Check(AdvanceDomainSnapshot(source, (uint)source.Length, seconds, result, size, out var copied));
            Require(copied == size, "Restored physical owner did not produce a complete checkpoint.");
            return result;
        }
        private static void TickCheckpointFixture()
        {
            if (checkpointVerified) return;
            if (checkpointExpected != null && latest.CompletedFrames > checkpointSourceFrame)
            {
                if (latest.CompletedFrames == checkpointSourceFrame + 1 && TryCapturePhysicalCheckpoint(out var actual))
                {
                    Require(actual.SequenceEqual(checkpointExpected), "Checkpoint continuation differs from the actual next native worker frame.");
                    checkpointActual = actual; checkpointResumedFrame = latest.CompletedFrames; checkpointResumedTime = latest.TimeSeconds;
                    checkpointVerified = true;
                    File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "native-physical-checkpoint.bin"), checkpointSource!);
                    File.WriteAllBytes(Path.Combine(ColonyRuntimeValidation.OutputDirectory, "native-physical-checkpoint-next.bin"), actual);
                    return;
                }
                // A skipped observation cannot certify the exact next frame.
                // Capture a fresh source without altering the simulation.
                checkpointExpected = null;
                Require(++checkpointAttempts < 8, "Could not observe the checkpoint's exact next completed native frame.");
            }
            if (checkpointExpected != null || !TryCapturePhysicalCheckpoint(out var source)) return;
            Require(ResumePhysicalCheckpoint(source, 0).SequenceEqual(source), "Physical checkpoint changed during validation/restore.");
            checkpointSource = source; checkpointSourceFrame = latest.CompletedFrames; checkpointSourceTime = latest.TimeSeconds;
            checkpointExpected = ResumePhysicalCheckpoint(source, latest.LastTimestepSeconds);
        }
        private static string CheckpointHash(byte[]? image)
        {
            if (image == null) return "";
            using (var hash = System.Security.Cryptography.SHA256.Create())
                return BitConverter.ToString(hash.ComputeHash(image)).Replace("-", "").ToLowerInvariant();
        }
        private static object CheckpointReport() => new
        {
            Verified = checkpointVerified, FormatVersion = checkpointSource == null ? 0 : System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(checkpointSource.AsSpan(8, 4)),
            ImageBytes = checkpointSource?.Length ?? 0,
            SourceFrame = checkpointSourceFrame, ResumedFrame = checkpointResumedFrame,
            SourcePhysicalTime = checkpointSourceTime, ResumedPhysicalTime = checkpointResumedTime,
            SourceSha256 = CheckpointHash(checkpointSource), ExpectedSha256 = CheckpointHash(checkpointExpected), ActualSha256 = CheckpointHash(checkpointActual),
            ObservationRetries = checkpointAttempts, OrdinaryGameSaveIntegrated = false
        };
    }
}
