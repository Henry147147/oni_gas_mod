using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Spaced Out! critters and plants that touch gas or liquid
    // (docs/dlc-compatibility.md#spaced-out-critters-and-plants). The objects
    // are the game's own prefabs, doing what their state machines do: the
    // scenarios only place them and sample the owner's cells and ledgers.
    internal static partial class ColonyScenarios
    {
        private static GameObject SpawnCreature(string id, int cell)
        {
            var prefab = Assets.GetPrefab(id);
            if (prefab == null) throw new InvalidOperationException("The prefab " + id + " does not exist (is Spaced Out! active?).");
            var created = Util.KInstantiate(prefab, Grid.CellToPosCBC(cell, Grid.SceneLayer.Creatures));
            created.SetActive(true);
            return created;
        }
        private static GameObject SpawnPlant(string id, int cell)
        {
            var prefab = Assets.GetPrefab(id);
            if (prefab == null) throw new InvalidOperationException("The prefab " + id + " does not exist (is Spaced Out! active?).");
            var created = GameUtil.KInstantiate(prefab, Grid.CellToPosCBC(cell, Grid.SceneLayer.BuildingFront), Grid.SceneLayer.BuildingFront);
            created.SetActive(true);
            return created;
        }
        // What a pipe holds, from the owner's pipe inventory.
        private static double PipeMass(bool gas, IEnumerable<int> cells)
        {
            var flow = gas ? Game.Instance.gasConduitFlow : Game.Instance.liquidConduitFlow;
            return cells.Sum(c => ColonyConduits.TryRead(flow, c, out var held) ? held.Mass : (double)flow.GetContents(c).mass);
        }

        // ---- spaced-out-critters

        // A Beeta falls asleep in carbon dioxide and its element consumer takes
        // the gas from the owner (BeeSleepMonitor, Bee.StartSleep). A gas
        // Staterpillar absorbs unbreathable gas into its storage (the
        // GasAndLiquidConsumerMonitor mass-consume path) and, with its
        // connector spawned at the ceiling as ConduitSleepStates does, pushes
        // it through a pipe to a vent; a liquid Staterpillar does the same with
        // the pool's water. The three phases run one after another in one
        // arena: gas on the left of the wall, a shallow pool on the right.
        private static GameObject? soBee, soGasCaterpillar, soLiquidCaterpillar, soGasVent, soLiquidVent;
        private static readonly List<int> soGasPipe = new List<int>(), soLiquidPipe = new List<int>();
        private static int SoCreatureFrame => 140;
        private static void SpacedOutCritters(int frame)
        {
            if (Once(0, "arena"))
            {
                soBee = soGasCaterpillar = soLiquidCaterpillar = soGasVent = soLiquidVent = null; soGasPipe.Clear(); soLiquidPipe.Clear();
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) : x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) :
                    x < 4 ? (SimHashes.CarbonDioxide, .9f, 300f) : y == 1 ? (SimHashes.Water, 990f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            }
            // Phase 1: a Beeta in carbon dioxide.
            if (Once(6, "beeta"))
            {
                recorded["co2"] = ArenaSpecies(SimHashes.CarbonDioxide); recorded["events"] = ColonyActors.Delivered; recorded["kg"] = ColonyActors.DeliveredKg;
                soBee = SpawnCreature(BeeConfig.ID, Cell(2, 3));
            }
            if (soBee != null && frame >= 6)
            {
                var sleep = soBee.GetSMI<BeeSleepMonitor.Instance>();
                if (sleep != null) recorded["beeta exposure max"] = Math.Max(recorded.TryGetValue("beeta exposure max", out var seen) ? seen : 0, sleep.CO2Exposure);
            }
            if (Once(130, "beeta check"))
            {
                var consumer = soBee != null ? soBee.GetComponent<ElementConsumer>() : null;
                var taken = recorded["co2"] - ArenaSpecies(SimHashes.CarbonDioxide);
                Record("the Beeta fell asleep in carbon dioxide", soBee != null && recorded.GetValueOrDefault("beeta exposure max") >= 5,
                    $"carbon dioxide exposure reached {recorded.GetValueOrDefault("beeta exposure max"):F0} (sleeps at 5)");
                Record("the sleeping Beeta's consumer took carbon dioxide from the owner", taken > .01 && ColonyActors.Delivered > recorded["events"] && ColonyActors.DeliveredKg > recorded["kg"],
                    $"carbon dioxide -{taken:G4} kg, {ColonyActors.Delivered - recorded["events"]} consumed-mass events, {ColonyActors.DeliveredKg - recorded["kg"]:G4} kg receipts; consumer {(consumer == null ? "missing" : $"rate {consumer.consumptionRate}, radius {consumer.consumptionRadius}")}");
                if (soBee != null) Util.KDestroyGameObject(soBee);
                soBee = null;
            }
            // Phase 2: the Staterpillars, once the Beeta's consumer has gone.
            if (Once(SoCreatureFrame, "staterpillars"))
            {
                recorded["co2 start"] = ArenaSpecies(SimHashes.CarbonDioxide); recorded["water start"] = ArenaSpecies(SimHashes.Water);
                soGasVent = Build(GasVentConfig.ID, Cell(3, 4));
                soLiquidVent = Build(LiquidVentConfig.ID, Cell(7, 4));
                soGasPipe.AddRange(Route(Cell(0, 4), Cell(3, 4))); soLiquidPipe.AddRange(Route(Cell(5, 4), Cell(7, 4)));
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, soGasPipe);
                Pipe(Game.Instance.liquidConduitSystem, LiquidConduitConfig.ID, soLiquidPipe);
                soGasCaterpillar = SpawnCreature(StaterpillarGasConfig.ID, Cell(1, 1));
                soLiquidCaterpillar = SpawnCreature(StaterpillarLiquidConfig.ID, Cell(6, 1));
            }
            // The connector a sleeping Staterpillar spawns on a ceiling cell above its pipe
            // (ConduitSleepStates), made by the creature's own methods once its monitors have
            // started: the idle monitor destroys any connector that exists when it first runs.
            if (Once(SoCreatureFrame + 10, "connectors"))
            {
                soGasCaterpillar!.GetComponent<Staterpillar>().SpawnConnectorBuilding(Cell(0, 5));
                soLiquidCaterpillar!.GetComponent<Staterpillar>().SpawnConnectorBuilding(Cell(5, 5));
                soGasCaterpillar.GetComponent<Staterpillar>().EnableConnector();
                soLiquidCaterpillar.GetComponent<Staterpillar>().EnableConnector();
            }
            if (frame > SoCreatureFrame)
            {
                void Peak(string key, double value) => recorded[key] = Math.Max(recorded.TryGetValue(key, out var seen) ? seen : 0, value);
                if (soGasCaterpillar != null) Peak("gas stored peak", Stored(soGasCaterpillar));
                if (soLiquidCaterpillar != null) Peak("liquid stored peak", Stored(soLiquidCaterpillar));
                Peak("gas pipe peak", PipeMass(true, soGasPipe)); Peak("liquid pipe peak", PipeMass(false, soLiquidPipe));
            }
            // Neither creature may be mid-inhale, nor have just finished: a consume request is in
            // flight for a frame before its mass reaches the creature's storage.
            bool Inhaling(GameObject? creature) => creature != null && creature.GetComponent<KPrefabID>().HasTag(GameTags.Creatures.WantsToStore);
            if (frame > SoCreatureFrame && (Inhaling(soGasCaterpillar) || Inhaling(soLiquidCaterpillar))) recorded["last inhale"] = frame;
            if (frame >= 620 && step <= 1400 + 1 && (frame - recorded.GetValueOrDefault("last inhale") >= 15 || frame >= 1400))
            {
                step = 1500; Current.Stage = "staterpillar check";
                double Held(GameObject? thing) => thing == null ? 0 : HeldMass(thing);
                string Connector(GameObject? caterpillar)
                {
                    var connector = caterpillar != null ? caterpillar.GetComponent<Staterpillar>().GetConduitDispenser() : null;
                    return connector == null ? "no connector" : $"connector connected {connector.IsConnected}, on {connector.isOn}";
                }
                var gasPipe = PipeMass(true, soGasPipe); var liquidPipe = PipeMass(false, soLiquidPipe);
                var gasHeld = Held(soGasCaterpillar) + Held(soGasVent); var liquidHeld = Held(soLiquidCaterpillar) + Held(soLiquidVent);
                var co2 = ArenaSpecies(SimHashes.CarbonDioxide); var water = ArenaSpecies(SimHashes.Water);
                Record("the gas Staterpillar absorbed carbon dioxide from the owner", recorded.GetValueOrDefault("gas stored peak") > .05,
                    $"peak stored {recorded.GetValueOrDefault("gas stored peak"):G4} kg; arena carbon dioxide {recorded["co2 start"]:G6} -> {co2:G6} kg; {Connector(soGasCaterpillar)}");
                Record("its connector sent the gas into the pipe", recorded.GetValueOrDefault("gas pipe peak") > .01,
                    $"peak pipe contents {recorded.GetValueOrDefault("gas pipe peak"):G4} kg; {Connector(soGasCaterpillar)}");
                Record("owner, pipe, Staterpillar and vent keep the carbon dioxide", Math.Abs(co2 + gasPipe + gasHeld - recorded["co2 start"]) <= 1e-3 * recorded["co2 start"],
                    $"{recorded["co2 start"]:G8} -> {co2 + gasPipe + gasHeld:G8} kg (arena {co2:G7}, pipe {gasPipe:G4}, held {gasHeld:G4}); checked at frame {frame}, last inhale seen at {recorded.GetValueOrDefault("last inhale")}");
                Record("the liquid Staterpillar absorbed water from the owner", recorded.GetValueOrDefault("liquid stored peak") > 1,
                    $"peak stored {recorded.GetValueOrDefault("liquid stored peak"):G4} kg; arena water {recorded["water start"]:G7} -> {water:G7} kg; {Connector(soLiquidCaterpillar)}");
                Record("its connector sent the water into the pipe", recorded.GetValueOrDefault("liquid pipe peak") > .1,
                    $"peak pipe contents {recorded.GetValueOrDefault("liquid pipe peak"):G4} kg; {Connector(soLiquidCaterpillar)}");
                Record("owner, pipe, Staterpillar and vent keep the water", Math.Abs(water + liquidPipe + liquidHeld - recorded["water start"]) <= 1e-4 * recorded["water start"],
                    $"{recorded["water start"]:G9} -> {water + liquidPipe + liquidHeld:G9} kg (arena {water:G8}, pipe {liquidPipe:G4}, held {liquidHeld:G4})");
                Finish();
            }
        }
        private static double Stored(GameObject creature) => creature.GetComponents<Storage>().Sum(s => s.MassStored());

        // ---- spaced-out-plants

        // Each Spaced Out! plant reads the owner's air: it is happy in normal
        // oxygen, reads low pressure in thin oxygen and a wrong atmosphere in
        // hydrogen. The filter plant's element consumer takes oxygen, and a
        // critter trap plant's digestion vents hydrogen into the owner.
        // The toe plant dies above 283 K and the cylindrica below 288 K, so the toe plant has the
        // cold chamber to itself; a shelf gives the left chamber a second row.
        private static readonly string[] SoPlantIds = { SwampHarvestPlantConfig.ID, WormPlantConfig.ID, SuperWormPlantConfig.ID, FilterPlantConfig.ID,
            CylindricaConfig.ID, WineCupsConfig.ID, CritterTrapPlantConfig.ID, ToePlantConfig.ID };
        private static readonly (int x, int y)[] SoPlantCells = { (0, 1), (1, 1), (2, 1), (3, 1), (0, 4), (1, 4), (2, 4), (6, 1) };
        private static float SoTemperature(int x) => x > 4 ? 265f : 300f;
        private static readonly List<GameObject> soPlants = new List<GameObject>();
        private static void Refill(SimHashes element, float kilograms)
        {
            var tool = CellEventLogger.Instance.SandBoxTool;
            for (var y = 1; y < Height; ++y) for (var x = 0; x < Width; ++x)
                if (!Grid.Solid[Cell(x, y)]) SimMessages.ReplaceElement(Cell(x, y), element, tool, kilograms, SoTemperature(x));
        }
        private static PressureVulnerable? Reader(GameObject plant) => plant != null ? plant.GetComponent<PressureVulnerable>() : null;
        private static void SpacedOutPlants(int frame)
        {
            if (Once(0, "arena"))
            {
                soPlants.Clear();
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : y == 0 ? (SimHashes.Granite, 1000f, SoTemperature(x)) :
                    x < 4 && y == 3 ? (SimHashes.Granite, 1000f, 300f) : (SimHashes.Oxygen, 1.2f, SoTemperature(x)));
            }
            if (Once(6, "plants"))
            {
                recorded["oxygen"] = ArenaSpecies(SimHashes.Oxygen); recorded["hydrogen"] = ArenaSpecies(SimHashes.Hydrogen);
                recorded["events"] = ColonyActors.Delivered; recorded["kg"] = ColonyActors.DeliveredKg;
                for (var i = 0; i < SoPlantIds.Length; ++i) soPlants.Add(SpawnPlant(SoPlantIds[i], Cell(SoPlantCells[i].x, SoPlantCells[i].y)));
                // A critter trap plant digesting a critter fills its storage with
                // hydrogen and vents it past a threshold (CritterTrapPlant.States.digesting).
                var trapObject = soPlants[Array.IndexOf(SoPlantIds, CritterTrapPlantConfig.ID)];
                var trap = soPlants[Array.IndexOf(SoPlantIds, CritterTrapPlantConfig.ID)].GetComponent<CritterTrapPlant>();
                trap.gasVentThreshold = .5f;
                trapObject.GetComponent<Storage>().AddGasChunk(SimHashes.Hydrogen, .6f, 300, byte.MaxValue, 0, false, true);
                recorded["trap stored"] = Stored(trapObject);
                trap.smi.sm.hasEatenCreature.Set(true, trap.smi);
            }
            if (Once(100, "normal check"))
            {
                var unsafeOnes = new List<string>();
                foreach (var plant in soPlants)
                {
                    var reader = Reader(plant);
                    if (reader == null || !reader.IsNormal || !reader.testAreaElementSafe) unsafeOnes.Add(plant == null ? "destroyed" : plant.PrefabID().Name + (reader == null ? " no reader" : $" normal {reader.IsNormal} safe {reader.testAreaElementSafe}"));
                }
                Record("every plant reads normal pressure and a safe atmosphere in oxygen", soPlants.Count == SoPlantIds.Length && unsafeOnes.Count == 0,
                    $"{soPlants.Count} plants; wrong: {(unsafeOnes.Count == 0 ? "none" : string.Join(", ", unsafeOnes))}");
                var filter = soPlants[Array.IndexOf(SoPlantIds, FilterPlantConfig.ID)];
                var consumer = filter != null ? filter.GetComponent<ElementConsumer>() : null;
                var o2 = recorded["oxygen"] - ArenaSpecies(SimHashes.Oxygen);
                Record("the filter plant's consumer took oxygen from the owner", consumer != null && o2 > 0 && ColonyActors.Delivered > recorded["events"],
                    $"oxygen -{o2:G4} kg, {ColonyActors.Delivered - recorded["events"]} consumed-mass events; consumer {(consumer == null ? "missing" : $"rate {consumer.consumptionRate:G4}, radius {consumer.consumptionRadius}")}");
                var trapPlant = soPlants[Array.IndexOf(SoPlantIds, CritterTrapPlantConfig.ID)];
                var gain = ArenaSpecies(SimHashes.Hydrogen) - recorded["hydrogen"]; var left = Stored(trapPlant);
                Record("the critter trap plant vented hydrogen into the owner", gain > .5 && gain + left >= recorded["trap stored"] - 1e-4,
                    $"hydrogen +{gain:G4} kg in the arena, {left:G4} kg left stored (started with {recorded["trap stored"]:G3} kg)");
                Refill(SimHashes.Hydrogen, .1f);
            }
            string Gone(int index) => soPlants[index] == null ? SoPlantIds[index] + " destroyed" : "";
            // Hydrogen is no gas the plants list as safe (the critter trap plant lists none).
            if (Once(170, "atmosphere check"))
            {
                var wrong = new List<string>();
                for (var i = 0; i < soPlants.Count; ++i)
                {
                    if (soPlants[i] == null) { wrong.Add(Gone(i)); continue; }
                    if (soPlants[i].GetComponent<CritterTrapPlant>() != null) continue;
                    var reader = Reader(soPlants[i]);
                    if (reader == null || reader.testAreaElementSafe) wrong.Add(SoPlantIds[i] + (reader == null ? " no reader" : " still safe"));
                }
                var cell = Cell(0, 1);
                Record("every plant reads a wrong atmosphere in hydrogen", wrong.Count == 0,
                    $"cell {Grid.Element[cell].id} {Grid.Mass[cell]:G3} kg; wrong: {(wrong.Count == 0 ? "none" : string.Join(", ", wrong))}");
                Refill(SimHashes.Oxygen, 1.2f);
            }
            if (Once(240, "thin air"))
                Refill(SimHashes.Oxygen, .01f);
            // ONI smooths a plant's pressure reading, so thin air needs about ten seconds to read low.
            if (Once(310, "thin check"))
            {
                var wrong = new List<string>();
                for (var i = 0; i < soPlants.Count; ++i)
                {
                    if (soPlants[i] == null) { wrong.Add(Gone(i)); continue; }
                    if (soPlants[i].GetComponent<CritterTrapPlant>() != null) continue; // not pressure sensitive
                    var reader = Reader(soPlants[i]);
                    if (reader == null || reader.IsNormal) wrong.Add(SoPlantIds[i] + (reader == null ? " no reader" : " still normal"));
                }
                var cell = Cell(0, 1);
                Record("every pressure-sensitive plant reads low pressure in thin oxygen", wrong.Count == 0,
                    $"cell {Grid.Element[cell].id} {Grid.Mass[cell]:G3} kg; wrong: {(wrong.Count == 0 ? "none" : string.Join(", ", wrong))}");
                Finish();
            }
        }
    }
}
