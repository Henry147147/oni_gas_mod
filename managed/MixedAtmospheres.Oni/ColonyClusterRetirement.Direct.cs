#if ONI_DIRECT_ROCKET_DELETE
using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyClusterRetirement
    {
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
#endif
