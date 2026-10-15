using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

if(args.Length<1 || args.Length>2) throw new ArgumentException("Pass the licensed ONI directory and optional privately built Oni DLL.");
var game=Path.Combine(Path.GetFullPath(args[0]),"OxygenNotIncluded_Data","Managed");
var oni=args.Length==2?Path.GetFullPath(args[1]):null;
AssemblyLoadContext.Default.Resolving+=(_,name)=>
{
    foreach(var directory in oni==null?new[]{game}:new[]{Path.GetDirectoryName(oni)!,game})
    {var file=Path.Combine(directory,name.Name+".dll");if(File.Exists(file))return AssemblyLoadContext.Default.LoadFromAssemblyPath(file);}
    return null;
};
var assemblyPath=Path.Combine(game,"Assembly-CSharp.dll");
var assemblyHash=Hash(assemblyPath);
Check(assemblyHash=="a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599","Wrong licensed Windows assembly.");
var corePath=Path.Combine(game,"UnityEngine.CoreModule.dll");
var coreHash=Hash(corePath);
Check(coreHash=="a9613c2b0f1258ac7f7d9323181f816f5849beb24114e4a7665037fd11ddd488","Wrong licensed Unity scene/animation declaration module.");
var assembly=AssemblyLoadContext.Default.LoadFromAssemblyPath(assemblyPath);
var sim=assembly.GetType("Sim",true)!;
var initialize=sim.GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.DeclaredOnly).Single(m=>m.Name=="SIM_Initialize");
Check(initialize.ReturnType==typeof(void) && initialize.GetParameters().Length==1
    && initialize.GetParameters()[0].ParameterType.FullName=="Sim+GAME_MessageHandler" && initialize.GetMethodBody()==null,"Wrong native initialization declaration.");
var opcodes=typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static).Where(f=>f.FieldType==typeof(OpCode))
    .Select(f=>(OpCode)f.GetValue(null)!).ToDictionary(o=>unchecked((ushort)o.Value));
var callers=new List<(MethodBase method,int calls)>();
var methodsScanned=0;
foreach(var type in assembly.GetTypes())
foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly).Cast<MethodBase>()
    .Concat(type.GetConstructors(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly)))
{
    var il=method.GetMethodBody()?.GetILAsByteArray();if(il==null)continue;++methodsScanned;
    var calls=Calls(il,method.Module,initialize,opcodes);if(calls>0)callers.Add((method,calls));
}
var descriptions=callers.OrderBy(c=>c.method.DeclaringType!.FullName,StringComparer.Ordinal).ThenBy(c=>c.method.ToString(),StringComparer.Ordinal)
    .Select(c=>new{Type=c.method.DeclaringType!.FullName,Name=c.method.Name,IsStatic=c.method.IsStatic,
        Returns=c.method is MethodInfo m?m.ReturnType.FullName:"<constructor>",Parameters=c.method.GetParameters().Select(p=>p.ParameterType.FullName).ToArray(),
        ParameterAssemblies=c.method.GetParameters().Select(p=>p.ParameterType.Assembly.GetName().Name).ToArray(),
        Calls=c.calls,MetadataToken=c.method.MetadataToken,BodySha256=Convert.ToHexStringLower(SHA256.HashData(c.method.GetMethodBody()!.GetILAsByteArray()!))}).ToArray();
Console.WriteLine(JsonSerializer.Serialize(new{Kind="Independent pinned initialization caller enumeration",AssemblySha256=assemblyHash,MethodsScanned=methodsScanned,Callers=descriptions,
    InstallInvoked=false,HarmonyPatchInvoked=false,NativeInvoked=false,GameInvoked=false},new JsonSerializerOptions{WriteIndented=true}));
if(oni!=null)
{
    Check(callers.Count==4 && callers.All(c=>c.calls==1),"Expected all four exact one-call initialization bodies.");
    Check(callers.Count(c=>c.method.DeclaringType!.FullName=="SaveLoader")==3
        && callers.Count(c=>c.method.DeclaringType!.FullName=="ProcGenGame.WorldGenSimUtil"&&c.method.Name=="DoSettleSim")==1,"Unexpected initialization identity.");
    var product=AssemblyLoadContext.Default.LoadFromAssemblyPath(oni);
    var helper=product.GetType("MixedAtmospheres.Oni.OwnedReservoirLoadValidationCounters",true)!;
    var inspect=helper.GetMethod("InspectContracts",BindingFlags.NonPublic|BindingFlags.Static)!;
    Check(inspect!=null && inspect.GetParameters().Length==0,"Pure preflight API missing.");
    var closure=helper.GetMethod("VerifyInitializationClosure",BindingFlags.NonPublic|BindingFlags.Static)!;
    var expected=callers.Select(c=>(MethodInfo)c.method).ToArray();
    closure.Invoke(null,new object?[]{initialize,expected});
    foreach(var invalid in new[]{expected.Where(m=>m.DeclaringType!.FullName=="SaveLoader").ToArray(),expected.Concat(new[]{expected[0]}).ToArray(),expected.Skip(1).Append(expected[1]).ToArray()})
    {
        try{closure.Invoke(null,new object?[]{initialize,invalid});throw new Exception("An incomplete/duplicate closure passed");}
        catch(TargetInvocationException error){Check(error.InnerException is NotSupportedException,"Unexpected closure rejection: "+error.InnerException);}
    }
    Console.WriteLine("PASS complete four-caller closure accepts; former three-caller and duplicate/missing lists reject");
    var preflight=inspect!.Invoke(null,null)!;
    var type=preflight.GetType();
    T Read<T>(string name)=>(T)type.GetProperty(name,BindingFlags.Public|BindingFlags.Instance)!.GetValue(preflight)!;
    Check(Read<int>("ExpectedMethodCount")==45 && Read<int>("ExpectedNativeMapKeyCount")==15
        && Read<int>("ExpectedManagedAntecedentKeyCount")==4 && Read<int>("ExpectedNativeSiteSpecifications")==12,"Incomplete exact contract cardinality.");
    var keys=Read<IReadOnlyList<string>>("MethodKeys");var initKeys=Read<IReadOnlyList<string>>("InitializationCallerKeys");
    Check(keys.Count==45 && keys.Distinct(StringComparer.Ordinal).Count()==45 && initKeys.Count==4,"Missing/duplicate contract keys.");
    var key=helper.GetMethod("Key",BindingFlags.NonPublic|BindingFlags.Static)!;
    foreach(var caller in callers)
    {
        var actual=(string)key.Invoke(null,new object?[]{caller.method.DeclaringType,caller.method.Name,caller.method.GetParameters().Select(p=>p.ParameterType).ToArray()})!;
        Check(initKeys.Contains(actual) && keys.Contains(actual),"Independent initialization caller not instrumented: "+actual);
    }
    var core=AssemblyLoadContext.Default.LoadFromAssemblyPath(corePath);
    var firstpass=AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(game,"Assembly-CSharp-firstpass.dll"));
    foreach(var expectedTarget in new[]{
        (type:assembly.GetType("Game",true)!,name:"OnPrefabInit",parameters:Type.EmptyTypes,isStatic:false,result:typeof(void)),
        (type:firstpass.GetType("KMonoBehaviour",true)!,name:"OnDestroy",parameters:Type.EmptyTypes,isStatic:false,result:typeof(void)),
        (type:core.GetType("UnityEngine.SceneManagement.SceneManager",true)!,name:"Internal_SceneUnloaded",parameters:new[]{core.GetType("UnityEngine.SceneManagement.Scene",true)!},isStatic:true,result:typeof(void)),
        (type:assembly.GetType("KBatchedAnimUpdater",true)!,name:"DoGridProcessing",parameters:Type.EmptyTypes,isStatic:false,result:typeof(bool))})
    {
        var method=expectedTarget.type.GetMethod(expectedTarget.name,BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly,expectedTarget.parameters)!;
        Check(method!=null && method.ReturnType==expectedTarget.result && method.IsStatic==expectedTarget.isStatic && method.GetMethodBody()!=null,"Missing exact lifecycle/animation original body.");
        var actual=(string)key.Invoke(null,new object?[]{expectedTarget.type,expectedTarget.name,expectedTarget.parameters})!;
        Check(keys.Contains(actual),"Uninstrumented lifecycle/animation body: "+actual);
        var returnCounts=Read<IReadOnlyDictionary<string,int>>("OriginalReturnSiteCounts");
        Check(returnCounts[actual]>0,"Lifecycle/animation body has no instrumented normal return.");
    }
    Console.WriteLine("PASS independent exact loader/scene/Game/animation bodies are included without invocation");
    var native=Read<IReadOnlyDictionary<string,int>>("NativeSiteCounts");Check(native.Count==12 && native.Values.All(n=>n==1),"Native-site preflight incomplete.");
    var installed=helper.GetProperty("Installed",BindingFlags.NonPublic|BindingFlags.Static)!;
    Check(installed.GetValue(null) is false,"Pure inspection installed hooks.");
    foreach(var registry in new[]{"Contracts","Counts","NativeSites","AntecedentSites"})
        Check(((System.Collections.IDictionary)helper.GetField(registry,BindingFlags.NonPublic|BindingFlags.Static)!.GetValue(null)!).Count==0,"Pure inspection populated runtime counter registry: "+registry);
    var again=inspect.Invoke(null,null)!;
    Check(JsonSerializer.Serialize(again,type)==JsonSerializer.Serialize(preflight,type),"Pure preflight changed its contract.");
    Console.WriteLine(JsonSerializer.Serialize(preflight,type,new JsonSerializerOptions{WriteIndented=true}));
    Console.WriteLine("PASS independent caller closure and complete pure helper contracts");
    Console.WriteLine("PASS repeated immutable preflight leaves installation and runtime registries untouched");
}
Check(Hash(assemblyPath)==assemblyHash,"Licensed assembly changed.");
Check(Hash(corePath)==coreHash,"Licensed Unity scene/animation module changed.");
return 0;

static void Check(bool condition,string message){if(!condition)throw new InvalidOperationException(message);}
static string Hash(string path)=>Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));
static int Calls(byte[] bytes,Module module,MethodInfo native,Dictionary<ushort,OpCode> codes)
{
    var position=0;var count=0;
    while(position<bytes.Length)
    {
        ushort encoded=bytes[position++];if(encoded==0xfe){Check(position<bytes.Length,"Truncated opcode.");encoded=(ushort)(0xfe00|bytes[position++]);}
        Check(codes.TryGetValue(encoded,out var code),"Unknown opcode.");var operand=position;
        int size=code.OperandType switch
        {
            OperandType.InlineNone=>0,
            OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar=>1,
            OperandType.InlineVar=>2,
            OperandType.InlineI8 or OperandType.InlineR=>8,
            OperandType.InlineSwitch=>SwitchSize(bytes,position),
            OperandType.InlineBrTarget or OperandType.InlineField or OperandType.InlineI or OperandType.InlineMethod or OperandType.InlineSig
                or OperandType.InlineString or OperandType.InlineTok or OperandType.InlineType or OperandType.ShortInlineR=>4,
            _=>throw new NotSupportedException("Unknown operand.")
        };
        Check(size<=bytes.Length-position,"Truncated operand.");
        if(code==OpCodes.Call || code==OpCodes.Callvirt)
        {
            var token=BitConverter.ToInt32(bytes,operand);
            if(module==native.Module && token==native.MetadataToken)++count;
        }
        position+=size;
    }
    return count;
}
static int SwitchSize(byte[] bytes,int position)
{
    Check(bytes.Length-position>=4,"Truncated switch.");var count=BitConverter.ToInt32(bytes,position);
    Check(count>=0 && count<=(bytes.Length-position-4)/4,"Malformed switch.");return 4+4*count;
}
