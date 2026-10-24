using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Fixed before the first measurement: oxygen arena; frame 5 an
        // isolated liquid pipe (3,3) and gas pipe (5,3); readiness by frame 30.
        // Stage 8: production runtime; liquid 3 kg water + 1 kg polluted water
        // + 0.003 kg salt water (a 0.075% trace) at 300 K with 1000 germs;
        // gas 0.5 kg oxygen + 0.5 kg CO2 at 300 K. Stage 10: the pipe status
        // line (hover/details) names every non-trace species with its share
        // and marks traces; the details inspection lists all three species.
        private static bool ReadersAt(int at,string label)=>Once(at+(recorded.TryGetValue("readers setup delay",out var delay)?(int)delay:0),label);
        private static void MixedPipeReaders(int frame)
        {
            var liquid=Game.Instance.liquidConduitFlow;var gas=Game.Instance.gasConduitFlow;
            if(Once(0,"reader arena"))Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));
            if(Once(5,"isolated reader pipes"))
            {Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{Cell(3,3)});Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(5,3)});}
            if(frame>=8&&step<=8)
            {
                if(!liquid.HasConduit(Cell(3,3))||!gas.HasConduit(Cell(5,3)))
                {
                    Current.Stage="waiting for reader pipe topology";
                    if(frame>=30){Record("reader pipe topology became ready",false,"readiness expired");Finish();}
                    return;
                }
                recorded["readers setup delay"]=frame-8;
            }
            if(ReadersAt(8,"supply mixtures with a trace"))
            {
                ColonyPipeRuntime.Start();
                ColonyConduits.SupplyControlInventory(liquid,Cell(3,3),Liquid(3,1,300,1000).Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.SaltWater),.003,300)));
                ColonyConduits.SupplyControlInventory(gas,Cell(5,3),PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Oxygen),.5,300).Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.CarbonDioxide),.5,300)));
            }
            if(ReadersAt(10,"pipe status lines and details"))
            {
                string Name(SimHashes id)=>ElementLoader.FindElementByHash(id).name;
                var status=Db.Get().BuildingStatusItems.Pipe;
                var liquidPipe=Grid.Objects[Cell(3,3),(int)ObjectLayer.LiquidConduit];var gasPipe=Grid.Objects[Cell(5,3),(int)ObjectLayer.GasConduit];
                var liquidLine=status.resolveStringCallback("{Contents}",liquidPipe.GetComponent<Conduit>());
                var gasLine=status.resolveStringCallback("{Contents}",gasPipe.GetComponent<Conduit>());
                Record("the liquid pipe status names both species with shares and marks the trace",liquidLine.Contains(Name(SimHashes.Water)+" 74.9")&&liquidLine.Contains(Name(SimHashes.DirtyWater)+" 25")&&
                    liquidLine.Contains("traces")&&!liquidLine.Contains(Name(SimHashes.SaltWater)),liquidLine);
                Record("the gas pipe status names both species",gasLine.Contains(Name(SimHashes.Oxygen)+" 50%")&&gasLine.Contains(Name(SimHashes.CarbonDioxide)+" 50%"),gasLine);
                var details=MixtureInspection.GetContainerInspectionText(liquidPipe);
                Record("the pipe details list every species including the trace",details.Contains(Name(SimHashes.Water))&&details.Contains(Name(SimHashes.DirtyWater))&&
                    details.Contains(Name(SimHashes.SaltWater))&&details.Contains("trace"),details.Replace('\n','|'));
                Record("reading pipes changes no account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
    }
}
