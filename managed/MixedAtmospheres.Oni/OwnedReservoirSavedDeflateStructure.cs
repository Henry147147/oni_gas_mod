using System;

namespace MixedAtmospheres.Oni
{
    // Original structural validation of RFC1951. This walker counts output;
    // it neither reconstructs bytes nor relies on a codec's window contents.
    internal sealed class SavedDeflateStructureReport
    {
        internal int OutputBytes { get; }
        internal int Blocks { get; }
        internal int Steps { get; }
        internal int FinalCodeBitPosition { get; }
        internal SavedDeflateStructureReport(int outputBytes, int blocks, int steps, int finalBit)
        { OutputBytes = outputBytes; Blocks = blocks; Steps = steps; FinalCodeBitPosition = finalBit; }
    }

    internal static class SavedDeflateStructureInspector
    {
        private static readonly int[] LengthBase =
            { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        private static readonly int[] LengthExtra =
            { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        private static readonly int[] DistanceBase =
            { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        private static readonly int[] DistanceExtra =
            { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
        private static readonly int[] CodeLengthOrder =
            { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

        // Input is the adapter's private compressed copy. Header/trailer bytes
        // are physically outside the bit reader's DEFLATE interval.
        internal static SavedDeflateStructureReport Inspect(byte[] input, int sourceOffset, SavedInflationLimits limits)
        {
            if (input == null) throw new ArgumentNullException(nameof(input));
            if (limits == null) throw new ArgumentNullException(nameof(limits));
            if (sourceOffset < 0 || input.Length < 6 || input.Length > limits.MaximumCompressedBytes ||
                (long)sourceOffset + input.Length > int.MaxValue)
                throw new ArgumentOutOfRangeException(nameof(input));
            var bits = new BitReader(input, sourceOffset, limits);
            int output = 0, blocks = 0;
            Huffman? fixedLiterals = null, fixedDistances = null;
            bool final;
            do
            {
                if (blocks == limits.MaximumStructureBlocks) throw bits.Fail("DEFLATE block budget exceeded", true);
                blocks++; bits.Charge();
                final = bits.Read(1) != 0;
                int kind = bits.Read(2);
                if (kind == 0)
                {
                    bits.Align();
                    int length = bits.Read(16), complement = bits.Read(16);
                    if ((length ^ complement) != 65535) throw bits.Fail("Stored DEFLATE length complement differs");
                    bits.RequireBytes(length);
                    AddOutput(bits, limits, ref output, length);
                    bits.SkipBytes(length);
                }
                else if (kind == 1 || kind == 2)
                {
                    Huffman literals, distances;
                    if (kind == 1)
                    {
                        if (fixedLiterals == null)
                        {
                            var literalLengths = new int[288];
                            for (int i = 0; i < literalLengths.Length; i++)
                                literalLengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                            var distanceLengths = new int[32];
                            for (int i = 0; i < distanceLengths.Length; i++) distanceLengths[i] = 5;
                            fixedLiterals = new Huffman(bits, literalLengths, 15, false, false);
                            fixedDistances = new Huffman(bits, distanceLengths, 15, false, false);
                        }
                        literals = fixedLiterals; distances = fixedDistances!;
                    }
                    else ReadDynamic(bits, out literals, out distances);
                    while (true)
                    {
                        int symbol = literals.Decode(bits);
                        if (symbol < 256) { AddOutput(bits, limits, ref output, 1); continue; }
                        if (symbol == 256) break;
                        if (symbol > 285) throw bits.Fail("Reserved DEFLATE literal/length symbol");
                        int index = symbol - 257;
                        int length = LengthBase[index] + bits.Read(LengthExtra[index]);
                        int distanceCode = distances.Decode(bits);
                        if (distanceCode > 29) throw bits.Fail("Reserved DEFLATE distance symbol");
                        int distance = DistanceBase[distanceCode] + bits.Read(DistanceExtra[distanceCode]);
                        // Check the history BEFORE advancing output. A copy
                        // may overlap itself, but cannot invent earlier bytes.
                        if (distance > output || distance > 32768)
                            throw bits.Fail("DEFLATE distance precedes available output history");
                        AddOutput(bits, limits, ref output, length);
                    }
                }
                else throw bits.Fail("Reserved DEFLATE block kind");
            } while (!final);
            int finalCodeBit = bits.Position;
            bits.Align();
            if (!bits.AtEnd) throw bits.Fail("Bytes follow the final DEFLATE block before the zlib trailer");
            return new SavedDeflateStructureReport(output, blocks, bits.Steps, finalCodeBit);
        }

        private static void AddOutput(BitReader bits, SavedInflationLimits limits, ref int total, int count)
        {
            if (count > limits.MaximumOutputBytes - total)
                throw bits.Fail("Structurally counted inflated body exceeds its limit", true);
            total += count;
        }

        private static void ReadDynamic(BitReader bits, out Huffman literals, out Huffman distances)
        {
            int literalCount = 257 + bits.Read(5), distanceCount = 1 + bits.Read(5), codeCount = 4 + bits.Read(4);
            if (literalCount > 286) throw bits.Fail("Too many DEFLATE literal/length codes");
            var codeLengths = new int[19];
            for (int i = 0; i < codeCount; i++) codeLengths[CodeLengthOrder[i]] = bits.Read(3);
            var codeTree = new Huffman(bits, codeLengths, 7, false, false);
            // One shared length sequence: repeats may cross the two alphabets.
            var lengths = new int[literalCount + distanceCount];
            int next = 0;
            while (next < lengths.Length)
            {
                int symbol = codeTree.Decode(bits);
                if (symbol <= 15) { lengths[next++] = symbol; continue; }
                int value = 0, repeat;
                if (symbol == 16)
                {
                    if (next == 0) throw bits.Fail("DEFLATE repeat has no previous code length");
                    value = lengths[next - 1]; repeat = 3 + bits.Read(2);
                }
                else if (symbol == 17) repeat = 3 + bits.Read(3);
                else if (symbol == 18) repeat = 11 + bits.Read(7);
                else throw bits.Fail("Invalid DEFLATE code-length symbol");
                if (repeat > lengths.Length - next) throw bits.Fail("DEFLATE code-length repeat crosses its declared alphabet");
                for (int i = 0; i < repeat; i++) lengths[next++] = value;
            }
            if (lengths[256] == 0) throw bits.Fail("DEFLATE literal tree has no end-of-block code");
            var literalLengths = new int[literalCount]; var distanceLengths = new int[distanceCount];
            Array.Copy(lengths, 0, literalLengths, 0, literalCount);
            Array.Copy(lengths, literalCount, distanceLengths, 0, distanceCount);
            literals = new Huffman(bits, literalLengths, 15, false, true);
            distances = new Huffman(bits, distanceLengths, 15, true, true);
        }

        private sealed class Huffman
        {
            private readonly int[] counts = new int[16], firstCode = new int[16], firstIndex = new int[16];
            private readonly int[] symbols;
            private readonly int maximumLength;
            internal Huffman(BitReader bits, int[] lengths, int maximumBits, bool allowEmpty, bool allowSingle)
            {
                int used = 0;
                foreach (int length in lengths)
                {
                    if (length < 0 || length > maximumBits) throw bits.Fail("Invalid DEFLATE Huffman code length");
                    if (length == 0) continue;
                    counts[length]++; used++; maximumLength = Math.Max(maximumLength, length);
                }
                if (used == 0)
                {
                    if (!allowEmpty) throw bits.Fail("Empty DEFLATE Huffman alphabet");
                    symbols = Array.Empty<int>(); return;
                }
                // Each depth has twice its parent's unused code space.
                int free = 1;
                for (int depth = 1; depth <= maximumBits; depth++)
                {
                    free = 2 * free - counts[depth];
                    if (free < 0) throw bits.Fail("Oversubscribed DEFLATE Huffman alphabet");
                }
                if (free != 0 && !(allowSingle && used == 1 && maximumLength == 1))
                    throw bits.Fail("Incomplete DEFLATE Huffman alphabet");
                int code = 0, index = 0;
                for (int depth = 1; depth <= maximumLength; depth++)
                {
                    code = 2 * (code + counts[depth - 1]);
                    firstCode[depth] = code; firstIndex[depth] = index; index += counts[depth];
                }
                symbols = new int[used];
                var next = (int[])firstIndex.Clone();
                for (int symbol = 0; symbol < lengths.Length; symbol++)
                    if (lengths[symbol] != 0) symbols[next[lengths[symbol]]++] = symbol;
            }
            internal int Decode(BitReader bits)
            {
                bits.Charge(); int code = 0;
                for (int depth = 1; depth <= maximumLength; depth++)
                {
                    // Huffman codes travel most-significant bit first even
                    // though other DEFLATE fields travel least-significant first.
                    code = 2 * code + bits.Read(1);
                    int relative = code - firstCode[depth];
                    if (relative >= 0 && relative < counts[depth]) return symbols[firstIndex[depth] + relative];
                }
                throw bits.Fail("Unused or empty DEFLATE Huffman code");
            }
        }

        private sealed class BitReader
        {
            private readonly byte[] input;
            private readonly int sourceOffset, endBits, maximumSteps;
            internal int Position { get; private set; } = 16;
            internal int Steps { get; private set; }
            internal bool AtEnd => Position == endBits;
            internal BitReader(byte[] input, int sourceOffset, SavedInflationLimits limits)
            { this.input = input; this.sourceOffset = sourceOffset; endBits = (input.Length - 4) * 8; maximumSteps = limits.MaximumStructureSteps; }
            internal SavedInspectionException Fail(string message, bool limit = false)
                => new SavedInspectionException(limit ? SavedInspectionFailureKind.LimitExceeded : SavedInspectionFailureKind.Malformed,
                    sourceOffset + Position / 8, "zlib", message);
            internal void Charge()
            {
                if (Steps == maximumSteps) throw Fail("DEFLATE structural step budget exceeded", true);
                Steps++;
            }
            internal int Read(int count)
            {
                if (count < 0 || count > 16) throw new ArgumentOutOfRangeException(nameof(count));
                if (count > endBits - Position) throw Fail("Truncated DEFLATE bit field");
                int value = 0;
                for (int i = 0; i < count; i++, Position++) value |= ((input[Position / 8] >> (Position % 8)) & 1) << i;
                return value;
            }
            internal void Align() { Position = (Position + 7) & ~7; }
            internal void RequireBytes(int count)
            {
                if (count < 0 || (Position & 7) != 0 || count > (endBits - Position) / 8)
                    throw Fail("Truncated stored DEFLATE bytes");
            }
            internal void SkipBytes(int count) { RequireBytes(count); Position += count * 8; }
        }
    }
}
