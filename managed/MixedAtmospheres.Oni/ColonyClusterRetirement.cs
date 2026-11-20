using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The game recovers resources one Unity frame after dismantling interior
    // buildings. Let the game's world list shed destroyed Unity objects while
    // reserving their identities and grid rectangles until evacuation finishes.
    //
    // Since build 763796 the game queues a PendingRocketInteriorDestruction
    // for each dismantled interior, with a save blocker, and one UI frame
    // later DeleteWorldObjects() handles every queued record at once:
    // recover the resources, destroy the world objects, free the grid slot,
    // remove the blocker. For a world the owner is retiring the adapter keeps
    // the record back from that call, runs the game's own recovery at once
    // (which exports the complete mixture through the patches below) and does
    // the rest itself a frame later, once the export is in both grids: close
    // the cells, destroy the objects, free the slot, remove the blocker. The
    // record stays in the game's list meanwhile, so the grid allocator and
    // the save blocker keep treating the slot as pending.
    internal static class ColonyClusterRetirement
    {
        private sealed class Retirement
        {
            internal readonly WorldContainer World;
            internal readonly ColonyWorldRegion Region;
            internal bool Exported,Unregistered,DeleteRequested,InDelete,Held;
            internal int RequestedUnityFrame;
            internal PendingRocketInteriorDestruction Record;
            internal Retirement(WorldContainer world)
            {World=world;Region=new ColonyWorldRegion(world.id,world.WorldOffset.x,world.WorldOffset.y,world.WorldSize.x,world.WorldSize.y,true);}
            internal uint Origin=>(uint)Grid.XYToCell(Region.X,Region.Y);
        }
        private static readonly Dictionary<int,Retirement> Worlds=new Dictionary<int,Retirement>();
        internal static bool Pending=>Worlds.Count!=0;
        internal static int Completed {get;private set;}
        internal static void Reset(){Worlds.Clear();Completed=0;}
        internal static bool Begin(int id)
        {
            if(!ColonyClusterWorlds.Enabled||!ColonyOwner.Active)return true;
            var world=ClusterManager.Instance.GetWorld(id);
            if(world==null||!world.IsModuleInterior)return true;
            if(Worlds.ContainsKey(id))return false;
            if(OniNativeColony.RetirementVersion()!=1)throw new NotSupportedException("Rocket retirement requires a matching native/managed bundle.");
            Worlds.Add(id,new Retirement(world));return true;
        }
        internal static IEnumerable<ColonyWorldRegion> Regions=>Worlds.Values.Select(state=>state.Region).ToArray();
        internal static bool CanUnregister(WorldContainer world)=>!Worlds.TryGetValue(world.id,out var state)||!state.Unregistered;
        internal static void Unregistered(WorldContainer world)
        {if(Worlds.TryGetValue(world.id,out var state))state.Unregistered=true;}
        internal static int AvailableWorldId(int proposed)=>!Pending?proposed:
            ColonyWorldLayout.AvailableWorldId(proposed,ClusterManager.Instance.WorldContainers.Select(world=>world.id),Worlds.Keys);
        // Before the game's DeleteWorldObjects(): take the records of retiring
        // worlds out of its list. A record seen for the first time has its
        // resources recovered now, exactly as the game would.
        private static readonly List<PendingRocketInteriorDestruction> held=new List<PendingRocketInteriorDestruction>();
        internal static void TakePending(ClusterManager cluster)
        {
            held.Clear();
            var pending=cluster.PendingRocketInteriorDestructions;
            for(var i=0;i<pending.Count;)
            {
                var record=pending[i];var world=record.container;
                if(ReferenceEquals(world,null)||!Worlds.TryGetValue(world.id,out var state)||!ReferenceEquals(state.World,world)){++i;continue;}
                pending.RemoveAt(i);held.Add(record);
                if(state.Held)continue;
                state.Held=true;state.Record=record;
                try
                {
                    if(!record.inFlight)world.TransferResourcesToParentWorld(record.rocketModuleWorldPos+new Vector3(0f,.5f,0f),record.noRefundTiles);
                    else world.TransferResourcesToDebris(record.clusterLocation,record.noRefundTiles,record.elementID);
                }
                catch
                {
                    // As the game does when its recovery throws: the blocker goes, the slot stays.
                    held.Remove(record);state.Held=false;cluster.RemoveSaveBlocker(record.saveBlockerID);throw;
                }
                state.DeleteRequested=true;state.RequestedUnityFrame=Time.frameCount;
            }
        }
        // After it: the kept records go back, so that the game's grid allocator
        // and save blocker still count them.
        internal static void RestorePending(ClusterManager cluster)
        {
            if(held.Count==0)return;
            cluster.PendingRocketInteriorDestructions.AddRange(held);held.Clear();
        }
        internal static void PumpDeletes()
        {
            if(!ColonyOwner.Active||ColonyOwner.Status.State!=1||!ColonySaveBarrier.CanRunLifecycle)return;
            foreach(var state in Worlds.Values.ToArray())
            {
                if(!state.Held||!state.DeleteRequested||!state.Exported||Time.frameCount<=state.RequestedUnityFrame)continue;
                state.DeleteRequested=false;
                var record=state.Record;var cluster=ClusterManager.Instance;var world=state.World;
                try
                {
                    if(!BeforeDelete(world))throw new InvalidOperationException("Rocket grid cleanup lost its retirement transaction.");
                    // The rest of the game's DeleteWorldObjects() for this record.
                    cluster.PendingRocketInteriorDestructions.RemoveAll(other=>other.saveBlockerID==record.saveBlockerID);
                    if(!record.inventory.IsNullOrDestroyed())UnityEngine.Object.Destroy(record.inventory);
                    UnityEngine.Object.Destroy(world);
                    Grid.FreeGridSpace(record.size,record.position);
                    AfterDelete(world);
                }
                catch(Exception error){ColonyFault.Raise("deferred rocket grid cleanup",error);return;}
                finally{cluster.RemoveSaveBlocker(record.saveBlockerID);}
            }
        }
        private static Retirement State(WorldContainer world)
        {
            if(!Worlds.TryGetValue(world.id,out var state)||!ReferenceEquals(state.World,world))
                throw new InvalidOperationException("Rocket resource recovery has no matching retirement transaction.");
            return state;
        }
        private static ulong[] Export(Retirement state)
        {
            if(state.Exported)throw new InvalidOperationException("Rocket resources were recovered more than once.");
            _=ColonySaveBarrier.FlushInputs();
            var before=OniNativeColony.Status.Empty;OniNativeColony.Check(OniNativeColony.CopyStatus(ref before));
            OniNativeColony.Check(OniNativeColony.PrepareRetirement(state.Origin,(uint)state.Region.Width,(uint)state.Region.Height,out var token,out var count));
            try
            {
                if(count>16*1024*1024)throw new InvalidOperationException("Rocket retirement inventory exceeded its bounded wire.");
                var wire=new byte[count];OniNativeColony.Check(OniNativeColony.CopyRetirement(token,wire,count,out var copied));
                if(copied!=count)throw new InvalidOperationException("Rocket retirement inventory changed while copying.");
                var inventory=ColonyRetirementInventory.Read(wire,before.Session,before.CompletedFrames,state.Origin,(uint)state.Region.Width,(uint)state.Region.Height,
                    ColonyConduits.MaterialPhase,ColonyConduits.Material);
                var ids=ColonyPipePorts.AdmitRetirement(inventory,(int)state.Origin,()=>OniNativeColony.Check(OniNativeColony.CommitRetirement(token)));
                state.Exported=true;
                _=ColonySaveBarrier.FlushInputs(); // project empty cells into native AND managed grids
                return ids.Concat(ColonyPipeTeardown.DetachWorld(state.Region)).Where(id=>id!=0).ToArray();
            }
            finally{OniNativeColony.Check(OniNativeColony.CancelRetirement(token));}
        }
        internal static void ToParent(WorldContainer world,Vector3 destination)
        {
            var state=State(world);
            foreach(var id in Export(state))ColonyMachinePorts.Chunks(ColonyPipePorts.TakeBuffer(id),destination);
            ColonySavedParticles.RelocateWorld(state.Region,destination);
        }
        private static Storage Debris(WorldContainer world,SimHashes material)
            =>(Storage)AccessTools.Method(typeof(WorldContainer),"SpawnRocketDebris").Invoke(world,new object[]{world.name,material});
        internal static void ToDebris(WorldContainer world,ref List<Storage> stores,SimHashes material)
        {
            var state=State(world);stores??=new List<Storage>();
            foreach(var id in Export(state))
            {
                var remaining=ColonyPipePorts.TakeBuffer(id);
                while(remaining.Count>0)
                {
                    var store=stores.LastOrDefault(s=>s.RemainingCapacity()>0);
                    if(store==null){store=Debris(world,material);stores.Add(store);}
                    var split=remaining.Split(Math.Min(remaining.Mass,store.RemainingCapacity()),ColonyConduits.Material);
                    if(split.Parcel.Count==0)throw new InvalidOperationException("Rocket debris refused its exact parcel capacity.");
                    ColonyMachinePorts.Deliver(store,split.Parcel,false);remaining=split.Remaining;
                }
            }
            var recovered=new List<Storage>();
            ColonySavedParticles.RecoverWorld(state.Region,(mass,temperature)=>
            {
                // These particles have not entered the fluid or pipe ledger.
                // Keep their original f32 mass/temperature in a fresh native
                // debris item, avoiding a second debit or a merged f32 packet.
                var store=Debris(world,material);store.AddGasChunk(SimHashes.CarbonDioxide,mass,temperature,byte.MaxValue,0,false,false);recovered.Add(store);
            });
            stores.AddRange(recovered);
        }
        internal static bool BeforeDelete(WorldContainer world)
        {
            if(!Worlds.TryGetValue(world.id,out var state))return false;
            if(!state.Exported)throw new InvalidOperationException("The game freed a rocket before complete-mixture recovery.");
            state.InDelete=true;
            _=ColonySaveBarrier.FlushInputs();
            if(ColonyPipeTeardown.HasWorldSpill(state.Region))
                throw new InvalidOperationException("Rocket cleanup produced an unexpected late fluid spill.");
            var closing=new List<OniNativeColony.Terrain>();
            for(var y=state.Region.Y;y<state.Region.Y+state.Region.Height;++y)
                for(var x=state.Region.X;x<state.Region.X+state.Region.Width;++x)
                    closing.Add(new OniNativeColony.Terrain{PublicCell=(uint)Grid.XYToCell(x,y),Kind=OniNativeColony.Close});
            OniNativeColony.Check(OniNativeColony.QueueTerrain(closing.ToArray(),(uint)closing.Count));
            foreach(var edit in closing)ColonyRouting.Queued((int)edit.PublicCell,edit.Kind);
            _=ColonySaveBarrier.FlushInputs();return true;
        }
        internal static void AfterDelete(WorldContainer world)
        {
            var state=State(world);_=ColonySaveBarrier.FlushInputs();
            OniNativeColony.Check(OniNativeColony.FinishRetirement(state.Origin,(uint)state.Region.Width,(uint)state.Region.Height));
            ColonyClusterWorlds.Removed(state.Region);
            if(!state.Unregistered)
                AccessTools.Method(typeof(ClusterManager),"UnregisterWorldContainer").Invoke(ClusterManager.Instance,new object[]{world});
            Worlds.Remove(world.id);++Completed;
            Debug.Log($"[Mixed Atmospheres] Retired rocket world {world.id}: complete mixture recovered; native grid slot released.");
        }
    }
    [HarmonyPatch(typeof(ClusterManager),"DestoryRocketInteriorWorld")]
    internal static class ColonyRocketRetirementPatch
    {
        private static bool Prefix(int __0)
        {try{return ColonyClusterRetirement.Begin(__0);}catch(Exception error){ColonyFault.Raise("rocket retirement",error);return false;}}
    }
    [HarmonyPatch(typeof(WorldContainer),"TransferLiquidsSolidsAndGases")]
    internal static class ColonyRocketResourceRecoveryPatch
    {
        private static bool Prefix(WorldContainer __instance,Vector3 __0)
        {
            if(!ColonyClusterWorlds.Enabled||!__instance.IsModuleInterior)return true;
            try{ColonyClusterRetirement.ToParent(__instance,__0);return true;}
            catch(Exception error){ColonyFault.Raise("rocket fluid recovery",error);return false;}
        }
    }
    [HarmonyPatch(typeof(WorldContainer),"TransferLiquidsSolidsAndGasesToDebris")]
    internal static class ColonyRocketDebrisRecoveryPatch
    {
        private static bool Prefix(WorldContainer __instance,ref List<Storage> __0,SimHashes __2)
        {
            if(!ColonyClusterWorlds.Enabled||!__instance.IsModuleInterior)return true;
            try{ColonyClusterRetirement.ToDebris(__instance,ref __0,__2);return true;}
            catch(Exception error){ColonyFault.Raise("rocket debris recovery",error);return false;}
        }
    }
    [HarmonyPatch(typeof(ClusterManager),"DeleteWorldObjects")]
    internal static class ColonyRocketDeletePatch
    {
        private static void Prefix(ClusterManager __instance)
        {
            if(!ColonyClusterWorlds.Enabled)return;
            try{ColonyClusterRetirement.TakePending(__instance);}
            catch(Exception error){ColonyFault.Raise("rocket fluid recovery",error);}
        }
        // A finalizer, so that the records return even when the game's own
        // handling of another record throws.
        private static void Finalizer(ClusterManager __instance)=>ColonyClusterRetirement.RestorePending(__instance);
    }
}
