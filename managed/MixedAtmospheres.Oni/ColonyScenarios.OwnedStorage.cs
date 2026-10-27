using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? exactReservoir;
        // Observe completed but uncredited work at its publication boundary.
        // A synchronized native frame can otherwise have all of its receipts
        // credited before the ordinary Unity validation update samples them.
        // The fixture retains its warm-up, maximum readiness delay, parcels
        // and acceptance bounds; only its observation epoch follows the work.
        internal static void BeforeStorageHeatPublication()
        {
            if(Current.Name!="mixed-storage-transfer-heat"||Current.Finished||started==0||
                !recorded.TryGetValue("exact storage setup delay",out var delay))return;
            var status=ColonyOwner.Status;
            if(status.CompletedFrames<started+30+(ulong)delay)return;
            _=RunOne(Current.Name,status);
        }
        private static bool StorageAt(int at,string label)=>Once(at+(recorded.TryGetValue("exact storage setup delay",out var delay)?(int)delay:0),label);
        // Fixed control: actual reservoir and prefab-built isolated pipe ports.
        // Intake 8 kg at 75/25 and 350 K; keep for 22 owner frames in 300 K oxygen so the
        // original element-chunk thermal service must do represented work.
        // Two separate outputs each have exactly 2 kg room. A later same-tick
        // trace/fractional/all-ID transfer tests physical storage, independently
        // of the still-gated live storage-biology and cold-rebind work.
        private static void MixedPipeOwnedStorage(int frame,bool verifyPendingHeat=false)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"exact storage arena")){exactReservoir=null;Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"build exact reservoir"))
            {
                exactReservoir=Build(LiquidReservoirConfig.ID,Cell(4,0));
                AccessTools.Field(typeof(ConduitConsumer),"isOn").SetValue(exactReservoir.GetComponent<ConduitConsumer>(),false);
                exactReservoir.GetComponent<ConduitDispenser>().isOn=false;
            }
            if(Once(7,"isolated exact reservoir ports and collection pipes"))
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(exactReservoir!)},new List<int>{OutputCell(exactReservoir!)},
                    new List<int>{Cell(1,5)},new List<int>{Cell(4,5)},new List<int>{Cell(7,5)});
            if(frame>=8&&step<=8)
            {
                if(!new[]{InputCell(exactReservoir!),OutputCell(exactReservoir!),Cell(1,5),Cell(4,5),Cell(7,5)}.All(flow.HasConduit))
                {Current.Stage="waiting for exact reservoir topology";if(frame>=30){Record("exact reservoir topology became ready",false,"readiness expired");Finish();}return;}
                recorded["exact storage setup delay"]=frame-8;
            }
            if(StorageAt(8,"admit warm exact reservoir mixture"))
            {
                ColonyPipeRuntime.Start();ColonyOwnedStorage.StartControl();
                ColonyConduits.SupplyControlInventory(flow,InputCell(exactReservoir!),Liquid(6,2,350,0));
                ColonyMachinePorts.Consume(exactReservoir!.GetComponent<ConduitConsumer>(),1,flow);
                var storage=exactReservoir.GetComponent<Storage>();var held=ColonyOwnedStorage.Inventory(storage);
                Record("actual reservoir intake retains the complete 75/25 mixture",held.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.Water).Id)==6&&
                    held.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id)==2&&ColonyOwnedStorage.Audit(storage),$"held {held.Mass:R}kg, native projections {storage.items.Count}");
            }
            var withdrawalDelay=0;
            if(verifyPendingHeat)
            {
                var readyAt=30+(recorded.TryGetValue("exact storage setup delay",out var setupDelay)?(int)setupDelay:0);
                if(frame>=readyAt&&step<=readyAt)
                {
                    // This separate regression needs an actual completed but
                    // uncredited heat segment. Wait with the outlet closed;
                    // sampling an already settled frame proves nothing.
                    if(ColonyOwnedStorage.PendingNativeHeat(exactReservoir!.GetComponent<Storage>()).Sign>=0)
                    {
                        Current.Stage="waiting for completed reservoir cooling before withdrawal";
                        if(frame>=readyAt+20){Record("completed reservoir cooling became observable",false,"no pending cooling within 20 owner frames");Finish();}
                        return;
                    }
                    recorded["exact storage transfer heat delay"]=frame-readyAt;
                }
                if(recorded.TryGetValue("exact storage transfer heat delay",out var observedDelay))withdrawalDelay=(int)observedDelay;
            }
            if(StorageAt(30+withdrawalDelay,"registered native heat and two capacity-limited withdrawals"))
            {
                var storage=exactReservoir!.GetComponent<Storage>();var water=ColonyConduits.MaterialOf(SimHashes.Water);var dirty=ColonyConduits.MaterialOf(SimHashes.DirtyWater);
                Record("owned reservoir keeps its native registered thermal contacts",ColonyOwnedStorage.RegisteredProjections(storage)==2&&ColonyOwnedStorage.HeatPublications>0&&
                    ColonyOwnedStorage.Heat(storage).Sign<0&&ColonyOwnedStorage.Audit(storage),$"bodies {ColonyOwnedStorage.RegisteredProjections(storage)}, heat {ColonyOwnedStorage.Heat(storage).Value:R}J, publications {ColonyOwnedStorage.HeatPublications}");
                var start=ColonyMachinePorts.FromStorage;
                var pending=verifyPendingHeat?ColonyOwnedStorage.PendingNativeHeat(storage):PipeAmount.Zero;
                var beforeHeat=ColonyOwnedStorage.Heat(storage);
                var beforeInventory=ColonyOwnedStorage.Inventory(storage);
                for(var withdrawal=1;withdrawal<=2;++withdrawal)
                {
                    ColonyConduits.SupplyControlInventory(flow,OutputCell(exactReservoir),PipeInventory.Single(water,8,300));
                    ColonyMachinePorts.Dispense(exactReservoir.GetComponent<ConduitDispenser>());
                    var output=ColonyMachinePorts.FromStorage;
                    if(verifyPendingHeat&&withdrawal==1)
                    {
                        var settled=ColonyOwnedStorage.Heat(storage).Minus(beforeHeat);
                        var returnedEnergy=output.Energy.Minus(start.Energy);
                        // This first draw takes 2 of the original 8 kg. It
                        // must carry its quarter of the already observed
                        // cooling, rather than leaving all that work behind.
                        var maximumReturnedEnergy=beforeInventory.Energy.Plus(pending).Times(.25);
                        Record("completed native cooling enters storage before its first withdrawal",pending.Sign<0&&settled.Units<=StorageJournalAmount.From(pending).Units&&
                            returnedEnergy.Minus(maximumReturnedEnergy).Sign<=0&&ColonyOwnedStorage.Audit(storage),
                            $"pending {pending.Value:R} J, settled {settled.Value:R} J; returned {returnedEnergy.Value:R} J, maximum with observed cooling {maximumReturnedEnergy.Value:R} J");
                    }
                    Record($"reservoir withdrawal {withdrawal} independently keeps 75/25",output.SpeciesMass(water.Id)-start.SpeciesMass(water.Id)==1.5*withdrawal&&
                        output.SpeciesMass(dirty.Id)-start.SpeciesMass(dirty.Id)==.5*withdrawal&&ColonyOwnedStorage.Audit(storage),
                        $"cumulative water {output.SpeciesMass(water.Id)-start.SpeciesMass(water.Id):R}, polluted {output.SpeciesMass(dirty.Id)-start.SpeciesMass(dirty.Id):R}kg");
                    ColonyConduits.TransferAll(flow,OutputCell(exactReservoir),Cell(withdrawal==1?1:7,5),double.MaxValue);
                }
                ColonyMachinePorts.Dispense(exactReservoir.GetComponent<ConduitDispenser>());
                ColonyConduits.TransferAll(flow,OutputCell(exactReservoir),Cell(4,5),double.MaxValue);
                Record("warm reservoir fully withdraws its actual inventory",ColonyOwnedStorage.Inventory(storage).Count==0&&ColonyOwnedStorage.Audit(storage),$"held {ColonyOwnedStorage.Inventory(storage).Mass:R}kg");
                var trace=PipeInventory.Single(water,3,300,0,.25).Joined(PipeInventory.Single(dirty,1,300,1,1e-240))
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.SaltWater),1e-240,300,2,.125));
                ColonyConduits.SupplyControlInventory(flow,InputCell(exactReservoir),trace);
                ColonyMachinePorts.Consume(exactReservoir.GetComponent<ConduitConsumer>(),1,flow);
                var held=ColonyOwnedStorage.Inventory(storage);
                Record("trace mass and all fractional germ identities physically enter storage",held.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.SaltWater).Id)==1e-240&&held.Germs.Length==3&&ColonyOwnedStorage.Audit(storage),
                    $"trace {held.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.SaltWater).Id):R}kg, identities {held.Germs.Length}");
                ColonyMachinePorts.Dispense(exactReservoir.GetComponent<ConduitDispenser>());
                var delivered=ColonyConduits.ReadSynced(flow,OutputCell(exactReservoir));
                bool Germ(int id,double count){foreach(var germ in delivered.Germs)if(germ.Id==id)return germ.Amount.Minus(new PipeAmount(count)).Sign==0;return false;}
                Record("trace and fractional all-ID passives physically return on withdrawal",delivered.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.SaltWater).Id)==1e-240&&
                    Germ(0,.25)&&Germ(1,1e-240)&&Germ(2,.125)&&ColonyOwnedStorage.Inventory(storage).Count==0&&ColonyOwnedStorage.Audit(storage),
                    $"output {delivered.Mass:R}kg, trace {delivered.SpeciesMass(ColonyConduits.MaterialOf(SimHashes.SaltWater).Id):R}kg, identities {delivered.Germs.Length}");
                Record("exact reservoir transfers keep every pipe account closed",ColonyConduits.Audit(out var detail),detail);
                Finish();
            }
        }
    }
}
