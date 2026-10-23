using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using HarmonyLib;
using MixedAtmospheres.Oni;

internal static class Program
{
    static int Main(string[] args)
    {
        if(args.Length!=1)throw new ArgumentException("Pass the licensed ONI installation path.");
        var managed=Path.Combine(Path.GetFullPath(args[0]),"OxygenNotIncluded_Data","Managed");
        AssemblyLoadContext.Default.Resolving+=(_,name)=>
        {
            var file=Path.Combine(managed,name.Name+".dll");return File.Exists(file)?AssemblyLoadContext.Default.LoadFromAssemblyPath(file):null;
        };
        return Run();
    }
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    [MethodImpl(MethodImplOptions.NoInlining)]
    static int Run()
    {
        var assembly=typeof(MixtureInspection).Assembly;
        var hook=assembly.GetType("MixedAtmospheres.Oni.ColonyMixedPipeFlowPatch")!;
        var transpiler=hook.GetMethod("Transpiler",BindingFlags.NonPublic|BindingFlags.Static)!;
        var original=AccessTools.Method(typeof(ConduitFlow),"UpdateConduit");
        var before=PatchProcessor.GetOriginalInstructions(original);
        var after=((IEnumerable<CodeInstruction>)transpiler.Invoke(null,new object[]{before,original})!).ToArray();
        var cursor=0;
        foreach(var item in after)if(cursor<before.Count&&ReferenceEquals(before[cursor],item))++cursor;
        Check(cursor==before.Count,"Native topology/scheduling instructions were removed or reordered.");
        var calls=after.Where(i=>i.operand is MethodInfo method&&method.DeclaringType?.Name=="ColonyConduits")
            .Select(i=>((MethodInfo)i.operand).Name).ToArray();
        Check(calls.SequenceEqual(new[]{"BeginCell","BeginCell","Compatible","Limit","Move","FinishTarget","FinishCell"}),"Owned payload hooks do not surround the actual native move.");
        var broken=before.Select(i=>i.opcode==OpCodes.Ceq?new CodeInstruction(OpCodes.Nop):i).ToArray();
        try
        {
            _=((IEnumerable<CodeInstruction>)transpiler.Invoke(null,new object[]{broken,original})!).ToArray();
            throw new Exception("Unknown native body accepted scalar mixing.");
        }
        catch(InvalidOperationException){}
        Console.WriteLine("PASS native pipe move contract: every original instruction retained, ownership surrounds actual scalar move, unknown bodies refuse");

        var runtime=assembly.GetType("MixedAtmospheres.Oni.ColonyConduits")!;
        var read=runtime.GetField("ReadGrid",BindingFlags.NonPublic|BindingFlags.Static)!.GetValue(null) as Delegate;
        var flow=(ConduitFlow)RuntimeHelpers.GetUninitializedObject(typeof(ConduitFlow));
        var gridField=AccessTools.Field(typeof(ConduitFlow),"grid");
        var nodeType=gridField.FieldType.GetElementType()!;var grid=Array.CreateInstance(nodeType,2);
        var node=Activator.CreateInstance(nodeType)!;var contents=new ConduitFlow.ConduitContents(SimHashes.Oxygen,.5f,300,255,0);
        AccessTools.Field(nodeType,"contents").SetValue(node,contents);grid.SetValue(node,1);gridField.SetValue(flow,grid);
        var copied=(ConduitFlow.ConduitContents)read!.DynamicInvoke(flow,1)!;
        Check(copied.element==contents.element&&copied.mass==contents.mass&&copied.temperature==contents.temperature,"Cached grid accessor mismatches actual native layout.");
        object?[] arguments={flow,1,contents};runtime.GetMethod("ReadNative",BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,arguments);
        var passive=(ConduitFlow.ConduitContents)arguments[2]!;
        Check(passive.element==contents.element&&passive.mass==contents.mass&&passive.temperature==contents.temperature,"Inactive ownership changed a legacy pipe reading.");
        Console.WriteLine("PASS native pipe layout: cached accessor reads actual private grid; inactive hooks preserve legacy contents");
        var flags=BindingFlags.NonPublic|BindingFlags.Static;
        var geometryType=assembly.GetType("MixedAtmospheres.Oni.PipeGeometry")!;
        var geometry=Activator.CreateInstance(geometryType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{2u,1u,0u,0u,1u,1u},null);
        runtime.GetField("geometry",flags)!.SetValue(null,geometry);
        var flowType=runtime.GetNestedType("Flow",BindingFlags.NonPublic)!;
        AccessTools.Field(typeof(ConduitFlow),"MaxMass").SetValue(flow,1f);
        var state=Activator.CreateInstance(flowType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{2,flow},null);
        var flows=(System.Collections.IDictionary)runtime.GetField("Flows",flags)!.GetValue(null)!;flows.Add(flow,state);
        runtime.GetProperty("Active",flags)!.SetValue(null,true);
        try
        {
            var allowed=(bool)runtime.GetMethod("Compatible",flags)!.Invoke(null,new object[]{true,flow,0,1})!;
            var limited=(float)runtime.GetMethod("Limit",flags)!.Invoke(null,new object[]{flow,0,1,.5f})!;
            Check(!allowed&&limited==0,"A scalar native edge crossed the owned boundary without a conservative port job.");
            var materialType=assembly.GetType("MixedAtmospheres.Oni.PipeMaterial")!;
            const int ordinal=176;
            Check((int)SimHashes.Oxygen<0,"Negative-hash adversarial fixture no longer selects a negative game hash.");
            var material=Activator.CreateInstance(materialType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{ordinal,1000.0,0.0},null)!;
            var materials=(System.Collections.IDictionary)runtime.GetField("Materials",flags)!.GetValue(null)!;materials.Add(ordinal,material);
            var hashes=(System.Collections.IDictionary)runtime.GetField("NativeElements",flags)!.GetValue(null)!;hashes.Add(ordinal,SimHashes.Oxygen);
            var inventoryType=assembly.GetType("MixedAtmospheres.Oni.PipeInventory")!;
            var inventory=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{material,.5,300.0,0,0.0})!;
            var cellType=runtime.GetNestedType("Cell",BindingFlags.NonPublic)!;
            var owned=Activator.CreateInstance(cellType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{inventory,contents},null)!;
            var cells=(Array)AccessTools.Field(flowType,"Cells").GetValue(state)!;cells.SetValue(owned,0);
            for(var repeat=0;repeat<100;++repeat)
            {
                object?[] inspected={flow,0,new ConduitFlow.ConduitContents(SimHashes.Oxygen,.5f,350,255,0)};
                runtime.GetMethod("ReadNative",flags)!.Invoke(null,inspected);
                Check(((ConduitFlow.ConduitContents)inspected[2]!).temperature==300,"A reader imported uncommitted native heat into its projection.");
                Check(((ConduitFlow.ConduitContents)inspected[2]!).element==SimHashes.Oxygen,"Ordinal pipe state was cast to a negative-hash compatibility element.");
                Check(ReferenceEquals(AccessTools.Field(cellType,"Inventory").GetValue(owned),inventory),"Inspection changed physical pipe inventory.");
            }
            Check(((ConduitFlow.ConduitContents)AccessTools.Field(cellType,"Shadow").GetValue(owned)!).temperature==300,
                "Inspection changed the native reconciliation boundary.");
            object staged=new ConduitFlow.ConduitContents(SimHashes.Oxygen,.25f,300,255,0);
            AccessTools.Field(typeof(ConduitFlow.ConduitContents),"added_mass").SetValue(staged,MathF.BitDecrement(MathF.BitDecrement(.25f)));
            object?[] stagingRead={flow,0,staged};runtime.GetMethod("ReadNative",flags)!.Invoke(null,stagingRead);
            var stagedProjection=(ConduitFlow.ConduitContents)stagingRead[2]!;
            var movable=(Delegate)runtime.GetField("Movable",flags)!.GetValue(null)!;
            Check(stagedProjection.mass==.5f&&(float)movable.DynamicInvoke(stagedProjection)! == .25f,
                "A one-ulp scalar correction made recently received mass movable before its native tick.");
            var pumpRuntime=assembly.GetType("MixedAtmospheres.Oni.ColonyPipePumps")!;
            var portsRuntime=assembly.GetType("MixedAtmospheres.Oni.ColonyPipePorts")!;
            pumpRuntime.GetProperty("Enabled",flags)!.SetValue(null,true);
            var consumer=(ElementConsumer)RuntimeHelpers.GetUninitializedObject(typeof(ElementConsumer));
            var storage=(Storage)RuntimeHelpers.GetUninitializedObject(typeof(Storage));
            var dispenser=(ConduitDispenser)RuntimeHelpers.GetUninitializedObject(typeof(ConduitDispenser));
            consumer.storage=storage;dispenser.storage=storage;dispenser.isOn=false;
            var pumpStateType=pumpRuntime.GetNestedType("State",BindingFlags.NonPublic)!;
            var pumpState=Activator.CreateInstance(pumpStateType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{consumer,dispenser,123,0,(byte)1,flow},null)!;
            var bufferType=portsRuntime.GetNestedType("Buffer",BindingFlags.NonPublic)!;
            var buffer=Activator.CreateInstance(bufferType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{(byte)1,0,inventory},null)!;
            ((System.Collections.IDictionary)portsRuntime.GetField("Buffers",flags)!.GetValue(null)!).Add(999UL,buffer);
            ((System.Collections.IList)AccessTools.Field(pumpStateType,"Buffers").GetValue(pumpState)!).Add(999UL);
            ((System.Collections.IDictionary)pumpRuntime.GetField("ByOutlet",flags)!.GetValue(null)!).Add((flow,0),pumpState);
            var full=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{material,1.0,300.0,0,0.0})!;
            AccessTools.Field(cellType,"Inventory").SetValue(owned,full);
            pumpRuntime.GetMethod("ProjectPipe",flags)!.Invoke(null,new object[]{flow,0});
            Check(dispenser.blocked&&!dispenser.empty,"Inactive full pump status was not invalidated by an outlet publication.");
            AccessTools.Field(cellType,"Inventory").SetValue(owned,inventory);
            pumpRuntime.GetMethod("ProjectPipe",flags)!.Invoke(null,new object[]{flow,0});
            Check(!dispenser.blocked&&!dispenser.empty,"Opening outlet room retained stale blocked state.");
            var empty=inventoryType.GetField("Empty",flags)!.GetValue(null)!;AccessTools.Field(bufferType,"Inventory").SetValue(buffer,empty);
            pumpRuntime.GetMethod("ProjectPipe",flags)!.Invoke(null,new object[]{flow,0});
            Check(dispenser.empty&&!dispenser.blocked&&ReferenceEquals(AccessTools.Field(cellType,"Inventory").GetValue(owned),inventory),
                "Owned pump status changed a physical inventory or retained a stale empty state.");
            Console.WriteLine("PASS pump status boundary: inactive/full, reopened outlet and emptied buffer update readers without changing inventories");
        }
        finally{runtime.GetMethod("Reset",flags)!.Invoke(null,null);}
        Console.WriteLine("PASS owned pipe boundary: ordinary scalar native edges cannot cross a world ownership boundary");
        Console.WriteLine("PASS pipe readers: repeated inspection cannot import native heat or mutate authoritative inventory/reconciliation state");
        var materialRecord=assembly.GetType("MixedAtmospheres.Oni.OniNativeColony+Material")!;
        var record=Activator.CreateInstance(materialRecord)!;
        materialRecord.GetField("ElementHash")!.SetValue(record,(int)SimHashes.Oxygen);materialRecord.GetField("NativeIndex")!.SetValue(record,176u);
        materialRecord.GetField("Id")!.SetValue(record,"Oxygen");
        var catalogue=Array.CreateInstance(materialRecord,1);catalogue.SetValue(record,0);
        var hashMethod=assembly.GetType("MixedAtmospheres.Oni.ColonyOwnerPersistence")!.GetMethod("CatalogueHash",flags)!;
        var hash=(byte[])hashMethod.Invoke(null,new object[]{catalogue})!;
        materialRecord.GetField("NativeIndex")!.SetValue(record,175u);catalogue.SetValue(record,0);
        Check(!hash.SequenceEqual((byte[])hashMethod.Invoke(null,new object[]{catalogue})!),"Catalogue identity omitted native ordinal mapping.");
        materialRecord.GetField("NativeIndex")!.SetValue(record,176u);materialRecord.GetField("ElementHash")!.SetValue(record,(int)SimHashes.CarbonDioxide);catalogue.SetValue(record,0);
        Check(!hash.SequenceEqual((byte[])hashMethod.Invoke(null,new object[]{catalogue})!),"Catalogue identity omitted element hash mapping.");
        Console.WriteLine("PASS pipe identity: negative hashes project from ordinals; saved catalogue hash covers both ordinal and element hash");
        return 0;
    }
}
