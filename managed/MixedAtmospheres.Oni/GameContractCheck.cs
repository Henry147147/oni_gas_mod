using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Writes (tools/GameContract) and verifies (Mod.OnLoad) the managed game
    // contract of this mod assembly (docs/game-update-resilience.md).
    internal static class GameContractCheck
    {
        private const int TypeRefTable = 0x01000000, MemberRefTable = 0x0A000000;
        private static readonly HashSet<string> NamedLookups = new HashSet<string>(StringComparer.Ordinal)
        {
            "HarmonyLib.AccessTools::Method", "HarmonyLib.AccessTools::DeclaredMethod", "HarmonyLib.AccessTools::Field", "HarmonyLib.AccessTools::DeclaredField",
            "HarmonyLib.AccessTools::Property", "HarmonyLib.AccessTools::DeclaredProperty", "HarmonyLib.AccessTools::PropertyGetter",
            "HarmonyLib.AccessTools::PropertySetter", "HarmonyLib.AccessTools::DeclaredPropertyGetter", "HarmonyLib.AccessTools::DeclaredPropertySetter",
            "HarmonyLib.AccessTools::Inner", "HarmonyLib.AccessTools::FieldRefAccess", "HarmonyLib.AccessTools::StaticFieldRefAccess",
            "HarmonyLib.AccessTools::TypeByName", "HarmonyLib.AccessTools::Indexer", "HarmonyLib.AccessTools::DeclaredIndexer",
            "System.Type::GetMethod", "System.Type::GetField", "System.Type::GetProperty", "System.Type::GetNestedType", "System.Type::GetMember", "System.Type::GetEvent",
            "HarmonyLib.Traverse::Field", "HarmonyLib.Traverse::Method", "HarmonyLib.Traverse::Property", "HarmonyLib.Traverse::Type",
        };

        // Called by tools/GameContract inside its isolated load context; the
        // game assemblies there are the installation the contract is for.
        internal static string Generate(Assembly mod, int gameBuild, string assemblyHash, bool raw)
        {
            var module = mod.ManifestModule;
            var platform = new GameContractPlatform { GameBuild = gameBuild, AssemblyCSharpSha256 = assemblyHash };
            for (var row = 1; ; ++row)
            {
                Type type;
                try { type = module.ResolveType(TypeRefTable | row); }
                catch (ArgumentOutOfRangeException) { break; }
                if (!GameContractScope.InScope(type)) continue;
                platform.Entries.Add(new GameContractEntry { Kind = GameContractScope.Kind_Type, Token = TypeRefTable | row, Key = GameContractCanon.Type(type), Value = GameContractScope.TypeValue(type) });
            }
            // A reference inside generic code resolves only in the generic
            // context of a method that uses it; the entry records that method.
            var contexts = new Dictionary<int, MethodBase>();
            foreach (var method in OwnMethods(mod))
                foreach (var instruction in Instructions(method))
                    if (instruction.Token >> 24 == MemberRefTable >> 24 && IsMemberOperand(instruction.Code) && !contexts.ContainsKey(instruction.Token))
                        contexts[instruction.Token] = method;
            for (var row = 1; ; ++row)
            {
                var token = MemberRefTable | row; MemberInfo member; var context = 0;
                try { member = module.ResolveMember(token)!; }
                catch (ArgumentOutOfRangeException) { break; }
                catch (ArgumentException) when (contexts.TryGetValue(token, out var user))
                { member = Resolve(module, token, user); context = user.MetadataToken; }
                catch (ArgumentException error) { platform.Unverified.Add($"member token 0x{token:x8} is unused or needs a generic context ({error.GetType().Name})"); continue; }
                if (!GameContractScope.InScope(member.DeclaringType)) continue;
                platform.Entries.Add(new GameContractEntry { Kind = GameContractScope.Kind_Member, Token = token, Context = context, Key = GameContractCanon.Member(member), Value = GameContractScope.MemberValue(member) });
            }
            var named = new SortedDictionary<string, GameContractEntry>(StringComparer.Ordinal);
            foreach (var site in NamedSites(mod, platform.Unverified))
            {
                var type = site.Token != 0 ? module.ResolveType(site.Token) : GameContractScope.FindType(site.TypeName);
                if (type == null) { platform.Unverified.Add("named type " + site.TypeName + " is absent"); continue; }
                if (!GameContractScope.InScope(type)) continue;
                var entry = new GameContractEntry { Kind = GameContractScope.Kind_Named, Token = site.Token, TypeName = site.TypeName, Name = site.Name };
                (entry.Key, entry.Value) = Named(type, site.Name);
                named[entry.Key] = entry;
            }
            platform.Entries.AddRange(named.Values);
            foreach (var target in Targets(mod).OrderBy(GameContractCanon.Method, StringComparer.Ordinal))
                platform.Entries.Add(new GameContractEntry { Kind = GameContractScope.Kind_Patched, Key = GameContractCanon.Method(target), Value = GameContractCanon.Body(target) });
            if (!raw) foreach (var entry in platform.Entries) entry.Value = GameContractCanon.Compact(entry.Value);
            return platform.ToJson();
        }

        private static bool Same(string expected, string actual) =>
            expected == actual || expected.StartsWith("sha256:", StringComparison.Ordinal) && expected == GameContractCanon.Compact(actual);

        // Game types a method names in its IL: type operands and the owners
        // of the members it uses (reflection names, for TypeByName).
        private static IEnumerable<string> TouchedTypes(MethodBase method)
        {
            var found = new SortedSet<string>(StringComparer.Ordinal);
            var typeArguments = method.DeclaringType != null && method.DeclaringType.IsGenericType ? method.DeclaringType.GetGenericArguments() : null;
            var methodArguments = method is MethodInfo && method.IsGenericMethod ? method.GetGenericArguments() : null;
            foreach (var instruction in Instructions(method))
            {
                var operand = instruction.Code.OperandType;
                if (operand != OperandType.InlineType && operand != OperandType.InlineTok && operand != OperandType.InlineMethod && operand != OperandType.InlineField) continue;
                MemberInfo? member = null;
                try { member = method.Module.ResolveMember(instruction.Token, typeArguments, methodArguments); } catch (ArgumentException) { }
                var type = member as Type ?? member?.DeclaringType;
                while (type != null && (type.IsArray || type.IsByRef || type.IsPointer)) type = type.GetElementType();
                if (type != null && !type.IsGenericParameter && GameContractScope.IsGame(type.Assembly) && type.FullName != null && !type.IsGenericType)
                    found.Add(type.FullName);
            }
            return found;
        }

        // Our static Type fields that hold typeof(T) or AccessTools.Inner(typeof(T), "N"), by reflection name.
        private static Dictionary<FieldInfo, string> TypeFields(Assembly mod)
        {
            var result = new Dictionary<FieldInfo, string>();
            var module = mod.ManifestModule;
            foreach (var type in AccessTools.GetTypesFromAssembly(mod))
            {
                var initializer = type.TypeInitializer;
                if (initializer == null) continue;
                Type? owner = null; string? inner = null;
                foreach (var instruction in Instructions(initializer))
                {
                    if (instruction.Code == OpCodes.Ldtoken) { try { owner = module.ResolveType(instruction.Token); } catch (ArgumentException) { owner = null; } inner = null; }
                    else if (instruction.Code == OpCodes.Ldstr) inner = module.ResolveString(instruction.Token);
                    else if (instruction.Code == OpCodes.Stsfld)
                    {
                        FieldInfo? field = null;
                        try { field = module.ResolveField(instruction.Token); } catch (ArgumentException) { }
                        if (field != null && field.FieldType == typeof(Type) && owner?.FullName != null)
                            result[field] = inner != null && IsMemberName(inner) ? owner.FullName + "+" + inner : owner.FullName;
                        owner = null; inner = null;
                    }
                }
            }
            return result;
        }

        private static int LocalIndex(GameContractCanon.Instruction instruction, bool store)
        {
            var code = instruction.Code;
            if (store ? code == OpCodes.Stloc_0 : code == OpCodes.Ldloc_0) return 0;
            if (store ? code == OpCodes.Stloc_1 : code == OpCodes.Ldloc_1) return 1;
            if (store ? code == OpCodes.Stloc_2 : code == OpCodes.Ldloc_2) return 2;
            if (store ? code == OpCodes.Stloc_3 : code == OpCodes.Ldloc_3) return 3;
            if (store ? code == OpCodes.Stloc_S : code == OpCodes.Ldloc_S) return instruction.Operand[0];
            if (store ? code == OpCodes.Stloc : code == OpCodes.Ldloc) return BitConverter.ToUInt16(instruction.Operand, 0);
            return -1;
        }

        private static bool IsMemberOperand(OpCode code) => code.OperandType == OperandType.InlineMethod || code.OperandType == OperandType.InlineField || code.OperandType == OperandType.InlineTok;

        private static MemberInfo Resolve(Module module, int token, MethodBase context) => module.ResolveMember(token,
            context.DeclaringType != null && context.DeclaringType.IsGenericType ? context.DeclaringType.GetGenericArguments() : null,
            context is MethodInfo && context.IsGenericMethod ? context.GetGenericArguments() : null)!;

        private static IEnumerable<MethodBase> OwnMethods(Assembly mod) => AccessTools.GetTypesFromAssembly(mod)
            .SelectMany(type => type.GetMethods(GameContractCanon.Declared).Cast<MethodBase>().Concat(type.GetConstructors(GameContractCanon.Declared)));

        private static IEnumerable<GameContractCanon.Instruction> Instructions(MethodBase method) =>
            GameContractCanon.Decode(method.GetMethodBody()?.GetILAsByteArray() ?? Array.Empty<byte>());

        private static List<MethodBase> Targets(Assembly mod) => GameContractScope.PatchTargets(mod).Concat(GameContractScope.ManualTargets()).Distinct().ToList();

        private const string AllMembers = "*";
        private static bool IsMemberName(string text) => System.Text.RegularExpressions.Regex.IsMatch(text, @"^[A-Za-z_<][A-Za-z0-9_`<>$]*$");

        private static (string key, string value) Named(Type type, string name) => name.Length == 0
            ? ("named " + GameContractCanon.Type(type), GameContractScope.TypeValue(type))
            : name == AllMembers ? ("named " + GameContractCanon.Type(type) + "::*", GameContractCanon.Hash(string.Join("\n",
                GameContractScope.GameMembers(type).Select(GameContractCanon.Member).OrderBy(m => m, StringComparer.Ordinal))))
            : ("named " + GameContractCanon.Type(type) + "::" + name, GameContractScope.NamedValue(type, name));

        private sealed class Site { internal int Token; internal string TypeName = "", Name = ""; }

        // Reflection by constant name: ldtoken T ... ldstr "name" ... call
        // AccessTools.Method/Field/... or Type.GetX, TypeByName("T"), and the
        // same through the mod's own helpers that take a Type and a name.
        private static IEnumerable<Site> NamedSites(Assembly mod, List<string> unverified)
        {
            var module = mod.ManifestModule;
            string Lookup(MethodBase? called) => called?.DeclaringType == null ? "" : called.DeclaringType.FullName + "::" + called.Name;
            MethodBase? Called(MethodBase method, GameContractCanon.Instruction instruction)
            {
                if (instruction.Code != OpCodes.Call && instruction.Code != OpCodes.Callvirt) return null;
                try { return module.ResolveMethod(instruction.Token, method.DeclaringType != null && method.DeclaringType.IsGenericType ? method.DeclaringType.GetGenericArguments() : null,
                    method is MethodInfo && method.IsGenericMethod ? method.GetGenericArguments() : null); }
                catch (ArgumentException) { return null; }
            }
            var helpers = new HashSet<MethodBase>(OwnMethods(mod).Where(method =>
                method.GetParameters().Any(p => p.ParameterType == typeof(Type)) && method.GetParameters().Any(p => p.ParameterType == typeof(string)) &&
                Instructions(method).Any(instruction => NamedLookups.Contains(Lookup(Called(method, instruction))))));
            var typeFields = TypeFields(mod);
            foreach (var method in OwnMethods(mod))
            {
                // Operands since the last statement boundary: the first type
                // token names the owner, every string a member name.
                var types = new List<int>(); var strings = new List<string>();
                var localTypes = new Dictionary<int, int>();
                var typeNames = new List<string>(); // owners known only by name (generic arguments, Type-valued fields)
                void Reset() { types.Clear(); strings.Clear(); typeNames.Clear(); }
                foreach (var instruction in Instructions(method))
                {
                    var code = instruction.Code;
                    if (code == OpCodes.Ldtoken && (instruction.Token >> 24 == 0x01 || instruction.Token >> 24 == 0x02 || instruction.Token >> 24 == 0x1B))
                    { types.Add(instruction.Token); continue; }
                    if (code == OpCodes.Ldstr) { strings.Add(module.ResolveString(instruction.Token)); continue; }
                    var local = LocalIndex(instruction, store: true);
                    if (local >= 0)
                    {
                        // var type = typeof(T); ... AccessTools.Field(type, "name")
                        if (types.Count == 1 && strings.Count == 0) localTypes[local] = types[0]; else localTypes.Remove(local);
                        Reset(); continue;
                    }
                    local = LocalIndex(instruction, store: false);
                    if (local >= 0) { if (localTypes.TryGetValue(local, out var stored)) types.Add(stored); continue; }
                    if (code == OpCodes.Ldsfld)
                    {
                        FieldInfo? field = null;
                        try { field = module.ResolveField(instruction.Token); } catch (ArgumentException) { }
                        if (field != null && typeFields.TryGetValue(field, out var fieldType)) typeNames.Add(fieldType);
                        continue;
                    }
                    if (code == OpCodes.Pop || code == OpCodes.Stfld || code == OpCodes.Stsfld || code == OpCodes.Ret ||
                        code.FlowControl == FlowControl.Branch || code.FlowControl == FlowControl.Cond_Branch) { Reset(); continue; }
                    var called = Called(method, instruction);
                    if (called == null) continue;
                    var name = Lookup(called);
                    if (name == "System.Type::GetTypeFromHandle" || name == "System.Type::MakeByRefType" || name == "System.Type::MakeArrayType") continue;
                    var helper = helpers.Contains(called);
                    if (!helper && !NamedLookups.Contains(name)) { if (called is MethodInfo info && info.ReturnType == typeof(void)) Reset(); continue; }
                    if (name == "HarmonyLib.AccessTools::TypeByName")
                    {
                        if (strings.Count != 0) yield return new Site { TypeName = strings[strings.Count - 1] };
                        else if (!helpers.Contains(method)) unverified.Add("dynamic type lookup in " + GameContractCanon.Method(method));
                    }
                    else if (types.Count == 0 && called.IsGenericMethod && strings.Count != 0 && called.GetGenericArguments()[0] is var generic &&
                             !generic.IsGenericParameter && generic.FullName != null)
                    {
                        // AccessTools.FieldRefAccess<Owner, Field>("name")
                        foreach (var text in strings.Distinct().Where(IsMemberName)) yield return new Site { TypeName = generic.FullName, Name = text };
                    }
                    else if (types.Count == 0 && typeNames.Count != 0 && strings.Count != 0)
                    {
                        foreach (var text in strings.Distinct().Where(IsMemberName)) yield return new Site { TypeName = typeNames[0], Name = text };
                    }
                    else if (types.Count != 0 && strings.Count != 0)
                    {
                        // Member names only: messages and patch-method names are not lookups.
                        foreach (var text in strings.Distinct().Where(IsMemberName)) yield return new Site { Token = types[0], Name = text };
                    }
                    else if (types.Count != 0)
                        // A computed name on a known type: every member signature of the type.
                        yield return new Site { Token = types[0], Name = AllMembers };
                    else if (name.StartsWith("HarmonyLib.AccessTools::", StringComparison.Ordinal) && strings.Count != 0 && strings[strings.Count - 1].Contains(":"))
                    {
                        var parts = strings[strings.Count - 1].Split(new[] { ':' }, 2);
                        yield return new Site { TypeName = parts[0], Name = parts[1] };
                    }
                    else if (!helpers.Contains(method))
                    {
                        // A computed name on a computed type: cover every member
                        // signature of each game type this method touches.
                        var touched = TouchedTypes(method).ToList();
                        foreach (var owner in touched) yield return new Site { TypeName = owner, Name = AllMembers };
                        if (touched.Count == 0) unverified.Add("dynamic " + name + " in " + GameContractCanon.Method(method));
                    }
                    Reset();
                }
            }
        }

        // Every difference between the contract and the loaded game, in
        // plain words; empty when the build is verified.
        // Milliseconds the last Verify spent per entry kind (Player.log).
        internal static readonly Dictionary<string, double> Timings = new Dictionary<string, double>(StringComparer.Ordinal);

        internal static List<string> Verify(GameContractPlatform contract, Assembly mod, out int checkedEntries)
        {
            var differences = new List<string>();
            var module = mod.ManifestModule;
            checkedEntries = 0;
            var expectedTargets = new Dictionary<string, string>(StringComparer.Ordinal);
            Timings.Clear();
            var clock = System.Diagnostics.Stopwatch.StartNew();
            foreach (var entry in contract.Entries)
            {
                if (entry.Kind == GameContractScope.Kind_Patched) { expectedTargets[entry.Key] = entry.Value; continue; }
                ++checkedEntries;
                var started = clock.Elapsed.TotalMilliseconds;
                string key, value;
                try
                {
                    switch (entry.Kind)
                    {
                        case GameContractScope.Kind_Type:
                            var type = module.ResolveType(entry.Token);
                            key = GameContractCanon.Type(type); value = GameContractScope.TypeValue(type); break;
                        case GameContractScope.Kind_Member:
                            var member = entry.Context == 0 ? module.ResolveMember(entry.Token)! : Resolve(module, entry.Token, module.ResolveMethod(entry.Context)!);
                            key = GameContractCanon.Member(member); value = GameContractScope.MemberValue(member); break;
                        case GameContractScope.Kind_Named:
                            var owner = entry.Token != 0 ? module.ResolveType(entry.Token) : GameContractScope.FindType(entry.TypeName)
                                ?? throw new TypeLoadException("type " + entry.TypeName + " is missing");
                            (key, value) = Named(owner, entry.Name); break;
                        default: throw new NotSupportedException("unknown contract entry kind " + entry.Kind);
                    }
                }
                catch (Exception error) { differences.Add("missing " + entry.Key + " (" + error.Message + ")"); continue; }
                finally { Timings[entry.Kind] = (Timings.TryGetValue(entry.Kind, out var spent) ? spent : 0) + clock.Elapsed.TotalMilliseconds - started; }
                if (key != entry.Key) differences.Add("changed " + entry.Key + " -> " + key);
                else if (!Same(entry.Value, value)) differences.Add("changed " + entry.Key);
            }
            var discovery = clock.Elapsed.TotalMilliseconds;
            List<MethodBase> targets;
            try { targets = Targets(mod); }
            catch (Exception error) { differences.Add("patch targets could not be resolved (" + (error.InnerException ?? error).Message + ")"); return differences; }
            var found = new HashSet<string>(StringComparer.Ordinal);
            foreach (var target in targets)
            {
                ++checkedEntries;
                string key, value;
                try { key = GameContractCanon.Method(target); value = GameContractCanon.Body(target); }
                catch (Exception error) { differences.Add("unreadable patch target " + target.DeclaringType?.FullName + "." + target.Name + " (" + error.Message + ")"); continue; }
                found.Add(key);
                if (!expectedTargets.TryGetValue(key, out var expected)) differences.Add("new patch target " + key);
                else if (!Same(expected, value)) differences.Add("changed patch target " + key);
            }
            foreach (var key in expectedTargets.Keys) if (!found.Contains(key)) differences.Add("missing patch target " + key);
            Timings[GameContractScope.Kind_Patched] = clock.Elapsed.TotalMilliseconds - discovery;
            return differences;
        }

        // tools/game_mono_contract_check.c runs this inside the game's own Mono
        // runtime, without Unity, to check the canonical forms there.
        // The host passes the contract's text: file access would need the
        // player's native helpers, which only the Unity player sets up.
        internal static string SelfCheck(string contractJson, string platform)
        {
            var file = GameContractFile.Parse(contractJson);
            var started = System.Diagnostics.Stopwatch.StartNew();
            var differences = Verify(file.Platforms[platform], typeof(GameContractCheck).Assembly, out var count);
            return $"{platform} (game Mono): build {GameBuildCheck.RunningBuild}, {count} dependencies checked in {started.ElapsedMilliseconds} ms (" +
                string.Join(", ", Timings.Select(t => t.Key + " " + ((long)t.Value).ToString() + " ms")) + $"), {differences.Count} differences" +
                string.Concat(differences.Take(40).Select(d => "\n  " + d));
        }

        // After patching: every original this Harmony owner patched must be a
        // verified target.
        internal static List<string> UnverifiedPatches(GameContractPlatform contract, string owner)
        {
            var verified = new HashSet<string>(contract.Entries.Where(e => e.Kind == GameContractScope.Kind_Patched).Select(e => e.Key), StringComparer.Ordinal);
            return Harmony.GetAllPatchedMethods().Where(m => Harmony.GetPatchInfo(m)?.Owners.Contains(owner) == true)
                .Select(GameContractCanon.Method).Where(key => !verified.Contains(key)).OrderBy(k => k, StringComparer.Ordinal).ToList();
        }
    }
}
