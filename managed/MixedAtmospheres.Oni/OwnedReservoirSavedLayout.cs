using System;
using System.Buffers.Binary;
using System.IO;

namespace MixedAtmospheres.Oni
{
    internal sealed class SavedLayoutLimits
    {
        internal int MaximumSourceBytes { get; }
        internal int MaximumHeaderJsonBytes { get; }
        internal SavedInspectionLimits DirectoryAndObjects { get; }
        internal SavedInflationLimits Inflation { get; }
        internal SavedPrefixLimits Prefix { get; }

        internal SavedLayoutLimits(int maximumSourceBytes = 1 << 30, int maximumHeaderJsonBytes = 1 << 20,
            SavedInspectionLimits? directoryAndObjects = null, SavedInflationLimits? inflation = null, SavedPrefixLimits? prefix = null)
        {
            if (maximumSourceBytes <= 0 || maximumSourceBytes > 1 << 30)
                throw new ArgumentOutOfRangeException(nameof(maximumSourceBytes));
            if (maximumHeaderJsonBytes <= 0 || maximumHeaderJsonBytes > OwnedReservoirLoadHeaderV1.MaximumJsonBytes)
                throw new ArgumentOutOfRangeException(nameof(maximumHeaderJsonBytes));
            MaximumSourceBytes = maximumSourceBytes;
            MaximumHeaderJsonBytes = maximumHeaderJsonBytes;
            DirectoryAndObjects = directoryAndObjects ?? new SavedInspectionLimits();
            Inflation = inflation ?? new SavedInflationLimits();
            Prefix = prefix ?? new SavedPrefixLimits();
        }
    }

    internal sealed class SavedLayoutHeader
    {
        internal OwnedReservoirLoadHeaderInspection Inspection { get; }
        internal SavedByteRange Range { get; }
        internal uint OniBuild { get; }
        internal uint HeaderVersion { get; }
        internal int Compression { get; }
        internal int SaveMajorVersion { get; }
        internal int SaveMinorVersion { get; }
        internal Guid NativeColonyId { get; }

        internal SavedLayoutHeader(OwnedReservoirLoadHeaderInspection inspection, SavedByteRange range,
            uint build, uint version, int compression, int major, int minor, Guid colony)
        {
            Inspection = inspection; Range = range; OniBuild = build; HeaderVersion = version;
            Compression = compression; SaveMajorVersion = major; SaveMinorVersion = minor; NativeColonyId = colony;
        }
    }

    // These are structural associations between immutable images. The native
    // Sim, opaque/custom components and terminal body tail remain unvalidated.
    internal sealed class SavedColonyLayoutReport
    {
        internal SavedImage SourceImage { get; }
        internal SavedLayoutHeader Header { get; }
        internal SavedDirectory Directory { get; }
        internal SavedInflationReport Inflation { get; }
        internal SavedBodyPrefixReport Prefix { get; }
        internal SavedKsavReport Objects { get; }
        internal bool OuterFramingDerived => true;
        internal bool NativeReaderUsed => false;
        internal bool NativeSimInvoked => false;
        internal bool NativeSimValidityProven => false;
        internal bool WholeFilePreflightProven => false;
        internal bool GeneralDescendantCoverageProven => false;
        internal bool NativeLoadPermitted => false;
        internal bool OwnedRestorePermitted => false;
        internal bool WriterPermission => false;
        internal bool QuarantineActivated => false;
        internal bool FirstPlayableColonyPassed => false;

        internal SavedColonyLayoutReport(SavedImage source, SavedLayoutHeader header, SavedDirectory directory,
            SavedInflationReport inflation, SavedBodyPrefixReport prefix, SavedKsavReport objects)
        { SourceImage = source; Header = header; Directory = directory; Inflation = inflation; Prefix = prefix; Objects = objects; }
    }

    internal static class SavedColonyLayoutInspector
    {
        private static SavedLayoutHeader Header(SavedImage source, SavedLayoutLimits limits)
        {
            if (source.Length < 16) throw HeaderFailure(0, "Incomplete native header prefix");
            var prefix = source.CopySlice(0, 16);
            uint build = BinaryPrimitives.ReadUInt32LittleEndian(prefix);
            int jsonBytes = BinaryPrimitives.ReadInt32LittleEndian(prefix.AsSpan(4));
            uint version = BinaryPrimitives.ReadUInt32LittleEndian(prefix.AsSpan(8));
            int compression = BinaryPrimitives.ReadInt32LittleEndian(prefix.AsSpan(12));
            if (jsonBytes <= 0 || jsonBytes > source.Length - 16)
                throw HeaderFailure(4, "Invalid or truncated native header JSON length");
            if (jsonBytes > limits.MaximumHeaderJsonBytes)
                throw HeaderFailure(4, "Header JSON exceeds its limit", SavedInspectionFailureKind.LimitExceeded);
            if (build != 744825 || version != 1 || compression != 1)
                throw HeaderFailure(0, "Only the pinned build/v1 compressed layout is supported", SavedInspectionFailureKind.Unsupported);
            try
            {
                var facts = OwnedReservoirLoadHeaderV1.InspectLayoutHeader(source.CopySlice(0, 16 + jsonBytes));
                return new SavedLayoutHeader(facts.Inspection, new SavedByteRange(0, 16 + jsonBytes), facts.OniBuild,
                    facts.HeaderVersion, facts.Compression, facts.SaveMajorVersion, facts.SaveMinorVersion, facts.NativeColonyId);
            }
            catch (NotSupportedException error) { throw HeaderFailure(16, error.Message, SavedInspectionFailureKind.Unsupported); }
            catch (InvalidDataException error) { throw HeaderFailure(16, error.Message); }
        }

        private static SavedInspectionException HeaderFailure(int offset, string message,
            SavedInspectionFailureKind kind = SavedInspectionFailureKind.Malformed)
            => new SavedInspectionException(kind, offset, "header", message);

        internal static SavedColonyLayoutReport Inspect(SavedImage source, SavedLayoutLimits? limits = null)
        {
            if (source == null) throw new ArgumentNullException(nameof(source));
            limits ??= new SavedLayoutLimits();
            if (source.Length > limits.MaximumSourceBytes)
                throw new SavedInspectionException(SavedInspectionFailureKind.LimitExceeded, 0, "layout", "Source image exceeds its layout limit");
            var header = Header(source, limits);
            var directory = SavedDirectoryInspector.InspectPrefix(source, header.Range.End, limits.DirectoryAndObjects);
            int start = directory.Range.End;
            if (start >= source.Length)
                throw new SavedInspectionException(SavedInspectionFailureKind.Malformed, start, "layout", "Missing compressed body after the complete directory");
            var inflation = SavedZlibInflationInspector.Inspect(source, new SavedByteRange(start, source.Length - start), limits.Inflation);
            var prefix = SavedBodyPrefixInspector.Inspect(inflation.BodyImage, directory, limits.Prefix);
            var objects = SavedKsavInspector.Inspect(inflation.BodyImage, prefix.KsavOffset, directory, limits.DirectoryAndObjects);
            return new SavedColonyLayoutReport(source, header, directory, inflation, prefix, objects);
        }
    }
}
