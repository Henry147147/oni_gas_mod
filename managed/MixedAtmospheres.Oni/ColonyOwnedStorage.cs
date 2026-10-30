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
            internal readonly GameObject Object;
            internal readonly float InitialCapacity,InitialTemperature;
            internal int Handle=-1;
            internal float Capacity,Temperature;
            internal bool Retiring;
            internal Projection(PrimaryElement item,float mass,float temperature)
            {Item=item;Object=item.gameObject;InitialCapacity=Capacity=mass*item.Element.specificHeatCapacity;InitialTemperature=Temperature=temperature;}
        }
        private sealed class Host
        {
            internal readonly Storage Storage;
            internal readonly OwnedFluidStore Store;
            internal ulong ProjectedRevision=ulong.MaxValue;
            internal readonly Dictionary<int,Projection> Projections=new Dictionary<int,Projection>();
            internal readonly List<Projection> Retiring=new List<Projection>();
            internal Host(Storage storage,OwnedFluidStore store){Storage=storage;Store=store;}
        }
        private static readonly Dictionary<Storage,Host> Hosts=new Dictionary<Storage,Host>();
        private static readonly RetiredFluidStorage?[] CompletedHistories=new RetiredFluidStorage?[3];
        [ThreadStatic] private static int editing,registering;
        internal static bool Enabled {get;private set;}
        internal static int Count=>Hosts.Count;
        internal static bool RetirementsPending=>Enabled&&Hosts.Values.Any(host=>host.Retiring.Count!=0);
        internal static long HeatPublications {get;private set;}
        internal static PipeInventory Inventory(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Store.Inventory:PipeInventory.Empty;
        internal static StorageJournalAmount Heat(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Store.Heat:default;
        internal static int RegisteredProjections(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Projections.Values.Count(view=>view.Handle!=-1):0;
        internal static int RetiringProjections(Storage storage)=>Hosts.TryGetValue(storage,out var host)?host.Retiring.Count:0;
        internal static bool Audit(Storage storage)=>Hosts.TryGetValue(storage,out var host)&&host.Store.Audit();
        private static bool ThermalProjectionsRegistered(Host host)=>
            host.Projections.Values.All(view=>
            {
                // Insulated stored items have a disabled thermal component:
                // no native contact or registration callback exists to wait for.
                var thermal=view.Item.GetComponent<SimTemperatureTransfer>();
                return OwnedFluidStore.NativeProjectionReady(view.InitialCapacity,thermal!=null&&thermal.enabled,view.Handle);
            });
        internal static bool ThermalProjectionsRegistered(Storage storage)=>
            Hosts.TryGetValue(storage,out var host)&&ThermalProjectionsRegistered(host);
        internal static object Diagnostics()=>new{Enabled,NativeHeatReceipts,NativeRetirements,
            Completed=CompletedHistories.Where(history=>history!=null).Select(history=>new{history!.Phase,AuditClosed=history.Audit(),
                HeatUnits=history.Heat.Units.ToString(),AdmittedSpecies=history.Admitted.Masses.Count,WithdrawnSpecies=history.Withdrawn.Masses.Count}).ToArray(),
            Stores=Hosts.Values.OrderBy(host=>host.Store.Address).Select(host=>new{
                host.Store.Address.Prefab,host.Store.Address.Ordinal,host.Store.Phase,Mass=host.Store.Inventory.Mass,
                AuditClosed=host.Store.Audit(),RegisteredProjections=host.Projections.Values.Count(view=>view.Handle!=-1),
                RetiringProjections=host.Retiring.Count,NativeItems=host.Storage.items.Count(item=>item!=null)
            }).ToArray()};
        internal static void StartControl()
        {
            if(!ColonyPipeRuntime.Active||!ColonyMachinePorts.Enabled)throw new InvalidOperationException("Exact component storage requires the mixed-pipe runtime.");
            RequireStorageHeat();
            Enabled=true;MixtureInspection.SetOwnedStorageReader(storage=>Owns(storage)?ColonyPipeInspection.Describe(Inventory(storage),"Stored mixture"):null);
        }
        internal static void Reset(){ClearStorageHeat();ResetBiology();Enabled=false;Hosts.Clear();Array.Clear(CompletedHistories,0,CompletedHistories.Length);editing=registering=0;HeatPublications=0;MixtureInspection.SetOwnedStorageReader(null);NativeMessageBoundary.ResetStoragePublication();}
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
            if(prefab==null||prefab.pendingDestruction||prefab.conflicted||!storage.isSpawned||storage.IsEndOfLife()||
                !ReferenceEquals(KPrefabIDTracker.Get().GetInstance(prefab.InstanceID),prefab))
                throw new InvalidOperationException($"Exact storage has no live, unique prefab owner: prefab {prefab?.InstanceID}, spawned {storage.isSpawned}, ending {storage.IsEndOfLife()}, destroying {prefab?.pendingDestruction}, conflicted {prefab?.conflicted}.");
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
            var prepared=PrepareAdmission(storage,parcel);if(prepared==null)return false;
            prepared.Commit();prepared.Publish();return true;
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
            TraceHeat("outlet-attempt",host);
            if(phase!=(flow.conduitType==ConduitType.Gas?1:2))throw new InvalidOperationException("An exact reservoir output changed fluid phase.");
            if(!TryBeginTransfer(host,out var lease))return true;
            using var transfer=lease;
            var destination=ColonyConduits.ReadSynced(flow,cell);
            var capacity=ColonyConduits.PortCapacity(flow);
            var move=host.Store.PrepareWithdrawalTo(destination,double.MaxValue,capacity,ColonyConduits.Material);
            parcel=move.Parcel;dispenser.blocked=parcel.Count==0;if(parcel.Count==0)return true;
            var receive=ColonyConduits.PrepareReceiveExact(flow,cell,parcel);
            PrepareNativeTransfer(host,move.After);receive.Validate();
            host.Store.Commit(move);nativeTransfer?.PhysicalCommit();receive.Commit();
            receive.Publish();Refresh(host,false);CompleteNativeTransfer(host);NotifyStorage(host);return true;
        }
        private static void Refresh(Host host,bool notify=true)
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
                    if(!active.Contains(pair.Key))
                    {
                        var item=pair.Value.Item;item.Mass=0;
                        if(item.DiseaseCount>0)item.ModifyDiseaseCount(-item.DiseaseCount,"Mixed Atmospheres exhausted storage projection");
                    }
            }
            finally{--editing;}
            host.ProjectedRevision=host.Store.Revision;
            if(notify)NotifyStorage(host);
        }
        private static (Host Host,Projection View)? ViewOf(PrimaryElement item)
        {
            if(!Enabled||item==null)return null;
            foreach(var host in Hosts.Values)foreach(var view in host.Projections.Values.Concat(host.Retiring))
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
            CreditNativeStorageHeat();
            foreach(var host in Hosts.Values)
            {
                foreach(var view in host.Projections.Values)
                {
                    var body=view.Item.sttOptimizationHook;
                    if(body==null||!Sim.IsValidHandle(body.SimHandle))continue;
                    if(view.Handle!=body.SimHandle)throw new InvalidOperationException("An owned storage thermal projection changed its native handle without registration.");
                }
                PublishThermalProjection(host);
                RetireExhaustedProjections(host);
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
            OniNativeColony.Check(RegisterStorageHeat(handle,view.Temperature,view.Capacity));
        }
        private static unsafe float ReadTemperature(int handle)=>Game.Instance.simData.elementChunks[Sim.GetHandleIndex(handle)].temperature;
        internal static void Registered(SimTemperatureTransfer body)
        {if(ViewOf(body.pe).HasValue&&registering>0)--registering;}
        private static long nativeDataDeclarations;
        internal static long NativeDataDeclarations=>System.Threading.Interlocked.Read(ref nativeDataDeclarations);
        internal static bool SentData(int handle,float temperature,float capacity)
        {
            System.Threading.Interlocked.Increment(ref nativeDataDeclarations);
            if(!Enabled)return true;
            foreach(var host in Hosts.Values)foreach(var view in host.Projections.Values.Concat(host.Retiring))
                if(view.Handle==handle)
                {
                    if(editing==0&&registering==0&&!(view.Retiring&&view.Capacity==0&&capacity==0))throw new InvalidOperationException("An unsupported native writer tried to replace an exact storage thermal projection.");
                    if(capacity<0||float.IsNaN(capacity)||float.IsInfinity(capacity)||!(temperature>0)||float.IsInfinity(temperature))
                        throw new InvalidOperationException("Invalid exact storage thermal projection data.");
                    TraceHeat("sent-before",host,view,temperature,capacity);
                    var suppressed=nativeTransfer!=null&&nativeTransfer.Suppress(handle,temperature,capacity);
                    if(nativeTransfer!=null&&!suppressed)throw new InvalidOperationException("A storage transfer tried to queue an unprepared native endpoint.");
                    if(!suppressed)OniNativeColony.Check(DeclareStorageHeatData(handle,temperature,capacity));
                    view.Capacity=capacity;view.Temperature=temperature;
                    TraceHeat("sent-after",host,view,temperature,capacity);return !suppressed;
                }
            return true;
        }
        // Pickupable removes exhausted UI/native carriers during cleanup. The
        // exact store retains any unrepresented trace; a zero view owns no mass
        // or thermal capacity. Keep its handle until native removal is proved
        // and all earlier represented heat is acknowledged.
        // Defer retirement until publication: a withdrawal followed by a new
        // admission in the same callback can reuse its carrier before it dies.
        private static void RetireExhaustedProjections(Host host)
        {
            foreach(var pair in host.Projections.Where(pair=>pair.Value.Handle!=-1&&pair.Value.Capacity==0&&pair.Value.Item.Mass==0).ToArray())
            {
                RetireProjection(host,pair.Key,pair.Value);
                Util.KDestroyGameObject(pair.Value.Object);
            }
        }
        private static void RetireProjection(Host host,int id,Projection view)
        {
            OniNativeColony.Check(DeclareStorageRemoval(view.Handle));
            view.Retiring=true;host.Projections.Remove(id);host.Retiring.Add(view);
        }
        internal static bool RetireEmptyProjection(Storage storage,GameObject item)
        {
            if(editing!=0||item==null||!Hosts.TryGetValue(storage,out var host))return false;
            var primary=item.GetComponent<PrimaryElement>();
            var pair=host.Projections.FirstOrDefault(p=>ReferenceEquals(p.Value.Item,primary));
            var view=pair.Value;
            if(view==null)return host.Retiring.Any(p=>ReferenceEquals(p.Item,primary));
            if(primary.Mass!=0||view.Capacity!=0||view.Handle==-1)return false;
            CreditNativeStorageHeat();
            RetireProjection(host,pair.Key,view);
            return true;
        }
        internal static bool IsRetiringEmptyProjection(Storage storage,GameObject item) =>
            item!=null&&Hosts.TryGetValue(storage,out var host)&&host.Retiring.Any(view=>ReferenceEquals(view.Object,item)&&view.Retiring&&view.Capacity==0&&view.Item.Mass==0);
        internal static void Reject(Storage storage,string operation)
        {
            if(editing!=0||!Hosts.TryGetValue(storage,out var host))return;
            var views=string.Join(", ",host.Projections.Values.Select(view=>$"{view.Item.ElementID} {view.Item.Mass:R}kg, capacity {view.Capacity:R}, handle {view.Handle}"));
            throw new InvalidOperationException($"Unsupported native mutation of exact storage: {operation}; exact inventory {host.Store.Inventory.Mass:R}kg; projections [{views}]");
        }
        internal static void Reject(PrimaryElement item,string operation)
        {if(editing==0&&ViewOf(item).HasValue)throw new InvalidOperationException("Unsupported native mutation of an exact storage projection: "+operation);}
        internal static bool Mass(Storage storage,out float mass)
        {mass=0;if(!Hosts.TryGetValue(storage,out var host))return false;mass=(float)host.Store.Inventory.Mass;return true;}
        internal static void RetireCompleted(Storage storage)
        {
            if(!Enabled||!Hosts.TryGetValue(storage,out var host))return;
            CreditNativeStorageHeat();
            if(host.Store.Inventory.Count!=0||host.Projections.Count!=0||host.Retiring.Count!=0||storage.items.Any(item=>item!=null)||!host.Store.Audit())
                throw new NotSupportedException("Exact reservoir cleanup requires complete discharge and native projection retirement; nonempty teardown needs an exact container adapter.");
            // Allocate and audit the merged journal before releasing the live
            // host. No prefab or native body identity survives in this history.
            var completed=CompletedHistories[host.Store.Phase];
            if(!host.Store.PristineEmpty)
            {var history=RetiredFluidStorage.From(host.Store);completed=completed==null?history:completed.Joined(history);}
            Hosts.Remove(storage);CompletedHistories[host.Store.Phase]=completed;
        }
        internal static OwnedFluidStorageImage Capture()
        {
            foreach(var host in Hosts.Values)
            {
                if(!Address(host.Storage).Equals(host.Store.Address)||!host.Store.Audit())throw new InvalidOperationException("Exact storage identity or physical inventory audit failed.");
                // Biology continuation and projection rebind are separately
                // required before this prototype may write a live checkpoint.
            }
            return new OwnedFluidStorageImage(Hosts.Values.Select(host=>host.Store),CompletedHistories.Where(history=>history!=null).Select(history=>history!));
        }
        internal static void RestorePristine(OwnedFluidStorageImage image)
        {
            if(!Enabled||Hosts.Count!=0||image.Retired.Length!=0||image.Stores.Any(store=>!store.PristineEmpty))
                throw new NotSupportedException("Exact storage restore currently binds only pristine empty reservoirs.");
            Restore(image);
        }
        // Cold or menu load, at the first positive-time barrier: every saved
        // store binds to its restored reservoir, and ONI's own restored native
        // items become that store's projections. A projection must be exactly
        // the binary32 view of the saved inventory (element, mass, temperature
        // and dominant culture bits); zero-mass views of withdrawn species are
        // kept bound. Any other native item refuses the load. A registered
        // thermal body joins the native heat receipt boundary at once.
        internal static void Restore(OwnedFluidStorageImage image)
        {
            if(!Enabled||Hosts.Count!=0||CompletedHistories.Any(history=>history!=null))throw new InvalidOperationException("Exact storage restore needs a fresh enabled control.");
            var prepared=new List<Host>();
            foreach(var store in image.Stores)
            {
                // Resolve the serialized identity, including an inactive
                // building. A scene-wide search also returns prefab assets
                // and omits inactive instances during loading.
                var prefab=KPrefabIDTracker.Get().GetInstance(store.Address.Prefab);
                if(prefab==null)throw new InvalidOperationException($"Saved exact reservoir {store.Address.Prefab} has no tracked native prefab.");
                var components=prefab.GetComponents<Storage>();
                if(store.Address.Ordinal>=components.Length)
                    throw new InvalidOperationException($"Saved exact reservoir {store.Address.Prefab} has no storage component {store.Address.Ordinal}.");
                var storage=components[store.Address.Ordinal];
                if(!Address(storage).Equals(store.Address)||ReservoirPhase(storage)!=store.Phase)
                    throw new InvalidOperationException($"Saved exact reservoir {store.Address.Prefab}/{store.Address.Ordinal} changed identity or fluid phase.");
                var host=new Host(storage,store){ProjectedRevision=store.Revision};
                BindRestoredProjections(host);
                prepared.Add(host);
            }
            foreach(var host in prepared)Hosts.Add(host.Storage,host);
            foreach(var history in image.Retired)CompletedHistories[history.Phase]=history;
            foreach(var host in prepared)foreach(var view in host.Projections.Values)RegisterRestoredHeat(host,view);
        }
        private static void BindRestoredProjections(Host host)
        {
            var name=$"{host.Store.Address.Prefab}/{host.Store.Address.Ordinal}";
            var items=host.Storage.items.Where(item=>item!=null).Select(item=>item.GetComponent<PrimaryElement>()).ToList();
            if(items.Any(item=>item==null))throw new InvalidOperationException($"Saved exact reservoir {name} holds a native item without a primary element.");
            if(host.Store.PristineEmpty)
            {
                if(items.Count!=0)throw new InvalidOperationException($"Saved empty exact reservoir {name} contains native items.");
                return;
            }
            static bool Same(float a,float b)=>BitConverter.SingleToInt32Bits(a)==BitConverter.SingleToInt32Bits(b);
            foreach(var view in ColonyMachinePorts.NativeItems(host.Store.Inventory))
            {
                if(!(view.Mass>0))continue; // carried only by the exact inventory
                var matches=items.Where(item=>item.ElementID==view.Element.id).ToList();
                if(matches.Count!=1)throw new InvalidOperationException($"Saved exact reservoir {name} has {matches.Count} native {view.Element.id} items for one projection.");
                var item=matches[0];
                if(!Same(item.Mass,view.Mass)||!Same(item.Temperature,view.Temperature)||item.DiseaseCount!=view.Count||(view.Count>0&&item.DiseaseIdx!=view.Disease))
                    throw new InvalidOperationException($"Saved exact reservoir {name}'s native {view.Element.id} item is not its inventory's projection: "+
                        $"{item.Mass:R} kg at {item.Temperature:R} K, germs {item.DiseaseIdx}/{item.DiseaseCount}; expected {view.Mass:R} kg at {view.Temperature:R} K, germs {view.Disease}/{view.Count}.");
                host.Projections.Add(view.Part.Id,new Projection(item,view.Mass,view.Temperature));items.Remove(item);
            }
            // A species withdrawn after its projection existed leaves a zero-mass view.
            foreach(var item in items)
            {
                var material=ColonyConduits.MaterialOf(item.ElementID);
                if(item.Mass!=0||host.Projections.ContainsKey(material.Id))
                    throw new InvalidOperationException($"Saved exact reservoir {name} holds an unaccounted native {item.ElementID} item ({item.Mass:R} kg).");
                host.Projections.Add(material.Id,new Projection(item,0,item.Temperature));
            }
        }
        private static void RegisterRestoredHeat(Host host,Projection view)
        {
            var body=view.Item.sttOptimizationHook;
            if(body==null||!Sim.IsValidHandle(body.SimHandle))return; // registers later through OnSimRegistered
            view.Handle=body.SimHandle;
            Credit(host,view,ReadTemperature(view.Handle));
            OniNativeColony.Check(RegisterStorageHeat(view.Handle,view.Temperature,view.Capacity));
        }
        // Save point: every reservoir holding native items is an exact store,
        // and each store's native items are exactly its current projections.
        internal static void RequireQuiescent()
        {
            if(!Enabled)return;
            CreditNativeStorageHeat();
            foreach(var host in Hosts.Values)
            {
                Refresh(host);
                if(host.Retiring.Count!=0)throw new InvalidOperationException("Exact storage has native projection retirements still in flight.");
                var bound=new HashSet<PrimaryElement>(host.Projections.Values.Select(view=>view.Item));
                if(host.Storage.items.Any(item=>item!=null&&!bound.Contains(item.GetComponent<PrimaryElement>())))
                    throw new NotSupportedException("An exact store holds a native item that is not its projection.");
            }
            foreach(var storage in UnityEngine.Object.FindObjectsByType<Storage>(FindObjectsInactive.Include,FindObjectsSortMode.None))
                if(storage.isSpawned&&!storage.IsEndOfLife()&&ReservoirPhase(storage)!=0&&!Hosts.ContainsKey(storage)&&storage.items.Any(item=>item!=null))
                    throw new NotSupportedException("An exact-storage checkpoint cannot omit a reservoir's native held items.");
        }
    }

    [HarmonyPatch]
    internal static class ColonyExactStorageCleanupPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()=>new[]{"OnQueueDestroyObject","OnCleanUp"}.Select(name=>AccessTools.Method(typeof(Storage),name));
        [HarmonyPriority(Priority.First)]
        private static void Prefix(Storage __instance)=>ColonyOwnedStorage.RetireCompleted(__instance);
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
            if(__originalMethod.Name=="Remove"&&__args.Length>0&&__args[0] is GameObject exhausted&&
                ColonyOwnedStorage.RetireEmptyProjection(__instance,exhausted))return;
            ColonyOwnedStorage.Reject(__instance,__originalMethod.Name);
            foreach(var argument in __args)
                if(argument is Storage storage)ColonyOwnedStorage.Reject(storage,__originalMethod.Name+" destination");
                else if(argument is GameObject item&&item!=null)
                {var pickup=item.GetComponent<Pickupable>();if(pickup!=null&&pickup.storage!=null)ColonyOwnedStorage.Reject(pickup.storage,__originalMethod.Name+" source");}
        }
    }
    // Removing an exhausted display carrier must not transfer the building's
    // disease into it. All fluid cultures already travelled with the exact
    // withdrawal, independently of the discarded native zero-mass view.
    [HarmonyPatch(typeof(Storage),"TransferDiseaseWithObject")]
    internal static class ColonyExactStorageRetiringDiseasePatch
    {
        private static bool Prefix(Storage __instance,GameObject __0) =>
            !ColonyOwnedStorage.IsRetiringEmptyProjection(__instance,__0);
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
    {private static bool Prefix(int __0,float __1,float __2)=>ColonyOwnedStorage.SentData(__0,__1,__2);}
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
        private static void Prefix(float __0)
        {
            if(!(__0>0))return;
            if(ColonyRuntimeValidation.Settings!=null)ColonyScenarios.BeforeStorageHeatPublication();
            ColonyOwnedStorage.Published();
        }
    }
    [HarmonyPatch(typeof(Game),"StepTheSim")]
    internal static class ColonyExactStorageZeroPublicationPatch
    {private static void Postfix(float __0){if(!(__0>0))ColonyOwnedStorage.Published();}}
}
