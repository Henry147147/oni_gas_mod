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
        {layout=null;worldIds=Array.Empty<int>();dirty=false;RegisteredRocketWorlds=QueuedOpenCells=0;ColonyClusterRetirement.Reset();}
        internal static void Initialize(ColonyWorldLayout? initial)
        {
            Reset();layout=initial;
            if(initial!=null)worldIds=initial.CellWorldIds();
        }
        internal static void Changed(){if(layout!=null)dirty=true;}
        internal static void Removed(ColonyWorldRegion world)
        {
            if(layout==null||!layout.Worlds.Any(w=>w.SameRegion(world)))throw new InvalidOperationException("The retiring rocket layout changed.");
            layout=new ColonyWorldLayout(layout.GridWidth,layout.GridHeight,layout.Worlds.Where(w=>w.Id!=world.Id));
            worldIds=layout.CellWorldIds();dirty=true;
        }
        internal static void RequireCapability()
        {
            try{if(OniNativeColony.ClusterWorldVersion()==1&&OniNativeColony.InputFlushVersion()==1&&OniNativeColony.RetirementVersion()==1)return;}
            catch(EntryPointNotFoundException){}
            throw new NotSupportedException("Dynamic cluster worlds require a matching native/managed bundle.");
        }
        internal static void BeforeTemplate(WorldContainer world)
        {
            if(layout==null||layout.Worlds.Any(w=>w.Id==world.id))return;
            var region=new ColonyWorldRegion(world.id,world.WorldOffset.x,world.WorldOffset.y,
                world.WorldSize.x,world.WorldSize.y,world.IsModuleInterior);
            if(!region.RocketInterior)throw new NotSupportedException("Only a rocket interior can be allocated after cluster import.");
            _=new ColonyWorldLayout(layout.GridWidth,layout.GridHeight,layout.Worlds.Concat(new[]{region}));
            var cells=new uint[checked(region.Width*region.Height)];var i=0;
            for(var y=region.Y;y<region.Y+region.Height;++y)
                for(var x=region.X;x<region.X+region.Width;++x)cells[i++]=(uint)(x+y*layout.GridWidth);
            // A reused grid slot must not inherit an earlier rocket's space
            // sink while native template sources are still arriving.
            OniNativeColony.Check(OniNativeColony.SetSpaceCells(cells,(uint)cells.Length,0));
        }
        internal static System.Action? AfterTemplate(System.Action? completed)=>layout==null?completed:
            ()=>{Changed();completed?.Invoke();};
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
            var space=new List<uint>();var interior=new List<uint>();
            foreach(var world in change.Added)
                for(var y=world.Y;y<world.Y+world.Height;++y)
                    for(var x=world.X;x<world.X+world.Width;++x)
                    {
                        var cell=x+y*next.GridWidth;
                        if(Grid.WorldIdx[cell]!=world.Id||ElementLoader.elements[source->elementIdx[cell]].IsSolid)continue;
                        var outside=World.Instance.zoneRenderData?.GetSubWorldZoneType(cell)==ProcGen.SubWorld.ZoneType.Space;
                        (outside?space:interior).Add((uint)cell);
                        openings.Add(new OniNativeColony.Terrain{PublicCell=(uint)cell,Kind=OniNativeColony.Open});
                    }
            if(space.Count>0)OniNativeColony.Check(OniNativeColony.SetSpaceCells(space.ToArray(),(uint)space.Count,1));
            if(interior.Count>0)OniNativeColony.Check(OniNativeColony.SetSpaceCells(interior.ToArray(),(uint)interior.Count,0));
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
        private static bool Prefix(WorldContainer __instance,ref System.Action? __1)
        {
            if(!ColonyClusterWorlds.Enabled)return true;
            try{ColonyClusterWorlds.BeforeTemplate(__instance);__1=ColonyClusterWorlds.AfterTemplate(__1);return true;}
            catch(Exception error){ColonyFault.Raise("rocket template",error);return false;}
        }
    }
    [HarmonyPatch(typeof(ClusterManager),"UnregisterWorldContainer")]
    internal static class ColonyRocketUnregisterPatch
    {
        private static bool Prefix(WorldContainer __0)=>!ColonyClusterRetirement.HoldUnregister(__0);
        private static void Postfix()=>ColonyClusterWorlds.Changed();
    }
}
