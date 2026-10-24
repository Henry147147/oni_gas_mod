using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Fixed before the first measurement: 8x6 oxygen arena (1.2 kg, 300 K).
        // Frame 5: liquid conditioner (1,0), conduction panel bridge (5,2).
        // Frame 7: one pipe cell on the conditioner input, its output routed
        // to (0,4) with no consumer, one cell on each panel port, an isolated
        // liquid pipe (7,5) and gas pipe (3,5). Readiness by frame 30 shifts
        // later stages. Stage 8: production runtime; conditioner input 6 kg
        // water + 2 kg polluted water at 300 K; panel input 5 kg water 350 K;
        // (7,5) 5 kg water + 1 kg polluted water at 380 K; (3,5) 0.5 kg steam
        // + 0.5 kg oxygen at 360 K. Stage 60: the conditioner moved the whole
        // mixture into its output at 14 K less (storage boundary within 5%
        // of the mixture's 14 K heat), the panel moved the water and gave its
        // body exactly the heat the mixture lost, the boiling and condensing
        // pipes each took one phase event and emptied, every account closes.
        private static readonly List<GameObject> heatBuildings=new List<GameObject>();
        private static bool HeatAt(int at,string label)=>Once(at+(recorded.TryGetValue("heat setup delay",out var delay)?(int)delay:0),label);
        private static void MixedPipeHeatMachines(int frame)
        {
            var liquid=Game.Instance.liquidConduitFlow;var gas=Game.Instance.gasConduitFlow;
            if(Once(0,"oxygen heat-machine arena")){heatBuildings.Clear();Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"build conditioner and conduction panel"))
            {heatBuildings.Add(Build(LiquidConditionerConfig.ID,Cell(1,0)));heatBuildings.Add(Build(ContactConductivePipeBridgeConfig.ID,Cell(5,2)));}
            if(Once(7,"pipe heat machines"))
            {
                var panel=heatBuildings[1].GetSMI<ContactConductivePipeBridge.Instance>();
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(heatBuildings[0])},Route(OutputCell(heatBuildings[0]),Cell(0,4)),
                    new List<int>{panel.inputCell},new List<int>{panel.outputCell},new List<int>{Cell(7,5)});
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(3,5)});
            }
            if(frame>=8&&step<=8)
            {
                var panel=heatBuildings[1].GetSMI<ContactConductivePipeBridge.Instance>();
                if(!new[]{InputCell(heatBuildings[0]),OutputCell(heatBuildings[0]),Cell(0,4),panel.inputCell,panel.outputCell,Cell(7,5)}.All(liquid.HasConduit)||!gas.HasConduit(Cell(3,5)))
                {
                    Current.Stage="waiting for heat-machine pipe topology";
                    if(frame>=30){Record("heat-machine pipe topology became ready",false,"readiness expired");Finish();}
                    return;
                }
                recorded["heat setup delay"]=frame-8;
            }
            if(HeatAt(8,"start mixed pipes and supply hot, cold and boiling mixtures"))
            {
                ColonyPipeRuntime.Start();var panel=heatBuildings[1].GetSMI<ContactConductivePipeBridge.Instance>();
                ColonyConduits.SupplyControlInventory(liquid,InputCell(heatBuildings[0]),Liquid(6,2,300,0));
                ColonyConduits.SupplyControlInventory(liquid,panel.inputCell,PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Water),5,350));
                ColonyConduits.SupplyControlInventory(liquid,Cell(7,5),Liquid(5,1,380,0));
                ColonyConduits.SupplyControlInventory(gas,Cell(3,5),PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Steam),.5,360).Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Oxygen),.5,360)));
                recorded["heat phase events"]=ColonyConduits.PhaseEvents;
                var water=ColonyConduits.MaterialOf(SimHashes.Water);var dirty=ColonyConduits.MaterialOf(SimHashes.DirtyWater);
                recorded["conditioner heat"]=14*(6*water.HeatCapacity+2*dirty.HeatCapacity);recorded["heat boundary"]=ColonyMachinePorts.StorageBoundary.Energy.Value;
                recorded["boil hp"]=Grid.Objects[Cell(7,5),(int)ObjectLayer.LiquidConduit].GetComponent<BuildingHP>().HitPoints;
                recorded["condense hp"]=Grid.Objects[Cell(3,5),(int)ObjectLayer.GasConduit].GetComponent<BuildingHP>().HitPoints;
            }
            if(HeatAt(60,"conditioner, panel and pipe phase changes"))
            {
                var water=ColonyConduits.MaterialOf(SimHashes.Water).Id;var dirty=ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id;
                var output=PipeInventory.Empty;foreach(var cell in Route(OutputCell(heatBuildings[0]),Cell(0,4))){ColonyConduits.TryRead(liquid,cell,out var held);output=output.Joined(held);}
                var pumped=ColonyMachinePorts.StorageBoundary.Energy.Value-recorded["heat boundary"];
                Record("the conditioner moves the whole mixture 14 K colder",Math.Abs(output.Mass-8)<1e-5&&output.SpeciesMass(water)>5.99&&output.SpeciesMass(dirty)>1.99&&
                    Math.Abs(pumped-recorded["conditioner heat"])<=.05*recorded["conditioner heat"],
                    $"output {output.Mass:R} kg at {(output.Count>0?output.Temperature(ColonyConduits.Material):0):R} K; pumped {pumped:R} J vs {recorded["conditioner heat"]:R} J; {ColonyMachinePorts.Report}");
                var panel=heatBuildings[1].GetSMI<ContactConductivePipeBridge.Instance>();ColonyConduits.TryRead(liquid,panel.outputCell,out var passed);ColonyConduits.TryRead(liquid,panel.inputCell,out var left);
                Record("the conduction panel passes the water and books its body heat",ColonyPipeSpecialEdges.Panels>0&&ColonyPipeSpecialEdges.PanelHeat.Sign<0&&left.Count==0,
                    $"{ColonyPipeSpecialEdges.Panels} parcels; fluid heat {ColonyPipeSpecialEdges.PanelHeat.Value:R} J; output {passed.Mass:R} kg; input left {left.Mass:R} kg");
                ColonyConduits.TryRead(liquid,Cell(7,5),out var boiled);ColonyConduits.TryRead(gas,Cell(3,5),out var condensed);
                var boilHp=Grid.Objects[Cell(7,5),(int)ObjectLayer.LiquidConduit].GetComponent<BuildingHP>().HitPoints;
                var condenseHp=Grid.Objects[Cell(3,5),(int)ObjectLayer.GasConduit].GetComponent<BuildingHP>().HitPoints;
                Record("boiling and condensing mixtures each damage their pipe once and leave it",boiled.Count==0&&condensed.Count==0&&
                    ColonyConduits.PhaseEvents-recorded["heat phase events"]==2&&boilHp==recorded["boil hp"]-1&&condenseHp==recorded["condense hp"]-1,
                    $"liquid {boiled.Mass:R} kg, gas {condensed.Mass:R} kg; events {ColonyConduits.PhaseEvents-recorded["heat phase events"]:R}; hp {recorded["boil hp"]:R}->{boilHp}, {recorded["condense hp"]:R}->{condenseHp}");
                Record("heat machines close every pipe account",ColonyConduits.Audit(out var detail)&&ColonyPipeTeardown.ActiveSpills==0&&ColonyPipePorts.PendingCount==0,detail+"; "+ColonyPipeRuntime.Report);Finish();
            }
        }
    }
}
