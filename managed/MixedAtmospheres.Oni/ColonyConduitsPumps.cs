using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyPipePumps
    {
        private sealed class State
        {
            internal readonly ElementConsumer Consumer;internal readonly ConduitDispenser Dispenser;internal readonly Storage Storage;
            internal readonly List<ulong> Buffers=new List<ulong>();internal readonly SortedDictionary<ulong,PipeAmount> Pending=new SortedDictionary<ulong,PipeAmount>();
            internal readonly int Handle,Cell;internal readonly byte Phase;internal readonly ConduitFlow Flow;
            internal State(ElementConsumer consumer,ConduitDispenser dispenser,int handle,int cell,byte phase,ConduitFlow flow)
            {Consumer=consumer;Dispenser=dispenser;Storage=consumer.storage;Handle=handle;Cell=cell;Phase=phase;Flow=flow;}
            internal PipeAmount ActualMass
            {get{var sum=PipeAmount.Zero;foreach(var id in Buffers)sum=sum.Plus(ColonyPipePorts.BufferInventory(id).Total);return sum;}}
            internal PipeAmount ReservedMass
            {get{var sum=ActualMass;foreach(var held in Pending.Values)sum=sum.Plus(held);return sum;}}
            internal PipeInventory Inventory
            {get{var inventory=PipeInventory.Empty;foreach(var id in Buffers)inventory=inventory.Joined(ColonyPipePorts.BufferInventory(id));return inventory;}}
        }
        private sealed class Identity : IEqualityComparer<Storage>
        {public bool Equals(Storage? a,Storage? b)=>ReferenceEquals(a,b);public int GetHashCode(Storage a)=>RuntimeHelpers.GetHashCode(a);}
        private static readonly Dictionary<Storage,State> States=new Dictionary<Storage,State>(new Identity());
        private static readonly Dictionary<ulong,State> Requests=new Dictionary<ulong,State>();
        private static readonly Dictionary<(ConduitFlow,int),State> ByOutlet=new Dictionary<(ConduitFlow,int),State>();
        private static readonly FieldInfo Instances=AccessTools.Field(typeof(ElementConsumer),"handleInstanceMap"),
            Outlet=AccessTools.Field(typeof(ConduitDispenser),"utilityCell"),Operation=AccessTools.Field(typeof(ConduitDispenser),"operational");
        private static readonly Func<ConduitDispenser,ConduitFlow> Manager=(Func<ConduitDispenser,ConduitFlow>)Delegate.CreateDelegate(typeof(Func<ConduitDispenser,ConduitFlow>),AccessTools.Method(typeof(ConduitDispenser),"GetConduitManager"));
        private static readonly Action<Sim.ConsumedMassInfo> Notify=(Action<Sim.ConsumedMassInfo>)Delegate.CreateDelegate(typeof(Action<Sim.ConsumedMassInfo>),AccessTools.Method(typeof(ElementConsumer),"AddMass"));
        [ThreadStatic] private static State? delivering;
        [ThreadStatic] private static int writes;
        [ThreadStatic] private static SimHashes expectedElement;
        [ThreadStatic] private static float expectedMass;
        internal static bool Enabled { get; private set; }
        internal static void StartControl()
        {
            if(Enabled)return;if(!ColonyPipePorts.Enabled)throw new InvalidOperationException("Pump ownership requires precise pipe ports.");
            uint version;
            try{version=OniNativeColony.ActorBatchVersion();}
            catch(EntryPointNotFoundException error){throw new NotSupportedException("Mixed pumps require atomic native actor batches before activation.",error);}
            if(version<1)throw new NotSupportedException("Mixed pumps require atomic native actor batches before activation.");
            Enabled=true;
        }
        internal static ColonyPipePorts.ActorBatch? BeginActorBatch()=>Enabled?new ColonyPipePorts.ActorBatch():null;
        internal static int BoundPumps=>States.Count;
        internal static long Notifications { get; private set; }
        internal static long Queued { get; private set; }
        internal static void Reset(){Enabled=false;States.Clear();Requests.Clear();ByOutlet.Clear();delivering=null;writes=0;Notifications=0;Queued=0;MixtureInspection.SetConduitStorageReader(null);}
        internal static bool IsPumpJob(ulong localId)=>Requests.ContainsKey(localId);

        // The actor retains its registration, saved rate and operational logic.
        // A claim covers the complete request: either queued with capacity held,
        // or refused without extracting anything. No scalar path also consumes it.
        internal static bool QueueConsumer(int handle,int worldCell,uint kind,uint radius,float requested,List<OniNativeColony.Transfer> ordered,ColonyPipePorts.ActorBatch batch)
        {
            if(!Enabled||kind!=OniNativeColony.ExtractGas&&kind!=OniNativeColony.ExtractLiquid)return false;
            var instances=(Dictionary<int,ElementConsumer>)Instances.GetValue(null);
            if(!instances.TryGetValue(handle,out var consumer)||consumer==null||!consumer.storeOnConsume||consumer.GetComponent<Pump>()==null)return false;
            var dispenser=consumer.GetComponent<ConduitDispenser>();if(dispenser==null||consumer.storage==null)return false;
            var flow=Manager(dispenser);var cell=(int)Outlet.GetValue(dispenser);
            var phase=kind==OniNativeColony.ExtractGas?(byte)1:(byte)2;
            if((phase==1)!=(flow.conduitType==ConduitType.Gas)||!ColonyConduits.Owns(flow,cell))return false;
            if(!States.TryGetValue(consumer.storage,out var state))
            {
                if(consumer.storage.items.Count!=0)throw new NotSupportedException("A mixed pump cannot silently adopt existing native storage packets.");
                state=new State(consumer,dispenser,handle,cell,phase,flow);States.Add(consumer.storage,state);ByOutlet.Add((flow,cell),state);MixtureInspection.SetConduitStorageReader(Inspect);
            }
            if(!ReferenceEquals(state.Consumer,consumer)||state.Handle!=handle||state.Cell!=cell||state.Phase!=phase)
                throw new InvalidOperationException("Mixed pump component or native handle identity changed.");
            var capacity=state.Storage.capacityKg;
            var room=new PipeAmount(capacity).Minus(state.ReservedMass);var amount=room.Sign>0?Math.Min(requested,room.Value):0;
            while(amount>0&&new PipeAmount(amount).Minus(room).Sign>0)amount=BitConverter.Int64BitsToDouble(BitConverter.DoubleToInt64Bits(amount)-1);
            if(!(amount>0))return true;
            var id=batch.Extract(ordered,cell,phase,(uint)worldCell,amount,radius);
            state.Pending.Add(id,new PipeAmount(amount));state.Buffers.Add(id);Requests.Add(id,state);return true;
        }
        internal static void Accepted(ulong localId){if(Requests.ContainsKey(localId))++Queued;}
        internal static void Aborted(ulong localId)
        {
            Requests.Remove(localId);
            foreach(var state in States.Values){state.Pending.Remove(localId);state.Buffers.Remove(localId);}
        }
        internal static void Completed(ulong localId,IReadOnlyList<OniNativeColony.Receipt>? receipts)
        {
            if(!Requests.TryGetValue(localId,out var state))return;
            state.Pending.Remove(localId);Requests.Remove(localId);
            if(state.Consumer!=null)
            {
                foreach(var receipt in receipts??Array.Empty<OniNativeColony.Receipt>())
                {
                    var info=ColonyActors.Event(in receipt);info.simHandle=state.Handle;
                    var previous=delivering;var previousWrites=writes;delivering=state;writes=info.mass>0?1:0;
                    expectedElement=ElementLoader.elements[info.removedElemIdx].id;expectedMass=info.mass;
                    try{Notify(info);if(writes!=0)throw new InvalidOperationException("Native pump notification did not reach its inspected storage boundary.");}
                    finally{delivering=previous;writes=previousWrites;}
                    ++Notifications;
                }
                if(state.Storage.items.Count!=0)throw new InvalidOperationException("A native callback introduced a second pump inventory.");
            }
            Prune(state);
        }
        internal static bool SuppressStorage(Storage storage,SimHashes element,float mass)
        {
            if(delivering==null||writes==0||!ReferenceEquals(delivering.Storage,storage))return false;
            if(element!=expectedElement||mass!=expectedMass)throw new InvalidOperationException("Pump notification attempted an unexpected native storage write.");
            --writes;
            // Native storage emits both notifications before the consumer's
            // stored event. The owned container has no fake mutable packet.
            Changed(delivering);return true;
        }
        internal static bool ReadMass(Storage storage,out float mass)
        {
            if(ColonyPipePorts.Enabled&&States.TryGetValue(storage,out var state)){mass=(float)state.ActualMass.Value;return true;}
            mass=0;return false;
        }
        internal static bool ReadFull(ElementConsumer consumer,out bool full)
        {
            if(ColonyPipePorts.Enabled&&consumer.storage!=null&&States.TryGetValue(consumer.storage,out var state))
            {full=(float)state.ActualMass.Value>=state.Storage.capacityKg;return true;}
            full=false;return false;
        }
        internal static bool Dispense(ConduitDispenser dispenser)
        {
            if(!ColonyPipePorts.Enabled||dispenser.storage==null||!States.TryGetValue(dispenser.storage,out var state))return false;
            var before=state.ActualMass;
            // The owned buffer has no native packet for the original reader.
            // Project its actual empty/blocked status even while power or a
            // native operational flag prevents dispensing; do not change that
            // operational decision or dispatch any material through it.
            dispenser.empty=before.Sign==0;
            dispenser.blocked=before.Sign>0&&!ColonyConduits.HasRoom(Manager(dispenser),state.Cell);
            var operational=(Operational)Operation.GetValue(dispenser);
            if(!dispenser.alwaysDispense&&(operational==null||!operational.IsOperational))return true;
            foreach(var id in state.Buffers.ToArray())if(!state.Pending.ContainsKey(id))ColonyPipePorts.PlaceBuffer(id);
            var after=state.ActualMass;dispenser.empty=after.Sign==0&&before.Sign==0;
            if(before.Sign>0&&before.Minus(after).Sign==0)dispenser.blocked=true;
            if(before.Minus(after).Sign!=0)
            {
                Changed(state);state.Storage.Trigger(2051543657,null);
            }
            Prune(state);return true;
        }
        private static MixturePipeReading? Inspect(Storage storage)=>Owns(storage)?ColonyPipeInspection.Describe(Inventory(storage),"Pump contents"):null;
        internal static void ProjectStatus(ConduitDispenser dispenser)
        {
            if(!Enabled||dispenser.storage==null||!States.TryGetValue(dispenser.storage,out var state))return;
            Project(state);
        }
        internal static void ProjectPipe(ConduitFlow flow,int cell)
        {if(Enabled&&ByOutlet.TryGetValue((flow,cell),out var state))Project(state);}
        private static void Project(State state)
        {
            state.Dispenser.empty=state.ActualMass.Sign==0;
            state.Dispenser.blocked=!state.Dispenser.empty&&!ColonyConduits.HasRoom(state.Flow,state.Cell);
        }
        internal static PipeInventory Inventory(Storage storage)=>States.TryGetValue(storage,out var state)?state.Inventory:PipeInventory.Empty;
        internal static bool Owns(Storage storage)=>ColonyPipePorts.Enabled&&States.ContainsKey(storage);
        private static void Changed(State state)
        {
            Project(state);
            state.Storage.Trigger((int)GameHashes.OnStorageChange,null);
            state.Storage.OnStorageChange?.Invoke(null!);
        }
        private static void Prune(State state)=>state.Buffers.RemoveAll(id=>!state.Pending.ContainsKey(id)&&ColonyPipePorts.BufferInventory(id).Count==0);
    }
    [HarmonyPatch]
    internal static class ColonyMixedPumpStorageMassPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()
        {yield return AccessTools.Method(typeof(Storage),"MassStored");yield return AccessTools.Method(typeof(Storage),"ExactMassStored");}
        private static bool Prefix(Storage __instance,ref float __result)
        {if(!ColonyPipePumps.ReadMass(__instance,out var mass))return true;__result=mass;return false;}
    }
    [HarmonyPatch(typeof(ElementConsumer),"IsStorageFull")]
    internal static class ColonyMixedPumpFullPatch
    {
        private static bool Prefix(ElementConsumer __instance,ref bool __result)
        {if(!ColonyPipePumps.ReadFull(__instance,out var full))return true;__result=full;return false;}
    }
    [HarmonyPatch]
    internal static class ColonyMixedPumpNotificationStoragePatch
    {
        private static IEnumerable<MethodBase> TargetMethods()
        {yield return AccessTools.Method(typeof(Storage),"AddGasChunk");yield return AccessTools.Method(typeof(Storage),"AddLiquid");}
        private static bool Prefix(Storage __instance,SimHashes __0,float __1)=>!ColonyPipePumps.SuppressStorage(__instance,__0,__1);
    }
    // Native ConduitUpdate clears blocked even when isOn is false and it never
    // calls Dispense. Keep the owned container's reader coherent at that final
    // boundary; this does not enable an inactive dispenser or move inventory.
    [HarmonyPatch(typeof(ConduitDispenser),"ConduitUpdate")]
    internal static class ColonyMixedPumpStatusPatch
    {private static void Postfix(ConduitDispenser __instance)=>ColonyPipePumps.ProjectStatus(__instance);}
    [HarmonyPatch(typeof(ConduitDispenser),"Dispense")]
    internal static class ColonyMixedPumpDispensePatch
    {private static bool Prefix(ConduitDispenser __instance)=>!ColonyPipePumps.Dispense(__instance);}
}
