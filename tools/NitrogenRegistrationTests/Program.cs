using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text.Json;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 1 || args.Length > 2) throw new ArgumentException("Pass the licensed ONI directory and optional actual-game nitrogen-validation.json.");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        {
            var path = Path.Combine(managed, name.Name + ".dll");
            return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
        };
        return Run(args.Length == 2 ? args[1] : null);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run(string? evidence)
    {
        var tests = new (string name, System.Action test)[]
        {
            ("custom identities have stable noncolliding SDBM hashes", StableIdentities),
            ("all native enum identities and tag calls pass through unchanged", NativePassThrough),
            ("custom tag adapter resolves the original string rather than an enum integer", CustomTags),
            ("localization keys remain stable under Turkish casing", LocalizedNames),
            ("explicit YAML descriptions bypass the native culture-dependent default", DescriptionMapping),
            ("native tag bridge still exposes exactly the inspected signatures", BridgeContract)
        };
        var failed = 0;
        foreach (var (name, test) in tests)
        {
            try { test(); Console.WriteLine("PASS " + name); }
            catch (Exception error) { ++failed; Console.WriteLine("FAIL " + name + ": " + error); }
        }
        if (evidence != null)
        {
            try { RecordedGame(evidence); Console.WriteLine("PASS all three phases retain their native item identity and visible inspection in actual Unity"); }
            catch (Exception error) { ++failed; Console.WriteLine("FAIL actual Unity nitrogen evidence: " + error); }
        }
        return failed == 0 ? 0 : 1;
    }
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static (string id, SimHashes hash, int expected)[] Identities() => new[]
    {
        (OniNitrogenElements.GasId, OniNitrogenElements.Gas, -1109340048),
        (OniNitrogenElements.LiquidId, OniNitrogenElements.Liquid, 1078696428),
        (OniNitrogenElements.SolidId, OniNitrogenElements.Solid, 418600943)
    };
    private static void StableIdentities()
    {
        foreach (var (id, hash, expected) in Identities())
            Check((int)hash == expected && Hash.SDBMLower(id) == expected && !Enum.IsDefined(hash) &&
                OniNitrogenElements.Identity(hash) == id, "Native hash or stable custom identity differs: " + id);
        Check(Identities().Select(item => item.hash).Distinct().Count() == 3, "Nitrogen phases share an identity.");
    }
    private static void NativePassThrough()
    {
        foreach (var value in Enum.GetValues<SimHashes>().Append((SimHashes)int.MaxValue))
        {
            var marker = new Tag("unchanged-native-result"); var result = marker;
            Check(!OniNitrogenElements.TryIdentity(value, out var identity) && identity == "" &&
                OniNitrogenElements.Identity(value) == value.ToString() && NitrogenElementTagPatch.Prefix(value, ref result) && result == marker,
                "The native identity or tag bridge was intercepted: " + value);
        }
    }
    private static void CustomTags()
    {
        foreach (var (id, hash, _) in Identities())
        {
            var result = Tag.Invalid;
            Check(!NitrogenElementTagPatch.Prefix(hash, ref result) && result == new Tag(id) && result.ToString() == id &&
                result != new Tag(((int)hash).ToString(CultureInfo.InvariantCulture)), "Custom enum tag resolved as an integer.");
        }
    }
    private static void LocalizedNames()
    {
        var previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("tr-TR");
            OniNitrogenElements.InitializeStrings();
            foreach (var (id, _, _) in Identities())
            {
                var prefix = "STRINGS.ELEMENTS." + id.ToUpperInvariant();
                Check(((string)Strings.Get(prefix + ".NAME")).Contains("nitrogen", StringComparison.OrdinalIgnoreCase) &&
                    !string.IsNullOrWhiteSpace(Strings.Get(prefix + ".DESC")), "Native localization is missing.");
            }
        }
        finally { CultureInfo.CurrentCulture = previous; }
    }
    private static void BridgeContract()
    {
        foreach (var name in new[] { "Create", "CreateTag" })
        {
            var method = typeof(GameTagExtensions).GetMethod(name, new[] { typeof(SimHashes) });
            Check(method != null && method.IsStatic && method.ReturnType == typeof(Tag), "Native tag helper signature changed.");
        }
    }
    private static void DescriptionMapping()
    {
        var yaml = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../elements/mixed_atmospheres_nitrogen.yaml"));
        var descriptions = new Dictionary<string, string>(StringComparer.Ordinal);
        var id = "";
        // This verifies this deliberately simple original mapping; actual YAML
        // parsing/loading is independently exercised inside Unity.
        foreach (var line in File.ReadLines(yaml))
        {
            if (line.StartsWith("  - elementId: ", StringComparison.Ordinal)) id = line.Substring("  - elementId: ".Length);
            else if (line.StartsWith("    description: ", StringComparison.Ordinal)) descriptions.Add(id, line.Substring("    description: ".Length));
        }
        var previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("tr-TR");
            foreach (var (identity, _, _) in Identities())
            {
                Check(descriptions.TryGetValue(identity, out var key) && key == "STRINGS.ELEMENTS." + identity.ToUpperInvariant() + ".DESC",
                    "YAML relies on the native culture-dependent implicit description key.");
                var entry = new ElementData.ElementEntry { elementId = identity, description = key };
                Check(entry.description == key && !string.IsNullOrWhiteSpace(Strings.Get(entry.description)), "The installed native entry did not preserve the explicit description.");
            }
        }
        finally { CultureInfo.CurrentCulture = previous; }
    }
    private static void RecordedGame(string file)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(file));
        var root = document.RootElement;
        Check(!root.GetProperty("MixturePhasePhysicsValidated").GetBoolean(), "Native element test misrepresents mixture phase physics.");
        var rows = root.GetProperty("Evidence").EnumerateArray().ToArray();
        Check(rows.Length == 3, "Missing native nitrogen phases.");
        foreach (var (id, _, expected) in Identities())
        {
            var row = rows.Single(item => item.GetProperty("ElementId").GetString() == id);
            Check(row.GetProperty("Hash").GetInt32() == expected && row.GetProperty("Tag").GetString() == id &&
                row.GetProperty("Passed").GetBoolean(), "A native nitrogen phase failed actual game inspection.");
        }
    }
}
