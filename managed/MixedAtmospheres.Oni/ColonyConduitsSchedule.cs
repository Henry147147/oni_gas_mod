using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Settle pending topology without advancing either conduit flow clock.
    // ForceRebuildNetworks only marks the manager dirty; Update performs the
    // synchronous rebuild and its normal registration callbacks.
    internal static class ColonyConduitsSchedule
    {
        private static readonly FieldInfo Manager = AccessTools.Field(typeof(ConduitFlow), "networkMgr");

        internal static int Flush(ConduitFlow flow)
        {
            var manager = Manager?.GetValue(flow) ?? throw new NotSupportedException("Missing conduit network manager.");
            var dirty = AccessTools.Property(manager.GetType(), "IsDirty") ??
                throw new NotSupportedException("Missing conduit topology readiness query.");
            var update = AccessTools.Method(manager.GetType(), "Update", Type.EmptyTypes) ??
                throw new NotSupportedException("Missing synchronous conduit topology update.");
            var updates = 0;
            while ((bool)dirty.GetValue(manager))
            {
                if (updates == 8) throw new InvalidOperationException("Conduit topology did not settle before capture or continuation.");
                update.Invoke(manager, null);
                ++updates;
            }
            return updates;
        }

        internal static int FlushTopology() => Flush(Game.Instance.gasConduitFlow) + Flush(Game.Instance.liquidConduitFlow);

        private static FieldInfo Field(Type type, string name) => AccessTools.Field(type,name) ??
            throw new NotSupportedException("Missing native conduit schedule field: "+type.FullName+"."+name);
        private static object Value(object instance,string name) => Field(instance.GetType(),name).GetValue(instance) ??
            throw new NotSupportedException("Missing native conduit schedule value: "+name);
        private static IList List(object instance,string name) => (IList)Value(instance,name);
        private static T Read<T>(object instance,string name) => (T)Value(instance,name);
        // Keep these native runtime fields explicit in the game contract.
        private static readonly FieldInfo ConsumerOn=AccessTools.Field(typeof(ConduitConsumer),"isOn"),
            ConsumerSatisfied=AccessTools.Field(typeof(ConduitConsumer),"satisfied"),
            ConsumerRate=AccessTools.Field(typeof(ConduitConsumer),"consumptionRate");

        private static PipeNativeScheduleState.Contents Pack(ConduitFlow.ConduitContents value) =>
            new PipeNativeScheduleState.Contents { Element=(int)value.element, Temperature=value.temperature,
                Disease=value.diseaseIdx,Germs=value.diseaseCount,Initial=Read<float>(value,"initial_mass"),
                Added=Read<float>(value,"added_mass"),Removed=Read<float>(value,"removed_mass") };
        private static ConduitFlow.ConduitContents Unpack(PipeNativeScheduleState.Contents value)
        {
            object contents=new ConduitFlow.ConduitContents((SimHashes)value.Element,0,value.Temperature,value.Disease,value.Germs);
            Field(contents.GetType(),"initial_mass").SetValue(contents,value.Initial);
            Field(contents.GetType(),"added_mass").SetValue(contents,value.Added);
            Field(contents.GetType(),"removed_mass").SetValue(contents,value.Removed);
            return (ConduitFlow.ConduitContents)contents;
        }
        private static PipeNativeScheduleState.Updater Identify(object updater)
        {
            var callback=Read<Action<float>>(updater,"callback");
            if(callback.GetInvocationList().Length!=1)
                throw new NotSupportedException("A multicast conduit updater cannot be rebound exactly.");
            var result=new PipeNativeScheduleState.Updater {Priority=Convert.ToInt32(Value(updater,"priority")),
                DeclaringType=callback.Method.DeclaringType?.FullName??"",Method=callback.Method.Name};
            if(callback.Target==null) { result.Validate();return result; }
            GameObject go;
            if(callback.Target is Component component)
            {
                result.Kind=1;go=component.gameObject;
                var peers=go.GetComponents(component.GetType());
                result.Ordinal=Array.FindIndex(peers,p=>ReferenceEquals(p,component));
            }
            else if(callback.Target is StateMachine.Instance smi)
            { result.Kind=2;go=smi.gameObject; }
            else throw new NotSupportedException("Unsupported conduit callback target: "+callback.Target.GetType().FullName);
            var prefab=go.GetComponent<KPrefabID>();
            if(prefab==null)throw new InvalidDataException("A conduit callback has no persistent prefab identity.");
            result.Prefab=prefab.InstanceID;result.TargetType=callback.Target.GetType().FullName??"";
            if(callback.Target is ConduitConsumer consumer)
            {
                result.ControlKind=1;
                result.ControlFlags=(byte)(((bool)ConsumerOn.GetValue(consumer)?1:0)|(consumer.isConsuming?2:0)|(consumer.consumedLastTick?4:0)|((bool)ConsumerSatisfied.GetValue(consumer)?8:0));
                result.ConsumerRate=(float)ConsumerRate.GetValue(consumer);result.LastConsumedElement=(int)consumer.lastConsumedElement;
            }
            else if(callback.Target is ConduitDispenser dispenser)
            {
                result.ControlKind=2;
                result.ControlFlags=(byte)((dispenser.isOn?1:0)|(dispenser.blocked?2:0)|(dispenser.empty?4:0));
            }
            result.Validate();return result;
        }
        private static IEnumerable<(int Cell,int Index,object Node)> Rows(ConduitFlow flow)
        {
            var grid=(Array)Value(flow,"grid");
            for(var cell=0;cell<grid.Length;++cell)
            {
                var node=grid.GetValue(cell)!;var index=Read<int>(node,"conduitIdx");
                if(index<0)continue;
                if(!ColonyConduits.Geometry.Contains(cell))
                    throw new InvalidDataException("A native conduit lies outside the saved owner geometry.");
                yield return (cell,index,node);
            }
        }
        private static PipeNativeScheduleState.Flow CaptureFlow(ConduitFlow flow,byte phase)
        {
            if(((IEnumerable)Value(flow,"replacements")).Cast<object>().Any())
                throw new InvalidOperationException("Native conduit replacements have not settled before scheduling capture.");
            var result=new PipeNativeScheduleState.Flow { Phase=phase,Elapsed=Read<float>(flow,"elapsedTime"),
                UpdatersDirty=Read<bool>(flow,"dirtyConduitUpdaters") };
            var soa=flow.soaInfo;var initial=List(soa,"initialContents");
            foreach(var row in Rows(flow))
                result.Cells.Add(new PipeNativeScheduleState.Cell { Index=row.Cell,Current=Pack(Read<ConduitFlow.ConduitContents>(row.Node,"contents")),
                    Initial=Pack((ConduitFlow.ConduitContents)initial[row.Index]),
                    Permitted=Convert.ToInt32(List(soa,"permittedFlowDirections")[row.Index]),
                    Source=Convert.ToInt32(List(soa,"srcFlowDirections")[row.Index]),
                    Pull=Convert.ToInt32(List(soa,"pullDirections")[row.Index]),
                    Target=Convert.ToInt32(List(soa,"targetFlowDirections")[row.Index]) });
            foreach(var network in List(flow,"networks"))
                result.Networks.Add(List(network!,"cells").Cast<int>().ToArray());
            foreach(var updater in List(flow,"conduitUpdaters"))result.Updaters.Add(Identify(updater!));
            return result;
        }
        internal static PipeNativeScheduleState Capture()
        {
            FlushTopology();
            var result=new PipeNativeScheduleState {PreservePortState=true};
            result.Flows.Add(CaptureFlow(Game.Instance.gasConduitFlow,1));
            result.Flows.Add(CaptureFlow(Game.Instance.liquidConduitFlow,2));
            result.Validate(ColonyConduits.Geometry);return result;
        }
        private static string Membership(IEnumerable<int> cells) => string.Join(",",cells.Distinct().OrderBy(c=>c));
        private static System.Action PreparePortState(object? target,PipeNativeScheduleState.Updater saved)
        {
            saved.Validate(true);
            if(saved.ControlKind==1)
            {
                if(!(target is ConduitConsumer consumer))throw new InvalidDataException("Saved consumer control has a different callback target.");
                return ()=>
                {
                    ConsumerOn.SetValue(consumer,(saved.ControlFlags&1)!=0);consumer.isConsuming=(saved.ControlFlags&2)!=0;
                    consumer.consumedLastTick=(saved.ControlFlags&4)!=0;ConsumerSatisfied.SetValue(consumer,(saved.ControlFlags&8)!=0);
                    ConsumerRate.SetValue(consumer,saved.ConsumerRate);consumer.lastConsumedElement=(SimHashes)saved.LastConsumedElement;
                };
            }
            if(saved.ControlKind==2)
            {
                if(!(target is ConduitDispenser dispenser))throw new InvalidDataException("Saved dispenser control has a different callback target.");
                return ()=>{dispenser.isOn=(saved.ControlFlags&1)!=0;dispenser.blocked=(saved.ControlFlags&2)!=0;dispenser.empty=(saved.ControlFlags&4)!=0;};
            }
            return ()=>{};
        }
        private static System.Action PrepareRestore(ConduitFlow flow,PipeNativeScheduleState.Flow saved,bool savedPortState)
        {
            if(((IEnumerable)Value(flow,"replacements")).Cast<object>().Any())
                throw new InvalidDataException("Native conduit replacements have not settled before scheduling restore.");
            var rows=Rows(flow).ToDictionary(row=>row.Cell);
            if(rows.Count!=saved.Cells.Count||saved.Cells.Any(cell=>!rows.ContainsKey(cell.Index)))
                throw new InvalidDataException("Restored native conduit cells differ from their saved schedule.");
            foreach(var cell in saved.Cells)ColonyConduits.ValidateNativeProjection(flow,cell.Index,Unpack(cell.Current));
            var networks=List(flow,"networks");
            var byMembership=new Dictionary<string,Queue<object>>();
            foreach(var network in networks)
            {
                var key=Membership(List(network!,"cells").Cast<int>());
                if(!byMembership.TryGetValue(key,out var queue))byMembership.Add(key,queue=new Queue<object>());
                queue.Enqueue(network!);
            }
            var orderedNetworks=new List<(object Network,int[] Cells)>();
            foreach(var cells in saved.Networks)
            {
                if(!byMembership.TryGetValue(Membership(cells),out var queue)||queue.Count==0)
                    throw new InvalidDataException("Restored native conduit network membership differs from its saved schedule.");
                orderedNetworks.Add((queue.Dequeue(),cells));
            }
            if(orderedNetworks.Count!=networks.Count)
                throw new InvalidDataException("Restored native conduit network count differs from its saved schedule.");
            var updaters=List(flow,"conduitUpdaters");
            var byIdentity=new Dictionary<(byte,int,int,int,string,string,string),Queue<object>>();
            var targets=new Dictionary<(byte,int,int,int,string,string,string),object?>();
            foreach(var updater in updaters)
            {
                var key=Identify(updater!).Key;var target=Read<Action<float>>(updater!,"callback").Target;
                if(targets.TryGetValue(key,out var other)&&!ReferenceEquals(other,target))
                    throw new InvalidDataException("Ambiguous persistent conduit callback identity.");
                targets[key]=target;
                if(!byIdentity.TryGetValue(key,out var queue))byIdentity.Add(key,queue=new Queue<object>());
                queue.Enqueue(updater!);
            }
            var orderedUpdaters=new List<object>();
            var controls=new List<System.Action>();
            foreach(var updater in saved.Updaters)
            {
                if(!byIdentity.TryGetValue(updater.Key,out var queue)||queue.Count==0)
                    throw new InvalidDataException("Restored native conduit callbacks differ from their saved schedule.");
                orderedUpdaters.Add(queue.Dequeue());
                if(savedPortState)controls.Add(PreparePortState(targets[updater.Key],updater));
            }
            if(orderedUpdaters.Count!=updaters.Count)
                throw new InvalidDataException("Restored native conduit callback count differs from its saved schedule.");
            // Resolve and validate both flows completely before changing one.
            var soa=flow.soaInfo;var initial=List(soa,"initialContents");
            var permitted=List(soa,"permittedFlowDirections");var source=List(soa,"srcFlowDirections");
            var pull=List(soa,"pullDirections");var targetDirections=List(soa,"targetFlowDirections");
            return () =>
            {
                foreach(var cell in saved.Cells)
                {
                    var index=rows[cell.Index].Index;
                    ColonyConduits.RestoreNativeProjection(flow,cell.Index,Unpack(cell.Current));
                    initial[index]=Unpack(cell.Initial);
                    permitted[index]=(ConduitFlow.FlowDirections)cell.Permitted;
                    source[index]=(ConduitFlow.FlowDirections)cell.Source;
                    pull[index]=(ConduitFlow.FlowDirections)cell.Pull;
                    targetDirections[index]=(ConduitFlow.FlowDirections)cell.Target;
                }
                networks.Clear();
                foreach(var network in orderedNetworks)
                {
                    var cells=List(network.Network,"cells");cells.Clear();foreach(var cell in network.Cells)cells.Add(cell);
                    networks.Add(network.Network);
                }
                foreach(var restore in controls)restore();
                updaters.Clear();foreach(var updater in orderedUpdaters)updaters.Add(updater);
                Field(typeof(ConduitFlow),"elapsedTime").SetValue(flow,saved.Elapsed);
                Field(typeof(ConduitFlow),"dirtyConduitUpdaters").SetValue(flow,saved.UpdatersDirty);
            };
        }
        internal static void Restore(PipeNativeScheduleState saved)
        {
            saved.Validate(ColonyConduits.Geometry);FlushTopology();
            var gas=PrepareRestore(Game.Instance.gasConduitFlow,saved.Flows[0],saved.PreservePortState);
            var liquid=PrepareRestore(Game.Instance.liquidConduitFlow,saved.Flows[1],saved.PreservePortState);
            gas();liquid();
        }
    }
}
