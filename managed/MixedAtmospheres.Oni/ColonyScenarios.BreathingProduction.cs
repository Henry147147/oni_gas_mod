using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using HarmonyLib;
using UnityEngine;
using Co2 = MixedAtmospheres.Oni.ColonyBreathingCo2Accounting;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static void BeginBreathingProduction(OniNativeColony.Status status) => BreathingProductionObservation.Begin(status.Session);
        private static Co2.Result EndBreathingProduction(OniNativeColony.Status status) => BreathingProductionObservation.End(status.Session);
        private static string BreathingProductionDetail(Co2.Result result,double globalNetKg) =>
            $"valid {result.Valid}, error {result.Error}; "+string.Join("; ",result.Rows.Select(row=>
                $"source {row.Source.Id}: held {row.InitialHeldKg:R}->{row.FinalHeldKg:R} + original puffs {row.OriginalPuffKg:R} = produced {row.ProducedKg:R} kg"))+
            $"; global net CO2 {globalNetKg:R} kg (diagnostic, not production attribution)";
    }

    // Source/read-only boundary observation for the proposed fixed breathing check.
    // No consumed mass, accumulatedCO2, emission threshold, provider or clock is set.
    internal static class BreathingProductionObservation
    {
        internal static bool Required => ColonyRuntimeValidation.Settings?.Scenario?.Split(',').Contains("breathing")==true;
        [ThreadStatic] private static OxygenBreather? current;
        private static readonly Dictionary<int,OxygenBreather> originals=new Dictionary<int,OxygenBreather>();
        private static Co2.Window? window;
        private static int thread;
        private static long nextPuff;
        internal readonly struct PuffCapture
        {
            internal readonly Co2.Window? Window;
            internal readonly OxygenBreather? Breather;
            internal readonly Co2.Source? Source;
            internal readonly long Event;
            internal PuffCapture(Co2.Window window,OxygenBreather breather,Co2.Source source,long eventId)
            { Window=window;Breather=breather;Source=source;Event=eventId; }
        }
        private static Co2.Sample[] Snapshot(bool begin)
        {
            var samples=new List<Co2.Sample>();
            foreach(var identity in Components.LiveMinionIdentities.Items)
            {
                var breather=identity.GetComponent<OxygenBreather>();
                if(breather==null)throw new InvalidOperationException("A source duplicant has no live OxygenBreather.");
                var prefab=breather.GetComponent<KPrefabID>();
                if(prefab==null)throw new InvalidOperationException("A source breather has no prefab identity.");
                if(begin)
                {
                    if(originals.ContainsKey(prefab.InstanceID))throw new InvalidOperationException("Duplicate source duplicant.");
                    originals.Add(prefab.InstanceID,breather);
                }
                else if(!originals.TryGetValue(prefab.InstanceID,out var source)||!ReferenceEquals(source,breather))
                    throw new InvalidOperationException("A source duplicant or breather component lifetime changed.");
                samples.Add(new Co2.Sample(new Co2.Source(prefab.InstanceID,breather.GetInstanceID()),(double)breather.accumulatedCO2));
            }
            return samples.ToArray();
        }
        internal static void Begin(ulong session)
        {
            if(!Required)throw new InvalidOperationException("The breathing production observer is not selected.");
            if(current!=null)throw new InvalidOperationException("Source boundary is inside Sim200ms.");
            originals.Clear();thread=Thread.CurrentThread.ManagedThreadId;nextPuff=0;
            window=new Co2.Window(session,Snapshot(true));
        }
        internal static Co2.Result End(ulong session)
        {
            var active=window ?? throw new InvalidOperationException("The breathing source window was not captured.");
            try
            {
                if(current!=null||Thread.CurrentThread.ManagedThreadId!=thread)active.Invalidate("Final boundary is inside or off the source simulation thread.");
                Co2.Sample[] final;
                try { final=Snapshot(false); }
                catch(Exception caught) { active.Invalidate(caught.Message);final=Array.Empty<Co2.Sample>(); }
                return active.Complete(session,final);
            }
            finally { originals.Clear(); }
        }
        internal static OxygenBreather? Enter(OxygenBreather source)
        {
            var previous=current;current=source;
            if(window?.Active==true && Thread.CurrentThread.ManagedThreadId!=thread)window.Invalidate("A Sim200ms scope ran off the captured source thread.");
            return previous;
        }
        internal static void Leave(OxygenBreather? previous,Exception? exception)
        {
            if(exception!=null && window?.Active==true)window.Invalidate("Original OxygenBreather.Sim200ms threw.");
            current=previous;
        }
        internal static PuffCapture CaptureOriginalSpawn()
        {
            var active=window;
            if(active?.Active!=true || current==null)return default; // unrelated, unscoped spawn is never credited
            try
            {
                var prefab=current.GetComponent<KPrefabID>();
                if(prefab==null){active.Invalidate("Scoped breather has no identity.");return default;}
                if(!originals.TryGetValue(prefab.InstanceID,out var source))return default; // not an original source
                if(!ReferenceEquals(source,current)){active.Invalidate("Scoped breather lifetime changed.");return default;}
                if(Thread.CurrentThread.ManagedThreadId!=thread){active.Invalidate("Puff ran off the captured source thread.");return default;}
                // One immutable token per original call, rather than one per
                // postfix observation: a duplicate observation cannot gain credit.
                return new PuffCapture(active,current,new Co2.Source(prefab.InstanceID,current.GetInstanceID()),checked(++nextPuff));
            }
            catch(Exception caught){active.Invalidate(caught.Message);return default;}
        }
        internal static void OriginalSpawn(PuffCapture capture,float mass,bool originalRan)
        {
            var active=capture.Window;
            if(active==null)return;
            try
            {
                if(!ReferenceEquals(active,window)||!active.Active)return;
                if(!ReferenceEquals(current,capture.Breather)||Thread.CurrentThread.ManagedThreadId!=thread)
                {active.Invalidate("Original puff scope changed before completion.");return;}
                active.OriginalPuff(capture.Event,ColonyOwner.Status.Session,capture.Source,true,originalRan,(double)mass);
            }
            catch(Exception caught){active.Invalidate(caught.Message);}
        }
    }
    [HarmonyPatch(typeof(OxygenBreather),nameof(OxygenBreather.Sim200ms))]
    internal static class BreathingProductionScopePatch
    {
        private static bool Prepare()=>BreathingProductionObservation.Required;
        private static void Prefix(OxygenBreather __instance,out OxygenBreather? __state)=>__state=BreathingProductionObservation.Enter(__instance);
        private static Exception? Finalizer(Exception? __exception,OxygenBreather? __state)
        { BreathingProductionObservation.Leave(__state,__exception);return ColonyFault.Pass(__exception); }
    }
    [HarmonyPatch(typeof(CO2Manager),nameof(CO2Manager.SpawnBreath))]
    internal static class BreathingProductionSpawnPatch
    {
        private static bool Prepare()=>BreathingProductionObservation.Required;
        private static void Prefix(out BreathingProductionObservation.PuffCapture __state)=>__state=BreathingProductionObservation.CaptureOriginalSpawn();
        private static void Postfix(float __1,bool __runOriginal,BreathingProductionObservation.PuffCapture __state)=>BreathingProductionObservation.OriginalSpawn(__state,__1,__runOriginal);
    }
}
