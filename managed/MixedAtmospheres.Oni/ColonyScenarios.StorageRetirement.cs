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
    }
}
