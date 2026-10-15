using System.Text;
using MixedAtmospheres.Oni;

internal static partial class Program
{
    private static void Limited(LayoutFixture fixture, SavedLayoutLimits limits, string? path = null)
        => Reject(fixture.File, SavedInspectionFailureKind.LimitExceeded, limits, path);
    private static void RegisterLimitChecks()
    {
        Add("layout source byte cap rejects one below the complete source", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(maximumSourceBytes: f.File.Length - 1), "layout"); Agreement(f, Parse(f, new SavedLayoutLimits(maximumSourceBytes: f.File.Length))); });
        Add("layout header JSON cap uses exact independently counted bytes", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(maximumHeaderJsonBytes: f.JsonBytes - 1), "header"); Agreement(f, Parse(f, new SavedLayoutLimits(maximumHeaderJsonBytes: f.JsonBytes))); });
        Add("directory and object image cap is honored on the original source", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumImageBytes: f.File.Length - 1)), "directory"); });
        Add("directory consumed byte cap is separate from physical EOF", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumDirectoryBytes: f.Directory.Length - 1)), "directory"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumDirectoryBytes: f.Directory.Length)))); });
        Add("directory maximum templates is cumulative and caller bounded", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumTemplates: 6)), "directory"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumTemplates: 7)))); });
        Add("directory maximum members counts all seven templates", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumMembers: 26)), "directory"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumMembers: 27)))); });
        Add("KSAV maximum groups is applied after derived inflation and prefix", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumGroups: 1)), "KSAV"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumGroups: 2)))); });
        Add("KSAV maximum roots includes both Storage descendants", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumRoots: 4))); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumRoots: 5)))); });
        Add("KSAV maximum components per root applies to four framed components", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumComponentsPerRoot: 3))); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumComponentsPerRoot: 4)))); });
        Add("KSAV maximum components charges top-level and nested records together", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumComponents: 10))); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumComponents: 11)))); });
        Add("Storage maximum depth counts the grandchild without flattening", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumStorageDepth: 1))); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumStorageDepth: 2)))); });
        Add("directory maximum descriptor depth includes dictionary array nesting", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumDescriptorDepth: 2)), "directory"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumDescriptorDepth: 3)))); });
        Add("directory maximum descriptor nodes counts every generic argument", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumDescriptorNodes: 32)), "directory"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumDescriptorNodes: 33)))); });
        Add("directory type names obey exact UTF8 byte bounds", () =>
        { var f = new LayoutFixture(); int n = Encoding.UTF8.GetByteCount(LayoutFixture.MarkerName); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumTypeNameBytes: n - 1)), "directory"); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumTypeNameBytes: n)))); });
        Add("directory member names obey the largest independently authored member", () =>
        {
            var f = new LayoutFixture(); int n = LayoutFixture.RootNames.Concat(LayoutFixture.LabelNames).Concat(LayoutFixture.StorageNames).Concat(LayoutFixture.MarkerNames)
                .Concat(new[] { "nextUniqueID", "gameID", "InstanceID", "serializedTags", "name", "hash" }).Max(s => Encoding.UTF8.GetByteCount(s));
            Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumMemberNameBytes: n - 1)), "directory");
            Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumMemberNameBytes: n))));
        });
        Add("KSAV value string cap preserves positive Unicode tag bytes", () =>
        { var f = new LayoutFixture(); int n = Encoding.UTF8.GetByteCount("tag 名"); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumValueStringBytes: n - 1))); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumValueStringBytes: n)))); });
        Add("KSAV tag cap counts both unowned identity records", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumTags: 3))); Agreement(f, Parse(f, new SavedLayoutLimits(directoryAndObjects: new SavedInspectionLimits(maximumTags: 4)))); });
        Add("body prefix image cap applies to the derived inflated body", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumImageBytes: f.Body.Length - 1)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumImageBytes: f.Body.Length)))); });
        Add("body prefix byte budget ends at metadata and excludes opaque Sim bytes", () =>
        { var f = new LayoutFixture(); int n = f.BodyRanges["sim-length"].End; Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumPrefixBytes: n - 1)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumPrefixBytes: n)))); });
        Add("streamed entry count budget rejects two with a limit of one", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStreamEntries: 1)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStreamEntries: 2)))); });
        Add("active mod label count budget rejects two with a limit of one", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumLabels: 1)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumLabels: 2)))); });
        Add("individual streamed byte array cap counts only its known payload", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStreamValueBytes: 4)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStreamValueBytes: 5)))); });
        Add("total streamed byte budget charges five and three rather than max", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStreamValueTotalBytes: 7)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStreamValueTotalBytes: 8)))); });
        Add("individual prefix string byte cap remains caller controlled", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumStringBytes: 1)), "body-prefix"); });
        Add("decoded prefix string budget counts all encoded keys labels and markers", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumDecodedStringBytes: f.ExpectedPrefixStringBytes - 1)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumDecodedStringBytes: f.ExpectedPrefixStringBytes)))); });
        Add("inline Sim byte cap does not deserialize or certify its arbitrary contents", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumSimBytes: f.Options.Sim.Length - 1)), "body-prefix"); Agreement(f, Parse(f, new SavedLayoutLimits(prefix: new SavedPrefixLimits(maximumSimBytes: f.Options.Sim.Length)))); });
        Add("inflation source-image cap remains independent of the layout source cap", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumImageBytes: f.File.Length - 1)), "zlib"); Agreement(f, Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumImageBytes: f.File.Length)))); });
        Add("inflation compressed byte cap uses the exact derived remaining range", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumCompressedBytes: f.Zlib.Length - 1)), "zlib"); Agreement(f, Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumCompressedBytes: f.Zlib.Length)))); });
        Add("inflation output cap must allow all independently written body bytes", () =>
        { var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumOutputBytes: f.Body.Length - 1)), "zlib"); Agreement(f, Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumOutputBytes: f.Body.Length)))); });
        Add("decoder chunk sizes change only bounded read granularity not accepted bytes", () =>
        {
            var f = new LayoutFixture(); foreach (int chunk in new[] { 1, 2, 3, 65536 }) Agreement(f, Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(chunkBytes: chunk))));
        });
        Add("decoder read-call cap includes the actual EOF probe", () =>
        {
            var f = new LayoutFixture(); Limited(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(chunkBytes: 1, maximumReadCalls: f.Body.Length)), "zlib");
            var report = Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(chunkBytes: 1, maximumReadCalls: f.Body.Length + 1)));
            Agreement(f, report); Check(report.Inflation.ReadCalls == f.Body.Length + 1);
        });
        Add("structural block budget counts every manual stored block", () =>
        { var f = new LayoutFixture(o => o.StoredBlockBytes = 37); Limited(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumStructureBlocks: f.StoredBlocks - 1)), "zlib"); Agreement(f, Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumStructureBlocks: f.StoredBlocks)))); });
        Add("structural step budget charges each manual stored block before acceptance", () =>
        { var f = new LayoutFixture(o => o.StoredBlockBytes = 37); Limited(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumStructureSteps: f.StoredBlocks - 1)), "zlib"); Agreement(f, Parse(f, new SavedLayoutLimits(inflation: new SavedInflationLimits(maximumStructureSteps: f.StoredBlocks)))); });
        Add("fixed maximum header JSON bytes cannot be raised by layout callers", () =>
        {
            string json = LayoutFixture.Json(false); json += new string(' ', (1 << 20) + 1 - Encoding.UTF8.GetByteCount(json));
            Reject(new LayoutFixture(o => o.Json = json).File, SavedInspectionFailureKind.LimitExceeded, path: "header");
        });
        Add("all thirty-three configured scalar limit constructors reject invalid bounds", () =>
        {
            var limits = new (Func<int, object> Make, int Maximum)[]
            {
                (x => new SavedLayoutLimits(maximumSourceBytes:x),1<<30),(x => new SavedLayoutLimits(maximumHeaderJsonBytes:x),1<<20),
                (x => new SavedInspectionLimits(maximumImageBytes:x),1<<30),(x => new SavedInspectionLimits(maximumDirectoryBytes:x),64<<20),
                (x => new SavedInspectionLimits(maximumTemplates:x),65536),(x => new SavedInspectionLimits(maximumMembers:x),65536),
                (x => new SavedInspectionLimits(maximumGroups:x),65536),(x => new SavedInspectionLimits(maximumRoots:x),262144),
                (x => new SavedInspectionLimits(maximumComponentsPerRoot:x),4096),(x => new SavedInspectionLimits(maximumComponents:x),1048576),
                (x => new SavedInspectionLimits(maximumStorageDepth:x),64),(x => new SavedInspectionLimits(maximumDescriptorDepth:x),32),
                (x => new SavedInspectionLimits(maximumDescriptorNodes:x),262144),(x => new SavedInspectionLimits(maximumTypeNameBytes:x),16384),
                (x => new SavedInspectionLimits(maximumMemberNameBytes:x),4096),(x => new SavedInspectionLimits(maximumValueStringBytes:x),1<<20),
                (x => new SavedInspectionLimits(maximumTags:x),1048576),
                (x => new SavedPrefixLimits(maximumImageBytes:x),1<<30),(x => new SavedPrefixLimits(maximumPrefixBytes:x),64<<20),
                (x => new SavedPrefixLimits(maximumStreamEntries:x),4096),(x => new SavedPrefixLimits(maximumLabels:x),4096),
                (x => new SavedPrefixLimits(maximumStreamValueBytes:x),64<<20),(x => new SavedPrefixLimits(maximumStreamValueTotalBytes:x),64<<20),
                (x => new SavedPrefixLimits(maximumStringBytes:x),1<<20),(x => new SavedPrefixLimits(maximumDecodedStringBytes:x),16<<20),
                (x => new SavedPrefixLimits(maximumSimBytes:x),512<<20),
                (x => new SavedInflationLimits(maximumImageBytes:x),1<<30),(x => new SavedInflationLimits(maximumCompressedBytes:x),64<<20),
                (x => new SavedInflationLimits(maximumOutputBytes:x),256<<20),(x => new SavedInflationLimits(chunkBytes:x),65536),
                (x => new SavedInflationLimits(maximumReadCalls:x),65536),(x => new SavedInflationLimits(maximumStructureBlocks:x),65536),
                (x => new SavedInflationLimits(maximumStructureSteps:x),1<<26)
            };
            Check(limits.Length == 33);
            foreach (var limit in limits) foreach (int value in new[] { -1, 0, limit.Maximum + 1 }) Throws<ArgumentOutOfRangeException>(() => limit.Make(value));
        });
    }
}
