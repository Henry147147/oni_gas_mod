using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // The phase the game's grid shows (docs/colony-owner.md#the-shown-phase,
        // user decision 2026-11-23): a cell that holds any liquid, from 0.001 %
        // of its volume (the share from which the readings report liquid), is a
        // liquid tile to the game, however much air is in it. A sealed room of
        // oxygen with 30 kg of water put into one floor cell: the water spreads
        // into a film a few kilograms deep, far below half a cell, which until
        // 1.13.0 the game could not see.
        private static double slWater0, slOxygen0;
        private static GameObject? slPlant, slVent;
        private static void ShownLiquid(int frame, OniNativeColony.Status status)
        {
            const int source = 3, check = 300;
            const float emitted = 0.5f, poured = 0.2f;
            const double meaningful = MixedAtmospheres.Core.MixturePresentationModel.MeaningfulLiquidVolumeFraction;
            if (Once(0, "arena"))
            {
                slWater0 = slOxygen0 = 0; slPlant = slVent = null;
                Arena((x, y) => x == source && y == 0 ? (SimHashes.Water, 30f, 300f) : (SimHashes.Oxygen, 1.8f, 300f));
            }
            if (Once(6, "start")) { slWater0 = ArenaSpecies(SimHashes.Water); slOxygen0 = ArenaSpecies(SimHashes.Oxygen); }
            if (Once(check, "check"))
            {
                int wet = 0, wetShown = 0, wetWrong = 0, dryShownLiquid = 0, traceCells = 0, breathable = 0, moppable = 0;
                double worstMass = 0, deepest = 0; var detail = new List<string>();
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                {
                    var cell = Cell(x, y);
                    if (!ColonyOwner.TryReadView(cell, out var view)) continue;
                    var water = SpeciesMass(cell, SimHashes.Water); var gas = CellMass(cell) - LiquidMass(cell);
                    var shown = Grid.Element[cell];
                    if (view.LiquidFraction >= meaningful)
                    {
                        ++wet; deepest = Math.Max(deepest, view.LiquidFraction);
                        if (shown.IsLiquid && shown.id == SimHashes.Water)
                        {
                            ++wetShown; worstMass = Math.Max(worstMass, Math.Abs(Grid.Mass[cell] - water) / water);
                            // The mop's own test of a cell: a liquid on a floor, at most 150 kg.
                            if (Grid.Solid[Grid.CellBelow(cell)] && Grid.Mass[cell] <= MopTool.maxMopAmt) ++moppable;
                        }
                        else { ++wetWrong; if (detail.Count < 4) detail.Add($"({x},{y}) {water:G3} kg water shown as {shown.id}"); }
                        // The air in a wet cell is still air to a duplicant.
                        if (gas > 0 && ColonyOwner.TryReadBreathingCell(cell, out var breath, out var element) && breath > 0 && element == SimHashes.Oxygen) ++breathable;
                    }
                    else
                    {
                        if (water > 0) ++traceCells;
                        if (shown.IsLiquid) { ++dryShownLiquid; if (detail.Count < 4) detail.Add($"({x},{y}) {water:G3} kg water, fraction {view.LiquidFraction:G3}, shown as {shown.id}"); }
                    }
                }
                Record("the water spread into a film below half a cell", wet >= 4 && deepest < 0.5,
                    FormattableString.Invariant($"{wet} cells hold liquid, the deepest {100 * deepest:F2} % of its cell"));
                Record("every cell that holds liquid is a liquid tile to the game", wet > 0 && wetShown == wet && wetWrong == 0,
                    FormattableString.Invariant($"{wetShown} of {wet} wet cells show Water") + (detail.Count > 0 ? "; " + string.Join("; ", detail) : ""));
                Record("the tile's mass is its liquid's", wetShown > 0 && worstMass <= 1e-4,
                    FormattableString.Invariant($"largest difference between the grid's mass and the cell's water {worstMass:P4}"));
                Record("a cell without liquid, or with only a trace, shows its gas", dryShownLiquid == 0,
                    FormattableString.Invariant($"{dryShownLiquid} cells below 0.001 % show a liquid ({traceCells} hold a trace)"));
                Record("the air above a film stays breathable", breathable == wet,
                    FormattableString.Invariant($"{breathable} of {wet} wet cells read as oxygen for breathing"));
                Record("the mop's test finds the film", moppable >= 4, FormattableString.Invariant($"{moppable} wet floor cells pass the mop tool's test"));
                var waterNow = ArenaSpecies(SimHashes.Water); var oxygenNow = ArenaSpecies(SimHashes.Oxygen);
                Record("water and oxygen were retained and the ledgers closed", Math.Abs(waterNow - slWater0) <= 1e-6 * slWater0 && Math.Abs(oxygenNow - slOxygen0) <= 1e-6 * slOxygen0 &&
                    status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"water {slWater0:G7} -> {waterNow:G7} kg, oxygen {slOxygen0:G7} -> {oxygenNow:G7} kg; mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                // The game's readers of the air (ColonyAtmosphereReaders) read the gas
                // of a cell that shows a film: its leading gas and the mass of its gas.
                int films = 0, filmsRead = 0; double worstGas = 0;
                for (var x = 0; x < Width; ++x)
                {
                    var cell = Cell(x, 0);
                    if (!ColonyOwner.TryReadView(cell, out var view) || !(view.LiquidFraction >= meaningful) || !Grid.Element[cell].IsLiquid) continue;
                    ++films; var gas = CellMass(cell) - LiquidMass(cell);
                    var element = ColonyAtmosphereReaders.Element(Grid.Element, cell); var mass = ColonyAtmosphereReaders.Mass(ref Grid.Mass, cell);
                    if (element.id == SimHashes.Oxygen && ColonyAtmosphereReaders.IsGas(cell) && !ColonyAtmosphereReaders.IsLiquid(cell)) ++filmsRead;
                    worstGas = Math.Max(worstGas, Math.Abs(mass - gas) / gas);
                }
                Record("a reader of the air reads the gas of a tile that shows a film", films >= 4 && filmsRead == films && worstGas <= 1e-4 && ColonyAtmosphereReaders.Rewritten >= 20,
                    FormattableString.Invariant($"{filmsRead} of {films} film tiles read as Oxygen, gas mass within {worstGas:P4}; {ColonyAtmosphereReaders.Rewritten} grid reads of the game rewritten"));
                // A one-tile plant and a gas vent standing in the film meet it through the game's own code.
                slPlant = GameUtil.KInstantiate(Assets.GetPrefab("PrickleGrass"), Grid.CellToPosCBC(Cell(2, 0), Grid.SceneLayer.BuildingFront), Grid.SceneLayer.BuildingFront);
                slPlant.SetActive(true);
                slVent = Build(GasVentConfig.ID, Cell(6, 0));
                // The game's own emitters meet the shown tiles: gas let out into a
                // tile that shows water, and water let out into a tile of air.
                SimMessages.AddRemoveSubstance(Cell(5, 0), SimHashes.Oxygen, CellEventLogger.Instance.OxygenModifierSimUpdate, emitted, 300, byte.MaxValue, 0);
                SimMessages.AddRemoveSubstance(Cell(1, 3), SimHashes.Water, CellEventLogger.Instance.OxygenModifierSimUpdate, poured, 300, byte.MaxValue, 0);
            }
            if (Once(check + 60, "emitted"))
            {
                var waterNow = ArenaSpecies(SimHashes.Water); var oxygenNow = ArenaSpecies(SimHashes.Oxygen);
                var floor = Cell(5, 0);
                Record("gas let out into a tile that shows liquid joins its air", Math.Abs(oxygenNow - slOxygen0 - emitted) <= 1e-4 * emitted,
                    FormattableString.Invariant($"oxygen {slOxygen0:G7} -> {oxygenNow:G7} kg for {emitted} kg let out"));
                Record("liquid let out into a tile of air joins the film", Math.Abs(waterNow - slWater0 - poured) <= 1e-4 * poured,
                    FormattableString.Invariant($"water {slWater0:G7} -> {waterNow:G7} kg for {poured} kg let out"));
                Record("the tile the gas entered still shows its water", Grid.Element[floor].id == SimHashes.Water && SpeciesMass(floor, SimHashes.Water) > 0.01,
                    FormattableString.Invariant($"shows {Grid.Element[floor].id}, {SpeciesMass(floor, SimHashes.Water):G4} kg of water"));
                var plantCell = Cell(2, 0); var ventCell = Cell(6, 0);
                var vulnerable = slPlant != null ? slPlant.GetComponent<PressureVulnerable>() : null;
                Record("a plant standing in a film finds its air", vulnerable != null && Grid.Element[plantCell].IsLiquid && vulnerable.testAreaElementSafe && vulnerable.IsNormal &&
                    vulnerable.currentAtmoElement != null && vulnerable.currentAtmoElement.id == SimHashes.Oxygen,
                    vulnerable == null ? "no plant" : FormattableString.Invariant($"tile shows {Grid.Element[plantCell].id} {Grid.Mass[plantCell]:G3} kg; atmosphere {vulnerable.currentAtmoElement?.id}, safe {vulnerable.testAreaElementSafe}, pressure normal {vulnerable.IsNormal}"));
                var vent = slVent != null ? slVent.GetComponent<Vent>() : null;
                var open = vent != null && Traverse.Create(vent).Method("IsValidOutputCell", new[] { typeof(int) }).GetValue<bool>(ventCell);
                var ventGas = CellMass(ventCell) - LiquidMass(ventCell);
                Record("a gas vent in a film judges the room's gas, not the film", vent != null && Grid.Element[ventCell].IsLiquid && Grid.Mass[ventCell] > vent.overpressureMass &&
                    ventGas < vent.overpressureMass && open,
                    vent == null ? "no vent" : FormattableString.Invariant($"tile shows {Grid.Element[ventCell].id} {Grid.Mass[ventCell]:G3} kg over {ventGas:G3} kg of gas; limit {vent.overpressureMass} kg, vent open {open}"));
                Record("the ledgers stayed closed", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                Finish();
            }
        }
    }
}
