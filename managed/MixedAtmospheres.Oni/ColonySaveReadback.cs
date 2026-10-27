using System;
using System.Buffers.Binary;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // A write receipt only. It associates disk bytes with the exact body
    // produced by the ordinary serializer; it grants no native-load authority.
    internal sealed class ColonySaveReadback
    {
        internal string FileSha256 { get; }
        internal string BodySha256 { get; }
        internal int FileBytes { get; }
        internal int BodyBytes { get; }
        private ColonySaveReadback(SavedImage file, SavedImage body)
        {
            FileBytes=file.Length;BodyBytes=body.Length;
            FileSha256=file.HashSlice(0,file.Length);BodySha256=body.HashSlice(0,body.Length);
        }
        // The native (sim) section of a complete save file, or null when the
        // bytes are not a save this inspector reads. Reads bytes only; shared
        // by the load checks and tools/DistributionTests.
        internal static byte[]? SimSection(byte[] file)
        {
            try
            {
                var source = SavedImage.CopyFrom(file);
                var count = BinaryPrimitives.ReadInt32LittleEndian(source.CopySlice(4, 4));
                var header = OwnedReservoirLoadHeaderV1.InspectLayoutHeader(source.CopySlice(0, 16 + count), allowUncompressed: true);
                var directory = SavedDirectoryInspector.InspectPrefix(source, 16 + count, new SavedInspectionLimits());
                var start = directory.Range.End;
                var body = header.Compression == 1 ?
                    SavedZlibInflationInspector.Inspect(source, new SavedByteRange(start, source.Length - start), new SavedInflationLimits()).BodyImage :
                    SavedImage.CopyFrom(source.CopySlice(start, source.Length - start));
                var range = SavedBodyPrefixInspector.Inspect(body, directory, new SavedPrefixLimits()).SimPayload;
                var sim = body.CopySlice(range.Offset, range.Length);
                // The envelope may keep ONI's int32 length prefix.
                return sim.Length >= 4 && !Envelope(sim) && Envelope(sim.AsSpan(4)) ? sim.AsSpan(4).ToArray() : sim;
            }
            catch (Exception) { return null; }
        }

        private static bool Envelope(ReadOnlySpan<byte> data) => data.Length >= 8 && data.Slice(0, 8).SequenceEqual("MACOLONY"u8);

        internal static ColonySaveReadback Verify(byte[] written, int expectedBodyBytes, string expectedBodySha256,
            ReadOnlySpan<byte> expectedSim, Guid expectedColony,ReadOnlySpan<byte> expectedHeaderJson)
        {
            if(written==null) throw new ArgumentNullException(nameof(written));
            if(expectedBodyBytes<=0 || expectedBodyBytes>(256<<20) || expectedColony==Guid.Empty || expectedSim.IsEmpty || expectedHeaderJson.IsEmpty ||
                expectedBodySha256==null || expectedBodySha256.Length!=64)
                throw new ArgumentException("Complete serialized body, sim section and colony identity are required.");
            var source=SavedImage.CopyFrom(written);
            if(source.Length<16) throw new InvalidDataException("Incomplete saved colony header.");
            var count=BinaryPrimitives.ReadInt32LittleEndian(source.CopySlice(4,4));
            if(count<=0 || count>OwnedReservoirLoadHeaderV1.MaximumJsonBytes || count>source.Length-16)
                throw new InvalidDataException("Invalid saved colony header length.");
            if(count!=expectedHeaderJson.Length || !source.CopySlice(16,count).AsSpan().SequenceEqual(expectedHeaderJson))
                throw new InvalidDataException("Saved header differs from this completed ordinary serialization.");
            var header=OwnedReservoirLoadHeaderV1.InspectLayoutHeader(source.CopySlice(0,16+count),allowUncompressed:true);
            if(header.NativeColonyId!=expectedColony || header.Inspection.Kind!=OwnedReservoirLoadInputKind.Unmarked)
                throw new InvalidDataException("Saved colony identity differs from this serializer.");
            var directory=SavedDirectoryInspector.InspectPrefix(source,16+count,new SavedInspectionLimits());
            var start=directory.Range.End;
            if(start>=source.Length) throw new InvalidDataException("Saved colony has no body.");
            if(header.Compression==0 && source.Length-start!=expectedBodyBytes)
                throw new InvalidDataException("Uncompressed saved body has a different complete length.");
            var body=header.Compression==1 ?
                SavedZlibInflationInspector.Inspect(source,new SavedByteRange(start,source.Length-start),new SavedInflationLimits()).BodyImage :
                SavedImage.CopyFrom(source.CopySlice(start,source.Length-start));
            if(body.Length!=expectedBodyBytes || body.HashSlice(0,body.Length)!=expectedBodySha256)
                throw new InvalidDataException("Saved body differs from the completed ordinary serialization.");
            var prefix=SavedBodyPrefixInspector.Inspect(body,directory,new SavedPrefixLimits());
            var sim=prefix.SimPayload;
            if(sim.Length!=expectedSim.Length || !body.CopySlice(sim.Offset,sim.Length).AsSpan().SequenceEqual(expectedSim))
                throw new InvalidDataException("Saved sim section differs from its retained native/physical pair.");
            return new ColonySaveReadback(source,body);
        }
    }
}
