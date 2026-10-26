using System.Collections;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using HarmonyLib;

internal static class PipeDiseaseVisibilityTests
{
    internal static void Run(Assembly assembly)
    {
        void Check(bool condition,string why){if(!condition)throw new Exception(why);}
        const BindingFlags flags=BindingFlags.NonPublic|BindingFlags.Static;
        var force=AccessTools.Method(typeof(ConduitFlow.SOAInfo),"ForcePermanentDiseaseContainer",new[]{typeof(int),typeof(bool)});
        var endFrame=AccessTools.Method(typeof(ConduitFlow.SOAInfo),"EndFrame");
        // The game reaches the shared container table from SetContents only,
        // and its pipe step joins every network job before the frame end.
        Check(PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(ConduitFlow.Conduit),"SetContents")).Any(i=>Equals(i.operand,force)),
            "Native SetContents no longer changes the disease container through the deferred method.");
        var step=PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(ConduitFlow),"Sim200ms"));
        var jobs=step.FindIndex(i=>i.operand is MethodInfo m&&m.Name=="Run"&&m.DeclaringType?.Name=="GlobalJobManager");
        var end=step.FindIndex(i=>Equals(i.operand,endFrame));
        Check(jobs>=0&&end>jobs&&step.FindLastIndex(i=>i.operand is MethodInfo m&&m.Name=="Run"&&m.DeclaringType?.Name=="GlobalJobManager")<end,
            "The native pipe step no longer finishes its network jobs before the frame end.");
        Check(!step.Take(end).Any(i=>i.operand is MethodInfo m&&m.DeclaringType==typeof(ConduitFlow)&&(m.Name=="SetContents"||m.Name=="AddElement"||m.Name=="RemoveElement")),
            "A main-thread pipe write precedes the deferred container publication.");
        foreach(var (name,target) in new[]{("ColonyPipeDiseaseVisibilityPatch",force),("ColonyPipeVisibilityFramePatch",endFrame)})
        {
            var patch=assembly.GetType("MixedAtmospheres.Oni."+name)!.GetCustomAttribute<HarmonyPatch>()!.info;
            Check(AccessTools.Method(patch.declaringType,patch.methodName,patch.argumentTypes)==target,name+" binds a different native method.");
        }
        var runtime=assembly.GetType("MixedAtmospheres.Oni.ColonyConduits")!;
        var flowing=runtime.GetField("flowing",flags)!;var pending=(IDictionary)runtime.GetField("Visibility",flags)!.GetValue(null)!;
        bool Defer(ConduitFlow.SOAInfo info,int conduit,bool visible)=>(bool)runtime.GetMethod("DeferVisibility",flags)!.Invoke(null,new object[]{info,conduit,visible})!;
        void Publish(ConduitFlow.SOAInfo info)=>runtime.GetMethod("PublishVisibility",flags)!.Invoke(null,new object[]{info});
        const int count=4096;
        ConduitFlow.SOAInfo Table()
        {
            var info=(ConduitFlow.SOAInfo)RuntimeHelpers.GetUninitializedObject(typeof(ConduitFlow.SOAInfo));
            AccessTools.Field(typeof(ConduitFlow.SOAInfo),"diseaseContentsVisible").SetValue(info,Enumerable.Repeat(false,count).ToList());
            var objects=AccessTools.Field(typeof(ConduitFlow.SOAInfo),"conduitGOs");var list=(IList)Activator.CreateInstance(objects.FieldType)!;
            for(var i=0;i<count;++i)list.Add(null);objects.SetValue(info,list);return info;
        }
        List<bool> Visible(ConduitFlow.SOAInfo info)=>(List<bool>)AccessTools.Field(typeof(ConduitFlow.SOAInfo),"diseaseContentsVisible").GetValue(info)!;
        var gas=Table();var liquid=Table();
        Check(!Defer(gas,1,true)&&pending.Count==0,"A main-thread container change outside a network update was deferred.");
        // The failed run's race: network workers publish germ packets at once.
        Parallel.For(0,8,new ParallelOptions{MaxDegreeOfParallelism=8},worker=>
        {
            flowing.SetValue(null,true);
            try{for(var i=worker;i<count;i+=8){Check(Defer(gas,i,true),"A worker projection changed the native container table.");Check(Defer(gas,i,i%3==0),"A repeated projection was not deferred.");}}
            finally{flowing.SetValue(null,false);}
        });
        flowing.SetValue(null,true);try{Check(Defer(liquid,5,true),"The liquid table's change was not deferred.");}finally{flowing.SetValue(null,false);}
        Check(pending.Count==count+1&&Visible(gas).All(v=>!v),"Concurrent worker projections lost a queued change or wrote the native table.");
        try{runtime.GetMethod("RequireQuiescent",flags)!.Invoke(null,null);throw new Exception("A save accepted a queued container change.");}
        catch(TargetInvocationException error)when(error.InnerException is InvalidOperationException){}
        Publish(gas);
        Check(Enumerable.Range(0,count).All(i=>Visible(gas)[i]==(i%3==0)),"The frame end did not apply each conduit's latest projection.");
        Check(pending.Count==1&&!Visible(liquid)[5],"One flow's frame end published the other flow's change.");
        Publish(liquid);
        Check(pending.Count==0&&Visible(liquid)[5],"The liquid frame end did not apply its change.");
        Console.WriteLine("PASS pipe disease containers: worker projections queue their latest change, each flow's main-thread frame end applies it in conduit order after the network jobs, saves refuse a queued change");
    }
}
