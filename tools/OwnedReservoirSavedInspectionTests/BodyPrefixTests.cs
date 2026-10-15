using System.Collections;
using System.Text;
using MixedAtmospheres.Oni;

internal static class BodyPrefixTests
{
    private static readonly string[] RootOrder = { "WidthInCells", "HeightInCells", "streamed", "clusterID", "requiredMods", "active_mods" };
    private static readonly string[] LabelOrder = { "distribution_platform", "id", "title", "version" };
    private static readonly string[] SettingsOrder = { "nextUniqueID", "gameID" };
    private const string RootName = "Klei.SaveFileRoot", LabelName = "KMod.Label", SettingsName = "Game+Settings";
    private sealed record Stream(string? Key, byte[]? Data);
    private sealed record Label(int Platform, string? Id, string? Title, long Version);
    private sealed class Options
    {
        internal string[] RootMembers = RootOrder, LabelMembers = LabelOrder, SettingsMembers = SettingsOrder;
        internal List<Stream> Streams = new() { new("GridVisible", new byte[] { 1, 2 }) };
        internal List<Label> Labels = new() { new(4, "fixture.id", "label", 1L << 40) };
        internal byte[] Sim = { 21, 22, 23 };
        internal string? World = "world", RootMarker = RootName, SettingsMarker = SettingsName, Cluster = "cluster 名";
        internal int Width = 73, Height = 9, NextId = 16, GameId = -17;
        internal int RequiredCount = -1, RequiredClaim = 0;
        internal bool NullMap, NullLabels, Interleave, MissingLabel, IncludeModInfo, RootProperties, ExtraRootLength;
        internal byte LabelValueFlag = 64, ModValueFlag = 64;
        internal string LabelEnumName = "KMod.Label+DistributionPlatform";
        internal bool InvalidWorldUtf8;
    }
    private sealed class Fixture
    {
        internal readonly byte[] Body;
        internal readonly SavedDirectory Directory;
        internal readonly Dictionary<string, int> At = new(StringComparer.Ordinal);
        internal readonly Options Config;
        internal Fixture(Action<Options>? change = null)
        {
            Config = new Options(); change?.Invoke(Config);
            byte[] directory = Bytes(WriteDirectory);
            Directory = SavedDirectoryInspector.Inspect(SavedImage.CopyFrom(directory), 0, directory.Length);
            Body = Bytes(WriteBody);
        }
        private int Position(BinaryWriter w, string name)
        { int position = checked((int)w.BaseStream.Position); At.Add(name, position); return position; }
        private void DataString(BinaryWriter w, string name, string? value)
        {
            Position(w, name + ".length");
            if (value == null) { w.Write(-1); Position(w, name + ".payload"); return; }
            byte[] bytes = Encoding.UTF8.GetBytes(value); w.Write(bytes.Length); Position(w, name + ".payload"); w.Write(bytes);
        }
        private void WriteBody(BinaryWriter w)
        {
            if (Config.InvalidWorldUtf8) { w.Write(1); w.Write((byte)255); }
            else DataString(w, "world", Config.World);
            DataString(w, "root-marker", Config.RootMarker);
            if (Config.ExtraRootLength) w.Write(123);
            Position(w, "root-data");
            foreach (string field in Config.RootMembers)
            {
                Position(w, "root." + field);
                switch (field)
                {
                    case "WidthInCells": w.Write(Config.Width); break;
                    case "HeightInCells": w.Write(Config.Height); break;
                    case "clusterID": DataString(w, "cluster", Config.Cluster); break;
                    case "requiredMods": w.Write(Config.RequiredClaim); w.Write(Config.RequiredCount); break;
                    case "streamed": WriteStreams(w); break;
                    case "active_mods": WriteLabels(w); break;
                }
            }
            Position(w, "root-end"); DataString(w, "settings-marker", Config.SettingsMarker);
            foreach (string field in Config.SettingsMembers)
            { Position(w, "settings." + field); w.Write(field == "nextUniqueID" ? Config.NextId : Config.GameId); }
            Position(w, "sim-length"); w.Write(Config.Sim.Length); Position(w, "sim-payload"); w.Write(Config.Sim);
            Position(w, "ksav"); w.Write(new byte[] { 75, 83, 65, 86 }); w.Write(7); w.Write(38); w.Write(0);
        }
        private void WriteStreams(BinaryWriter w)
        {
            int claimPosition = Position(w, "dict-length"); w.Write(0);
            Position(w, "dict-count"); w.Write(Config.NullMap ? -1 : Config.Streams.Count);
            int start = Position(w, "dict-payload");
            if (!Config.NullMap)
            {
                if (Config.Interleave)
                {
                    for (int i = 0; i < Config.Streams.Count; i++) { DataString(w, "key" + i, Config.Streams[i].Key); WriteArray(w, i, Config.Streams[i].Data); }
                }
                else
                {
                    for (int i = 0; i < Config.Streams.Count; i++) WriteArray(w, i, Config.Streams[i].Data);
                    for (int i = 0; i < Config.Streams.Count; i++) DataString(w, "key" + i, Config.Streams[i].Key);
                }
            }
            int end = checked((int)w.BaseStream.Position); w.BaseStream.Position = claimPosition; w.Write(end - start); w.BaseStream.Position = end;
        }
        private void WriteArray(BinaryWriter w, int index, byte[]? data)
        {
            Position(w, "array" + index + ".length"); w.Write(data?.Length ?? 0);
            Position(w, "array" + index + ".count"); w.Write(data?.Length ?? -1);
            Position(w, "array" + index + ".payload"); if (data != null) w.Write(data);
        }
        private void WriteLabels(BinaryWriter w)
        {
            int claimPosition = Position(w, "labels-length"); w.Write(0);
            Position(w, "labels-count"); w.Write(Config.NullLabels ? -1 : Config.Labels.Count);
            int start = checked((int)w.BaseStream.Position);
            if (!Config.NullLabels)
            {
                for (int i = 0; i < Config.Labels.Count; i++)
                {
                    var label = Config.Labels[i]; Position(w, "label" + i);
                    foreach (string field in Config.LabelMembers)
                    {
                        Position(w, "label" + i + "." + field);
                        switch (field)
                        {
                            case "distribution_platform": w.Write(label.Platform); break;
                            case "id": DataString(w, "label" + i + ".id-data", label.Id); break;
                            case "title": DataString(w, "label" + i + ".title-data", label.Title); break;
                            case "version": w.Write(label.Version); break;
                        }
                    }
                }
            }
            int end = checked((int)w.BaseStream.Position); w.BaseStream.Position = claimPosition; w.Write(end - start); w.BaseStream.Position = end;
        }
        private void WriteDirectory(BinaryWriter w)
        {
            w.Write((Config.MissingLabel ? 2 : 3) + (Config.IncludeModInfo ? 1 : 0));
            Template(w, RootName, Config.RootMembers, RootType, Config.RootProperties);
            Template(w, SettingsName, Config.SettingsMembers, _ => new byte[] { 6 });
            if (!Config.MissingLabel) Template(w, LabelName, Config.LabelMembers, name => name switch
            {
                "distribution_platform" => Bytes(x => { x.Write((byte)13); WireString(x, Config.LabelEnumName); }),
                "version" => new byte[] { 8 }, _ => new byte[] { 12 }
            });
            if (Config.IncludeModInfo) Template(w, "ModInfo", new[] { "not-a-supported-layout" }, _ => new byte[] { 6 });
        }
        private byte[] RootType(string field) => field switch
        {
            "WidthInCells" or "HeightInCells" => new byte[] { 6 },
            "clusterID" => new byte[] { 12 },
            "streamed" => new byte[] { 147, 2, 12, 17, 2 },
            _ => Bytes(w => { w.Write((byte)148); w.Write((byte)1); w.Write(field == "requiredMods" ? Config.ModValueFlag : Config.LabelValueFlag); WireString(w, field == "requiredMods" ? "ModInfo" : LabelName); })
        };
    }

    internal static int Run()
    {
        int groups = 0;
        void Test(string name, Action check) { check(); groups++; Console.WriteLine("PASS prefix " + name); }
        Test("body-zero derivation matches independent writer positions and composes", () =>
        {
            var f = new Fixture(); var report = Parse(f);
            Require(report.KsavOffset == f.At["ksav"] && report.SimPayload.Offset == f.At["sim-payload"] && report.SimLengthField.Offset == f.At["sim-length"]);
            Require(report.SaveFileRootMembers.Offset == f.At["root-data"] && report.SaveFileRootMembers.End == f.At["root-end"]);
            Require(report.WidthInCells == 73 && report.HeightInCells == 9 && report.NextUniqueId == 16 && report.GameId == -17);
            Require(report.Streamed[0].Key == "GridVisible" && report.ActiveMods[0].Version == 1L << 40);
            Require(report.Image.CopySlice(report.SimPayload.Offset, report.SimPayload.Length).SequenceEqual(f.Config.Sim));
            Require(SavedKsavInspector.Inspect(report.Image, report.KsavOffset, f.Directory).Section.End == f.Body.Length);
            Denials(report);
        });
        Test("canonical null requiredMods needs no ModInfo definition; empty values differ", () =>
        {
            var f = new Fixture(); Require(!f.Directory.Templates.ContainsKey("ModInfo") && Parse(f).RequiredMods.Length == 8);
            foreach (int count in new[] { 0, 1, int.MaxValue })
            {
                Reject(() => Parse(new Fixture(o => o.RequiredCount = count)), SavedInspectionFailureKind.Unsupported);
                Reject(() => Parse(new Fixture(o => { o.RequiredCount = count; o.IncludeModInfo = true; })), SavedInspectionFailureKind.Unsupported);
            }
            Reject(() => Parse(new Fixture(o => o.RequiredCount = -2)));
            Reject(() => Parse(new Fixture(o => o.RequiredClaim = 1)));
        });
        Test("empty map/Label values and zero native blob remain structural only", () =>
        {
            var f = new Fixture(o => { o.Streams.Clear(); o.Labels.Clear(); o.Sim = Array.Empty<byte>(); o.Cluster = null; });
            var p = Parse(f); Require(p.Streamed.Count == 0 && p.ActiveMods.Count == 0 && p.SimPayload.Length == 0 && p.ClusterId == null);
            Require(p.KsavOffset == f.At["ksav"]); Denials(p);
            Reject(() => Parse(new Fixture(o => { o.Labels.Clear(); o.MissingLabel = true; })), SavedInspectionFailureKind.Unsupported);
        });
        Test("all values precede keys and payload decoys never supply an anchor", () =>
        {
            var f = new Fixture(o => o.Streams = new() { new("second", Encoding.UTF8.GetBytes("KSAV Sim Game+Settings")), new("first", new byte[] { 9, 8, 7 }) });
            var p = Parse(f); Require(p.KsavOffset == f.At["ksav"] && p.Streamed[0].Key == "second" && p.Streamed[1].Key == "first");
            foreach (int i in new[] { 0, 1 }) Require(p.Image.CopySlice(p.Streamed[i].Payload.Offset, p.Streamed[i].Payload.Length).SequenceEqual(f.Config.Streams[i].Data!));
            Reject(() => Parse(new Fixture(o => { o.Interleave = true; o.Streams.Add(new("two", new byte[] { 3 })); })));
        });
        Test("null/empty arrays and Unicode nullable data are preserved by ordinal", () =>
        {
            var f = new Fixture(o => { o.Streams = new() { new("空", null), new("", Array.Empty<byte>()), new("sim\0", new byte[] { 4 }) }; o.Labels = new() { new(-9, null, "名 😀", long.MinValue), new(4, "é", null, long.MaxValue) }; });
            var p = Parse(f); Require(p.Streamed[0].IsNull && !p.Streamed[1].IsNull && p.Streamed[0].EncodedValue.Length == 8 && p.Streamed[1].Payload.Length == 0);
            Require(p.ActiveMods[0].Id == null && p.ActiveMods[0].Title == "名 😀" && p.ActiveMods[0].Version == long.MinValue);
            Require(p.ActiveMods[1].Title == null && p.ActiveMods[1].Version == long.MaxValue);
        });
        Test("closed saved sets are walked in saved field order", () =>
        {
            var f = new Fixture(o => { o.RootMembers = RootOrder.Reverse().ToArray(); o.SettingsMembers = SettingsOrder.Reverse().ToArray(); o.LabelMembers = LabelOrder.Reverse().ToArray(); o.GameId = int.MinValue; o.Width = -4; });
            var p = Parse(f); Require(p.KsavOffset == f.At["ksav"] && p.Members[0].MemberName == "active_mods" && p.ActiveMods[0].Members[0].MemberName == "version");
            Require(p.GameId == int.MinValue && p.WidthInCells == -4 && !p.NativeGridValidityProven);
        });
        Test("exact world/type markers and top-level unframed values are required", () =>
        {
            Reject(() => Parse(new Fixture(o => o.World = null)));
            Reject(() => Parse(new Fixture(o => o.World = "World")), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.RootMarker = "SaveFileRoot")), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.SettingsMarker = "Game.Settings")), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.ExtraRootLength = true)));
        });
        Test("value flags, exact enum name and closed template members fail closed", () =>
        {
            Reject(() => Parse(new Fixture(o => o.LabelValueFlag = 0)), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.ModValueFlag = 0)), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.LabelEnumName = "Other.Enum")), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.RootProperties = true)), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.RootMembers = RootOrder.Take(5).ToArray())), SavedInspectionFailureKind.Unsupported);
        });
        Test("legacy Sim, null maps/lists and null/duplicate keys are unsupported", () =>
        {
            foreach (byte[]? data in new byte[]?[] { null, Array.Empty<byte>(), new byte[] { 1 } })
                Reject(() => Parse(new Fixture(o => o.Streams = new() { new("Sim", data) })), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.NullMap = true)), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.NullLabels = true)), SavedInspectionFailureKind.Unsupported);
            Reject(() => Parse(new Fixture(o => o.Streams = new() { new(null, new byte[0]) })));
            Reject(() => Parse(new Fixture(o => o.Streams.Add(new("GridVisible", new byte[0])))));
            Require(Parse(new Fixture(o => o.Streams = new() { new("sim", new byte[0]) })).Streamed[0].Key == "sim");
        });
        Test("inner array and outer dictionary/list claims cannot control the cursor", () =>
        {
            var f = new Fixture();
            foreach (string target in new[] { "array0.length", "dict-length", "labels-length" })
            foreach (int value in new[] { -1, 0, int.MaxValue })
            { var body = CopyWith(f, target, value); Reject(() => Parse(f, body: body)); }
            var compensated = CopyWith(f, "array0.length", 1); Put32(compensated, f.At["dict-length"], Get32(f.Body, f.At["dict-length"]) - 1);
            Reject(() => Parse(f, body: compensated));
            var tooLong = CopyWith(f, "dict-length", Get32(f.Body, f.At["dict-length"]) + 1); Reject(() => Parse(f, body: tooLong));
        });
        Test("noncanonical null counts/claims and count plausibility reject", () =>
        {
            var f = new Fixture(o => o.Streams[0] = new("null", null));
            Reject(() => Parse(f, body: CopyWith(f, "array0.length", 1)));
            Reject(() => Parse(f, body: CopyWith(f, "array0.count", -2)));
            var normal = new Fixture();
            Reject(() => Parse(normal, body: CopyWith(normal, "dict-count", -2)));
            Reject(() => Parse(normal, body: CopyWith(normal, "labels-count", -2)));
            Reject(() => Parse(normal, body: CopyWith(normal, "dict-count", 2)));
            Reject(() => Parse(normal, body: CopyWith(normal, "labels-count", 2)));
        });
        Test("every truncated synthetic prefix/body fails without a report", () =>
        {
            var f = new Fixture();
            for (int i = 0; i < f.Body.Length; i++) { int length = i; Reject(() => Parse(f, body: f.Body.Take(length).ToArray())); }
        });
        Test("inline count/derived header crossing and decoys never trigger retries", () =>
        {
            var f = new Fixture(o => o.Sim = Encoding.UTF8.GetBytes("KSAV KSAV"));
            foreach (int count in new[] { -1, int.MaxValue, f.Config.Sim.Length + 1 }) Reject(() => Parse(f, body: CopyWith(f, "sim-length", count)));
            var wrong = (byte[])f.Body.Clone(); wrong[f.At["ksav"]] = 0;
            Reject(() => Parse(f, body: wrong.Concat(new byte[] { 75, 83, 65, 86, 7, 0, 0, 0, 38, 0, 0, 0, 0, 0, 0, 0 }).ToArray()));
            Reject(() => Parse(f, body: CopyWith(f, "ksav", 0)));
        });
        Test("all per-item and aggregate resource caps reject before use", () =>
        {
            var f = new Fixture(o => { o.Streams.Add(new("two", new byte[] { 3, 4 })); o.Labels.Add(new(0, "b", "c", 2)); });
            Reject(() => Parse(f, new SavedPrefixLimits(maximumStreamEntries: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumLabels: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumStreamValueBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumStreamValueTotalBytes: 3)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumPrefixBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumDecodedStringBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumSimBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumImageBytes: 1)), SavedInspectionFailureKind.LimitExceeded);
            var title = new Fixture(o => o.Labels[0] = new(4, "a", new string('x', 18), 1));
            Reject(() => Parse(title, new SavedPrefixLimits(maximumStringBytes: 17)), SavedInspectionFailureKind.LimitExceeded);
            var strings = new string?[] { "world", RootName, SettingsName, f.Config.Cluster }
                .Concat(f.Config.Streams.Select(x => x.Key))
                .Concat(f.Config.Labels.SelectMany(x => new string?[] { x.Id, x.Title }))
                .Where(x => x != null).Select(x => Encoding.UTF8.GetByteCount(x!)).ToArray();
            var exact = new SavedPrefixLimits(maximumImageBytes: f.Body.Length, maximumPrefixBytes: f.At["sim-payload"],
                maximumStreamEntries: 2, maximumLabels: 2, maximumStreamValueBytes: 2, maximumStreamValueTotalBytes: 4,
                maximumStringBytes: strings.Max(), maximumDecodedStringBytes: strings.Sum(), maximumSimBytes: f.Config.Sim.Length);
            Require(Parse(f, exact).KsavOffset == f.At["ksav"]);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumPrefixBytes: f.At["sim-payload"] - 1)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, new SavedPrefixLimits(maximumDecodedStringBytes: strings.Sum() - 1)), SavedInspectionFailureKind.LimitExceeded);
        });
        Test("strict UTF8 and huge collection counts cannot allocate trusted records", () =>
        {
            Reject(() => Parse(new Fixture(o => o.InvalidWorldUtf8 = true)));
            var f = new Fixture(); var invalid = (byte[])f.Body.Clone(); invalid[f.At["label0.id-data.payload"]] = 255;
            Reject(() => Parse(f, body: invalid));
            Reject(() => Parse(f, body: CopyWith(f, "dict-count", int.MaxValue)), SavedInspectionFailureKind.LimitExceeded);
            Reject(() => Parse(f, body: CopyWith(f, "labels-count", int.MaxValue)), SavedInspectionFailureKind.LimitExceeded);
        });
        Test("immutable image/result records and independent invocations", () =>
        {
            var f = new Fixture(); var original = (byte[])f.Body.Clone(); var image = SavedImage.CopyFrom(f.Body); f.Body[0] = 0;
            var p = SavedBodyPrefixInspector.Inspect(image, f.Directory); Require(ReferenceEquals(p.Image, image) && ReferenceEquals(p.Directory, f.Directory));
            var copy = p.Image.CopySlice(0, image.Length); copy[0] = 0; Require(p.Image.CopySlice(0, image.Length).SequenceEqual(original));
            try { ((IList)p.Streamed).Clear(); throw new Exception("Mutable stream records"); } catch (NotSupportedException) { }
            try { ((IList)p.ActiveMods[0].Members).Clear(); throw new Exception("Mutable label members"); } catch (NotSupportedException) { }
            var other = new Fixture(o => o.Streams.Clear()); var q = Parse(other); Require(q.Streamed.Count == 0 && p.Streamed.Count == 1);
            SavedBodyPrefixReport? partial = null;
            Reject(() => partial = Parse(other, body: CopyWith(other, "sim-length", -1))); Require(partial == null);
            Require(Parse(other).KsavOffset == q.KsavOffset);
        });
        Test("constructor caps are finite positive and cannot be widened", () =>
        {
            foreach (var create in new Func<SavedPrefixLimits>[] { () => new(maximumLabels: 0), () => new(maximumStreamEntries: 4097),
                () => new(maximumSimBytes: (512 << 20) + 1), () => new(maximumDecodedStringBytes: -1) })
            { try { _ = create(); throw new Exception("Invalid prefix budget accepted"); } catch (ArgumentOutOfRangeException) { } }
        });
        return groups;
    }

    private static SavedBodyPrefixReport Parse(Fixture f, SavedPrefixLimits? limits = null, byte[]? body = null)
        => SavedBodyPrefixInspector.Inspect(SavedImage.CopyFrom(body ?? f.Body), f.Directory, limits);
    private static void Denials(SavedBodyPrefixReport p)
    { Require(p.BodyPrefixFramingComplete && !p.NativeGridValidityProven && !p.NativeSimValidityProven && !p.WholeFilePreflightProven &&
        !p.GeneralDescendantCoverageProven && !p.NativeLoadPermitted && !p.OwnedRestorePermitted && !p.WriterPermission && !p.QuarantineActivated); }
    private static void Require(bool condition) { if (!condition) throw new Exception("Prefix assertion failed"); }
    private static SavedInspectionException Reject(Action action, SavedInspectionFailureKind? kind = null)
    {
        try { action(); } catch (SavedInspectionException e)
        { Require(kind == null || e.Kind == kind); Require(e.Offset >= 0 && e.RecordPath.Length > 0); return e; }
        throw new Exception("Malformed/unsupported prefix produced a report");
    }
    private static byte[] Bytes(Action<BinaryWriter> write)
    { using var s = new MemoryStream(); using (var w = new BinaryWriter(s, Encoding.UTF8, true)) write(w); return s.ToArray(); }
    private static void WireString(BinaryWriter w, string value) { byte[] bytes = Encoding.UTF8.GetBytes(value); w.Write(bytes.Length); w.Write(bytes); }
    private static void Template(BinaryWriter w, string type, string[] members, Func<string, byte[]> encoding, bool properties = false)
    { WireString(w, type); w.Write(properties ? 0 : members.Length); w.Write(properties ? members.Length : 0); foreach (string member in members) { WireString(w, member); w.Write(encoding(member)); } }
    private static byte[] CopyWith(Fixture f, string field, int value) { var bytes = (byte[])f.Body.Clone(); Put32(bytes, f.At[field], value); return bytes; }
    private static void Put32(byte[] bytes, int offset, int value)
    { Require(BitConverter.IsLittleEndian); Array.Copy(BitConverter.GetBytes(value), 0, bytes, offset, 4); }
    private static int Get32(byte[] bytes, int offset) { Require(BitConverter.IsLittleEndian); return BitConverter.ToInt32(bytes, offset); }
}
