using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // A reservoir gives out its stored mixture (user decision 2026-11-23,
        // docs/mixed-pipes.md#reservoirs). An 8x6 arena of thin oxygen (0.5 kg,
        // 300 K). Frame 6: a gas reservoir at (2,0) and a liquid reservoir at
        // (6,0) on the floor, a gas vent at (2,5) and a liquid vent at (6,5),
        // each reservoir's outlet piped straight up to its vent. Frame 20: the
        // liquid reservoir receives 60 kg of water at 300 K and 20 kg of
        // polluted water at 320 K; the gas reservoir 6 kg of oxygen, 3 kg of
        // carbon dioxide and 1 kg of hydrogen at 300 K. Frame 45, five pipe
        // ticks later: every packet in the two pipes holds the stored
        // proportions (75/25 and 60/30/10, to one part in 10,000), the liquid
        // packets are at the mixture's 305 K, and what is left in each
        // reservoir still has those proportions. Frame 160: both reservoirs
        // are empty, the world has received each constituent's whole amount
        // and every pipe account closes. The bounds were fixed before the
        // first run.
        private static readonly List<GameObject> mixBuildings=new List<GameObject>();
        private static readonly List<int> mixLiquidRoute=new List<int>(),mixGasRoute=new List<int>();
        private static double StoredOf(GameObject building,SimHashes element)=>building.GetComponent<Storage>().items.Where(i=>i!=null)
            .Select(i=>i.GetComponent<PrimaryElement>()).Where(p=>p!=null&&p.ElementID==element).Sum(p=>(double)p.Mass);
        // The packets on a route: how many there are and the largest deviation of a constituent's share from `shares`.
        private static (int packets,double worst,double temperature) MixPackets(ConduitFlow flow,List<int> route,(SimHashes element,double share)[] shares)
        {
            var packets=0;var worst=0.0;var temperature=0.0;
            foreach(var cell in route)
            {
                if(!ColonyConduits.TryRead(flow,cell,out var held)||held.Count==0)continue;
                ++packets;temperature=held.Temperature(ColonyConduits.Material);
                foreach(var (element,share) in shares)worst=Math.Max(worst,Math.Abs(held.SpeciesMass(ColonyConduits.MaterialOf(element).Id)/held.Mass-share));
                if(held.Count!=shares.Length)worst=Math.Max(worst,1);
            }
            return (packets,worst,temperature);
        }
        private static void ReservoirMix(int frame)
        {
            var liquids=Game.Instance.liquidConduitFlow;var gases=Game.Instance.gasConduitFlow;
            var liquidShares=new[]{(SimHashes.Water,.75),(SimHashes.DirtyWater,.25)};
            var gasShares=new[]{(SimHashes.Oxygen,.6),(SimHashes.CarbonDioxide,.3),(SimHashes.Hydrogen,.1)};
            if(Once(0,"thin oxygen arena")){mixBuildings.Clear();mixLiquidRoute.Clear();mixGasRoute.Clear();Arena((x,y)=>(SimHashes.Oxygen,.5f,300f));}
            if(Once(6,"build two reservoirs and their vents"))
            {
                mixBuildings.Add(Build(GasReservoirConfig.ID,Cell(2,0)));mixBuildings.Add(Build(LiquidReservoirConfig.ID,Cell(6,0)));
                mixBuildings.Add(Build(GasVentConfig.ID,Cell(2,5)));mixBuildings.Add(Build(LiquidVentConfig.ID,Cell(6,5)));
                var gas=mixBuildings[0].GetComponent<Building>().Def;var liquid=mixBuildings[1].GetComponent<Building>().Def;
                if(Grid.OffsetCell(Cell(2,0),gas.UtilityOutputOffset)!=Cell(2,0)||Grid.OffsetCell(Cell(2,0),gas.UtilityInputOffset)!=Cell(3,2)||
                    Grid.OffsetCell(Cell(6,0),liquid.UtilityOutputOffset)!=Cell(6,0)||Grid.OffsetCell(Cell(6,0),liquid.UtilityInputOffset)!=Cell(7,2))
                    throw new InvalidOperationException("The reservoirs' ports are not where the fixed routes expect them.");
                mixGasRoute.AddRange(Route(Cell(2,0),Cell(2,5)));mixLiquidRoute.AddRange(Route(Cell(6,0),Cell(6,5)));
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,mixGasRoute);Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,mixLiquidRoute);
            }
            if(Once(20,"fill the reservoirs with mixtures"))
            {
                foreach(var (element,_) in liquidShares.Concat(gasShares))recorded["world "+element]=ArenaSpecies(element);
                recorded["from storage"]=ColonyMachinePorts.Enabled?ColonyMachinePorts.FromStorage.Mass:double.NaN;
                var gas=mixBuildings[0].GetComponent<Storage>();var liquid=mixBuildings[1].GetComponent<Storage>();
                liquid.AddLiquid(SimHashes.Water,60f,300f,byte.MaxValue,0,false,false);liquid.AddLiquid(SimHashes.DirtyWater,20f,320f,byte.MaxValue,0,false,false);
                gas.AddGasChunk(SimHashes.Oxygen,6f,300f,byte.MaxValue,0,false,false);gas.AddGasChunk(SimHashes.CarbonDioxide,3f,300f,byte.MaxValue,0,false,false);
                gas.AddGasChunk(SimHashes.Hydrogen,1f,300f,byte.MaxValue,0,false,false);
            }
            if(Once(45,"packets in the pipes after five pipe ticks"))
            {
                Record("mixed pipes carry the scenario",ColonyPipeRuntime.Active&&ColonyMachinePorts.Enabled,ColonyPipeRuntime.StatusLine);
                var liquid=MixPackets(liquids,mixLiquidRoute,liquidShares);var gas=MixPackets(gases,mixGasRoute,gasShares);
                Record("every liquid packet holds the reservoir's 75/25 mixture",liquid.packets>=3&&liquid.worst<=1e-4,$"{liquid.packets} packets, largest deviation of a share {liquid.worst:G3}");
                Record("the liquid packets are at the mixture's temperature",liquid.packets>=1&&Math.Abs(liquid.temperature-305)<=.05,$"{liquid.temperature:F3} K (water 300 K, polluted water 320 K)");
                Record("every gas packet holds the reservoir's 60/30/10 mixture",gas.packets>=3&&gas.worst<=1e-4,$"{gas.packets} packets, largest deviation of a share {gas.worst:G3}");
                var water=StoredOf(mixBuildings[1],SimHashes.Water);var dirty=StoredOf(mixBuildings[1],SimHashes.DirtyWater);
                var oxygen=StoredOf(mixBuildings[0],SimHashes.Oxygen);var co2=StoredOf(mixBuildings[0],SimHashes.CarbonDioxide);var hydrogen=StoredOf(mixBuildings[0],SimHashes.Hydrogen);
                Record("what stays in the reservoirs keeps its proportions",water+dirty>0&&water+dirty<80&&Math.Abs(water/(water+dirty)-.75)<=1e-5&&
                    oxygen+co2+hydrogen>0&&oxygen+co2+hydrogen<10&&Math.Abs(oxygen/(oxygen+co2+hydrogen)-.6)<=1e-5&&Math.Abs(hydrogen/(oxygen+co2+hydrogen)-.1)<=1e-5,
                    $"liquid {water:R} + {dirty:R} kg, gas {oxygen:R} + {co2:R} + {hydrogen:R} kg");
            }
            if(Once(160,"both reservoirs emptied into the world"))
            {
                var output=ColonyMachinePorts.Enabled?ColonyMachinePorts.FromStorage.Mass-recorded["from storage"]:double.NaN;
                Record("both reservoirs gave out everything they held",HeldMass(mixBuildings[0])<=1e-5&&HeldMass(mixBuildings[1])<=1e-5&&Math.Abs(output-90)<=1e-3,
                    $"gas reservoir {HeldMass(mixBuildings[0]):R} kg, liquid reservoir {HeldMass(mixBuildings[1]):R} kg, reservoir outputs {output:R} kg of 90");
                var gains=liquidShares.Concat(gasShares).Select(s=>(s.Item1,gain:ArenaSpecies(s.Item1)-recorded["world "+s.Item1])).ToArray();
                var expected=new Dictionary<SimHashes,double>{{SimHashes.Water,60},{SimHashes.DirtyWater,20},{SimHashes.Oxygen,6},{SimHashes.CarbonDioxide,3},{SimHashes.Hydrogen,1}};
                Record("the world received each constituent's whole amount",gains.All(g=>Math.Abs(g.gain-expected[g.Item1])<=1e-3),string.Join(", ",gains.Select(g=>$"{g.Item1} {g.gain:F4} of {expected[g.Item1]} kg")));
                var detail="single-element packets";
                Record("the reservoir run closes every pipe account",!ColonyPipeRuntime.Active||ColonyConduits.Audit(out detail),detail);
                Finish();
            }
        }
    }
}
