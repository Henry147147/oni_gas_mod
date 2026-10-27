using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private sealed class ClusterPortProbe
        {
            internal readonly string Name;
            internal readonly ConduitFlow Flow;
            internal readonly int Input,Output;
            internal readonly Storage Storage;
            internal ClusterPortProbe(string name,ConduitFlow flow,int input,int output,Storage storage)
            {Name=name;Flow=flow;Input=input;Output=output;Storage=storage;}
        }
        private static readonly List<ClusterPortProbe> clusterPorts=new List<ClusterPortProbe>();
        private static object ClusterField(object value,string name)=>AccessTools.Field(value.GetType(),name).GetValue(value);
        private static ConduitType ClusterPortType(object value)=>(ConduitType)ClusterField(ClusterField(value,"conduitPortInfo"),"conduitType");
        private static int ClusterPortCell(object value,bool input)=>(int)ClusterField(ClusterField(value,"conduitPort"),input?"inputCell":"outputCell");
        private static bool RocketPortWorld(Component value)=>clusterRocketWorld!=null&&Grid.IsValidCell(Grid.PosToCell(value.gameObject))&&
            (Grid.WorldIdx[Grid.PosToCell(value.gameObject)]==clusterRocketWorld.id||Grid.WorldIdx[Grid.PosToCell(value.gameObject)]==clusterOriginalWorld);
        private static double ClusterPipeMass(PipeInventory inventory,SimHashes element)
        {var id=ColonyConduits.MaterialOf(element).Id;double mass=0;foreach(var part in inventory.Parts)if(part.Id==id)mass+=part.Mass;return mass;}
        private static void SpacedOutRocketPorts(int frame,OniNativeColony.Status status)
        {
            if(Once(0,"rocket port connections"))
            {
                if(clusterCapsule==null||clusterRocketWorld==null)
                    throw new InvalidOperationException("Run spaced-out-rocket before its live port control.");
                clusterPorts.Clear();
                var senders=Resources.FindObjectsOfTypeAll<RocketConduitSender>().Where(s=>s.isSpawned&&RocketPortWorld(s)).ToArray();
                var receivers=Resources.FindObjectsOfTypeAll<RocketConduitReceiver>().Where(r=>r.isSpawned&&RocketPortWorld(r)).ToArray();
                if(senders.Length!=4||receivers.Length!=4)throw new InvalidOperationException($"Expected four live gas/liquid sender/receiver pairs; got {senders.Length}/{receivers.Length}.");
                foreach(var value in senders.Cast<Component>().Concat(receivers))
                {
                    var type=ClusterPortType(value);var cell=ClusterPortCell(value,value is RocketConduitSender);
                    var network=type==ConduitType.Gas?Game.Instance.gasConduitSystem:Game.Instance.liquidConduitSystem;
                    Pipe(network,type==ConduitType.Gas?GasConduitConfig.ID:LiquidConduitConfig.ID,new List<int>{cell});
                }
            }
            if(frame>=4&&!recorded.ContainsKey("rocket ports seeded"))
            {
                var ready=true;clusterPorts.Clear();
                foreach(var sender in Resources.FindObjectsOfTypeAll<RocketConduitSender>().Where(s=>s.isSpawned&&RocketPortWorld(s)))
                {
                    var receiver=(RocketConduitReceiver)ClusterField(sender,"partnerReceiver");
                    if(receiver==null){ready=false;continue;}
                    var type=ClusterPortType(sender);var flow=Conduit.GetFlowManager(type);
                    var input=ClusterPortCell(sender,true);var output=ClusterPortCell(receiver,false);
                    var storage=(Storage)ClusterField(sender,"conduitStorage");
                    if(!flow.HasConduit(input)||!flow.HasConduit(output)||!ReferenceEquals(storage,ClusterField(receiver,"senderConduitStorage")))
                    {ready=false;continue;}
                    var from=Grid.WorldIdx[input];var to=Grid.WorldIdx[output];
                    if(from==to)throw new InvalidOperationException("A rocket port control did not cross worlds.");
                    clusterPorts.Add(new ClusterPortProbe($"{type} {from}->{to}",flow,input,output,storage));
                }
                if(!ready||clusterPorts.Count!=4)
                {
                    if(frame<30)return;
                    Record("native rocket partners and conduits became ready",false,$"ready {clusterPorts.Count}/4");Finish();return;
                }
                foreach(var probe in clusterPorts)
                {
                    if(ColonyConduits.ReadSynced(probe.Flow,probe.Input).Mass!=0||ColonyConduits.ReadSynced(probe.Flow,probe.Output).Mass!=0||probe.Storage.MassStored()!=0)
                        throw new InvalidOperationException("The rocket port source, target or shared storage was not empty: "+probe.Name);
                    var gas=probe.Flow.conduitType==ConduitType.Gas;
                    var first=gas?SimHashes.Oxygen:SimHashes.Water;var second=gas?SimHashes.CarbonDioxide:SimHashes.DirtyWater;
                    const float one=.75f,two=.25f;
                    var a=ColonyConduits.Seed(probe.Flow,probe.Input,first,one,300,byte.MaxValue,0);
                    var b=ColonyConduits.Seed(probe.Flow,probe.Input,second,two,300,byte.MaxValue,0);
                    if(a!=one||b!=two)throw new InvalidOperationException("Rocket port fixture source was refused: "+probe.Name);
                }
                recorded["rocket ports seeded"]=frame;
            }
            if(recorded.TryGetValue("rocket ports seeded",out var seeded)&&frame>=seeded+16&&!Current.Finished)
            {
                foreach(var probe in clusterPorts)
                {
                    var gas=probe.Flow.conduitType==ConduitType.Gas;
                    var output=ColonyConduits.ReadSynced(probe.Flow,probe.Output);
                    var first=ClusterPipeMass(output,gas?SimHashes.Oxygen:SimHashes.Water);
                    var second=ClusterPipeMass(output,gas?SimHashes.CarbonDioxide:SimHashes.DirtyWater);
                    var input=ColonyConduits.ReadSynced(probe.Flow,probe.Input).Mass;
                    Record(probe.Name+" moves both constituents",Math.Abs(first-.75)<1e-6&&Math.Abs(second-.25)<1e-6,
                        $"first {first:R} kg, second {second:R} kg; input {input:R} kg, storage {probe.Storage.MassStored():R} kg");
                    Record(probe.Name+" leaves no duplicate inventory",input<1e-6&&probe.Storage.MassStored()<1e-6,"source pipe and actual shared native storage drained");
                }
                Record("rocket ports keep closed world ledgers",status.State==1&&status.MaximumRelativeMassError<=1e-12&&status.MaximumRelativeEnergyError<=1e-12,
                    $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
                Record("rocket pipe and storage boundary accounts close",ColonyConduits.Audit(out var detail),detail);
                Finish();
            }
        }
    }
}
