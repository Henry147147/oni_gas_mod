#if !ONI_DIRECT_ROCKET_DELETE
using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyClusterRetirement
    {
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
#endif
