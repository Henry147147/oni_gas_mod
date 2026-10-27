using System;
using System.Buffers.Binary;
using System.IO;
using System.Security.Cryptography;

namespace MixedAtmospheres.Oni
{
    // Original mod-owned framing. This is deliberately NOT a SIMSAVE header:
    // an unpatched native loader must reject the container instead of loading
    // only the vacuum shadow. Hooking loaders/savers and atomic disk replacement
    // remain lifecycle responsibilities; this codec installs no patches.
    internal sealed class ColonySaveEnvelope
    {
        private static readonly byte[] Magic = { (byte)'M', (byte)'A', (byte)'C', (byte)'O', (byte)'L', (byte)'O', (byte)'N', (byte)'Y' };
        private static readonly byte[] NativeMagic = { (byte)'S', (byte)'I', (byte)'M', (byte)'S', (byte)'A', (byte)'V', (byte)'E', 0 };
        internal const int FormatVersion = 1;
        // The first game build Mixed Atmospheres saves exist for.
        internal const int FirstSupportedBuild = 744825;
        internal const int CatalogHashBytes = 32;
        internal const int MaximumPayloadBytes = 640 * 1024 * 1024;
        private const int HeaderBytes = 76;
        private const int ChecksumBytes = 32;
        private readonly byte[] bytes;
        private readonly int nativeLength, customLength;

        internal int OniBuild { get; }
        internal int MaterialModelVersion { get; }
        internal ulong CompletedFrame { get; }
        internal ReadOnlySpan<byte> MaterialCatalogHash => bytes.AsSpan(28, CatalogHashBytes);
        internal ReadOnlySpan<byte> NativeData => bytes.AsSpan(HeaderBytes, nativeLength);
        internal ReadOnlySpan<byte> CustomData => bytes.AsSpan(HeaderBytes + nativeLength, customLength);
        internal int EncodedLength => bytes.Length;

        private ColonySaveEnvelope(byte[] data, int nativeLength, int customLength)
        {
            bytes = data; this.nativeLength = nativeLength; this.customLength = customLength;
            OniBuild = BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(12));
            MaterialModelVersion = BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(16));
            CompletedFrame = BinaryPrimitives.ReadUInt64LittleEndian(data.AsSpan(20));
        }

        internal static bool HasMagic(ReadOnlySpan<byte> data) => data.Length >= Magic.Length && data.Slice(0, Magic.Length).SequenceEqual(Magic);

        internal static ColonySaveEnvelope Create(int oniBuild, int materialModelVersion, ulong completedFrame,
            ReadOnlySpan<byte> materialCatalogHash, ReadOnlySpan<byte> nativeData, ReadOnlySpan<byte> customData)
        {
            ValidateIdentity(oniBuild, materialModelVersion, materialCatalogHash);
            ValidateNativePayload(nativeData);
            if (customData.IsEmpty) throw new InvalidDataException("A mixture save requires complete custom state.");
            var length = checked((long)HeaderBytes + nativeData.Length + customData.Length + ChecksumBytes);
            if (length > MaximumPayloadBytes) throw new InvalidDataException("Colony save exceeds the envelope limit.");
            var data = new byte[(int)length];
            Magic.CopyTo(data, 0);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(8), FormatVersion);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(12), oniBuild);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(16), materialModelVersion);
            BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(20), completedFrame);
            materialCatalogHash.CopyTo(data.AsSpan(28, CatalogHashBytes));
            BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(60), nativeData.Length);
            BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(68), customData.Length);
            nativeData.CopyTo(data.AsSpan(HeaderBytes));
            customData.CopyTo(data.AsSpan(HeaderBytes + nativeData.Length));
            ComputeChecksum(data.AsSpan(0, data.Length - ChecksumBytes), data.AsSpan(data.Length - ChecksumBytes));
            return new ColonySaveEnvelope(data, nativeData.Length, customData.Length);
        }

        // All metadata, lengths, native framing and checksum pass before a
        // caller can receive either subpayload or publish custom state.
        internal static ColonySaveEnvelope Parse(ReadOnlySpan<byte> data, int expectedOniBuild,
            int expectedMaterialModelVersion, ReadOnlySpan<byte> expectedMaterialCatalogHash) =>
            Parse(data, expectedOniBuild, expectedOniBuild, expectedMaterialModelVersion, expectedMaterialCatalogHash);

        // A save written by any build from earliestBuild to the running
        // (latest) build: the envelope's own format, material model and
        // catalogue decide whether its contents can continue.
        internal static ColonySaveEnvelope Parse(ReadOnlySpan<byte> data, int earliestBuild, int latestBuild,
            int expectedMaterialModelVersion, ReadOnlySpan<byte> expectedMaterialCatalogHash)
        {
            ValidateIdentity(earliestBuild, expectedMaterialModelVersion, expectedMaterialCatalogHash);
            ValidateIdentity(latestBuild, expectedMaterialModelVersion, expectedMaterialCatalogHash);
            if (data.Length < HeaderBytes + ChecksumBytes || data.Length > MaximumPayloadBytes || !HasMagic(data))
                throw new InvalidDataException("Unrecognized or incomplete Mixed Atmospheres colony save.");
            if (BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8)) != FormatVersion)
                throw new NotSupportedException("Unsupported Mixed Atmospheres colony-save version.");
            var build = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12));
            if (build > latestBuild)
                throw new NotSupportedException($"This colony was saved by a newer game build ({build}) than this one ({latestBuild}).");
            if (build < earliestBuild)
                throw new NotSupportedException($"This colony was saved by game build {build}, which this Mixed Atmospheres version does not support.");
            if (BinaryPrimitives.ReadInt32LittleEndian(data.Slice(16)) != expectedMaterialModelVersion)
                throw new NotSupportedException("This colony was saved with a different Mixed Atmospheres material model.");
            if (!data.Slice(28, CatalogHashBytes).SequenceEqual(expectedMaterialCatalogHash))
                throw new NotSupportedException("This game's gases and liquids differ from the ones this colony was saved with, so its fluids cannot continue " +
                    "without a save migration that this Mixed Atmospheres version does not have.");
            var nativeLength = BinaryPrimitives.ReadInt64LittleEndian(data.Slice(60));
            var customLength = BinaryPrimitives.ReadInt64LittleEndian(data.Slice(68));
            // Compare lengths using subtraction, before any conversion/allocation
            // or addition that malicious lengths could overflow.
            var payloadLength = data.Length - HeaderBytes - ChecksumBytes;
            if (nativeLength <= 0 || nativeLength > payloadLength || customLength <= 0 || customLength != payloadLength - nativeLength)
                throw new InvalidDataException("Colony-save section lengths disagree with the complete container.");
            var checksum = new byte[ChecksumBytes];
            ComputeChecksum(data.Slice(0, data.Length - ChecksumBytes), checksum);
            if (!CryptographicOperations.FixedTimeEquals(checksum, data.Slice(data.Length - ChecksumBytes)))
                throw new InvalidDataException("Colony-save checksum does not match.");
            ValidateNativePayload(data.Slice(HeaderBytes, (int)nativeLength));
            return new ColonySaveEnvelope(data.ToArray(), (int)nativeLength, (int)customLength);
        }

        // Matches the existing Sim.Save/LoadWorld outer int32 framing exactly.
        internal void WriteLengthPrefixed(BinaryWriter writer)
        {
            if (writer == null) throw new ArgumentNullException(nameof(writer));
            writer.Write(bytes.Length); writer.Write(bytes);
        }

        internal static ColonySaveEnvelope ReadLengthPrefixed(BinaryReader reader, int expectedOniBuild,
            int expectedMaterialModelVersion, ReadOnlySpan<byte> expectedMaterialCatalogHash)
        {
            if (reader == null) throw new ArgumentNullException(nameof(reader));
            var length = reader.ReadInt32();
            if (length < HeaderBytes + ChecksumBytes || length > MaximumPayloadBytes)
                throw new InvalidDataException("Invalid colony-save length prefix.");
            var data = reader.ReadBytes(length);
            if (data.Length != length) throw new EndOfStreamException("Incomplete colony-save container.");
            return Parse(data, expectedOniBuild, expectedMaterialModelVersion, expectedMaterialCatalogHash);
        }

        internal byte[] CopyEncodedData() => (byte[])bytes.Clone();

        private static void ValidateIdentity(int oniBuild, int modelVersion, ReadOnlySpan<byte> catalogHash)
        {
            if (oniBuild <= 0 || modelVersion <= 0 || catalogHash.Length != CatalogHashBytes)
                throw new ArgumentException("A positive build/model version and SHA256 material-catalog identity are required.");
        }

        private static void ValidateNativePayload(ReadOnlySpan<byte> data)
        {
            // This envelope version supports only the inspected compact v15
            // native world. Exact material/guard/placement checks belong to the
            // existing NativeWorldBlob/NativeColonyImport staging APIs.
            if (data.Length < 29 || !data.Slice(0, NativeMagic.Length).SequenceEqual(NativeMagic) ||
                BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8)) != 15)
                throw new NotSupportedException("Expected an inspected native v15 SIMSAVE payload.");
            var width = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12));
            var height = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(16));
            if (width < 3 || height < 3 || (long)width * height > (MaximumPayloadBytes - 29L) / 36 ||
                data.Length != 29L + 36L * width * height)
                throw new InvalidDataException("Native save dimensions disagree with its complete payload.");
        }

        private static void ComputeChecksum(ReadOnlySpan<byte> content, Span<byte> output)
        {
            using var hash = SHA256.Create();
            if (!hash.TryComputeHash(content, output, out var written) || written != ChecksumBytes)
                throw new CryptographicException("Could not compute colony-save integrity checksum.");
        }
    }
}
