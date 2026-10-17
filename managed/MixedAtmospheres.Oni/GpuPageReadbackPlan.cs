using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static class GpuPageReadbackPlan
    {
        internal static int ReadY(int logicalY, int height, int textureHeight, bool reflected)
        {
            if (logicalY < 0 || height < 1 || (long)logicalY + height > textureHeight)
                throw new ArgumentException("GPU readback rectangle exceeds its texture.");
            return reflected ? textureHeight - logicalY - height : logicalY;
        }
        internal static int SourceIndex(int index, int width, int height, bool reflected)
        {
            if (width < 1 || height < 1 || index < 0 || index >= (long)width * height)
                throw new ArgumentException("GPU readback pixel exceeds its rectangle.");
            return reflected ? checked((height - 1 - index / width) * width + index % width) : index;
        }
        internal static bool SelectReflection(IReadOnlyList<int[]> expected, IReadOnlyList<int[]> direct, IReadOnlyList<int[]> reflected)
        {
            bool Same(IReadOnlyList<int[]> values) => values.Count == expected.Count &&
                Enumerable.Range(0, expected.Count).All(index => expected[index].SequenceEqual(values[index]));
            if (expected.Count == 0) throw new ArgumentException("GPU orientation requires a bounded reference pattern.");
            var identityMatches = Same(direct); var reflectionMatches = Same(reflected);
            if (identityMatches == reflectionMatches)
                throw new InvalidOperationException("Native phase/mass GPU orientation is ambiguous or differs from both coordinate controls.");
            return reflectionMatches;
        }
    }
}
