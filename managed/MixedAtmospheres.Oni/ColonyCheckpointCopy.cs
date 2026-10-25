using System;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // A size query is not a reservation: queued input can settle before the
    // next native call. Each successful copy is one complete immutable image.
    internal static class ColonyCheckpointCopy
    {
        internal delegate int NativeCopy(byte[]? buffer, uint capacity, out uint bytes);
        private const int BufferTooSmall = 5;

        internal static byte[] Read(NativeCopy copy, Action<int> check)
        {
            check(copy(null, 0, out var required));
            for (var attempt = 0; attempt < 8; ++attempt)
            {
                if (required == 0 || required > int.MaxValue)
                    throw new InvalidDataException("Invalid colony checkpoint size.");
                var image = new byte[(int)required];
                var result = copy(image, required, out var written);
                if (result == BufferTooSmall && written > required)
                {
                    required = written;
                    continue;
                }
                check(result);
                if (written == 0 || written > required)
                    throw new InvalidDataException("Native colony checkpoint copy returned an invalid length.");
                if (written != required) Array.Resize(ref image, (int)written);
                return image;
            }
            throw new InvalidOperationException("Colony checkpoint size kept changing during capture.");
        }
    }
}
