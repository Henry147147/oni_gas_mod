using System.Collections;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static int passed;
    private const string Marker = "MixedAtmospheres.Oni.OwnedReservoirOwnerMarker";
    private static readonly (string name, byte[] type)[] MarkerFields =
    {
        ("schemaVersion", new byte[] { 6 }), ("nativeColonyId", new byte[] { 12 }), ("persistentOwnerToken", new byte[] { 12 }),
        ("coreColonyId", new byte[] { 12 }), ("containerId", new byte[] { 12 }), ("generation", new byte[] { 8 }), ("allocationToken", new byte[] { 12 })
    };
    private static readonly (string name, byte[] type)[] StorageFields =
    { ("onlyFetchMarkedItems", new byte[] { 3 }), ("shouldSaveItems", new byte[] { 3 }), ("workTimeRemaining", new byte[] { 10 }), ("numberOfUses", new byte[] { 6 }) };
    private static readonly byte[] TagType = Bytes(w => { w.Write((byte)64); String(w, "Tag"); });
    private static readonly (string name, byte[] type)[] IdentityFields =
    { ("InstanceID", new byte[] { 6 }), ("serializedTags", Bytes(w => { w.Write((byte)149); w.Write((byte)1); w.Write(TagType); })) };
    private static readonly (string name, byte[] type)[] TagFields = { ("name", new byte[] { 12 }), ("hash", new byte[] { 6 }) };
    private static string GuidN(int value) => value.ToString("x32");

    private static int Main(string[] args)
    {
        try
        {
            Test("empty copied images, exact section extent and tail", () =>
            {
                var directory = Directory(); var bytes = Body(Array.Empty<(string, byte[][])>(), new byte[] { 9, 8 });
                var report = Parse(bytes, directory);
                Require(report.Section.Length == 16 && report.UntouchedTail.Length == 2 && report.Groups.Count == 0);
                Require(!report.OwnedRestorePermitted && !report.WholeFilePreflightProven && !report.GeneralDescendantCoverageProven);
            });
            Test("opaque records and exact supplied offset, no magic search", () =>
            {
                var opaque = Encoding.UTF8.GetBytes("KSAV " + Marker);
                var root = Root(("Other", opaque), ("Other", new byte[] { 1, 2 }));
                var body = new byte[] { 0, 1, 2 }.Concat(Body(new[] { ("UnknownPrefab", new[] { root }) }, new byte[] { 3, 2, 1 })).ToArray();
                var report = SavedKsavInspector.Inspect(SavedImage.CopyFrom(body), 3, Directory());
                Require(report.OpaqueComponentCount == 2 && report.Groups[0].Roots[0].Components[1].Ordinal == 1);
                var range = report.Groups[0].Roots[0].Components[0].Payload;
                Require(report.Image.CopySlice(range.Offset, range.Length).SequenceEqual(opaque));
                Reject(() => SavedKsavInspector.Inspect(SavedImage.CopyFrom(body), 0, Directory()));
            });
            Test("default assigned invalid and null markers preserve Int64", () =>
            {
                var items = new[] { Owner(0, 0), Owner(1, 2, 1L << 40), Owner(91, 3), Owner(0, 4, nullString: true) };
                var report = Parse(Body(new[] { ("GasReservoir", items) }), Directory());
                var roots = report.Groups[0].Roots;
                Require(roots[0].Markers[0].State == SavedMarkerState.Unassigned);
                Require(roots[1].Markers[0].State == SavedMarkerState.Assigned && roots[1].Markers[0].Generation == 1L << 40);
                Require(roots[2].Markers[0].State == SavedMarkerState.Invalid && roots[3].Markers[0].State == SavedMarkerState.Invalid);
            });
            Test("inherited fields follow saved order and preserve infinity bits", () =>
            {
                var order = StorageFields.Reverse().ToArray();
                var directory = Directory(storage: order);
                var storage = Storage(Array.Empty<(string, byte[])>(), order, 0x7f800000);
                var report = Parse(Body(new[] { ("GasReservoir", new[] { Root(("Storage", storage)) }) }), directory);
                var record = report.Groups[0].Roots[0].Storages[0];
                Require(record.ShouldSaveItems && !record.OnlyFetchMarkedItems && record.WorkTimeRemainingBits == 0x7f800000 && record.NumberOfUses == 7);
            });
            Test("all top-level tables scan before known layouts", () =>
            {
                var body = Body(new[] { ("GasReservoir", new[] { Owner(1, 5), Root(("Other", new byte[1])) }) });
                // Position is supplied by the independent writer's exact length,
                // not discovered by searching for any component/magic bytes.
                int lastLength = body.Length - 5;
                var hostile = (byte[])body.Clone(); Put32(hostile, lastLength, 1000);
                SavedKsavReport? result = null;
                Reject(() => result = Parse(hostile, Directory())); Require(result == null);
            });
            Test("unowned positive packets traverse and remain byte-identical", () =>
            {
                var packet = Root(("KPrefabID", Identity(25)), ("PrimaryElement", new byte[] { 1, 9, 8, 7, 6 }));
                var parent = Root((Marker, MarkerData(0)), ("KPrefabID", Identity(24)), ("Storage", Storage(new[] { ("Oxygen", packet) })));
                var bytes = Body(new[] { ("GasReservoir", new[] { parent }) });
                var report = Parse(bytes, Directory()); var storage = report.Groups[0].Roots[0].Storages[0];
                Require(storage.PacketCount == 1 && storage.Packets[0].PrefabIdentities[0].InstanceId == 25);
                Require(storage.Packets[0].StorageDepth == 1 && report.AllRecognizedRoots.Count == 2);
                Require(report.Image.CopySlice(0, bytes.Length).SequenceEqual(bytes));
            });
            Test("assigned native packets are reported, never dropped", () =>
            {
                var packet = Root(("Other", new byte[] { 5 }));
                var parent = Root((Marker, MarkerData(1)), ("KPrefabID", Identity(4)), ("Storage", Storage(new[] { ("Oxygen", packet) })));
                var report = Parse(Body(new[] { ("GasReservoir", new[] { parent }) }), Directory());
                Require(report.SemanticIssues.Any(x => x.Contains("native Storage packets")) && report.Groups[0].Roots[0].Storages[0].PacketCount == 1);
            });
            Test("tag value elements have no per-element envelope and are not repaired", () =>
            {
                var record = Root(("KPrefabID", Identity(0, new[] { ("Tag 名", 123), ((string?)null, -5) })));
                var identity = Parse(Body(new[] { ("Unknown", new[] { record }) }), Directory()).Groups[0].Roots[0].PrefabIdentities[0];
                Require(identity.InstanceId == 0 && identity.Tags.Count == 2 && identity.Tags[1].Name == null && identity.Tags[1].Hash == -5);
            });
            Test("duplicate reserved components and later IDs are explicit", () =>
            {
                var duplicate = Root((Marker, MarkerData(1)), (Marker, MarkerData(1)), ("KPrefabID", Identity(9)), ("Storage", Storage(Array.Empty<(string, byte[])>())));
                var report = Parse(Body(new[] { ("GasReservoir", new[] { duplicate, Owner(1, 9) }) }), Directory());
                Require(report.SemanticIssues.Any(x => x.Contains("exactly one")) && report.SemanticIssues.Any(x => x.Contains("duplicate saved InstanceID")));
                Require(report.SemanticIssues.Any(x => x.Contains("duplicate persistent owner token")) && report.SemanticIssues.Any(x => x.Contains("duplicate full Core handle")));
                Require(report.Groups[0].Roots[0].Markers.Count == 2);
            });
            Test("marker outside supported prefab or inside packet has no authority", () =>
            {
                var nested = Root(("Storage", Storage(new[] { ("GasReservoir", Owner(1, 2)) })));
                var report = Parse(Body(new[] { ("Wrong", new[] { Owner(1, 1), nested }) }), Directory());
                Require(report.SemanticIssues.Count(x => x.Contains("outside a supported")) == 2 && !report.OwnedRestorePermitted);
            });
            Test("all truncated complete-directory prefixes reject with typed failure", () =>
            {
                var bytes = DirectoryBytes();
                for (int i = 0; i < bytes.Length; i++) { int length = i; Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, length)); }
            });
            Test("all truncated complete-KSAV prefixes reject with no result", () =>
            {
                var bytes = Body(new[] { ("GasReservoir", new[] { Owner(1, 12) }) });
                for (int i = 0; i < bytes.Length; i++) { int length = i; Reject(() => Parse(bytes.Take(length).ToArray(), Directory())); }
            });
            Test("wrong magic/version rejects even with a later valid section", () =>
            {
                var body = Body(Array.Empty<(string, byte[][])>()).Concat(Body(Array.Empty<(string, byte[][])>())).ToArray();
                body[0] = 0; Reject(() => Parse(body, Directory())); body[0] = (byte)'K';
                Put32(body, 8, -1); Reject(() => Parse(body, Directory()), SavedInspectionFailureKind.Unsupported);
            });
            Test("negative or impossible group/component extents reject", () =>
            {
                var groups = Body(new[] { ("X", new[] { Root() }) });
                foreach (int value in new[] { -1, int.MaxValue, 46, 44 })
                { var bad = (byte[])groups.Clone(); Put32(bad, 25, value); Reject(() => Parse(bad, Directory())); }
                var badCount = Body(Array.Empty<(string, byte[][])>()); Put32(badCount, 12, -1); Reject(() => Parse(badCount, Directory()));
            });
            Test("duplicate groups reject without choosing a winner", () =>
                Reject(() => Parse(Body(new[] { ("X", new[] { Root() }), ("X", new[] { Root() }) }), Directory())));
            Test("strict UTF8, required names and negative string sentinels", () =>
            {
                foreach (var name in new[] { new byte[] { 255 }, Array.Empty<byte>() })
                {
                    var data = Bytes(w => { w.Write(1); w.Write(name.Length); w.Write(name); w.Write(0); w.Write(0); });
                    Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(data), 0, data.Length));
                }
                var invalid = Bytes(w => { w.Write(1); w.Write(-2); });
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(invalid), 0, invalid.Length));
                var control = DirectoryBytes(extra: new[] { ("bad\0type", Array.Empty<(string, byte[])>()) });
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(control), 0, control.Length));
            });
            Test("descriptor arrays/generics/enum/value type are descriptive only", () =>
            {
                var generic = Bytes(w => { w.Write((byte)128); String(w, "Never.Resolve`1[[Arbitrary.Type, MissingAssembly]]"); w.Write((byte)1); w.Write((byte)17); w.Write((byte)6); });
                var enumeration = Bytes(w => { w.Write((byte)13); String(w, "Unknown.Enum"); });
                var data = DirectoryBytes(extra: new[] { ("Unresolved", new[] { ("generic", generic), ("enum", enumeration), ("value", TagType) }) });
                var directory = SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(data), 0, data.Length);
                Require(directory.Templates["Unresolved"].Members[0].Type.Arguments[0].Kind == 17);
                Require(directory.Templates["Unresolved"].Members[1].Type.TypeName == "Unknown.Enum");
            });
            Test("invalid descriptor flag/arity and ambiguous members reject", () =>
            {
                foreach (var type in new[] { new byte[] { 255 }, new byte[] { 70 }, new byte[] { 20 }, new byte[] { 148, 2, 6, 6 }, new byte[] { 145, 1, 6 }, new byte[] { 128 } })
                {
                    var data = DirectoryBytes(extra: new[] { ("Bad", new[] { ("x", type) }) });
                    Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(data), 0, data.Length));
                }
                var duplicate = DirectoryBytes(extra: new[] { ("Bad", new[] { ("x", new byte[] { 6 }), ("x", new byte[] { 6 }) }) });
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(duplicate), 0, duplicate.Length));
                var repeated = DirectoryBytes(extra: new[] { ("Storage", StorageFields) });
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(repeated), 0, repeated.Length));
            });
            Test("unknown/missing/extra known members and properties reject", () =>
            {
                var wrong = StorageFields.Select(x => x.name == "workTimeRemaining" ? (x.name, new byte[] { 11 }) : x).ToArray();
                Reject(() => Parse(Body(new[] { ("X", new[] { Root(("Storage", Storage(Array.Empty<(string, byte[])>()))) }) }), Directory(storage: wrong)), SavedInspectionFailureKind.Unsupported);
                var data = Bytes(w => { w.Write(1); String(w, Marker); w.Write(0); w.Write(7); foreach (var field in MarkerFields) { String(w, field.name); w.Write(field.type); } });
                var props = SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(data), 0, data.Length);
                Reject(() => Parse(Body(new[] { ("X", new[] { Root((Marker, MarkerData(0))) }) }), props), SavedInspectionFailureKind.Unsupported);
                var emptyDirectory = SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(new byte[4]), 0, 4);
                Reject(() => Parse(Body(new[] { ("X", new[] { Root(("KPrefabID", Identity(1))) }) }), emptyDirectory), SavedInspectionFailureKind.Unsupported);
            });
            Test("known component trailing bytes and Boolean anomalies reject", () =>
            {
                foreach (var component in new[] { (Marker, MarkerData(0)), ("KPrefabID", Identity(1)), ("Storage", Storage(Array.Empty<(string, byte[])>())) })
                    Reject(() => Parse(Body(new[] { ("X", new[] { Root((component.Item1, component.Item2.Concat(new byte[] { 0 }).ToArray())) }) }), Directory()));
                var storage = Storage(Array.Empty<(string, byte[])>()); storage[0] = 2;
                Reject(() => Parse(Body(new[] { ("X", new[] { Root(("Storage", storage)) }) }), Directory()));
            });
            Test("tag byte/count mismatches and null set framing", () =>
            {
                var nullSet = Bytes(w => { w.Write(2); w.Write(0); w.Write(-1); });
                Require(Parse(Body(new[] { ("X", new[] { Root(("KPrefabID", nullSet)) }) }), Directory()).Groups[0].Roots[0].PrefabIdentities[0].TagsAreNull);
                foreach (var id in new[] { Bytes(w => { w.Write(2); w.Write(1); w.Write(-1); w.Write((byte)0); }), Bytes(w => { w.Write(2); w.Write(0); w.Write(1); }) })
                    Reject(() => Parse(Body(new[] { ("X", new[] { Root(("KPrefabID", id)) }) }), Directory()));
            });
            Test("root/component/tag global budgets fail explicitly", () =>
            {
                var two = Body(new[] { ("X", new[] { Root(), Root() }) });
                Reject(() => Parse(two, Directory(), new SavedInspectionLimits(maximumRoots: 1)), SavedInspectionFailureKind.LimitExceeded);
                var comp = Body(new[] { ("X", new[] { Root(("X", new byte[0]), ("Y", new byte[0])) }) });
                Reject(() => Parse(comp, Directory(), new SavedInspectionLimits(maximumComponents: 1)), SavedInspectionFailureKind.LimitExceeded);
                var tags = new[] { Root(("KPrefabID", Identity(1, new (string? name, int hash)[] { ("A", 1), ("B", 2) }))) };
                Reject(() => Parse(Body(new[] { ("X", tags) }), Directory(), new SavedInspectionLimits(maximumTags: 1)), SavedInspectionFailureKind.LimitExceeded);
            });
            Test("nested Storage depth and descriptor budgets", () =>
            {
                var packet = Root(("Storage", Storage(new[] { ("X", Root()) })));
                var parent = Root(("Storage", Storage(new[] { ("X", packet) })));
                Reject(() => Parse(Body(new[] { ("X", new[] { parent }) }), Directory(), new SavedInspectionLimits(maximumStorageDepth: 1)), SavedInspectionFailureKind.LimitExceeded);
                var data = DirectoryBytes(extra: new[] { ("Deep", new[] { ("x", new byte[] { 17, 17, 6 }) }) });
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(data), 0, data.Length, new SavedInspectionLimits(maximumDescriptorDepth: 1)), SavedInspectionFailureKind.LimitExceeded);
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(data), 0, data.Length, new SavedInspectionLimits(maximumDescriptorNodes: 1)), SavedInspectionFailureKind.LimitExceeded);
            });
            Test("copy isolation and read-only result collection identity", () =>
            {
                var data = Body(new[] { ("X", new[] { Root(("Other", new byte[] { 55 })) }) });
                var image = SavedImage.CopyFrom(data); data[0] = 0;
                var directory = Directory(); var report = SavedKsavInspector.Inspect(image, 0, directory);
                Require(ReferenceEquals(report.Image, image) && ReferenceEquals(report.Directory, directory));
                var copy = image.CopySlice(0, image.Length); copy[0] = 0;
                Require(image.CopySlice(0, 1)[0] == (byte)'K');
                try { ((IList)report.Groups).Clear(); throw new Exception("Mutable result collection"); } catch (NotSupportedException) { }
                var directoryCopy = directory.Image.CopySlice(0, directory.Image.Length); directoryCopy[0] = 0;
                Require(directory.Templates.Count == 4);
            });
            Test("bounded limits reject invalid construction and section inputs", () =>
            {
                try { _ = new SavedInspectionLimits(maximumRoots: 0); throw new Exception("Unbounded limit accepted"); } catch (ArgumentOutOfRangeException) { }
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(new byte[4]), -1, 4));
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(new byte[4]), 0, int.MaxValue), SavedInspectionFailureKind.LimitExceeded);
                var bytes = DirectoryBytes();
                Reject(() => SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, bytes.Length, new SavedInspectionLimits(maximumTemplates: 1)), SavedInspectionFailureKind.LimitExceeded);
            });
            Test("exact reserved name only, opaque descendant coverage never claimed", () =>
            {
                var report = Parse(Body(new[] { ("X", new[] { Root((Marker.ToLowerInvariant(), MarkerData(1))) }) }), Directory());
                Require(report.Groups[0].Roots[0].Markers.Count == 0 && report.OpaqueComponentCount == 1 && !report.GeneralDescendantCoverageProven);
            });

            passed += BodyPrefixTests.Run();

            object? actual = null;
            if (args.Length != 0)
            {
                Require(args.Length == 4);
                if ((args[0] == "--actual-capture" || args[0] == "--derived-capture") && args[2] == "--ordinary-save")
                {
                    bool derive = args[0] == "--derived-capture";
                    actual = ActualCapture(args[1], args[3], derive);
                    passed++; Console.WriteLine(derive ? "PASS actual v38 body-zero derived KSAV and untouched packet bytes" : "PASS actual v38 directory and KSAV native ranges, known layouts and untouched packet bytes");
                }
                else
                {
                    Require(args[0] == "--ordinary-fixture" && args[2] == "--directory-end");
                    int end = int.Parse(args[3], System.Globalization.CultureInfo.InvariantCulture);
                    actual = ActualDirectory(args[1], end);
                    passed++; Console.WriteLine("PASS actual v33 saved directory and exact native observed end (no KSAV body claim)");
                }
            }
            Console.WriteLine(JsonSerializer.Serialize(new { Passed = true, Groups = passed,
                ActualDirectory = args.Length == 4 && args[0] == "--ordinary-fixture" ? actual : null,
                ActualCapture = args.Length == 4 && (args[0] == "--actual-capture" || args[0] == "--derived-capture") ? actual : null,
                OwnedRestorePermitted = false, WholeFilePreflightProven = false }));
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static object ActualCapture(string captureFolder, string ordinaryPath, bool derivePrefix = false)
    {
        const string fileSha = "b165e67adf05765b0837826c09492d0d7ea0a0cd55f4dfbcccbb2d6a1ac3d7a8";
        const string bodySha = "aa16d9ee13d82bece7318dc7b55546cf9bca181cf602665f8300838af9708262";
        const string observationsSha = "56b0e087bcd75272a9127906742795f3c0c74905f17546a906e842c5c09ee423";
        string Hash(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
        byte[] Read(string path, int expectedLength, string expectedHash)
        {
            Require(new FileInfo(path).Length == expectedLength);
            var bytes = File.ReadAllBytes(path);
            Require(bytes.Length == expectedLength && Hash(bytes) == expectedHash);
            return bytes;
        }
        string observationsPath = Path.Combine(captureFolder, "observations.json");
        Require(new FileInfo(observationsPath).Length is > 0 and < 65536);
        byte[] observationBytes = File.ReadAllBytes(observationsPath);
        Require(Hash(observationBytes) == observationsSha);
        using var document = JsonDocument.Parse(observationBytes, new JsonDocumentOptions { MaxDepth = 32 });
        var observation = document.RootElement;
        Require(observation.GetProperty("Kind").GetString() == "private-read-only-ordinary-native-section-observation");
        Require(observation.GetProperty("Errors").GetArrayLength() == 0);
        foreach (string count in new[] { "DirectoryEntries", "DirectoryReturns", "SectionEntries", "SectionReturns" })
            Require(observation.GetProperty(count).GetInt32() == 1);
        foreach (string flag in new[] { "NativeReaderModified", "ExtraDeserializerCalls", "MarkerAssigned", "OwnedRestorePermitted" })
            Require(!observation.GetProperty(flag).GetBoolean());
        var records = observation.GetProperty("Records").EnumerateArray().ToArray();
        JsonElement One(string kind) => records.Single(x => x.GetProperty("Kind").GetString() == kind);
        var directoryEntry = One("directory-entry"); var directoryReturn = One("directory-return");
        var sectionEntry = One("ksav-entry"); var sectionReturn = One("ksav-return");
        Require(directoryEntry.GetProperty("Position").GetInt32() == 398 && directoryReturn.GetProperty("Start").GetInt32() == 398);
        Require(directoryReturn.GetProperty("End").GetInt32() == 29965 && directoryReturn.GetProperty("Length").GetInt32() == 29567);
        Require(sectionEntry.GetProperty("Position").GetInt32() == 9472651 && sectionReturn.GetProperty("Start").GetInt32() == 9472651);
        Require(sectionReturn.GetProperty("End").GetInt32() == 10456036 && sectionReturn.GetProperty("Length").GetInt32() == 983385);
        foreach (var entry in new[] { directoryEntry, sectionEntry })
        {
            Require(entry.GetProperty("SourceSha256").GetString() == fileSha && entry.GetProperty("Thread").GetInt32() == 1);
            Require(entry.GetProperty("ReaderType").GetString() == "FastReader, Assembly-CSharp-firstpass, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
        }
        foreach (var returned in new[] { directoryReturn, sectionReturn })
        {
            foreach (string flag in new[] { "SameReader", "SameRawArray", "SameBytes", "NormalResult" }) Require(returned.GetProperty(flag).GetBoolean());
            Require(returned.GetProperty("Thread").GetInt32() == 1);
        }
        var nativeScalars = records.Where(x => x.GetProperty("Kind").GetString() == "native-scalar-expectations").ToArray();
        Require(nativeScalars.Length == 3);
        Require(nativeScalars.Select(x => x.GetProperty("Stage").GetString()).SequenceEqual(new[] { "before-ordinary-save", "actual-native-storage-serialize", "after-ordinary-load" }));
        foreach (var scalar in nativeScalars)
        {
            Require(scalar.GetProperty("ParentSavedId").GetInt32() == 247004 && scalar.GetProperty("PacketSavedId").GetInt32() == 247005);
            Require(scalar.GetProperty("NativeItemCount").GetInt32() == 1 && scalar.GetProperty("MarkerState").GetString() == "Unassigned");
            Require(!scalar.GetProperty("NativeOwnerAssignment").GetBoolean());
            Require(scalar.GetProperty("Element").GetInt32() == -1528777920 && scalar.GetProperty("MassBits").GetInt32() == 1095237632 &&
                scalar.GetProperty("TemperatureBits").GetInt32() == 1134231552 && scalar.GetProperty("DiseaseIndex").GetInt32() == 0 &&
                scalar.GetProperty("DiseaseCount").GetInt32() == 12345);
        }
        byte[] ordinary = Read(ordinaryPath, 1139745, fileSha);
        byte[] directoryImage = Read(Path.Combine(captureFolder, "directory-image.bin"), 1139745, fileSha);
        byte[] body = Read(Path.Combine(captureFolder, "ksav-image.bin"), 10552251, bodySha);
        Require(directoryImage.AsSpan().SequenceEqual(ordinary));
        Require(directoryEntry.GetProperty("RawLength").GetInt32() == ordinary.Length && directoryEntry.GetProperty("RawSha256").GetString() == fileSha);
        Require(sectionEntry.GetProperty("RawLength").GetInt32() == body.Length && sectionEntry.GetProperty("RawSha256").GetString() == bodySha);
        Require(BitConverter.IsLittleEndian && BitConverter.ToUInt32(ordinary, 0) == 744825 && BitConverter.ToUInt32(ordinary, 8) == 1 &&
            BitConverter.ToInt32(ordinary, 12) == 1 && checked(16 + BitConverter.ToInt32(ordinary, 4)) == 398);
        var inflated = new byte[body.Length];
        using (var compressed = new MemoryStream(ordinary, 29965, ordinary.Length - 29965, false))
        using (var zlib = new ZLibStream(compressed, CompressionMode.Decompress))
        {
            int position = 0;
            while (position < inflated.Length)
            {
                int count = zlib.Read(inflated, position, inflated.Length - position);
                Require(count > 0); position += count;
            }
            Require(zlib.ReadByte() == -1);
        }
        Require(inflated.AsSpan().SequenceEqual(body));
        var directory = SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(directoryImage), 398, 29965 - 398);
        Require(directory.Range.End == 29965);
        var bodyImage = SavedImage.CopyFrom(body);
        SavedBodyPrefixReport? prefix = derivePrefix ? SavedBodyPrefixInspector.Inspect(bodyImage, directory) : null;
        // The observed start is an external comparison only. In derived mode,
        // no observed offset is passed to the prefix product API.
        if (prefix != null) Require(prefix.KsavOffset == 9472651);
        var report = SavedKsavInspector.Inspect(bodyImage, prefix?.KsavOffset ?? 9472651, directory);
        Require(report.Section.End == 10456036 && report.Section.Length == 983385 && report.UntouchedTail.Length == body.Length - 10456036);
        Require(report.SemanticIssues.Count == 0 && report.FramingComplete && report.RecognizedStorageTraversalComplete);
        Require(!report.WholeFilePreflightProven && !report.GeneralDescendantCoverageProven && !report.OwnedRestorePermitted);
        Require(report.Image.CopySlice(0, body.Length).AsSpan().SequenceEqual(body));
        var parent = report.AllRecognizedRoots.Single(x => x.PrefabIdentities.Any(id => id.InstanceId == 247004));
        Require(parent.StorageDepth == 0 && parent.PrefabTag == "GasReservoir" && parent.PrefabIdentities.Count == 1 && parent.Markers.Count == 1 && parent.Storages.Count == 1);
        Require(parent.Markers[0].State == SavedMarkerState.Unassigned && parent.Storages[0].PacketCount == 1);
        var markerBytes = parent.Components.Single(x => x.TypeName == Marker).Payload;
        Require(markerBytes.Length == 32);
        var storageComponent = parent.Components.Single(x => x.TypeName == "Storage");
        var packet = parent.Storages[0].Packets.Single();
        Require(packet.PrefabIdentities.Count == 1 && packet.PrefabIdentities[0].InstanceId == 247005 && packet.StorageDepth == 1);
        Require(packet.Range.Offset >= storageComponent.Payload.Offset && packet.Range.End <= storageComponent.Payload.End);
        var primary = packet.Components.Single(x => x.TypeName == "PrimaryElement");
        int components = 0;
        long payloadBytes = 0;
        foreach (var root in report.AllRecognizedRoots)
        foreach (var component in root.Components)
        {
            var range = component.Payload;
            Require(report.Image.CopySlice(range.Offset, range.Length).AsSpan().SequenceEqual(body.AsSpan(range.Offset, range.Length)));
            components++; payloadBytes += range.Length;
        }
        object Range(SavedByteRange range) => new { range.Offset, range.Length, range.End, Sha256 = report.Image.HashSlice(range.Offset, range.Length) };
        return new
        {
            Fixture = "actual-ordinary-Windows-v38", SourceSha256 = fileSha, ObservationsSha256 = observationsSha,
            ExactBoundedIndependentInflation = true,
            Directory = new { directory.Range.Offset, directory.Range.Length, directory.Range.End, directory.Sha256, Templates = directory.Templates.Count },
            Ksav = new { report.Section.Offset, report.Section.Length, report.Section.End, ImageBytes = body.Length, ImageSha256 = bodySha,
                Groups = report.Groups.Count, TopLevelRoots = report.Groups.Sum(x => x.Roots.Count), RecognizedRoots = report.AllRecognizedRoots.Count,
                Components = components, PayloadBytesIncludingNestedRecords = payloadBytes, report.OpaqueComponentCount, SemanticIssues = report.SemanticIssues.Count,
                NativeStoragePackets = report.AllRecognizedRoots.Sum(x => x.Storages.Sum(s => s.PacketCount)), UntouchedPrefix = Range(new SavedByteRange(0, report.Section.Offset)),
                UntouchedTail = Range(report.UntouchedTail) },
            Parent = new { SavedId = 247004, parent.PrefabTag, parent.StorageDepth, MarkerState = parent.Markers[0].State.ToString(), MarkerPayload = Range(markerBytes),
                StoragePacketCount = 1, StoragePayload = Range(storageComponent.Payload), EntireRoot = Range(parent.Range) },
            Packet = new { SavedId = 247005, packet.PrefabTag, packet.StorageDepth, EntireRoot = Range(packet.Range), PrimaryElementOpaquePayload = Range(primary.Payload) },
            NativeScalarWitnesses = nativeScalars.Select(x => x.Clone()).ToArray(),
            Prefix = prefix == null ? null : new
            {
                StartsAtBodyZero = true, KsavOffsetAuthority = "walked closed prefix and inline Sim envelope from body0",
                prefix.KsavOffset, prefix.WidthInCells, prefix.HeightInCells, prefix.ClusterId, prefix.NextUniqueId, prefix.GameId,
                SaveFileRootMembers = Range(prefix.SaveFileRootMembers), SettingsMembers = Range(prefix.SettingsMembers),
                RequiredMods = Range(prefix.RequiredMods), StreamedDictionary = Range(prefix.StreamedDictionary), ActiveModsList = Range(prefix.ActiveModsList),
                SimLengthField = Range(prefix.SimLengthField), SimPayload = Range(prefix.SimPayload),
                Streamed = prefix.Streamed.Select(x => new { x.Key, x.IsNull, EncodedValue = Range(x.EncodedValue), Payload = Range(x.Payload), EncodedKey = Range(x.EncodedKey) }).ToArray(),
                ActiveMods = prefix.ActiveMods.Select(x => new { x.DistributionPlatform, x.Id, x.Title, x.Version, Range = Range(x.Range) }).ToArray(),
                prefix.NativeGridValidityProven, prefix.NativeSimValidityProven, prefix.NativeLoadPermitted, prefix.WriterPermission, prefix.QuarantineActivated
            },
            PrimaryElementMassTemperatureDiseaseParsed = false, WholeFilePreflightProven = false, GeneralDescendantCoverageProven = false, OwnedRestorePermitted = false,
            Scope = derivePrefix ? "KSAV start structurally derived from immutable inflated body0 and closed saved layouts; outer file association is fixture calibration only; native scalar witnesses separate from opaque PrimaryElement bytes; no native calls or restore"
                : "Actual directory/KSAV sections from observed native offsets only; native scalar witnesses are separate from opaque PrimaryElement bytes; no native calls or restore"
        };
    }

    private static object ActualDirectory(string path, int expectedEnd)
    {
        var info = new FileInfo(path); Require(info.Length == 1143798);
        byte[] raw = File.ReadAllBytes(path); string hash = Convert.ToHexStringLower(SHA256.HashData(raw));
        Require(hash == "ac6146317872e088b41e20dd96ee0d4c5f84f75897f86845d9f8485df2f6941e" && expectedEnd == 30035);
        // This fixture wrapper checks the already pinned ordinary native v1
        // header only. It is not a product whole-file/header parser.
        Require(BitConverter.ToUInt32(raw, 0) == 744825 && BitConverter.ToUInt32(raw, 8) == 1 && BitConverter.ToInt32(raw, 12) == 1);
        int headerBytes = BitConverter.ToInt32(raw, 4); Require(headerBytes > 0 && headerBytes < expectedEnd - 16);
        int start = checked(16 + headerBytes);
        var image = SavedImage.CopyFrom(raw);
        var directory = SavedDirectoryInspector.Inspect(image, start, expectedEnd - start);
        Require(directory.Range.End == expectedEnd && directory.Templates.ContainsKey(Marker) && directory.Templates.ContainsKey("Storage") && directory.Templates.ContainsKey("KPrefabID"));
        Require(directory.Templates[Marker].Members.Count == 7 && directory.Templates["Storage"].Members.Count == 4 && directory.Templates["KPrefabID"].Members.Count == 2);
        return new { FileSha256 = hash, Bytes = raw.Length, Start = start, End = directory.Range.End, directory.Sha256,
            Templates = directory.Templates.Count, Known = new[] { Marker, "Storage", "KPrefabID", "Tag" }.ToDictionary(x => x,
                x => directory.Templates[x].Members.Select(m => new { m.Name, m.IsProperty, m.Type.Encoding, m.Type.TypeName,
                    Arguments = m.Type.Arguments.Select(a => new { a.Encoding, a.TypeName }).ToArray() }).ToArray()),
            Scope = "Actual saved-directory bytes only; no KSAV anchor/body parsing, native callback execution or restore" };
    }

    private static SavedKsavReport Parse(byte[] body, SavedDirectory directory, SavedInspectionLimits? limits = null)
        => SavedKsavInspector.Inspect(SavedImage.CopyFrom(body), 0, directory, limits);
    private static void Test(string name, Action test) { test(); passed++; Console.WriteLine("PASS " + name); }
    private static void Require(bool value) { if (!value) throw new Exception("Assertion failed"); }
    private static SavedInspectionException Reject(Action action, SavedInspectionFailureKind? kind = null)
    {
        try { action(); } catch (SavedInspectionException failure)
        { Require(kind == null || failure.Kind == kind); Require(failure.Offset >= 0 && failure.RecordPath.Length > 0); return failure; }
        throw new Exception("Malformed/unsupported input produced a report");
    }
    private static byte[] Bytes(Action<BinaryWriter> write)
    { using var stream = new MemoryStream(); using (var writer = new BinaryWriter(stream, Encoding.UTF8, true)) write(writer); return stream.ToArray(); }
    private static void String(BinaryWriter writer, string? value)
    { if (value == null) writer.Write(-1); else { byte[] bytes = Encoding.UTF8.GetBytes(value); writer.Write(bytes.Length); writer.Write(bytes); } }
    private static SavedDirectory Directory((string name, byte[] type)[]? storage = null)
    { byte[] bytes = DirectoryBytes(storage); return SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(bytes), 0, bytes.Length); }
    private static byte[] DirectoryBytes((string name, byte[] type)[]? storage = null, (string name, (string name, byte[] type)[] fields)[]? extra = null)
    {
        return Bytes(w =>
        {
            w.Write(4 + (extra?.Length ?? 0));
            foreach (var template in new[] { (Marker, MarkerFields), ("KPrefabID", IdentityFields), ("Storage", storage ?? StorageFields), ("Tag", TagFields) }.Concat(extra ?? Array.Empty<(string, (string, byte[])[])>()))
            {
                String(w, template.Item1); w.Write(template.Item2.Length); w.Write(0);
                foreach (var field in template.Item2) { String(w, field.Item1); w.Write(field.Item2); }
            }
        });
    }
    private static byte[] Body((string tag, byte[][] roots)[] groups, byte[]? tail = null)
        => Bytes(w =>
        {
            w.Write(new byte[] { 75, 83, 65, 86 }); w.Write(7); w.Write(38); w.Write(groups.Length);
            foreach (var group in groups) { String(w, group.tag); w.Write(group.roots.Length); w.Write(group.roots.Sum(x => x.Length)); foreach (var root in group.roots) w.Write(root); }
            if (tail != null) w.Write(tail);
        });
    private static byte[] Root(params (string type, byte[] data)[] components)
        => Bytes(w =>
        {
            for (uint i = 1; i <= 10; i++) w.Write(i); w.Write((byte)0); w.Write(components.Length);
            foreach (var component in components) { String(w, component.type); w.Write(component.data.Length); w.Write(component.data); }
        });
    private static byte[] Owner(int version, int id, long generation = 1, bool nullString = false)
        => Root((Marker, MarkerData(version, generation, nullString)), ("KPrefabID", Identity(id)), ("Storage", Storage(Array.Empty<(string, byte[])>())));
    private static byte[] MarkerData(int version, long generation = 1, bool nullString = false)
        => Bytes(w => { w.Write(version); String(w, nullString ? null : version == 0 ? "" : GuidN(1)); String(w, version == 0 ? "" : GuidN(2)); String(w, version == 0 ? "" : GuidN(3)); String(w, version == 0 ? "" : "container 名"); w.Write(version == 0 ? 0L : generation); String(w, version == 0 ? "" : GuidN(4)); });
    private static byte[] Identity(int id, (string? name, int hash)[]? tags = null)
        => Bytes(w =>
        {
            w.Write(id); var items = tags ?? Array.Empty<(string?, int)>();
            var payload = Bytes(tw => { foreach (var tag in items) { String(tw, tag.name); tw.Write(tag.hash); } });
            w.Write(payload.Length); w.Write(items.Length); w.Write(payload);
        });
    private static byte[] Storage((string tag, byte[] root)[] packets, (string name, byte[] type)[]? order = null, uint workBits = 0x7fc01234)
        => Bytes(w =>
        {
            foreach (var field in order ?? StorageFields)
            {
                if (field.name == "onlyFetchMarkedItems") w.Write((byte)0);
                else if (field.name == "shouldSaveItems") w.Write((byte)1);
                else if (field.name == "workTimeRemaining") w.Write(workBits);
                else w.Write(7);
            }
            w.Write(packets.Length); foreach (var packet in packets) { String(w, packet.tag); w.Write(packet.root); }
        });
    private static void Put32(byte[] data, int offset, int value)
    { var bytes = BitConverter.GetBytes(value); Require(BitConverter.IsLittleEndian); Array.Copy(bytes, 0, data, offset, 4); }
}
