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
        internal static string Generate(Assembly mod, int gameBuild, string assemblyHash, string firstpassHash, bool raw)
        {
            var module = mod.ManifestModule;
            var platform = new GameContractPlatform { GameBuild = gameBuild, AssemblyCSharpSha256 = assemblyHash, FirstpassSha256 = firstpassHash };
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
                entry.Shape = GameContractScope.NamedShape(type, site.Name, GameContractScope.IsGame) ?? "";
                named[entry.Key] = entry;
            }
            // Harmony resolves triple-underscore parameters by field name while building
            // a patch. The adapter has no metadata member reference for these fields.
            var targets = Targets(mod, field =>
            {
                var owner = field.DeclaringType!;
                var entry = new GameContractEntry { Kind = GameContractScope.Kind_Named, TypeName = owner.FullName!, Name = field.Name };
                (entry.Key, entry.Value) = Named(owner, field.Name);
                entry.Shape = GameContractScope.NamedShape(owner, field.Name, GameContractScope.IsGame) ?? "";
                named[entry.Key] = entry;
            });
            platform.Entries.AddRange(named.Values);
            foreach (var target in targets.OrderBy(GameContractCanon.Method, StringComparer.Ordinal))
                platform.Entries.Add(new GameContractEntry { Kind = GameContractScope.Kind_Patched, Key = GameContractCanon.Method(target), Value = GameContractCanon.Body(target) });
            if (!raw) foreach (var entry in platform.Entries) { entry.Value = GameContractCanon.Compact(entry.Value); entry.Shape = GameContractCanon.Compact(entry.Shape); }
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

        private static List<MethodBase> Targets(Assembly mod, Action<FieldInfo>? onInjectedField = null) => GameContractScope.PatchTargets(mod, onInjectedField).Concat(GameContractScope.ManualTargets()).Distinct().ToList();

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

        // Milliseconds the last Verify spent per entry kind (Player.log).
        internal static readonly Dictionary<string, double> Timings = new Dictionary<string, double>(StringComparer.Ordinal);

        // A difference that is a mod's Harmony patch on verified game code
        // (GameContractScope.PatchedByAMod): the text and the patched methods.
        // An empty Difference: a patch seen on a method the contract uses that
        // left no difference (Moono, VerifyMode.Pinned), reported all the same.
        internal sealed class ModPatched { internal string Difference = ""; internal MethodBase[] Methods = Array.Empty<MethodBase>(); }

        // How Verify finds and judges entries (docs/game-update-resilience.md#moono).
        internal sealed class VerifyMode
        {
            // Find each entry's member by its key, not its token: Moono's loader
            // re-emits this assembly and renumbers its metadata rows.
            internal bool ByKey;
            // Moono's tolerances, given only once the game's files match the
            // contract's hashes: the assemblies whose files the contract pins
            // (their code Moono may rewrite at load; everything else about them
            // must match). Every other assembly is Moono's engine, checked for
            // presence and signature (the key) only.
            internal Func<Assembly, bool>? Pinned;
            // Out: contract entries accepted under those tolerances.
            internal readonly List<string> Rewritten = new List<string>(), Engine = new List<string>();
        }

        // Every in-scope reference of the loaded mod assembly by its canonical
        // key, from the rows Generate reads: TypeRef and TypeSpec for types,
        // MemberRef for members (with each generic context a method of this
        // assembly gives one that needs it).
        internal sealed class KeyIndex
        {
            internal readonly Dictionary<string, Type> Types = new Dictionary<string, Type>(StringComparer.Ordinal);
            internal readonly Dictionary<string, List<MemberInfo>> Members = new Dictionary<string, List<MemberInfo>>(StringComparer.Ordinal);

            // A type named only in attributes (typeof in [HarmonyPatch]) has a
            // TypeRef row from the compiler that Cecil's writer, and so Moono's
            // loader, leaves out: such a type is found by its name.
            internal Type Type(string key) => Types.TryGetValue(key, out var type) ? type :
                key.IndexOfAny(new[] { '<', '[', '&', '*', '!' }) < 0 && GameContractScope.FindType(key.Replace('/', '+')) is Type named ? named :
                throw new TypeLoadException("the mod assembly holds no reference to " + key);

            internal List<MemberInfo> Member(string key) => Members.TryGetValue(key, out var members) ? members :
                throw new MissingMemberException("the mod assembly holds no reference to " + key);
        }

        private const int TypeSpecTable = 0x1B000000;

        internal static KeyIndex IndexByKey(Assembly mod)
        {
            var module = mod.ManifestModule;
            var index = new KeyIndex();
            void AddMember(MemberInfo member)
            {
                if (member.DeclaringType == null || !GameContractScope.InScope(member.DeclaringType)) return;
                var key = GameContractCanon.Member(member);
                if (!index.Members.TryGetValue(key, out var found)) index.Members[key] = found = new List<MemberInfo>();
                if (!found.Contains(member)) found.Add(member);
            }
            foreach (var table in new[] { TypeRefTable, TypeSpecTable })
                for (int row = 1, failures = 0; failures < 100000; ++row)
                {
                    Type type;
                    try { type = module.ResolveType(table | row); failures = 0; }
                    catch (ArgumentOutOfRangeException) { break; }
                    catch (Exception) { ++failures; continue; } // a generic context is needed, or the engine lacks it
                    if (!GameContractScope.InScope(type)) continue;
                    var key = GameContractCanon.Type(type);
                    if (!index.Types.ContainsKey(key)) index.Types[key] = type;
                }
            HashSet<int>? contextual = null;
            for (int row = 1, failures = 0; failures < 100000; ++row)
            {
                var token = MemberRefTable | row;
                MemberInfo member;
                try { member = module.ResolveMember(token)!; failures = 0; }
                catch (ArgumentOutOfRangeException) { break; }
                catch (ArgumentException) { (contextual ??= new HashSet<int>()).Add(token); continue; }
                catch (Exception) { ++failures; continue; }
                AddMember(member);
            }
            if (contextual != null)
                foreach (var method in OwnMethods(mod))
                {
                    IEnumerable<GameContractCanon.Instruction> code;
                    try { code = Instructions(method).ToList(); }
                    catch (Exception) { continue; }
                    foreach (var instruction in code)
                        if (IsMemberOperand(instruction.Code) && contextual.Contains(instruction.Token))
                            try { AddMember(Resolve(module, instruction.Token, method)); } catch (Exception) { }
                }
            return index;
        }

        // The canonical type a named entry's key names before its member part.
        private static string OwnerKey(GameContractEntry entry)
        {
            var key = entry.Key.StartsWith("named ", StringComparison.Ordinal) ? entry.Key.Substring(6) : entry.Key;
            var suffix = entry.Name.Length == 0 ? "" : "::" + entry.Name;
            return suffix.Length != 0 && key.EndsWith(suffix, StringComparison.Ordinal) ? key.Substring(0, key.Length - suffix.Length) : key;
        }

        // One entry found in the loaded game: its key and value as computed
        // there, the game methods it covers, and how Moono's tolerances judge it.
        private sealed class Found
        {
            internal string Key = "", Value = "";
            internal MethodBase[] Methods = Array.Empty<MethodBase>();
            internal Func<string>? Unpatched;
            internal Assembly? Home;        // where the entry's type or member is defined
            internal bool GameBody;         // the value is a game method's body form
            internal Func<Func<Assembly, bool>, string?>? Shape;
        }

        private static Found Find(GameContractEntry entry, Module module, KeyIndex? index)
        {
            var found = new Found();
            switch (entry.Kind)
            {
                case GameContractScope.Kind_Type:
                    var type = index != null ? index.Type(entry.Key) : module.ResolveType(entry.Token);
                    found.Key = GameContractCanon.Type(type); found.Value = GameContractScope.TypeValue(type);
                    found.Home = GameContractScope.Home(type);
                    break;
                case GameContractScope.Kind_Member:
                    MemberInfo member;
                    if (index == null) member = entry.Context == 0 ? module.ResolveMember(entry.Token)! : Resolve(module, entry.Token, module.ResolveMethod(entry.Context)!);
                    else
                    {
                        // One reference per generic context that resolves it: the
                        // one whose value is the contract's, else the first.
                        var candidates = index.Member(entry.Key);
                        member = candidates.FirstOrDefault(m => Same(entry.Value, GameContractScope.MemberValue(m))) ?? candidates[0];
                    }
                    found.Key = GameContractCanon.Member(member); found.Value = GameContractScope.MemberValue(member);
                    found.Home = member.DeclaringType == null ? null : GameContractScope.Home(member.DeclaringType);
                    if (member is MethodBase called)
                    {
                        found.Methods = new[] { called }; found.Unpatched = () => GameContractScope.MemberValue(member, true);
                        found.GameBody = called.DeclaringType != null && GameContractScope.IsGame(called.DeclaringType.Assembly);
                    }
                    break;
                case GameContractScope.Kind_Named:
                    var owner = entry.Token == 0 ? GameContractScope.FindType(entry.TypeName) :
                        index != null ? index.Type(OwnerKey(entry)) : module.ResolveType(entry.Token);
                    if (owner == null) throw new TypeLoadException("type " + entry.TypeName + " is missing");
                    (found.Key, found.Value) = Named(owner, entry.Name);
                    found.Home = GameContractScope.Home(owner);
                    found.Shape = pinned => GameContractScope.NamedShape(owner, entry.Name, pinned);
                    if (entry.Name.Length != 0 && entry.Name != AllMembers)
                    {
                        found.Methods = GameContractScope.GameMembers(owner, entry.Name).OfType<MethodBase>().ToArray();
                        found.Unpatched = () => GameContractScope.NamedValue(owner, entry.Name, true);
                    }
                    break;
                default: throw new NotSupportedException("unknown contract entry kind " + entry.Kind);
            }
            return found;
        }

        // Moono (VerifyMode.Pinned): may an entry whose key matches differ in
        // value? A pinned game assembly's code may be rewritten at load, and
        // only its method bodies may differ; an engine entry is checked for
        // presence and signature, which its key holds.
        private static bool Tolerated(GameContractEntry entry, Found found, VerifyMode mode)
        {
            var pinned = mode.Pinned!;
            if (found.Home != null && pinned(found.Home))
            {
                if (entry.Kind == GameContractScope.Kind_Member && found.GameBody) { mode.Rewritten.Add(entry.Key); return true; }
                if (entry.Kind != GameContractScope.Kind_Named || entry.Shape.Length == 0 || found.Shape == null || !(found.Shape(pinned) is string shape) || !Same(entry.Shape, shape))
                    return false;
                // A member list ("::*") holds no bodies: what differs there is
                // what the type inherits from Moono's engine.
                (entry.Name == AllMembers ? mode.Engine : mode.Rewritten).Add(entry.Key);
                return true;
            }
            // Engine entries: presence and signature (the key) only. Layouts,
            // enum literals and member lists of Moono's own Unity types need
            // not be Unity's (its TextClipping has no Ellipsis, for one; the
            // mod's code holds the values it uses as constants).
            if (entry.Kind == GameContractScope.Kind_Named && found.Value == "absent") return false;
            mode.Engine.Add(entry.Key);
            return true;
        }

        // Every difference between the contract and the loaded game, in
        // plain words; empty when the build is verified.
        // Such a difference is worded apart and, when `modPatched` is given,
        // recorded there: when every difference is of that kind the build is
        // verified (GameBuildCheck.Admit) and Player.log names the mods.
        internal static List<string> Verify(GameContractPlatform contract, Assembly mod, out int checkedEntries, List<ModPatched>? modPatched = null, VerifyMode? mode = null)
        {
            var differences = new List<string>();
            var module = mod.ManifestModule;
            checkedEntries = 0;
            var expectedTargets = new Dictionary<string, string>(StringComparer.Ordinal);
            Timings.Clear();
            var clock = System.Diagnostics.Stopwatch.StartNew();
            KeyIndex? index = null;
            if (mode?.ByKey == true)
            {
                index = IndexByKey(mod);
                Timings["index"] = clock.Elapsed.TotalMilliseconds;
            }
            var seenPatched = new HashSet<MethodBase>();
            void Observe(MethodBase[] methods)
            {
                // Moono: patches that left no difference are reported too.
                if (mode?.Pinned == null || modPatched == null) return;
                var patched = methods.Where(m => seenPatched.Add(m) && GameContractScope.PatchedByAMod(m)).ToArray();
                if (patched.Length != 0) modPatched.Add(new ModPatched { Methods = patched });
            }
            foreach (var entry in contract.Entries)
            {
                if (entry.Kind == GameContractScope.Kind_Patched) { expectedTargets[entry.Key] = entry.Value; continue; }
                ++checkedEntries;
                var started = clock.Elapsed.TotalMilliseconds;
                Found found;
                try { found = Find(entry, module, index); }
                catch (Exception error) { differences.Add("missing " + entry.Key + " (" + error.Message + ")"); continue; }
                finally { Timings[entry.Kind] = (Timings.TryGetValue(entry.Kind, out var spent) ? spent : 0) + clock.Elapsed.TotalMilliseconds - started; }
                Observe(found.Methods);
                if (found.Key != entry.Key) differences.Add("changed " + entry.Key + " -> " + found.Key);
                else if (!Same(entry.Value, found.Value) && !(mode?.Pinned != null && Tolerated(entry, found, mode)))
                    Changed(entry.Key, "", entry.Value, found.Methods, found.Unpatched);
            }
            void Changed(string key, string what, string expected, MethodBase[] methods, Func<string>? unpatched)
            {
                var patched = methods.Where(GameContractScope.PatchedByAMod).ToArray();
                if (patched.Length == 0 || unpatched == null || !Same(expected, unpatched())) { differences.Add("changed " + what + key); return; }
                var difference = "patched by another mod: " + what + key;
                differences.Add(difference);
                modPatched?.Add(new ModPatched { Difference = difference, Methods = patched });
            }
            var discovery = clock.Elapsed.TotalMilliseconds;
            List<MethodBase> targets;
            try { targets = Targets(mod); }
            catch (Exception error) { differences.Add("patch targets could not be resolved (" + (error.InnerException ?? error).Message + ")"); return differences; }
            var foundTargets = new HashSet<string>(StringComparer.Ordinal);
            foreach (var target in targets)
            {
                ++checkedEntries;
                string key, value;
                try { key = GameContractCanon.Method(target); value = GameContractCanon.Body(target); }
                catch (Exception error) { differences.Add("unreadable patch target " + target.DeclaringType?.FullName + "." + target.Name + " (" + error.Message + ")"); continue; }
                foundTargets.Add(key);
                Observe(new[] { target });
                if (!expectedTargets.TryGetValue(key, out var expected)) differences.Add("new patch target " + key);
                else if (Same(expected, value)) continue;
                else if (mode?.Pinned != null && target.DeclaringType != null && mode.Pinned(GameContractScope.Home(target.DeclaringType))) mode.Rewritten.Add("patch target " + key);
                else Changed(key, "patch target ", expected, new[] { target }, () => GameContractCanon.Body(target, MethodImplAttributes.NoInlining));
            }
            foreach (var key in expectedTargets.Keys) if (!foundTargets.Contains(key)) differences.Add("missing patch target " + key);
            Timings[GameContractScope.Kind_Patched] = clock.Elapsed.TotalMilliseconds - discovery;
            return differences;
        }

        // The Mono control of the key path (tools/game_mono_contract_check.py):
        // on the original player the token path is the authority, and the key
        // path must find, for every entry, the same key and value. Empty when
        // both agree.
        internal static List<string> CrossCheck(GameContractPlatform contract, Assembly mod)
        {
            var mismatches = new List<string>();
            var module = mod.ManifestModule;
            var index = IndexByKey(mod);
            foreach (var entry in contract.Entries)
            {
                if (entry.Kind == GameContractScope.Kind_Patched) continue;
                string byToken, byKey;
                try { var found = Find(entry, module, null); byToken = found.Key + " = " + found.Value; }
                catch (Exception error) { byToken = "missing (" + error.Message + ")"; }
                try { var found = Find(entry, module, index); byKey = found.Key + " = " + found.Value; }
                catch (Exception error) { byKey = "missing (" + error.Message + ")"; }
                if (byToken != byKey) mismatches.Add(entry.Kind + " " + entry.Key + ": by token " + Shorter(byToken) + ", by key " + Shorter(byKey));
            }
            return mismatches;
        }

        private static string Shorter(string text) => text.Length <= 200 ? text : text.Substring(0, 200) + "...";

        // tools/game_mono_contract_check.c runs this inside the game's own Mono
        // runtime, without Unity, to check the canonical forms there.
        // The host passes the contract's text: file access would need the
        // player's native helpers, which only the Unity player sets up.
        internal static string SelfCheck(string contractJson, string platform)
        {
            var file = GameContractFile.Parse(contractJson);
            var started = System.Diagnostics.Stopwatch.StartNew();
            var modPatched = new List<ModPatched>();
            var mod = typeof(GameContractCheck).Assembly;
            // The key path Moono uses (docs/game-update-resilience.md#moono)
            // must agree with the token path entry by entry, and its Verify
            // must report the same differences. A disagreement is a difference.
            var keyed = CrossCheck(file.Platforms[platform], mod);
            var keyDifferences = Verify(file.Platforms[platform], mod, out _, null, new VerifyMode { ByKey = true });
            var differences = Verify(file.Platforms[platform], mod, out var count, modPatched);
            var timings = Timings.Select(t => t.Key + " " + ((long)t.Value).ToString() + " ms").ToList();
            var tokenSet = new HashSet<string>(differences, StringComparer.Ordinal);
            keyed.AddRange(keyDifferences.Where(d => !tokenSet.Contains(d)).Select(d => "only by key: " + d));
            var keySet = new HashSet<string>(keyDifferences, StringComparer.Ordinal);
            keyed.AddRange(differences.Where(d => !keySet.Contains(d)).Select(d => "only by token: " + d));
            var keyLine = keyed.Count == 0 ? "key path agrees" : keyed.Count + " key path disagreements";
            differences.AddRange(keyed.Select(d => "key path: " + d));
            var refusal = GameBuildCheck.Admit(GameBuildCheck.RunningBuild, differences, modPatched, out var otherModPatches);
            return $"{platform} (game Mono): build {GameBuildCheck.RunningBuild}, {count} dependencies checked in {started.ElapsedMilliseconds} ms (" +
                string.Join(", ", timings) + $"; {keyLine}), {differences.Count} differences" +
                string.Concat(differences.Take(40).Select(d => "\n  " + d)) +
                // What Mod.OnLoad would decide (--other-mod controls): the
                // refusal's notice, or the verified build's other-mod lines.
                (refusal != null ? "\nnotice:\n" + refusal.Notice : differences.Count == 0 ? "" :
                    "\nverified:" + ModAdmission.OtherModPatchesLog(otherModPatches));
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
