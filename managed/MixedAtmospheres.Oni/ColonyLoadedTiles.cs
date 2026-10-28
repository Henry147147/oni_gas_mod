using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static unsafe class ColonyLoadedTiles
    {
        private static OniNativeColony.Config? loaded;
        private static readonly Dictionary<PrimaryElement,Dictionary<int,ColonyTileLoadProjection>> tiles=new Dictionary<PrimaryElement,Dictionary<int,ColonyTileLoadProjection>>();
        [ThreadStatic]private static Dictionary<int,ColonyTileLoadProjection>? current;
        private static uint epoch;
        internal static int Preserved {get;private set;}
        internal static void Reset(){loaded=null;tiles.Clear();current=null;++epoch;Preserved=0;}
        internal static void Load(OniNativeColony.Config config){Reset();loaded=config;}
        internal static void Complete()
        {
            if(loaded==null)return;
            if(tiles.Keys.Any(tile=>tile!=null))throw new InvalidOperationException("Saved tiles have not finished recreating before simulation time.");
            loaded=null;tiles.Clear();current=null;
        }
        internal static void Deserialized(PrimaryElement primary)
        {
            if(loaded is not OniNativeColony.Config config||primary==null)return;
            var occupier=primary.GetComponent<SimCellOccupier>();var building=primary.GetComponent<Building>();
            if(occupier==null||!occupier.doReplaceElement||building==null)return;
            var cells=new Dictionary<int,ColonyTileLoadProjection>();
            building.RunOnArea(cell=>
            {
                if(!Grid.IsValidCell(cell))return;
                var x=(uint)(cell%Grid.WidthInCells);var y=(uint)(cell/Grid.WidthInCells);
                if(x<config.OffsetX||y<config.OffsetY||x-config.OffsetX>=config.Width||y-config.OffsetY>=config.Height)return;
                var element=Grid.Element[cell];
                if(element==null||!element.IsSolid||element.id!=primary.ElementID)return;
                cells.Add(cell,new ColonyTileLoadProjection(cell,element.idx,Grid.Mass[cell],Grid.Temperature[cell],Grid.DiseaseIdx[cell],Grid.DiseaseCount[cell]));
            });
            if(cells.Count!=0)tiles.Add(primary,cells);
        }
        internal sealed class Scope:IDisposable
        {
            private readonly Dictionary<int,ColonyTileLoadProjection>? previous;
            private readonly uint generation;
            private PrimaryElement? tile;
            private bool disposed;
            internal Scope(Dictionary<int,ColonyTileLoadProjection>? cells){previous=current;generation=epoch;current=cells;}
            internal void Track(PrimaryElement primary)=>tile=primary;
            internal void Finish(bool success)
            {
                // A failed original OnSpawn keeps its tile pending: positive
                // time then refuses the load instead of accepting partial setup.
                if(!disposed&&generation==epoch&&success&&!ReferenceEquals(tile,null))tiles.Remove(tile!);
                Dispose();
            }
            public void Dispose(){if(disposed)return;disposed=true;if(generation==epoch)current=previous;}
        }
        internal static Scope? Begin(SimCellOccupier occupier)
        {
            var primary=occupier.GetComponent<PrimaryElement>();Dictionary<int,ColonyTileLoadProjection>? cells=null;
            if(primary!=null)tiles.TryGetValue(primary,out cells);
            if(cells==null&&current==null)return null;
            var scope=new Scope(cells);if(cells!=null)scope.Track(primary!);return scope;
        }
        internal static void Rewrite(int message,int length,byte* data)
        {
            if(current==null||message!=-1252920804||length!=28||data==null)return;
            if(current.TryGetValue(*(int*)data,out var saved)&&saved.Rewrite(new Span<byte>(data,length)))++Preserved;
        }
    }
    [HarmonyPatch(typeof(PrimaryElement),"OnDeserialized")]
    internal static class ColonyLoadedTileDeserializePatch
    {
        private static void Postfix(PrimaryElement __instance)
        {try{ColonyLoadedTiles.Deserialized(__instance);}catch(Exception error){ColonyLoadRefusal.Refuse(error);}}
    }
    [HarmonyPatch(typeof(SimCellOccupier),"OnSpawn")]
    internal static class ColonyLoadedTileSpawnPatch
    {
        private static void Prefix(SimCellOccupier __instance,ref ColonyLoadedTiles.Scope? __state)=>__state=ColonyLoadedTiles.Begin(__instance);
        private static Exception? Finalizer(Exception? __exception,ColonyLoadedTiles.Scope? __state){__state?.Finish(__exception==null);return __exception;}
    }
}
