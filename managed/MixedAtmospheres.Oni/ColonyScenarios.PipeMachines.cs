using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Fixed before the first measurement (docs/mixed-pipes.md#machine-ports):
        // 8x6 oxygen arena (1.2 kg, 300 K). Frame 5: electrolyzer (1,0),
        // liquid reservoir (5,0), liquid vent (3,4); frame 7: one pipe cell on
        // each machine input, reservoir outlet -> vent route, isolated pipes
        // at (2,5) and (7,5). Readiness (by frame 30) shifts later stages.
        // Stage 8: the production runtime starts; supply electrolyzer 3 kg water
        // + 1 kg polluted water (germ 0: 4000), reservoir 6 + 3 kg (germ 0:
        // 9000), (7,5) 2 + 1 kg, all 300 K; (2,5) 2 + 0.5 kg at 265 K.
        // Stage 10: a duplicant-equivalent EmptyContents on (7,5). Stage 100:
        // recipe-only storage, exact wrong-element dump, reservoir output to an
        // at-rest liquid vent, one frozen-pipe event, empty ports, closed audit.
        private static readonly List<GameObject> machineBuildings=new List<GameObject>();
        private static bool MachineAt(int at,string label)=>Once(at+(recorded.TryGetValue("machine setup delay",out var delay)?(int)delay:0),label);
        private static int InputCell(GameObject building)=>(int)AccessTools.Field(typeof(ConduitConsumer),"utilityCell").GetValue(building.GetComponent<ConduitConsumer>());
        private static int OutputCell(GameObject building)=>(int)AccessTools.Field(typeof(ConduitDispenser),"utilityCell").GetValue(building.GetComponent<ConduitDispenser>());
        private static PipeInventory Liquid(double water,double dirty,double temperature,double germs)=>
            PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Water),water,temperature)
                .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.DirtyWater),dirty,temperature,0,germs));
        // Fixed before the first measurement: a new colony started with mixed
        // pipes (MA_COLONY_MIXED_PIPES=1, owner start) builds the prepared
        // base (water reservoir -> two CO2 scrubbers -> waste reservoirs, all
        // piped), keeps its four base checks, and runs to frame 400. Then the
        // reservoir has dispensed into owned pipes, the scrubbers' intakes
        // have stored water, no spill or job is stuck and every pipe account
        // closes exactly.
        private static void MixedPipeColony(int frame,OniNativeColony.Status status)
        {
            if(frame==0&&!recorded.ContainsKey("colony pipes"))
            {
                recorded["colony pipes"]=1;
                Record("the new colony started with owned mixed pipes",ColonyPipeRuntime.Active&&ColonyMachinePorts.Enabled,ColonyPipeRuntime.Report);
            }
            if(frame<=30)
            {
                Base(frame,status);
                if(Current.Finished){Current.Finished=false;Current.Stage="base built; running the piped colony";}
                return;
            }
            if(Once(400,"piped colony accounts"))
            {
                var water=ColonyConduits.MaterialOf(SimHashes.Water).Id;
                Record("the water reservoir dispensed into owned pipes",ColonyMachinePorts.FromStorage.SpeciesMass(water)>0,ColonyMachinePorts.Report);
                Record("the scrubbers stored piped water",ColonyMachinePorts.ToStorage.SpeciesMass(water)>0,$"to storage {ColonyMachinePorts.ToStorage.Mass:R} kg");
                Record("no spill or pipe job is stuck",ColonyPipeTeardown.ActiveSpills==0,$"spills {ColonyPipeTeardown.ActiveSpills}, pending {ColonyPipePorts.PendingCount}");
                Record("the piped colony closes every pipe account",ColonyConduits.Audit(out var detail),detail+"; "+ColonyPipeRuntime.Report);
                Record("the owner's ledgers stayed closed with mixed pipes",status.MaximumRelativeMassError<1e-9&&status.MaximumRelativeEnergyError<1e-9,
                    $"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}");
                Finish();
            }
        }
        private static void MixedPipeMachines(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"oxygen machine arena")){machineBuildings.Clear();Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"build electrolyzer, reservoir and liquid vent"))
            {machineBuildings.Add(Build(ElectrolyzerConfig.ID,Cell(1,0)));machineBuildings.Add(Build(LiquidReservoirConfig.ID,Cell(5,0)));machineBuildings.Add(Build(LiquidVentConfig.ID,Cell(3,4)));}
            if(Once(7,"pipe machine ports"))
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(machineBuildings[0])},new List<int>{InputCell(machineBuildings[1])},
                    Route(OutputCell(machineBuildings[1]),InputCell(machineBuildings[2])),new List<int>{Cell(2,5)},new List<int>{Cell(7,5)});
            if(frame>=8&&step<=8)
            {
                var cells=new[]{InputCell(machineBuildings[0]),InputCell(machineBuildings[1]),OutputCell(machineBuildings[1]),InputCell(machineBuildings[2]),Cell(2,5),Cell(7,5)};
                if(!cells.All(flow.HasConduit))
                {
                    Current.Stage="waiting for machine pipe topology";
                    if(frame>=30){Record("machine pipe topology became ready",false,"readiness expired");Finish();}
                    return;
                }
                recorded["machine setup delay"]=frame-8;
            }
            if(MachineAt(8,"start production mixed pipes and supply machine inputs"))
            {
                ColonyPipeRuntime.Start();
                ColonyConduits.SupplyControlInventory(flow,InputCell(machineBuildings[0]),Liquid(3,1,300,4000));
                ColonyConduits.SupplyControlInventory(flow,InputCell(machineBuildings[1]),Liquid(6,3,300,9000));
                ColonyConduits.SupplyControlInventory(flow,Cell(7,5),Liquid(2,1,300,0));
                ColonyConduits.SupplyControlInventory(flow,Cell(2,5),Liquid(2,.5,265,0));
                var pipe=Grid.Objects[Cell(2,5),(int)ObjectLayer.LiquidConduit];
                recorded["frozen pipe hp"]=pipe.GetComponent<BuildingHP>().HitPoints;recorded["phase events"]=ColonyConduits.PhaseEvents;
                Record("production runtime starts every pipe component",ColonyMachinePorts.Enabled&&ColonyPipeVents.Enabled&&ColonyPipeTeardown.Enabled&&ColonyPipePumps.Enabled,ColonyPipeRuntime.Report);
            }
            if(MachineAt(10,"empty one owned pipe cell by hand"))
            {
                var before=ColonyMachinePorts.ToStorage;var pipe=Grid.Objects[Cell(7,5),(int)ObjectLayer.LiquidConduit];
                AccessTools.Method(typeof(EmptyConduitWorkable),"EmptyContents").Invoke(pipe.GetComponent<EmptyConduitWorkable>(),null);
                ColonyConduits.TryRead(flow,Cell(7,5),out var left);var taken=ColonyMachinePorts.ToStorage.Total.Minus(before.Total);
                Record("emptying a mixed pipe makes one chunk per species from the exact contents",left.Count==0&&taken.Minus(new PipeAmount(3)).Sign==0,$"taken {taken.Value:R} kg; left {left.Mass:R} kg; {ColonyMachinePorts.Report}");
            }
            if(MachineAt(100,"machine ports, liquid vent and frozen pipe"))
            {
                var water=ColonyConduits.MaterialOf(SimHashes.Water).Id;var dirty=ColonyConduits.MaterialOf(SimHashes.DirtyWater).Id;
                var stored=ColonyMachinePorts.ToStorage;var dumped=ColonyMachinePorts.DumpedAt(Grid.PosToCell(machineBuildings[0]));
                Record("machines store only accepted species and dump the rest exactly",stored.SpeciesMass(water)==11&&stored.SpeciesMass(dirty)==4&&
                    dumped.Count==1&&dumped.SpeciesMass(dirty)==1&&ColonyMachinePorts.DamageEvents>0,
                    $"stored water {stored.SpeciesMass(water):R}, polluted {stored.SpeciesMass(dirty):R} kg; electrolyzer dumped {dumped.Mass:R} kg ({dumped.Count} species); {ColonyMachinePorts.DamageEvents} damage events");
                var reservoir=machineBuildings[1].GetComponent<Storage>();var vent=machineBuildings[2].GetComponent<Storage>();
                ColonyConduits.TryRead(flow,InputCell(machineBuildings[2]),out var ventPipe);
                Record("reservoir output reaches the world through the liquid vent",Math.Abs(ColonyMachinePorts.FromStorage.Mass-9)<=1e-5&&reservoir.MassStored()<=1e-5&&
                    ColonyPipeVents.Inventory(vent).Count==0&&ventPipe.Count==0,
                    $"from storage {ColonyMachinePorts.FromStorage.Mass:R} kg; reservoir {reservoir.MassStored():R} kg; vent {ColonyPipeVents.ControlDetails(vent)}; vent pipe {ventPipe.Mass:R} kg");
                var world=ColonyPipePorts.ToWorld;
                Record("world receives dump, frozen-pipe and vent mixtures",Math.Abs(world.Mass-12.5)<1e-5&&Math.Abs(world.SpeciesMass(water)-8)<1e-5&&Math.Abs(world.SpeciesMass(dirty)-4.5)<1e-5&&ColonyPipeTeardown.ActiveSpills==0&&ColonyPipePorts.PendingCount==0,
                    $"to world {world.Mass:R} kg (water {world.SpeciesMass(water):R}, polluted {world.SpeciesMass(dirty):R}); spills {ColonyPipeTeardown.ActiveSpills}; pending {ColonyPipePorts.PendingCount}");
                var pipe=Grid.Objects[Cell(2,5),(int)ObjectLayer.LiquidConduit];ColonyConduits.TryRead(flow,Cell(2,5),out var frozen);
                Record("a freezing mixture damages its pipe once and leaves it",frozen.Count==0&&ColonyConduits.PhaseEvents-recorded["phase events"]==1&&
                    pipe!=null&&pipe.GetComponent<BuildingHP>().HitPoints==recorded["frozen pipe hp"]-1,
                    $"pipe {frozen.Mass:R} kg; phase events {ColonyConduits.PhaseEvents-recorded["phase events"]:R}; hp {recorded["frozen pipe hp"]:R} -> {(pipe==null?-1:pipe.GetComponent<BuildingHP>().HitPoints)}");
                var boundary=ColonyMachinePorts.StorageBoundary;var bookedMass=boundary.Mass.Values.Aggregate(PipeAmount.Zero,(sum,value)=>sum.Plus(value));
                var bookedGerms=boundary.Germs.Values.Aggregate(PipeAmount.Zero,(sum,value)=>sum.Plus(value));
                Record("storage boundary books only native representation",Math.Abs(bookedMass.Value)<=1e-5&&Math.Abs(boundary.Energy.Value)<=1&&Math.Abs(bookedGerms.Value)<=20,boundary.Describe());
                Record("machine ports close every pipe account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
    }
}
