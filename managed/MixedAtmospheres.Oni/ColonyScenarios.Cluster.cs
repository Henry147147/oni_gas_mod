using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static int[] clusterProbeCells=Array.Empty<int>();
        private static int clusterOriginalWorld,clusterOtherWorld;
        private static ulong clusterProbeSession,clusterCameraFrame;
        private static GameObject? clusterLaunchPad,clusterEngine,clusterCapsule;
        private static WorldContainer? clusterRocketWorld;
        private static int clusterWorldsBeforeRocket;

        private static void SpacedOutInputFlush(int frame,OniNativeColony.Status status)
        {
            if(!Once(0,"cluster lifecycle input boundary"))return;
            if(clusterProbeCells.Length==0)throw new InvalidOperationException("Run spaced-out-worlds before the input-boundary control.");
            _=ColonySaveBarrier.FlushInputs();
            var before=OniNativeColony.Status.Empty;OniNativeColony.Check(OniNativeColony.CopyStatus(ref before));
            var cell=clusterProbeCells[0];var oxygen=SpeciesMass(cell,SimHashes.Oxygen);var carbon=SpeciesMass(cell,SimHashes.CarbonDioxide);
            SimMessages.AddRemoveSubstance(cell,SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,.125f,300,byte.MaxValue,0);
            SimMessages.AddRemoveSubstance(cell,SimHashes.CarbonDioxide,CellEventLogger.Instance.OxygenModifierSimUpdate,.0625f,300,byte.MaxValue,0);
            ulong extracted;
            using(var batch=new ColonyPipePorts.ActorBatch())
            {
                var ordered=new System.Collections.Generic.List<OniNativeColony.Transfer>();
                extracted=batch.Extract(ordered,cell,1,(uint)cell,.375,0);batch.Commit(ordered);
            }
            _=ColonySaveBarrier.FlushInputs();
            var parcel=ColonyPipePorts.BufferInventory(extracted);
            Record("zero-time boundary delivers precise mixed outcomes",!ColonyPipePorts.IsPending(extracted)&&
                Math.Abs(ClusterPipeMass(parcel,SimHashes.Oxygen)-.25)<1e-9&&Math.Abs(ClusterPipeMass(parcel,SimHashes.CarbonDioxide)-.125)<1e-9,
                $"oxygen {ClusterPipeMass(parcel,SimHashes.Oxygen):R} kg, carbon dioxide {ClusterPipeMass(parcel,SimHashes.CarbonDioxide):R} kg");
            var returned=ColonyPipePorts.DispatchBuffer(extracted,(uint)cell);
            _=ColonySaveBarrier.FlushInputs();
            var after=OniNativeColony.Status.Empty;OniNativeColony.Check(OniNativeColony.CopyStatus(ref after));
            Record("lifecycle boundary retains the same physical clock",after.State==1&&after.Session==before.Session&&
                after.CompletedFrames==before.CompletedFrames&&after.NativeTicket==before.NativeTicket&&after.TimeSeconds==before.TimeSeconds,
                $"frames {before.CompletedFrames}/{after.CompletedFrames}, tickets {before.NativeTicket}/{after.NativeTicket}, time {before.TimeSeconds:R}/{after.TimeSeconds:R}");
            Record("zero-time return retains both source constituents",Math.Abs(SpeciesMass(cell,SimHashes.Oxygen)-oxygen-.125)<1e-9&&
                Math.Abs(SpeciesMass(cell,SimHashes.CarbonDioxide)-carbon-.0625)<1e-9&&returned.All(id=>!ColonyPipePorts.IsPending(id)&&ColonyPipePorts.BufferInventory(id).Count==0),
                $"oxygen {SpeciesMass(cell,SimHashes.Oxygen):R} kg, carbon dioxide {SpeciesMass(cell,SimHashes.CarbonDioxide):R} kg");
            Record("lifecycle boundary closes the pipe account",ColonyConduits.Audit(out var detail),detail);
            Finish();
        }

        // Exercise each real generated asteroid, rather than counting world
        // metadata while owning only the currently selected camera world.
        private static void SpacedOutWorlds(int frame,OniNativeColony.Status status)
        {
            if(Once(0,"cluster rooms"))
            {
                if(!DlcManager.IsExpansion1Active())throw new InvalidOperationException("Cluster ownership requires actual Spaced Out content.");
                var layout=ColonyOwner.CaptureWorldLayout();
                if(layout.Worlds.Count<2)throw new InvalidOperationException("The cluster control needs multiple generated worlds.");
                clusterOriginalWorld=ClusterManager.Instance.activeWorldId;
                clusterOtherWorld=layout.Worlds.First(w=>w.Id!=clusterOriginalWorld).Id;
                clusterProbeSession=status.Session;
                clusterProbeCells=layout.Worlds.Select(w=>(w.X+w.Width/2)+(w.Y+w.Height/2)*layout.GridWidth).ToArray();
                var tool=CellEventLogger.Instance.SandBoxTool;
                foreach(var cell in clusterProbeCells)
                {
                    Grid.CellToXY(cell,out var x,out var y);
                    FloorArenas.Clear(x-1,y-1,3,3,out _);
                    for(var dy=-1;dy<=1;++dy)for(var dx=-1;dx<=1;++dx)
                        SimMessages.ReplaceElement(Grid.XYToCell(x+dx,y+dy),dx==0&&dy==0?SimHashes.Vacuum:SimHashes.Unobtanium,
                            tool,dx==0&&dy==0?0:20000,dx==0&&dy==0?0:300);
                }
                Record("actual multi-asteroid cluster",true,$"{layout.Worlds.Count} worlds on {layout.GridWidth}x{layout.GridHeight}");
            }
            // Closing the border displaces its original gas into neighbours,
            // including the centre. Clear that completed displacement before
            // applying the declared source, as the normal arena helper does.
            if(Once(4,"clear sealed cluster probes"))
                foreach(var cell in clusterProbeCells)
                    SimMessages.ReplaceElement(cell,SimHashes.Vacuum,CellEventLogger.Instance.SandBoxTool,0,0);
            if(Once(6,"cluster mixtures"))
            {
                var empty=clusterProbeCells.All(cell=>CellMass(cell)==0);
                Record("sealed probes are empty before the source",empty,string.Join("; ",clusterProbeCells.Select(cell=>$"{cell}: {CellMass(cell):R} kg")));
                foreach(var cell in clusterProbeCells)
                {
                    SimMessages.AddRemoveSubstance(cell,SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,1,300,byte.MaxValue,0);
                    SimMessages.AddRemoveSubstance(cell,SimHashes.CarbonDioxide,CellEventLogger.Instance.OxygenModifierSimUpdate,.5f,300,byte.MaxValue,0);
                }
            }
            if(Once(14,"cluster ownership"))
            {
                foreach(var cell in clusterProbeCells)
                {
                    var oxygen=SpeciesMass(cell,SimHashes.Oxygen);var carbon=SpeciesMass(cell,SimHashes.CarbonDioxide);
                    Record($"world {Grid.WorldIdx[cell]} owns both constituents",ColonyOwner.OwnsFluidCell(cell)&&Math.Abs(oxygen-1)<1e-6&&Math.Abs(carbon-.5)<1e-6,
                        $"cell {cell}: oxygen {oxygen:R} kg, carbon dioxide {carbon:R} kg");
                }
                var ids=ColonyOwner.CaptureWorldLayout().CellWorldIds();var hole=Array.FindIndex(ids,id=>id<0);
                Record("unallocated cluster cells stay closed",hole>=0&&!ColonyOwner.OwnsFluidCell(hole),$"reserved cell {hole}");
                clusterCameraFrame=status.CompletedFrames;ClusterManager.Instance.SetActiveWorld(clusterOtherWorld);
            }
            if(Once(18,"other world camera"))
            {
                Record("changing worlds retains the running owner",ClusterManager.Instance.activeWorldId==clusterOtherWorld&&
                    status.Session==clusterProbeSession&&status.CompletedFrames>clusterCameraFrame&&clusterProbeCells.All(ColonyOwner.OwnsFluidCell),
                    $"camera world {ClusterManager.Instance.activeWorldId}, session {status.Session}, frame {status.CompletedFrames}");
                ClusterManager.Instance.SetActiveWorld(clusterOriginalWorld);
            }
            if(Once(22,"cluster complete"))
            {
                Record("returning camera preserves cluster ownership",ClusterManager.Instance.activeWorldId==clusterOriginalWorld&&
                    status.Session==clusterProbeSession&&clusterProbeCells.All(ColonyOwner.OwnsFluidCell),$"camera world {ClusterManager.Instance.activeWorldId}");
                Record("cluster mass and energy ledgers close",status.State==1&&status.MaximumRelativeMassError<=1e-12&&status.MaximumRelativeEnergyError<=1e-12,
                    $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
                Finish();
            }
        }
        private static (double Oxygen,double Carbon,ulong Frame) WorldSpecies(WorldContainer world)
        {
            var offset=world.WorldOffset;var size=world.WorldSize;var cells=new uint[checked(size.x*size.y)];
            for(var i=0;i<cells.Length;++i)cells[i]=(uint)Grid.XYToCell(offset.x+i%size.x,offset.y+i/size.x);
            // The shared native sampler retains one immutable publication;
            // per-cell reads could cross a flow publication while summing.
            var rows=new MeltSampleCell[cells.Length];var parts=new MeltSamplePart[checked(cells.Length*ColonyOwner.Materials.Length)];
            OniNativeColony.Check(SampleMelt(cells,(uint)cells.Length,out var source,rows,parts,(uint)parts.Length,out var count,out _));
            var oxygen=ElementLoader.FindElementByHash(SimHashes.Oxygen).idx;var carbon=ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).idx;
            double oxygenMass=0,carbonMass=0;
            for(var i=0U;i<count;++i)
            {if(parts[i].NativeIndex==oxygen)oxygenMass+=parts[i].Mass;else if(parts[i].NativeIndex==carbon)carbonMass+=parts[i].Mass;}
            return(oxygenMass,carbonMass,source.CompletedFrames);
        }
        private static void SpacedOutRocket(int frame,OniNativeColony.Status status)
        {
            if(Once(0,"rocket site"))
            {
                if(!DlcManager.IsExpansion1Active())throw new InvalidOperationException("Rocket control requires actual Spaced Out content.");
                clusterWorldsBeforeRocket=ClusterManager.Instance.WorldContainers.Count;
                clusterLaunchPad=clusterEngine=clusterCapsule=null;clusterRocketWorld=null;
                FloorArenas.Clear(x0-1,y0-1,12,26,out _);
                for(var y=-1;y<25;++y)for(var x=-1;x<11;++x)
                {
                    var wall=y==-1||y==24||x==-1||x==10;
                    SimMessages.ReplaceElement(Cell(x,y),wall?SimHashes.Unobtanium:SimHashes.Vacuum,
                        CellEventLogger.Instance.SandBoxTool,wall?20000:0,wall?300:0);
                }
            }
            if(Once(4,"rocket platform"))clusterLaunchPad=Build(LaunchPadConfig.ID,Cell(4,0));
            if(Once(8,"rocket engine"))
            {
                clusterEngine=AddRocketModule(clusterLaunchPad!,CO2EngineConfig.ID,true);
            }
            if(Once(12,"rocket capsule"))
            {
                clusterCapsule=AddRocketModule(clusterEngine!,HabitatModuleSmallConfig.ID,false);
            }
            if(frame>=16&&!recorded.ContainsKey("rocket mixture frame"))
            {
                var exterior=clusterCapsule?.GetComponent<ClustercraftExteriorDoor>();
                var paired=exterior==null?null:HarmonyLib.AccessTools.Field(typeof(ClustercraftExteriorDoor),"targetDoor").GetValue(exterior);
                clusterRocketWorld=paired==null?null:exterior!.GetTargetWorld();
                var open=clusterRocketWorld==null?Array.Empty<int>():Enumerable.Range(0,clusterRocketWorld.WorldSize.x*clusterRocketWorld.WorldSize.y)
                    .Select(i=>Grid.XYToCell(clusterRocketWorld.WorldOffset.x+i%clusterRocketWorld.WorldSize.x,
                        clusterRocketWorld.WorldOffset.y+i/clusterRocketWorld.WorldSize.x))
                    .Where(cell=>Grid.WorldIdx[cell]==clusterRocketWorld.id&&!Grid.Solid[cell]).ToArray();
                if(clusterRocketWorld==null||open.Length==0||!open.All(ColonyOwner.OwnsFluidCell))
                {
                    if(frame<40)return;
                    Record("native rocket interior became owned",false,$"world {clusterRocketWorld?.id}, open {open.Length}, unowned {open.Count(cell=>!ColonyOwner.OwnsFluidCell(cell))}");Finish();return;
                }
                Record("native rocket interior became owned",clusterRocketWorld.IsModuleInterior&&ClusterManager.Instance.WorldContainers.Count==clusterWorldsBeforeRocket+1,
                    $"world {clusterRocketWorld.id}, {open.Length} non-solid cells, owner session {status.Session}");
                recorded["rocket mixture frame"]=frame;
                var baseline=WorldSpecies(clusterRocketWorld);
                recorded["rocket oxygen"]=baseline.Oxygen;recorded["rocket carbon"]=baseline.Carbon;
                recorded["rocket baseline owner frame"]=baseline.Frame;
                var door=clusterCapsule!.GetComponent<ClustercraftExteriorDoor>();
                var cabin=(int)HarmonyLib.AccessTools.Method(typeof(ClustercraftExteriorDoor),"TargetCell").Invoke(door,null);
                if(!open.Contains(cabin)||World.Instance.zoneRenderData.GetSubWorldZoneType(cabin)==ProcGen.SubWorld.ZoneType.Space)
                    throw new InvalidOperationException("The rocket's actual arrival cell is not an owned cabin cell.");
                SimMessages.AddRemoveSubstance(cabin,SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,.25f,300,byte.MaxValue,0);
                SimMessages.AddRemoveSubstance(cabin,SimHashes.CarbonDioxide,CellEventLogger.Instance.OxygenModifierSimUpdate,.125f,300,byte.MaxValue,0);
            }
            if(recorded.TryGetValue("rocket mixture frame",out var seeded)&&frame>=seeded+8&&!Current.Finished)
            {
                var proof=WorldSpecies(clusterRocketWorld!);
                var oxygen=proof.Oxygen-recorded["rocket oxygen"];var carbon=proof.Carbon-recorded["rocket carbon"];
                Record("rocket retains oxygen and carbon dioxide",Math.Abs(oxygen-.25)<1e-6&&Math.Abs(carbon-.125)<1e-6,
                    $"added oxygen {oxygen:R} kg, carbon dioxide {carbon:R} kg; snapshot frames {recorded["rocket baseline owner frame"]}/{proof.Frame}");
                Record("rocket cluster ledgers close",status.State==1&&status.MaximumRelativeMassError<=1e-12&&status.MaximumRelativeEnergyError<=1e-12,
                    $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
                Finish();
            }
        }
        private static GameObject AddRocketModule(GameObject parent,string id,bool first)
        {
            var def=Assets.GetBuildingDef(id);var previous=DebugHandler.InstantBuildMode;
            // Use the actual player construction command: it assigns the
            // craft interface and attachment context before OnSpawn runs.
            // The instant-build switch is fixture setup only and restored
            // before the next game update; no simulation callback is called.
            DebugHandler.InstantBuildMode=true;
            try
            {
                var module=first?parent.GetComponent<LaunchPad>().AddBaseModule(def,def.DefaultElements()):
                    parent.GetComponent<ReorderableBuilding>().AddModule(def,def.DefaultElements());
                if(module==null)throw new InvalidOperationException("The native rocket construction command refused "+id+".");
                powered.Add(module);return module;
            }
            finally{DebugHandler.InstantBuildMode=previous;}
        }
    }
}
