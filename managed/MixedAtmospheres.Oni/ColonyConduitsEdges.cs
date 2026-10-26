using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Explicit fixture-only observation of the native conduit clock. No state
    // or transfer setting is changed, and ordinary games never enable it.
    internal static class ColonyValveControl
    {
        private sealed class Sample {internal int Calls;internal double Seconds,Requested;internal ulong PreviousFrame,LastFrame;internal bool PreviousClosed,LastClosed;internal readonly List<string> Before=new List<string>();}
        private static Dictionary<ValveBase,Sample>? samples;
        internal static void Begin(IEnumerable<ValveBase> valves)
        {samples=new Dictionary<ValveBase,Sample>();foreach(var valve in valves)samples.Add(valve,new Sample());}
        internal static void Restart(IEnumerable<ValveBase> valves)
        {if(samples==null)samples=new Dictionary<ValveBase,Sample>();foreach(var valve in valves)samples[valve]=new Sample();}
        internal static void Reset()=>samples=null;
        internal static void Observe(object instance,float dt)
        {
            if(samples==null||!(instance is ValveBase valve)||!samples.TryGetValue(valve,out var sample))return;
            ++sample.Calls;sample.Seconds+=dt;sample.Requested+=(float)(valve.CurrentFlow*dt);
            sample.PreviousFrame=sample.LastFrame;sample.LastFrame=ColonyOwner.Status.CompletedFrames;
            sample.PreviousClosed=sample.LastClosed;sample.LastClosed=valve.CurrentFlow==0;
            if(sample.Before.Count<16)
                try{sample.Before.Add($"call {sample.Calls}, dt {dt:R}: "+State(valve));}
                catch(Exception error){sample.Before.Add("observation refused: "+error.GetType().Name+": "+error.Message);}
        }
        internal static bool PositivePhase(ValveBase valve,ulong seedFrame,out string detail)
        {
            detail=$"{valve.conduitType} seed {seedFrame}, fixed rate window {seedFrame+10}..{seedFrame+20}, partial window {seedFrame+24}..{seedFrame+28}";
            if(samples==null||!samples.TryGetValue(valve,out var sample)||sample.Calls<2||!sample.PreviousClosed||!sample.LastClosed)
            {detail+="; waiting for two actual closed callbacks";return false;}
            var fits=ColonyValvePreparation.TryCallbackPhase(seedFrame,sample.PreviousFrame,sample.LastFrame,out var next);
            detail+=$"; closed callbacks {sample.PreviousFrame}/{sample.LastFrame}, rate callbacks {next-15}/{next-10}, next partial callback {next}, fits {fits}";
            return fits;
        }
        internal static bool NegativePartialPhase(ValveBase valve,ulong seedFrame,out string detail)
        {
            detail="waiting for two actual closed liquid callbacks";
            if(samples==null||!samples.TryGetValue(valve,out var sample)||sample.Calls<2||sample.PreviousFrame==0||
                !sample.PreviousClosed||!sample.LastClosed||sample.LastFrame<=sample.PreviousFrame)return false;
            var period=sample.LastFrame-sample.PreviousFrame;var arm=checked(seedFrame+24);var end=checked(seedFrame+28);
            detail=$"closed callbacks {sample.PreviousFrame}/{sample.LastFrame}, cadence {period}, seed {seedFrame}, fixed partial window {arm}..{end}";
            if(period!=5||arm<=sample.LastFrame)return false;
            var phase=(arm-sample.LastFrame)%period;var next=checked(arm+period-phase);
            detail+=$", next liquid callback strictly after arm {next}; no manual callback or clock reset";
            return phase==0&&next>end;
        }
        internal static int Calls(ValveBase valve)=>samples!=null&&samples.TryGetValue(valve,out var sample)?sample.Calls:0;
        private static string Inventory(ConduitFlow flow,int cell)
        {
            if(!ColonyConduits.TryRead(flow,cell,out var inventory))return $"cell {cell} not owned";
            var parts=new List<string>();var germs=new List<string>();
            foreach(var part in inventory.Parts)parts.Add($"{part.Id}:{part.Amount.Value:R}");
            foreach(var germ in inventory.Germs)germs.Add($"{germ.Id}:{germ.Amount.Value:R}");
            var native=ColonyConduits.ThermalProbeNative(flow,cell);var type=typeof(ConduitFlow.ConduitContents);
            var initial=(float)AccessTools.Field(type,"initial_mass").GetValue(native);
            var added=(float)AccessTools.Field(type,"added_mass").GetValue(native);
            var removed=(float)AccessTools.Field(type,"removed_mass").GetValue(native);
            return $"cell {cell}, {inventory.Mass:R}kg, U {inventory.Energy.Value:R}J, parts [{string.Join(",",parts)}], germs [{string.Join(",",germs)}]; native {native.element}/{native.mass:R}kg/{native.temperature:R}K, stages {initial:R}+{added:R}-{removed:R}, movable {initial-removed:R}kg";
        }
        internal static string State(ValveBase valve)
        {
            var flow=Conduit.GetFlowManager(valve.conduitType);
            var input=(int)AccessTools.Field(typeof(ValveBase),"inputCell").GetValue(valve);
            var output=(int)AccessTools.Field(typeof(ValveBase),"outputCell").GetValue(valve);
            return $"owner frame {ColonyOwner.Status.CompletedFrames}, clock {GameClock.Instance.GetTime():R}s, rate/max {valve.CurrentFlow:R}/{valve.MaxFlow:R}; input {Inventory(flow,input)}; output {Inventory(flow,output)}; {ColonyScenarios.MachineState(valve.gameObject)}";
        }
        internal static string Describe(ValveBase valve)=>samples!=null&&samples.TryGetValue(valve,out var sample)?
            $"native calls {sample.Calls}, dt sum {sample.Seconds:R}s, unconstrained request {sample.Requested:R}kg; before callbacks [{string.Join(" | ",sample.Before)}]":"native clock control inactive";
    }
    // Reuse native rate/operation/topology decisions. AddElement reserves the
    // chosen destination; its matching RemoveElement moves the whole mixture
    // atomically. No dominant-element scalar is ever imported as that mixture.
    [HarmonyPatch]
    internal static class ColonyMixedPipeEdgePatch
    {
        private sealed class Fields
        {
            internal readonly FieldInfo Input,Type;
            internal readonly FieldInfo? Port,Secondary;
            internal Fields(System.Type type)
            {
                Input=AccessTools.Field(type,"inputCell");Secondary=AccessTools.Field(type,"secondaryInput");
                Type=AccessTools.Field(type,"conduitType")??AccessTools.Field(type,"type");
                if(Type==null){Port=AccessTools.Field(type,"portInfo");Type=AccessTools.Field(Port.FieldType,"conduitType");}
            }
        }
        private static readonly ConcurrentDictionary<Type,Fields> Cached=new ConcurrentDictionary<Type,Fields>();
        private static IEnumerable<MethodBase> TargetMethods()
        {
            yield return AccessTools.Method(typeof(ConduitBridge),"ConduitUpdate");
            yield return AccessTools.Method(typeof(ValveBase),"ConduitUpdate");
            yield return AccessTools.Method(typeof(ConduitOverflow),"ConduitUpdate");
            yield return AccessTools.Method(typeof(ConduitPreferentialFlow),"ConduitUpdate");
        }
        private static void Prefix(object __instance,float __0,ref ColonyConduits.TransferScope? __state)
        {
            ColonyValveControl.Observe(__instance,__0);
            if(!ColonyConduits.Active)return;
            var fields=Cached.GetOrAdd(__instance.GetType(),type=>new Fields(type));
            var type=(ConduitType)fields.Type.GetValue(fields.Port==null?__instance:fields.Port.GetValue(__instance));
            if(type!=ConduitType.Gas&&type!=ConduitType.Liquid)return;
            var input=(int)fields.Input.GetValue(__instance);var secondary=-1;
            if(fields.Secondary?.GetValue(__instance) is object item)secondary=(int)AccessTools.Property(item.GetType(),"Cell").GetValue(item);
            __state=ColonyConduits.BeginTransfer(Conduit.GetFlowManager(type),input,secondary);
        }
        private static Exception? Finalizer(Exception? __exception,ColonyConduits.TransferScope? __state)
        {ColonyConduits.EndTransfer(__state,__exception);return __exception;}
    }
}
