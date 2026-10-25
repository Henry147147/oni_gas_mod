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
        private static void MixedOwnedStorageReaders(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"exact storage inspection arena"))
            {storageReaderBuilding=null;storageReaderPhase=0;Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
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
                ColonyMachinePorts.Dispense(storageReaderBuilding.GetComponent<ConduitDispenser>());
                storageReaderPhase=2;storageReaderRenderFrame=Time.frameCount;
            }
            if(storageReaderPhase==2&&Time.frameCount>storageReaderRenderFrame)
            {
                var storage=storageReaderBuilding!.GetComponent<Storage>();var text=MixtureInspection.GetContainerInspectionText(storageReaderBuilding);
                Record("empty exact storage has an explicit empty reading after physical withdrawal",ColonyOwnedStorage.Inventory(storage).Count==0&&
                    text.Contains("Stored mixture")&&text.Contains("Empty"),text.Replace('\n','|'));
                Record("storage reader control preserves every pipe account",ColonyConduits.Audit(out var detail),detail);Finish();return;
            }
            if(frame>=80){Record("inspection observes later render frames",false,"render readiness expired");Finish();}
        }
    }
}
