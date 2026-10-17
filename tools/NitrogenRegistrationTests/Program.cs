using System.Globalization;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text.Json;
using MixedAtmospheres.Oni;
using HarmonyLib;

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
            ("all native enum identities and rewritten tag results remain unchanged", NativePassThrough),
            ("custom tag adapter resolves the original string rather than an enum integer", CustomTags),
            ("localization keys remain stable under Turkish casing", LocalizedNames),
            ("explicit YAML descriptions bypass the native culture-dependent default", DescriptionMapping),
            ("native tag bridge still exposes exactly the inspected signatures", BridgeContract),
            ("YAML phases load only through the adapter's private content gate", ContentGate),
            ("tag IL rewrite rejects changed enum and branch shapes", InvalidBridge),
            ("both original native tag helpers can be invoked before patch composition",()=>
            { NitrogenElementTagPatch.WarmNativeCalls();Check(!NitrogenElementTagPatch.Verified,"Unpatched preflight cannot certify patched helpers."); })
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
            Check(!OniNitrogenElements.TryIdentity(value, out var identity) && identity == "" &&
                OniNitrogenElements.Identity(value) == value.ToString(),"The native identity was changed: " + value);
        }
        foreach(var bridge in RewrittenBridges()) foreach(var value in Enum.GetValues<SimHashes>().Append((SimHashes)int.MaxValue))
            Check(bridge(value)==TagManager.Create(value.ToString()),"Rewritten native tag changed: "+value);
    }
    private static void CustomTags()
    {
        foreach (var (id, hash, _) in Identities())
        {
            foreach(var bridge in RewrittenBridges())
            {
                var result=bridge(hash);
                Check(result == new Tag(id) && result.ToString() == id &&
                    result != new Tag(((int)hash).ToString(CultureInfo.InvariantCulture)), "Custom enum tag resolved as an integer.");
            }
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
    private static CodeInstruction[] OriginalBridge(string name)
    {
        var method=typeof(GameTagExtensions).GetMethod(name,new[]{typeof(SimHashes)})!;
        var il=method.GetMethodBody()!.GetILAsByteArray()!;
        Check(il.Length==19 && il[0]==0x0f && il[1]==0 && il[2]==0xfe && il[3]==0x16 &&
            il[8]==0x6f && il[13]==0x28 && il[18]==0x2a,"Pinned native tag IL changed.");
        return new[]{new CodeInstruction(OpCodes.Ldarga_S,(byte)0),
            new CodeInstruction(OpCodes.Constrained,method.Module.ResolveType(BitConverter.ToInt32(il,4))),
            new CodeInstruction(OpCodes.Callvirt,method.Module.ResolveMethod(BitConverter.ToInt32(il,9))),
            new CodeInstruction(OpCodes.Call,method.Module.ResolveMethod(BitConverter.ToInt32(il,14))),new CodeInstruction(OpCodes.Ret)};
    }
    private static IEnumerable<Func<SimHashes,Tag>> RewrittenBridges()
    {
        foreach(var name in new[]{"Create","CreateTag"})
        {
            var original=OriginalBridge(name);var code=NitrogenElementTagPatch.Transpiler(original).ToArray();
            Check(code.Length==4 && ReferenceEquals(code[2],original[3]) && ReferenceEquals(code[3],original[4]),
                "Native Tag construction/return instructions were changed.");
            var method=new DynamicMethod("rewritten_"+name,typeof(Tag),new[]{typeof(SimHashes)},typeof(Program),true);
            var generator=method.GetILGenerator();
            foreach(var instruction in code)
                if(instruction.operand is MethodInfo target)generator.Emit(instruction.opcode,target);
                else if(instruction.operand==null)generator.Emit(instruction.opcode);
                else throw new Exception("Unexpected rewritten operand");
            yield return method.CreateDelegate<Func<SimHashes,Tag>>();
        }
    }
    private static void InvalidBridge()
    {
        void Reject(CodeInstruction[] code)
        {
            try{_ = NitrogenElementTagPatch.Transpiler(code).ToArray();}
            catch(InvalidDataException){return;}
            throw new Exception("Changed native tag IL was accepted.");
        }
        Reject(Array.Empty<CodeInstruction>());
        var code=OriginalBridge("Create");code[1].operand=typeof(int);Reject(code);
        code=OriginalBridge("Create");var generator=new DynamicMethod("label",typeof(void),Type.EmptyTypes).GetILGenerator();
        code[2].labels.Add(generator.DefineLabel());Reject(code);
        code=OriginalBridge("Create");Reject(code.Concat(OriginalBridge("CreateTag")).ToArray());
    }
    private static void ContentGate()
    {
        // ONI loads elements/ even when the DLL fails; ElementLoader.Load skips
        // an entry whose dlcId has no substance table. Only the adapter's gate
        // supplies this key, so a failed adapter leaves no half-registered phase.
        var key = OniNitrogenElements.ContentGateKey;
        Check(!new[] { DlcManager.VANILLA_ID, DlcManager.EXPANSION1_ID, DlcManager.DLC2_ID, DlcManager.DLC3_ID, DlcManager.DLC4_ID,
            DlcManager.DLC5_ID, DlcManager.COSMETIC1_ID }.Contains(key), "The content gate reuses a native content key.");
        var yaml = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../elements/mixed_atmospheres_nitrogen.yaml"));
        var gates = new Dictionary<string, string>(StringComparer.Ordinal);
        var id = "";
        foreach (var line in File.ReadLines(yaml))
        {
            if (line.StartsWith("  - elementId: ", StringComparison.Ordinal)) id = line.Substring("  - elementId: ".Length);
            else if (line.StartsWith("    dlcId: ", StringComparison.Ordinal)) gates.Add(id, line.Substring("    dlcId: ".Length));
        }
        Check(gates.Count == 3 && Identities().All(item => gates.TryGetValue(item.id, out var value) && value == key),
            "A nitrogen YAML phase can load without the adapter's content gate.");
        var vanilla = (SubstanceTable)RuntimeHelpers.GetUninitializedObject(typeof(SubstanceTable));
        var other = (SubstanceTable)RuntimeHelpers.GetUninitializedObject(typeof(SubstanceTable));
        var tables = new Dictionary<string, SubstanceTable> { [DlcManager.VANILLA_ID] = vanilla, [DlcManager.EXPANSION1_ID] = other };
        OniNitrogenElements.OpenContentGate(tables); OniNitrogenElements.OpenContentGate(tables);
        Check(tables.Count == 3 && ReferenceEquals(tables[key], vanilla) && ReferenceEquals(tables[DlcManager.EXPANSION1_ID], other),
            "The content gate must resolve exactly the base-game table and leave native keys unchanged.");
        static bool Rejects(Dictionary<string, SubstanceTable> value)
        {
            try { OniNitrogenElements.OpenContentGate(value); return false; }
            catch (InvalidOperationException) { return true; }
        }
        Check(Rejects(new Dictionary<string, SubstanceTable> { [DlcManager.VANILLA_ID] = vanilla, [key] = other }) &&
            Rejects(new Dictionary<string, SubstanceTable> { [DlcManager.EXPANSION1_ID] = other }), "A conflicting or missing gate table was accepted.");
        var patch = typeof(NitrogenContentGate);
        var prefix = patch.GetMethod("Prefix", BindingFlags.NonPublic | BindingFlags.Static)!.GetCustomAttribute<HarmonyLib.HarmonyPriority>();
        var postfix = patch.GetMethod("Postfix", BindingFlags.NonPublic | BindingFlags.Static)!.GetCustomAttribute<HarmonyLib.HarmonyPriority>();
        var target = patch.GetCustomAttributes<HarmonyLib.HarmonyPatch>().Single().info;
        Check(prefix?.info.priority == HarmonyLib.Priority.Last && postfix?.info.priority == HarmonyLib.Priority.First &&
            target.declaringType == typeof(ElementLoader) && target.methodName == nameof(ElementLoader.Load),
            "The content gate must open only around the native element load.");
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
