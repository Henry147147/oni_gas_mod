using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;

namespace MixedAtmospheres.Oni
{
    internal sealed class SavedInflationLimits
    {
        internal int MaximumImageBytes { get; }
        internal int MaximumCompressedBytes { get; }
        internal int MaximumOutputBytes { get; }
        internal int ChunkBytes { get; }
        internal int MaximumReadCalls { get; }
        internal int MaximumStructureBlocks { get; }
        internal int MaximumStructureSteps { get; }
        internal SavedInflationLimits(int maximumImageBytes = 1 << 30, int maximumCompressedBytes = 64 << 20,
            int maximumOutputBytes = 256 << 20, int chunkBytes = 65536, int maximumReadCalls = 65536,
            int maximumStructureBlocks = 65536, int maximumStructureSteps = 1 << 26)
        {
            MaximumImageBytes = Cap(maximumImageBytes, 1 << 30, nameof(maximumImageBytes));
            MaximumCompressedBytes = Cap(maximumCompressedBytes, 64 << 20, nameof(maximumCompressedBytes));
            MaximumOutputBytes = Cap(maximumOutputBytes, 256 << 20, nameof(maximumOutputBytes));
            ChunkBytes = Cap(chunkBytes, 65536, nameof(chunkBytes));
            MaximumReadCalls = Cap(maximumReadCalls, 65536, nameof(maximumReadCalls));
            MaximumStructureBlocks = Cap(maximumStructureBlocks, 65536, nameof(maximumStructureBlocks));
            MaximumStructureSteps = Cap(maximumStructureSteps, 1 << 26, nameof(maximumStructureSteps));
        }
        private static int Cap(int value, int maximum, string name)
        { if (value <= 0 || value > maximum) throw new ArgumentOutOfRangeException(name); return value; }
    }

    internal sealed class SavedInflationReport
    {
        internal SavedImage SourceImage { get; }
        internal SavedByteRange CompressedRange { get; }
        internal SavedImage BodyImage { get; }
        // This count comes from the strict complete-fragment structural walk,
        // not a claimed consumed-input counter on a read-ahead stream.
        internal int ValidatedCompressedBytes => CompressedRange.Length;
        internal long TotalBytesOut => BodyImage.Length;
        internal uint Adler32 { get; }
        internal int ReadCalls { get; }
        internal SavedDeflateStructureReport DeflateStructure { get; }
        internal bool DeflateStructureValidated => true;
        internal bool DeflateFinalBlockProven => true;
        internal bool ExactCompressedRangeValidated => true;
        internal bool DecoderEofObserved => true;
        internal bool DecoderSourcePositionUsedAsFramingProof => false;
        internal bool NativeReaderUsed => false;
        internal bool NativeSimInvoked => false;
        internal bool WholeFilePreflightProven => false;
        internal bool NativeSimValidityProven => false;
        internal bool NativeLoadPermitted => false;
        internal bool OwnedRestorePermitted => false;
        internal bool WriterPermission => false;
        internal bool QuarantineActivated => false;
        internal bool FirstPlayableColonyPassed => false;
        internal SavedInflationReport(SavedImage source, SavedByteRange range, SavedImage body,
            uint adler, int calls, SavedDeflateStructureReport structure)
        {
            SourceImage = source; CompressedRange = new SavedByteRange(range.Offset, range.Length);
            BodyImage = body; Adler32 = adler; ReadCalls = calls; DeflateStructure = structure;
        }
    }

    // Original bounded zlib framing/output adapter. RFC1951 syntax and exact
    // input closure come from the original walker, not stream Position/EOF.
    internal static class SavedZlibInflationInspector
    {
        internal const string Decoder = "System.IO.Compression.DeflateStream/raw";
        private static SavedInspectionException Failure(int offset, string reason,
            SavedInspectionFailureKind kind = SavedInspectionFailureKind.Malformed)
            => new SavedInspectionException(kind, Math.Max(0, offset), "zlib", reason);

        internal static SavedInflationReport Inspect(SavedImage source, SavedByteRange compressed,
            SavedInflationLimits? limits = null)
        {
            if (source == null) throw new ArgumentNullException(nameof(source));
            if (compressed == null) throw new ArgumentNullException(nameof(compressed));
            limits ??= new SavedInflationLimits();
            if (source.Length > limits.MaximumImageBytes)
                throw Failure(compressed.Offset, "Source image exceeds its limit", SavedInspectionFailureKind.LimitExceeded);
            if (compressed.Offset < 0 || compressed.Length < 0 ||
                (long)compressed.Offset + compressed.Length > source.Length)
                throw Failure(compressed.Offset, "Compressed range is outside the source image");
            if (compressed.Length > limits.MaximumCompressedBytes)
                throw Failure(compressed.Offset, "Compressed range exceeds its limit", SavedInspectionFailureKind.LimitExceeded);
            if (compressed.Length < 6) throw Failure(compressed.Offset, "Incomplete zlib envelope");
            byte cmf = source.ByteAt(compressed.Offset), flg = source.ByteAt(compressed.Offset + 1);
            if ((cmf & 15) != 8)
                throw Failure(compressed.Offset, "Unsupported zlib compression method", SavedInspectionFailureKind.Unsupported);
            if ((cmf >> 4) > 7) throw Failure(compressed.Offset, "Invalid zlib window declaration");
            if ((cmf * 256 + flg) % 31 != 0) throw Failure(compressed.Offset, "Invalid zlib header check");
            if (cmf != 0x78)
                throw Failure(compressed.Offset, "Only the 32-KiB zlib window is supported", SavedInspectionFailureKind.Unsupported);
            if ((flg & 32) != 0)
                throw Failure(compressed.Offset, "Preset dictionaries are unsupported", SavedInspectionFailureKind.Unsupported);

            byte[] input = source.CopySlice(compressed.Offset, compressed.Length);
            var structure = SavedDeflateStructureInspector.Inspect(input, compressed.Offset, limits);
            try
            {
                // Physically exclude the zlib header/trailer. Raw framing was
                // already proved by the complete structural walk above.
                using var raw = new MemoryStream(input, 2, input.Length - 6, false);
                using var decoder = new DeflateStream(raw, CompressionMode.Decompress, true);
                var buffer = new byte[limits.ChunkBytes];
                var pieces = new List<byte[]>();
                int total = 0, calls = 0;
                ulong sum1 = 1, sum2 = 0;
                while (true)
                {
                    if (calls == limits.MaximumReadCalls)
                        throw Failure(compressed.Offset, "Decoder Read call budget exceeded", SavedInspectionFailureKind.LimitExceeded);
                    int remaining = limits.MaximumOutputBytes - total;
                    int capacity = remaining == 0 ? 1 : Math.Min(buffer.Length, remaining);
                    calls++;
                    int produced = decoder.Read(buffer, 0, capacity);
                    if (produced < 0 || produced > capacity) throw Failure(compressed.Offset, "Decoder returned an invalid output count");
                    if (produced > remaining)
                        throw Failure(compressed.Offset, "Inflated body exceeds its limit", SavedInspectionFailureKind.LimitExceeded);
                    if (produced == 0)
                    {
                        if (total != structure.OutputBytes)
                            throw Failure(compressed.Offset, "Decoder output differs from the structural count");
                        uint computed = (uint)((sum2 << 16) | sum1);
                        int end = input.Length - 4;
                        uint declared = ((uint)input[end] << 24) | ((uint)input[end + 1] << 16) |
                            ((uint)input[end + 2] << 8) | input[end + 3];
                        if (computed != declared) throw Failure(compressed.Offset + end, "Independent Adler-32 check failed");
                        var body = new byte[total]; int at = 0;
                        foreach (byte[] piece in pieces) { Buffer.BlockCopy(piece, 0, body, at, piece.Length); at += piece.Length; }
                        if (at != total) throw Failure(compressed.Offset, "Inflated output assembly is inconsistent");
                        return new SavedInflationReport(source, compressed, SavedImage.CopyFrom(body), computed, calls, structure);
                    }
                    // Charge every partial read before allocating a retained
                    // piece. At most 64 KiB occurs between Adler reductions.
                    total += produced;
                    var saved = new byte[produced];
                    Buffer.BlockCopy(buffer, 0, saved, 0, produced); pieces.Add(saved);
                    foreach (byte value in saved) { sum1 += value; sum2 += sum1; }
                    sum1 %= 65521; sum2 %= 65521;
                }
            }
            catch (InvalidDataException error)
            { throw Failure(compressed.Offset, "Raw DEFLATE decoder rejected the stream: " + error.Message); }
            catch (IOException error)
            { throw Failure(compressed.Offset, "Raw DEFLATE decoder failed: " + error.Message); }
            catch (Exception error) when (error is PlatformNotSupportedException || error is DllNotFoundException ||
                error is EntryPointNotFoundException || error is BadImageFormatException || error is NotImplementedException)
            { throw Failure(compressed.Offset, "The platform DEFLATE decoder is unavailable: " + error.Message, SavedInspectionFailureKind.Unsupported); }
        }
    }
}
