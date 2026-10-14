// Original metadata-only audit. This never loads or executes the inspected assembly.
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;

if ((args.Length != 1 && (args.Length != 3 || args[1] != "--calls-to")) || !File.Exists(args[0]))
{
    Console.Error.WriteLine("Usage: dotnet run --project tools/InteropAudit -- <licensed-assembly-path> [--calls-to comma,separated,qualified.methods]");
    return 2;
}
using var stream = File.OpenRead(args[0]);
var hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
stream.Position = 0;
using var pe = new PEReader(stream);
var md = pe.GetMetadataReader();
var opcodes = typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)
    .Where(f => f.FieldType == typeof(OpCode)).Select(f => (OpCode)f.GetValue(null)!)
    .ToDictionary(o => (ushort)o.Value);
string TypeName(EntityHandle handle)
{
    if (handle.Kind == HandleKind.TypeDefinition)
    {
        var type = md.GetTypeDefinition((TypeDefinitionHandle)handle);
        return type.GetDeclaringType().IsNil ? md.GetString(type.Name)
            : TypeName(type.GetDeclaringType()) + "+" + md.GetString(type.Name);
    }
    if (handle.Kind == HandleKind.TypeReference)
    {
        var type = md.GetTypeReference((TypeReferenceHandle)handle);
        return md.GetString(type.Namespace) + "." + md.GetString(type.Name);
    }
    return "<unresolved:" + handle.Kind + ">";
}
string Name(EntityHandle handle)
{
    if (handle.Kind == HandleKind.MethodSpecification)
        return Name(md.GetMethodSpecification((MethodSpecificationHandle)handle).Method);
    if (handle.Kind == HandleKind.MethodDefinition)
    {
        var method = md.GetMethodDefinition((MethodDefinitionHandle)handle);
        return TypeName(method.GetDeclaringType()) + "." + md.GetString(method.Name);
    }
    if (handle.Kind == HandleKind.FieldDefinition)
    {
        var field = md.GetFieldDefinition((FieldDefinitionHandle)handle);
        return TypeName(field.GetDeclaringType()) + "." + md.GetString(field.Name);
    }
    if (handle.Kind == HandleKind.MemberReference)
    {
        var member = md.GetMemberReference((MemberReferenceHandle)handle);
        return TypeName(member.Parent) + "." + md.GetString(member.Name);
    }
    return "<unresolved:" + handle.Kind + ">";
}
var imports = new Dictionary<string, object>();
foreach (var handle in md.MethodDefinitions)
{
    var method = md.GetMethodDefinition(handle);
    if ((method.Attributes & MethodAttributes.PinvokeImpl) == 0) continue;
    var import = method.GetImport();
    imports.Add(Name(handle), new
    {
        method = Name(handle), module = md.GetString(md.GetModuleReference(import.Module).Name),
        entryPoint = md.GetString(import.Name), attributes = import.Attributes.ToString()
    });
}
var rawGridFields = new HashSet<string>(new[]
{
    "elementIdx", "temperature", "mass", "radiation", "properties", "strengthInfo",
    "insulation", "diseaseIdx", "diseaseCount", "exposedToSunlight", "AccumulatedFlowValues"
}.Select(n => "Grid." + n));
var selectedCallTargets = args.Length == 3 ? new HashSet<string>(args[2].Split(',', StringSplitOptions.RemoveEmptyEntries)) : new HashSet<string>();
var selectedCalls = new List<object>();
var nativeCalls = new List<object>();
var gridAssignments = new List<object>();
var possiblePointerWriters = new List<object>();
var unresolvedInstructions = new List<object>();
var indirectCalls = new List<object>();
var unresolvedInstructionCount = 0;
var reflectionOrDelegateCalls = new Dictionary<string, int>();
var methodsScanned = 0;
foreach (var handle in md.MethodDefinitions)
{
    var definition = md.GetMethodDefinition(handle);
    if (definition.RelativeVirtualAddress == 0) continue;
    var method = Name(handle);
    var il = pe.GetMethodBody(definition.RelativeVirtualAddress).GetILBytes()!;
    var instructions = new List<(int offset, string opcode, string target)>();
    methodsScanned++;
    for (int index = 0; index < il.Length;)
    {
        int offset = index;
        ushort value = il[index++];
        if (value == 0xfe) value = (ushort)(0xfe00 | il[index++]);
        var opcode = opcodes[value];
        int length = opcode.OperandType switch
        {
            OperandType.InlineNone => 0,
            OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
            OperandType.InlineVar => 2,
            OperandType.InlineI8 or OperandType.InlineR => 8,
            OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(il, index),
            _ => 4
        };
        string target = "";
        if (opcode.OperandType is OperandType.InlineMethod or OperandType.InlineField or OperandType.InlineTok)
        {
            target = Name(MetadataTokens.EntityHandle(BitConverter.ToInt32(il, index)));
            if (target.Contains("<unresolved:"))
            {
                unresolvedInstructionCount++;
                if (unresolvedInstructions.Count < 100) unresolvedInstructions.Add(new { method, offset, target });
            }
        }
        instructions.Add((offset, opcode.Name!, target));
        index += length;
    }
    foreach (var instruction in instructions)
    {
        var (offset, opcode, target) = instruction;
        if (opcode is "call" or "callvirt" or "ldftn" or "ldvirtftn")
        {
            if (imports.ContainsKey(target)) nativeCalls.Add(new { method, offset, opcode, target });
            if (selectedCallTargets.Contains(target)) selectedCalls.Add(new { method, offset, opcode, target });
            if (target.EndsWith(".Invoke") || target.Contains("Reflection.") || target.EndsWith(".GetMethod"))
                reflectionOrDelegateCalls[target] = reflectionOrDelegateCalls.GetValueOrDefault(target) + 1;
        }
        if (opcode == "calli") indirectCalls.Add(new { method, offset, opcode });
        if (opcode is "stsfld" or "stfld" && target.StartsWith("Grid."))
            gridAssignments.Add(new { method, offset, target, rawSimulationPointer = rawGridFields.Contains(target) });
    }
    // A conservative candidate filter, NOT pointer alias/data-flow analysis. Many stores target out parameters.
    var fields = instructions.Where(i => i.opcode is "ldsfld" or "ldsflda" && rawGridFields.Contains(i.target))
        .Select(i => i.target).Distinct().ToArray();
    var stores = instructions.Where(i => i.opcode.StartsWith("stind") || i.opcode is "stobj" or "cpblk" or "initblk")
        .Select(i => new { i.offset, i.opcode }).ToArray();
    if (fields.Length > 0 && stores.Length > 0)
        possiblePointerWriters.Add(new { method, rawGridFieldsRead = fields, indirectStores = stores });
}
Console.WriteLine(JsonSerializer.Serialize(new
{
    schemaVersion = 1, assembly = Path.GetFullPath(args[0]), sha256 = hash, methodsScanned,
    nativeImports = imports.Values, nativeCalls, selectedCallTargets, selectedCalls, gridFieldAssignments = gridAssignments,
    possibleRawGridPointerWriters = possiblePointerWriters, unresolvedInstructionCount,
    unresolvedInstructionSamples = unresolvedInstructions, indirectCalls,
    reflectionOrDelegateCalls,
    limitations = new[]
    {
        "Metadata and IL facts only; no game code executed and no native implementation analyzed.",
        "Raw-pointer candidates are same-method read/store co-occurrence; they are not proven writes to Grid.",
        "Does not prove absence of aliased pointer writes, array/cache writes, native writers or calls in other assemblies.",
        "Indirect calls, delegate/reflection dispatch, generated runtime code and other mods need separate review.",
        "Member references with unresolved generic TypeSpec parents are reported, not silently considered safe."
    }
}, new JsonSerializerOptions { WriteIndented = true }));
return 0;
