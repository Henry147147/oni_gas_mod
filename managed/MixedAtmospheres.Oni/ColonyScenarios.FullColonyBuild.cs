using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? M(string label) => fcMachines.TryGetValue(fcPrefix + label, out var go) ? go : null;
        private static void FcLink(FcNet net, GameObject? from, GameObject? to) { if (from != null && to != null) FcConnect(net, FcOut(from), FcIn(to)); }

        private static GameObject? fcFilterPump, fcFilter, fcFilterPassVent, fcFilterCo2Vent;
        // Machines of the halls. Positions are relative to the pod.
        private static void FcMachinesHalls()
        {
            // ---- hall 0, east of the pod: electrolysis (water reservoir, three electrolyzers, three hydrogen generators)
            // and two algae deoxidizers.
            var x = 8;
            var water1 = FcRow("water-1", LiquidReservoirConfig.ID, ref x, 0);
            var electrolyzers = new[] { 1, 2, 3 }.Select(i => FcRow("electrolyzer-" + i, ElectrolyzerConfig.ID, ref x, 0)).ToList();
            var generators = new[] { 1, 2, 3 }.Select(i => FcRow("hydrogen-gen-" + i, HydrogenGeneratorConfig.ID, ref x, 0)).ToList();
            var deoxidizers = new[] { 1, 2 }.Select(i => FcRow("deoxidizer-" + i, MineralDeoxidizerConfig.ID, ref x, 0)).ToList();
            var feed = FcNew("electrolysis water", FcKind.Liquid);
            foreach (var e in electrolyzers) FcLink(feed, water1, e);
            var h2 = FcNew("electrolysis hydrogen", FcKind.Gas);
            foreach (var e in electrolyzers) if (e != null && generators[0] != null) FcConnect(h2, FcOut(e), FcIn(generators[0]!));
            foreach (var g in generators.Skip(1)) if (g != null && generators[0] != null) FcConnect(h2, FcIn(g), FcIn(generators[0]!));
            FcPowerGroups("hall0", electrolyzers.Concat(generators).Concat(deoxidizers), 9, 5);

            // ---- hall 1 (y 13..20): carbon dioxide scrubbers with water and waste reservoirs, algae habitats,
            // and on the west plaza a gas pump with a filter that takes carbon dioxide out of the hall air.
            x = 8; var y = 13;
            var water2 = FcRow("water-2", LiquidReservoirConfig.ID, ref x, y);
            var scrubber1 = FcRow("scrubber-1", CO2ScrubberConfig.ID, ref x, y);
            var waste1 = FcRow("waste-1", LiquidReservoirConfig.ID, ref x, y);
            var scrubber2 = FcRow("scrubber-2", CO2ScrubberConfig.ID, ref x, y);
            var waste2 = FcRow("waste-2", LiquidReservoirConfig.ID, ref x, y);
            var habitat1 = FcRow("algae-habitat-1", AlgaeHabitatConfig.ID, ref x, y);
            var habitat2 = FcRow("algae-habitat-2", AlgaeHabitatConfig.ID, ref x, y);
            var scrubIn = FcNew("scrubber water", FcKind.Liquid);
            FcLink(scrubIn, water2, scrubber1); FcLink(scrubIn, water2, scrubber2);
            FcLink(FcNew("scrubber 1 waste", FcKind.Liquid), scrubber1, waste1);
            FcLink(FcNew("scrubber 2 waste", FcKind.Liquid), scrubber2, waste2);
            // A gas pump through a valve into a vent, and one into a gas reservoir (east end of the hall).
            var valvePump = FcRow("valve-pump", GasPumpConfig.ID, ref x, y);
            var valve = FcRow("gas-valve", GasValveConfig.ID, ref x, y);
            var valveVent = FcRow("valve-vent", GasVentConfig.ID, ref x, y);
            if (valvePump != null && valve != null && valveVent != null)
            { FcConnect(FcNew("valve in", FcKind.Gas), FcOut(valvePump), FcIn(valve)); FcConnect(FcNew("valve out", FcKind.Gas), FcOut(valve), FcIn(valveVent)); }
            var storePump = FcRow("store-pump", GasPumpConfig.ID, ref x, y);
            var store = FcRow("gas-store", GasReservoirConfig.ID, ref x, y);
            if (storePump != null && store != null) FcConnect(FcNew("store", FcKind.Gas), FcOut(storePump), FcIn(store));
            x = -7;
            fcFilterPump = FcRow("gas-pump-0", GasPumpConfig.ID, ref x, y, 3);
            fcFilter = FcRow("gas-filter-0", GasFilterConfig.ID, ref x, y, 3);
            fcFilterPassVent = FcRow("gas-vent-pass", GasVentConfig.ID, ref x, y, 3);
            fcFilterCo2Vent = FcRow("gas-vent-co2", GasVentConfig.ID, ref x, y, 3);
            FcPowerGroups("hall1", new[] { scrubber1, scrubber2, habitat1, habitat2, fcFilterPump, fcFilter, valvePump, storePump }, 20, 4);

            if (!fcCompact)
            {
            // ---- hall 2 (y 22..27): water sieve with its clean-water reservoir on the west plaza; east of the ladder the oil chain.
                y = 22; x = -7;
                var dirty = FcRow("dirty-water", LiquidReservoirConfig.ID, ref x, y, 3);
                var sieve = FcRow("water-sieve", WaterPurifierConfig.ID, ref x, y, 3);
                var clean = FcRow("clean-water", LiquidReservoirConfig.ID, ref x, y, 3);
                FcLink(FcNew("sieve in", FcKind.Liquid), dirty, sieve);
                FcLink(FcNew("sieve out", FcKind.Liquid), sieve, clean);
                x = 8;
                var refinery = FcRow("oil-refinery", OilRefineryConfig.ID, ref x, y, 34);
                var petroleum = FcRow("petroleum", LiquidReservoirConfig.ID, ref x, y, 34);
                var petrogen = FcRow("petroleum-gen", PetroleumGeneratorConfig.ID, ref x, y, 34);
                var press = FcRow("polymer-press", PolymerizerConfig.ID, ref x, y, 34);
                FcLink(FcNew("refined", FcKind.Liquid), refinery, petroleum);
                var feedPetroleum = FcNew("petroleum feed", FcKind.Liquid);
                FcLink(feedPetroleum, petroleum, petrogen); FcLink(feedPetroleum, petroleum, press);
                if (press != null) FcOutlet("press-vent", press, FcKind.Gas, FcNew("press exhaust", FcKind.Gas));
                if (petrogen != null)
                {
                    var kind = petrogen.GetComponent<Building>().Def.OutputConduitType == ConduitType.Gas ? FcKind.Gas : FcKind.Liquid;
                    FcOutlet("petroleum-gen-vent", petrogen, kind, FcNew("petroleum exhaust", kind));
                }
                x = 27;
                var heater1 = FcRow("hall-heater-1", SpaceHeaterConfig.ID, ref x, y, 34);
                var heater2 = FcRow("hall-heater-2", SpaceHeaterConfig.ID, ref x, y, 34);
                FcPowerGroups("hall2", new[] { sieve, refinery, press, petrogen, heater1, heater2 }, 27, 4);
            }

            // ---- hall -1 (y -11..-2): living quarters, food, toilets.
            x = 8; y = -11;
            for (var bed = 1; bed <= 8; ++bed) FcRow("bed-" + bed, BedConfig.ID, ref x, y);
            for (var box = 1; box <= 2; ++box) FcRow("rations-" + box, RationBoxConfig.ID, ref x, y);
            FcRow("outhouse-1", OuthouseConfig.ID, ref x, y);
            x = -3;
            for (var bed = 9; bed <= 10; ++bed) FcRow("bed-" + bed, BedConfig.ID, ref x, y, 3);
            FcRow("outhouse-2", OuthouseConfig.ID, ref x, y, 3);
        }
        // The filter knows its port cells only once it has spawned.
        private static void FcFilterNets()
        {
            if (fcFilterPump == null || fcFilter == null || fcFilterPassVent == null || fcFilterCo2Vent == null) return;
            fcFilter.GetComponent<Filterable>().SelectedTag = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).tag;
            var component = fcFilter.GetComponent<ElementFilter>();
            int Field(string name) => (int)AccessTools.Field(typeof(ElementFilter), name).GetValue(component);
            var into = Field("inputCell"); var passed = Field("outputCell"); var removed = Field("filteredCell");
            foreach (var port in new[] { into, passed, removed }) fcPorts[FcKind.Gas].Add(port);
            FcConnect(FcNew("filter intake", FcKind.Gas), FcOut(fcFilterPump), into);
            FcConnect(FcNew("filter passed", FcKind.Gas), passed, FcIn(fcFilterPassVent));
            FcConnect(FcNew("filter removed", FcKind.Gas), removed, FcIn(fcFilterCo2Vent));
        }

        // Loads and generators of a hall in groups of `size`, each with its own bus and Dev generator.
        private static void FcPowerGroups(string name, IEnumerable<GameObject?> members, int busDy, int size)
        {
            var sorted = members.Where(m => m != null).Select(m => m!).OrderBy(m => Grid.PosToCell(m) % Grid.WidthInCells).ToList();
            for (var k = 0; k * size < sorted.Count; ++k)
                FcPower(name + "-" + k, sorted.Skip(k * size).Take(size), busDy);
        }

        // The four sealed vaults (west of the upper halls).
        private static void FcMachinesVaults()
        {
            // Hot vault (x -52..-31, y 18..25): a hot steam vent, a pump feeding a steam turbine, heaters.
            var hot = new List<GameObject?>();
            SpawnGeyser("hot-steam-vent", GeyserGenericConfig.HotSteam, -50, 18);
            var x = -45;
            hot.Add(FcRow("heater-1", SpaceHeaterConfig.ID, ref x, 18, -31));
            hot.Add(FcRow("heater-2", SpaceHeaterConfig.ID, ref x, 18, -31));
            var pump = FcRow("steam-pump", GasPumpConfig.ID, ref x, 18, -31); hot.Add(pump);
            var turbine = FcRow("steam-turbine", SteamTurbineConfig2.ID, ref x, 18, -31); hot.Add(turbine);
            if (pump != null && turbine != null)
            {
                FcConnect(FcNew("steam", FcKind.Gas), FcOut(pump), FcIn(turbine));
                var kind = turbine.GetComponent<Building>().Def.OutputConduitType == ConduitType.Gas ? FcKind.Gas : FcKind.Liquid;
                FcOutlet("turbine-vent", turbine, kind, FcNew("turbine water", kind), 2);
            }
            FcPowerGroups("vault-hot", hot, 25, 5);

            // Natural gas vault (x -52..-31, y 29..36): a natural gas vent, a pump, a generator and a vent for its exhaust.
            var gas = new List<GameObject?>();
            SpawnGeyser("natural-gas-vent", GeyserGenericConfig.Methane, -50, 29);
            x = -45;
            var gasPump = FcRow("gas-vent-pump", GasPumpConfig.ID, ref x, 29, -31); gas.Add(gasPump);
            var methane = FcRow("methane-gen", MethaneGeneratorConfig.ID, ref x, 29, -31); gas.Add(methane);
            if (gasPump != null && methane != null)
            {
                FcConnect(FcNew("natural gas", FcKind.Gas), FcOut(gasPump), FcIn(methane));
                var kind = methane.GetComponent<Building>().Def.OutputConduitType == ConduitType.Gas ? FcKind.Gas : FcKind.Liquid;
                FcOutlet("methane-vent", methane, kind, FcNew("methane exhaust", kind), 2);
            }
            FcPowerGroups("vault-gas", gas, 36, 5);

            // Cold vault (x -27..-12, y 18..25): wheezeworts in carbon dioxide at 235 K; they are placed with the life.
            // Pool vault (x -27..-12, y 29..36): a cool and a warm pool, each stirred by a pump and a vent above it.
            var pool = new List<GameObject?>();
            var pumpA = FcBuild("pool-pump-a", LiquidPumpConfig.ID, -25, 29); pool.Add(pumpA);
            var ventA = FcBuild("pool-vent-a", LiquidVentConfig.ID, -21, 33, false);
            var pumpB = FcBuild("pool-pump-b", LiquidPumpConfig.ID, -16, 29); pool.Add(pumpB);
            var ventB = FcBuild("pool-vent-b", LiquidVentConfig.ID, -13, 33, false);
            if (pumpA != null && ventA != null) FcConnect(FcNew("pool A loop", FcKind.Liquid), FcOut(pumpA), FcIn(ventA));
            if (pumpB != null && ventB != null) FcConnect(FcNew("pool B loop", FcKind.Liquid), FcOut(pumpB), FcIn(ventB));
            FcPowerGroups("vault-pool", pool, 36, 5);
        }

        private static void SpawnGeyser(string label, string kind, int dx, int dy)
        {
            var prefab = Assets.GetPrefab(GeyserGenericConfig.ID + "_" + kind);
            if (prefab == null) { FcNote(label + ": prefab missing"); return; }
            var geyser = GameUtil.KInstantiate(prefab, Grid.CellToPosCBC(FC(dx, dy), Grid.SceneLayer.BuildingBack), Grid.SceneLayer.BuildingBack);
            geyser.SetActive(true);
            fcMachines[fcPrefix + label] = geyser; fcGeysers.Add(geyser);
        }
        private static readonly List<GameObject> fcGeysers = new List<GameObject>();

        // Fluid and solid stock the machines would otherwise wait for a duplicant to bring.
        private static void FcStock()
        {
            void Liquid(string label, SimHashes element, float kilograms, float temperature = 300f)
            {
                var go = M(label); if (go == null) return;
                go.GetComponent<Storage>().AddLiquid(element, kilograms, temperature, byte.MaxValue, 0, false, true);
            }
            Liquid("water-1", SimHashes.Water, 4800); Liquid("water-2", SimHashes.Water, 4800);
            var oil = M("oil-refinery"); if (oil != null) Store(oil, SimHashes.CrudeOil, 2000);
            Liquid("dirty-water", SimHashes.DirtyWater, 4000, 300f);
            foreach (var label in new[] { "electrolyzer-1", "electrolyzer-2", "electrolyzer-3" }) { var e = M(label); if (e != null) Store(e, SimHashes.Water, 100); }
            foreach (var label in new[] { "deoxidizer-1", "deoxidizer-2" }) { var d = M(label); if (d != null) Store(d, SimHashes.Algae, 600); }
            var sieve = M("water-sieve"); if (sieve != null) Store(sieve, SimHashes.Sand, 900);
            foreach (var label in new[] { "algae-habitat-1", "algae-habitat-2" }) { var h = M(label); if (h != null) { Store(h, SimHashes.Algae, 100); Store(h, SimHashes.Water, 200); } }
            foreach (var label in new[] { "outhouse-1", "outhouse-2" }) { var o = M(label); if (o != null) Store(o, SimHashes.Dirt, 200); }
            foreach (var box in new[] { "rations-1", "rations-2" })
            {
                var rations = M(box); if (rations == null) continue;
                var food = GameUtil.KInstantiate(Assets.GetPrefab(FieldRationConfig.ID), rations.transform.GetPosition(), Grid.SceneLayer.Ore);
                food.SetActive(true); food.GetComponent<PrimaryElement>().Units = 60;
                rations.GetComponent<Storage>().Store(food, true);
            }
            foreach (var geyser in fcGeysers)
            {
                var g = geyser != null ? geyser.GetComponent<global::Geyser>() : null;
                if (g != null) g.ShiftTimeTo(global::Geyser.TimeShiftStep.ActiveState);
            }
        }

        // Duplicants, plants and critters.
        private static void FcLife()
        {
            for (var i = 0; i < fcExtraDuplicants; ++i)
            {
                UnityEngine.Random.InitState(14714700 + i + (fcCompact ? 100 : 0));
                var stats = new MinionStartingStats(false, null, null, false);
                fcDuplicants.Add(stats.Deliver(Grid.CellToPosCBC(FC(-8 + i, 0), Grid.SceneLayer.Move)));
            }
            int Floor(int x, int y) => FC(x, y);
            for (var k = 0; k < 8; ++k) SpawnPlant(BasicSingleHarvestPlantConfig.ID, Floor(30 + k * 2, -11));
            for (var k = 0; k < 4; ++k) SpawnPlant(PrickleFlowerConfig.ID, Floor(30 + k * 2, 13));
            for (var k = 0; k < (fcCompact ? 3 : 5); ++k) SpawnCreature(PuftConfig.ID, Floor(12 + k * 3, 3));
            for (var k = 0; k < (fcCompact ? 2 : 4); ++k) SpawnCreature(HatchConfig.ID, Floor(30 + k * 3, 1));
            if (fcCompact) return;
            for (var k = 0; k < 4; ++k) SpawnPlant(ColdBreatherConfig.ID, Floor(-25 + k * 3, 18));
            for (var k = 0; k < 3; ++k) SpawnCreature(OilFloaterConfig.ID, Floor(18 + k * 4, 22));
            for (var k = 0; k < 4; ++k) SpawnCreature(PacuConfig.ID, Floor(-25 + k, 30));
        }

        private static void FcReport(OniNativeColony.Status status, int minimumDuplicants)
        {
            var dupes = Components.LiveMinionIdentities.Count;
            Record("the colony has its duplicants", dupes >= minimumDuplicants, $"{dupes} duplicants alive");
            var operational = fcMachines.Where(m => m.Value != null && m.Value.GetComponent<Operational>() != null).ToList();
            var running = operational.Where(m => m.Value.GetComponent<Operational>().IsOperational).Select(m => m.Key).ToList();
            var idle = operational.Where(m => !m.Value.GetComponent<Operational>().IsOperational).Select(m => m.Key + " [" + MachineState(m.Value) + "]").ToList();
            Debug.Log($"[FC-SUMMARY] machines {fcMachines.Count}, operational {running.Count}: {string.Join(",", running)}");
            foreach (var line in idle) Debug.Log("[FC-IDLE] " + line);
            Record("the machines were built", fcMachines.Count >= 30, $"{fcMachines.Count} machines, {operational.Count} with an operating state, {running.Count} operational; {fcBuildings} buildings and conduit cells; notes: {string.Join("; ", fcNotes)}");
            Record("the owner's ledgers stayed closed", status.MaximumRelativeMassError < 1e-9 && status.MaximumRelativeEnergyError < 1e-9,
                $"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}");
            int gasCells = 0, liquidCells = 0;
            for (var cell = 0; cell < Grid.CellCount; ++cell)
            {
                if (!ColonyOwner.OwnsFluidCell(cell) || Grid.Solid[cell] || Grid.Mass[cell] <= 0) continue;
                if (Grid.Element[cell].IsLiquid) ++liquidCells; else if (Grid.Element[cell].IsGas) ++gasCells;
            }
            Debug.Log($"[FC-SUMMARY] gas cells {gasCells}, liquid cells {liquidCells}, duplicants {dupes}, buildings {Components.BuildingCompletes.Count}");
        }
    }
}
namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // MA_COLONY_CENSUS=1: what a colony holds when a run completes (full-colony benchmark metadata).
        internal static object Census(OniNativeColony.Status status)
        {
            long gas = 0, liquid = 0, solid = 0, gasPipes = 0, liquidPipes = 0, wires = 0, vacuum = 0;
            double gasKg = 0, liquidKg = 0;
            for (var cell = 0; cell < Grid.CellCount; ++cell)
            {
                if (!Grid.IsValidCell(cell)) continue;
                if (Grid.Objects[cell, (int)ObjectLayer.GasConduit] != null) ++gasPipes;
                if (Grid.Objects[cell, (int)ObjectLayer.LiquidConduit] != null) ++liquidPipes;
                if (Grid.Objects[cell, (int)ObjectLayer.Wire] != null) ++wires;
                if (Grid.Solid[cell]) { ++solid; continue; }
                var element = Grid.Element[cell];
                if (Grid.Mass[cell] <= 0 || element.id == SimHashes.Vacuum) { ++vacuum; continue; }
                if (element.IsLiquid) { ++liquid; liquidKg += Grid.Mass[cell]; } else if (element.IsGas) { ++gas; gasKg += Grid.Mass[cell]; }
            }
            return new
            {
                Frame = status.CompletedFrames, GameCycle = GameClock.Instance == null ? -1 : GameClock.Instance.GetCycle(), GameSeconds = GameClock.Instance == null ? 0 : GameClock.Instance.GetTime(),
                Duplicants = Components.LiveMinionIdentities.Count, Buildings = Components.BuildingCompletes.Count,
                GasCells = gas, LiquidCells = liquid, GasKg = gasKg, LiquidKg = liquidKg, EmptyFluidCells = vacuum, SolidCells = solid,
                GasPipeCells = gasPipes, LiquidPipeCells = liquidPipes, WireCells = wires, OwnedPipeCellsWithContents = ColonyConduits.RestoredCells,
                OwnedCells = ColonyOwner.OwnedCells, Plants = Components.Uprootables.Count
            };
        }
    }
}
