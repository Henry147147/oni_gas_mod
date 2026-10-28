using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? activityCreature, activityFilter, activityTile;
        private static readonly System.Reflection.MethodInfo suspendActivityBrain = AccessTools.Method(typeof(Brain), "Suspend", new[] { typeof(string) });
        private static readonly System.Reflection.MethodInfo stopActivityNavigator = AccessTools.Method(typeof(Navigator), "Stop", new[] { typeof(bool), typeof(bool) });
        private static readonly System.Reflection.MethodInfo clearActivityOccupancy = AccessTools.Method(typeof(OccupyArea), "ClearOccupiedArea");
        private static readonly System.Reflection.MethodInfo updateActivityOccupancy = AccessTools.Method(typeof(OccupyArea), "UpdateOccupiedArea");
        private static void FreezeActivityCreature(GameObject creature)
        {
            var brain=creature.GetComponent<Brain>(); if(brain!=null&&brain.isSpawned)suspendActivityBrain.Invoke(brain,new object[]{"Mixed Atmospheres activity lifecycle control"});
            var navigator=creature.GetComponent<Navigator>();if(navigator!=null&&navigator.isSpawned)stopActivityNavigator.Invoke(navigator,new object[]{true,false});
        }
        private static void MoveActivityCreature(GameObject creature,int cell)
        {
            var occupy=creature.GetComponent<OccupyArea>();if(occupy!=null&&occupy.isSpawned)clearActivityOccupancy.Invoke(occupy,null);
            creature.transform.position=Grid.CellToPosCBC(cell,Grid.SceneLayer.Creatures);
            if(occupy!=null&&occupy.isSpawned)updateActivityOccupancy.Invoke(occupy,null);
        }
        private static GameObject NewActivityCreature(int cell)
        {
            var creature=Util.KInstantiate(Assets.GetPrefab(PuftConfig.ID),Grid.CellToPosCBC(cell,Grid.SceneLayer.Creatures));
            creature.SetActive(false);return creature;
        }
        private static Dictionary<int,OniNativeColony.ActivityCell> ReadActivity(params int[] requested)
        {
            var cells=requested.Select(c=>(uint)c).Distinct().OrderBy(c=>c).ToArray();
            var rows=new OniNativeColony.ActivityCell[cells.Length];var status=OniNativeColony.Status.Empty;
            var published=ColonyOwner.Status;
            OniNativeColony.Check(OniNativeColony.SampleActivity(published.Session,published.CompletedFrames,cells,(uint)cells.Length,ref status,rows));
            if(status.Session!=published.Session || status.CompletedFrames!=published.CompletedFrames)
                throw new InvalidOperationException("Activity control sampled a different publication.");
            return rows.ToDictionary(r=>(int)r.PublicCell);
        }
        private static void ActivityFlush()
        { ColonyActivity.Refresh();_=ColonySaveBarrier.FlushInputs(); }
        private static void ActivityLifecycle(int frame,OniNativeColony.Status status)
        {
            if(!ColonyActivity.Active)throw new InvalidOperationException("Activity lifecycle control requires integrated profile9.");
            if(Once(0,"activity lifecycle arena"))
            {
                activityCreature=activityFilter=activityTile=null;
                Arena((x,y)=>(SimHashes.ContaminatedOxygen,1f,300f));
            }
            if(Once(6,"inactive creature and real building footprints"))
            {
                activityCreature=NewActivityCreature(Cell(1,4));
                var filterId=new[]{GasFilterConfig.ID,LiquidFilterConfig.ID}.FirstOrDefault(id=>
                    Assets.GetBuildingDef(id).BuildingComplete.GetComponents<ISecondaryOutput>().Length!=0);
                if(filterId==null)throw new InvalidOperationException("No base-game filter exposes a secondary output port.");
                // A footprint-only filter stays unpowered: the convenience
                // Build helper would route actor-bearing wires through the
                // creature's motion corridor.
                var filter=Assets.GetBuildingDef(filterId);
                activityFilter=filter.Build(Cell(1,0),Orientation.Neutral,null,filter.DefaultElements(),300,playsound:false);
                activityTile=Build(TileConfig.ID,Cell(6,0));
                Record("fixture begins inactive before first spawn",!activityCreature.activeInHierarchy&&!activityCreature.GetComponent<PrimaryElement>().isSpawned,
                    "real Puft prefab awaits OnSpawn");
            }
            if(Once(10,"activate inactive prefab through normal spawn"))
            {
                SpeedControlScreen.Instance.Pause(false);
                try
                {
                    ActivityFlush();var empty=ReadActivity(Cell(1,4),Cell(6,4));
                    var prefab=activityCreature!.GetComponent<KPrefabID>();var brain=activityCreature.GetComponent<Brain>();
                    var registered=Components.Brains.Items.Any(b=>ReferenceEquals(b,brain))&&
                        ReferenceEquals(KPrefabIDTracker.Get().GetInstance(prefab.InstanceID),prefab);
                    Record("inactive prefab census follows the real game registry",(empty[Cell(1,4)].Actors!=0)==registered&&empty[Cell(6,4)].Actors==0,
                        $"tracked live brain {registered}, initial anchor actor {empty[Cell(1,4)].Actors}, destination actor {empty[Cell(6,4)].Actors}");
                    activityCreature!.SetActive(true);
                }
                finally{SpeedControlScreen.Instance.Unpause(false);}
            }
            if(Once(12,"spawn, inactive motion and zero-time policy boundaries"))
            {
                SpeedControlScreen.Instance.Pause(false);
                try
                {
                    ActivityFlush();var before=ColonyOwner.Status;var gameBefore=GameClock.Instance.GetTime();
                    FreezeActivityCreature(activityCreature!);MoveActivityCreature(activityCreature!,Cell(1,4));ActivityFlush();
                    var spawned=ReadActivity(Cell(1,4));
                    Record("OnSpawn publishes the real creature's actor mask",activityCreature!.GetComponent<PrimaryElement>().isSpawned&&spawned[Cell(1,4)].Actors!=0,"spawned Puft anchor");
                    activityCreature.SetActive(false);MoveActivityCreature(activityCreature,Cell(6,4));
                    ActivityFlush();var moved=ReadActivity(Cell(1,4),Cell(6,4));
                    Record("inactive moved actor remains in the full census",!activityCreature.activeInHierarchy&&moved[Cell(6,4)].Actors!=0&&ColonyActivity.InactiveActors>0,
                        $"new anchor actor {moved[Cell(6,4)].Actors}, old contact clock {moved[Cell(1,4)].LastActivitySeconds:R}");
                    Record("motion touches its previous footprint at zero time",moved[Cell(1,4)].LastActivitySeconds>=before.TimeSeconds,
                        $"old activity {moved[Cell(1,4)].LastActivitySeconds:R}, owner {before.TimeSeconds:R}");
                    activityCreature.SetActive(true);FreezeActivityCreature(activityCreature);ActivityFlush();
                    CheckActivityBuildingFootprints();
                    foreach(var enabled in new uint[]{0,1})
                    {
                        OniNativeColony.Check(OniNativeColony.SetActivityTiers(before.Session,enabled));ActivityFlush();
                        var policy=OniNativeColony.ActivityState.Empty;OniNativeColony.Check(OniNativeColony.CopyActivity(ref policy));
                        var sample=ReadActivity(Cell(1,4),Cell(6,4));
                        Record(enabled==0?"Always simulate applies at a zero-time boundary":"activity tiers re-enable without catch-up",
                            policy.Enabled==enabled&&sample.Values.All(c=>c.PendingSeconds==0&&c.NextRemoteSeconds==0),$"requested policy {policy.Enabled}");
                    }
                    var after=ColonyOwner.Status;
                    Record("paused lifecycle work preserves both physical clocks and owner frames",before.Session==after.Session&&before.CompletedFrames==after.CompletedFrames&&
                        BitConverter.DoubleToInt64Bits(before.TimeSeconds)==BitConverter.DoubleToInt64Bits(after.TimeSeconds)&&
                        BitConverter.DoubleToInt64Bits(gameBefore)==BitConverter.DoubleToInt64Bits(GameClock.Instance.GetTime()),
                        $"owner {before.TimeSeconds:R}->{after.TimeSeconds:R}, frame {before.CompletedFrames}->{after.CompletedFrames}, game {gameBefore:R}->{GameClock.Instance.GetTime():R}");
                }
                finally{SpeedControlScreen.Instance.Unpause(false);}
            }
            if(Once(16,"remove real actor"))
            { recorded["activity removal requested time"]=ColonyOwner.Status.TimeSeconds;if(activityCreature!=null)Util.KDestroyGameObject(activityCreature); }
            if(recorded.ContainsKey("activity removal requested time")&&!recorded.ContainsKey("activity removal checked")&&activityCreature==null)
            {
                SpeedControlScreen.Instance.Pause(false);
                try
                {
                    // Complete any positive frame already submitted before
                    // pausing, then measure only the zero-time boundary.
                    _=ColonySaveBarrier.Settle();
                    var before=ColonyOwner.Status;ActivityFlush();var sample=ReadActivity(Cell(1,4),Cell(6,4));
                    Record("cleanup removes the actor from both positions",activityCreature==null&&sample.Values.All(c=>c.Actors==0),
                        string.Join(", ",sample.Select(p=>$"{p.Key}: actor {p.Value.Actors}, touched {p.Value.LastActivitySeconds:R}")));
                    Record("removal wakes its last fluid contact",sample[Cell(6,4)].LastActivitySeconds>=recorded["activity removal requested time"]&&
                        before.CompletedFrames==ColonyOwner.Status.CompletedFrames&&before.TimeSeconds==ColonyOwner.Status.TimeSeconds,
                        $"removal requested {recorded["activity removal requested time"]:R}, contact activity {sample[Cell(6,4)].LastActivitySeconds:R}, zero boundary {before.TimeSeconds:R}->{ColonyOwner.Status.TimeSeconds:R}");
                    recorded["activity removal checked"]=1;
                    activityCreature=NewActivityCreature(Cell(6,4));activityCreature.SetActive(true);FreezeActivityCreature(activityCreature);
                }
                finally{SpeedControlScreen.Instance.Unpause(false);}
            }
            if(Once(24,"foundation for the supported live consumer and emitter"))
            {
                if(!recorded.ContainsKey("activity removal checked"))throw new InvalidOperationException("The creature did not finish normal cleanup.");
                foreach(var x in new[]{4,5})SimMessages.ReplaceElement(Cell(x,0),SimHashes.Granite,CellEventLogger.Instance.SandBoxTool,1000,300);
            }
            if(Once(26,"supported live callback-owned actor fixture"))Build(DevLifeSupportConfig.ID,Cell(4,1));
            if(Once(28,"hold live actor for the pending save"))
            { FreezeActivityCreature(activityCreature!);MoveActivityCreature(activityCreature!,Cell(6,4)); }
            if(Once(32,"normal save with one prepared physical frame"))
            {
                SpeedControlScreen.Instance.Pause(false);ActivityFlush();
                var before=ColonyOwner.Status;var actorInputs=ColonyActors.Queued;var sequence=ColonyActivity.StageNextFrameForSave(.2f);
                var path=Path.Combine(ColonyRuntimeValidation.OutputDirectory,"activity-pending.sav");var saves=ColonyOwnerPersistence.Saves;
                SaveLoader.Instance.Save(path,false,false);
                ColonyContinuationProbe.SourceSaved(ColonyOwnerPersistence.LastSavedFrame,path);
                var after=ColonyOwner.Status;var activity=OniNativeColony.ActivityState.Empty;OniNativeColony.Check(OniNativeColony.CopyActivity(ref activity));
                Record("prepared packet contains real callback-owned actor inputs",ColonyActors.Queued>actorInputs&&ColonyCallbackRequests.SavedRequests>0,
                    $"queued actor transfers {ColonyActors.Queued-actorInputs}, saved requests {ColonyCallbackRequests.SavedRequests}");
                Record("normal save persists an unbound actor frame",File.Exists(path)&&ColonyOwnerPersistence.Saves==saves+1&&activity.LastCommittedSequence<sequence&&
                    activity.NextSubmissionSequence==sequence+1&&before.CompletedFrames==after.CompletedFrames&&before.TimeSeconds==after.TimeSeconds,
                    $"{path}; queued sequence {sequence}, committed {activity.LastCommittedSequence}, next {activity.NextSubmissionSequence}");
                Record("activity controls retain closed ledgers",after.State==1&&after.MaximumRelativeMassError<=1e-12&&after.MaximumRelativeEnergyError<=1e-12,
                    $"mass {after.MaximumRelativeMassError:R}, energy {after.MaximumRelativeEnergyError:R}");
                recorded["activity staged save frame"]=after.CompletedFrames;
            }
            if(recorded.ContainsKey("activity staged save frame")&&ColonyActivity.StagedDeliveries>0&&ColonyContinuationProbe.Done)
            {
                Record("uninterrupted resume delivers the prepared frame once",ColonyActivity.StagedDeliveries==1&&
                    ColonyOwner.Status.CompletedFrames>(ulong)recorded["activity staged save frame"],
                    $"prepared deliveries {ColonyActivity.StagedDeliveries}, frame {ColonyOwner.Status.CompletedFrames}");
                Finish();
            }
        }
        private static void CheckActivityBuildingFootprints()
        {
            var filter=activityFilter??throw new InvalidOperationException("Filter fixture missing.");
            var anchor=Grid.PosToCell(filter);var building=filter.GetComponent<Building>();
            var expected=building.Def.PlacementOffsets.Select(o=>Grid.OffsetCell(anchor,o)).ToList();
            var secondaries=new List<int>();
            foreach(var port in filter.GetComponents<ISecondaryOutput>())foreach(var kind in new[]{ConduitType.Gas,ConduitType.Liquid,ConduitType.Solid})
                if(port.HasSecondaryConduitType(kind))secondaries.Add(Grid.OffsetCell(anchor,port.GetSecondaryConduitOffset(kind)));
            expected.AddRange(secondaries);var cells=ReadActivity(expected.ToArray());
            Record("building body and secondary ports enter the actor mask",secondaries.Count>0&&cells.Values.All(c=>c.Actors!=0),
                $"{building.Def.PrefabID}: {expected.Distinct().Count()} body/port cells, secondary ports {secondaries.Count}");
            var tile=Grid.PosToCell(activityTile);var contacts=new[]{Grid.CellLeft(tile),Grid.CellRight(tile),Grid.CellAbove(tile)};
            var contactRows=ReadActivity(contacts.Concat(new[]{tile}).ToArray());
            Record("solid building footprint activates its adjacent fluid contacts",!ColonyOwner.PublishedActivityFluid((uint)tile)&&contactRows[tile].Actors==0&&
                contacts.Where(c=>ColonyOwner.PublishedActivityFluid((uint)c)).All(c=>contactRows[c].Actors!=0)&&contacts.Any(c=>ColonyOwner.PublishedActivityFluid((uint)c)),
                $"tile actor {contactRows[tile].Actors}; contacts "+string.Join(",",contacts.Select(c=>$"{c}:{contactRows[c].Actors}")));
        }
    }
}
