using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyOwnedStorage
    {
        [ThreadStatic] private static NativeTransfer? nativeTransfer;
        private sealed class NativeTransfer : IDisposable
        {
            internal readonly Host Host;
            private readonly ulong token;
            private StorageThermalTargets? targets;
            private bool committed,disposed;
            internal NativeTransfer(Host host,ulong token){Host=host;this.token=token;}
            internal void Prepare(PipeInventory after)
            {
                if(disposed||targets!=null)throw new InvalidOperationException("A storage transfer prepared its thermal endpoints twice.");
                var views=ColonyMachinePorts.NativeItems(after).ToDictionary(view=>view.Part.Id);
                var endpoints=new List<StorageThermalEndpoint>();var before=new List<StorageThermalEndpoint>();var published=new List<StorageThermalEndpoint>();
                foreach(var pair in Host.Projections)
                {
                    var projection=pair.Value;if(projection.Handle==-1)continue;
                    var present=views.TryGetValue(pair.Key,out var view);
                    before.Add(new StorageThermalEndpoint(projection.Handle,projection.Item.Temperature,
                        projection.Item.Mass*projection.Item.Element.specificHeatCapacity));
                    published.Add(new StorageThermalEndpoint(projection.Handle,ReadTemperature(projection.Handle),projection.Capacity));
                    endpoints.Add(new StorageThermalEndpoint(projection.Handle,
                        present?view.Temperature:projection.Item.Temperature,
                        present?view.Mass*view.Element.specificHeatCapacity:0f));
                }
                var batch=endpoints.ToArray();var prepared=new StorageThermalTargets(batch,before.ToArray(),published.ToArray());
                OniNativeColony.Check(ReplaceStorageHeat(token,batch,(uint)batch.Length));
                targets=prepared;
            }
            internal bool Suppress(int handle,float temperature,float capacity)=>
                targets!=null&&targets.Transition(handle,temperature,capacity);
            internal void ValidateProjection()
            {
                if(targets==null)throw new InvalidOperationException("An exact storage transfer has no prepared native projection.");
                foreach(var view in Host.Projections.Values)
                    if(view.Handle!=-1&&!targets.Matches(view.Handle,view.Temperature,view.Capacity))
                        throw new InvalidOperationException("A storage transfer acquired an unprepared registered projection.");
            }
            internal void PhysicalCommit()=>committed=true;
            public void Dispose()
            {
                if(disposed)return;
                OniNativeColony.Check(EndStorageHeat(token,committed?1U:0U));
                disposed=true;if(ReferenceEquals(nativeTransfer,this))nativeTransfer=null;
            }
        }
        private static bool TryBeginTransfer(Host host,out NativeTransfer? transfer)
        {
            transfer=null;
            if(nativeTransfer!=null)throw new InvalidOperationException("Exact storage transfers cannot nest across native thermal leases.");
            if(!ThermalProjectionsRegistered(host)){TraceHeat("transfer-wait-registration",host);return false;}
            if(!host.Projections.Values.Any(view=>view.Handle!=-1))
            {CreditNativeStorageHeat();return true;}
            ulong token=0;var result=BeginStorageHeat(ref token);
            if(result==3){TraceHeat("transfer-wait-unposted-data",host);return false;} // natural retry after native publication
            OniNativeColony.Check(result);
            try
            {
                CreditNativeStorageHeat();
                transfer=new NativeTransfer(host,token);nativeTransfer=transfer;return true;
            }
            catch{OniNativeColony.Check(EndStorageHeat(token,0));throw;}
        }
        // Acquire before taking any pipe fluid. A deferred admission leaves
        // both inventories untouched and is retried by the normal updater.
        internal static bool TryBeginAdmission(Storage storage,out IDisposable? transfer)
        {
            transfer=null;if(!Enabled)return true;var phase=ReservoirPhase(storage);if(phase==0)return true;
            var host=BindEmpty(storage,phase);
            if(!TryBeginTransfer(host,out var lease))return false;
            transfer=lease;return true;
        }
        internal static bool TransfersReady(Storage storage)
        {
            if(!Enabled||!Hosts.TryGetValue(storage,out var host)||!ThermalProjectionsRegistered(host))return false;
            if(!TryBeginTransfer(host,out var transfer))return false;
            transfer?.Dispose();return true;
        }
        internal sealed class PreparedAdmission
        {
            private readonly Host host;
            private readonly OwnedFluidStore.Move move;
            private bool committed,published;
            private PreparedAdmission(Host host,OwnedFluidStore.Move move){this.host=host;this.move=move;}
            internal static PreparedAdmission? Create(Storage storage,PipeInventory parcel)
            {
                if(!Enabled)return null;var phase=ReservoirPhase(storage);if(phase==0)return null;
                foreach(var part in parcel.Parts)
                    if(ColonyConduits.MaterialPhase(part.Id)!=phase)throw new InvalidOperationException("A reservoir received a foreign fluid phase.");
                CreditNativeStorageHeat();var host=BindEmpty(storage,phase);
                if(host.Projections.Values.Any(view=>view.Handle!=-1)&&
                    (nativeTransfer==null||!ReferenceEquals(nativeTransfer.Host,host)))
                    throw new InvalidOperationException("A registered storage intake requires its native thermal lease before preparation.");
                var move=host.Store.PrepareAdmission(parcel,storage.capacityKg,ColonyConduits.Material);
                if(move.Outside.Count!=0)throw new InvalidOperationException("An exact reservoir intake exceeded its actual capacity.");
                // Allocate and validate the complete native batch before any
                // fluid leaves the pipe. A failed replace rolls back the lease.
                var prepared=new PreparedAdmission(host,move);PrepareNativeTransfer(host,move.After);return prepared;
            }
            private void Validate()
            {
                if(committed)throw new InvalidOperationException("A storage admission committed twice.");
                host.Store.Validate(move);
            }
            internal void Commit()
            {
                Validate();host.Store.Commit(move);nativeTransfer?.PhysicalCommit();committed=true;
            }
            internal void Commit(ColonyConduits.PreparedPortChange source)
            {
                Validate();source.Validate();Commit();source.Commit();
            }
            internal void Publish()
            {
                if(!committed||published)throw new InvalidOperationException("An uncommitted or already published storage admission cannot publish.");
                Refresh(host,false);CompleteNativeTransfer(host);published=true;NotifyStorage(host);
            }
        }
        internal static PreparedAdmission? PrepareAdmission(Storage storage,PipeInventory parcel)=>PreparedAdmission.Create(storage,parcel);
        private static void PrepareNativeTransfer(Host host,PipeInventory after)
        {
            if(nativeTransfer==null)return;
            if(!ReferenceEquals(nativeTransfer.Host,host))throw new InvalidOperationException("A storage transfer changed its thermal owner.");
            nativeTransfer.Prepare(after);
        }
        private static void CompleteNativeTransfer(Host host)
        {
            var transfer=nativeTransfer;
            if(transfer==null)return;
            if(!ReferenceEquals(transfer.Host,host))throw new InvalidOperationException("A storage transfer completed a foreign thermal owner.");
            transfer.ValidateProjection();
            transfer.Dispose();
        }
        private static void PublishThermalProjection(Host host)
        {
            if(host.ProjectedRevision==host.Store.Revision)return;
            if(!TryBeginTransfer(host,out var lease))return;
            using var transfer=lease;
            PrepareNativeTransfer(host,host.Store.Inventory);
            // The physical heat credit is already committed. Install its
            // projection under the same lease instead of queuing absolute
            // resets that would make every later scheduled outlet busy.
            nativeTransfer?.PhysicalCommit();Refresh(host,false);
            CompleteNativeTransfer(host);NotifyStorage(host);
        }
        private static void NotifyStorage(Host host)
        {host.Storage.Trigger(-1697596308,null);host.Storage.OnStorageChange?.Invoke(null!);}
    }
}
