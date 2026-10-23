using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Explicit thermal acceptance only until mixed runtime/save activation.
    // Native helpers replay work without editing either endpoint. Actual carrier
    // bits must agree before any represented body heat can enter the inventory.
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
        private sealed class Row
        {
            internal readonly ConduitFlow Flow;internal readonly int Cell;internal readonly PipeInventory Inventory;
            internal readonly ConduitFlow.ConduitContents Before,Shadow;internal readonly Input Input;
            internal Output Result;internal bool Consumed;
            internal Row(ConduitFlow flow,int cell,PipeInventory inventory,ConduitFlow.ConduitContents before,ConduitFlow.ConduitContents shadow,Input input)
            {Flow=flow;Cell=cell;Inventory=inventory;Before=before;Shadow=shadow;Input=input;}
        }
        internal sealed class Scope
        {
            private readonly long epoch;
            private readonly List<Row> rows=new List<Row>();
            private readonly Dictionary<(ConduitFlow,int),Row> byCell=new Dictionary<(ConduitFlow,int),Row>();
            private bool validated;
            internal Scope(float seconds)
            {
                epoch=Epoch;var bodies=new HashSet<int>();
                foreach(var sample in ColonyConduits.ThermalProbeSamples())
                {
                    if(!(sample.Native.mass>0))continue; // Exact unprojectable traces remain held.
                    var conduit=sample.Flow.GetConduit(sample.Cell);
                    var go=sample.Flow.soaInfo.GetConduitGO(conduit.idx);
                    if(go==null)throw new InvalidOperationException("Owned conduit contact has no physical body.");
                    var body=go.GetComponent<PrimaryElement>();var building=go.GetComponent<Building>();
                    if(body==null||building==null)throw new InvalidOperationException("Owned conduit body registration is incomplete.");
                    var payload=GameComps.StructureTemperatures.GetPayload(GameComps.StructureTemperatures.GetHandle(go));
                    var handle=payload.simHandleCopy;
                    if(!payload.enabled||payload.bypass||!Sim.IsValidHandle(handle)||Game.Instance.simData.buildingTemperatures==null)
                        throw new NotSupportedException("Owned contact needs a live, enabled native body registration.");
                    var record=Game.Instance.simData.buildingTemperatures[Sim.GetHandleIndex(handle)];
                    if(record.handle!=handle||!bodies.Add(handle))throw new NotSupportedException("Owned contact body identity is stale or shared by multiple contacts.");
                    var carrier=ElementLoader.FindElementByHash(sample.Native.element);var definition=building.Def;
                    var input=new Input{Size=44,Version=1,Flags=definition.ThermalConductivity<1?1u:0,
                        Seconds=seconds,FluidTemperature=sample.Native.temperature,FluidCapacity=sample.Native.mass*carrier.specificHeatCapacity,
                        FluidConductivity=carrier.thermalConductivity,BodyTemperature=record.temperature,
                        BodyCapacity=definition.MassForTemperatureModification*body.Element.specificHeatCapacity,
                        ConduitConductivity=body.Element.thermalConductivity*definition.ThermalConductivity};
                    var row=new Row(sample.Flow,sample.Cell,sample.Inventory,sample.Native,sample.Shadow,input);rows.Add(row);byCell.Add((sample.Flow,sample.Cell),row);
                }
                // Replay is pure; batching changes no native contact ordering or
                // state. Shared bodies were refused before making any call.
                for(var first=0;first<rows.Count;first+=4096)
                {
                    var count=Math.Min(4096,rows.Count-first);var inputs=new Input[count];var outputs=new Output[count];
                    for(var i=0;i<count;++i)inputs[i]=rows[first+i].Input;
                    fixed(Input* source=inputs)fixed(Output* destination=outputs)
                    {var result=Replay(source,(uint)count,destination,(uint)count);if(result!=0)throw new InvalidOperationException("Native conduit contact replay refused: "+result);}
                    for(var i=0;i<count;++i)
                    {
                        if(outputs[i].Size!=20||outputs[i].Version!=1)throw new InvalidOperationException("Native contact returned an unsupported record.");
                        rows[first+i].Result=outputs[i];
                    }
                }
            }
            internal void Validate()
            {
                if(epoch!=Epoch)return;
                foreach(var row in rows)
                {
                    if(!row.Flow.HasConduit(row.Cell)||!ColonyConduits.TryRead(row.Flow,row.Cell,out var held)||!ReferenceEquals(held,row.Inventory))
                        throw new NotSupportedException("Conduit inventory/phase changed before contact validation.");
                    var actual=ColonyConduits.ThermalProbeNative(row.Flow,row.Cell);
                    if(BitConverter.SingleToInt32Bits(actual.temperature)!=BitConverter.SingleToInt32Bits(row.Result.CarrierTemperature))
                        throw new InvalidOperationException($"Native contact carrier mismatch at {row.Cell}: actual {actual.temperature:R}, replay {row.Result.CarrierTemperature:R}.");
                }
                validated=true;
            }
            internal bool Take(ConduitFlow flow,int cell,ConduitFlow.ConduitContents after,ConduitFlow.ConduitContents shadow,out PipeAmount heat)
            {
                heat=PipeAmount.Zero;if(epoch!=Epoch||!byCell.TryGetValue((flow,cell),out var row))return false;
                if(!validated)throw new NotSupportedException("A pipe phase/transfer reached an unvalidated native contact.");
                if(row.Consumed)return false;
                if(BitConverter.SingleToInt32Bits(after.temperature)!=BitConverter.SingleToInt32Bits(row.Result.CarrierTemperature)||
                    BitConverter.SingleToInt32Bits(shadow.temperature)!=BitConverter.SingleToInt32Bits(row.Shadow.temperature))
                    throw new InvalidOperationException("Native contact changed between validation and physical settlement.");
                var carrier=ElementLoader.FindElementByHash(row.Shadow.element);
                var lagged=new PipeAmount((double)row.Shadow.mass*carrier.specificHeatCapacity*1000*((double)row.Before.temperature-row.Shadow.temperature));
                var contact=PipeContactWork.IntoFluid(row.Input.BodyCapacity,row.Input.BodyTemperature,row.Result.BodyTemperature);
                heat=lagged.Plus(contact);row.Consumed=true;ContactHeat=ContactHeat.Plus(contact);LaggedHeat=LaggedHeat.Plus(lagged);++Contacts;return true;
            }
            internal void Complete()
            {
                if(epoch!=Epoch)return;
                foreach(var row in rows)if(!row.Consumed)throw new InvalidOperationException("Validated conduit work was not settled.");
            }
        }
        private static Scope? active;
        private static long Epoch;
        internal static bool Enabled { get; private set; }
        internal static long Contacts { get; private set; }
        internal static PipeAmount ContactHeat { get; private set; }
        internal static PipeAmount LaggedHeat { get; private set; }
        internal static string Report=>$"{Contacts} validated represented-body contacts; {ContactHeat.Value:R} J contact and {LaggedHeat.Value:R} J retained earlier heat";
        internal static void StartControl()
        {
            if(!ColonyConduits.Active)throw new InvalidOperationException("Contact heat requires owned pipe contents.");
            if(Marshal.SizeOf<Input>()!=44||Marshal.SizeOf<Output>()!=20||Version()!=1)throw new NotSupportedException("Native contact replay version/layout is unsupported.");
            Enabled=true;
        }
        internal static void Reset(){Enabled=false;active=null;++Epoch;Contacts=0;ContactHeat=LaggedHeat=PipeAmount.Zero;}
        internal static Scope? Begin(float seconds)
        {
            if(!Enabled)return null;if(active!=null)throw new InvalidOperationException("Nested native conduit contacts are unsupported.");
            var scope=new Scope(seconds);active=scope;return scope;
        }
        internal static bool TryTake(ConduitFlow flow,int cell,ConduitFlow.ConduitContents after,ConduitFlow.ConduitContents shadow,out PipeAmount heat)
        {heat=PipeAmount.Zero;return active!=null&&active.Take(flow,cell,after,shadow,out heat);}
        internal static void End(Scope? scope){if(ReferenceEquals(active,scope))active=null;}
    }
}
