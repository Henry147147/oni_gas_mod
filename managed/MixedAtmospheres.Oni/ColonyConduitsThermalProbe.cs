using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Diagnostic only. Raw structure snapshot deltas are deliberately NOT
    // assigned to a pipe: a body can have other contacts or delayed work.
    internal static unsafe class ColonyPipeThermalProbe
    {
        private static readonly string? PathSetting=Environment.GetEnvironmentVariable("MA_COLONY_PIPE_THERMAL_LOG");
        private static StreamWriter? writer;
        private static StreamWriter? workWriter;
        private static ulong ordinal;
        [ThreadStatic] private static Scope? activeScope;
        private static HashSet<int>? knownBodies;
        private static readonly object WorkLock=new object();
        internal sealed class Scope
        {
            internal readonly ulong Frame,Ordinal;internal readonly float Dt;
            internal readonly List<Row> Rows=new List<Row>();
            internal Scope(float dt){Frame=ColonyOwner.Status.CompletedFrames;Ordinal=++ordinal;Dt=dt;}
        }
        internal sealed class Row
        {
            internal ConduitFlow Flow=null!;internal int Cell;internal PrimaryElement Body=null!;
            internal PipeInventory Inventory=PipeInventory.Empty;
            internal ConduitFlow.ConduitContents Before,Shadow,After;
            internal BodyReading BodyBefore,BodyAfter;
            internal double RegisteredMass,BodyCv,BodyCvUnrounded,CarrierCv;
        }
        internal readonly struct BodyReading
        {
            internal readonly int Handle,RecordHandle;
            internal readonly double NativeTemperature,CallbackTemperature,InternalTemperature,PendingKilojoules;
            internal readonly bool Enabled,Bypass;
            internal BodyReading(PrimaryElement body)
            {
                var payload=GameComps.StructureTemperatures.GetPayload(GameComps.StructureTemperatures.GetHandle(body.gameObject));
                Handle=payload.simHandleCopy;RecordHandle=-1;NativeTemperature=double.NaN;
                if(Sim.IsValidHandle(Handle)&&Game.Instance.simData.buildingTemperatures!=null)
                {
                    var source=Game.Instance.simData.buildingTemperatures[Sim.GetHandleIndex(Handle)];RecordHandle=source.handle;NativeTemperature=source.temperature;
                }
                CallbackTemperature=body.Temperature;InternalTemperature=body.InternalTemperature;
                PendingKilojoules=payload.pendingEnergyModifications;Enabled=payload.enabled;Bypass=payload.bypass;
            }
        }
        internal static Scope? Begin(float dt)
        {
            if(string.IsNullOrEmpty(PathSetting)||!ColonyConduits.Active)return null;
            var scope=new Scope(dt);
            foreach(var sample in ColonyConduits.ThermalProbeSamples())
            {
                var layer=sample.Flow.conduitType==ConduitType.Gas?ObjectLayer.GasConduit:ObjectLayer.LiquidConduit;
                var go=Grid.Objects[sample.Cell,(int)layer];if(go==null)continue;
                var body=go.GetComponent<PrimaryElement>();var building=go.GetComponent<Building>();if(body==null||building==null)continue;
                var mass=building.Def.MassForTemperatureModification;var cp=body.Element.specificHeatCapacity;
                var carrier=ElementLoader.FindElementByHash(sample.Shadow.element);
                scope.Rows.Add(new Row{Flow=sample.Flow,Cell=sample.Cell,Body=body,Inventory=sample.Inventory,Before=sample.Native,Shadow=sample.Shadow,
                    BodyBefore=new BodyReading(body),RegisteredMass=mass,BodyCv=(double)(mass*cp)*1000,BodyCvUnrounded=(double)mass*cp*1000,
                    CarrierCv=(double)sample.Shadow.mass*carrier.specificHeatCapacity*1000});
            }
            if(scope.Rows.Count==0)return null;
            lock(WorkLock)
            {
                knownBodies??=new HashSet<int>();foreach(var row in scope.Rows)knownBodies.Add(row.BodyBefore.Handle);
                EnsureWorkLog();
            }
            activeScope=scope;return scope;
        }
        internal static void RecordWork(string channel,int bodyHandle,float kilojoules,string source)
        {
            if(string.IsNullOrEmpty(PathSetting))return;
            lock(WorkLock)
            {
                if(knownBodies==null||!knownBodies.Contains(bodyHandle))return;
                EnsureWorkLog();
                var scope=activeScope;
                workWriter!.WriteLine(string.Join("\t",scope?.Frame??ColonyOwner.Status.CompletedFrames,scope?.Ordinal??0,
                    System.Threading.Thread.CurrentThread.ManagedThreadId,channel,bodyHandle,((double)kilojoules).ToString("R",CultureInfo.InvariantCulture),
                    ((double)kilojoules*1000).ToString("R",CultureInfo.InvariantCulture),source.Replace('\t',' ').Replace('\n',' '),scope!=null));
                workWriter.Flush();
            }
        }
        private static void EnsureWorkLog()
        {
            if(workWriter!=null)return;
            using(var meta=new StreamWriter(new FileStream(PathSetting+".meta",FileMode.CreateNew,FileAccess.Write,FileShare.Read)))
            {
                var requested=Harmony.GetPatchInfo(AccessTools.Method(typeof(SimMessages),nameof(SimMessages.ModifyBuildingEnergy)));
                var produced=Harmony.GetPatchInfo(AccessTools.Method(typeof(StructureTemperatureComponents),nameof(StructureTemperatureComponents.ProduceEnergy)));
                meta.WriteLine("SimMessages.ModifyBuildingEnergy prefix installed: "+(requested?.Prefixes.Any(p=>p.PatchMethod.DeclaringType==typeof(ColonyPipeThermalRequestedWorkPatch))==true));
                meta.WriteLine("StructureTemperature.ProduceEnergy prefix installed: "+(produced?.Prefixes.Any(p=>p.PatchMethod.DeclaringType==typeof(ColonyPipeThermalProducedWorkPatch))==true));
                meta.WriteLine("Registered body work outside thermal context is recorded separately; channels must not be double-counted.");
            }
            workWriter=new StreamWriter(new FileStream(PathSetting+".work",FileMode.CreateNew,FileAccess.Write,FileShare.Read));
            workWriter.WriteLine("owner_frame\tthermal_ordinal\tmanaged_thread\tchannel\tbody_handle\trequested_kilojoules\trequested_joules\tsource\tin_thermal_context");workWriter.Flush();
        }
        internal static void RecordProduced(HandleVector<int>.Handle handle,float kilojoules,string source)
        {
            if(string.IsNullOrEmpty(PathSetting))return;
            var payload=GameComps.StructureTemperatures.GetPayload(handle);RecordWork("StructureTemperature.ProduceEnergy",payload.simHandleCopy,kilojoules,source);
        }
        internal static void AfterNative(Scope? scope)
        {
            if(scope==null)return;
            foreach(var row in scope.Rows)
            {
                row.After=ColonyConduits.ThermalProbeNative(row.Flow,row.Cell);
                row.BodyAfter=row.Body==null?row.BodyBefore:new BodyReading(row.Body);
            }
        }
        internal static void AfterBook(Scope? scope)
        {
            if(scope==null)return;
            if(writer==null)
            {
                writer=new StreamWriter(new FileStream(PathSetting!,FileMode.CreateNew,FileAccess.Write,FileShare.Read));
                writer.WriteLine("owner_frame\tordinal\tdt\tphase\tcell\tcarrier_element\tnative_mass_before\tnative_mass_after\tshadow_temp\tnative_temp_before\tnative_temp_after\tcarrier_cv_j_k\tregistered_body_mass\tbody_cv_native_j_k\tbody_cv_unrounded_j_k\tbody_handle_before\tbody_record_before\tbody_native_temp_before\tbody_callback_temp_before\tbody_internal_temp_before\tbody_pending_kj_before\tbody_handle_after\tbody_record_after\tbody_native_temp_after\tbody_callback_temp_after\tbody_internal_temp_after\tbody_pending_kj_after\tbody_enabled\tbody_bypass\tpipe_mass_before\tpipe_mass_after\tpipe_energy_delta_j\tlagged_carrier_work_j\tcurrent_carrier_work_j\texpected_booked_j\tunattributed_body_snapshot_delta_j");
            }
            foreach(var row in scope.Rows)
            {
                ColonyConduits.TryRead(row.Flow,row.Cell,out var after);
                var a=row.BodyBefore;var b=row.BodyAfter;
                object[] values={scope.Frame,scope.Ordinal,scope.Dt,(int)row.Flow.conduitType,row.Cell,(int)row.Shadow.element,row.Before.mass,row.After.mass,
                    row.Shadow.temperature,row.Before.temperature,row.After.temperature,row.CarrierCv,row.RegisteredMass,row.BodyCv,row.BodyCvUnrounded,
                    a.Handle,a.RecordHandle,a.NativeTemperature,a.CallbackTemperature,a.InternalTemperature,a.PendingKilojoules,
                    b.Handle,b.RecordHandle,b.NativeTemperature,b.CallbackTemperature,b.InternalTemperature,b.PendingKilojoules,b.Enabled,b.Bypass,
                    row.Inventory.Mass,after.Mass,after.Energy.Minus(row.Inventory.Energy).Value,
                    row.CarrierCv*((double)row.Before.temperature-row.Shadow.temperature),row.CarrierCv*((double)row.After.temperature-row.Before.temperature),
                    row.CarrierCv*((double)row.After.temperature-row.Shadow.temperature),row.BodyCv*(b.NativeTemperature-a.NativeTemperature)};
                for(var i=0;i<values.Length;++i)
                {
                    if(i!=0)writer.Write('\t');
                    writer.Write(values[i] is double d?d.ToString("R",CultureInfo.InvariantCulture):values[i] is float f?f.ToString("R",CultureInfo.InvariantCulture):
                        values[i] is IFormattable number?number.ToString(null,CultureInfo.InvariantCulture):values[i].ToString());
                }
                writer.WriteLine();
            }
            writer.Flush();
        }
        internal static void EndScope(Scope? scope){if(ReferenceEquals(scope,activeScope))activeScope=null;}
        internal static void Reset()
        {writer?.Dispose();writer=null;lock(WorkLock){workWriter?.Dispose();workWriter=null;knownBodies=null;}activeScope=null;ordinal=0;}
    }
    [HarmonyPatch(typeof(SimMessages),nameof(SimMessages.ModifyBuildingEnergy))]
    internal static class ColonyPipeThermalRequestedWorkPatch
    {private static void Prefix(int __0,float __1)=>ColonyPipeThermalProbe.RecordWork("SimMessages.ModifyBuildingEnergy",__0,__1,"");}
    [HarmonyPatch(typeof(StructureTemperatureComponents),nameof(StructureTemperatureComponents.ProduceEnergy))]
    internal static class ColonyPipeThermalProducedWorkPatch
    {private static void Prefix(HandleVector<int>.Handle __0,float __1,string __2)=>ColonyPipeThermalProbe.RecordProduced(__0,__1,__2);}
}
