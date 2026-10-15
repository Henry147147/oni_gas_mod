using System.Collections;
using System.Text;
using MixedAtmospheres.Oni;

internal static partial class Program
{
    private static void RegisterLayoutChecks()
    {
        Add("independent stored block and Adler writer has fixed RFC controls", () =>
        {
            Check(LayoutFixture.IndependentAdler(Encoding.ASCII.GetBytes("Wikipedia")) == 0x11e60398);
            Check(LayoutFixture.StoredZlib(Array.Empty<byte>()).SequenceEqual(new byte[] { 0x78, 1, 1, 0, 0, 255, 255, 0, 0, 0, 1 }));
        });
        Add("complete ordinary image derives every section from one immutable source", () =>
        {
            var fixture = new LayoutFixture(); var report = Parse(fixture); Agreement(fixture, report);
            Check(report.Header.Inspection.Kind == OwnedReservoirLoadInputKind.Unmarked && !report.Header.Inspection.MustReject);
        });
        Add("small supported empty tables still retain all authority denials", () =>
        { var fixture = new LayoutFixture(o => o.Minimal = true); Agreement(fixture, Parse(fixture)); });
        Add("recognized owned declaration has structural evidence and no restore authority", () =>
        {
            var fixture = new LayoutFixture(o => o.Owned = true); var report = Parse(fixture); Agreement(fixture, report);
            Check(report.Header.Inspection.Kind == OwnedReservoirLoadInputKind.OwnedHeader && report.Header.Inspection.MustReject &&
                report.Header.Inspection.ReservedNamespaceObserved && report.Header.Inspection.Declaration!.NativeColonyId.ToString("N") == LayoutFixture.NativeN);
            Check(report.Objects.Groups[0].Roots.All(r => r.Markers.Single().State == SavedMarkerState.Unassigned));
        });
        Add("reversed saved field and JSON order is consumed by its own exact schema", () =>
        {
            foreach (bool owned in new[] { false, true })
            { var fixture = new LayoutFixture(o => { o.ReverseMembers = true; o.ReverseJson = true; o.Owned = owned; }); Agreement(fixture, Parse(fixture)); }
        });
        Add("JSON length counts UTF8 bytes rather than UTF16 chars", () =>
        {
            string json = LayoutFixture.Json(false); var fixture = new LayoutFixture();
            Check(fixture.JsonBytes == Encoding.UTF8.GetByteCount(json) && fixture.JsonBytes > json.Length);
            Agreement(fixture, Parse(fixture)); Reject(Put32(fixture.File, 4, json.Length));
        });
        Add("manual multi-stored-block stream preserves every byte and exact block count", () =>
        { var fixture = new LayoutFixture(o => o.StoredBlockBytes = 37); Check(fixture.StoredBlocks > 20); Agreement(fixture, Parse(fixture)); });
        Add("caller arrays and exported bytes cannot mutate pinned source or body", () =>
        {
            var fixture = new LayoutFixture(); var caller = (byte[])fixture.File.Clone(); var image = SavedImage.CopyFrom(caller);
            Array.Fill(caller, (byte)0); var report = SavedColonyLayoutInspector.Inspect(image); Agreement(fixture, report);
            var body = report.Inflation.BodyImage.CopySlice(0, fixture.Body.Length); Array.Fill(body, (byte)255);
            var header = report.Header.Inspection.CopyHeaderBytes(); Array.Fill(header, (byte)255);
            Check(report.Inflation.BodyImage.HashSlice(0, fixture.Body.Length) == Sha(fixture.Body) && report.Header.Inspection.CopyHeaderBytes().SequenceEqual(fixture.Header));
        });
        Add("descriptor group component and prefix collections reject caller mutation", () =>
        {
            var report = Parse(new LayoutFixture());
            Throws<NotSupportedException>(() => ((IDictionary)report.Directory.Templates).Clear());
            foreach (IList list in new IList[] { (IList)report.Objects.Groups, (IList)report.Objects.AllRecognizedRoots,
                (IList)report.Objects.Groups[0].Roots[0].Components, (IList)report.Prefix.ActiveMods,
                (IList)report.Directory.Templates[LayoutFixture.RootName].Members }) Throws<NotSupportedException>(() => list.Clear());
        });
        Add("parallel whole-chain calls share only immutable input and have private results", () =>
        {
            var fixture = new LayoutFixture(); var image = SavedImage.CopyFrom(fixture.File);
            var tasks = Enumerable.Range(0, 12).Select(_ => Task.Run(() => SavedColonyLayoutInspector.Inspect(image))).ToArray(); Task.WaitAll(tasks);
            foreach (var task in tasks) { Check(ReferenceEquals(task.Result.SourceImage, image)); Agreement(fixture, task.Result); }
            Check(tasks.Select(t => t.Result.Inflation.BodyImage).Distinct().Count() == 12 && image.HashSlice(0, image.Length) == Sha(fixture.File));
        });
        Add("opaque Sim custom and terminal tail bytes are preserved without magic search", () =>
        {
            var fixture = new LayoutFixture(); var report = Parse(fixture); Agreement(fixture, report);
            Check(report.Prefix.Image.CopySlice(report.Prefix.SimPayload.Offset, report.Prefix.SimPayload.Length).SequenceEqual(fixture.Options.Sim));
            Check(report.Objects.Image.CopySlice(report.Objects.UntouchedTail.Offset, report.Objects.UntouchedTail.Length).SequenceEqual(fixture.Options.Tail));
            Check(report.Objects.Groups[1].Roots[0].Components.Single().TypeName == "Never.Resolve.This, Missing.Assembly" && report.Objects.AllRecognizedRoots.Count == 5);
        });
        Add("assigned marker generations and native packets are diagnostics never activation", () =>
        {
            var fixture = new LayoutFixture(o => { o.Owned = true; o.AssignedMarker = true; }); var report = Parse(fixture); Agreement(fixture, report);
            Check(report.Objects.Groups[0].Roots.All(r => r.Markers.Single().State == SavedMarkerState.Assigned && r.Markers.Single().Generation == 1L << 40));
            Check(report.Objects.SemanticIssues.Any(s => s.Contains("native Storage packets", StringComparison.Ordinal)));
        });
        Add("every small ordinary complete-file truncation rejects atomically", () =>
        {
            var fixture = new LayoutFixture(o => o.Minimal = true);
            for (int length = 0; length < fixture.File.Length; ++length) Reject(fixture.File.Take(length).ToArray());
        });
        Add("every small owned complete-file truncation rejects atomically", () =>
        {
            var fixture = new LayoutFixture(o => { o.Minimal = true; o.Owned = true; });
            for (int length = 0; length < fixture.File.Length; ++length) Reject(fixture.File.Take(length).ToArray());
        });
        Add("appended compressed-range bytes cannot become an opaque file suffix", () =>
        {
            foreach (bool owned in new[] { false, true })
            {
                var fixture = new LayoutFixture(o => o.Owned = owned);
                foreach (byte[] suffix in new[] { new byte[] { 0 }, new byte[] { 255 }, new byte[1024] })
                    Reject(fixture.File.Concat(suffix).ToArray(), SavedInspectionFailureKind.Malformed, path: "zlib");
            }
        });
        Add("concatenated zlib streams reject rather than choosing one body", () =>
        {
            var fixture = new LayoutFixture();
            Reject(fixture.File.Concat(fixture.Zlib).ToArray(), SavedInspectionFailureKind.Malformed, path: "zlib");
            Reject(fixture.File.Concat(LayoutFixture.StoredZlib(Array.Empty<byte>())).ToArray(), SavedInspectionFailureKind.Malformed, path: "zlib");
        });
        Add("pinned binary build header-version and compression are explicit unsupported inputs", () =>
        {
            foreach (var configure in new Action<LayoutFixtureOptions>[] { o => o.Build = 0, o => o.Build = 744824, o => o.HeaderVersion = 0,
                o => o.HeaderVersion = 2, o => o.Compression = 0, o => o.Compression = 2 })
                Reject(new LayoutFixture(configure).File, SavedInspectionFailureKind.Unsupported, path: "header");
        });
        Add("zero negative huge and off-by-one JSON claims never derive a directory", () =>
        {
            var fixture = new LayoutFixture();
            foreach (int length in new[] { -1, 0, int.MaxValue, fixture.JsonBytes - 1, fixture.JsonBytes + 1 }) Reject(Put32(fixture.File, 4, length));
        });
        Add("object version mismatch is unsupported while wrong types are malformed", () =>
        {
            foreach (string json in new[] { LayoutFixture.Json(false).Replace(":7,", ":6,", StringComparison.Ordinal), LayoutFixture.Json(false).Replace(":38", ":37", StringComparison.Ordinal) })
                Reject(new LayoutFixture(o => o.Json = json).File, SavedInspectionFailureKind.Unsupported, path: "header");
            foreach (string json in new[] { LayoutFixture.Json(false).Replace(":7,", ":7.0,", StringComparison.Ordinal), LayoutFixture.Json(false).Replace(":38", ":\"38\"", StringComparison.Ordinal), "{}" })
                Reject(new LayoutFixture(o => o.Json = json).File, SavedInspectionFailureKind.Malformed, path: "header");
        });
        Add("native colony Guid is canonical nonempty D text", () =>
        {
            foreach (string replacement in new[] { "00000000-0000-0000-0000-000000000000", LayoutFixture.NativeN, "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", " " + LayoutFixture.NativeD })
                Reject(new LayoutFixture(o => o.Json = LayoutFixture.Json(false).Replace(LayoutFixture.NativeD, replacement, StringComparison.Ordinal)).File, SavedInspectionFailureKind.Malformed, path: "header");
            Reject(new LayoutFixture(o => o.Json = LayoutFixture.Json(false).Replace("\"" + LayoutFixture.NativeD + "\"", "123", StringComparison.Ordinal)).File, SavedInspectionFailureKind.Malformed);
        });
        Add("strict JSON and UTF8 reject duplicates truncation comments and invalid bytes", () =>
        {
            foreach (string json in new[] { "{", "[]", "null", LayoutFixture.Json(false) + "{}", "/*x*/" + LayoutFixture.Json(false),
                LayoutFixture.Json(false).Replace("\"saveMajorVersion\":7", "\"saveMajorVersion\":7,\"saveMajorVersion\":7", StringComparison.Ordinal) })
                Reject(new LayoutFixture(o => o.Json = json).File, SavedInspectionFailureKind.Malformed, path: "header");
            var fixture = new LayoutFixture(); Reject(PutByte(fixture.File, 17, 255), SavedInspectionFailureKind.Malformed, path: "header");
        });
        Add("escaped exact owned namespace is recognized without granting permission", () =>
        {
            var fixture = new LayoutFixture(o => o.Json = LayoutFixture.Json(true).Replace("mixedAtmospheresOwnedReservoirs", "\\u006dixedAtmospheresOwnedReservoirs", StringComparison.Ordinal));
            var report = Parse(fixture); Agreement(fixture, report); Check(report.Header.Inspection.Kind == OwnedReservoirLoadInputKind.OwnedHeader && report.Header.Inspection.MustReject);
        });
        Add("malformed owned declaration never downgrades to ordinary layout", () =>
        {
            foreach (var change in new[] { ("\"formatVersion\":1", "\"formatVersion\":99"), ("\"ownerCount\":2", "\"ownerCount\":1"),
                ("\"ownerCount\":2,", ""), ("mixedAtmospheresOwnedReservoirs", "MixedAtmospheresOwnedReservoirs"),
                ("\"checkpointSha256\":\"" + new string('a', 64), "\"checkpointSha256\":\"" + new string('A', 64)),
                (LayoutFixture.NativeN, "22222222222233334444555555555555") })
                Reject(new LayoutFixture(o => o.Json = LayoutFixture.Json(true).Replace(change.Item1, change.Item2, StringComparison.Ordinal)).File, SavedInspectionFailureKind.Malformed, path: "header");
        });
        Add("fixed header JSON complexity guards remain active in the complete chain", () =>
        {
            foreach (string extra in new[] { new string('[', 34) + "0" + new string(']', 34), "[" + string.Join(',', Enumerable.Repeat("0", 65536)) + "]" })
            {
                string json = LayoutFixture.Json(false); json = json[..^1] + ",\"oversized\":" + extra + "}";
                Reject(new LayoutFixture(o => o.Json = json).File, SavedInspectionFailureKind.Malformed, path: "header");
            }
        });
        Add("directory negative impossible and duplicate template counts are bounded", () =>
        {
            var fixture = new LayoutFixture();
            foreach (int count in new[] { -1, int.MaxValue }) Reject(fixture.Redirectory(Put32(fixture.Directory, 0, count)));
            var first = fixture.DirectoryRanges["template/" + LayoutFixture.RootName];
            var duplicate = Put32(fixture.Directory, 0, 8).Concat(fixture.Directory.AsSpan(first.Offset, first.Length).ToArray()).ToArray();
            Reject(fixture.Redirectory(duplicate), SavedInspectionFailureKind.Malformed, path: "directory");
        });
        Add("directory names require valid UTF8 and descriptors retain exact flags and arity", () =>
        {
            var fixture = new LayoutFixture();
            Reject(fixture.Redirectory(PutByte(fixture.Directory, fixture.DirectoryRanges["name/" + LayoutFixture.RootName].Offset + 4, 255)), SavedInspectionFailureKind.Malformed, path: "directory");
            Reject(fixture.Redirectory(PutByte(fixture.Directory, fixture.DirectoryRanges["descriptor/" + LayoutFixture.RootName + "/WidthInCells"].Offset, 70)), SavedInspectionFailureKind.Unsupported, path: "directory");
            Reject(fixture.Redirectory(PutByte(fixture.Directory, fixture.DirectoryRanges["descriptor/" + LayoutFixture.RootName + "/streamed"].Offset + 1, 1)), SavedInspectionFailureKind.Unsupported, path: "directory");
        });
        Add("count-derived empty directory cannot pretend required schemas exist", () =>
        { var fixture = new LayoutFixture(); Reject(fixture.Redirectory(Wire.Make(w => w.I32(0)).Bytes), SavedInspectionFailureKind.Unsupported, path: "body-prefix"); });
        Add("missing compressed fragment rejects after a fully framed directory", () =>
        { var fixture = new LayoutFixture(); Reject(fixture.Header.Concat(fixture.Directory).ToArray(), SavedInspectionFailureKind.Malformed, path: "layout"); });
        Add("stored length complement final block checksum and zlib headers are enforced", () =>
        {
            var fixture = new LayoutFixture();
            foreach (var change in new[] { (0, (byte)0x79), (1, (byte)0), (2, (byte)7), (5, (byte)(fixture.Zlib[5] ^ 1)), (fixture.Zlib.Length - 1, (byte)(fixture.Zlib[^1] ^ 1)) })
                Reject(fixture.Header.Concat(fixture.Directory).Concat(PutByte(fixture.Zlib, change.Item1, change.Item2)).ToArray(), path: "zlib");
        });
        Add("fixed DEFLATE reference before any output fails at its history guard", () =>
        {
            var fixture = new LayoutFixture();
            // BFINAL=1/BTYPE=01, fixed length257 (3), distance0 (1), EOB256.
            // No literal precedes that reference. Trailer is empty Adler=1.
            byte[] impossible = { 0x78, 1, 0x03, 0x02, 0, 0, 0, 0, 1 };
            SavedColonyLayoutReport? report = null;
            try { report = SavedColonyLayoutInspector.Inspect(SavedImage.CopyFrom(fixture.Header.Concat(fixture.Directory).Concat(impossible).ToArray())); }
            catch (SavedInspectionException error)
            { Check(report is null && error.Kind == SavedInspectionFailureKind.Malformed && error.RecordPath == "zlib" && error.Message.Contains("available output history", StringComparison.Ordinal)); return; }
            throw new InvalidOperationException("Invalid fixed reference escaped the structural guard");
        });
        Add("wrong prefix marker cannot use a later literal as fallback", () =>
        {
            var fixture = new LayoutFixture();
            Reject(fixture.Rebody(PutByte(fixture.Body, fixture.BodyRanges["world"].Offset + 4, (byte)'x')), SavedInspectionFailureKind.Unsupported, path: "body-prefix");
            Reject(fixture.Rebody(PutByte(fixture.Body, fixture.BodyRanges["root-marker"].Offset + 4, (byte)'x')), SavedInspectionFailureKind.Unsupported, path: "body-prefix");
        });
        Add("streamed claims nulls counts duplicate keys and value lengths are closed", () =>
        {
            var fixture = new LayoutFixture(); int at = fixture.BodyRanges["root/streamed"].Offset;
            foreach (var mutation in new[] { (at, -1), (at + 4, -2), (at + 4, int.MaxValue), (at, 1), (fixture.BodyRanges["streamed-payload/value/0"].Offset, 4) })
                Reject(fixture.Rebody(Put32(fixture.Body, mutation.Item1, mutation.Item2)), path: "body-prefix");
            var bad = (byte[])fixture.Body.Clone(); Encoding.UTF8.GetBytes("alpha").CopyTo(bad, fixture.BodyRanges["streamed-payload/key/1"].Offset + 4);
            Reject(fixture.Rebody(bad), SavedInspectionFailureKind.Malformed, path: "body-prefix");
        });
        Add("legacy required mods and noncanonical mod-list claims reject", () =>
        {
            var fixture = new LayoutFixture(); int required = fixture.BodyRanges["root/requiredMods"].Offset, mods = fixture.BodyRanges["root/active_mods"].Offset;
            Reject(fixture.Rebody(Put32(fixture.Body, required + 4, 0)), SavedInspectionFailureKind.Unsupported, path: "body-prefix");
            foreach (var mutation in new[] { (required, 1), (mods, -1), (mods, 0), (mods + 4, -1) }) Reject(fixture.Rebody(Put32(fixture.Body, mutation.Item1, mutation.Item2)), path: "body-prefix");
        });
        Add("inline Sim count is opaque but its one derived endpoint is bounded", () =>
        {
            var fixture = new LayoutFixture();
            foreach (int count in new[] { -1, int.MaxValue, fixture.Options.Sim.Length + 1 })
                Reject(fixture.Rebody(Put32(fixture.Body, fixture.BodyRanges["sim-length"].Offset, count)), path: "body-prefix");
        });
        Add("KSAV exact computed magic version and counts reject without searches", () =>
        {
            var fixture = new LayoutFixture(); int at = fixture.BodyRanges["ksav"].Offset;
            Reject(fixture.Rebody(PutByte(fixture.Body, at, 0)), SavedInspectionFailureKind.Malformed, path: "body-prefix");
            Reject(fixture.Rebody(Put32(fixture.Body, at + 4, 8)), SavedInspectionFailureKind.Unsupported, path: "body-prefix");
            Reject(fixture.Rebody(Put32(fixture.Body, at + 12, -1)), SavedInspectionFailureKind.Malformed, path: "body-prefix");
        });
        Add("late malformed root framing prevents a report after earlier valid owners", () =>
        {
            var fixture = new LayoutFixture(o => { o.Owned = true; o.AssignedMarker = true; });
            int at = fixture.BodyRanges["unknown-group/component-length/0"].Offset;
            Reject(fixture.Rebody(Put32(fixture.Body, at, int.MaxValue)), SavedInspectionFailureKind.Malformed);
        });
        Add("duplicate KSAV groups are malformed even when both payloads are empty", () =>
        {
            var fixture = new LayoutFixture(); var ksav = Wire.Make(w =>
            { w.Raw(new byte[] { 75, 83, 65, 86 }); w.I32(7); w.I32(38); w.I32(2); for (int i = 0; i < 2; ++i) { w.Text("x"); w.I32(0); w.I32(0); } });
            var body = fixture.Body.Take(fixture.BodyRanges["ksav"].Offset).Concat(ksav.Bytes).ToArray();
            Reject(fixture.Rebody(body), SavedInspectionFailureKind.Malformed, path: "KSAV");
        });
        Add("recognized Storage Boolean and saved payload closure remain strict", () =>
        {
            var fixture = new LayoutFixture(); int at = fixture.BodyRanges["reservoir-group/owner-a/component/2"].Offset;
            Reject(fixture.Rebody(PutByte(fixture.Body, at, 2)), SavedInspectionFailureKind.Malformed);
            Reject(fixture.Rebody(Put32(fixture.Body, at + 10, -1)), SavedInspectionFailureKind.Malformed);
        });
        Add("null layout input and direct header helper limits reject by contract", () =>
        {
            Throws<ArgumentNullException>(() => SavedColonyLayoutInspector.Inspect(null!));
            Throws<InvalidDataException>(() => OwnedReservoirLoadHeaderV1.InspectLayoutHeader(ReadOnlySpan<byte>.Empty));
            Throws<ArgumentOutOfRangeException>(() => new SavedLayoutLimits(maximumHeaderJsonBytes: 0));
        });
    }
}
