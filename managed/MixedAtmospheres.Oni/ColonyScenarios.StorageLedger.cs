using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Storage beyond the reservoirs is ONI's own (user decision 2026-11-04,
    // docs/completion-plan.md amendments), but every crossing of mass, heat
    // and germs between the owner's cells and ONI storage or items must be
    // booked. This scenario drives the real ONI code of each crossing that has
    // no other end-to-end check and compares the owner's side with ONI's side:
    //   mop             Moppable's ConsumeMass and chunk (OnCellMopped) on a hidden puddle
    //   bottle          LiquidPumpingStation's WorkSession (the pitcher pump)
    //   demolition      Storage.DropAll through Deconstructable: a gas is
    //                   vented into the owner, a liquid becomes a chunk
    //   chunk release   SubstanceChunk.OnRelease puts the chunk back
    //   emptier         the gas canister emptier
    // Per path: the owner's species mass changes by what ONI's items gain or
    // lose (nothing created, nothing lost); germs follow the item's disease
    // (against a control cell that undergoes the same evolution); a chunk's
    // temperature is the temperature of what left the owner.
    // Arena: floor y 0; pool x 0..3 and a control puddle x 5 and a puddle x 7
    // at y 1, oxygen elsewhere.
    internal static partial class ColonyScenarios
    {
        private static double ArenaGerms(byte disease)
        {
            double total = 0;
            for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
            {
                var cell = Cell(x, y);
                if (Grid.DiseaseIdx[cell] == disease && Grid.DiseaseCount[cell] > 0) total += Grid.DiseaseCount[cell];
            }
            return total;
        }
        private static double GasGerms(byte disease)
        {
            double total = 0;
            for (var y = 2; y < Height; ++y) for (var x = 0; x < Width; ++x)   // the air above the liquid pockets
            {
                var cell = Cell(x, y);
                if (!Grid.Solid[cell] && Grid.DiseaseIdx[cell] == disease && Grid.DiseaseCount[cell] > 0) total += Grid.DiseaseCount[cell];
            }
            return total;
        }
        // A cell's water and its steam: phase change moves mass between the two, so only their sum is a crossing's account.
        private static double WaterAndSteam(int cell) => SpeciesMass(cell, SimHashes.Water) + SpeciesMass(cell, SimHashes.Steam);
        private static double PoolWater() => Enumerable.Range(0, 3).Sum(x => WaterAndSteam(Cell(x, 1)));
        private static double CellGerms(int cell, byte disease) => Grid.DiseaseIdx[cell] == disease && Grid.DiseaseCount[cell] > 0 ? Grid.DiseaseCount[cell] : 0;
        // ONI's own items of an element, wherever they are (floor, storage, carried).
        private static IEnumerable<PrimaryElement> Items(SimHashes element) =>
            Components.Pickupables.Items.Where(item => item != null && !item.wasAbsorbed && !item.GetComponent<KPrefabID>().pendingDestruction &&
                // The pitcher pump's stored chunks are placeholders for the pool beneath it, not mass.
                (item.storage == null || item.storage.GetComponent<LiquidPumpingStation>() == null))
                .Select(item => item.GetComponent<PrimaryElement>()).Where(primary => primary != null && primary.ElementID == element);
        private static double ItemMass(SimHashes element) => Items(element).Sum(primary => (double)primary.Mass);
        private static double ItemGerms(SimHashes element, byte disease) =>
            Items(element).Where(primary => primary.DiseaseIdx == disease).Sum(primary => (double)primary.DiseaseCount);

        private static double StoredGerms(Storage storage, byte disease) =>
            storage.items.Where(item => item != null).Select(item => item.GetComponent<PrimaryElement>()).Where(primary => primary != null && primary.DiseaseIdx == disease)
                .Sum(primary => (double)primary.DiseaseCount);

        private static object? bottleSession;
        private static int ledgerLogged = -1;
        private static GameObject? ledgerLocker;
        private static string Fraction(double value, double expected) => expected > 0 ? $"{value / expected:F3} of the expected {expected:G5}" : $"{value:G5}";

        private static void StorageLedger(int frame)
        {
            // Food poisoning is stable in liquid (half-life 12,000 s below 5,000 germs per kg) and in gas (1,200 s), so the counts below hold for the run.
            var food = DiseaseIndex(Db.Get().Diseases.FoodGerms); var pollen = food; var slime = food; var spores = food;
            if (Once(0, "arena"))
                // Liquid flows in the owner, so every liquid sits in its own pocket: the pool behind a wall at x 3,
                // the control puddle at x 4 and the mopped puddle at x 7, each closed by solids. The puddles are
                // sealed from the air because the owner shows a cell's liquid to ONI (and so to the mop) only when it
                // fills half the cell or the cell holds no gas.
                Arena((x, y) => y == 0 ? (SimHashes.Granite, 1000f, 300f) :
                    x == 3 || y == 1 && (x == 5 || x == 6) || y == 2 && (x == 4 || x == 7) ? (SimHashes.Unobtanium, 20000f, 300f) :
                    y == 1 && x <= 2 ? (SimHashes.Water, 500f, 300f) :
                    y == 1 && (x == 4 || x == 7) ? (SimHashes.Water, 20f, 300f) : (SimHashes.Oxygen, 1.2f, 300f));
            if (Once(6, "seed germs"))
            {
                // Equal germs in every pool cell (no net diffusion) and in both puddles; the control puddle is never touched.
                for (var x = 0; x <= 2; ++x) SimMessages.ModifyDiseaseOnCell(Cell(x, 1), pollen, 500000);
                SimMessages.ModifyDiseaseOnCell(Cell(4, 1), slime, 50000);
                SimMessages.ModifyDiseaseOnCell(Cell(7, 1), slime, 50000);
                // Air in each puddle pocket (the arena fill places one element per cell).
                foreach (var puddle in new[] { Cell(4, 1), Cell(7, 1) })
                    SimMessages.AddRemoveSubstance(puddle, SimHashes.Oxygen, CellEventLogger.Instance.OxygenModifierSimUpdate, 1.2f, 300, byte.MaxValue, 0);
            }

            // ---- mopping: ConsumeMass of the shown liquid, the chunk takes the receipt.
            if (Once(10, "mop"))
            {
                recorded["mop water"] = WaterAndSteam(Cell(7, 1)); recorded["mop items"] = ItemMass(SimHashes.Water);
                recorded["mop item germs"] = ItemGerms(SimHashes.Water, slime); recorded["mop germs"] = CellGerms(Cell(7, 1), slime);
                // Moppable.MopCell's messages and OnCellMopped's chunk, without MopCell's guard that the cell shows a
                // liquid (a puddle under air is hidden from ONI, so the mop tool never offers it).
                var cell = Cell(7, 1); recorded["mop placed"] = 1; recorded["mop extracted"] = ColonyOwner.Status.ExtractedKg;
                recorded["mop germs"] = CellGerms(cell, slime);
                var handle = Game.Instance.massConsumedCallbackManager.Add((info, data) =>
                {
                    if (!(info.mass > 0)) return;
                    LiquidSourceManager.Instance.CreateChunk(ElementLoader.elements[info.elemIdx], info.mass, info.temperature, info.diseaseIdx, info.diseaseCount, Grid.CellToPosCCC(cell, Grid.SceneLayer.Ore));
                }, null, "Moppable");
                SimMessages.ConsumeMass(cell, SimHashes.Water, 1000f, 1, handle.index);
            }
            if (Once(18, "mop check"))
            {
                var lost = recorded["mop water"] - WaterAndSteam(Cell(7, 1)); var gained = ItemMass(SimHashes.Water) - recorded["mop items"];
                Record("mopping moves the puddle from the owner into a chunk, nothing created or lost", recorded["mop placed"] == 1 && lost > 19 && Math.Abs(lost - gained) < 2e-3 && Math.Abs(recorded["mop water"] - 20) < 2e-2,
                    $"owner -{lost:R} kg, chunk +{gained:R} kg (puddle held {recorded["mop water"]:R} kg of water and steam); left {WaterAndSteam(Cell(7, 1)):G5} kg");
                var chunkGerms = ItemGerms(SimHashes.Water, slime) - recorded["mop item germs"]; var control = CellGerms(Cell(4, 1), slime);
                Record("the mopped chunk and what the puddle kept hold the puddle's germs, once", chunkGerms > 0 && chunkGerms + CellGerms(Cell(7, 1), slime) > .98 * recorded["mop germs"] && chunkGerms + CellGerms(Cell(7, 1), slime) < 1.02 * recorded["mop germs"],
                    $"chunk {chunkGerms:F0} germs, puddle held {recorded["mop germs"]:F0}, control puddle now {control:F0}, puddle left {CellGerms(Cell(7, 1), slime):F0}");
                // The chunk is the mass that moved, not 20 kg: the puddle holds only water until its air arrives at
                // frame 6, and water boils into that vacuum towards its saturation pressure (about 25 g of steam at
                // 300 K), which the mop leaves behind.
                var chunk = Items(SimHashes.Water).OrderByDescending(primary => primary.Mass).FirstOrDefault(primary => Math.Abs(primary.Mass - gained) < 2e-3);
                Record("the mopped chunk has the temperature of the liquid that left", chunk != null && Math.Abs(chunk.Temperature - 300) < .5, $"{chunk?.Temperature:F3} K");
            }

            // ---- a bottle filled at the pitcher pump: ConsumeMass of one element, radius 2.
            if (Once(20, "pitcher pump"))
            {
                var station = Build(LiquidPumpingStationConfig.ID, Cell(1, 2));
                recorded["pump water"] = PoolWater(); recorded["pump germs"] = CellGerms(Cell(1, 1), pollen);
                var session = AccessTools.Inner(typeof(LiquidPumpingStation), "WorkSession");
                bottleSession = Activator.CreateInstance(session, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null,
                    new object?[] { Grid.PosToCell(station), SimHashes.Water, null, 30f, station }, null);
            }
            if (Once(30, "pitcher check"))
            {
                var session = bottleSession!.GetType();
                var consumed = (float)AccessTools.Method(session, "GetConsumedAmount").Invoke(bottleSession, null)!;
                var info = AccessTools.Method(session, "GetDiseaseInfo").Invoke(bottleSession, null)!;
                var germs = (int)AccessTools.Field(info.GetType(), "count").GetValue(info)!; var idx = (byte)AccessTools.Field(info.GetType(), "idx").GetValue(info)!;
                var temperature = (float)AccessTools.Method(session, "GetTemperature").Invoke(bottleSession, null)!;
                var lost = recorded["pump water"] - PoolWater();
                Record("a bottle filled at the pitcher pump holds what the owner lost", Math.Abs(consumed - 30) < 2e-3 && Math.Abs(lost - consumed) < 2e-3, $"bottle {consumed:R} kg, owner -{lost:R} kg");
                // 30 of the cell's 500 kg: its germs are 6% of the control cell's (same evolution).
                // 30 of the cell's 500 kg is 6% of its germs at the moment of the draw; the control cell then evolves on.
                var expected = 0.06 * recorded["pump germs"];
                Record("the bottle carries its share of the cell's germs once", idx == pollen && germs > .85 * expected && germs < 1.15 * expected, $"{germs} germs: {Fraction(germs, expected)} (sibling cell has {CellGerms(Cell(2, 1), pollen):F0} now, the drawn cell {CellGerms(Cell(1, 1), pollen):F0})");
                Record("the bottle has the temperature of the liquid that left", Math.Abs(temperature - 300) < .5, $"{temperature:F3} K");
            }

            // ---- demolition: DropAll vents a gas into the owner and leaves a liquid as a chunk.
            if (Once(32, "locker"))
            {
                // A new storage drops what its filter does not accept: accept both first, fill a frame later.
                ledgerLocker = Build(StorageLockerConfig.ID, Cell(5, 2));
                ledgerLocker.GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { SimHashes.CarbonDioxide.CreateTag(), SimHashes.Water.CreateTag() });
            }
            if (Once(33, "fill locker"))
            {
                var storage = ledgerLocker!.GetComponent<Storage>();
                storage.AddGasChunk(SimHashes.CarbonDioxide, 3, 310, food, 90000, false, true);
                storage.AddLiquid(SimHashes.Water, 12, 290, spores, 30000, false, true);
            }
            if (Once(34, "demolish"))
            {
                recorded["drop co2"] = ArenaSpecies(SimHashes.CarbonDioxide); recorded["drop water"] = ArenaSpecies(SimHashes.Water);
                recorded["drop water items"] = ItemMass(SimHashes.Water);
                recorded["drop food"] = GasGerms(food); recorded["drop spore items"] = ItemGerms(SimHashes.Water, spores);
                recorded["drop stored"] = ledgerLocker!.GetComponent<Storage>().MassStored(); recorded["drop injected"] = ColonyOwner.Status.InjectedKg;
                Debug.Log("[Mixed Atmospheres] storage-ledger locker holds " + string.Join(", ", ledgerLocker.GetComponent<Storage>().items.Select(i => i == null ? "null" : $"{i.GetComponent<PrimaryElement>().ElementID} {i.GetComponent<PrimaryElement>().Mass:G4} kg dumpable {i.GetComponent<Dumpable>() != null}")) + $"; cell {Grid.Element[Cell(5, 2)].id} {Grid.Mass[Cell(5, 2)]:G4} kg owned {ColonyOwner.OwnsFluidCell(Cell(5, 2))}");
                ledgerLocker.GetComponent<Deconstructable>().ForceDestroyAndGetMaterials();
            }
            if (Once(46, "demolition check"))
            {
                var vented = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["drop co2"];
                Record("a demolished building vents its stored gas into the owner, nothing created or lost", recorded["drop stored"] > 14.99 && ledgerLocker == null && Math.Abs(vented - 3) < 2e-3,
                    $"owner +{vented:R} kg (stored 3 kg, all of it vented); locker {(ledgerLocker == null ? "gone" : "present")}; owner injected +{ColonyOwner.Status.InjectedKg - recorded["drop injected"]:G5} kg; cell {Grid.Element[Cell(5, 2)].id} {Grid.Mass[Cell(5, 2)]:G4} kg");
                var germs = GasGerms(food) - recorded["drop food"];
                Record("the vented gas brings its germs to the owner once", germs > .7 * 90000 && germs < 1.1 * 90000, $"+{germs:F0} germs (stored 90000)");
                var chunks = Items(SimHashes.Water).Where(primary => Math.Abs(primary.Mass - 12) < .01f).ToList();
                Record("the liquid is dropped as a chunk of ONI and the owner's water is untouched", chunks.Count == 1 && Math.Abs(ArenaSpecies(SimHashes.Water) - recorded["drop water"]) < 2e-3 &&
                    Math.Abs(ItemMass(SimHashes.Water) - recorded["drop water items"]) < 2e-3, $"{chunks.Count} chunk(s) of 12 kg; owner water {ArenaSpecies(SimHashes.Water) - recorded["drop water"]:G3} kg");
                Record("the dropped chunk keeps its germs and its temperature (ONI's item, nothing taken over)", chunks.Count == 1 && chunks[0].DiseaseIdx == spores && chunks[0].DiseaseCount > .9 * 30000 && chunks[0].DiseaseCount < 1.1 * 30000 &&
                    Math.Abs(chunks[0].Temperature - 290) < .5, $"{chunks.FirstOrDefault()?.DiseaseCount} germs, {chunks.FirstOrDefault()?.Temperature:F3} K");
            }

            // ---- the chunk released back into the world (the 'release' button).
            if (Once(48, "release chunk"))
            {
                var chunk = Items(SimHashes.Water).FirstOrDefault(primary => Math.Abs(primary.Mass - 12) < .01f);
                recorded["release water"] = ArenaSpecies(SimHashes.Water); recorded["release items"] = ItemMass(SimHashes.Water);
                recorded["release spores"] = ArenaGerms(spores); recorded["release found"] = chunk != null ? 1 : 0; recorded["release chunk germs"] = chunk?.DiseaseCount ?? 0;
                var substance = chunk?.GetComponent<SubstanceChunk>();
                if (substance != null) AccessTools.Method(typeof(SubstanceChunk), "OnRelease").Invoke(substance, null);
            }
            if (Once(60, "release check"))
            {
                var gained = ArenaSpecies(SimHashes.Water) - recorded["release water"]; var gone = recorded["release items"] - ItemMass(SimHashes.Water);
                Record("a released chunk returns to the owner exactly", recorded["release found"] == 1 && Math.Abs(gained - 12) < 2e-3 && Math.Abs(gained - gone) < 2e-3, $"owner +{gained:R} kg, chunk -{gone:R} kg");
                var germs = ArenaGerms(spores) - recorded["release spores"];
                Record("the released chunk's germs return once", germs > .8 * recorded["release chunk germs"] && germs < 1.1 * recorded["release chunk germs"], $"+{germs:F0} germs (chunk {recorded["release chunk germs"]:F0})");
            }

            // ---- the gas canister emptier: ModifyCell with the stored gas's germs and temperature.
            if (Once(62, "emptier"))
            {
                // Once spawned, an emptier drops whatever its element filter does not accept: the filter first.
                Build(BottleEmptierGasConfig.ID, Cell(6, 2)).GetComponent<TreeFilterable>().UpdateFilters(new HashSet<Tag> { SimHashes.CarbonDioxide.CreateTag() });
            }
            if (Once(63, "fill emptier"))
            {
                var emptier = powered.Last();
                recorded["empty co2"] = ArenaSpecies(SimHashes.CarbonDioxide); recorded["empty food"] = GasGerms(food);
                emptier.GetComponent<Storage>().AddGasChunk(SimHashes.CarbonDioxide, 4, 305, food, 80000, false, true);
                recorded["empty stored"] = emptier.GetComponent<Storage>().MassStored();
            }
            // The germs in the air of the pool's room (left of the wall at x 3) and of the emptier's room, with what
            // the emptier still stores: one line every five frames while it works.
            if (step >= 35 && step <= 64 && (ColonyFrame() % 5 == 0 || (ColonyFrame() >= 58 && ColonyFrame() <= 68)) && ColonyFrame() != ledgerLogged)
            {
                ledgerLogged = ColonyFrame();
                double left = 0, right = 0;
                for (var y = 2; y < Height; ++y) for (var x = 0; x < Width; ++x)
                {
                    var cell = Cell(x, y);
                    if (!Grid.Solid[cell] && Grid.DiseaseIdx[cell] == food && Grid.DiseaseCount[cell] > 0) { if (x < 3) left += Grid.DiseaseCount[cell]; else right += Grid.DiseaseCount[cell]; }
                }
                Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] storage-ledger emptier f{ColonyFrame()}: air germs pool room {left:F0}, emptier room {right:F0}, stored {(step == 64 ? StoredGerms(powered.Last().GetComponent<Storage>(), food) : 0):F0}, CO2 in the arena {ArenaSpecies(SimHashes.CarbonDioxide):G4} kg, water in rows 2+ {Enumerable.Range(0, Width).Sum(x => Enumerable.Range(2, Height - 2).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Water))):G4} kg"));
            }
            // The emptier works at its own rate: wait until its store is empty, then a few frames for the last injection.
            if (step <= 101 && ColonyFrame() >= 63 && powered.Last().GetComponent<Storage>().MassStored() < 1e-6f && ColonyFrame() >= 70) recorded["emptier done"] = ColonyFrame();
            if (ColonyFrame() >= 100 && (ColonyFrame() >= recorded.GetValueOrDefault("emptier done", 1e9) + 6 || ColonyFrame() >= 400) && Once(100, "emptier check"))
            {
                var emptier = powered.Last();
                var left = emptier.GetComponent<Storage>().MassStored(); var arena = ArenaSpecies(SimHashes.CarbonDioxide) - recorded["empty co2"];
                Record("the emptier moves its gas into the owner, nothing created or lost", recorded["empty stored"] > 3.99 && arena > .05 && Math.Abs(arena + left - 4) < 3e-3,
                    $"owner +{arena:R} kg, still stored {left:R} kg (stored 4 kg); {MachineState(emptier)}");
                var germs = GasGerms(food) - recorded["empty food"]; var stored = StoredGerms(emptier.GetComponent<Storage>(), food);
                Record("the emptied gas brings its germs once", germs > 0 && germs + stored > .85 * 80000 && germs + stored < 1.1 * 80000, $"owner +{germs:F0} germs, still stored {stored:F0} (stored 80000)");
                Finish();
            }
        }
    }
}
