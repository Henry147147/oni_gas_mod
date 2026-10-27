using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

// The managed game contract (docs/game-update-resilience.md), outside the game.
//   generate: write game-contract.json for one built mod assembly against
//             licensed installations (--raw keeps full values for diffs);
//   verify:   check a contract against an installation, as the game does;
//   controls: synthetic changes to a contract must each be refused.
// The mod's own GameContractCheck runs inside an isolated load context with
// the installation's assemblies: reflection only, no game code invoked. The
// output holds member names, signatures and hashes, never game code.
static class Program
{
    static int Main(string[] args)
    {
        if (args.Length == 0) return Usage();
        string? modDirectory = null, output = null, contract = null; var raw = false;
        var games = new List<(string platform, string root)>();
        for (var i = 1; i < args.Length; ++i)
        {
            string Next() => ++i < args.Length ? args[i] : throw new ArgumentException("Missing value for " + args[i - 1]);
            switch (args[i])
            {
                case "--mod": modDirectory = Next(); break;
                case "--output": output = Next(); break;
                case "--contract": contract = Next(); break;
                case "--raw": raw = true; break;
                case "--game":
                    var value = Next(); var split = value.IndexOf('=');
                    if (split <= 0) throw new ArgumentException("--game expects windows-x64=DIR or linux-x64=DIR");
                    games.Add((value[..split], value[(split + 1)..])); break;
                default: throw new ArgumentException("Unknown argument " + args[i]);
            }
        }
        if (modDirectory == null || games.Count == 0 || games.Any(g => g.platform is not ("windows-x64" or "linux-x64")) ||
            games.Select(g => g.platform).Distinct().Count() != games.Count) return Usage();
        var modPath = Path.GetFullPath(Path.Combine(modDirectory, "MixedAtmospheres.Oni.dll"));
        switch (args[0])
        {
            case "generate" when output != null: return Generate(modPath, games, output, raw);
            case "verify" when contract != null: return Verify(modPath, games, contract, controls: false);
            case "controls" when contract != null: return Verify(modPath, games, contract, controls: true);
            default: return Usage();
        }
    }

    static int Usage()
    {
        Console.Error.WriteLine("usage: GameContract generate --mod DIR --game windows-x64=ROOT [--game linux-x64=ROOT] --output FILE [--raw]\n" +
                                "       GameContract verify|controls --mod DIR --game PLATFORM=ROOT --contract FILE");
        return 2;
    }

    sealed record Host(Assembly Mod, Type Check, int Build, string AssemblyHash);

    static Host Load(string modPath, string platform, string root)
    {
        var managed = Path.GetFullPath(Path.Combine(root, "OxygenNotIncluded_Data", "Managed"));
        var gameAssembly = Path.Combine(managed, "Assembly-CSharp.dll");
        if (!File.Exists(gameAssembly)) throw new FileNotFoundException("Not an ONI installation", gameAssembly);
        // Not collectible: Harmony generates code that collectible contexts refuse.
        var context = new AssemblyLoadContext("oni-" + platform);
        context.Resolving += (loader, name) =>
        {
            var simple = name.Name ?? "";
            if (simple is "mscorlib" or "netstandard" || simple == "System" || simple.StartsWith("System.", StringComparison.Ordinal) ||
                simple.StartsWith("Microsoft.", StringComparison.Ordinal)) return null;
            var candidate = simple.StartsWith("MixedAtmospheres.", StringComparison.Ordinal)
                ? Path.Combine(Path.GetDirectoryName(modPath)!, simple + ".dll") : Path.Combine(managed, simple + ".dll");
            return File.Exists(candidate) ? loader.LoadFromAssemblyPath(candidate) : null;
        };
        var game = context.LoadFromAssemblyPath(gameAssembly);
        var build = Convert.ToInt32(game.GetType("KleiVersion", true)!.GetField("ChangeList")!.GetRawConstantValue());
        var mod = context.LoadFromAssemblyPath(modPath);
        using var stream = File.OpenRead(gameAssembly);
        return new Host(mod, mod.GetType("MixedAtmospheres.Oni.GameContractCheck", true)!, build, Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant());
    }

    static object? Call(Type type, string name, params object?[] arguments)
    {
        try { return type.GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, arguments); }
        catch (TargetInvocationException error) { Console.Error.WriteLine((error.InnerException ?? error).ToString()); throw error.InnerException ?? error; }
    }

    static int Generate(string modPath, List<(string platform, string root)> games, string output, bool raw)
    {
        var file = new JsonObject { ["SchemaVersion"] = 1, ["ModuleVersionId"] = "", ["Platforms"] = new JsonObject() };
        if (games.Count > 1)
        {
            // One installation per process: reflection-by-name searches every
            // loaded assembly, so two games in one process could mix.
            foreach (var (platform, root) in games.OrderBy(g => g.platform, StringComparer.Ordinal))
            {
                var part = output + "." + platform + ".part";
                var self = Environment.ProcessPath!;
                var start = new ProcessStartInfo(self) { UseShellExecute = false };
                if (Path.GetFileNameWithoutExtension(self) == "dotnet") start.ArgumentList.Add(Assembly.GetEntryAssembly()!.Location);
                foreach (var argument in new[] { "generate", "--mod", Path.GetDirectoryName(modPath)!, "--game", platform + "=" + root, "--output", part }) start.ArgumentList.Add(argument);
                if (raw) start.ArgumentList.Add("--raw");
                using (var child = Process.Start(start)!) { child.WaitForExit(); if (child.ExitCode != 0) return child.ExitCode; }
                var single = JsonNode.Parse(File.ReadAllText(part))!;
                File.Delete(part);
                file["ModuleVersionId"] = (string)single["ModuleVersionId"]!;
                file["Platforms"]![platform] = single["Platforms"]![platform]!.DeepClone();
            }
            File.WriteAllText(output, file.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
            return 0;
        }
        foreach (var (platform, root) in games)
        {
            var host = Load(modPath, platform, root);
            var node = JsonNode.Parse((string)Call(host.Check, "Generate", host.Mod, host.Build, host.AssemblyHash, raw)!)!;
            file["ModuleVersionId"] = host.Mod.ManifestModule.ModuleVersionId.ToString("D");
            file["Platforms"]![platform] = node;
            var entries = node["Entries"]!.AsArray();
            Console.WriteLine($"{platform}: build {host.Build}, {entries.Count} entries (" +
                string.Join(", ", entries.GroupBy(e => (string)e!["Kind"]!).Select(g => g.Count() + " " + g.Key)) +
                $"); {node["Unverified"]!.AsArray().Count} reflection sites outside the contract");
        }
        File.WriteAllText(output, file.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
        return 0;
    }

    // Builds the mod's GameContractPlatform inside the load context from JSON.
    static object Platform(Host host, JsonNode node)
    {
        var platformType = host.Mod.GetType("MixedAtmospheres.Oni.GameContractPlatform", true)!;
        var entryType = host.Mod.GetType("MixedAtmospheres.Oni.GameContractEntry", true)!;
        var platform = Activator.CreateInstance(platformType, true)!;
        platformType.GetField("GameBuild")!.SetValue(platform, (int)node["GameBuild"]!);
        var entries = (System.Collections.IList)platformType.GetField("Entries")!.GetValue(platform)!;
        foreach (var item in node["Entries"]!.AsArray())
        {
            var entry = Activator.CreateInstance(entryType, true)!;
            foreach (var name in new[] { "Kind", "TypeName", "Name", "Key", "Value" }) entryType.GetField(name)!.SetValue(entry, (string)item![name]!);
            foreach (var name in new[] { "Token", "Context" }) entryType.GetField(name)!.SetValue(entry, (int)item![name]!);
            entries.Add(entry);
        }
        return platform;
    }

    static List<string> Differences(Host host, JsonNode platform, out int checkedEntries, out double milliseconds)
    {
        var arguments = new object?[] { Platform(host, platform), host.Mod, 0 };
        var timer = Stopwatch.StartNew();
        var differences = ((IEnumerable<string>)Call(host.Check, "Verify", arguments)!).ToList();
        milliseconds = timer.Elapsed.TotalMilliseconds; checkedEntries = (int)arguments[2]!;
        return differences;
    }

    static int Verify(string modPath, List<(string platform, string root)> games, string contractPath, bool controls)
    {
        var file = JsonNode.Parse(File.ReadAllText(contractPath))!;
        var failures = 0;
        if (games.Count != 1) { Console.Error.WriteLine("verify and controls take one installation per run"); return 2; }
        foreach (var (platform, root) in games)
        {
            var host = Load(modPath, platform, root);
            if ((string)file["ModuleVersionId"]! != host.Mod.ManifestModule.ModuleVersionId.ToString("D"))
            { Console.WriteLine($"{platform}: the contract belongs to a different mod assembly"); ++failures; continue; }
            var node = file["Platforms"]?[platform] ?? throw new InvalidDataException("No contract for " + platform);
            var differences = Differences(host, node, out var count, out var ms);
            var timings = (System.Collections.Generic.Dictionary<string, double>)host.Check.GetField("Timings", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
            Console.WriteLine($"{platform}: build {host.Build}, {count} dependencies checked in {ms:F0} ms (" +
                string.Join(", ", timings.Select(t => $"{t.Key} {t.Value:F0} ms")) + $"), {differences.Count} differences");
            foreach (var difference in differences.Take(50)) Console.WriteLine("  " + difference);
            if (differences.Count != 0) { ++failures; continue; }
            if (controls) failures += Controls(host, platform, node);
        }
        return failures == 0 ? 0 : 1;
    }

    // Each synthetic change to an unchanged build's contract must be refused,
    // naming the changed entry; the unchanged contract passes (checked above).
    static int Controls(Host host, string platform, JsonNode original)
    {
        var failures = 0; var cases = 0;
        void Case(string name, Func<JsonNode, string?> mutate)
        {
            var copy = JsonNode.Parse(original.ToJsonString())!;
            var expected = mutate(copy);
            if (expected == null) return;
            ++cases;
            var differences = Differences(host, copy, out _, out _);
            var refused = differences.Any(d => d.Contains(expected, StringComparison.Ordinal));
            if (!refused) { ++failures; Console.WriteLine($"  FAILED {name}: expected a difference naming {expected}; got {differences.Count}: {string.Join(" | ", differences.Take(3))}"); }
        }
        JsonObject First(JsonNode copy, string kind, Func<JsonObject, bool>? where = null) =>
            copy["Entries"]!.AsArray().Select(e => e!.AsObject()).First(e => (string)e["Kind"]! == kind && (where == null || where(e)));
        foreach (var kind in new[] { "type", "member", "named", "patched" })
        {
            Case("changed " + kind + " value", copy => { var e = First(copy, kind); e["Value"] = "sha256:" + new string('0', 64); return (string)e["Key"]!; });
            if (kind != "patched")
                Case("changed " + kind + " key", copy => { var e = First(copy, kind); var key = (string)e["Key"]!; e["Key"] = key + "#renamed"; return key + "#renamed"; });
        }
        Case("changed layout", copy => { var e = First(copy, "type", x => ((string)x["Key"]!).EndsWith("Sim/Cell", StringComparison.Ordinal)); e["Value"] = ((string)e["Value"]!) + "x"; return "Sim/Cell"; });
        Case("changed enum value", copy => { var e = First(copy, "type", x => ((string)x["Key"]!) == "SimHashes"); e["Value"] = "sha256:" + new string('1', 64); return "SimHashes"; });
        Case("changed method body", copy => { var e = First(copy, "member", x => ((string)x["Value"]!).Length == 64); e["Value"] = new string('2', 64); return (string)e["Key"]!; });
        Case("missing member", copy => { var e = First(copy, "member"); var other = First(copy, "member", x => (int)x["Token"]! != (int)e["Token"]! && (string)x["Key"]! != (string)e["Key"]!);
            e["Token"] = (int)other["Token"]!; e["Context"] = (int)other["Context"]!; return (string)e["Key"]! + " -> "; });
        Case("absent named member", copy => { var e = First(copy, "named", x => ((string)x["Key"]!).Contains("::", StringComparison.Ordinal) && !((string)x["Key"]!).EndsWith("::*", StringComparison.Ordinal));
            e["Name"] = (string)e["Name"]! + "Removed"; return (string)e["Key"]!; });
        Case("unexpected patch target", copy => { var entries = copy["Entries"]!.AsArray(); var e = First(copy, "patched"); var key = (string)e["Key"]!; entries.Remove(e); return "new patch target " + key; });
        Case("vanished patch target", copy => { copy["Entries"]!.AsArray().Add(new JsonObject { ["Kind"] = "patched", ["Token"] = 0, ["Context"] = 0, ["TypeName"] = "", ["Name"] = "",
            ["Key"] = "static Game::RemovedMethod()System.Void", ["Value"] = "sha256:" + new string('3', 64) }); return "missing patch target static Game::RemovedMethod()System.Void"; });
        Console.WriteLine($"{platform}: {cases - failures}/{cases} synthetic contract changes refused");
        return failures;
    }
}
