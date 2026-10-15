using System;
using System.Collections.Generic;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Original closed wire inspection only. No game types, native calls,
    // reflective deserialization, input mutation or load permission.
    internal sealed class SavedPrefixLimits
    {
        internal int MaximumImageBytes { get; }
        internal int MaximumPrefixBytes { get; }
        internal int MaximumStreamEntries { get; }
        internal int MaximumLabels { get; }
        internal int MaximumStreamValueBytes { get; }
        internal int MaximumStreamValueTotalBytes { get; }
        internal int MaximumStringBytes { get; }
        internal int MaximumDecodedStringBytes { get; }
        internal int MaximumSimBytes { get; }
        internal SavedPrefixLimits(int maximumImageBytes = 1 << 30, int maximumPrefixBytes = 64 << 20,
            int maximumStreamEntries = 4096, int maximumLabels = 4096, int maximumStreamValueBytes = 64 << 20,
            int maximumStreamValueTotalBytes = 64 << 20, int maximumStringBytes = 1 << 20,
            int maximumDecodedStringBytes = 16 << 20, int maximumSimBytes = 512 << 20)
        {
            MaximumImageBytes = Limit(maximumImageBytes, 1 << 30, nameof(maximumImageBytes));
            MaximumPrefixBytes = Limit(maximumPrefixBytes, 64 << 20, nameof(maximumPrefixBytes));
            MaximumStreamEntries = Limit(maximumStreamEntries, 4096, nameof(maximumStreamEntries));
            MaximumLabels = Limit(maximumLabels, 4096, nameof(maximumLabels));
            MaximumStreamValueBytes = Limit(maximumStreamValueBytes, 64 << 20, nameof(maximumStreamValueBytes));
            MaximumStreamValueTotalBytes = Limit(maximumStreamValueTotalBytes, 64 << 20, nameof(maximumStreamValueTotalBytes));
            MaximumStringBytes = Limit(maximumStringBytes, 1 << 20, nameof(maximumStringBytes));
            MaximumDecodedStringBytes = Limit(maximumDecodedStringBytes, 16 << 20, nameof(maximumDecodedStringBytes));
            MaximumSimBytes = Limit(maximumSimBytes, 512 << 20, nameof(maximumSimBytes));
        }
        private static int Limit(int value, int upper, string name)
        { if (value <= 0 || value > upper) throw new ArgumentOutOfRangeException(name); return value; }
    }

    internal sealed class SavedPrefixMemberRange
    {
        internal string TypeName { get; }
        internal string MemberName { get; }
        internal SavedByteRange Range { get; }
        internal SavedPrefixMemberRange(string type, string member, SavedByteRange range)
        { TypeName = type; MemberName = member; Range = range; }
    }

    internal sealed class SavedStreamedValue
    {
        internal string Key { get; }
        internal bool IsNull { get; }
        internal SavedByteRange EncodedValue { get; }
        internal SavedByteRange Payload { get; }
        internal SavedByteRange EncodedKey { get; }
        internal SavedStreamedValue(string key, bool isNull, SavedByteRange encoded, SavedByteRange payload, SavedByteRange encodedKey)
        { Key = key; IsNull = isNull; EncodedValue = encoded; Payload = payload; EncodedKey = encodedKey; }
    }

    internal sealed class SavedModLabel
    {
        internal int DistributionPlatform { get; }
        internal string? Id { get; }
        internal string? Title { get; }
        internal long Version { get; }
        internal SavedByteRange Range { get; }
        internal IReadOnlyList<SavedPrefixMemberRange> Members { get; }
        internal SavedModLabel(int platform, string? id, string? title, long version, SavedByteRange range, List<SavedPrefixMemberRange> members)
        {
            DistributionPlatform = platform; Id = id; Title = title; Version = version; Range = range;
            Members = Array.AsReadOnly(members.ToArray());
        }
    }

    internal sealed class SavedBodyPrefixReport
    {
        internal SavedImage Image { get; }
        internal SavedDirectory Directory { get; }
        internal SavedByteRange WorldMarker { get; }
        internal SavedByteRange SaveFileRootMarker { get; }
        internal SavedByteRange SaveFileRootMembers { get; }
        internal SavedByteRange SettingsMarker { get; }
        internal SavedByteRange SettingsMembers { get; }
        internal IReadOnlyList<SavedPrefixMemberRange> Members { get; }
        internal int WidthInCells { get; }
        internal int HeightInCells { get; }
        internal string? ClusterId { get; }
        internal int NextUniqueId { get; }
        internal int GameId { get; }
        internal IReadOnlyList<SavedStreamedValue> Streamed { get; }
        internal IReadOnlyList<SavedModLabel> ActiveMods { get; }
        internal SavedByteRange StreamedDictionary { get; }
        internal SavedByteRange RequiredMods { get; }
        internal SavedByteRange ActiveModsList { get; }
        internal SavedByteRange SimLengthField { get; }
        internal SavedByteRange SimPayload { get; }
        internal int KsavOffset => SimPayload.End;
        internal SavedByteRange KsavHeader { get; }
        internal bool BodyPrefixFramingComplete => true;
        internal bool NativeGridValidityProven => false;
        internal bool NativeSimValidityProven => false;
        internal bool WholeFilePreflightProven => false;
        internal bool GeneralDescendantCoverageProven => false;
        internal bool NativeLoadPermitted => false;
        internal bool OwnedRestorePermitted => false;
        internal bool WriterPermission => false;
        internal bool QuarantineActivated => false;
        internal SavedBodyPrefixReport(SavedImage image, SavedDirectory directory, SavedByteRange world, SavedByteRange rootMarker,
            SavedByteRange rootMembers, SavedByteRange settingsMarker, SavedByteRange settingsMembers,
            List<SavedPrefixMemberRange> members, int width, int height, string? cluster, int nextId, int gameId,
            List<SavedStreamedValue> streams, List<SavedModLabel> labels, SavedByteRange streamed, SavedByteRange required,
            SavedByteRange active, SavedByteRange simLength, SavedByteRange simPayload, SavedByteRange ksavHeader)
        {
            Image = image; Directory = directory; WorldMarker = world; SaveFileRootMarker = rootMarker; SaveFileRootMembers = rootMembers;
            SettingsMarker = settingsMarker; SettingsMembers = settingsMembers; Members = Array.AsReadOnly(members.ToArray());
            WidthInCells = width; HeightInCells = height; ClusterId = cluster; NextUniqueId = nextId; GameId = gameId;
            Streamed = Array.AsReadOnly(streams.ToArray()); ActiveMods = Array.AsReadOnly(labels.ToArray());
            StreamedDictionary = streamed; RequiredMods = required; ActiveModsList = active;
            SimLengthField = simLength; SimPayload = simPayload; KsavHeader = ksavHeader;
        }
    }

    internal static class SavedBodyPrefixInspector
    {
        internal static SavedBodyPrefixReport Inspect(SavedImage body, SavedDirectory directory, SavedPrefixLimits? limits = null)
        {
            if (body == null) throw new ArgumentNullException(nameof(body));
            if (directory == null) throw new ArgumentNullException(nameof(directory));
            limits ??= new SavedPrefixLimits();
            if (body.Length > limits.MaximumImageBytes)
                throw new SavedInspectionException(SavedInspectionFailureKind.LimitExceeded, 0, "body-prefix", "Image exceeds its prefix profile limit");
            return new Inspection(body, directory, limits).Run();
        }

        private sealed class Inspection
        {
            private const string RootType = "Klei.SaveFileRoot", SettingsType = "Game+Settings", LabelType = "KMod.Label";
            private readonly SavedImage image;
            private readonly SavedDirectory directory;
            private readonly SavedPrefixLimits limits;
            private readonly SavedBoundedCursor cursor;
            private int decodedStringBytes, streamValueBytes;
            private string context = "schema";
            internal Inspection(SavedImage body, SavedDirectory savedDirectory, SavedPrefixLimits profile)
            { image = body; directory = savedDirectory; limits = profile; cursor = new SavedBoundedCursor(body, 0, body.Length, "body-prefix"); }

            internal SavedBodyPrefixReport Run()
            {
                var rootTemplate = Template(RootType, "WidthInCells", "HeightInCells", "streamed", "clusterID", "requiredMods", "active_mods");
                var settingsTemplate = Template(SettingsType, "nextUniqueID", "gameID");
                var labelTemplate = Template(LabelType, "distribution_platform", "id", "title", "version");
                var world = Marker("world");
                var rootMarker = Marker(RootType);
                int rootStart = cursor.Position, width = 0, height = 0;
                string? cluster = null;
                var members = new List<SavedPrefixMemberRange>();
                var streams = new List<SavedStreamedValue>();
                var labels = new List<SavedModLabel>();
                SavedByteRange? streamedRange = null, requiredRange = null, activeRange = null;
                foreach (var member in rootTemplate.Members)
                {
                    context = RootType + "/" + member.Name; int start = cursor.Position;
                    switch (member.Name)
                    {
                        case "WidthInCells": width = Int32(); break;
                        case "HeightInCells": height = Int32(); break;
                        case "clusterID": cluster = String(true); break;
                        case "streamed": streams = Streamed(); streamedRange = Range(start); break;
                        case "requiredMods": RequiredNull(); requiredRange = Range(start); break;
                        case "active_mods": labels = Labels(labelTemplate); activeRange = Range(start); break;
                    }
                    members.Add(new SavedPrefixMemberRange(RootType, member.Name, Range(start)));
                }
                var rootMembers = Range(rootStart);
                var settingsMarker = Marker(SettingsType);
                int settingsStart = cursor.Position, nextId = 0, gameId = 0;
                foreach (var member in settingsTemplate.Members)
                {
                    context = SettingsType + "/" + member.Name; int start = cursor.Position;
                    if (member.Name == "nextUniqueID") nextId = Int32(); else gameId = Int32();
                    members.Add(new SavedPrefixMemberRange(SettingsType, member.Name, Range(start)));
                }
                var settingsMembers = Range(settingsStart);
                context = "inline-Sim-length"; int lengthStart = cursor.Position;
                int nativeLength = Int32();
                var lengthRange = Range(lengthStart);
                if (nativeLength < 0) throw Fail("Negative inline Sim byte count");
                if (nativeLength > limits.MaximumSimBytes) throw Fail("Inline Sim exceeds its byte budget", SavedInspectionFailureKind.LimitExceeded);
                if (cursor.Remaining < 16 || nativeLength > cursor.Remaining - 16) throw Fail("Inline Sim crosses the body or leaves no KSAV header");
                // Native blob bytes are excluded from the metadata budget and
                // remain opaque; no native load/validity check is performed.
                var native = cursor.Take(nativeLength);
                var ksavHeader = HeaderAtComputedOffset();
                return new SavedBodyPrefixReport(image, directory, world, rootMarker, rootMembers, settingsMarker, settingsMembers,
                    members, width, height, cluster, nextId, gameId, streams, labels, streamedRange!, requiredRange!, activeRange!,
                    lengthRange, native, ksavHeader);
            }

            private SavedTemplateDescriptor Template(string type, params string[] names)
            {
                if (!directory.Templates.TryGetValue(type, out var template) || template.TypeName != type || template.Members.Count != names.Length)
                    throw Fail("Missing or unsupported saved template " + type, SavedInspectionFailureKind.Unsupported);
                var seen = new HashSet<string>(StringComparer.Ordinal);
                foreach (var member in template.Members)
                {
                    bool expected = false;
                    foreach (var name in names) if (member.Name == name) expected = true;
                    if (member.IsProperty || !expected || !seen.Add(member.Name) || !MemberType(type, member))
                        throw Fail("Unsupported saved member " + type + "/" + member.Name, SavedInspectionFailureKind.Unsupported);
                }
                return template;
            }
            private static bool Primitive(SavedTypeDescriptor type, byte encoding) => type.Encoding == encoding && type.TypeName == null && type.Arguments.Count == 0;
            private static bool Named(SavedTypeDescriptor type, byte encoding, string name) => type.Encoding == encoding && type.TypeName == name && type.Arguments.Count == 0;
            private static bool ValueList(SavedTypeDescriptor type, string name) => type.Encoding == 148 && type.TypeName == null && type.Arguments.Count == 1 && Named(type.Arguments[0], 64, name);
            private static bool StreamDictionary(SavedTypeDescriptor type) => type.Encoding == 147 && type.TypeName == null && type.Arguments.Count == 2 &&
                Primitive(type.Arguments[0], 12) && type.Arguments[1].Encoding == 17 && type.Arguments[1].TypeName == null &&
                type.Arguments[1].Arguments.Count == 1 && Primitive(type.Arguments[1].Arguments[0], 2);
            private static bool MemberType(string owner, SavedMemberDescriptor member)
            {
                if (owner == SettingsType) return Primitive(member.Type, 6);
                if (owner == LabelType)
                {
                    if (member.Name == "distribution_platform") return Named(member.Type, 13, "KMod.Label+DistributionPlatform");
                    return Primitive(member.Type, member.Name == "version" ? (byte)8 : (byte)12);
                }
                switch (member.Name)
                {
                    case "WidthInCells": case "HeightInCells": return Primitive(member.Type, 6);
                    case "clusterID": return Primitive(member.Type, 12);
                    case "streamed": return StreamDictionary(member.Type);
                    case "requiredMods": return ValueList(member.Type, "ModInfo");
                    case "active_mods": return ValueList(member.Type, LabelType);
                    default: return false;
                }
            }

            private List<SavedStreamedValue> Streamed()
            {
                int claim = Int32(), count = Int32();
                Claim(claim);
                if (count == -1)
                {
                    if (claim != 0) throw Fail("Noncanonical null dictionary");
                    throw Fail("Null streamed dictionary is unsupported", SavedInspectionFailureKind.Unsupported);
                }
                if (count < 0) throw Fail("Invalid streamed dictionary count");
                Count(count, limits.MaximumStreamEntries, 12, claim, "streamed entries");
                int payloadStart = cursor.Position;
                var values = new List<(bool isNull, SavedByteRange encoded, SavedByteRange payload)>(count);
                for (int i = 0; i < count; i++)
                {
                    context = RootType + "/streamed/value[" + i + "]";
                    int start = cursor.Position, length = Int32(), bytes = Int32();
                    if (bytes == -1)
                    {
                        if (length != 0) throw Fail("Noncanonical null byte array");
                        values.Add((true, Range(start), new SavedByteRange(cursor.Position, 0))); continue;
                    }
                    if (length < 0 || bytes < 0 || length != bytes) throw Fail("Byte array claim differs from its count-derived consumption");
                    if (bytes > limits.MaximumStreamValueBytes || bytes > limits.MaximumStreamValueTotalBytes - streamValueBytes)
                        throw Fail("Streamed byte-array budget exceeded", SavedInspectionFailureKind.LimitExceeded);
                    NeedMetadata(bytes); streamValueBytes += bytes;
                    var payload = cursor.Take(bytes); values.Add((false, Range(start), payload));
                }
                var result = new List<SavedStreamedValue>(count);
                var keys = new HashSet<string>(StringComparer.Ordinal);
                for (int i = 0; i < count; i++)
                {
                    context = RootType + "/streamed/key[" + i + "]";
                    int start = cursor.Position;
                    string key = String(false)!;
                    if (!keys.Add(key)) throw Fail("Duplicate streamed dictionary key");
                    if (key == "Sim") throw Fail("Legacy streamed Sim is unsupported", SavedInspectionFailureKind.Unsupported);
                    var value = values[i]; result.Add(new SavedStreamedValue(key, value.isNull, value.encoded, value.payload, Range(start)));
                }
                Closure(payloadStart, claim, "streamed dictionary"); return result;
            }

            private void RequiredNull()
            {
                int claim = Int32(), count = Int32();
                if (claim < 0 || count < -1) throw Fail("Invalid requiredMods list header");
                if (count >= 0) throw Fail("Non-null legacy requiredMods is unsupported, including empty value lists", SavedInspectionFailureKind.Unsupported);
                if (claim != 0) throw Fail("Noncanonical null requiredMods list");
                // No ModInfo member template is requested for this null path.
            }

            private List<SavedModLabel> Labels(SavedTemplateDescriptor template)
            {
                int claim = Int32(), count = Int32(); Claim(claim);
                if (count == -1)
                {
                    if (claim != 0) throw Fail("Noncanonical null active_mods");
                    throw Fail("Null active_mods is unsupported", SavedInspectionFailureKind.Unsupported);
                }
                if (count < 0) throw Fail("Invalid active_mods count");
                Count(count, limits.MaximumLabels, 20, claim, "active_mods labels");
                int payloadStart = cursor.Position;
                var result = new List<SavedModLabel>(count);
                for (int i = 0; i < count; i++)
                {
                    int start = cursor.Position, platform = 0; long version = 0; string? id = null, title = null;
                    var members = new List<SavedPrefixMemberRange>(4);
                    foreach (var member in template.Members)
                    {
                        context = LabelType + "[" + i + "]/" + member.Name; int memberStart = cursor.Position;
                        switch (member.Name)
                        {
                            case "distribution_platform": platform = Int32(); break;
                            case "id": id = String(true); break;
                            case "title": title = String(true); break;
                            case "version": version = Int64(); break;
                        }
                        members.Add(new SavedPrefixMemberRange(LabelType, member.Name, Range(memberStart)));
                    }
                    result.Add(new SavedModLabel(platform, id, title, version, Range(start), members));
                }
                Closure(payloadStart, claim, "active_mods list"); return result;
            }

            private SavedByteRange Marker(string expected)
            {
                context = "marker/" + expected; int start = cursor.Position;
                if (String(false) != expected) throw Fail("Wrong typed/literal marker", SavedInspectionFailureKind.Unsupported);
                return Range(start);
            }
            private string? String(bool nullable)
            {
                int length = Int32();
                if (length == -1 && nullable) return null;
                if (length < 0) throw Fail("Invalid null or negative string length");
                if (length > limits.MaximumStringBytes || length > limits.MaximumDecodedStringBytes - decodedStringBytes)
                    throw Fail("Decoded string budget exceeded", SavedInspectionFailureKind.LimitExceeded);
                NeedMetadata(length);
                string value;
                try { value = image.DecodeString(cursor.Position, length); }
                catch (DecoderFallbackException) { throw Fail("Invalid UTF-8 string"); }
                decodedStringBytes += length; cursor.Skip(length); return value;
            }
            private int Int32() { NeedMetadata(4); return cursor.ReadInt32(); }
            private long Int64() { NeedMetadata(8); return cursor.ReadInt64(); }
            private SavedByteRange Range(int start) => new SavedByteRange(start, cursor.Position - start);
            private void NeedMetadata(int bytes)
            {
                if (bytes < 0 || bytes > cursor.Remaining) throw Fail("Truncated or crossing prefix record");
                if (cursor.Position > limits.MaximumPrefixBytes || bytes > limits.MaximumPrefixBytes - cursor.Position)
                    throw Fail("Body prefix byte budget exceeded", SavedInspectionFailureKind.LimitExceeded);
            }
            private void Claim(int length)
            {
                if (length < 0) throw Fail("Negative composite payload claim");
                NeedMetadata(length); // Additional rejection bound; never seeks.
            }
            private void Count(int count, int maximum, int minimum, int claimedBytes, string label)
            {
                if (count > maximum) throw Fail(label + " exceeds its count budget", SavedInspectionFailureKind.LimitExceeded);
                if (count > cursor.Remaining / minimum || count > claimedBytes / minimum) throw Fail(label + " cannot fit the enclosing bytes");
                if (cursor.Position > limits.MaximumPrefixBytes || count > (limits.MaximumPrefixBytes - cursor.Position) / minimum)
                    throw Fail(label + " cannot fit its prefix budget", SavedInspectionFailureKind.LimitExceeded);
            }
            private void Closure(int start, int claim, string label)
            { if ((long)cursor.Position - start != claim) throw Fail(label + " claim differs from actual consumed bytes"); }
            private SavedByteRange HeaderAtComputedOffset()
            {
                int offset = cursor.Position;
                var header = new SavedBoundedCursor(image, offset, 16, "body-prefix/derived-KSAV-header");
                if (header.ReadByte() != (byte)'K' || header.ReadByte() != (byte)'S' || header.ReadByte() != (byte)'A' || header.ReadByte() != (byte)'V')
                    throw new SavedInspectionException(SavedInspectionFailureKind.Malformed, offset, header.Path, "No KSAV header at the one computed offset");
                if (header.ReadInt32() != 7 || header.ReadInt32() != 38)
                    throw new SavedInspectionException(SavedInspectionFailureKind.Unsupported, offset, header.Path, "Unsupported KSAV version at the computed offset");
                if (header.ReadInt32() < 0) throw new SavedInspectionException(SavedInspectionFailureKind.Malformed, offset, header.Path, "Negative KSAV group count");
                return new SavedByteRange(offset, 16);
            }
            private SavedInspectionException Fail(string message, SavedInspectionFailureKind kind = SavedInspectionFailureKind.Malformed)
                => new SavedInspectionException(kind, cursor.Position, "body-prefix/" + context, message);
        }
    }
}
