using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

// The managed game contract (docs/game-update-resilience.md), outside the game.
//   generate: write game-contract.json for one built mod assembly against
//             licensed installations (--raw keeps full values for diffs);
//   verify:   check a contract against an installation, as the game does
//             (a newer installation as the package root checks it: entries that
//             differ only by the build number are listed as forgiven);
//   controls: synthetic changes to a contract must each be refused.
// The mod's own GameContractCheck runs inside an isolated load context with
// the installation's assemblies: reflection only, no game code invoked. The
// output holds member names, signatures and hashes, never game code.
static class Program
{
    static int Main(string[] args)
    {
        if (args.Length == 0) return Usage();
        string? modDirectory = null, output = null, contract = null, packageVersion = null, modAssembly = null, filesOf = null; var raw = false; var byKey = false;
        var games = new List<(string platform, string root)>();
        for (var i = 1; i < args.Length; ++i)
        {
            string Next() => ++i < args.Length ? args[i] : throw new ArgumentException("Missing value for " + args[i - 1]);
            switch (args[i])
            {
                case "--mod": modDirectory = Next(); break;
                case "--output": output = Next(); break;
                case "--contract": contract = Next(); break;
                case "--package-version": packageVersion = Next(); break;
                case "--raw": raw = true; break;
                // Moono's way (docs/game-update-resilience.md#moono): entries by
                // key, and the game's Assembly-CSharp(-firstpass) pinned only by
                // their files when the game's own files match the contract.
                case "--by-key": byKey = true; break;
                // Another copy of MixedAtmospheres.Oni.dll to check (a rewritten
                // one, as Moono's loader emits it); the contract stays --mod's.
                case "--mod-assembly": modAssembly = Next(); break;
                // With --by-key: the installation whose files the loaded game
                // assemblies report as theirs (Moono loads rewritten images of
                // unchanged files; Assembly.Location stays the file's).
                case "--files-of": filesOf = Next(); break;
                case "--game":
                    var value = Next(); var split = value.IndexOf('=');
                    if (split <= 0) throw new ArgumentException("--game expects windows-x64=DIR or linux-x64=DIR");
                    games.Add((value[..split], value[(split + 1)..])); break;
                default: throw new ArgumentException("Unknown argument " + args[i]);
            }
        }
        if (modDirectory == null || games.Count == 0 || games.Any(g => g.platform is not ("windows-x64" or "linux-x64")) ||
            games.Select(g => g.platform).Distinct().Count() != games.Count) return Usage();
        var modPath = Path.GetFullPath(modAssembly ?? Path.Combine(modDirectory, "MixedAtmospheres.Oni.dll"));
        switch (args[0])
        {
            case "generate" when output != null: return Generate(modPath, games, output, raw, packageVersion);
            case "verify" when contract != null: return Verify(modPath, games, contract, controls: false, byKey, filesOf);
            case "controls" when contract != null: return Verify(modPath, games, contract, controls: true, byKey, filesOf);
            default: return Usage();
        }
    }

    static int Usage()
    {
        Console.Error.WriteLine("usage: GameContract generate --mod DIR --game windows-x64=ROOT [--game linux-x64=ROOT] --output FILE [--raw]\n" +
                                "       GameContract verify|controls --mod DIR --game PLATFORM=ROOT --contract FILE [--by-key [--files-of ROOT]] [--mod-assembly DLL]");
        return 2;
    }

    sealed record Host(Assembly Mod, Type Check, int Build, string AssemblyHash, string FirstpassHash, Assembly Game, Assembly? Firstpass);

    static Host Load(string modPath, string platform, string root)
    {
        var managed = Path.GetFullPath(Path.Combine(root, "OxygenNotIncluded_Data", "Managed"));
        var gameAssembly = Path.Combine(managed, "Assembly-CSharp.dll");
        if (!File.Exists(gameAssembly)) throw new FileNotFoundException("Not an ONI installation", gameAssembly);
        // Not collectible: Harmony generates code that collectible contexts refuse.
        var context = new AssemblyLoadContext("oni-" + platform);
        var modDirectory = Path.GetDirectoryName(modPath)!;
        context.Resolving += (loader, name) =>
        {
            var simple = name.Name ?? "";
            if (simple is "mscorlib" or "netstandard" || simple == "System" || simple.StartsWith("System.", StringComparison.Ordinal) ||
                simple.StartsWith("Microsoft.", StringComparison.Ordinal)) return null;
            var candidate = simple.StartsWith("MixedAtmospheres.", StringComparison.Ordinal)
                ? Path.Combine(modDirectory, simple + ".dll") : Path.Combine(managed, simple + ".dll");
            return File.Exists(candidate) ? loader.LoadFromAssemblyPath(candidate) : null;
        };
        var game = context.LoadFromAssemblyPath(gameAssembly);
        var build = Convert.ToInt32(game.GetType("KleiVersion", true)!.GetField("ChangeList")!.GetRawConstantValue());
        var mod = context.LoadFromAssemblyPath(modPath);
        var firstpass = Path.Combine(managed, "Assembly-CSharp-firstpass.dll");
        var firstpassAssembly = File.Exists(firstpass) ? context.LoadFromAssemblyName(new AssemblyName("Assembly-CSharp-firstpass")) : null;
        return new Host(mod, mod.GetType("MixedAtmospheres.Oni.GameContractCheck", true)!, build, FileHash(gameAssembly), File.Exists(firstpass) ? FileHash(firstpass) : "", game, firstpassAssembly);
    }

    static string FileHash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
    }

    static object? Call(Type type, string name, params object?[] arguments)
    {
        try { return type.GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, arguments); }
        catch (TargetInvocationException error) { Console.Error.WriteLine((error.InnerException ?? error).ToString()); throw error.InnerException ?? error; }
    }

    static int Generate(string modPath, List<(string platform, string root)> games, string output, bool raw, string? packageVersion)
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
                if (packageVersion != null) { start.ArgumentList.Add("--package-version"); start.ArgumentList.Add(packageVersion); }
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
            if (packageVersion != null)
            {
                var compiled = host.Mod.GetType("MixedAtmospheres.Oni.GameBuildCheck")?.GetField("CompiledBuild", BindingFlags.Static | BindingFlags.NonPublic)?.GetRawConstantValue();
                var version = host.Mod.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
                if (!Equals(compiled, host.Build) || version != packageVersion)
                    throw new InvalidDataException($"Adapter targets game {compiled}, mod {version}. Packaging requires game {host.Build}, mod {packageVersion}.");
            }
            var node = JsonNode.Parse((string)Call(host.Check, "Generate", host.Mod, host.Build, host.AssemblyHash, host.FirstpassHash, raw)!)!;
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
        foreach (var name in new[] { "AssemblyCSharpSha256", "FirstpassSha256" }) platformType.GetField(name)!.SetValue(platform, (string?)node[name] ?? "");
        var entries = (System.Collections.IList)platformType.GetField("Entries")!.GetValue(platform)!;
        foreach (var item in node["Entries"]!.AsArray())
        {
            var entry = Activator.CreateInstance(entryType, true)!;
            foreach (var name in new[] { "Kind", "TypeName", "Name", "Key", "Value" }) entryType.GetField(name)!.SetValue(entry, (string)item![name]!);
            entryType.GetField("Shape")!.SetValue(entry, (string?)item!["Shape"] ?? "");
            // Contracts written before tolerated newer builds have no build-number reading.
            entryType.GetField("BuildMasked")!.SetValue(entry, (string?)item!["BuildMasked"] ?? "");
            foreach (var name in new[] { "Token", "Context" }) entryType.GetField(name)!.SetValue(entry, (int)item![name]!);
            entries.Add(entry);
        }
        return platform;
    }

    // Moono's mode (--by-key): the mod's VerifyMode with ByKey and, when the
    // installation's game files match the contract's hashes, those two
    // assemblies pinned (the tolerances Mod.OnLoad applies on Moono).
    static object? Mode(Host host, JsonNode platform, bool byKey, string? filesOf)
    {
        if (!byKey) return null;
        var managed = filesOf == null ? null : Path.Combine(filesOf, "OxygenNotIncluded_Data", "Managed");
        var gameHash = managed == null ? host.AssemblyHash : FileHash(Path.Combine(managed, "Assembly-CSharp.dll"));
        var firstpassHash = managed == null ? host.FirstpassHash : FileHash(Path.Combine(managed, "Assembly-CSharp-firstpass.dll"));
        var modeType = host.Mod.GetType("MixedAtmospheres.Oni.GameContractCheck+VerifyMode", true)!;
        var mode = Activator.CreateInstance(modeType, true)!;
        modeType.GetField("ByKey", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(mode, true);
        if (host.Build > (int)platform["GameBuild"]!)
        {
            // A newer game: the key path with the build-number reading and
            // none of Moono's tolerances, which need the contract build's files
            // (in the game, Moono refuses a newer build by its number).
            Console.WriteLine("  by key, newer than the contract's build: the build-number reading, no Moono tolerances");
            return mode;
        }
        var pinned = new HashSet<Assembly>();
        if ((string?)platform["AssemblyCSharpSha256"] == gameHash) pinned.Add(host.Game);
        if (host.Firstpass != null && (string?)platform["FirstpassSha256"] == firstpassHash) pinned.Add(host.Firstpass);
        modeType.GetField("Pinned", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(mode, new Func<Assembly, bool>(pinned.Contains));
        Console.WriteLine($"  by key; pinned by file: {string.Join(", ", pinned.Select(a => a.GetName().Name))}");
        return mode;
    }

    static List<string> ModeList(object? mode, string field) => mode == null ? new List<string>() :
        (List<string>)mode.GetType().GetField(field, BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(mode)!;

    // `forgiven`: for an installation newer than the contract's build, the
    // entries that differ only by the build number (null when not newer).
    static List<string> Differences(Host host, JsonNode platform, out int checkedEntries, out double milliseconds, out List<string>? forgiven, object? mode = null)
    {
        object? tolerance = null;
        if (host.Build > (int)platform["GameBuild"]!)
            tolerance = Activator.CreateInstance(host.Mod.GetType("MixedAtmospheres.Oni.GameContractCheck+BuildTolerance", true)!,
                BindingFlags.NonPublic | BindingFlags.Instance, null, new object[] { host.Build }, null);
        var arguments = new object?[] { Platform(host, platform), host.Mod, 0, null, mode, tolerance }; // null: no record of other mods' patches
        var timer = Stopwatch.StartNew();
        var differences = ((IEnumerable<string>)Call(host.Check, "Verify", arguments)!).ToList();
        milliseconds = timer.Elapsed.TotalMilliseconds; checkedEntries = (int)arguments[2]!;
        forgiven = tolerance == null ? null : ((IEnumerable<string>)tolerance.GetType().GetField("Forgiven", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(tolerance)!).ToList();
        return differences;
    }

    static List<string> Differences(Host host, JsonNode platform, out int checkedEntries, out double milliseconds, object? mode = null) =>
        Differences(host, platform, out checkedEntries, out milliseconds, out _, mode);

    static int Verify(string modPath, List<(string platform, string root)> games, string contractPath, bool controls, bool byKey, string? filesOf)
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
            var mode = Mode(host, node, byKey, filesOf);
            host_ShortPart = host.Mod.GetType("MixedAtmospheres.Oni.ModAdmission", true)!.GetMethod("ShortPart", BindingFlags.NonPublic | BindingFlags.Static);
            var differences = Differences(host, node, out var count, out var ms, out var forgiven, mode);
            var timings = (System.Collections.Generic.Dictionary<string, double>)host.Check.GetField("Timings", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
            Console.WriteLine($"{platform}: build {host.Build}, {count} dependencies checked in {ms:F0} ms (" +
                string.Join(", ", timings.Select(t => $"{t.Key} {t.Value:F0} ms")) + $"), {differences.Count} differences");
            foreach (var difference in differences.Take(50)) Console.WriteLine("  " + difference);
            foreach (var (field, what) in new[] { ("Rewritten", "game entries whose code differs from the files' (Moono's rewrite at load)"), ("Engine", "engine entries checked for presence and signature only") })
            {
                var list = ModeList(mode, field);
                if (mode != null) Console.WriteLine($"  {list.Count} {what}" + string.Concat(list.Select(k => "\n    " + k)));
            }
            if (forgiven != null)
            {
                // The package root's decision for a newer game (docs/game-update-resilience.md#tolerated-newer-builds).
                Console.WriteLine($"{platform}: newer than the contract's build {(int)node["GameBuild"]!}: {forgiven.Count} entries differ only by the build number");
                foreach (var entry in forgiven) Console.WriteLine("  forgiven " + entry);
                Console.WriteLine(differences.Count == 0 ? $"{platform}: accepted as a newer build" :
                    $"{platform}: refused, an update of the mod is needed: " + string.Join(", ", differences.Select(ShortPart).Distinct().Take(10)));
            }
            Console.WriteLine($"{platform}: canonical pipe scheduling signature {SchedulingSignature(host)}");
            if (differences.Count != 0) { ++failures; continue; }
            // The synthetic changes test the token path, the authority on Unity's player.
            if (controls) failures += Controls(host, platform, node);
        }
        return failures == 0 ? 0 : 1;
    }

    static string ShortPart(string difference) => (string)host_ShortPart!.Invoke(null, new object[] { difference })!;
    static MethodInfo? host_ShortPart;

    // ColonyPipeBiology's token-independent signature of its five game
    // bodies in this installation, and whether it is the expected one.
    static string SchedulingSignature(Host host)
    {
        var biology = host.Mod.GetType("MixedAtmospheres.Oni.ColonyPipeBiology", true)!;
        var expected = (string)biology.GetField("ExpectedCanonicalSchedulingSignature", BindingFlags.NonPublic | BindingFlags.Static)!.GetRawConstantValue()!;
        try
        {
            var actual = (string)biology.GetMethod("CanonicalSchedulingSignature", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, null)!;
            return actual + (actual == expected ? " (expected)" : " (NOT the expected " + expected + ")");
        }
        catch (TargetInvocationException error) { return "unavailable: " + (error.InnerException ?? error).Message; }
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
        Case("renamed field injected by Harmony", copy => {
            var e = copy["Entries"]!.AsArray().FirstOrDefault(x => (string?)x?["Key"] == "named Sublimates::primaryElement");
            if (e == null) return null; // Older adapters did not record injected fields.
            e["Name"] = "RemovedPrimaryElement"; return (string)e["Key"]!;
        });
        Case("unexpected patch target", copy => { var entries = copy["Entries"]!.AsArray(); var e = First(copy, "patched"); var key = (string)e["Key"]!; entries.Remove(e); return "new patch target " + key; });
        Case("vanished patch target", copy => { copy["Entries"]!.AsArray().Add(new JsonObject { ["Kind"] = "patched", ["Token"] = 0, ["Context"] = 0, ["TypeName"] = "", ["Name"] = "",
            ["Key"] = "static Game::RemovedMethod()System.Void", ["Value"] = "sha256:" + new string('3', 64) }); return "missing patch target static Game::RemovedMethod()System.Void"; });
        Console.WriteLine($"{platform}: {cases - failures}/{cases} synthetic contract changes refused");
        return failures + NewerBuildControls(host, platform, original);
    }

    // The build-number reading (docs/game-update-resilience.md#tolerated-newer-builds),
    // on this one installation: the contract is made to read as written for
    // an earlier build. An entry whose value differs while its recorded
    // build-number reading still equals the game's is forgiven; a changed
    // reading, or a difference in an entry without one, is refused.
    static int NewerBuildControls(Host host, string platform, JsonNode original)
    {
        var failures = 0;
        void Fail(string message) { ++failures; Console.WriteLine("  FAILED " + message); }
        JsonNode Earlier() { var copy = JsonNode.Parse(original.ToJsonString())!; copy["GameBuild"] = host.Build - 1; return copy; }
        IEnumerable<JsonObject> Entries(JsonNode copy) => copy["Entries"]!.AsArray().Select(e => e!.AsObject());
        var masked = Entries(original).Where(e => ((string?)e["BuildMasked"] ?? "").Length != 0).Select(e => (string)e["Key"]!).ToList();
        if (masked.Count == 0) { Fail("the contract records no build-number reading; every game build compiles its number into the code the mod patches"); return failures; }
        // 1. Unchanged contract, read as an earlier build's: nothing differs, nothing to forgive.
        var differences = Differences(host, Earlier(), out _, out _, out var forgiven);
        if (differences.Count != 0 || forgiven == null || forgiven.Count != 0) Fail($"unchanged contract as an earlier build: {differences.Count} differences, {forgiven?.Count} forgiven");
        // 2. Every entry with a reading differs in its exact value: each is forgiven, nothing refused.
        var copy = Earlier();
        foreach (var e in Entries(copy).Where(e => ((string?)e["BuildMasked"] ?? "").Length != 0)) e["Value"] = "sha256:" + new string('4', 64);
        differences = Differences(host, copy, out _, out _, out forgiven);
        if (differences.Count != 0 || forgiven == null || forgiven.Count != masked.Count)
            Fail($"entries differing only by the build number: {differences.Count} differences ({string.Join(" | ", differences.Take(3))}), {forgiven?.Count} of {masked.Count} forgiven");
        // 3. The same, with one reading changed too: that entry is refused by name.
        copy = Earlier();
        var target = Entries(copy).First(e => ((string?)e["BuildMasked"] ?? "").Length != 0);
        foreach (var e in Entries(copy).Where(e => ((string?)e["BuildMasked"] ?? "").Length != 0)) e["Value"] = "sha256:" + new string('4', 64);
        target["BuildMasked"] = "sha256:" + new string('5', 64);
        differences = Differences(host, copy, out _, out _, out forgiven);
        if (!differences.Any(d => d.EndsWith((string)target["Key"]!, StringComparison.Ordinal)) || forgiven == null || forgiven.Count != masked.Count - 1)
            Fail($"a changed build-number reading must be refused by name: {string.Join(" | ", differences.Take(3))}");
        // 4. A changed entry without a reading is refused even on a newer build.
        copy = Earlier();
        var plain = Entries(copy).First(e => (string)e["Kind"]! == "patched" && ((string?)e["BuildMasked"] ?? "").Length == 0);
        plain["Value"] = "sha256:" + new string('6', 64);
        differences = Differences(host, copy, out _, out _, out _);
        if (!differences.Any(d => d == "changed patch target " + (string)plain["Key"]!)) Fail($"a changed entry without a reading must be refused: {string.Join(" | ", differences.Take(3))}");
        // 5. The same contract for the running build itself: no forgiveness at all.
        copy = JsonNode.Parse(original.ToJsonString())!;
        foreach (var e in Entries(copy).Where(e => ((string?)e["BuildMasked"] ?? "").Length != 0)) e["Value"] = "sha256:" + new string('4', 64);
        differences = Differences(host, copy, out _, out _, out forgiven);
        if (differences.Count != masked.Count || forgiven != null) Fail($"the running build's own contract forgives nothing: {differences.Count} of {masked.Count} refused");
        Console.WriteLine($"{platform}: newer-build controls {(failures == 0 ? "passed" : "FAILED")} ({masked.Count} entries carry a build-number reading: " +
            string.Join(", ", masked.Take(8)) + ")");
        return failures;
    }
}

