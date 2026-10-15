using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    internal sealed class SavedComponentRecord
    {
        internal string TypeName { get; }
        internal int Ordinal { get; }
        internal SavedByteRange Payload { get; }
        internal SavedComponentRecord(string typeName, int ordinal, SavedByteRange payload)
        { TypeName = typeName; Ordinal = ordinal; Payload = payload; }
    }

    internal enum SavedMarkerState { Unassigned, Assigned, Invalid }

    internal sealed class SavedMarkerRecord
    {
        internal SavedMarkerState State { get; }
        internal string Error { get; }
        internal int SchemaVersion { get; }
        internal string? NativeColonyId { get; }
        internal string? PersistentOwnerToken { get; }
        internal string? CoreColonyId { get; }
        internal string? ContainerId { get; }
        internal long Generation { get; }
        internal string? AllocationToken { get; }
        internal SavedMarkerRecord(int version, string? native, string? owner, string? core, string? container, long generation, string? allocation)
        {
            SchemaVersion = version; NativeColonyId = native; PersistentOwnerToken = owner; CoreColonyId = core;
            ContainerId = container; Generation = generation; AllocationToken = allocation;
            if (version == 0 && native == "" && owner == "" && core == "" && container == "" && generation == 0 && allocation == "")
            { State = SavedMarkerState.Unassigned; Error = ""; return; }
            Error = "Unsupported or partially populated marker";
            if (version != 1 || !GuidText(native) || !GuidText(owner) || !GuidText(core) || !GuidText(allocation) ||
                string.IsNullOrWhiteSpace(container) || container!.Length > 256 || generation <= 0)
            { State = SavedMarkerState.Invalid; return; }
            string text = container!;
            foreach (char character in text) if (char.IsControl(character)) { State = SavedMarkerState.Invalid; return; }
            if (new System.Text.UTF8Encoding(false, true).GetByteCount(text) > 1024) { State = SavedMarkerState.Invalid; return; }
            State = SavedMarkerState.Assigned; Error = "";
        }
        private static bool GuidText(string? text) => text != null && text.Length == 32 && Guid.TryParseExact(text, "N", out var value) &&
            value != Guid.Empty && text == value.ToString("N");
    }

    internal sealed class SavedTagRecord
    {
        internal string? Name { get; }
        internal int Hash { get; }
        internal SavedTagRecord(string? name, int hash) { Name = name; Hash = hash; }
    }

    internal sealed class SavedPrefabIdentity
    {
        internal int InstanceId { get; }
        internal bool TagsAreNull { get; }
        internal IReadOnlyList<SavedTagRecord> Tags { get; }
        internal SavedPrefabIdentity(int id, bool tagsAreNull, List<SavedTagRecord> tags)
        { InstanceId = id; TagsAreNull = tagsAreNull; Tags = Array.AsReadOnly(tags.ToArray()); }
    }

    internal sealed class SavedStorageRecord
    {
        internal bool OnlyFetchMarkedItems { get; }
        internal bool ShouldSaveItems { get; }
        internal uint WorkTimeRemainingBits { get; }
        internal int NumberOfUses { get; }
        internal int PacketCount => Packets.Count;
        internal IReadOnlyList<SavedRootRecord> Packets { get; }
        internal SavedStorageRecord(bool marked, bool save, uint remaining, int uses, List<SavedRootRecord> packets)
        { OnlyFetchMarkedItems = marked; ShouldSaveItems = save; WorkTimeRemainingBits = remaining; NumberOfUses = uses; Packets = Array.AsReadOnly(packets.ToArray()); }
    }

    internal sealed class SavedRootRecord
    {
        internal string PrefabTag { get; }
        internal string Path { get; }
        internal int StorageDepth { get; }
        internal SavedByteRange Range { get; }
        internal IReadOnlyList<uint> TransformBits { get; }
        internal byte LegacyByte { get; }
        internal IReadOnlyList<SavedComponentRecord> Components { get; }
        internal IReadOnlyList<SavedMarkerRecord> Markers { get; }
        internal IReadOnlyList<SavedPrefabIdentity> PrefabIdentities { get; }
        internal IReadOnlyList<SavedStorageRecord> Storages { get; }
        internal SavedRootRecord(string tag, string path, int depth, SavedByteRange range, uint[] transform, byte legacy,
            List<SavedComponentRecord> components, List<SavedMarkerRecord> markers, List<SavedPrefabIdentity> identities, List<SavedStorageRecord> storages)
        {
            PrefabTag = tag; Path = path; StorageDepth = depth; Range = range;
            TransformBits = Array.AsReadOnly((uint[])transform.Clone()); LegacyByte = legacy;
            Components = Array.AsReadOnly(components.ToArray()); Markers = Array.AsReadOnly(markers.ToArray());
            PrefabIdentities = Array.AsReadOnly(identities.ToArray()); Storages = Array.AsReadOnly(storages.ToArray());
        }
    }

    internal sealed class SavedGroupRecord
    {
        internal string PrefabTag { get; }
        internal SavedByteRange Payload { get; }
        internal IReadOnlyList<SavedRootRecord> Roots { get; }
        internal SavedGroupRecord(string tag, SavedByteRange payload, List<SavedRootRecord> roots)
        { PrefabTag = tag; Payload = payload; Roots = Array.AsReadOnly(roots.ToArray()); }
    }

    internal sealed class SavedKsavReport
    {
        internal SavedImage Image { get; }
        internal SavedDirectory Directory { get; }
        internal SavedByteRange Section { get; }
        internal SavedByteRange UntouchedTail { get; }
        internal IReadOnlyList<SavedGroupRecord> Groups { get; }
        internal IReadOnlyList<SavedRootRecord> AllRecognizedRoots { get; }
        internal IReadOnlyList<string> SemanticIssues { get; }
        internal int OpaqueComponentCount { get; }
        internal bool FramingComplete => true;
        internal bool RecognizedStorageTraversalComplete => true;
        // Arbitrary custom/details payloads and source-to-section derivation
        // remain outside this inspector even when a particular image is empty.
        internal bool GeneralDescendantCoverageProven => false;
        internal bool WholeFilePreflightProven => false;
        internal bool OwnedRestorePermitted => false;
        internal SavedKsavReport(SavedImage image, SavedDirectory directory, SavedByteRange section,
            List<SavedGroupRecord> groups, List<SavedRootRecord> roots, List<string> issues, int opaque)
        {
            Image = image; Directory = directory; Section = section; UntouchedTail = new SavedByteRange(section.End, image.Length - section.End);
            Groups = Array.AsReadOnly(groups.ToArray()); AllRecognizedRoots = Array.AsReadOnly(roots.ToArray());
            SemanticIssues = Array.AsReadOnly(issues.ToArray()); OpaqueComponentCount = opaque;
        }
    }

    internal static class SavedKsavInspector
    {
        internal const string MarkerTypeName = "MixedAtmospheres.Oni.OwnedReservoirOwnerMarker";
        internal static SavedKsavReport Inspect(SavedImage body, int sectionOffset, SavedDirectory directory, SavedInspectionLimits? limits = null)
        {
            if (body == null) throw new ArgumentNullException(nameof(body));
            if (directory == null) throw new ArgumentNullException(nameof(directory));
            limits ??= new SavedInspectionLimits();
            if (body.Length > limits.MaximumImageBytes)
                throw new SavedInspectionException(SavedInspectionFailureKind.LimitExceeded, Math.Max(0, sectionOffset), "KSAV", "Image exceeds its limit");
            if (sectionOffset < 0 || sectionOffset > body.Length)
                throw new SavedInspectionException(SavedInspectionFailureKind.Malformed, Math.Max(0, sectionOffset), "KSAV", "Invalid section offset");
            return new Inspection(body, directory, limits).Run(sectionOffset);
        }

        private sealed class RootFrame
        {
            internal readonly string Tag, Path;
            internal readonly int Depth;
            internal readonly SavedByteRange Range;
            internal readonly uint[] Transform;
            internal readonly byte Legacy;
            internal readonly List<SavedComponentRecord> Components;
            internal RootFrame(string tag, string path, int depth, SavedByteRange range, uint[] transform, byte legacy, List<SavedComponentRecord> components)
            { Tag = tag; Path = path; Depth = depth; Range = range; Transform = transform; Legacy = legacy; Components = components; }
        }

        private sealed class Inspection
        {
            private readonly SavedImage image;
            private readonly SavedDirectory directory;
            private readonly SavedInspectionLimits limits;
            private int rootCount, componentCount, opaqueCount, tagCount;
            private readonly List<SavedRootRecord> allRoots = new List<SavedRootRecord>();
            private readonly List<string> issues = new List<string>();
            private readonly HashSet<int> ids = new HashSet<int>();
            private readonly HashSet<string> tokens = new HashSet<string>(StringComparer.Ordinal);
            private readonly HashSet<(string, string, long, string)> handles = new HashSet<(string, string, long, string)>();
            internal Inspection(SavedImage source, SavedDirectory savedDirectory, SavedInspectionLimits savedLimits)
            { image = source; directory = savedDirectory; limits = savedLimits; }

            internal SavedKsavReport Run(int offset)
            {
                var cursor = new SavedBoundedCursor(image, offset, image.Length - offset, "KSAV");
                if (cursor.ReadByte() != (byte)'K' || cursor.ReadByte() != (byte)'S' || cursor.ReadByte() != (byte)'A' || cursor.ReadByte() != (byte)'V')
                    throw cursor.Fail("Wrong KSAV magic");
                if (cursor.ReadInt32() != 7 || cursor.ReadInt32() != 38)
                    throw cursor.Fail("Only KSAV 7.38 is supported", SavedInspectionFailureKind.Unsupported);
                int groupsCount = cursor.Count(limits.MaximumGroups, 13, "group count");
                var frames = new List<(string tag, SavedByteRange range, List<RootFrame> roots)>();
                var groupNames = new HashSet<string>(StringComparer.Ordinal);
                // Complete all top-level component framing before interpreting
                // even a seemingly valid owner component.
                for (int i = 0; i < groupsCount; i++)
                {
                    string tag = cursor.String(limits.MaximumTypeNameBytes, true, true)!;
                    if (!groupNames.Add(tag)) throw cursor.Fail("Duplicate prefab group");
                    int count = cursor.Count(limits.MaximumRoots - rootCount, 0, "root count");
                    int bytes = cursor.ReadInt32();
                    var range = cursor.Take(bytes);
                    if (count > range.Length / 45) throw cursor.Fail("Roots cannot fit their group bytes");
                    var group = new SavedBoundedCursor(image, range.Offset, range.Length, "group[" + i + "]:" + tag);
                    var roots = new List<RootFrame>();
                    for (int j = 0; j < count; j++) roots.Add(ReadFrame(group, tag, group.Path + "/root[" + j + "]", 0));
                    group.RequireEnd(); frames.Add((tag, range, roots));
                }
                int end = cursor.Position;
                var groups = new List<SavedGroupRecord>();
                foreach (var group in frames)
                {
                    var roots = new List<SavedRootRecord>();
                    foreach (var root in group.roots) roots.Add(Decode(root));
                    groups.Add(new SavedGroupRecord(group.tag, group.range, roots));
                }
                return new SavedKsavReport(image, directory, new SavedByteRange(offset, end - offset), groups, allRoots, issues, opaqueCount);
            }

            private RootFrame ReadFrame(SavedBoundedCursor cursor, string tag, string path, int depth)
            {
                if (depth > limits.MaximumStorageDepth || rootCount >= limits.MaximumRoots)
                    throw cursor.Fail("Root/depth budget exceeded", SavedInspectionFailureKind.LimitExceeded);
                rootCount++;
                int start = cursor.Position;
                var bits = new uint[10];
                for (int i = 0; i < bits.Length; i++) bits[i] = cursor.ReadUInt32();
                byte legacy = cursor.ReadByte();
                int count = cursor.Count(Math.Min(limits.MaximumComponentsPerRoot, limits.MaximumComponents - componentCount), 9, "component count");
                componentCount += count;
                var components = new List<SavedComponentRecord>();
                for (int i = 0; i < count; i++)
                {
                    string name = cursor.String(limits.MaximumTypeNameBytes, true, true)!;
                    int length = cursor.ReadInt32();
                    components.Add(new SavedComponentRecord(name, i, cursor.Take(length)));
                }
                return new RootFrame(tag, path, depth, new SavedByteRange(start, cursor.Position - start), bits, legacy, components);
            }

            private SavedRootRecord Decode(RootFrame frame)
            {
                var markers = new List<SavedMarkerRecord>();
                var identities = new List<SavedPrefabIdentity>();
                var storages = new List<SavedStorageRecord>();
                foreach (var component in frame.Components)
                {
                    var cursor = new SavedBoundedCursor(image, component.Payload.Offset, component.Payload.Length,
                        frame.Path + "/component[" + component.Ordinal + "]:" + component.TypeName);
                    if (component.TypeName == MarkerTypeName) markers.Add(ReadMarker(cursor));
                    else if (component.TypeName == "KPrefabID") identities.Add(ReadIdentity(cursor));
                    else if (component.TypeName == "Storage") storages.Add(ReadStorage(cursor, frame.Depth));
                    else opaqueCount++;
                }
                foreach (var identity in identities)
                    if (!ids.Add(identity.InstanceId)) issues.Add(frame.Path + ": duplicate saved InstanceID " + identity.InstanceId);
                if (markers.Count > 0)
                {
                    if (markers.Count != 1 || identities.Count != 1 || storages.Count != 1)
                        issues.Add(frame.Path + ": marker root does not have exactly one marker, KPrefabID and Storage");
                    foreach (var marker in markers)
                    {
                        if (marker.State == SavedMarkerState.Invalid) issues.Add(frame.Path + ": invalid marker");
                        if (marker.State != SavedMarkerState.Assigned) continue;
                        if (frame.Tag != "GasReservoir" || frame.Depth != 0) issues.Add(frame.Path + ": assigned marker is outside a supported top-level GasReservoir");
                        if (!tokens.Add(marker.PersistentOwnerToken!)) issues.Add(frame.Path + ": duplicate persistent owner token");
                        if (!handles.Add((marker.CoreColonyId!, marker.ContainerId!, marker.Generation, marker.AllocationToken!)))
                            issues.Add(frame.Path + ": duplicate full Core handle");
                        if (identities.Count == 1 && (identities[0].InstanceId < 0 || identities[0].TagsAreNull)) issues.Add(frame.Path + ": unsupported assigned-owner KPrefabID");
                        foreach (var storage in storages) if (storage.PacketCount != 0) issues.Add(frame.Path + ": assigned owner has native Storage packets");
                    }
                }
                var result = new SavedRootRecord(frame.Tag, frame.Path, frame.Depth, frame.Range, frame.Transform, frame.Legacy,
                    frame.Components, markers, identities, storages);
                allRoots.Add(result);
                return result;
            }

            private IReadOnlyList<SavedMemberDescriptor> Layout(SavedBoundedCursor cursor, string type, params (string name, byte encoding)[] expected)
            {
                if (!directory.Templates.TryGetValue(type, out var template)) throw cursor.Fail("Missing saved template for " + type, SavedInspectionFailureKind.Unsupported);
                if (template.Members.Count != expected.Length) throw cursor.Fail("Unsupported saved member set for " + type, SavedInspectionFailureKind.Unsupported);
                foreach (var member in template.Members)
                {
                    bool match = false;
                    foreach (var value in expected) if (member.Name == value.name && member.Type.Encoding == value.encoding) match = true;
                    if (!match || member.IsProperty) throw cursor.Fail("Unsupported saved member descriptor for " + type, SavedInspectionFailureKind.Unsupported);
                }
                return template.Members;
            }

            private SavedMarkerRecord ReadMarker(SavedBoundedCursor cursor)
            {
                var members = Layout(cursor, MarkerTypeName, ("schemaVersion", 6), ("nativeColonyId", 12), ("persistentOwnerToken", 12),
                    ("coreColonyId", 12), ("containerId", 12), ("generation", 8), ("allocationToken", 12));
                int version = 0; long generation = 0;
                string? native = null, owner = null, core = null, container = null, allocation = null;
                foreach (var member in members)
                {
                    switch (member.Name)
                    {
                        case "schemaVersion": version = cursor.ReadInt32(); break;
                        case "generation": generation = cursor.ReadInt64(); break;
                        case "nativeColonyId": native = cursor.String(limits.MaximumValueStringBytes, false, false); break;
                        case "persistentOwnerToken": owner = cursor.String(limits.MaximumValueStringBytes, false, false); break;
                        case "coreColonyId": core = cursor.String(limits.MaximumValueStringBytes, false, false); break;
                        case "containerId": container = cursor.String(limits.MaximumValueStringBytes, false, false); break;
                        case "allocationToken": allocation = cursor.String(limits.MaximumValueStringBytes, false, false); break;
                    }
                }
                cursor.RequireEnd();
                return new SavedMarkerRecord(version, native, owner, core, container, generation, allocation);
            }

            private SavedPrefabIdentity ReadIdentity(SavedBoundedCursor cursor)
            {
                var members = Layout(cursor, "KPrefabID", ("InstanceID", 6), ("serializedTags", 149));
                int id = -1; bool nullTags = false;
                var tags = new List<SavedTagRecord>();
                foreach (var member in members)
                {
                    if (member.Name == "InstanceID") { id = cursor.ReadInt32(); continue; }
                    var type = member.Type;
                    if (type.Arguments.Count != 1 || type.Arguments[0].Encoding != 64 || type.Arguments[0].TypeName != "Tag")
                        throw cursor.Fail("KPrefabID tag-set subtype is unsupported", SavedInspectionFailureKind.Unsupported);
                    int bytes = cursor.ReadInt32(), count = cursor.ReadInt32();
                    if (count == -1)
                    {
                        if (bytes != 0) throw cursor.Fail("Null tag-set has nonzero byte length");
                        nullTags = true; continue;
                    }
                    if (count < 0) throw cursor.Fail("Invalid tag-set count");
                    if (count > limits.MaximumMembers || count > limits.MaximumTags - tagCount)
                        throw cursor.Fail("Tag count exceeds its limit", SavedInspectionFailureKind.LimitExceeded);
                    tagCount += count;
                    var range = cursor.Take(bytes);
                    if (count > range.Length / 8) throw cursor.Fail("Tag elements cannot fit their payload");
                    var tagCursor = new SavedBoundedCursor(image, range.Offset, range.Length, cursor.Path + "/tags");
                    var tagMembers = Layout(tagCursor, "Tag", ("name", 12), ("hash", 6));
                    for (int i = 0; i < count; i++)
                    {
                        string? name = null; int hash = 0;
                        foreach (var tagMember in tagMembers)
                        {
                            if (tagMember.Name == "name") name = tagCursor.String(limits.MaximumValueStringBytes, false, false);
                            else hash = tagCursor.ReadInt32();
                        }
                        tags.Add(new SavedTagRecord(name, hash));
                    }
                    tagCursor.RequireEnd();
                }
                cursor.RequireEnd(); return new SavedPrefabIdentity(id, nullTags, tags);
            }

            private SavedStorageRecord ReadStorage(SavedBoundedCursor cursor, int depth)
            {
                var members = Layout(cursor, "Storage", ("onlyFetchMarkedItems", 3), ("shouldSaveItems", 3),
                    ("workTimeRemaining", 10), ("numberOfUses", 6));
                bool marked = false, save = false; uint remaining = 0; int uses = 0;
                foreach (var member in members)
                {
                    if (member.Type.Encoding == 3)
                    {
                        byte value = cursor.ReadByte();
                        if (value > 1) throw cursor.Fail("Noncanonical Boolean byte");
                        if (member.Name == "onlyFetchMarkedItems") marked = value != 0; else save = value != 0;
                    }
                    else if (member.Name == "workTimeRemaining") remaining = cursor.ReadUInt32();
                    else uses = cursor.ReadInt32();
                }
                int count = cursor.Count(limits.MaximumRoots - rootCount, 50, "Storage packet count");
                var frames = new List<RootFrame>();
                for (int i = 0; i < count; i++)
                {
                    string tag = cursor.String(limits.MaximumTypeNameBytes, true, true)!;
                    frames.Add(ReadFrame(cursor, tag, cursor.Path + "/packet[" + i + "]", depth + 1));
                }
                cursor.RequireEnd();
                var packets = new List<SavedRootRecord>();
                foreach (var frame in frames) packets.Add(Decode(frame));
                return new SavedStorageRecord(marked, save, remaining, uses, packets);
            }
        }
    }
}
