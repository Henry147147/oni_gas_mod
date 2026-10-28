using System;
using System.Threading;
using UnityEngine;
namespace MixedAtmospheres.Oni
{
    // Explicit private correctness run only; full readings and destination-byte reads
    // deliberately add cost. Never mix these samples into GC/speed acceptance.
    internal static partial class MixtureWorldRendering
    {
        private static readonly bool compactProof = Environment.GetEnvironmentVariable("MA_COLONY_COMPACT_TEXTURE_PROOF") == "1";
        private const int ProofLimitPerChannel = 2048;
        private static readonly long[] proofAttempts = new long[3], proofMatches = new long[3];
        private static long proofMismatches, proofForcedFull, proofFullJobs, proofMinFrame = long.MaxValue, proofMaxFrame;
        internal static object CompactProofReport() => new { Enabled = compactProof, ColorMatches = Interlocked.Read(ref proofMatches[0]), OpacityMatches = Interlocked.Read(ref proofMatches[1]), HazardMatches = Interlocked.Read(ref proofMatches[2]), Mismatches = Interlocked.Read(ref proofMismatches), ForcedFullBatches = Interlocked.Read(ref proofForcedFull), FullJobs = Interlocked.Read(ref proofFullJobs), MinimumFrame = Interlocked.Read(ref proofMinFrame) == long.MaxValue ? 0 : Interlocked.Read(ref proofMinFrame), MaximumFrame = Interlocked.Read(ref proofMaxFrame), Scope = "Separate private correctness; scalar/rich plus actual CPU texture bytes, not GPU readback or performance" };
        private static bool ProofAdmit(RuntimeMixturePresentationFrame frame, bool admitted)
        {
            if (compactProof && admitted && frame.Revision % 7 == 0)
            {
                Interlocked.Increment(ref proofForcedFull);
                return false;
            }
            return admitted;
        }
        private static void ProofFullJob()
        {
            if (compactProof)
                Interlocked.Increment(ref proofFullJobs);
        }
        private static void VerifyCompactWrite(int channel, TextureRegion region, int cell, int x, int y, RuntimeMixturePresentationFrame frame, OpacityParameters parameters, GasVisualSample compact)
        {
            if (!compactProof || frame.Revision < 5 || frame.Revision % 5 != 0 || (((uint)cell * 2654435761u + (uint)frame.Revision) & 15) != 0)
                return;
            if (Interlocked.Increment(ref proofAttempts[channel]) > ProofLimitPerChannel)
                return;
            if (!frame.TryGetCell(cell, out var rich) || rich == null)
                throw new InvalidOperationException("Private compact texture proof lacks its rich oracle.");
            var expected = Sample(rich, x, y, parameters);
            if (!TryByteOffset(region.x, region.y, region.targetWidth, region.targetHeight, region.pageWidth, region.bytesPerPixel, region.bytes.Length, x, y, channel == 0 ? 4 : 1, out var offset))
                throw new InvalidOperationException("Private compact texture proof cannot read its CPU destination.");
            Span<byte> actual = stackalloc byte[4];
            actual[0] = region.bytes[offset];
            if (channel == 0)
            {
                actual[1] = region.bytes[offset + 1];
                actual[2] = region.bytes[offset + 2];
                actual[3] = region.bytes[offset + 3];
            }
            VerifyCompactProofBytes(channel, expected, compact, actual.Slice(0, channel == 0 ? 4 : 1));
            Interlocked.Increment(ref proofMatches[channel]);
            UpdateMinimum(ref proofMinFrame, frame.Revision);
            UpdateMaximum(ref proofMaxFrame, frame.Revision);
        }
        internal static void VerifyCompactProofBytes(int channel, GasVisualSample expected, GasVisualSample compact, ReadOnlySpan<byte> destination)
        {
            var equal = expected.R == compact.R && expected.G == compact.G && expected.B == compact.B && expected.Alpha == compact.Alpha && expected.Opacity == compact.Opacity && expected.Hazard == compact.Hazard && BitConverter.DoubleToInt64Bits(expected.PhysicalPressurePa) == BitConverter.DoubleToInt64Bits(compact.PhysicalPressurePa);
            if (channel == 0)
                equal &= destination.Length == 4 && destination[0] == compact.R && destination[1] == compact.G && destination[2] == compact.B && destination[3] == compact.Alpha;
            else if (channel == 1)
                equal &= destination.Length == 1 && destination[0] == compact.Opacity;
            else if (channel == 2)
                equal &= destination.Length == 1 && destination[0] == compact.Hazard;
            else
                equal = false;
            if (!equal)
            {
                Interlocked.Increment(ref proofMismatches);
                throw new InvalidOperationException("Private compact texture bytes differ from the rich oracle or CPU destination.");
            }
        }
        private static void UpdateMinimum(ref long location, long value)
        {
            var before = Interlocked.Read(ref location);
            while (value < before)
            {
                var actual = Interlocked.CompareExchange(ref location, value, before);
                if (actual == before)
                    return;
                before = actual;
            }
        }
        private static void UpdateMaximum(ref long location, long value)
        {
            var before = Interlocked.Read(ref location);
            while (value > before)
            {
                var actual = Interlocked.CompareExchange(ref location, value, before);
                if (actual == before)
                    return;
                before = actual;
            }
        }
    }
}
