using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // A water pool drains through a dug hole into an air pocket below it
        // (2026-11-08 playtest: a flooded pool corner held water with liquid
        // carbon dioxide at 5.5 MPa beside a near-vacuum cell). The pocket's
        // gas must rise through the pool rather than be squeezed into liquid
        // cells, and no liquid may sit at a pressure far above its column.
        private const int FloodHeight = 12;
        private static readonly List<string> floodLog = new List<string>();
        private static double floodPeak, floodLiquidCo2Peak;
        private static void FloodPocket(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
            {
                floodLog.Clear(); floodPeak = floodLiquidCo2Peak = 0;
                Arena((x, y) => y <= 1 ? (SimHashes.CarbonDioxide, 1f, 295f) : y <= 3 ? (SimHashes.Oxygen, .6f, 295f) :
                    y == 4 ? (SimHashes.SandStone, 1000f, 295f) : y <= 9 ? (SimHashes.Water, 1000f, 295f) : (SimHashes.Oxygen, .6f, 295f),
                    Width, FloodHeight);
            }
            double Rows(int from, int to, Func<int, double> each)
            {
                double t = 0; for (var y = from; y <= to; ++y) for (var x = 0; x < Width; ++x) t += each(Cell(x, y)); return t;
            }
            double Gas(int cell) => CellMass(cell) - LiquidMass(cell);
            double Pressure(int cell) => MixturePresentationRuntime.TryGetCell(cell, out var r) && r != null ? (r.MechanicalPressurePa ?? r.TotalPressurePa) : 0;
            if (Once(6, "start"))
            {
                recorded["pocket"] = Rows(0, 3, Gas); recorded["co2"] = FloodSpecies(SimHashes.CarbonDioxide);
                recorded["water"] = FloodSpecies(SimHashes.Water);
            }
            if (Once(8, "dig")) { SimMessages.Dig(Cell(3, 4)); SimMessages.Dig(Cell(4, 4)); }
            if (frame >= 10 && frame % 10 == 0 && frame <= 400 && (floodLog.Count == 0 || !floodLog[floodLog.Count - 1].StartsWith("f" + frame + " ")))
            {
                var peak = 0.0; var at = "";
                for (var y = 0; y < FloodHeight; ++y) for (var x = 0; x < Width; ++x)
                {
                    var p = Pressure(Cell(x, y)); if (p > peak) { peak = p; at = x + "," + y; }
                }
                var liquidCo2 = FloodSpecies(SimHashes.LiquidCarbonDioxide);
                var steam = FloodSpecies(SimHashes.Steam);
                if (frame >= 60) floodPeak = Math.Max(floodPeak, peak);
                floodLiquidCo2Peak = Math.Max(floodLiquidCo2Peak, liquidCo2);
                floodLog.Add(FormattableString.Invariant($"f{frame} peak {peak / 1000:G4} kPa at {at}; pocket gas {Rows(0, 3, Gas):G4} kg, water low {Rows(0, 3, LiquidMass):G6} kg; liquid CO2 {liquidCo2:G4} kg; steam {steam:G4} kg"));
            }
            if (Once(400, "check"))
            {
                Debug.Log("[Mixed Atmospheres] flood-pocket: " + string.Join(" | ", floodLog));
                var pocket = Rows(0, 3, Gas);
                Record("the pocket's gas rose through the pool", pocket <= .1 * recorded["pocket"], FormattableString.Invariant($"{recorded["pocket"]:G4} -> {pocket:G4} kg below the hole"));
                // 32 pocket cells below 40 cells of water: most of the pool drains down.
                Record("the water filled the pocket", Rows(0, 3, LiquidMass) > 25000,
                    FormattableString.Invariant($"{Rows(0, 3, LiquidMass):G6} kg of liquid in the pocket rows"));
                Record("no carbon dioxide liquefied at room temperature", floodLiquidCo2Peak < 1e-3, FormattableString.Invariant($"peak liquid CO2 {floodLiquidCo2Peak:G4} kg"));
                Record("no cell held liquid far above its column's pressure", floodPeak < 400e3, FormattableString.Invariant($"peak {floodPeak / 1000:G4} kPa after 12 s"));
                Record("flooding keeps closed ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                Finish();
            }
        }
        private static double FloodSpecies(SimHashes element) =>
            Enumerable.Range(0, Width).Sum(x => Enumerable.Range(0, FloodHeight).Sum(y => SpeciesMass(Cell(x, y), element)));
    }
}
namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Liquid carbon dioxide at room temperature in the bottom corner of a
        // full water pool (the 2026-11-08 playtest cell: 1,070 kg, 5.51 MPa)
        // must boil away and leave the pool at its hydrostatic pressure.
        private static readonly List<string> cornerLog = new List<string>();
        private static void FloodCo2Corner(int frame, OniNativeColony.Status status)
        {
            const int height = 8;
            if (Once(0, "arena"))
            {
                cornerLog.Clear();
                Arena((x, y) => x == 0 && y == 0 ? (SimHashes.LiquidCarbonDioxide, 70f, 295f) : y <= 5 ? (SimHashes.Water, 1000f, 295f) : (SimHashes.Oxygen, .6f, 295f), Width, height);
            }
            double Species(SimHashes e) => Enumerable.Range(0, Width).Sum(x => Enumerable.Range(0, height).Sum(y => SpeciesMass(Cell(x, y), e)));
            double Pressure(int cell) => MixturePresentationRuntime.TryGetCell(cell, out var r) && r != null ? (r.MechanicalPressurePa ?? r.TotalPressurePa) : 0;
            if (frame >= 6 && frame % 10 == 6 && frame <= 306 && (cornerLog.Count == 0 || !cornerLog[cornerLog.Count - 1].StartsWith("f" + frame + " ")))
                cornerLog.Add(FormattableString.Invariant($"f{frame} corner {Pressure(Cell(0, 0)) / 1000:G4} kPa {CellMass(Cell(0, 0)):G5} kg; above {Pressure(Cell(0, 1)) / 1000:G4} kPa; liquid CO2 {Species(SimHashes.LiquidCarbonDioxide):G4} kg, CO2 gas {Species(SimHashes.CarbonDioxide):G4} kg"));
            if (Once(306, "check"))
            {
                Debug.Log("[Mixed Atmospheres] flood-co2-corner: " + string.Join(" | ", cornerLog));
                var liquid = Species(SimHashes.LiquidCarbonDioxide);
                Record("room-temperature liquid carbon dioxide boiled away", liquid < .01 * 70, FormattableString.Invariant($"70 -> {liquid:G4} kg"));
                Record("the corner returned to its column's pressure", Pressure(Cell(0, 0)) < 200e3, FormattableString.Invariant($"{Pressure(Cell(0, 0)) / 1000:G4} kPa"));
                Record("the corner's ledgers stay closed", status.State == 1 && status.MaximumRelativeMassError <= 1e-12, FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}"));
                Finish();
            }
        }
    }
}
