using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Native helpers replay work without editing either endpoint. Actual carrier
    // bits must agree before any represented body heat can enter the inventory.
    // A contact that cannot be replayed exactly (shared or missing body
    // registration, changed inventory, carrier mismatch) is excluded and
    // counted; it keeps the carrier-inferred exchange instead of faulting.
    internal static unsafe class ColonyPipeContactHeat
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct Input
        {
            public uint Size,Version,Flags,Reserved;
            public float Seconds,FluidTemperature,FluidCapacity,FluidConductivity,BodyTemperature,BodyCapacity,ConduitConductivity;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Output
        {public uint Size,Version;public float NativeHeatToBody,CarrierTemperature,BodyTemperature;}
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_conduit_contact_version",CallingConvention=CallingConvention.Cdecl)]
        private static extern uint Version();
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_conduit_contact_replay",CallingConvention=CallingConvention.Cdecl)]
        private static extern int Replay(Input* inputs,uint count,Output* outputs,uint capacity);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_conduit_heat_version",CallingConvention=CallingConvention.Cdecl)]
        private static extern uint CompletionVersion();
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_conduit_heat_expect",CallingConvention=CallingConvention.Cdecl)]
        private static extern int ExpectCompletion(int handle,float energy,float minimum,float maximum);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_conduit_heat_pending",CallingConvention=CallingConvention.Cdecl)]
        private static extern int PendingCompletions(out uint count);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_conduit_heat_reset",CallingConvention=CallingConvention.Cdecl)]
        private static extern int ResetCompletions();
        private sealed class Row
        {
            internal readonly ConduitFlow Flow;internal readonly int Cell;internal readonly PipeInventory Inventory;
            internal readonly ConduitFlow.ConduitContents Before,Shadow;internal readonly Input Input;
            internal Output Result;internal bool Consumed;internal int Handle;
            internal Row(ConduitFlow flow,int cell,PipeInventory inventory,ConduitFlow.ConduitContents before,ConduitFlow.ConduitContents shadow,Input input)
            {Flow=flow;Cell=cell;Inventory=inventory;Before=before;Shadow=shadow;Input=input;}
        }
        internal sealed class Scope
        {
            private readonly long epoch;
            private readonly List<Row> rows=new List<Row>();
            private readonly Dictionary<(ConduitFlow,int),Row> byCell=new Dictionary<(ConduitFlow,int),Row>();
            private readonly HashSet<Row> invalid=new HashSet<Row>();
            private bool validated;
            internal Scope(float seconds)
            {
                epoch=Epoch;var bodies=new Dictionary<int,Row?>();
                foreach(var sample in ColonyConduits.ThermalProbeSamples())
                {
                    if(!(sample.Native.mass>0))continue; // Exact unprojectable traces remain held.
                    var conduit=sample.Flow.GetConduit(sample.Cell);
                    var go=sample.Flow.soaInfo.GetConduitGO(conduit.idx);
                    var body=go==null?null:go.GetComponent<PrimaryElement>();var building=go==null?null:go.GetComponent<Building>();
                    if(body==null||building==null){++Excluded;continue;}
                    var payload=GameComps.StructureTemperatures.GetPayload(GameComps.StructureTemperatures.GetHandle(go));
                    var handle=payload.simHandleCopy;
                    if(!payload.enabled||payload.bypass||!Sim.IsValidHandle(handle)||Game.Instance.simData.buildingTemperatures==null){++Excluded;continue;}
                    var record=Game.Instance.simData.buildingTemperatures[Sim.GetHandleIndex(handle)];
                    if(record.handle!=handle){++Excluded;continue;}
                    // Several contacts on one body would need the native's
                    // sequential body update; exclude all of them.
                    if(bodies.TryGetValue(handle,out var sharing))
                    {
                        if(sharing!=null){rows.Remove(sharing);byCell.Remove((sharing.Flow,sharing.Cell));bodies[handle]=null;++Excluded;}
                        ++Excluded;continue;
                    }
                    var carrier=ElementLoader.FindElementByHash(sample.Native.element);var definition=building.Def;
                    var input=new Input{Size=44,Version=1,Flags=definition.ThermalConductivity<1?1u:0,
                        Seconds=seconds,FluidTemperature=sample.Native.temperature,FluidCapacity=sample.Native.mass*carrier.specificHeatCapacity,
                        FluidConductivity=carrier.thermalConductivity,BodyTemperature=record.temperature,
                        BodyCapacity=definition.MassForTemperatureModification*body.Element.specificHeatCapacity,
                        ConduitConductivity=body.Element.thermalConductivity*definition.ThermalConductivity};
                    var row=new Row(sample.Flow,sample.Cell,sample.Inventory,sample.Native,sample.Shadow,input){Handle=handle};rows.Add(row);byCell.Add((sample.Flow,sample.Cell),row);bodies.Add(handle,row);
                }
                // Replay is pure; batching changes no native contact ordering or
                // state. Shared bodies were excluded before making any call. A
                // refused batch is replayed row by row to exclude only the
                // inputs outside the native helper's range.
                var admitted=new List<Row>();
                foreach(var row in rows)
                {
                    // The pinned Update on both platforms skips contacts with
                    // either capacity <=1e-4 kJ/K; its pure helpers do not.
                    if(row.Input.FluidCapacity<=1e-4f||row.Input.BodyCapacity<=1e-4f)
                        row.Result=new Output{Size=20,Version=1,CarrierTemperature=row.Input.FluidTemperature,BodyTemperature=row.Input.BodyTemperature};
                    else admitted.Add(row);
                }
                for(var first=0;first<admitted.Count;first+=4096)
                {
                    var count=Math.Min(4096,admitted.Count-first);var inputs=new Input[count];var outputs=new Output[count];
                    for(var i=0;i<count;++i)inputs[i]=admitted[first+i].Input;
                    int result;
                    fixed(Input* source=inputs)fixed(Output* destination=outputs)result=Replay(source,(uint)count,destination,(uint)count);
                    for(var i=0;i<count;++i)
                    {
                        var row=admitted[first+i];var output=outputs[i];
                        if(result!=0)
                        {
                            var single=row.Input;Output one=default;int status;
                            status=Replay(&single,1,&one,1);
                            if(status!=0){invalid.Add(row);++Excluded;continue;}
                            output=one;
                        }
                        if(output.Size!=20||output.Version!=1){invalid.Add(row);++Excluded;continue;}
                        row.Result=output;
                    }
                }
            }
            internal void Validate()
            {
                if(epoch!=Epoch)return;
                foreach(var row in rows)
                {
                    if(invalid.Contains(row))continue;
                    if(!row.Flow.HasConduit(row.Cell)||!ColonyConduits.TryRead(row.Flow,row.Cell,out var held)||!ReferenceEquals(held,row.Inventory))
                    {invalid.Add(row);++Excluded;continue;}
                    var actual=ColonyConduits.ThermalProbeNative(row.Flow,row.Cell);
                    if(BitConverter.SingleToInt32Bits(actual.temperature)!=BitConverter.SingleToInt32Bits(row.Result.CarrierTemperature))
                    {
                        if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows))Debug.Log("[Mixed Atmospheres] Windows conduit contact mismatch: "+
                            $"cell {row.Cell}, flags {row.Input.Flags}, dt {row.Input.Seconds:R}, fluid T/C/k {row.Input.FluidTemperature:R}/{row.Input.FluidCapacity:R}/{row.Input.FluidConductivity:R}, "+
                            $"body T/C/k {row.Input.BodyTemperature:R}/{row.Input.BodyCapacity:R}/{row.Input.ConduitConductivity:R}; "+
                            $"expected {row.Result.CarrierTemperature:R} ({BitConverter.SingleToInt32Bits(row.Result.CarrierTemperature):X8}), "+
                            $"actual {actual.temperature:R} ({BitConverter.SingleToInt32Bits(actual.temperature):X8}), heat {row.Result.NativeHeatToBody:R}");
                        invalid.Add(row);Mismatch();continue;
                    }
                    ValidationObserved?.Invoke(row.Flow,row.Cell,row.Input);
                }
                validated=true;
            }
            internal bool Take(ConduitFlow flow,int cell,ConduitFlow.ConduitContents after,ConduitFlow.ConduitContents shadow,out PipeAmount heat)
            {
                heat=PipeAmount.Zero;if(epoch!=Epoch||!byCell.TryGetValue((flow,cell),out var row)||invalid.Contains(row))return false;
                if(!validated||row.Consumed)return false;
                if(BitConverter.SingleToInt32Bits(after.temperature)!=BitConverter.SingleToInt32Bits(row.Result.CarrierTemperature)||
                    BitConverter.SingleToInt32Bits(shadow.temperature)!=BitConverter.SingleToInt32Bits(row.Shadow.temperature))
                {invalid.Add(row);Mismatch();return false;}
                var carrier=ElementLoader.FindElementByHash(row.Shadow.element);
                var lagged=new PipeAmount((double)row.Shadow.mass*carrier.specificHeatCapacity*1000*((double)row.Before.temperature-row.Shadow.temperature));
                var contact=PipeContactWork.IntoFluid(row.Input.BodyCapacity,row.Input.BodyTemperature,row.Result.BodyTemperature);
                // The native sender forms its queued energy from the rounded
                // carrier endpoints. Reproduce those two binary32 operations
                // for message identity; the represented-work credit below
                // retains its existing body-endpoint arithmetic.
                var carrierDelta=row.Input.FluidTemperature-row.Result.CarrierTemperature;
                var queuedEnergy=carrierDelta*row.Input.FluidCapacity;
                if(queuedEnergy!=0)
                    OniNativeColony.Check(ExpectCompletion(row.Handle,queuedEnergy,
                        Math.Min(row.Input.BodyTemperature,row.Input.FluidTemperature),Math.Max(row.Input.BodyTemperature,row.Input.FluidTemperature)));
                heat=lagged.Plus(contact);row.Consumed=true;ContactHeat=ContactHeat.Plus(contact);LaggedHeat=LaggedHeat.Plus(lagged);
                // Restore/topology/save callbacks can validate at zero time.
                // Retained work still settles, while the diagnostic counts
                // timed contacts independently of those lifecycle callbacks.
                if(row.Input.Seconds>0)++Contacts;
                return true;
            }
            internal void Complete()
            {
                if(epoch!=Epoch)return;
                foreach(var row in rows)if(!row.Consumed&&!invalid.Contains(row))++Unsettled;
            }
        }
        private static Scope? active;
        private static long Epoch;
        internal static bool Enabled { get; private set; }
        internal static long Contacts { get; private set; }
        internal static PipeAmount ContactHeat { get; private set; }
        internal static PipeAmount LaggedHeat { get; private set; }
        internal static long Excluded { get; private set; }
        internal static long Mismatches { get; private set; }
        internal static Action<ConduitFlow,int,Input>? ValidationObserved;
        private static string failure="";
        private static void Mismatch()
        {
            ++Mismatches;
            if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                failure="Windows conduit heat disagrees with its verified contact law. This colony cannot be saved; update Mixed Atmospheres and load your last save.";
                if(SpeedControlScreen.Instance!=null)SpeedControlScreen.Instance.Pause(false);
                ColonyNotices.Show("Mixed Atmospheres stopped",failure);
                throw new InvalidOperationException(failure);
            }
        }
        internal static long Unsettled { get; private set; }
        internal static string Report=>$"{Contacts} validated timed body contacts; {ContactHeat.Value:R} J contact and {LaggedHeat.Value:R} J retained earlier heat; {Excluded} excluded, {Mismatches} carrier mismatches, {Unsettled} unsettled";
        internal static void StartControl()
        {
            if(!ColonyConduits.Active)throw new InvalidOperationException("Contact heat requires owned pipe contents.");
            if(Marshal.SizeOf<Input>()!=44||Marshal.SizeOf<Output>()!=20||Version()!=1)throw new NotSupportedException("Native contact replay version/layout is unsupported.");
            try{if(CompletionVersion()!=1)throw new NotSupportedException("Native conduit heat completion observation is unsupported.");}
            catch(EntryPointNotFoundException error){throw new NotSupportedException("Native conduit heat completion observation needs a matching bundle.",error);}
            Enabled=true;
        }
        internal static void Reset(){if(Enabled)OniNativeColony.Check(ResetCompletions());Enabled=false;active=null;failure="";ValidationObserved=null;++Epoch;Contacts=Excluded=Mismatches=Unsettled=0;ContactHeat=LaggedHeat=PipeAmount.Zero;}
        internal static void Capture(PipeRuntimeImage image)
        {image.Contacts=Contacts;image.Excluded=Excluded;image.Mismatches=Mismatches;image.Unsettled=Unsettled;image.ContactHeat=ContactHeat;image.LaggedHeat=LaggedHeat;}
        internal static void Restore(PipeRuntimeImage image)
        {Contacts=image.Contacts;Excluded=image.Excluded;Mismatches=image.Mismatches;Unsettled=image.Unsettled;ContactHeat=image.ContactHeat;LaggedHeat=image.LaggedHeat;}
        internal static bool Open=>active!=null;
        // Equality with a former temperature is not a pending-work test: an
        // intervening native contact can cool the body and its completed pipe
        // energy can warm it back to precisely that same binary32 value.
        // Confirm the original energy message was processed instead.
        internal static string? BodyHeatMismatch()
        {
            if(failure.Length>0)return failure;
            if(!Enabled)return null;
            OniNativeColony.Check(PendingCompletions(out var pending));
            return pending==0?null:$"{pending} native conduit body-energy messages have not completed";
        }
        internal static Scope? Begin(float seconds)
        {
            if(failure.Length>0)throw new InvalidOperationException(failure);
            if(!Enabled)return null;if(active!=null)throw new InvalidOperationException("Nested native conduit contacts are unsupported.");
            var scope=new Scope(seconds);active=scope;return scope;
        }
        internal static bool TryTake(ConduitFlow flow,int cell,ConduitFlow.ConduitContents after,ConduitFlow.ConduitContents shadow,out PipeAmount heat)
        {heat=PipeAmount.Zero;return active!=null&&active.Take(flow,cell,after,shadow,out heat);}
        internal static void End(Scope? scope){if(ReferenceEquals(active,scope))active=null;}
    }
}
