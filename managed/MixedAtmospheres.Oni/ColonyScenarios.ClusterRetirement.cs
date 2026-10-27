using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static ColonyWorldRegion retiredRocket;
        private static Dictionary<int,double> retirementExpected=new Dictionary<int,double>(),retirementParent=new Dictionary<int,double>();
        private static void ClusterAdd(Dictionary<int,double> values,int id,double mass)
        {values.TryGetValue(id,out var old);values[id]=old+mass;}
        private static Dictionary<int,double> ClusterPickupFluids(int world)
        {
            var result=new Dictionary<int,double>();
            foreach(var item in Resources.FindObjectsOfTypeAll<PrimaryElement>())
            {
                if(!item.isSpawned||item.Mass<=0||!item.Element.IsGas&&!item.Element.IsLiquid||item.GetComponent<Pickupable>()==null)continue;
                var cell=Grid.PosToCell(item.gameObject);
                if(Grid.IsValidCell(cell)&&Grid.WorldIdx[cell]==world)ClusterAdd(result,item.Element.idx,item.Mass);
            }
            return result;
        }
        private static Dictionary<int,double> ClusterWorldFluid(int worldId,bool pipesAndItems)
        {
            var world=ClusterManager.Instance.GetWorld(worldId);var region=new ColonyWorldRegion(worldId,world.WorldOffset.x,world.WorldOffset.y,world.WorldSize.x,world.WorldSize.y,true);
            var cells=Enumerable.Range(0,region.Width*region.Height).Select(i=>(uint)Grid.XYToCell(region.X+i%region.Width,region.Y+i/region.Width)).ToArray();
            var rows=new MeltSampleCell[cells.Length];var parts=new MeltSamplePart[checked(cells.Length*ColonyOwner.Materials.Length)];
            OniNativeColony.Check(SampleMelt(cells,(uint)cells.Length,out _,rows,parts,(uint)parts.Length,out var count,out _));
            var result=pipesAndItems?ClusterPickupFluids(worldId):new Dictionary<int,double>();
            for(var i=0U;i<count;++i)ClusterAdd(result,(int)parts[i].NativeIndex,parts[i].Mass);
            if(pipesAndItems)foreach(var cell in cells)
                foreach(var flow in new[]{Game.Instance.gasConduitFlow,Game.Instance.liquidConduitFlow})
                    if(flow.HasConduit((int)cell))foreach(var part in ColonyConduits.ReadSynced(flow,(int)cell).Parts)ClusterAdd(result,part.Id,part.Mass);
            return result;
        }
        private static void SpacedOutRocketRetirement(int frame,OniNativeColony.Status status)
        {
            if(Once(0,"retire real rocket"))
            {
                if(clusterCapsule==null||clusterRocketWorld==null)throw new InvalidOperationException("Run rocket creation and ports before retirement.");
                _=ColonySaveBarrier.FlushInputs();
                retiredRocket=new ColonyWorldRegion(clusterRocketWorld.id,clusterRocketWorld.WorldOffset.x,clusterRocketWorld.WorldOffset.y,
                    clusterRocketWorld.WorldSize.x,clusterRocketWorld.WorldSize.y,true);
                retirementExpected=ClusterWorldFluid(clusterRocketWorld.id,true);retirementParent=ClusterPickupFluids(clusterOriginalWorld);
                Record("retirement has real mixed gas and liquid sources",retirementExpected.Count>=4&&retirementExpected.Values.All(m=>m>0),
                    string.Join("; ",retirementExpected.Select(pair=>$"{ElementLoader.elements[pair.Key].id}: {pair.Value:R} kg")));
                // The normal deconstruction completion entry performs module
                // attachment and interior cleanup; no sim callback is invoked.
                AccessTools.Method(typeof(ReorderableBuilding),"RemoveModule").Invoke(clusterCapsule.GetComponent<ReorderableBuilding>(),null);
            }
            if(frame>=4&&!recorded.ContainsKey("retired rocket observed"))
            {
                if(ColonyClusterRetirement.Pending||ClusterManager.Instance.GetWorld(retiredRocket.Id)!=null)
                {
                    if(frame<40)return;
                    Record("rocket retirement finished",false,$"pending {ColonyClusterRetirement.Pending}, live {ClusterManager.Instance.GetWorld(retiredRocket.Id)!=null}");Finish();return;
                }
                var recovered=ClusterPickupFluids(clusterOriginalWorld);
                foreach(var expected in retirementExpected)
                {
                    recovered.TryGetValue(expected.Key,out var now);retirementParent.TryGetValue(expected.Key,out var before);
                    Record("rocket recovers "+ElementLoader.elements[expected.Key].id,Math.Abs(now-before-expected.Value)<1e-6,
                        $"recovered {now-before:R} kg, expected {expected.Value:R} kg");
                }
                Record("removed rocket cells are no longer owned",Enumerable.Range(0,retiredRocket.Width*retiredRocket.Height)
                    .All(i=>!ColonyOwner.OwnsFluidCell(Grid.XYToCell(retiredRocket.X+i%retiredRocket.Width,retiredRocket.Y+i/retiredRocket.Width))),
                    $"world {retiredRocket.Id}, completed retirements {ColonyClusterRetirement.Completed}");
                Record("retirement keeps exact pipe accounts",ColonyConduits.Audit(out var audit),audit);
                recorded["retired rocket observed"]=frame;
                clusterCapsule=AddRocketModule(clusterEngine!,HabitatModuleSmallConfig.ID,false);clusterRocketWorld=null;
            }
            if(recorded.TryGetValue("retired rocket observed",out var retired)&&frame>=retired+4&&!recorded.ContainsKey("reused rocket mixture"))
            {
                var exterior=clusterCapsule?.GetComponent<ClustercraftExteriorDoor>();
                var paired=exterior==null?null:AccessTools.Field(typeof(ClustercraftExteriorDoor),"targetDoor").GetValue(exterior);
                clusterRocketWorld=paired==null?null:exterior!.GetTargetWorld();
                var open=clusterRocketWorld==null?Array.Empty<int>():Enumerable.Range(0,clusterRocketWorld.WorldSize.x*clusterRocketWorld.WorldSize.y)
                    .Select(i=>Grid.XYToCell(clusterRocketWorld.WorldOffset.x+i%clusterRocketWorld.WorldSize.x,clusterRocketWorld.WorldOffset.y+i/clusterRocketWorld.WorldSize.x))
                    .Where(cell=>Grid.WorldIdx[cell]==clusterRocketWorld.id&&!Grid.Solid[cell]).ToArray();
                if(open.Length==0||!open.All(ColonyOwner.OwnsFluidCell))
                {
                    if(frame<retired+40)return;
                    Record("recreated rocket became owned",false,$"world {clusterRocketWorld?.id}, open {open.Length}");Finish();return;
                }
                Record("native allocator reuses the retired slot",clusterRocketWorld!.WorldOffset.x==retiredRocket.X&&clusterRocketWorld.WorldOffset.y==retiredRocket.Y,
                    $"old world {retiredRocket.Id} at {retiredRocket.X},{retiredRocket.Y}; new world {clusterRocketWorld.id} at {clusterRocketWorld.WorldOffset}");
                var fluids=ClusterWorldFluid(clusterRocketWorld.id,false);
                var leftovers=new[]{SimHashes.CarbonDioxide,SimHashes.Water,SimHashes.DirtyWater}.Select(element=>ElementLoader.FindElementByHash(element).idx)
                    .Sum(id=>fluids.TryGetValue(id,out var mass)?mass:0);
                Record("new interior inherits no former constituents",leftovers<1e-9,$"former carbon dioxide/liquids {leftovers:R} kg");
                var baseline=WorldSpecies(clusterRocketWorld);recorded["reused oxygen"]=baseline.Oxygen;recorded["reused carbon"]=baseline.Carbon;
                recorded["reused rocket mixture"]=frame;
                var cabin=(int)AccessTools.Method(typeof(ClustercraftExteriorDoor),"TargetCell").Invoke(exterior,null);
                SimMessages.AddRemoveSubstance(cabin,SimHashes.Oxygen,CellEventLogger.Instance.OxygenModifierSimUpdate,.125f,300,byte.MaxValue,0);
                SimMessages.AddRemoveSubstance(cabin,SimHashes.CarbonDioxide,CellEventLogger.Instance.OxygenModifierSimUpdate,.0625f,300,byte.MaxValue,0);
            }
            if(recorded.TryGetValue("reused rocket mixture",out var seeded)&&frame>=seeded+8)
            {
                var proof=WorldSpecies(clusterRocketWorld!);
                Record("reused rocket retains its own mixture",Math.Abs(proof.Oxygen-recorded["reused oxygen"]-.125)<1e-6&&
                    Math.Abs(proof.Carbon-recorded["reused carbon"]-.0625)<1e-6,$"oxygen delta {proof.Oxygen-recorded["reused oxygen"]:R}, carbon delta {proof.Carbon-recorded["reused carbon"]:R}");
                Record("retirement and reuse keep closed world ledgers",status.State==1&&status.MaximumRelativeMassError<=1e-12&&status.MaximumRelativeEnergyError<=1e-12,
                    $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");Finish();
            }
        }
    }
}
