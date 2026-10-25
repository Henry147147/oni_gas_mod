using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // First exact component-storage adapter: reservoirs. Native items provide
    // UI and the existing registered thermal contacts, never physical mass,
    // internal energy or germ ownership. Broader recipe/bottle mutations are
    // refused until their exact adapters are installed.
    internal static partial class ColonyOwnedStorage
    {
        private sealed class Projection
        {
            internal readonly PrimaryElement Item;
            internal readonly float InitialCapacity,InitialTemperature;
            internal int Handle=-1;
            internal float Capacity,Temperature;
            internal Projection(PrimaryElement item,float mass,float temperature)
            {Item=item;InitialCapacity=Capacity=mass*item.Element.specificHeatCapacity;InitialTemperature=Temperature=temperature;}
        }
        private sealed class Host
        {
            internal readonly Storage Storage;
            internal readonly OwnedFluidStore Store;
            internal ulong ProjectedRevision=ulong.MaxValue;
            internal readonly Dictionary<int,Projection> Projections=new Dictionary<int,Projection>();
            internal Host(Storage storage,OwnedFluidStore store){Storage=storage;Store=store;}
        }
        private static readonly Dictionary<Storage,Host> Hosts=new Dictionary<Storage,Host>();
        [ThreadStatic] private static int editing,registering;
        internal static bool Enabled {get;private set;}
        internal static int Count=>Hosts.Count;
        internal static long HeatPublications {get;private set;}
        internal static PipeInventory Inventory(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Store.Inventory:PipeInventory.Empty;
        internal static StorageJournalAmount Heat(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Store.Heat:default;
        internal static int RegisteredProjections(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Projections.Values.Count(view=>view.Handle>=0):0;
        internal static bool Audit(Storage storage)=>Hosts.TryGetValue(storage,out var host)&&host.Store.Audit();
        internal static void StartControl()
        {
            if(!ColonyPipeRuntime.Active||!ColonyMachinePorts.Enabled)throw new InvalidOperationException("Exact component storage requires the mixed-pipe runtime.");
            Enabled=true;MixtureInspection.SetOwnedStorageReader(storage=>Owns(storage)?ColonyPipeInspection.Describe(Inventory(storage),"Stored mixture"):null);
        }
        internal static void Reset(){ResetBiology();Enabled=false;Hosts.Clear();editing=registering=0;HeatPublications=0;MixtureInspection.SetOwnedStorageReader(null);}
        internal static bool Owns(Storage storage)=>Enabled&&Hosts.ContainsKey(storage);
        private static byte ReservoirPhase(Storage storage)
        {
            var prefab=storage.GetComponent<KPrefabID>();
            if(prefab==null||!ColonyConduits.Geometry.Contains(Grid.PosToCell(storage)))return 0;
            return prefab.PrefabTag.Name==GasReservoirConfig.ID?(byte)1:prefab.PrefabTag.Name==LiquidReservoirConfig.ID?(byte)2:(byte)0;
        }
        private static StoredFluidAddress Address(Storage storage)
        {
            var prefab=storage.GetComponent<KPrefabID>();var components=storage.GetComponents<Storage>();
            var ordinal=Array.FindIndex(components,item=>ReferenceEquals(item,storage));
            if(prefab==null||prefab.pendingDestruction||prefab.conflicted||!storage.isSpawned||storage.IsEndOfLife())
                throw new InvalidOperationException("Exact storage has no live, unique prefab owner.");
            return new StoredFluidAddress(prefab.InstanceID,ordinal);
        }
        private static Host BindEmpty(Storage storage,byte phase)
        {
            if(Hosts.TryGetValue(storage,out var existing))return existing;
            if(storage.items.Any(item=>item!=null))
                throw new InvalidOperationException("A native-item reservoir cannot be silently upgraded to exact storage.");
            var address=Address(storage);
            if(Hosts.Values.Any(host=>host.Store.Address.Equals(address)))throw new InvalidOperationException("Exact storage prefab/component identity is shared.");
            var host=new Host(storage,new OwnedFluidStore(address,phase,PipeInventory.Empty));Hosts.Add(storage,host);return host;
        }
        internal static bool Deliver(Storage storage,PipeInventory parcel)
        {
            if(!Enabled)return false;var phase=ReservoirPhase(storage);if(phase==0)return false;
            foreach(var part in parcel.Parts)if(ColonyConduits.MaterialPhase(part.Id)!=phase)throw new InvalidOperationException("A reservoir received a foreign fluid phase.");
            var host=BindEmpty(storage,phase);var move=host.Store.PrepareAdmission(parcel,storage.capacityKg,ColonyConduits.Material);
            if(move.Outside.Count!=0)throw new InvalidOperationException("An exact reservoir intake exceeded its actual capacity.");
            host.Store.Commit(move);Refresh(host);return true;
        }
        internal static float IntakeLimit(Storage storage,float request)
        {
            if(!Enabled)return request;var phase=ReservoirPhase(storage);if(phase==0)return request;
            var room=new PipeAmount(storage.capacityKg).Minus(BindEmpty(storage,phase).Store.Inventory.Total);
            if(room.Sign<=0)return 0;var limit=(float)Math.Min(request,room.Value);
            while(limit>0&&new PipeAmount(limit).Minus(room).Sign>0)
                limit=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(limit)-1);
            return limit;
        }
        internal static bool Dispense(ConduitDispenser dispenser,ConduitFlow flow,int cell,out PipeInventory parcel)
        {
            parcel=PipeInventory.Empty;if(!Enabled)return false;var phase=ReservoirPhase(dispenser.storage);if(phase==0)return false;
            if(!dispenser.IsConnected||!flow.HasConduit(cell))return true;
            if(dispenser.elementFilter!=null&&dispenser.elementFilter.Length!=0)
                throw new InvalidOperationException("A reservoir output unexpectedly acquired a constituent filter.");
            var host=BindEmpty(dispenser.storage,phase);var held=host.Store.Inventory;
            dispenser.empty=held.Count==0;if(held.Count==0)return true;
            if(phase!=(flow.conduitType==ConduitType.Gas?1:2))throw new InvalidOperationException("An exact reservoir output changed fluid phase.");
            var destination=ColonyConduits.ReadSynced(flow,cell);
            var capacity=ColonyConduits.PortCapacity(flow);
            var move=host.Store.PrepareWithdrawalTo(destination,double.MaxValue,capacity,ColonyConduits.Material);
            parcel=move.Parcel;dispenser.blocked=parcel.Count==0;if(parcel.Count==0)return true;
            ColonyConduits.ReceiveExact(flow,cell,parcel);host.Store.Commit(move);Refresh(host);return true;
        }
        private static void Refresh(Host host)
        {
            if(host.ProjectedRevision==host.Store.Revision)return;
            ++editing;
            try
            {
                var active=new HashSet<int>();
                foreach(var view in ColonyMachinePorts.NativeItems(host.Store.Inventory))
                {
                    active.Add(view.Part.Id);
                    if(!host.Projections.TryGetValue(view.Part.Id,out var projection))
                    {
                        // A trace smaller than binary32 has no mass carrier;
                        // it stays in Store.Inventory and travels on withdrawal.
                        if(!(view.Mass>0))continue;
                        var item=view.Element.IsLiquid?host.Storage.AddLiquid(view.Element.id,view.Mass,view.Temperature,view.Disease,view.Count,true,false):
                            host.Storage.AddGasChunk(view.Element.id,view.Mass,view.Temperature,view.Disease,view.Count,true,false);
                        projection=new Projection(item,view.Mass,view.Temperature);host.Projections.Add(view.Part.Id,projection);
                    }
                    else
                    {
                        TraceHeat("project-before",host,projection,view.Temperature);
                        projection.Item.SetMassTemperature(view.Mass,view.Temperature);
                        TraceHeat("project-after",host,projection,view.Temperature);
                    }
                    // Native disease is a view. The exact per-ID populations
                    // remain in Store.Inventory even when the view is empty.
                    var primary=projection.Item;
                    if(primary.DiseaseIdx!=view.Disease||primary.DiseaseCount!=view.Count)
                    {
                        if(primary.DiseaseCount>0)primary.ModifyDiseaseCount(-primary.DiseaseCount,"Mixed Atmospheres storage projection");
                        if(view.Count>0)primary.AddDisease(view.Disease,view.Count,"Mixed Atmospheres storage projection");
                    }
                }
                foreach(var pair in host.Projections)
                    if(!active.Contains(pair.Key))pair.Value.Item.Mass=0;
            }
            finally{--editing;}
            host.ProjectedRevision=host.Store.Revision;
            host.Storage.Trigger(-1697596308,null);host.Storage.OnStorageChange?.Invoke(null!);
        }
        private static (Host Host,Projection View)? ViewOf(PrimaryElement item)
        {
            if(!Enabled||item==null)return null;
            foreach(var host in Hosts.Values)foreach(var view in host.Projections.Values)
                if(ReferenceEquals(view.Item,item))return(host,view);
            return null;
        }
        internal static bool IsProjection(PrimaryElement item)=>ViewOf(item).HasValue;
        // The native registered f32 capacity and represented body temperature
        // define this heat work. Catalogue capacity and repeated UI getters do
        // not participate in the physical credit.
        private static void Credit(Host host,Projection view,float published)
        {
            if(!(published>0)||float.IsInfinity(published))throw new InvalidOperationException("An owned storage thermal projection published an invalid temperature.");
            TraceHeat("credit-before",host,view,published);
            if(view.Capacity>0&&published!=view.Temperature)
            {
                var work=PipeContactWork.IntoFluid(view.Capacity,view.Temperature,published).Negated();
                host.Store.AddHeat(work,ColonyConduits.Material);
            }
            view.Temperature=published;
            TraceHeat("credit-after",host,view,published);
        }
        internal static void Published()
        {
            if(!Enabled)return;
            foreach(var host in Hosts.Values)
            {
                foreach(var view in host.Projections.Values)
                {
                    var body=view.Item.sttOptimizationHook;
                    if(body==null||!Sim.IsValidHandle(body.SimHandle))continue;
                    if(view.Handle!=body.SimHandle)throw new InvalidOperationException("An owned storage thermal projection changed its native handle without registration.");
                    Credit(host,view,view.Item.Temperature);
                }
                Refresh(host);
            }
            ++HeatPublications;
        }
        internal static void Registering(SimTemperatureTransfer body,int handle)
        {
            var found=ViewOf(body.pe);if(!found.HasValue)return;++registering;
            var host=found.Value.Host;var view=found.Value.View;
            if(!Sim.IsValidHandle(handle))throw new InvalidOperationException("An owned storage thermal projection received an invalid native handle.");
            if(view.Handle!=-1)throw new InvalidOperationException("An owned storage thermal projection registered twice.");
            view.Handle=handle;view.Capacity=view.InitialCapacity;view.Temperature=view.InitialTemperature;
            // The registration publication can already include contact work;
            // read it before the native callback overwrites its output view.
            Credit(host,view,ReadTemperature(handle));
        }
        private static unsafe float ReadTemperature(int handle)=>Game.Instance.simData.elementChunks[Sim.GetHandleIndex(handle)].temperature;
        internal static void Registered(SimTemperatureTransfer body)
        {if(ViewOf(body.pe).HasValue&&registering>0)--registering;}
        internal static void SentData(int handle,float temperature,float capacity)
        {
            if(!Enabled)return;
            foreach(var host in Hosts.Values)foreach(var view in host.Projections.Values)
                if(view.Item.sttOptimizationHook!=null&&view.Item.sttOptimizationHook.SimHandle==handle)
                {
                    if(editing==0&&registering==0)throw new InvalidOperationException("An unsupported native writer tried to replace an exact storage thermal projection.");
                    if(capacity<0||float.IsNaN(capacity)||float.IsInfinity(capacity)||!(temperature>0)||float.IsInfinity(temperature))
                        throw new InvalidOperationException("Invalid exact storage thermal projection data.");
                    TraceHeat("sent-before",host,view,temperature,capacity);
                    view.Capacity=capacity;view.Temperature=temperature;
                    TraceHeat("sent-after",host,view,temperature,capacity);return;
                }
        }
        internal static void Reject(Storage storage,string operation)
        {if(editing==0&&Owns(storage))throw new InvalidOperationException("Unsupported native mutation of exact storage: "+operation);}
        internal static void Reject(PrimaryElement item,string operation)
        {if(editing==0&&ViewOf(item).HasValue)throw new InvalidOperationException("Unsupported native mutation of an exact storage projection: "+operation);}
        internal static bool Mass(Storage storage,out float mass)
        {mass=0;if(!Hosts.TryGetValue(storage,out var host))return false;mass=(float)host.Store.Inventory.Mass;return true;}
        internal static OwnedFluidStorageImage Capture()
        {
            foreach(var host in Hosts.Values)
            {
                if(!Address(host.Storage).Equals(host.Store.Address)||!host.Store.Audit())throw new InvalidOperationException("Exact storage identity or physical inventory audit failed.");
                // Biology continuation and projection rebind are separately
                // required before this prototype may write a live checkpoint.
            }
            return new OwnedFluidStorageImage(Hosts.Values.Select(host=>host.Store));
        }
        internal static void RequireQuiescent()
        {
            if(Enabled&&Hosts.Count>0)
                throw new NotSupportedException("Exact storage live checkpointing awaits the complete biology and native projection cold-rebind controls.");
        }
    }

    [HarmonyPatch]
    internal static class ColonyExactStorageMutationPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()
        {
            var names=new HashSet<string>{"Store","AddElement","AddGasChunk","AddLiquid","AddOre","AddToPrimaryElement","ConsumeAndGetDisease","ConsumeIgnoringDisease","ConsumeAllIgnoringDisease",
                "Remove","Drop","DropAll","DropSome","DropUnlessMatching","DropUnlessHasTag","DropHasTags","ForceStore","Transfer","TransferMass","TransferUnitMass","ClearItems"};
            return typeof(Storage).GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.DeclaredOnly).Where(method=>names.Contains(method.Name));
        }
        private static void Prefix(Storage __instance,object[] __args,MethodBase __originalMethod)
        {
            ColonyOwnedStorage.Reject(__instance,__originalMethod.Name);
            foreach(var argument in __args)
                if(argument is Storage storage)ColonyOwnedStorage.Reject(storage,__originalMethod.Name+" destination");
                else if(argument is GameObject item&&item!=null)
                {var pickup=item.GetComponent<Pickupable>();if(pickup!=null&&pickup.storage!=null)ColonyOwnedStorage.Reject(pickup.storage,__originalMethod.Name+" source");}
        }
    }
    [HarmonyPatch]
    internal static class ColonyExactStorageItemMutationPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()=>new[]{"SetMass","SetTemperature","SetMassTemperature","SetElement","ModifyDiseaseCount","AddDisease"}.Select(name=>AccessTools.Method(typeof(PrimaryElement),name));
        private static void Prefix(PrimaryElement __instance,MethodBase __originalMethod)=>ColonyOwnedStorage.Reject(__instance,__originalMethod.Name);
    }
    [HarmonyPatch(typeof(SimTemperatureTransfer),"OnSimRegistered")]
    internal static class ColonyExactStorageRegistrationPatch
    {
        private static void Prefix(SimTemperatureTransfer __instance,int __0)=>ColonyOwnedStorage.Registering(__instance,__0);
        private static Exception? Finalizer(SimTemperatureTransfer __instance,Exception? __exception){ColonyOwnedStorage.Registered(__instance);return __exception;}
    }
    [HarmonyPatch(typeof(SimMessages),nameof(SimMessages.SetElementChunkData))]
    internal static class ColonyExactStorageBodyDataPatch
    {private static void Prefix(int __0,float __1,float __2)=>ColonyOwnedStorage.SentData(__0,__1,__2);}
    [HarmonyPatch]
    internal static class ColonyExactStorageMassPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()=>new[]{nameof(Storage.MassStored),nameof(Storage.ExactMassStored)}.Select(name=>AccessTools.Method(typeof(Storage),name));
        private static bool Prefix(Storage __instance,ref float __result)
        {if(!ColonyOwnedStorage.Mass(__instance,out var mass))return true;__result=mass;return false;}
    }
    [HarmonyPatch(typeof(Storage),nameof(Storage.IsEmpty))]
    internal static class ColonyExactStorageEmptyPatch
    {private static bool Prefix(Storage __instance,ref bool __result){if(!ColonyOwnedStorage.Owns(__instance))return true;__result=ColonyOwnedStorage.Inventory(__instance).Count==0;return false;}}
    [HarmonyPatch]
    internal static class ColonyExactStoragePublicationPatch
    {
        private static MethodBase TargetMethod()=>AccessTools.Method(AccessTools.TypeByName("ConduitTemperatureManager"),"Sim200ms");
        [HarmonyPriority(Priority.First)]
        private static void Prefix(float __0){if(__0>0)ColonyOwnedStorage.Published();}
    }
    [HarmonyPatch(typeof(Game),"StepTheSim")]
    internal static class ColonyExactStorageZeroPublicationPatch
    {private static void Postfix(float __0){if(!(__0>0))ColonyOwnedStorage.Published();}}
}
