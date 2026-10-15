using System.Text;

// Original synthetic writer. It never calls an inspector, decoder, game API,
// native observation, DeflateStream or another test's fixture implementation.
internal readonly record struct WireRange(int Offset, int Length) { internal int End => checked(Offset + Length); }
internal sealed record WirePiece(byte[] Bytes, Dictionary<string, WireRange> Ranges);

internal sealed class Wire
{
    private readonly List<byte> bytes = new();
    internal readonly Dictionary<string, WireRange> Ranges = new(StringComparer.Ordinal);
    internal int Position => bytes.Count;
    internal int StringBytes { get; private set; }
    internal void U8(byte v) => bytes.Add(v);
    internal void U16(int v) { U8((byte)v); U8((byte)(v >> 8)); }
    internal void I32(int v) => U32(unchecked((uint)v));
    internal void U32(uint v) { for (int i = 0; i < 4; ++i) U8((byte)(v >> (8 * i))); }
    internal void I64(long v) { for (int i = 0; i < 8; ++i) U8((byte)(unchecked((ulong)v) >> (8 * i))); }
    internal void Raw(byte[] data) => bytes.AddRange(data);
    internal void Mark(string name, int start) => Ranges.Add(name, new WireRange(start, Position - start));
    internal void Part(string name, Action<Wire> write) { int at = Position; write(this); Mark(name, at); }
    internal void Text(string? value)
    {
        if (value is null) { I32(-1); return; }
        byte[] data = new UTF8Encoding(false, true).GetBytes(value);
        I32(data.Length); Raw(data); StringBytes += data.Length;
    }
    internal void Child(string name, WirePiece child)
    {
        int at = Position; Raw(child.Bytes); Mark(name, at);
        foreach (var row in child.Ranges) Ranges.Add(name + "/" + row.Key, new WireRange(at + row.Value.Offset, row.Value.Length));
    }
    internal WirePiece Finish() => new(bytes.ToArray(), new Dictionary<string, WireRange>(Ranges, StringComparer.Ordinal));
    internal static WirePiece Make(Action<Wire> write) { var w = new Wire(); write(w); return w.Finish(); }
}

internal sealed class LayoutFixtureOptions
{
    internal bool Owned, ReverseMembers, ReverseJson, Minimal, AssignedMarker;
    internal int StoredBlockBytes = 65535;
    internal string? Json;
    internal uint Build = 744825, HeaderVersion = 1;
    internal int Compression = 1;
    internal byte[] Sim = { 0, 75, 83, 65, 86, 77, 65, 67, 79, 76, 79, 78, 89, 255, 17, 0, 9 };
    internal byte[] Tail = { 75, 83, 65, 86, 0, 255, 120, 1 };
}

internal sealed class LayoutFixture
{
    internal const string NativeD = "11111111-2222-3333-4444-555555555555";
    internal const string NativeN = "11111111222233334444555555555555";
    internal const string MarkerName = "MixedAtmospheres.Oni.OwnedReservoirOwnerMarker";
    internal const string RootName = "Klei.SaveFileRoot", SettingsName = "Game+Settings", LabelName = "KMod.Label";
    internal static readonly string[] RootNames = { "WidthInCells", "HeightInCells", "streamed", "clusterID", "requiredMods", "active_mods" };
    internal static readonly string[] LabelNames = { "distribution_platform", "id", "title", "version" };
    internal static readonly string[] StorageNames = { "onlyFetchMarkedItems", "shouldSaveItems", "workTimeRemaining", "numberOfUses" };
    internal static readonly string[] MarkerNames = { "schemaVersion", "nativeColonyId", "persistentOwnerToken", "coreColonyId", "containerId", "generation", "allocationToken" };
    internal readonly LayoutFixtureOptions Options;
    internal readonly byte[] File, Header, Directory, Body, Zlib;
    internal readonly WireRange HeaderRange, DirectoryRange, CompressedRange;
    internal readonly Dictionary<string, WireRange> BodyRanges, DirectoryRanges;
    internal readonly int JsonBytes, PrefixStringBytes, StoredBlocks;
    internal readonly uint Adler;

    internal LayoutFixture(Action<LayoutFixtureOptions>? configure = null)
    {
        Options = new LayoutFixtureOptions(); configure?.Invoke(Options);
        string json = Options.Json ?? Json(Options.Owned, Options.ReverseJson);
        byte[] text = new UTF8Encoding(false, true).GetBytes(json); JsonBytes = text.Length;
        Header = Wire.Make(w => { w.U32(Options.Build); w.I32(text.Length); w.U32(Options.HeaderVersion); w.I32(Options.Compression); w.Raw(text); }).Bytes;
        var directory = DirectoryBytes(); Directory = directory.Bytes; DirectoryRanges = directory.Ranges;
        var body = BodyBytes(); Body = body.piece.Bytes; BodyRanges = body.piece.Ranges; PrefixStringBytes = body.strings;
        Zlib = StoredZlib(Body, Options.StoredBlockBytes); Adler = IndependentAdler(Body);
        StoredBlocks = Math.Max(1, (Body.Length + Options.StoredBlockBytes - 1) / Options.StoredBlockBytes);
        HeaderRange = new WireRange(0, Header.Length); DirectoryRange = new WireRange(Header.Length, Directory.Length);
        CompressedRange = new WireRange(DirectoryRange.End, Zlib.Length);
        File = Header.Concat(Directory).Concat(Zlib).ToArray();
    }

    internal byte[] Rebody(byte[] body) => Header.Concat(Directory).Concat(StoredZlib(body, Options.StoredBlockBytes)).ToArray();
    internal byte[] Redirectory(byte[] directory) => Header.Concat(directory).Concat(Zlib).ToArray();
    internal static string Json(bool owned, bool reverse = false)
    {
        var fields = new List<string> { "\"baseName\":\"synthetic 量💨 KSAV\"", "\"colonyGuid\":\"" + NativeD + "\"", "\"saveMajorVersion\":7", "\"saveMinorVersion\":38" };
        if (owned) fields.Add("\"mixedAtmospheresOwnedReservoirs\":{" + string.Join(',', new[] {
            "\"formatVersion\":1", "\"scope\":\"two-gas-reservoirs-idle\"", "\"oniBuild\":744825", "\"nativeColonyId\":\""+NativeN+"\"", "\"ownerCount\":2",
            "\"checkpointFormatVersion\":1", "\"checkpointSha256\":\""+new string('a',64)+"\"", "\"envelopeFormatVersion\":1", "\"envelopeSha256\":\""+new string('b',64)+"\"",
            "\"coreFormatVersion\":3", "\"materialModelVersion\":1", "\"catalogVersion\":\"original-synthetic\"", "\"catalogFingerprint\":\""+new string('c',64)+"\"",
            "\"assemblyCSharpSha256\":\"a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599\"",
            "\"assemblyCSharpFirstpassSha256\":\"72d21f51cda136443bfa1a2f6eadb08167a4c508c3d03f558b8cdb60160f249c\"" }) + "}");
        if (reverse) fields.Reverse();
        return "{" + string.Join(',', fields) + "}";
    }

    private string[] Order(IEnumerable<string> names) => (Options.ReverseMembers ? names.Reverse() : names).ToArray();
    private static byte[] Primitive(byte code) => new[] { code };
    private static byte[] Named(byte code, string name) => Wire.Make(w => { w.U8(code); w.Text(name); }).Bytes;
    private static byte[] Generic(byte code, params byte[][] children) => Wire.Make(w => { w.U8(code); w.U8((byte)children.Length); foreach (var child in children) w.Raw(child); }).Bytes;
    private WirePiece DirectoryBytes()
    {
        var templates = new List<(string Name, (string Name, byte[] Type)[] Fields)>
        {
            (RootName, Order(RootNames).Select(name => (name, name switch {
                "WidthInCells" or "HeightInCells" => Primitive(6), "clusterID" => Primitive(12),
                "streamed" => Generic(147, Primitive(12), new byte[] { 17, 2 }),
                "requiredMods" => Generic(148, Named(64, "ModInfo")), _ => Generic(148, Named(64, LabelName)) })).ToArray()),
            (SettingsName, Order(new[] { "nextUniqueID", "gameID" }).Select(name => (name, Primitive(6))).ToArray()),
            (LabelName, Order(LabelNames).Select(name => (name, name == "distribution_platform" ? Named(13, "KMod.Label+DistributionPlatform") : Primitive(name == "version" ? (byte)8 : (byte)12))).ToArray()),
            (MarkerName, Order(MarkerNames).Select(name => (name, Primitive(name == "schemaVersion" ? (byte)6 : name == "generation" ? (byte)8 : (byte)12))).ToArray()),
            ("Storage", Order(StorageNames).Select(name => (name, Primitive(name is "onlyFetchMarkedItems" or "shouldSaveItems" ? (byte)3 : name == "workTimeRemaining" ? (byte)10 : (byte)6))).ToArray()),
            ("KPrefabID", Order(new[] { "InstanceID", "serializedTags" }).Select(name => (name, name == "InstanceID" ? Primitive(6) : Generic(149, Named(64, "Tag")))).ToArray()),
            ("Tag", Order(new[] { "name", "hash" }).Select(name => (name, Primitive(name == "name" ? (byte)12 : (byte)6))).ToArray())
        };
        return Wire.Make(w =>
        {
            w.Part("count", x => x.I32(templates.Count));
            foreach (var template in templates) w.Part("template/" + template.Name, x =>
            {
                x.Part("name/" + template.Name, z => z.Text(template.Name)); x.I32(template.Fields.Length); x.I32(0);
                foreach (var field in template.Fields)
                {
                    x.Text(field.Name); x.Part("descriptor/" + template.Name + "/" + field.Name, z => z.Raw(field.Type));
                }
            });
        });
    }

    private (WirePiece piece, int strings) BodyBytes()
    {
        int strings = 0;
        var result = Wire.Make(w =>
        {
            w.Part("world", x => x.Text("world")); w.Part("root-marker", x => x.Text(RootName));
            w.Part("root-members", x =>
            {
                foreach (string member in Order(RootNames)) x.Part("root/" + member, z =>
                {
                    switch (member)
                    {
                        case "WidthInCells": z.I32(73); break;
                        case "HeightInCells": z.I32(9); break;
                        case "clusterID": z.Text("cluster 名🔬"); break;
                        case "requiredMods": z.I32(0); z.I32(-1); break;
                        case "streamed": WriteStreamed(z); break;
                        case "active_mods": WriteLabels(z); break;
                    }
                });
            });
            w.Part("settings-marker", x => x.Text(SettingsName));
            w.Part("settings-members", x => { foreach (var name in Order(new[] { "nextUniqueID", "gameID" })) x.Part("settings/" + name, z => z.I32(name == "nextUniqueID" ? 19 : -23)); });
            strings = w.StringBytes;
            byte[] sim = Options.Minimal ? Array.Empty<byte>() : Options.Sim;
            w.Part("sim-length", x => x.I32(sim.Length)); w.Part("sim", x => x.Raw(sim));
            w.Part("ksav", WriteKsav);
            w.Part("tail", x => x.Raw(Options.Minimal ? Array.Empty<byte>() : Options.Tail));
        });
        return (result, strings);
    }

    private void WriteStreamed(Wire w)
    {
        (string Key, byte[] Data)[] entries = Options.Minimal ? Array.Empty<(string Key, byte[] Data)>() : new[] { ("alpha", new byte[] { 9, 75, 83, 65, 86 }), ("βeta", new byte[] { 255, 0, 127 }) };
        var payload = Wire.Make(p =>
        {
            for (int i = 0; i < entries.Length; ++i) p.Part("value/" + i, x => { x.I32(entries[i].Data.Length); x.I32(entries[i].Data.Length); x.Raw(entries[i].Data); });
            for (int i = 0; i < entries.Length; ++i) p.Part("key/" + i, x => x.Text(entries[i].Key));
        });
        w.I32(payload.Bytes.Length); w.I32(entries.Length); w.Child("streamed-payload", payload);
        // Child strings belong to this prefix even though length-framing was
        // built independently. Accumulate only the known keys, not a decoder.
        foreach (var entry in entries) prefixChildStringBytes += Encoding.UTF8.GetByteCount(entry.Key);
    }
    private int prefixChildStringBytes;
    private void WriteLabels(Wire w)
    {
        int count = Options.Minimal ? 0 : 2;
        var payload = Wire.Make(p =>
        {
            for (int i = 0; i < count; ++i) p.Part("label/" + i, x =>
            {
                foreach (string member in Order(LabelNames)) x.Part("label/" + i + "/" + member, z =>
                {
                    switch (member)
                    {
                        case "distribution_platform": z.I32(i == 0 ? 4 : -3); break;
                        case "id": z.Text(i == 0 ? "fixture.α" : null); break;
                        case "title": z.Text(i == 0 ? "标题💨" : null); break;
                        case "version": z.I64((1L << 40) + i); break;
                    }
                });
            });
        });
        w.I32(payload.Bytes.Length); w.I32(count); w.Child("labels-payload", payload);
        if (!Options.Minimal) prefixChildStringBytes += Encoding.UTF8.GetByteCount("fixture.α") + Encoding.UTF8.GetByteCount("标题💨");
    }
    private void WriteKsav(Wire w)
    {
        w.Raw(new byte[] { 75, 83, 65, 86 }); w.I32(7); w.I32(38); w.I32(Options.Minimal ? 0 : 2);
        if (Options.Minimal) return;
        var reservoir = Wire.Make(p => { p.Child("owner-a", Root(0, true)); p.Child("owner-b", Root(1, false)); });
        w.Text("GasReservoir"); w.I32(2); w.I32(reservoir.Bytes.Length); w.Child("reservoir-group", reservoir);
        var other = Root(9, false, true);
        w.Text("UnresolvedPrefab"); w.I32(1); w.I32(other.Bytes.Length); w.Child("unknown-group", other);
    }
    private WirePiece Root(int identity, bool nested, bool unknown = false, int depth = 0)
    {
        var parts = unknown
            ? new[] { (Name: "Never.Resolve.This, Missing.Assembly", Payload: Wire.Make(w => w.Raw(new byte[] { 75, 83, 65, 86, 255, 0, 17 }))) }
            : depth > 0
                ? depth == 1 ? new[] { (Name: "Storage", Payload: Storage(identity, true, depth)) }
                    : new[] { (Name: "OpaquePacket", Payload: Wire.Make(w => w.Raw(new byte[] { 77, 65, 67, 79, 76, 79, 78, 89 }))) }
                : new[] { (Name: MarkerName, Payload: Marker(identity)), (Name: "KPrefabID", Payload: Identity(identity)),
                    (Name: "Storage", Payload: Storage(identity, nested, depth)), (Name: "OpaqueCustom", Payload: Wire.Make(w => w.Raw(new byte[] { 75, 83, 65, 86, 0, 255, 120, 1 }))) };
        return Wire.Make(w =>
        {
            for (int i = 0; i < 10; ++i) w.U32(0x7fc00000u + (uint)i); w.U8(255); w.I32(parts.Length);
            for (int i = 0; i < parts.Length; ++i) { w.Text(parts[i].Name); w.Part("component-length/" + i, x => x.I32(parts[i].Payload.Bytes.Length)); w.Child("component/" + i, parts[i].Payload); }
        });
    }
    private WirePiece Marker(int id) => Wire.Make(w =>
    {
        foreach (var name in Order(MarkerNames))
            switch (name)
            {
                case "schemaVersion": w.I32(Options.AssignedMarker ? 1 : 0); break;
                case "generation": w.I64(Options.AssignedMarker ? 1L << 40 : 0); break;
                case "nativeColonyId": w.Text(Options.AssignedMarker ? NativeN : ""); break;
                case "persistentOwnerToken": w.Text(Options.AssignedMarker ? (id + 2).ToString("x32") : ""); break;
                case "coreColonyId": w.Text(Options.AssignedMarker ? (7).ToString("x32") : ""); break;
                case "containerId": w.Text(Options.AssignedMarker ? "container-" + id : ""); break;
                case "allocationToken": w.Text(Options.AssignedMarker ? (id + 17).ToString("x32") : ""); break;
            }
    });
    private WirePiece Identity(int id) => Wire.Make(w =>
    {
        foreach (var name in Order(new[] { "InstanceID", "serializedTags" }))
        {
            if (name == "InstanceID") w.I32(id);
            else
            {
                var tags = Wire.Make(p => { for (int i = 0; i < 2; ++i) foreach (var member in Order(new[] { "name", "hash" })) { if (member == "name") p.Text(i == 0 ? "tag 名" : null); else p.I32(i == 0 ? 123 : -5); } });
                w.I32(tags.Bytes.Length); w.I32(2); w.Child("tags", tags);
            }
        }
    });
    private WirePiece Storage(int id, bool nested, int depth) => Wire.Make(w =>
    {
        foreach (var name in Order(StorageNames))
            switch (name) { case "onlyFetchMarkedItems": w.U8(0); break; case "shouldSaveItems": w.U8(1); break; case "workTimeRemaining": w.U32(0x7f800000); break; case "numberOfUses": w.I32(7); break; }
        w.I32(nested ? 1 : 0);
        if (nested) { w.Text(depth == 0 ? "Oxygen" : "TracePacket"); w.Child("packet", Root(id + 20, depth == 0, depth: depth + 1)); }
    });

    internal static uint IndependentAdler(byte[] bytes)
    {
        uint a = 1, b = 0;
        foreach (byte value in bytes) { a = (a + value) % 65521; b = (b + a) % 65521; }
        return b << 16 | a;
    }
    internal static byte[] StoredZlib(byte[] plain, int blockBytes = 65535)
    {
        if (blockBytes <= 0 || blockBytes > 65535) throw new ArgumentOutOfRangeException(nameof(blockBytes));
        return Wire.Make(w =>
        {
            w.U8(0x78); w.U8(0x01); // RFC1950, 32KiB, no dictionary, FCHECK.
            int at = 0;
            do
            {
                int length = Math.Min(blockBytes, plain.Length - at); bool final = at + length == plain.Length;
                w.U8(final ? (byte)1 : (byte)0); // BFINAL then BTYPE=00; zero padding.
                w.U16(length); w.U16(length ^ 65535); w.Raw(plain.AsSpan(at, length).ToArray()); at += length;
            } while (at < plain.Length);
            uint adler = IndependentAdler(plain);
            for (int i = 3; i >= 0; --i) w.U8((byte)(adler >> (8 * i)));
        }).Bytes;
    }
    internal int ExpectedPrefixStringBytes => PrefixStringBytes + prefixChildStringBytes;
}
