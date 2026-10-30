using System;
using System.Collections.Generic;
using System.Globalization;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? storageReaderBuilding;
        private static int storageReaderPhase,storageReaderRenderFrame;
        private static string storageReaderDispenseDetail=string.Empty;
        private static void MixedOwnedStorageReaders(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"exact storage inspection arena"))
            {storageReaderBuilding=null;storageReaderPhase=0;storageReaderDispenseDetail=string.Empty;Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"build an exact reservoir for inspection"))
            {
                storageReaderBuilding=Build(LiquidReservoirConfig.ID,Cell(4,0));
                AccessTools.Field(typeof(ConduitConsumer),"isOn").SetValue(storageReaderBuilding.GetComponent<ConduitConsumer>(),false);
                storageReaderBuilding.GetComponent<ConduitDispenser>().isOn=false;
            }
            if(Once(7,"isolated inspection reservoir ports"))
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(storageReaderBuilding!)},
                    new List<int>{OutputCell(storageReaderBuilding!)});
            if(frame>=8&&storageReaderPhase==0)
            {
                if(!flow.HasConduit(InputCell(storageReaderBuilding!))||!flow.HasConduit(OutputCell(storageReaderBuilding!)))
                {Current.Stage="waiting for inspection reservoir topology";if(frame>=30){Record("inspection topology became ready",false,"readiness expired");Finish();}return;}
                ColonyPipeRuntime.Start();ColonyOwnedStorage.StartControl();
                var parcel=PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Water),3,300,0,.25)
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.DirtyWater),1,300,1,1e-240))
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.SaltWater),1e-240,300,2,.125));
                ColonyConduits.SupplyControlInventory(flow,InputCell(storageReaderBuilding!),parcel);
                ColonyMachinePorts.Consume(storageReaderBuilding!.GetComponent<ConduitConsumer>(),1,flow);
                storageReaderPhase=1;storageReaderRenderFrame=Time.frameCount;
            }
            // Presentation caches a coherent Unity render frame. Read the
            // newly completed physical inventory on a subsequent render.
            if(storageReaderPhase==1&&Time.frameCount>storageReaderRenderFrame)
            {
                var readyStorage=storageReaderBuilding!.GetComponent<Storage>();
                var readyDispenser=storageReaderBuilding.GetComponent<ConduitDispenser>();
                var readyOperation=(Operational)AccessTools.Field(typeof(ConduitDispenser),"operational").GetValue(readyDispenser);
                if(!readyDispenser.IsConnected||(!readyDispenser.alwaysDispense&&(readyOperation==null||!readyOperation.IsOperational))||
                    !ColonyOwnedStorage.ThermalProjectionsRegistered(readyStorage))
                {
                    Current.Stage="waiting for native storage and outlet readiness";
                    if(frame>=80){Record("inspection observes later render frames",false,"native storage/outlet readiness expired");Finish();}
                    return;
                }
                var storage=storageReaderBuilding!.GetComponent<Storage>();var before=ColonyOwnedStorage.Inventory(storage);
                var heat=ColonyOwnedStorage.Heat(storage);var publications=ColonyOwnedStorage.HeatPublications;
                var text=MixtureInspection.GetContainerInspectionText(storageReaderBuilding);
                bool All=true;
                foreach(var part in before.Parts)
                    All&=text.Contains(ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id)).name);
                Record("exact reservoir details list every physically held species including an unprojectable trace",before.Count==3&&All&&text.Contains("trace")&&
                    text.Contains("1E-240"),text.Replace('\n','|'));
                foreach(var germ in before.Germs)
                    All&=text.Contains(GameUtil.GetFormattedDiseaseName((byte)germ.Id,false)+": "+germ.Mass.ToString("G6",CultureInfo.InvariantCulture));
                Record("exact reservoir details list all fractional and minority germ identities",before.Germs.Length==3&&All,text.Replace('\n','|'));
                for(var i=0;i<100;++i)
                    if(MixtureInspection.GetContainerInspectionText(storageReaderBuilding)!=text)throw new InvalidOperationException("Repeated exact inventory inspection changed its completed reading.");
                Record("repeated exact storage inspection changes no inventory heat or account",ReferenceEquals(before,ColonyOwnedStorage.Inventory(storage))&&
                    heat.Minus(ColonyOwnedStorage.Heat(storage)).Sign==0&&publications==ColonyOwnedStorage.HeatPublications&&ColonyOwnedStorage.Audit(storage),
                    $"100 reads; {before.Mass:R} kg; {before.Count} species; {before.Germs.Length} germ identities; physical publications {publications}");
                var dispenser=storageReaderBuilding.GetComponent<ConduitDispenser>();
                var cell=(int)AccessTools.Field(typeof(ConduitDispenser),"utilityCell").GetValue(dispenser);
                var operational=(Operational)AccessTools.Field(typeof(ConduitDispenser),"operational").GetValue(dispenser);
                var expectedCell=(int)AccessTools.Method(typeof(ConduitDispenser),"GetOutputCell").Invoke(dispenser,new object[]{flow.conduitType});
                var pipeObject=Grid.IsValidCell(cell)?Grid.Objects[cell,(int)ObjectLayer.LiquidConduit]:null;
                var outputBefore=ColonyMachinePorts.FromStorage.Mass;
                storageReaderDispenseDetail=$"before: ports {ColonyMachinePorts.Enabled}, type {dispenser.conduitType}/{flow.conduitType}, pipe object {pipeObject!=null}, complete {pipeObject!=null&&pipeObject.GetComponent<BuildingComplete>()!=null}, owned {Grid.IsValidCell(cell)&&ColonyConduits.Owns(flow,cell)}, cell {cell}/expected {expectedCell}, conduit {Grid.IsValidCell(cell)&&flow.HasConduit(cell)}, connected {Grid.IsValidCell(cell)&&dispenser.IsConnected}, operational {operational?.IsOperational}, always {dispenser.alwaysDispense}, on {dispenser.isOn}, blocked {dispenser.blocked}, empty {dispenser.empty}; held {before.Mass:R} kg/{before.Count} species";
                var handled=ColonyMachinePorts.Dispense(dispenser);
                var after=ColonyOwnedStorage.Inventory(storage);
                storageReaderDispenseDetail+=$"; after: handled {handled}, blocked {dispenser.blocked}, empty {dispenser.empty}; held {after.Mass:R} kg/{after.Count} species; dispensed {ColonyMachinePorts.FromStorage.Mass-outputBefore:R} kg";
                storageReaderPhase=2;storageReaderRenderFrame=Time.frameCount;
            }
            if(storageReaderPhase==2&&Time.frameCount>storageReaderRenderFrame)
            {
                var storage=storageReaderBuilding!.GetComponent<Storage>();var text=MixtureInspection.GetContainerInspectionText(storageReaderBuilding);
                Record("empty exact storage has an explicit empty reading after physical withdrawal",ColonyOwnedStorage.Inventory(storage).Count==0&&
                    text.Contains("Stored mixture")&&text.Contains("Empty"),text.Replace('\n','|')+"; "+storageReaderDispenseDetail);
                Record("storage reader control preserves every pipe account",ColonyConduits.Audit(out var detail),detail);Finish();return;
            }
            if(frame>=80){Record("inspection observes later render frames",false,"render readiness expired");Finish();}
        }
    }
}
