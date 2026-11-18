using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Pipe throughput (docs/mixed-pipes.md#throughput). A granite wall at
        // x = 3 divides the 8x6 arena. Left of it is a pool (water, 990 kg a
        // cell, 300 K, four rows) under oxygen; right of it oxygen (1.2 kg,
        // 300 K). Frame 6: a liquid pump at (1,0) in the pool and liquid
        // reservoirs A at (4,0) and B at (6,0) on the floor; the pump's outlet
        // is piped over the wall to A's inlet (9 cells) and A's outlet to B's
        // inlet (6 cells); B's outlet stays open, so B only fills. From frame
        // 150 to frame 400 (50 pipe ticks of one second) each stage must pass
        // at least 475 kg, 95 % of a liquid pipe's 10 kg a second: the pump's
        // draw from the pool, A's output and B's intake. The bounds were fixed
        // before the first measurement. That run, on the build of the Windows
        // playtest of 2026-11-18, measured frames 60 to 310 and showed the
        // recording's fault: A passed 250 kg, a packet every other tick. The
        // window then moved to 150, after both pipes have filled, so that
        // filling the outlet pipe does not count against B's intake.
        private static readonly List<GameObject> throughputBuildings=new List<GameObject>();
        private static readonly List<int> throughputOutlet=new List<int>();
        private static string ThroughputPort(GameObject building)
        {
            var dispenser=building.GetComponent<ConduitDispenser>();var cell=OutputCell(building);Grid.CellToXY(cell,out var x,out var y);
            return $"outlet at {x-x0},{y-y0} (route starts {(throughputOutlet.Count>0&&throughputOutlet[0]==cell?"there":"elsewhere")}), connected {dispenser.IsConnected}, on {dispenser.isOn}, blocked {dispenser.blocked}, empty {dispenser.empty}; {MachineState(building)}";
        }
        private static void PipeThroughput(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"pool arena"))
            {
                throughputBuildings.Clear();throughputOutlet.Clear();
                Arena((x,y)=>x==3?(SimHashes.Granite,1000f,300f):x<3&&y<=3?(SimHashes.Water,990f,300f):(SimHashes.Oxygen,1.2f,300f));
            }
            if(Once(6,"build pump and two reservoirs"))
            {
                recorded["water"]=ArenaSpecies(SimHashes.Water);
                throughputBuildings.Add(Build(LiquidPumpConfig.ID,Cell(1,0)));
                throughputBuildings.Add(Build(LiquidReservoirConfig.ID,Cell(4,0)));throughputBuildings.Add(Build(LiquidReservoirConfig.ID,Cell(6,0)));
                var pumpOut=Grid.OffsetCell(Cell(1,0),throughputBuildings[0].GetComponent<Building>().Def.UtilityOutputOffset);
                var tank=throughputBuildings[1].GetComponent<Building>().Def;
                if(pumpOut!=Cell(2,1)||Grid.OffsetCell(Cell(4,0),tank.UtilityInputOffset)!=Cell(5,2)||Grid.OffsetCell(Cell(4,0),tank.UtilityOutputOffset)!=Cell(4,0))
                    throw new InvalidOperationException("The throughput machines' ports are not where the fixed routes expect them.");
                // The routes share no cell and neither touches B's outlet.
                var first=new List<int>{Cell(2,1),Cell(2,2),Cell(2,3),Cell(2,4),Cell(3,4),Cell(4,4),Cell(5,4),Cell(5,3),Cell(5,2)};
                var second=new List<int>{Cell(4,0),Cell(4,1),Cell(5,1),Cell(6,1),Cell(7,1),Cell(7,2)};
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,first,second);throughputOutlet.AddRange(second);
            }
            if(Once(150,"start of the measured window"))
            {
                recorded["drawn"]=recorded["water"]-ArenaSpecies(SimHashes.Water);
                recorded["from storage"]=ColonyMachinePorts.Enabled?ColonyMachinePorts.FromStorage.Mass:double.NaN;
                recorded["a"]=HeldMass(throughputBuildings[1]);recorded["b"]=HeldMass(throughputBuildings[2]);
            }
            if(Once(400,"throughput over 50 pipe ticks"))
            {
                var drawn=recorded["water"]-ArenaSpecies(SimHashes.Water)-recorded["drawn"];
                var a=HeldMass(throughputBuildings[1]);var b=HeldMass(throughputBuildings[2]);
                var output=ColonyMachinePorts.Enabled?ColonyMachinePorts.FromStorage.Mass-recorded["from storage"]:double.NaN;
                var pattern=string.Join(" ",throughputOutlet.Select(c=>flow.GetContents(c).mass.ToString("0.##")));
                Record("mixed pipes carry the scenario",ColonyPipeRuntime.Active&&ColonyMachinePorts.Enabled,ColonyPipeRuntime.StatusLine);
                Record("the pump draws a full pipe from the pool",drawn>=475&&drawn<=510,$"{drawn:F1} kg in 50 s; {MachineState(throughputBuildings[0])}");
                Record("a reservoir passes on a full pipe",b-recorded["b"]>=475&&b-recorded["b"]<=510&&!(output<475),
                    $"B gained {b-recorded["b"]:F1} kg in 50 s, all machine outputs {output:F1} kg, A holds {a:F1} kg (was {recorded["a"]:F1}); outlet pipe kg by cell: {pattern}; {ThroughputPort(throughputBuildings[1])}");
                var detail="single-element packets";
                Record("the throughput run closes every pipe account",!ColonyPipeRuntime.Active||ColonyConduits.Audit(out detail),detail);
                Finish();
            }
        }
    }
}
