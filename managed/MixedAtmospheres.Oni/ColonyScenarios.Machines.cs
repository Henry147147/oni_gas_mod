using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Machines on mixtures (docs/colony-owner.md#machines): each takes only the
    // element its recipe uses from a mixed atmosphere, its world outputs reach
    // the owner, and a blocked output takes neither inputs nor power.
    internal static partial class ColonyScenarios
    {
        private static GameObject? machine;
        // The arena's gas as a mixture: the fill's gas plus an added gas in
        // every cell (the owner holds both in each cell).
        private static void Mix(SimHashes added, float kilograms)
        {
            for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                if (!Grid.Solid[Cell(x, y)])
                    SimMessages.AddRemoveSubstance(Cell(x, y), added, CellEventLogger.Instance.OxygenModifierSimUpdate, kilograms, 300, byte.MaxValue, 0);
        }
        private static void Store(GameObject building, SimHashes element, float mass)
        {
            var storage = building.GetComponent<Storage>(); var e = ElementLoader.FindElementByHash(element);
            if (e.IsLiquid) storage.AddLiquid(element, mass, 300, byte.MaxValue, 0, false, true);
            else if (e.IsGas) storage.AddGasChunk(element, mass, 300, byte.MaxValue, 0, false, true);
            else storage.AddOre(element, mass, 300, byte.MaxValue, 0, false, true);
        }
        // A machine with a pipe output runs only while a pipe is connected
        // there: one segment, which its output then fills.
        private static readonly System.Reflection.FieldInfo DispenserCell = HarmonyLib.AccessTools.Field(typeof(ConduitDispenser), "utilityCell");
        private static void ConnectOutput(GameObject building)
        {
            foreach (var dispenser in building.GetComponents<ConduitDispenser>())
            {
                var gas = dispenser.conduitType == ConduitType.Gas;
                var cell = (int)DispenserCell.GetValue(dispenser);
                if (!Grid.IsValidCell(cell)) cell = building.GetComponent<Building>().GetUtilityOutputCell();
                if (Grid.IsValidCell(cell))
                    Pipe(gas ? Game.Instance.gasConduitSystem : Game.Instance.liquidConduitSystem, gas ? GasConduitConfig.ID : LiquidConduitConfig.ID, new System.Collections.Generic.List<int> { cell });
            }
        }
        private static float Stored(GameObject? building, SimHashes element) =>
            building == null ? 0 : building.GetComponents<Storage>().Sum(s => s.GetMassAvailable(element));
        private static string Change(string label, double before, double after) => $"{label} {before:G5} -> {after:G5} kg";

        // A world-input converter in a two-gas mixture: it draws only its own
        // element, and its outputs go where its recipe sends them.
        private static void MixedConverter(int frame, string id, SimHashes fill, SimHashes added, SimHashes taken, SimHashes untouched,
            (SimHashes element, float mass)[] inputs, SimHashes? worldOutput, SimHashes? storedOutput, int checkFrame = 60)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (fill, .8f, 300f));
            if (Once(3, "mix")) Mix(added, .4f);
            if (Once(8, "build"))
            {
                recorded["taken"] = ArenaSpecies(taken); recorded["untouched"] = ArenaSpecies(untouched);
                recorded["output"] = worldOutput.HasValue ? ArenaSpecies(worldOutput.Value) : 0;
                machine = Build(id, Cell(3, 1));
                foreach (var (element, mass) in inputs) Store(machine, element, mass);
                ConnectOutput(machine);
            }
            if (Once(checkFrame, "check"))
            {
                var taken0 = recorded["taken"]; var taken1 = ArenaSpecies(taken); var other0 = recorded["untouched"]; var other1 = ArenaSpecies(untouched);
                var outputDetail = "";
                var outputs = true;
                if (worldOutput.HasValue)
                {
                    var o1 = ArenaSpecies(worldOutput.Value);
                    outputs &= o1 > recorded["output"] + 1e-3; outputDetail += Change(worldOutput.Value.ToString(), recorded["output"], o1) + "; ";
                }
                if (storedOutput.HasValue) { var s = Stored(machine, storedOutput.Value); outputs &= s > 1e-3; outputDetail += $"stored {storedOutput.Value} {s:G4} kg; "; }
                var otherTaken = worldOutput == untouched ? 0 : other0 - other1;
                Record("the machine took only its element from the mixture", taken1 < taken0 - 1e-3 && Math.Abs(otherTaken) < 1e-6 * Math.Max(1, other0),
                    $"{Change(taken.ToString(), taken0, taken1)}, {Change(untouched.ToString(), other0, other1)}; {MachineState(machine)}");
                Record("its outputs went where its recipe sends them", outputs, outputDetail + MachineState(machine));
                Finish();
            }
        }

        private static void Skimmer(int frame) => MixedConverter(frame, CO2ScrubberConfig.ID, SimHashes.Oxygen, SimHashes.CarbonDioxide,
            SimHashes.CarbonDioxide, SimHashes.Oxygen, new[] { (SimHashes.Water, 200f) }, null, SimHashes.DirtyWater);
        private static void Terrarium(int frame) => MixedConverter(frame, AlgaeHabitatConfig.ID, SimHashes.Oxygen, SimHashes.CarbonDioxide,
            SimHashes.CarbonDioxide, SimHashes.Hydrogen, new[] { (SimHashes.Algae, 100f), (SimHashes.Water, 200f) }, SimHashes.Oxygen, SimHashes.DirtyWater, 90);
        private static void Deodorizer(int frame) => MixedConverter(frame, AirFilterConfig.ID, SimHashes.Oxygen, SimHashes.ContaminatedOxygen,
            SimHashes.ContaminatedOxygen, SimHashes.CarbonDioxide, new[] { (SimHashes.Sand, 100f) }, SimHashes.Oxygen, null);
        // A rust deoxidizer adds oxygen and chlorine to a mixture it takes nothing from.
        private static void RustDeoxidizer(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.CarbonDioxide, .8f, 300f));
            if (Once(3, "mix")) Mix(SimHashes.Hydrogen, .1f);
            if (Once(8, "build"))
            {
                foreach (var e in new[] { SimHashes.Oxygen, SimHashes.ChlorineGas, SimHashes.CarbonDioxide, SimHashes.Hydrogen }) recorded[e.ToString()] = ArenaSpecies(e);
                machine = Build(RustDeoxidizerConfig.ID, Cell(3, 1)); Store(machine, SimHashes.Rust, 100); Store(machine, SimHashes.Salt, 100);
            }
            if (Once(60, "check"))
            {
                double Delta(SimHashes e) => ArenaSpecies(e) - recorded[e.ToString()];
                Record("oxygen and chlorine reached the owner", Delta(SimHashes.Oxygen) > 1e-3 && Delta(SimHashes.ChlorineGas) > 1e-4,
                    $"oxygen +{Delta(SimHashes.Oxygen):G4} kg, chlorine +{Delta(SimHashes.ChlorineGas):G4} kg; {MachineState(machine)}");
                Record("the mixture's other gases were kept", Math.Abs(Delta(SimHashes.CarbonDioxide)) < 1e-6 * recorded["CarbonDioxide"] &&
                    Math.Abs(Delta(SimHashes.Hydrogen)) < 1e-6 * Math.Max(1, recorded["Hydrogen"]), $"carbon dioxide {Delta(SimHashes.CarbonDioxide):G3} kg, hydrogen {Delta(SimHashes.Hydrogen):G3} kg");
                Finish();
            }
        }

        // A coal generator's exhaust reaches the owner as carbon dioxide.
        private static void CoalGenerator(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .8f, 300f));
            if (Once(8, "build"))
            {
                recorded["co2"] = ArenaSpecies(SimHashes.CarbonDioxide);
                machine = Build(GeneratorConfig.ID, Cell(3, 1)); Store(machine, SimHashes.Carbon, 200);
                Wire(new System.Collections.Generic.List<int> { Grid.OffsetCell(Grid.PosToCell(machine), machine.GetComponent<Building>().Def.PowerOutputOffset) });
                recorded["coal"] = Stored(machine, SimHashes.Carbon);
            }
            if (Once(60, "check"))
            {
                var co2 = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["co2"]; var burned = recorded["coal"] - Stored(machine, SimHashes.Carbon);
                Record("the generator's exhaust reached the owner", co2 > 1e-3 && burned > 0, $"carbon dioxide +{co2:G4} kg, coal burned {burned:G4} kg; {MachineState(machine)}");
                Finish();
            }
        }

        // A pitcher pump over a mixed pool offers each of its liquids.
        private static void PitcherPump(int frame)
        {
            if (Once(0, "pool")) Arena((x, y) => y <= 1 ? (SimHashes.Water, 900f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(3, "mix"))
                for (var x = 0; x < Width; ++x) for (var y = 0; y <= 1; ++y)
                    SimMessages.AddRemoveSubstance(Cell(x, y), SimHashes.DirtyWater, CellEventLogger.Instance.OxygenModifierSimUpdate, 60, 300, byte.MaxValue, 0);
            if (Once(8, "build")) machine = Build(LiquidPumpingStationConfig.ID, Cell(3, 2));
            if (Once(14, "check"))
            {
                var station = machine!.GetComponent<LiquidPumpingStation>();
                var infos = (System.Array)HarmonyLib.AccessTools.Field(typeof(LiquidPumpingStation), "infos").GetValue(station);
                var count = HarmonyLib.AccessTools.Field(typeof(LiquidPumpingStation), "infoCount");
                count.SetValue(station, 0);
                ColonyPitcherPump.Sample(station, Cell(3, 1)); ColonyPitcherPump.Sample(station, Cell(3, 0));
                var type = HarmonyLib.AccessTools.Inner(typeof(LiquidPumpingStation), "LiquidInfo");
                var offered = new List<string>();
                for (var i = 0; i < (int)count.GetValue(station); ++i)
                {
                    var info = infos.GetValue(i);
                    offered.Add(((Element)HarmonyLib.AccessTools.Field(type, "element").GetValue(info)).id + " " + ((float)HarmonyLib.AccessTools.Field(type, "amount").GetValue(info)).ToString("G4") + " kg");
                }
                Record("the pump offers every liquid of a mixed pool", ColonyPitcherPump.Ready && offered.Exists(o => o.StartsWith("Water ")) && offered.Exists(o => o.StartsWith("DirtyWater ")),
                    string.Join(", ", offered) + $"; shown {Grid.Element[Cell(3, 1)].id} {Grid.Mass[Cell(3, 1)]:G4} kg");
                Finish();
            }
        }

        // An oil refinery's natural gas goes into the owner; its petroleum stays stored.
        private static void OilRefinery(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, .8f, 300f));
            if (Once(8, "build"))
            {
                recorded["methane"] = ArenaSpecies(SimHashes.Methane);
                machine = Build(OilRefineryConfig.ID, Cell(2, 1)); Store(machine, SimHashes.CrudeOil, 300);
            }
            if (Once(60, "check"))
            {
                var gas = ArenaSpecies(SimHashes.Methane) - recorded["methane"]; var petroleum = Stored(machine, SimHashes.Petroleum);
                Record("natural gas reached the owner and petroleum stayed stored", gas > 1e-3 && petroleum > 1e-3, $"natural gas +{gas:G4} kg, petroleum {petroleum:G4} kg; {MachineState(machine)}");
                Finish();
            }
        }

        // An electrolyzer in a mixture above its overpressure limit stays idle:
        // it takes no water and draws no power, then runs once the air is thin.
        private static void ElectrolyzerBlocked(int frame)
        {
            if (Once(0, "arena")) Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 2.5f, 300f));
            if (Once(3, "mix")) Mix(SimHashes.CarbonDioxide, .5f);
            if (Once(8, "build"))
            {
                recorded["h2"] = ArenaSpecies(SimHashes.Hydrogen);
                machine = Build(ElectrolyzerConfig.ID, Cell(3, 1)); Store(machine, SimHashes.Water, 100);
                recorded["water"] = Stored(machine, SimHashes.Water);
            }
            if (Once(40, "blocked"))
            {
                var water = Stored(machine, SimHashes.Water); var h2 = ArenaSpecies(SimHashes.Hydrogen) - recorded["h2"];
                var operational = machine!.GetComponent<Operational>(); var power = machine.GetComponent<EnergyConsumer>();
                Record("an overpressured electrolyzer takes no water and no power", water == recorded["water"] && h2 < 1e-9 && !operational.IsActive,
                    $"water {recorded["water"]:G6} -> {water:G6} kg, hydrogen +{h2:G3} kg, active {operational.IsActive}, power drawn {power.WattsUsed:G4} W; {MachineState(machine)}");
                Finish();
            }
        }
    }
}
