using System;
using System.Buffers.Binary;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // Read the durable counter, never a potentially older publication status.
    // This is the existing owner framing; it does not interpret physical bytes.
    internal static class ColonyCheckpointMetadata
    {
        internal static ulong CompletedFrames(ReadOnlySpan<byte> image)
        {
            if (image.Length < 12 || !image.Slice(0,8).SequenceEqual(new byte[]{77,65,67,79,76,78,48,49}))
                throw new InvalidDataException("Missing colony checkpoint framing.");
            var version=BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(8,4));
            if(version<6 || version>9) throw new InvalidDataException("Unsupported colony checkpoint metadata version.");
            var at=12;
            Skip(image,ref at,56); // MAOniColonyConfig
            var materials=U32(image,ref at);
            Skip(image,ref at,(long)materials*168);
            var cells=U32(image,ref at);Skip(image,ref at,cells);
            var blocking=U32(image,ref at);Skip(image,ref at,blocking);
            var reservoir=U32(image,ref at);Skip(image,ref at,(long)reservoir*32);
            // Native long-double accounts are encoded as two binary64 terms.
            Skip(image,ref at,(version>=7?6L*materials+8:5L*materials+5)*16);
            if(version>=7) Skip(image,ref at,16); // trace counters
            var offset=at;Skip(image,ref at,8);
            return BinaryPrimitives.ReadUInt64LittleEndian(image.Slice(offset,8));
        }
        private static uint U32(ReadOnlySpan<byte> image,ref int at)
        {var offset=at;Skip(image,ref at,4);return BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(offset,4));}
        private static void Skip(ReadOnlySpan<byte> image,ref int at,long bytes)
        {
            if(bytes<0 || bytes>image.Length-at) throw new InvalidDataException("Truncated colony checkpoint metadata.");
            at+=checked((int)bytes);
        }
    }
}
