using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using HarmonyLib;
using Newtonsoft.Json;

namespace MixedAtmospheres.Oni
{
    // The managed game contract (docs/game-update-resilience.md): every game
    // member, type, layout and patch target this mod depends on, in a canonical
    // form that does not depend on metadata tokens or the host runtime. The
    // same compiled code runs in the game (Mono) to verify a build and in
    // tools/GameContract (CoreCLR, reflection only, no game code invoked) to
    // write the contract for the mod assembly it ships with.
    internal static class GameContractCanon
    {
        internal const BindingFlags Declared = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        private static readonly Dictionary<short, OpCode> OpCodesByValue = typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
            .Select(field => (OpCode)field.GetValue(null)!).GroupBy(code => code.Value).ToDictionary(group => group.Key, group => group.First());

        internal static string Type(Type? type)
        {
            if (type == null) return "?";
            if (type.IsByRef) return Type(type.GetElementType()) + "&";
            if (type.IsPointer) return Type(type.GetElementType()) + "*";
            if (type.IsArray) return Type(type.GetElementType()) + "[" + new string(',', type.GetArrayRank() - 1) + "]";
            if (type.IsGenericParameter) return (type.DeclaringMethod != null ? "!!" : "!") + type.GenericParameterPosition;
            if (type.IsGenericType && !type.IsGenericTypeDefinition)
                return Type(type.GetGenericTypeDefinition()) + "<" + string.Join(",", type.GetGenericArguments().Select(Type)) + ">";
            if (type.IsNested) return Type(type.DeclaringType) + "/" + type.Name;
            return string.IsNullOrEmpty(type.Namespace) ? type.Name : type.Namespace + "." + type.Name;
        }

        // The open definition of a member of a constructed generic type or a
        // generic method instance, so parameter types read !0/!!0 everywhere.
        private static MethodBase Definition(MethodBase method)
        {
            if (method is MethodInfo info && info.IsGenericMethod && !info.IsGenericMethodDefinition) method = info.GetGenericMethodDefinition();
            var owner = method.DeclaringType;
            return owner != null && owner.IsGenericType && !owner.IsGenericTypeDefinition ? method.Module.ResolveMethod(method.MetadataToken)! : method;
        }

        internal static string Method(MethodBase method)
        {
            var definition = Definition(method);
            var text = new StringBuilder(method.IsStatic ? "static " : "");
            text.Append(Type(method.DeclaringType)).Append("::").Append(method.Name);
            if (method.IsGenericMethod)
                text.Append('<').Append(method.IsGenericMethodDefinition ? method.GetGenericArguments().Length.ToString() :
                    string.Join(",", method.GetGenericArguments().Select(Type))).Append('>');
            text.Append('(').Append(string.Join(",", definition.GetParameters().Select(p => Type(p.ParameterType)))).Append(')');
            if (definition is MethodInfo returning) text.Append(Type(returning.ReturnType));
            return text.ToString();
        }

        internal static string Field(FieldInfo field)
        {
            var owner = field.DeclaringType;
            var definition = owner != null && owner.IsGenericType && !owner.IsGenericTypeDefinition ? field.Module.ResolveField(field.MetadataToken)! : field;
            return (field.IsStatic ? "static " : "") + Type(owner) + "::" + field.Name + ":" + Type(definition.FieldType);
        }

        internal static string Member(MemberInfo member) => member switch
        {
            MethodBase method => Method(method),
            FieldInfo field => Field(field),
            PropertyInfo property => "property " + Type(property.DeclaringType) + "::" + property.Name + ":" + Type(property.PropertyType) +
                "[" + string.Join(",", property.GetIndexParameters().Select(p => Type(p.ParameterType))) + "]",
            EventInfo e => "event " + Type(e.DeclaringType) + "::" + e.Name + ":" + Type(e.EventHandlerType),
            System.Type type => "type " + Type(type),
            _ => "member " + member.Name,
        };

        // Normalised method body: tokens become what they resolve to, strings
        // their text; branch offsets, constants, locals and clauses are kept.
        internal static string Body(MethodBase method)
        {
            var text = new StringBuilder(Method(method)).Append('\n');
            text.Append("attributes ").Append((int)(method.Attributes & ~MethodAttributes.ReservedMask)).Append(' ').Append((int)method.GetMethodImplementationFlags()).Append('\n');
            if ((method.Attributes & MethodAttributes.PinvokeImpl) != 0)
                // Module and entry point only: how each runtime fills the
                // pseudo-attribute's other defaults differs.
                foreach (var import in method.GetCustomAttributes(typeof(DllImportAttribute), false).Cast<DllImportAttribute>())
                    text.Append("import ").Append(import.Value).Append(' ').Append(import.EntryPoint).Append('\n');
            var body = method.GetMethodBody();
            if (body == null) return Hash(text.Append("no body").ToString());
            text.Append("stack ").Append(body.MaxStackSize).Append(" init ").Append(body.InitLocals).Append('\n');
            foreach (var local in body.LocalVariables) text.Append("local ").Append(Type(local.LocalType)).Append(local.IsPinned ? " pinned\n" : "\n");
            foreach (var clause in body.ExceptionHandlingClauses)
                text.Append("clause ").Append(clause.Flags).Append(' ').Append(clause.TryOffset).Append(' ').Append(clause.TryLength).Append(' ')
                    .Append(clause.HandlerOffset).Append(' ').Append(clause.HandlerLength).Append(' ')
                    .Append(clause.Flags == ExceptionHandlingClauseOptions.Clause ? Type(clause.CatchType) :
                        clause.Flags == ExceptionHandlingClauseOptions.Filter ? clause.FilterOffset.ToString() : "").Append('\n');
            var il = body.GetILAsByteArray() ?? Array.Empty<byte>();
            var owner = method.DeclaringType;
            var typeArguments = owner != null && owner.IsGenericType ? owner.GetGenericArguments() : null;
            var methodArguments = method is MethodInfo && method.IsGenericMethod ? method.GetGenericArguments() : null;
            foreach (var instruction in Decode(il))
            {
                text.Append(instruction.Code.Name);
                switch (instruction.Code.OperandType)
                {
                    case OperandType.InlineString: text.Append(" \"").Append(method.Module.ResolveString(instruction.Token).Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n")).Append('"'); break;
                    case OperandType.InlineMethod: case OperandType.InlineField: case OperandType.InlineType: case OperandType.InlineTok:
                        text.Append(' ').Append(Member(method.Module.ResolveMember(instruction.Token, typeArguments, methodArguments)!)); break;
                    case OperandType.InlineSig: text.Append(" sig ").Append(Hex(method.Module.ResolveSignature(instruction.Token))); break;
                    default: if (instruction.Operand.Length != 0) text.Append(' ').Append(Hex(instruction.Operand)); break;
                }
                text.Append('\n');
            }
            return Hash(text.ToString());
        }

        internal readonly struct Instruction
        {
            internal readonly OpCode Code; internal readonly byte[] Operand; internal readonly int Token;
            internal Instruction(OpCode code, byte[] operand) { Code = code; Operand = operand; Token = operand.Length == 4 ? BitConverter.ToInt32(operand, 0) : 0; }
        }

        internal static IEnumerable<Instruction> Decode(byte[] il)
        {
            for (var at = 0; at < il.Length;)
            {
                short value = il[at] == 0xFE && at + 1 < il.Length ? (short)(0xFE00 | il[at + 1]) : il[at];
                if (!OpCodesByValue.TryGetValue(value, out var code)) throw new InvalidDataException("Unknown IL opcode at " + at);
                at += code.Size;
                int size = code.OperandType switch
                {
                    OperandType.InlineNone => 0,
                    OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
                    OperandType.InlineVar => 2,
                    OperandType.InlineI8 or OperandType.InlineR => 8,
                    OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(il, at),
                    _ => 4,
                };
                if (at + size > il.Length) throw new InvalidDataException("Truncated IL operand at " + at);
                var operand = new byte[size]; Array.Copy(il, at, operand, 0, size); at += size;
                yield return new Instruction(code, operand);
            }
        }

        // Value types: kind, pack, size and ordered fields, nested value types
        // inline; Marshal sizes and offsets where every field is blittable.
        internal static string Layout(Type type)
        {
            var text = new StringBuilder();
            Layout(type, text, new HashSet<Type>());
            return text.ToString();
        }

        private static void Layout(Type type, StringBuilder text, HashSet<Type> seen)
        {
            if (!seen.Add(type)) { text.Append("(cycle ").Append(Type(type)).Append(')'); return; }
            var layout = type.StructLayoutAttribute;
            // Mono reports the x64 default packing as 8 where CoreCLR reports 0.
            var pack = layout == null ? 0 : layout.Pack == 0 ? 8 : layout.Pack;
            text.Append("layout ").Append(Type(type)).Append(' ').Append(layout?.Value).Append(" pack ").Append(pack).Append(" size ").Append(layout?.Size)
                .Append(' ').Append(layout?.CharSet).Append(" {");
            var fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).OrderBy(f => f.MetadataToken).ToArray();
            var blittable = Blittable(type, new HashSet<Type>());
            foreach (var field in fields)
            {
                text.Append(' ').Append(field.Name).Append(':').Append(Type(field.FieldType));
                if (blittable) text.Append('@').Append(Marshal.OffsetOf(type, field.Name).ToInt64());
                if (field.FieldType.IsValueType && !field.FieldType.IsPrimitive && !field.FieldType.IsEnum && field.FieldType.Assembly == type.Assembly)
                { text.Append('='); Layout(field.FieldType, text, seen); }
            }
            text.Append(" }");
            if (blittable) text.Append(" marshal ").Append(Marshal.SizeOf(type));
            seen.Remove(type);
        }

        private static bool Blittable(Type type, HashSet<Type> seen)
        {
            if (type.IsEnum || type.IsPointer || type == typeof(IntPtr) || type == typeof(UIntPtr)) return true;
            if (type.IsPrimitive) return type != typeof(bool) && type != typeof(char);
            if (!type.IsValueType || type.IsGenericType || !seen.Add(type)) return false;
            return type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).All(f => Blittable(f.FieldType, seen));
        }

        // Enum members, or a type's const fields, sorted by name.
        internal static string Literals(Type type)
        {
            var literals = type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.DeclaredOnly)
                .Where(f => f.IsLiteral && !(type.FullName == "KleiVersion" && f.Name == "ChangeList"))
                .OrderBy(f => f.Name, StringComparer.Ordinal)
                .Select(f => f.Name + "=" + Constant(f.GetRawConstantValue()));
            return (type.IsEnum ? "enum " + Type(Enum.GetUnderlyingType(type)) + " " : "consts ") + string.Join(";", literals);
        }

        // Long values ship as their hash; tools/GameContract --raw keeps the text for diffs.
        internal static string Compact(string value) => value.Length <= 96 ? value : "sha256:" + Hash(value);

        // Runtime-independent text for a constant: Mono and CoreCLR format
        // floating-point values differently, so those are their bits.
        internal static string Constant(object? value) => value switch
        {
            null => "null",
            float single => "f" + BitConverter.SingleToInt32Bits(single).ToString("x8"),
            double number => "d" + BitConverter.DoubleToInt64Bits(number).ToString("x16"),
            char character => "c" + ((int)character).ToString(System.Globalization.CultureInfo.InvariantCulture),
            string text => "\"" + text + "\"",
            _ => Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture) ?? "",
        };

        internal static string Hash(string text)
        {
            using var sha = SHA256.Create();
            return Hex(sha.ComputeHash(Encoding.UTF8.GetBytes(text)));
        }

        internal static string Hex(byte[] bytes) => BitConverter.ToString(bytes).Replace("-", "").ToLowerInvariant();
    }

    // What the contract covers and how each entry is recomputed.
    internal static class GameContractScope
    {
        // Assemblies whose method bodies are pinned; others (Unity, Harmony,
        // Newtonsoft) are checked for presence and signature only.
        internal static readonly string[] GameAssemblies = { "Assembly-CSharp", "Assembly-CSharp-firstpass" };
        internal const string Kind_Type = "type", Kind_Member = "member", Kind_Named = "named", Kind_Patched = "patched";

        // A type by reflection name in the game assemblies this mod was
        // built against, before any other loaded assembly.
        internal static Type? FindType(string name) =>
            typeof(KleiVersion).Assembly.GetType(name) ?? typeof(DlcManager).Assembly.GetType(name) ?? AccessTools.TypeByName(name);

        internal static bool IsGame(Assembly assembly) => Array.IndexOf(GameAssemblies, assembly.GetName().Name) >= 0;

        // Framework assemblies are the runtime's own and never part of the contract.
        internal static bool IsFramework(Assembly assembly)
        {
            var name = assembly.GetName().Name ?? "";
            return name == "mscorlib" || name == "netstandard" || name == "System" || name.StartsWith("System.", StringComparison.Ordinal) ||
                name.StartsWith("Mono.", StringComparison.Ordinal) || name == "System.Private.CoreLib" || name.StartsWith("Microsoft.", StringComparison.Ordinal);
        }

        internal static bool IsOwn(Assembly assembly) => (assembly.GetName().Name ?? "").StartsWith("MixedAtmospheres.", StringComparison.Ordinal);

        internal static bool InScope(Type? type)
        {
            while (type != null && (type.IsArray || type.IsByRef || type.IsPointer)) type = type.GetElementType();
            if (type == null || type.IsGenericParameter) return false;
            var assembly = (type.IsGenericType ? type.GetGenericTypeDefinition() : type).Assembly;
            return !IsFramework(assembly) && !IsOwn(assembly);
        }

        internal static string TypeValue(Type type)
        {
            var value = new StringBuilder("present");
            if (type.IsEnum) value.Append(' ').Append(GameContractCanon.Literals(type));
            else if (type.IsValueType && !type.IsPrimitive) value.Append(' ').Append(GameContractCanon.Layout(type));
            else if (IsGame(type.Assembly)) value.Append(' ').Append(GameContractCanon.Literals(type));
            return value.ToString();
        }

        internal static string MemberValue(MemberInfo member) => member is MethodBase method && method.DeclaringType != null && IsGame(method.DeclaringType.Assembly)
            ? GameContractCanon.Body(method) : member is FieldInfo field && field.IsLiteral
            ? "const " + GameContractCanon.Constant(field.GetRawConstantValue()) : "present";

        // Every member of a type that reflection by this name can reach.
        // Members a type declares or inherits from game and engine types.
        // Framework base classes (System.Object, ...) are each runtime's own
        // and differ between Mono and CoreCLR, so they are left out.
        internal static IEnumerable<MemberInfo> GameMembers(Type type, string? name = null)
        {
            for (var owner = type; owner != null && InScope(owner); owner = owner.BaseType)
                foreach (var member in name == null ? owner.GetMembers(GameContractCanon.Declared) : owner.GetMember(name, GameContractCanon.Declared))
                    yield return member;
        }

        internal static string NamedValue(Type type, string name)
        {
            var members = GameMembers(type, name).ToArray();
            if (members.Length == 0) return "absent";
            return string.Join("\n", members.Select(m => GameContractCanon.Member(m) + " " + MemberValue(m)).OrderBy(s => s, StringComparer.Ordinal));
        }

        // The originals Harmony's PatchAll would patch, found without patching
        // (mirrors Harmony 2.4's PatchClassProcessor; Prepare is not run, so
        // conditional patches are included). The game run checks afterwards
        // that every patched original is one of these.
        internal static List<MethodBase> PatchTargets(Assembly mod)
        {
            var targets = new List<MethodBase>();
            foreach (var type in AccessTools.GetTypesFromAssembly(mod))
            {
                // PatchAll processes only classes with a Harmony attribute.
                if (HarmonyMethodExtensions.GetFromType(type).Count == 0) continue;
                var patches = AccessTools.GetDeclaredMethods(type).Where(IsPatchMethod).ToList();
                if (patches.Count == 0) continue;
                var bulk = Auxiliary(type, "TargetMethods");
                if (bulk != null)
                {
                    var found = ((IEnumerable<MethodBase>?)bulk.Invoke(null, Array.Empty<object>()))?.ToList();
                    if (found == null || found.Any(m => m == null)) throw new InvalidOperationException("TargetMethods of " + type.FullName + " returned a null target.");
                    targets.AddRange(found);
                    continue;
                }
                var single = Auxiliary(type, "TargetMethod");
                if (single != null)
                {
                    if (single.Invoke(null, Array.Empty<object>()) is MethodBase target) targets.Add(target);
                    continue;
                }
                var container = HarmonyMethod.Merge(HarmonyMethodExtensions.GetFromType(type));
                if (!container.methodType.HasValue) container.methodType = MethodType.Normal;
                foreach (var patch in patches)
                {
                    var info = container.Merge(HarmonyMethod.Merge(HarmonyMethodExtensions.GetFromMethod(patch)));
                    targets.Add(Original(info) ?? throw new InvalidOperationException("Undefined target method for patch " + type.FullName + "." + patch.Name));
                }
            }
            return targets.Distinct().ToList();
        }

        private static readonly string[] PatchNames = { "Prefix", "Postfix", "Transpiler", "Finalizer", "ReversePatch", "InnerPrefix", "InnerPostfix" };
        private static bool IsPatchMethod(MethodInfo method) => PatchNames.Contains(method.Name) ||
            method.GetCustomAttributes(true).Any(a => PatchNames.Any(n => a.GetType().FullName == "HarmonyLib.Harmony" + n));

        private static MethodInfo? Auxiliary(Type type, string name)
        {
            var attributed = type.GetMethods(AccessTools.all).FirstOrDefault(m => m.GetCustomAttributes(true).Any(a => a.GetType().FullName == "HarmonyLib.Harmony" + name));
            return attributed ?? type.GetMethod(name, AccessTools.all);
        }

        private static MethodBase? Original(HarmonyMethod info)
        {
            if (info.declaringType == null) return null;
            switch (info.methodType ?? MethodType.Normal)
            {
                case MethodType.Normal: return string.IsNullOrEmpty(info.methodName) ? null : AccessTools.DeclaredMethod(info.declaringType, info.methodName, info.argumentTypes);
                case MethodType.Getter: return string.IsNullOrEmpty(info.methodName) ? AccessTools.DeclaredIndexerGetter(info.declaringType, info.argumentTypes) : AccessTools.DeclaredPropertyGetter(info.declaringType, info.methodName);
                case MethodType.Setter: return string.IsNullOrEmpty(info.methodName) ? AccessTools.DeclaredIndexerSetter(info.declaringType, info.argumentTypes) : AccessTools.DeclaredPropertySetter(info.declaringType, info.methodName);
                case MethodType.Constructor: return AccessTools.DeclaredConstructor(info.declaringType, info.argumentTypes);
                case MethodType.StaticConstructor: return AccessTools.GetDeclaredConstructors(info.declaringType).FirstOrDefault(c => c.IsStatic);
                default: throw new NotSupportedException("Patch method type " + info.methodType + " is outside the game contract.");
            }
        }

        // Manual Harmony.Patch targets installed during OnLoad, declared by
        // their installers so they are verified before they are patched.
        internal static IEnumerable<MethodBase> ManualTargets() => MixtureWorldRendering.ContractTargets().Concat(OwnedReservoirOwnerMarker.ContractTargets());
    }

    [JsonObject(MemberSerialization.OptIn)]
    internal sealed class GameContractEntry
    {
        [JsonProperty] public string Kind = "";
        [JsonProperty] public int Token;
        [JsonProperty] public int Context;
        [JsonProperty] public string TypeName = "";
        [JsonProperty] public string Name = "";
        [JsonProperty] public string Key = "";
        [JsonProperty] public string Value = "";
    }

    [JsonObject(MemberSerialization.OptIn)]
    internal sealed class GameContractPlatform
    {
        [JsonProperty] public int GameBuild;
        [JsonProperty] public string AssemblyCSharpSha256 = "";
        [JsonProperty] public List<GameContractEntry> Entries = new List<GameContractEntry>();
        [JsonProperty] public List<string> Unverified = new List<string>();

        // Written without Newtonsoft: the game's Unity build of it cannot
        // serialize inside tools/GameContract's CoreCLR host.
        internal string ToJson()
        {
            var text = new System.Text.StringBuilder("{\n  \"GameBuild\": ").Append(GameBuild).Append(",\n  \"AssemblyCSharpSha256\": ")
                .Append(Quote(AssemblyCSharpSha256)).Append(",\n  \"Entries\": [");
            for (var i = 0; i < Entries.Count; ++i)
            {
                var e = Entries[i];
                text.Append(i == 0 ? "\n" : ",\n").Append("    { \"Kind\": ").Append(Quote(e.Kind)).Append(", \"Token\": ").Append(e.Token).Append(", \"Context\": ").Append(e.Context)
                    .Append(", \"TypeName\": ").Append(Quote(e.TypeName)).Append(", \"Name\": ").Append(Quote(e.Name))
                    .Append(", \"Key\": ").Append(Quote(e.Key)).Append(", \"Value\": ").Append(Quote(e.Value)).Append(" }");
            }
            text.Append("\n  ],\n  \"Unverified\": [").Append(string.Join(", ", Unverified.Select(Quote))).Append("]\n}");
            return text.ToString();
        }

        private static string Quote(string value)
        {
            var text = new System.Text.StringBuilder("\"");
            foreach (var c in value)
                switch (c)
                {
                    case '"': text.Append("\\\""); break;
                    case '\\': text.Append("\\\\"); break;
                    case '\n': text.Append("\\n"); break;
                    case '\r': text.Append("\\r"); break;
                    case '\t': text.Append("\\t"); break;
                    default:
                        if (c < 0x20) text.Append("\\u").Append(((int)c).ToString("x4")); else text.Append(c);
                        break;
                }
            return text.Append('"').ToString();
        }
    }

    [JsonObject(MemberSerialization.OptIn)]
    internal sealed class GameContractFile
    {
        internal const string FileName = "game-contract.json";
        [JsonProperty] public int SchemaVersion = 1;
        [JsonProperty] public string ModuleVersionId = "";
        [JsonProperty] public Dictionary<string, GameContractPlatform> Platforms = new Dictionary<string, GameContractPlatform>();

        internal static GameContractFile Parse(string text)
        {
            var file = JsonConvert.DeserializeObject<GameContractFile>(text, new JsonSerializerSettings
            { MissingMemberHandling = MissingMemberHandling.Error, TypeNameHandling = TypeNameHandling.None })
                ?? throw new InvalidDataException("The game contract is empty.");
            if (file.SchemaVersion != 1) throw new NotSupportedException("Unsupported game contract schema " + file.SchemaVersion + ".");
            return file;
        }

        internal string ToJson() => JsonConvert.SerializeObject(this, Formatting.Indented);
    }
}
