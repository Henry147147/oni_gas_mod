using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The game recovers resources one Unity frame after dismantling interior
    // buildings. Hold its early unregister until that recovery and native grid
    // cleanup finish, so the allocator cannot reuse a still-live rectangle.
    internal static class ColonyClusterRetirement
    {
        private sealed class Retirement
        {
            internal readonly WorldContainer World;
            internal readonly ColonyWorldRegion Region;
            internal bool Exported,Releasing,DeleteRequested,AllowDelete,InDelete;
            internal int RequestedUnityFrame;
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
        internal static bool HoldUnregister(WorldContainer world)=>Worlds.TryGetValue(world.id,out var state)&&!state.Releasing;
        internal static bool DeferDelete(WorldContainer world)
        {
            if(!Worlds.TryGetValue(world.id,out var state))return false;
            if(state.InDelete)return true;
            if(state.AllowDelete)return false;
            state.DeleteRequested=true;state.RequestedUnityFrame=Time.frameCount;return true;
        }
        internal static void PumpDeletes()
        {
            if(!ColonyOwner.Active||ColonyOwner.Status.State!=1||!ColonySaveBarrier.CanRunLifecycle)return;
            foreach(var state in Worlds.Values.ToArray())
            {
                if(!state.DeleteRequested||!state.Exported||Time.frameCount<=state.RequestedUnityFrame)continue;
                state.DeleteRequested=false;state.AllowDelete=true;
                try{AccessTools.Method(typeof(ClusterManager),"DeleteWorldObjects").Invoke(ClusterManager.Instance,new object[]{state.World});}
                catch(Exception error){ColonyFault.Raise("deferred rocket grid cleanup",error);return;}
                finally{state.AllowDelete=false;}
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
            _=ColonySaveBarrier.FlushInputs();return true;
        }
        internal static void AfterDelete(WorldContainer world)
        {
            var state=State(world);_=ColonySaveBarrier.FlushInputs();
            OniNativeColony.Check(OniNativeColony.FinishRetirement(state.Origin,(uint)state.Region.Width,(uint)state.Region.Height));
            ColonyClusterWorlds.Removed(state.Region);state.Releasing=true;
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
        private static bool Prefix(WorldContainer __0,out bool __state)
        {
            __state=false;if(!ColonyClusterWorlds.Enabled)return true;
            try
            {
                if(ColonyClusterRetirement.DeferDelete(__0))return false;
                __state=ColonyClusterRetirement.BeforeDelete(__0);return true;
            }
            catch(Exception error){ColonyFault.Raise("rocket grid cleanup",error);return false;}
        }
        private static void Postfix(WorldContainer __0,bool __state)
        {if(__state)try{ColonyClusterRetirement.AfterDelete(__0);}catch(Exception error){ColonyFault.Raise("rocket grid cleanup",error);}}
    }
}
