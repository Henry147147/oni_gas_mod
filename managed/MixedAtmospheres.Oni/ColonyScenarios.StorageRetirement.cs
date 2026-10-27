using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? retirementReservoir;
        private static int retirementPhase,retirementReadyAt;
        private static long retirementBaseline;
        // Separate lifecycle regression: 6 kg water + 2 kg polluted water at
        // 350 K, one million germs, 22 owner frames in 300 K oxygen, then full
        // withdrawal and eight further positive frames for actual item cleanup.
        // Existing storage fixtures and their acceptance bounds stay unchanged.
        private static void MixedOwnedStorageRetirement(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"storage retirement arena"))
            {retirementReservoir=null;retirementPhase=0;Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"build retirement reservoir"))
            {
                retirementReservoir=Build(LiquidReservoirConfig.ID,Cell(4,0));
                AccessTools.Field(typeof(ConduitConsumer),"isOn").SetValue(retirementReservoir.GetComponent<ConduitConsumer>(),false);
                retirementReservoir.GetComponent<ConduitDispenser>().isOn=false;
            }
            if(Once(7,"retirement reservoir ports"))
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(retirementReservoir!)},new List<int>{OutputCell(retirementReservoir!)});
            if(frame>=8&&retirementPhase==0)
            {
                if(!flow.HasConduit(InputCell(retirementReservoir!))||!flow.HasConduit(OutputCell(retirementReservoir!)))
                {
                    Current.Stage="waiting for storage retirement topology";
                    if(frame>=30){Record("storage retirement topology became ready",false,"no native ports within readiness bound");Finish();}
                    return;
                }
                ColonyPipeRuntime.Start();ColonyOwnedStorage.StartControl();ColonyOwnedStorage.StartBiologyControl();
                retirementBaseline=ColonyOwnedStorage.NativeRetirements;retirementReadyAt=frame;retirementPhase=1;
                ColonyConduits.SupplyControlInventory(flow,InputCell(retirementReservoir!),Liquid(6,2,350,1000000));
                ColonyMachinePorts.Consume(retirementReservoir!.GetComponent<ConduitConsumer>(),1,flow);
                Record("warm germy mixture enters exact storage",ColonyOwnedStorage.Inventory(retirementReservoir.GetComponent<Storage>()).Mass==8,"8 kg at 75/25, one million germs");
            }
            if(retirementPhase==1&&frame>=retirementReadyAt+22)
            {
                var storage=retirementReservoir!.GetComponent<Storage>();var before=ColonyOwnedStorage.Inventory(storage);
                Record("retiring projections first performed real thermal and culture work",ColonyOwnedStorage.RegisteredProjections(storage)==2&&
                    ColonyOwnedStorage.Heat(storage).Sign<0&&before.Germs.Length>0&&ColonyOwnedStorage.Audit(storage),
                    $"bodies {ColonyOwnedStorage.RegisteredProjections(storage)}, heat {ColonyOwnedStorage.Heat(storage).Value:R} J, cultures {before.Germs.Length}");
                ColonyMachinePorts.Dispense(retirementReservoir.GetComponent<ConduitDispenser>());
                var output=ColonyConduits.ReadSynced(flow,OutputCell(retirementReservoir));
                var same=output.Total.Minus(before.Total).Sign==0&&output.Germs.Length==before.Germs.Length;
                for(var i=0;i<before.Germs.Length&&same;++i)
                    same=output.Germs[i].Id==before.Germs[i].Id&&output.Germs[i].Amount.Minus(before.Germs[i].Amount).Sign==0;
                foreach(var part in before.Parts)if(output.SpeciesMass(part.Id)!=part.Mass)same=false;
                Record("full withdrawal keeps every mass and culture",same&&ColonyOwnedStorage.Inventory(storage).Count==0&&ColonyOwnedStorage.Audit(storage),
                    $"output {output.Mass:R} kg, cultures {output.Germs.Length}, held {ColonyOwnedStorage.Inventory(storage).Mass:R} kg");
                Record("empty native projections retain no displayed germs",storage.items.Where(item=>item!=null).All(item=>
                    item.GetComponent<PrimaryElement>().Mass==0&&item.GetComponent<PrimaryElement>().DiseaseCount==0),"all discarded view masses and culture counts zero");
                retirementPhase=2;
            }
            if(retirementPhase==2&&frame>=retirementReadyAt+30)
            {
                var storage=retirementReservoir!.GetComponent<Storage>();
                Record("native cleanup retires and acknowledges both handles",ColonyOwnedStorage.NativeRetirements>=retirementBaseline+2&&
                    ColonyOwnedStorage.RetiringProjections(storage)==0&&ColonyOwnedStorage.RegisteredProjections(storage)==0&&!storage.items.Any(item=>item!=null),
                    $"retired {ColonyOwnedStorage.NativeRetirements-retirementBaseline}, pending {ColonyOwnedStorage.RetiringProjections(storage)}, items {storage.items.Count}");
                Record("storage and pipe accounts remain closed after cleanup",ColonyOwnedStorage.Audit(storage)&&ColonyConduits.Audit(out _),ColonyMachinePorts.Report);
                Finish();
            }
        }

        private static GameObject? activeOutletReservoir;
        private static int activeOutletPhase,activeOutletReadyAt;
        private static long activeOutletRetirements;
        private static ulong activeOutletDrainedSave;
        private static byte[]? activeOutletCompletedHistory;
        private static byte[] CompletedHistoryBytes(RetiredFluidStorage history)
        {using var stream=new System.IO.MemoryStream();using var writer=new System.IO.BinaryWriter(stream);history.Write(writer);return stream.ToArray();}
        // Separate saved-outlet regression, fixed before measurement: the same
        // 8 kg 75/25 mixture at 350 K, 22 warm frames, enable its real native
        // dispenser and save before the next update, then eight positive frames
        // for natural discharge and native cleanup. No manual dispense occurs.
        private static void MixedOwnedStorageActiveOutlet(int frame,bool saveDrained=false,bool retireBeforeSave=false)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"active outlet arena"))
            {activeOutletReservoir=null;activeOutletPhase=0;activeOutletCompletedHistory=null;Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"build active outlet reservoir"))
            {
                activeOutletReservoir=Build(LiquidReservoirConfig.ID,Cell(4,0));
                AccessTools.Field(typeof(ConduitConsumer),"isOn").SetValue(activeOutletReservoir.GetComponent<ConduitConsumer>(),false);
                activeOutletReservoir.GetComponent<ConduitDispenser>().isOn=false;
            }
            if(Once(7,"active outlet ports"))
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(activeOutletReservoir!)},new List<int>{OutputCell(activeOutletReservoir!)});
            if(frame>=8&&activeOutletPhase==0)
            {
                if(!flow.HasConduit(InputCell(activeOutletReservoir!))||!flow.HasConduit(OutputCell(activeOutletReservoir!)))
                {
                    Current.Stage="waiting for active outlet topology";
                    if(frame>=30){Record("active outlet topology became ready",false,"no native ports within readiness bound");Finish();}
                    return;
                }
                if(!ColonyPipeRuntime.NativeScheduleControl)throw new InvalidOperationException("The active outlet save control requires the native schedule writer.");
                if(saveDrained&&!ColonyPipeContactHeat.Enabled)throw new InvalidOperationException("The drained-store control requires native conduit contact completion observation.");
                activeOutletReadyAt=frame;activeOutletPhase=1;activeOutletRetirements=ColonyOwnedStorage.NativeRetirements;
                ColonyConduits.SupplyControlInventory(flow,InputCell(activeOutletReservoir!),Liquid(6,2,350,1000000));
                ColonyMachinePorts.Consume(activeOutletReservoir!.GetComponent<ConduitConsumer>(),1,flow);
            }
            if(activeOutletPhase==1&&frame>=activeOutletReadyAt+22)
            {
                var storage=activeOutletReservoir!.GetComponent<Storage>();
                Record("closed outlet held the complete warm mixture",ColonyOwnedStorage.Inventory(storage).Mass==8&&
                    ColonyOwnedStorage.RegisteredProjections(storage)==2&&ColonyOwnedStorage.Heat(storage).Sign<0&&ColonyOwnedStorage.Audit(storage),
                    $"held {ColonyOwnedStorage.Inventory(storage).Mass:R} kg, heat {ColonyOwnedStorage.Heat(storage).Value:R} J");
                activeOutletReservoir.GetComponent<ConduitDispenser>().isOn=true;
                var path=System.IO.Path.Combine(ColonyRuntimeValidation.OutputDirectory,"active-outlet.sav");
                SpeedControlScreen.Instance.Pause(false);var saves=ColonyOwnerPersistence.Saves;
                SaveLoader.Instance.Save(path,false,false);
                Record("ordinary save captures an open outlet and nonempty store",ColonyOwnerPersistence.Saves==saves+1&&System.IO.File.Exists(path)&&
                    activeOutletReservoir.GetComponent<ConduitDispenser>().isOn&&ColonyOwnedStorage.Inventory(storage).Mass==8,
                    $"saved frame {ColonyOwnerPersistence.LastSavedFrame}, held {ColonyOwnedStorage.Inventory(storage).Mass:R} kg");
                if(!saveDrained)ColonyContinuationProbe.SourceSaved(ColonyOwnerPersistence.LastSavedFrame,path);
                activeOutletPhase=2;
            }
            if(activeOutletPhase==2&&frame>=activeOutletReadyAt+30)
            {
                var storage=activeOutletReservoir!.GetComponent<Storage>();var output=ColonyConduits.ReadSynced(flow,OutputCell(activeOutletReservoir));
                Record("native scheduled outlet discharged every constituent",output.Mass==8&&
                    output.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.Water).Id)==6&&output.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id)==2&&
                    output.Germs.Length==1&&ColonyOwnedStorage.Inventory(storage).Count==0,$"output {output.Mass:R} kg, cultures {output.Germs.Length}");
                Record("scheduled discharge retires both native projections",ColonyOwnedStorage.NativeRetirements>=activeOutletRetirements+2&&
                    ColonyOwnedStorage.RegisteredProjections(storage)==0&&ColonyOwnedStorage.RetiringProjections(storage)==0&&!storage.items.Any(item=>item!=null),
                    $"retired {ColonyOwnedStorage.NativeRetirements-activeOutletRetirements}, pending {ColonyOwnedStorage.RetiringProjections(storage)}, items {storage.items.Count}");
                Record("storage and pipe accounts close after scheduled discharge",ColonyOwnedStorage.Audit(storage)&&ColonyConduits.Audit(out _),ColonyMachinePorts.Report);
                if(!saveDrained){Finish();return;}
                var history=ColonyOwnedStorage.Capture().Stores.Single(store=>store.Address.Prefab==storage.GetComponent<KPrefabID>().InstanceID);
                var water=ColonyConduits.MaterialOf(SimHashes.Water).Id;var dirty=ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id;
                bool Mass(System.Collections.Generic.IReadOnlyDictionary<int,StorageJournalAmount> amounts,int id,double value)=>
                    amounts.TryGetValue(id,out var amount)&&amount.Units==StorageJournalAmount.From(new PipeAmount(value)).Units;
                Record("drained store retains its transfer heat and culture history",!history.PristineEmpty&&history.Inventory.Count==0&&history.Heat.Sign<0&&
                    Mass(history.Admitted.Masses,water,6)&&Mass(history.Admitted.Masses,dirty,2)&&Mass(history.Withdrawn.Masses,water,6)&&Mass(history.Withdrawn.Masses,dirty,2)&&
                    history.Admitted.Germs.Count==1&&history.Withdrawn.Germs.Count==1&&history.Births.Count+history.Deaths.Count>0&&history.Clocks.Count==0&&history.CultureContexts.Count==0,
                    $"heat {history.Heat.Value:R} J, admitted/withdrawn species {history.Admitted.Masses.Count}/{history.Withdrawn.Masses.Count}, birth/death histories {history.Births.Count}/{history.Deaths.Count}");
                if(retireBeforeSave)
                {
                    activeOutletCompletedHistory=CompletedHistoryBytes(RetiredFluidStorage.From(history));
                    activeOutletPhase=4;Util.KDestroyGameObject(activeOutletReservoir);return;
                }
                var path=System.IO.Path.Combine(ColonyRuntimeValidation.OutputDirectory,"drained-outlet.sav");
                var saves=ColonyOwnerPersistence.Saves;SpeedControlScreen.Instance.Pause(false);SaveLoader.Instance.Save(path,false,false);
                Record("ordinary save captures the drained store after native retirement",ColonyOwnerPersistence.Saves==saves+1&&System.IO.File.Exists(path)&&
                    !ColonyOwnedStorage.RetirementsPending&&ColonyOwnedStorage.Inventory(storage).Count==0&&ColonyPipeContactHeat.Contacts>0&&
                    ColonyPipeContactHeat.BodyHeatMismatch()==null,$"saved frame {ColonyOwnerPersistence.LastSavedFrame}");
                activeOutletDrainedSave=ColonyOwnerPersistence.LastSavedFrame;
                ColonyContinuationProbe.SourceSaved(activeOutletDrainedSave,path);activeOutletPhase=3;
            }
            if(activeOutletPhase==4&&frame>=activeOutletReadyAt+32)
            {
                var state=ColonyOwnedStorage.Capture();
                Record("demolished empty reservoir retains exact completed history without a native host",activeOutletReservoir==null&&state.Stores.Length==0&&
                    state.Retired.Length==1&&state.Retired[0].Audit()&&CompletedHistoryBytes(state.Retired[0]).SequenceEqual(activeOutletCompletedHistory!),
                    $"live stores {state.Stores.Length}, completed phases {state.Retired.Length}");
                var path=System.IO.Path.Combine(ColonyRuntimeValidation.OutputDirectory,"retired-outlet.sav");
                var saves=ColonyOwnerPersistence.Saves;SpeedControlScreen.Instance.Pause(false);SaveLoader.Instance.Save(path,false,false);
                Record("ordinary save captures completed history after reservoir demolition",ColonyOwnerPersistence.Saves==saves+1&&System.IO.File.Exists(path)&&
                    !ColonyOwnedStorage.RetirementsPending&&ColonyPipeContactHeat.Contacts>0&&ColonyPipeContactHeat.BodyHeatMismatch()==null,
                    $"saved frame {ColonyOwnerPersistence.LastSavedFrame}");
                activeOutletDrainedSave=ColonyOwnerPersistence.LastSavedFrame;
                ColonyContinuationProbe.SourceSaved(activeOutletDrainedSave,path);activeOutletPhase=3;
            }
            if(activeOutletPhase==3&&ColonyOwner.Status.CompletedFrames>=activeOutletDrainedSave+8)Finish();
        }
    }
}
