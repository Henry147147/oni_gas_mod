using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The game allocates rocket interiors inside its fixed managed grid.
    // A template can leave vacuum cells behind: native fluid capture alone
    // cannot discover those, so admit them from a completed native snapshot.
    // This path is still confined to the explicit Spaced Out development run.
    internal static unsafe class ColonyClusterWorlds
    {
        private static ColonyWorldLayout? layout;
        private static int[] worldIds=Array.Empty<int>();
        private static bool dirty;
        internal static bool Enabled=>layout!=null;
        internal static int RegisteredRocketWorlds {get;private set;}
        internal static int QueuedOpenCells {get;private set;}
        internal static void Reset()
        {layout=null;worldIds=Array.Empty<int>();dirty=false;RegisteredRocketWorlds=QueuedOpenCells=0;}
        internal static void Initialize(ColonyWorldLayout? initial)
        {
            Reset();layout=initial;
            if(initial!=null)worldIds=initial.CellWorldIds();
        }
        internal static void Changed(){if(layout!=null)dirty=true;}
        internal static bool Contains(int cell)=>layout==null||cell>=0&&cell<worldIds.Length&&
            worldIds[cell]>=0&&Grid.WorldIdx[cell]==worldIds[cell];
        internal static void Synchronize(Sim.GameDataUpdate* source)
        {
            if(layout==null||!dirty||source->numFramesProcessed<=0)return;
            var next=ColonyOwner.CaptureWorldLayout();var change=next.ChangesFrom(layout);
            // Until evacuation has its own complete-mixture transaction, do
            // not silently abandon an owner-held world or reuse its cells.
            if(change.Removed.Length!=0)
                throw new NotSupportedException("Rocket world removal requires conservative fluid evacuation.");
            if(change.Added.Any(w=>!w.RocketInterior))
                throw new NotSupportedException("A new asteroid requires its own complete inventory admission.");
            var openings=new List<OniNativeColony.Terrain>();
            foreach(var world in change.Added)
                for(var y=world.Y;y<world.Y+world.Height;++y)
                    for(var x=world.X;x<world.X+world.Width;++x)
                    {
                        var cell=x+y*next.GridWidth;
                        if(Grid.WorldIdx[cell]!=world.Id||ElementLoader.elements[source->elementIdx[cell]].IsSolid)continue;
                        openings.Add(new OniNativeColony.Terrain{PublicCell=(uint)cell,Kind=OniNativeColony.Open});
                    }
            if(openings.Count>0)
                OniNativeColony.Check(OniNativeColony.QueueTerrain(openings.ToArray(),(uint)openings.Count));
            // The already registered owner's masses, energy, germs and clock
            // are untouched. Only the new cells queue the existing terrain
            // operation; native template sources use the ordinary mass route.
            worldIds=next.CellWorldIds();layout=next;dirty=false;
            RegisteredRocketWorlds+=change.Added.Length;QueuedOpenCells+=openings.Count;
            foreach(var world in change.Added)
                Debug.Log($"[Mixed Atmospheres] Registered rocket world {world.Id}: {world.X},{world.Y} {world.Width}x{world.Height}; queued cell ownership.");
        }
        internal static object Report()=>new{Experimental=layout!=null,Worlds=layout?.Worlds.Count??0,RegisteredRocketWorlds,QueuedOpenCells,PendingLayout=dirty};
    }
    [HarmonyPatch(typeof(WorldContainer),"PlaceInteriorTemplate")]
    internal static class ColonyRocketTemplatePatch
    {
        private static void Prefix(ref System.Action __1)
        {
            if(!ColonyClusterWorlds.Enabled)return;
            var completed=__1;
            __1=()=>{ColonyClusterWorlds.Changed();completed?.Invoke();};
        }
    }
    [HarmonyPatch(typeof(ClusterManager),"UnregisterWorldContainer")]
    internal static class ColonyRocketUnregisterPatch
    {private static void Postfix()=>ColonyClusterWorlds.Changed();}
}
