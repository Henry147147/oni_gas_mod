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
        StoredBiologyHooks.Run(assembly);
        var cultureProbe=assembly.GetType("MixedAtmospheres.Oni.ColonyGermEvolutionProbe")!;
        var cultureAllocate=(MethodInfo)cultureProbe.GetField("AllocateCulture",BindingFlags.Static|BindingFlags.NonPublic)!.GetValue(null)!;
        Check(cultureAllocate.DeclaringType==typeof(ConduitDiseaseManager)&&cultureAllocate.GetParameters().Length==2&&
            cultureAllocate.GetParameters()[1].ParameterType==typeof(ConduitFlow.ConduitContents).MakeByRefType(),
            "Native culture allocation bound an inherited generic overload instead of the actual two-argument allocator.");
        Console.WriteLine("PASS native germ oracle binding: declared allocator uses original temperature handle and by-ref contents, excluding inherited overloads");
        var biology=assembly.GetType("MixedAtmospheres.Oni.ColonyPipeBiology")!;
        var scheduling=(string)biology.GetMethod("SchedulingSignature",BindingFlags.Static|BindingFlags.NonPublic)!.Invoke(null,null)!;
        Check(scheduling==(string)biology.GetField("ExpectedSchedulingSignature",BindingFlags.Static|BindingFlags.NonPublic)!.GetRawConstantValue()!,
            "Pipe worker scheduling changed: the supported game must join prior conduit jobs and run disease updates before the next gas/liquid flows.");
        Console.WriteLine("PASS native biology scheduling: inspected synchronous StepTheSim/flow/job/disease bodies match the supported worker-join boundary");
        var contact=assembly.GetType("MixedAtmospheres.Oni.ColonyPipeContactHeat")!;
        Check(System.Runtime.InteropServices.Marshal.SizeOf(contact.GetNestedType("Input",BindingFlags.NonPublic)!)==44&&
            System.Runtime.InteropServices.Marshal.SizeOf(contact.GetNestedType("Output",BindingFlags.NonPublic)!)==20,
            "Native contact layouts differ from the read-only C ABI.");
        Console.WriteLine("PASS conduit contact ABI: registered input44/output20 byte layouts, no runtime activation");
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
            var biologyStates=(System.Collections.IDictionary)biology.GetField("States",flags)!.GetValue(null)!;
            var biologyCell=Activator.CreateInstance(biology.GetNestedType("CellState",BindingFlags.NonPublic)!,BindingFlags.NonPublic|BindingFlags.Instance,null,new[]{new object(),new object()},null)!;
            biologyStates.Add((flow,0),biologyCell);
            biology.GetMethod("RetireEmptyCultures",flags)!.Invoke(null,null);
            Check(biologyStates.Contains((flow,0)),"A temporarily skipped biology row retired a still-owned culture and its rounding clock.");
            AccessTools.Field(cellType,"Inventory").SetValue(owned,empty);
            biology.GetMethod("RetireEmptyCultures",flags)!.Invoke(null,null);
            Check(biologyStates.Count==0,"An emptied pipe retained a retired culture.");
            AccessTools.Field(cellType,"Inventory").SetValue(owned,inventory);
            Console.WriteLine("PASS biology clock lifetime: a temporarily unavailable row retains owned culture state; an emptied pipe retires it");
            biology.GetProperty("Enabled",flags)!.SetValue(null,true);
            Check(biology.GetMethod("Begin",flags)!.Invoke(null,new object?[]{null,0f})==null,
                "A zero-time save publication entered the owned biological update.");
            biology.GetProperty("Enabled",flags)!.SetValue(null,false);
            var cultureState=assembly.GetType("MixedAtmospheres.Oni.ColonyPipeCultureState")!;
            var nativeCulture=new ConduitDiseaseManager.Data{elemIdx=17,mass=0,diseaseIdx=1,diseaseCount=1000,accumulatedError=.375f,
                growthInfo=new Klei.AI.DiseaseGrowthRules.ElemGrowthInfo{populationHalfLife=-300,maxCountPerKG=float.PositiveInfinity}};
            var savedCulture=cultureState.GetMethod("Capture",flags)!.Invoke(null,new object[]{(byte)1,0,nativeCulture})!;
            var freshCulture=new ConduitDiseaseManager.Data{elemIdx=23,mass=.5f,diseaseIdx=1,diseaseCount=1000,
                growthInfo=new Klei.AI.DiseaseGrowthRules.ElemGrowthInfo{populationHalfLife=600}};
            var rebound=(ConduitDiseaseManager.Data)cultureState.GetMethod("Restore",flags)!.Invoke(null,new[]{savedCulture,(object)freshCulture})!;
            Check(rebound.elemIdx==17&&rebound.mass==0&&rebound.accumulatedError==.375f&&rebound.growthInfo.populationHalfLife==-300&&
                float.IsPositiveInfinity(rebound.growthInfo.maxCountPerKG)&&rebound.temperatureHandle.Equals(freshCulture.temperatureHandle),
                "A cold native culture used the loaded packet's allocation mass/cache or replaced its fresh temperature handle.");
            var recaptured=cultureState.GetMethod("Capture",flags)!.Invoke(null,new object[]{(byte)1,0,rebound})!;
            Check((bool)savedCulture.GetType().GetMethod("SameBits",BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(savedCulture,new[]{recaptured})!,
                "Final native culture verification lost the saved state after rebinding.");
            freshCulture.diseaseCount=999;
            try{cultureState.GetMethod("Restore",flags)!.Invoke(null,new[]{savedCulture,(object)freshCulture});throw new Exception("A changed germ projection accepted a saved native culture.");}
            catch(TargetInvocationException error)when(error.InnerException is InvalidDataException){}
            Console.WriteLine("PASS native biology restore: original zero allocation mass/cache/residue replace cold packet context, fresh handle retained, changed germ projection refuses");
            portsRuntime.GetProperty("Enabled",flags)!.SetValue(null,true);
            var secondMaterial=Activator.CreateInstance(materialType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{ordinal+1,2000.0,0.0},null)!;
            materials.Add(ordinal+1,secondMaterial);
            var left=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{material,.5,300.0,0,50.0})!;
            var right=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{secondMaterial,.5,500.0,1,70.0})!;
            var expectedMix=inventoryType.GetMethod("Joined",BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(left,new[]{right})!;
            byte[] Image(object value){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);inventoryType.GetMethod("Write",BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(value,new object[]{writer});return stream.ToArray();}
            var buffers=(System.Collections.IDictionary)portsRuntime.GetField("Buffers",flags)!.GetValue(null)!;
            buffers.Add(1000UL,Activator.CreateInstance(bufferType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{(byte)1,0,left},null)!);
            buffers.Add(1001UL,Activator.CreateInstance(bufferType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{(byte)1,0,right},null)!);
            var pumpBuffers=(System.Collections.IList)AccessTools.Field(pumpStateType,"Buffers").GetValue(pumpState)!;pumpBuffers.Add(1000UL);pumpBuffers.Add(1001UL);
            pumpRuntime.GetMethod("Consolidate",flags)!.Invoke(null,new[]{pumpState});
            Check(pumpBuffers.Count==1&&buffers.Count==1,"Completed pump intakes retained independently drainable buffers.");
            var mixed=AccessTools.Field(bufferType,"Inventory").GetValue(buffers[pumpBuffers[0]!])!;
            Check(Image(mixed).SequenceEqual(Image(expectedMix)),"Pump storage coalescence changed species, heat or per-identity germs.");
            var lookup=Delegate.CreateDelegate(typeof(Func<,>).MakeGenericType(typeof(int),materialType),runtime.GetMethod("Material",flags)!);
            var moved=inventoryType.GetMethod("Transfer",flags)!.Invoke(null,new object?[]{mixed,empty,.25,1.0,lookup,null,false})!;
            var parcel=moved.GetType().GetField("Item3")!.GetValue(moved)!;
            var expectedA=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{material,.125,300.0,0,12.5})!;
            var expectedB=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{secondMaterial,.125,500.0,1,17.5})!;
            var expectedParcel=inventoryType.GetMethod("Joined",BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(expectedA,new[]{expectedB})!;
            Check(Image(parcel).SequenceEqual(Image(expectedParcel)),"Partial storage discharge sampled an arrival packet instead of the complete mixture.");

            var merge=portsRuntime.GetMethod("MergeBuffers",flags)!;
            try{merge.Invoke(null,new object[]{new[]{(ulong)pumpBuffers[0]!, (ulong)pumpBuffers[0]!}});throw new Exception("Duplicate buffer merged twice.");}
            catch(TargetInvocationException error)when(error.InnerException is InvalidOperationException){}
            Check(Image(AccessTools.Field(bufferType,"Inventory").GetValue(buffers[pumpBuffers[0]!])!).SequenceEqual(Image(expectedMix)),"Rejected buffer merge changed inventory.");
            Console.WriteLine("PASS pump storage mixing: separate compositions become one exact inventory, partial discharge samples both, rejected duplicate preserves state");

            var ventRuntime=assembly.GetType("MixedAtmospheres.Oni.ColonyPipeVents")!;
            var ventStateType=ventRuntime.GetNestedType("State",BindingFlags.NonPublic)!;
            var ventState=RuntimeHelpers.GetUninitializedObject(ventStateType);
            var ventStorage=(Storage)RuntimeHelpers.GetUninitializedObject(typeof(Storage));
            var otherStorage=(Storage)RuntimeHelpers.GetUninitializedObject(typeof(Storage));
            var ventConsumer=(ConduitConsumer)RuntimeHelpers.GetUninitializedObject(typeof(ConduitConsumer));ventConsumer.storage=ventStorage;
            AccessTools.Field(ventStateType,"Storage").SetValue(ventState,ventStorage);
            AccessTools.Field(ventStateType,"Consumer").SetValue(ventState,ventConsumer);
            AccessTools.Field(ventStateType,"Buffers").SetValue(ventState,new List<ulong>{2000,2001});
            AccessTools.Field(ventStateType,"Pending").SetValue(ventState,new HashSet<ulong>{2001});
            buffers.Add(2000UL,Activator.CreateInstance(bufferType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object[]{(byte)1,0,parcel},null)!);
            var reserve=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{material,.25,300.0,0,1e-240})!;
            var traceMass=inventoryType.GetMethod("Single",flags)!.Invoke(null,new object[]{material,1e-240,300.0,0,0.0})!;
            reserve=inventoryType.GetMethod("Joined",BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(reserve,new[]{traceMass})!;
            var jobType=assembly.GetType("MixedAtmospheres.Oni.PipePortJob")!;
            var job=Activator.CreateInstance(jobType,BindingFlags.NonPublic|BindingFlags.Instance,null,new object?[]{2001UL,2001UL,0,(byte)1,0u,(uint)ordinal,1u,0u,.25,300.0,reserve,null},null)!;
            var pendingType=portsRuntime.GetNestedType("Pending",BindingFlags.NonPublic)!;
            ((System.Collections.IDictionary)portsRuntime.GetField("Jobs",flags)!.GetValue(null)!).Add(2001UL,
                Activator.CreateInstance(pendingType,BindingFlags.NonPublic|BindingFlags.Instance,null,new[]{job},null)!);
            ((System.Collections.IDictionary)ventRuntime.GetField("States",flags)!.GetValue(null)!).Add(ventStorage,ventState);
            ventRuntime.GetProperty("Enabled",flags)!.SetValue(null,true);
            var inventoryMethod=ventRuntime.GetMethod("Inventory",flags)!;var heldBefore=Image(inventoryMethod.Invoke(null,new object[]{ventStorage})!);
            for(var repeat=0;repeat<100;++repeat)
            {
                object?[] ventRead={ventStorage,0f};Check((bool)ventRuntime.GetMethod("ReadMass",flags)!.Invoke(null,ventRead)!&&((float)ventRead[1]!) == .5f,"Vent storage omitted native-queued reservations or required scalar packets.");
            }
            object?[] foreignRead={otherStorage,0f};Check(!(bool)ventRuntime.GetMethod("ReadMass",flags)!.Invoke(null,foreignRead)!,"Vent storage ownership aliased another native object.");
            Check(heldBefore.SequenceEqual(Image(inventoryMethod.Invoke(null,new object[]{ventStorage})!))&&Image(reserve).SequenceEqual(Image(AccessTools.Field(jobType,"Reserved").GetValue(job)!)),"Vent capacity/inspection reads changed precise inventory or pending reservations.");
            Console.WriteLine("PASS vent storage readers: completed mixtures and native-queued reservations share capacity, traces survive repeated reads, foreign storage stays native");

        }
        finally{runtime.GetMethod("Reset",flags)!.Invoke(null,null);}
        var saveGuard=assembly.GetType("MixedAtmospheres.Oni.ColonyMixedPipeSaveGuard")!;
        var savePrefix=saveGuard.GetMethod("Prefix",flags)!;
        var saveTargets=((IEnumerable<MethodBase>)saveGuard.GetMethod("TargetMethods",flags)!.Invoke(null,null)!).ToArray();
        Check(saveTargets.Length==6&&saveTargets.All(method=>method.GetMethodBody()!=null),"Mixed save guard omitted a pinned ingress.");
        savePrefix.Invoke(null,null); // Inactive legacy/ordinary owner saves pass through.
        try
        {
            runtime.GetProperty("Active",flags)!.SetValue(null,true);
            try{savePrefix.Invoke(null,null);throw new Exception("Unsupported active mixed saves passed the early guard.");}
            catch(TargetInvocationException error)when(error.InnerException is NotSupportedException refusal&&refusal.Message.Contains("mixed-pipe")){}
        }
        finally{runtime.GetMethod("Reset",flags)!.Invoke(null,null);}
        Console.WriteLine("PASS mixed save ingress declarations: six original bodies pinned, inactive pass-through, active guard refusal; public I/O checked in game");
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
        // Production port/phase/status hooks bind the installed declarations.
        var statusTarget=AccessTools.Method(typeof(Database.BuildingStatusItems).GetNestedType("<>c",BindingFlags.NonPublic|BindingFlags.Public),"<CreateStatusItems>b__337_35");
        var statusBody=statusTarget?.GetMethodBody()?.GetILAsByteArray();
        var getContents=AccessTools.Method(typeof(ConduitFlow),nameof(ConduitFlow.GetContents));
        Check(statusTarget!=null&&statusTarget.ReturnType==typeof(string)&&statusBody!=null&&
            Enumerable.Range(0,statusBody.Length-4).Any(i=>(statusBody[i]==0x6F||statusBody[i]==0x28)&&BitConverter.ToInt32(statusBody,i+1)==getContents.MetadataToken),
            $"Pipe contents status closure no longer reads ConduitFlow.GetContents ({statusTarget?.Name}, {statusBody?.Length}, token {getContents.MetadataToken:X}).");
        foreach(var (type,name) in new[]{(typeof(ConduitConsumer),"Consume"),(typeof(ConduitDispenser),"Dispense"),(typeof(ConduitDispenser),"FindSuitableElement"),
            (typeof(AirConditioner),"UpdateState"),(typeof(EmptyConduitWorkable),"EmptyContents"),(typeof(ConduitFlow),"FreezeConduitContents"),(typeof(ConduitFlow),"MeltConduitContents"),
            (typeof(ContactConductivePipeBridge.Instance),"Flow"),(typeof(ContactConductivePipeBridge.Instance),"ExchangeStorageTemperatureWithBuilding"),
            (typeof(MorbRoverMaker.Instance),"Flow"),(typeof(Storage),"OnQueueDestroyObject")})
            Check(AccessTools.Method(type,name)?.GetMethodBody()!=null,$"Pipe port hook target {type.Name}.{name} is missing.");
        Console.WriteLine("PASS pipe port hooks: consumer/dispenser/conditioner/emptying/phase/panel/morb/storage-retire targets and the pipe status closure bind installed declarations");
        return 0;
    }
}
