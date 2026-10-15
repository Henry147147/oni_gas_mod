using System.Security.Cryptography;
using System.Text.Json;
using MixedAtmospheres.Oni;

internal static partial class Program
{
    private const int ExpectedGroups = 74;
    private static readonly List<(string Name, Action Body)> Tests = new();
    private static readonly List<string> Passed = new();
    private static void Add(string name, Action action) => Tests.Add((name, action));
    internal static void Check(bool ok, string message = "Independent layout assertion failed") { if (!ok) throw new InvalidOperationException(message); }
    internal static string Sha(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
    internal static byte[] Put32(byte[] source, int at, int value)
    {
        var result = (byte[])source.Clone();
        for (int i = 0; i < 4; ++i) result[at + i] = (byte)(unchecked((uint)value) >> (8 * i));
        return result;
    }
    internal static byte[] PutByte(byte[] source, int at, byte value) { var result = (byte[])source.Clone(); result[at] = value; return result; }
    internal static void Range(SavedByteRange actual, WireRange expected)
        => Check(actual.Offset == expected.Offset && actual.Length == expected.Length && actual.End == expected.End, "Range differs from independently written bytes");
    internal static void Denials(SavedColonyLayoutReport report)
    {
        Check(report.OuterFramingDerived && !report.NativeReaderUsed && !report.NativeSimInvoked && !report.NativeSimValidityProven &&
            !report.WholeFilePreflightProven && !report.GeneralDescendantCoverageProven && !report.NativeLoadPermitted &&
            !report.OwnedRestorePermitted && !report.WriterPermission && !report.QuarantineActivated && !report.FirstPlayableColonyPassed);
        var prefix = report.Prefix;
        Check(prefix.BodyPrefixFramingComplete && !prefix.NativeGridValidityProven && !prefix.NativeSimValidityProven &&
            !prefix.WholeFilePreflightProven && !prefix.GeneralDescendantCoverageProven && !prefix.NativeLoadPermitted &&
            !prefix.OwnedRestorePermitted && !prefix.WriterPermission && !prefix.QuarantineActivated);
        Check(report.Objects.FramingComplete && report.Objects.RecognizedStorageTraversalComplete &&
            !report.Objects.GeneralDescendantCoverageProven && !report.Objects.WholeFilePreflightProven && !report.Objects.OwnedRestorePermitted);
        var z = report.Inflation;
        Check(z.DeflateFinalBlockProven && z.ExactCompressedRangeValidated && z.DecoderEofObserved && z.DeflateStructureValidated &&
            !z.DecoderSourcePositionUsedAsFramingProof && !z.NativeReaderUsed && !z.NativeSimInvoked && !z.NativeSimValidityProven &&
            !z.NativeLoadPermitted && !z.WholeFilePreflightProven && !z.OwnedRestorePermitted && !z.WriterPermission &&
            !z.QuarantineActivated && !z.FirstPlayableColonyPassed);
    }
    internal static SavedColonyLayoutReport Parse(LayoutFixture fixture, SavedLayoutLimits? limits = null)
        => SavedColonyLayoutInspector.Inspect(SavedImage.CopyFrom(fixture.File), limits);
    internal static void Reject(byte[] file, SavedInspectionFailureKind? kind = null, SavedLayoutLimits? limits = null, string? path = null)
    {
        SavedColonyLayoutReport? report = null;
        try { report = SavedColonyLayoutInspector.Inspect(SavedImage.CopyFrom(file), limits); }
        catch (SavedInspectionException error)
        {
            Check(report is null && error.Offset >= 0 && !string.IsNullOrWhiteSpace(error.RecordPath));
            if (kind.HasValue) Check(error.Kind == kind, error.ToString());
            if (path is not null) Check(error.RecordPath.StartsWith(path, StringComparison.Ordinal), error.ToString());
            return;
        }
        throw new InvalidOperationException("Expected typed layout rejection without a partial report");
    }
    internal static void Throws<T>(Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new InvalidOperationException("Expected " + typeof(T).Name); }
    internal static void Agreement(LayoutFixture fixture, SavedColonyLayoutReport report)
    {
        Denials(report);
        Range(report.Header.Range, fixture.HeaderRange); Range(report.Directory.Range, fixture.DirectoryRange); Range(report.Inflation.CompressedRange, fixture.CompressedRange);
        Check(report.Header.Range.End == report.Directory.Range.Offset && report.Directory.Range.End == report.Inflation.CompressedRange.Offset && report.Inflation.CompressedRange.End == fixture.File.Length);
        Check(ReferenceEquals(report.SourceImage, report.Directory.Image) && ReferenceEquals(report.SourceImage, report.Inflation.SourceImage));
        Check(ReferenceEquals(report.Inflation.BodyImage, report.Prefix.Image) && ReferenceEquals(report.Inflation.BodyImage, report.Objects.Image));
        Check(ReferenceEquals(report.Directory, report.Prefix.Directory) && ReferenceEquals(report.Directory, report.Objects.Directory));
        Check(report.SourceImage.CopySlice(0, fixture.File.Length).SequenceEqual(fixture.File));
        Check(report.Header.Inspection.CopyHeaderBytes().SequenceEqual(fixture.Header) && report.Header.Inspection.HeaderSha256 == Sha(fixture.Header));
        Check(report.Directory.Sha256 == Sha(fixture.Directory) && report.Inflation.BodyImage.CopySlice(0, fixture.Body.Length).SequenceEqual(fixture.Body));
        Check(report.Inflation.Adler32 == fixture.Adler && report.Inflation.TotalBytesOut == fixture.Body.Length && report.Inflation.ValidatedCompressedBytes == fixture.Zlib.Length);
        Check(report.Inflation.DeflateStructure.OutputBytes == fixture.Body.Length && report.Inflation.DeflateStructure.Blocks == fixture.StoredBlocks && report.Inflation.DeflateStructure.Steps == fixture.StoredBlocks);
        Check(report.Header.OniBuild == 744825 && report.Header.HeaderVersion == 1 && report.Header.Compression == 1 &&
            report.Header.SaveMajorVersion == 7 && report.Header.SaveMinorVersion == 38 && report.Header.NativeColonyId.ToString("D") == LayoutFixture.NativeD);
        Check(report.Directory.Templates.Count == 7);
        foreach (var template in report.Directory.Templates.Values)
        {
            var range = fixture.DirectoryRanges["template/" + template.TypeName];
            Range(template.Range, new WireRange(fixture.DirectoryRange.Offset + range.Offset, range.Length));
        }
        var b = fixture.BodyRanges; var p = report.Prefix;
        Range(p.WorldMarker, b["world"]); Range(p.SaveFileRootMarker, b["root-marker"]); Range(p.SaveFileRootMembers, b["root-members"]);
        Range(p.SettingsMarker, b["settings-marker"]); Range(p.SettingsMembers, b["settings-members"]);
        Range(p.StreamedDictionary, b["root/streamed"]); Range(p.RequiredMods, b["root/requiredMods"]); Range(p.ActiveModsList, b["root/active_mods"]);
        Range(p.SimLengthField, b["sim-length"]); Range(p.SimPayload, b["sim"]); Range(p.KsavHeader, new WireRange(b["ksav"].Offset, 16));
        Range(report.Objects.Section, b["ksav"]); Range(report.Objects.UntouchedTail, b["tail"]);
        Check(p.KsavOffset == b["ksav"].Offset && p.WidthInCells == 73 && p.HeightInCells == 9 && p.ClusterId == "cluster 名🔬" && p.NextUniqueId == 19 && p.GameId == -23);
        foreach (var member in p.Members) Range(member.Range, b[(member.TypeName == LayoutFixture.RootName ? "root/" : "settings/") + member.MemberName]);
        Check(p.Streamed.Count == (fixture.Options.Minimal ? 0 : 2) && p.ActiveMods.Count == (fixture.Options.Minimal ? 0 : 2));
        if (fixture.Options.Minimal) { Check(report.Objects.Groups.Count == 0); return; }
        for (int i = 0; i < 2; ++i)
        {
            Range(p.Streamed[i].EncodedValue, b["streamed-payload/value/" + i]); Range(p.Streamed[i].EncodedKey, b["streamed-payload/key/" + i]);
            Range(p.Streamed[i].Payload, new WireRange(b["streamed-payload/value/" + i].Offset + 8, i == 0 ? 5 : 3));
            Check(p.Streamed[i].Key == (i == 0 ? "alpha" : "βeta") && !p.Streamed[i].IsNull);
            Range(p.ActiveMods[i].Range, b["labels-payload/label/" + i]);
            foreach (var member in p.ActiveMods[i].Members) Range(member.Range, b["labels-payload/label/" + i + "/" + member.MemberName]);
            Check(p.ActiveMods[i].Version == (1L << 40) + i && p.ActiveMods[i].DistributionPlatform == (i == 0 ? 4 : -3));
        }
        Check(p.ActiveMods[0].Id == "fixture.α" && p.ActiveMods[0].Title == "标题💨" && p.ActiveMods[1].Id is null && p.ActiveMods[1].Title is null);
        Check(report.Objects.Groups.Count == 2 && report.Objects.AllRecognizedRoots.Count == 5 && report.Objects.OpaqueComponentCount == 4);
        Range(report.Objects.Groups[0].Payload, b["reservoir-group"]); Range(report.Objects.Groups[1].Payload, b["unknown-group"]);
        for (int i = 0; i < 2; ++i)
        {
            string key = "reservoir-group/owner-" + (i == 0 ? "a" : "b");
            var root = report.Objects.Groups[0].Roots[i]; Range(root.Range, b[key]);
            Check(root.PrefabIdentities.Single().InstanceId == i && root.PrefabIdentities.Single().Tags.Count == 2 &&
                root.PrefabIdentities.Single().Tags[0].Name == "tag 名" && root.PrefabIdentities.Single().Tags[1].Name is null);
            Check(root.TransformBits.Count == 10 && root.TransformBits[0] == 0x7fc00000 && root.LegacyByte == 255 && root.Components.Count == 4);
            Check(root.Storages.Single().ShouldSaveItems && !root.Storages.Single().OnlyFetchMarkedItems && root.Storages.Single().WorkTimeRemainingBits == 0x7f800000 && root.Storages.Single().NumberOfUses == 7);
            for (int j = 0; j < root.Components.Count; ++j) Range(root.Components[j].Payload, b[key + "/component/" + j]);
        }
        Check(report.Objects.Groups[0].Roots[0].Storages.Single().Packets.Single().StorageDepth == 1 &&
            report.Objects.Groups[0].Roots[0].Storages.Single().Packets.Single().Storages.Single().Packets.Single().StorageDepth == 2);
    }

    private static int Main(string[] args)
    {
        var result = new Dictionary<string, object?> { ["Passed"] = false, ["OriginalSyntheticFixtures"] = true,
            ["NativeInvoked"] = false, ["ActualGameInputs"] = false, ["WholeFilePreflightProven"] = false,
            ["OwnedRestorePermitted"] = false, ["WriterPermission"] = false, ["QuarantineActivated"] = false, ["FirstPlayableColonyPassed"] = false };
        try
        {
            Check(args.Length <= 1, "Expected optional fresh receipt path");
            RegisterLayoutChecks(); RegisterLimitChecks();
            Check(Tests.Count == ExpectedGroups, "Source-declared complete group count differs");
            foreach (var test in Tests)
            {
                try { test.Body(); } catch (Exception error) { throw new InvalidOperationException("FIRST FAILED " + test.Name, error); }
                Passed.Add(test.Name); Console.WriteLine("PASS " + test.Name);
            }
            result["Passed"] = true;
        }
        catch (Exception error) { result["Failure"] = error.ToString(); Console.Error.WriteLine(error); }
        result["Groups"] = Passed.Count; result["ExpectedGroups"] = ExpectedGroups; result["Names"] = Passed;
        string json = JsonSerializer.Serialize(result);
        if (args.Length == 1) { using var stream = new FileStream(args[0], FileMode.CreateNew); using var writer = new StreamWriter(stream); writer.WriteLine(json); }
        Console.WriteLine(json);
        return (bool)result["Passed"]! ? 0 : 1;
    }
}
