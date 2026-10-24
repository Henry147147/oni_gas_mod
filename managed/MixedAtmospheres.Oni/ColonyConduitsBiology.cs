using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Explicit acceptance control only. Every identity owns its own clock;
    // the native table remains a compatibility projection, never its inventory.
    internal static class ColonyPipeBiology
    {
        private sealed class Culture
        {
            internal readonly GermPopulationClock Clock;
            internal Culture(GermPopulationClock clock){Clock=clock;}
        }
        private sealed class CellState
        {
            internal readonly object TemperatureHandle,PipeIdentity;
            internal readonly Dictionary<int,Culture> Cultures=new Dictionary<int,Culture>();
            internal CellState(object handle,object pipeIdentity){TemperatureHandle=handle;PipeIdentity=pipeIdentity;}
        }
        private sealed class Update
        {
            internal readonly ConduitFlow Flow;internal readonly int Cell,Row;
            internal readonly ConduitDiseaseManager.Data Native;
            internal readonly CellState Before,After;
            internal readonly PipeInventory Inventory;
            internal readonly PipePart[] Changes;
            internal Update(ConduitFlow flow,int cell,int row,ConduitDiseaseManager.Data native,CellState before,CellState after,PipeInventory inventory,PipePart[] changes)
            {Flow=flow;Cell=cell;Row=row;Native=native;Before=before;After=after;Inventory=inventory;Changes=changes;}
        }
        internal sealed class Scope
        {
            private readonly List<ConduitDiseaseManager.Data> rows;
            private readonly List<Update> updates;
            private bool closed;
            private Scope(List<ConduitDiseaseManager.Data> rows,List<Update> updates){this.rows=rows;this.updates=updates;}
            internal void Finish(bool success)
            {
                if(closed)return;
                var started=TimingEnabled?Stopwatch.GetTimestamp():0;
                try{FinishCore(success);}
                finally{if(TimingEnabled){finishTicks+=Stopwatch.GetTimestamp()-started;++finishCalls;}}
            }
            private void FinishCore(bool success)
            {
                closed=true;
                foreach(var update in updates)rows[update.Row]=update.Native;
                if(!success)return;
                var live=new HashSet<(ConduitFlow,int)>();foreach(var update in updates)live.Add((update.Flow,update.Cell));
                var retired=new List<(ConduitFlow,int)>();foreach(var key in States.Keys)if(!live.Contains(key))retired.Add(key);
                foreach(var key in retired)States.Remove(key);
                foreach(var update in updates)
                {
                    States[(update.Flow,update.Cell)]=update.After;
                    ColonyConduits.CommitBiology(update.Flow,update.Cell,update.Inventory,update.Changes);
                    // SetContents refreshes the native ID/count/growth view.
                    // Publish the selected identity's own residue afterwards.
                    var data=rows[update.Row];
                    data.accumulatedError=update.After.Cultures.TryGetValue(data.diseaseIdx,out var selected)?selected.Clock.NativeResidue:0;
                    rows[update.Row]=data;
                    ColonyPipeBiologyOracle.Compare(update.Flow,update.Cell,update.Inventory,data);
                    foreach(var change in update.Changes)
                    {
                        var journal=change.Amount.Sign>0?Births:Deaths;
                        journal.TryGetValue(change.Id,out var previous);journal[change.Id]=previous.Plus(change.Amount.Sign>0?change.Amount:change.Amount.Negated());
                    }
                    ++UpdatedCells;UpdatedIdentities+=update.After.Cultures.Count;
                    foreach(var culture in update.After.Cultures.Values)if(culture.Clock.Expected)++ExpectedIdentities;
                }
                ++Ticks;
            }
            internal static Scope Capture(ConduitDiseaseManager manager,float seconds)
            {
                var rows=(List<ConduitDiseaseManager.Data>)Rows.GetValue(manager);
                var byHandle=new Dictionary<object,int>();
                for(var row=0;row<rows.Count;++row)
                {object key=rows[row].temperatureHandle;if(byHandle.ContainsKey(key))byHandle[key]=-1;else byHandle.Add(key,row);}
                var updates=new List<Update>();
                foreach(var sample in ColonyConduits.BiologicalCells())
                {
                    var data=sample.Flow.soaInfo.GetDiseaseData(sample.Flow.GetConduit(sample.Cell).idx);
                    // A cell the exact rule cannot take this tick keeps the
                    // native projected update and its reconciliation instead.
                    if(!byHandle.TryGetValue(data.temperatureHandle,out var row)||row<0){++Skipped;continue;}
                    var pipe=sample.Flow.soaInfo.GetConduitGO(sample.Flow.GetConduit(sample.Cell).idx);
                    if(!States.TryGetValue((sample.Flow,sample.Cell),out var state)||!state.TemperatureHandle.Equals(data.temperatureHandle)||!ReferenceEquals(state.PipeIdentity,pipe))state=new CellState(data.temperatureHandle,pipe);
                    var after=new CellState(data.temperatureHandle,pipe);var inventory=sample.Inventory;var changes=new List<PipePart>();var supported=true;
                    ColonyPipeBiologyOracle.Advance(manager,sample.Flow,sample.Cell,data,seconds);
                    var temperature=(float)GetTemperature.Invoke(Temperature.GetValue(manager),new object[]{data.temperatureHandle});
                    foreach(var germ in sample.Inventory.Germs)
                    {
                        if(germ.Id<0||germ.Id>=byte.MaxValue||germ.Id>=Db.Get().Diseases.Count){supported=false;break;}
                        var definition=Db.Get().Diseases[(byte)germ.Id];
                        var clock=state.Cultures.TryGetValue(germ.Id,out var existing)?existing.Clock:
                            new GermPopulationClock(false,data.diseaseIdx==germ.Id?data.accumulatedError:0);
                        // One-material, one-ID integer cultures retain the
                        // actual native cached descriptor, including its native
                        // refresh semantics when material changes under an ID.
                        var nativeSingle=sample.Inventory.Count==1&&sample.Inventory.Germs.Length==1&&data.diseaseIdx==germ.Id;
                        Klei.AI.DiseaseGrowthRules.ElemGrowthInfo Growth(int ordinal)=>nativeSingle?data.growthInfo:definition.elemGrowthInfo[ordinal];
                        float Material(int count)
                        {
                            if(nativeSingle)return data.growthInfo.CalculateDiseaseCountDelta(count,data.mass,seconds);
                            var flux=0.0;
                            foreach(var part in sample.Inventory.Parts)
                                flux+=(part.Mass/sample.Inventory.Mass)*Growth(part.Id).CalculateDiseaseCountDelta(count,data.mass,seconds);
                            return (float)flux;
                        }
                        PipeAmount Expected(PipeAmount count)
                        {
                            var flux=PipeAmount.Zero;
                            foreach(var part in sample.Inventory.Parts)
                                flux=flux.Plus(ColonyNativeGermEffects.ExpectedMaterialChange(count,Growth(part.Id),data.mass,seconds).Times(part.Mass/sample.Inventory.Mass));
                            return flux;
                        }
                        var factor=Klei.AI.Disease.HalfLifeToGrowthRate(definition.CalculateTemperatureHalfLife(temperature),seconds);
                        if(!ColonyGermPopulation.TryStep(germ.Amount,clock,Material,Expected,factor,out var step)){supported=false;break;}
                        if(step.Population.Sign>0)after.Cultures.Add(germ.Id,new Culture(step.Clock));
                        if(step.Change.Sign!=0){inventory=inventory.WithDiseaseDelta(germ.Id,step.Change);changes.Add(new PipePart(germ.Id,step.Change));}
                    }
                    if(!supported){++Skipped;continue;}
                    updates.Add(new Update(sample.Flow,sample.Cell,row,data,state,after,inventory,changes.ToArray()));
                }
                // Only mask rows after all changes have been prepared without
                // mutating any physical population or saved culture clock.
                foreach(var update in updates){var data=rows[update.Row];data.diseaseIdx=byte.MaxValue;rows[update.Row]=data;}
                return new Scope(rows,updates);
            }
        }
        private static readonly FieldInfo Rows=AccessTools.Field(typeof(ConduitDiseaseManager),"data");
        private static readonly FieldInfo Temperature=AccessTools.Field(typeof(ConduitDiseaseManager),"temperatureManager");
        private static readonly MethodInfo GetTemperature=AccessTools.Method(AccessTools.TypeByName("ConduitTemperatureManager"),"GetTemperature");
        private static readonly Dictionary<(ConduitFlow,int),CellState> States=new Dictionary<(ConduitFlow,int),CellState>();
        private static readonly SortedDictionary<int,PipeAmount> Births=new SortedDictionary<int,PipeAmount>(),Deaths=new SortedDictionary<int,PipeAmount>();
        private static readonly bool TimingEnabled=Environment.GetEnvironmentVariable("MA_COLONY_PIPE_BIOLOGY_TIMING")=="1";
        private static long captureTicks,finishTicks,captureCalls,finishCalls;
        internal static bool Enabled { get; private set; }
        internal static long Ticks { get; private set; }
        internal static long UpdatedCells { get; private set; }
        internal static long UpdatedIdentities { get; private set; }
        internal static long ExpectedIdentities { get; private set; }
        internal static long Skipped { get; private set; }
        internal static PipeAmount Born(int id)=>Births.TryGetValue(id,out var count)?count:PipeAmount.Zero;
        internal static PipeAmount Died(int id)=>Deaths.TryGetValue(id,out var count)?count:PipeAmount.Zero;
        internal static string Report=>$"{Ticks} native cadence ticks, {UpdatedCells} cells/{UpdatedIdentities} identities ({ExpectedIdentities} expected-population), {Skipped} skipped; {Births.Count}/{Deaths.Count} birth/death IDs"+
            (TimingEnabled?$"; capture {captureCalls} calls/{Milliseconds(captureTicks,captureCalls):F6} ms mean; commit {finishCalls} calls/{Milliseconds(finishTicks,finishCalls):F6} ms mean (includes fixture oracle)":"");
        private static double Milliseconds(long ticks,long calls)=>calls==0?0:1000.0*ticks/Stopwatch.Frequency/calls;
        internal static void StartControl()
        {
            if(!ColonyConduits.Active)throw new InvalidOperationException("All-ID biology needs owned pipe inventories.");
            if(SchedulingSignature()!=ExpectedSchedulingSignature)throw new NotSupportedException("All-ID pipe biology requires the validated synchronous native conduit scheduling boundary.");
            Enabled=true;
        }
        internal const string ExpectedSchedulingSignature="1CD6F26E9E76A9FB3F5CD1EF5BA0A5AA3D007EEDDF615AE593EC1A321BCD1B49";
        internal static string SchedulingSignature()
        {
            using var bytes=new MemoryStream();using(var writer=new BinaryWriter(bytes,System.Text.Encoding.UTF8,true))
                foreach(var pair in new[]{(typeof(Game),"StepTheSim"),(typeof(ConduitFlow),"Sim200ms"),
                    (AccessTools.TypeByName("GlobalJobManager"),"Run"),(AccessTools.TypeByName("JobManager"),"Run"),
                    (typeof(ConduitDiseaseManager),"Sim200ms")})
                {
                    var method=AccessTools.Method(pair.Item1,pair.Item2);var body=method?.GetMethodBody()?.GetILAsByteArray();
                    if(body==null)throw new NotSupportedException("Native pipe scheduling body is unavailable: "+pair.Item2);
                    writer.Write(pair.Item1.FullName+"::"+pair.Item2);writer.Write(body.Length);writer.Write(body);
                }
            using var hash=SHA256.Create();return BitConverter.ToString(hash.ComputeHash(bytes.ToArray())).Replace("-","");
        }
        internal static void Reset(){Enabled=false;States.Clear();Births.Clear();Deaths.Clear();Ticks=UpdatedCells=UpdatedIdentities=ExpectedIdentities=Skipped=captureTicks=finishTicks=captureCalls=finishCalls=0;ColonyPipeBiologyOracle.Reset();}
        internal static void Capture(PipeRuntimeImage image)
        {
            foreach(var item in States)
            {
                if(item.Value.Cultures.Count==0)continue;
                image.Cultures.Add(new PipeRuntimeImage.CultureRecord{Phase=item.Key.Item1.conduitType==ConduitType.Gas?(byte)1:(byte)2,Cell=item.Key.Item2,
                    Cultures=item.Value.Cultures.Select(c=>(c.Key,c.Value.Clock)).ToArray()});
            }
            foreach(var item in Births)image.Births.Add(item.Key,item.Value);foreach(var item in Deaths)image.Deaths.Add(item.Key,item.Value);
            image.BiologyTicks=Ticks;image.BiologyCells=UpdatedCells;image.BiologyIdentities=UpdatedIdentities;image.BiologyExpected=ExpectedIdentities;image.BiologySkipped=Skipped;
        }
        // Restored clocks bind to the conduit's current native temperature
        // handle and body, which a load re-creates.
        internal static void Restore(PipeRuntimeImage image)
        {
            foreach(var record in image.Cultures)
            {
                var flow=record.Phase==1?Game.Instance.gasConduitFlow:Game.Instance.liquidConduitFlow;
                if(!flow.HasConduit(record.Cell))throw new InvalidDataException("A saved pipe culture has no restored conduit.");
                var idx=flow.GetConduit(record.Cell).idx;var data=flow.soaInfo.GetDiseaseData(idx);
                var state=new CellState(data.temperatureHandle,flow.soaInfo.GetConduitGO(idx));
                foreach(var culture in record.Cultures)state.Cultures.Add(culture.Id,new Culture(culture.Clock));
                States.Add((flow,record.Cell),state);
            }
            foreach(var item in image.Births)Births.Add(item.Key,item.Value);foreach(var item in image.Deaths)Deaths.Add(item.Key,item.Value);
            Ticks=image.BiologyTicks;UpdatedCells=image.BiologyCells;UpdatedIdentities=image.BiologyIdentities;ExpectedIdentities=image.BiologyExpected;Skipped=image.BiologySkipped;
        }
        internal static Scope? Begin(ConduitDiseaseManager manager,float seconds)
        {
            if(!Enabled||!ReferenceEquals(manager,Game.Instance.conduitDiseaseManager))return null;
            var started=TimingEnabled?Stopwatch.GetTimestamp():0;
            try{return Scope.Capture(manager,seconds);}
            finally{if(TimingEnabled){captureTicks+=Stopwatch.GetTimestamp()-started;++captureCalls;}}
        }
    }
    [HarmonyPatch(typeof(ConduitDiseaseManager),"Sim200ms")]
    internal static class ColonyMixedPipeBiologyPatch
    {
        private static void Prefix(ConduitDiseaseManager __instance,float __0,ref ColonyPipeBiology.Scope? __state)=>__state=ColonyPipeBiology.Begin(__instance,__0);
        private static void Postfix(ColonyPipeBiology.Scope? __state)=>__state?.Finish(true);
        private static Exception? Finalizer(Exception? __exception,ColonyPipeBiology.Scope? __state)
        {__state?.Finish(false);return __exception;}
    }
}
