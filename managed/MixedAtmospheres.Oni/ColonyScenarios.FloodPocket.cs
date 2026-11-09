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
        private static int floodLogged = -1;
        private static void FloodPocket(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
            {
                floodLog.Clear(); floodPeak = floodLiquidCo2Peak = 0; floodLogged = -1;
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
            // Once per scenario frame (Update runs on every Unity frame).
            if (frame >= 10 && frame % 10 == 0 && frame <= 400 && frame != floodLogged)
            {
                floodLogged = frame;
                var peak = 0.0; var at = "";
                for (var y = 0; y < FloodHeight; ++y) for (var x = 0; x < Width; ++x)
                {
                    var p = Pressure(Cell(x, y)); if (p > peak) { peak = p; at = x + "," + y; }
                }
                var liquidCo2 = FloodSpecies(SimHashes.LiquidCarbonDioxide);
                var steam = FloodSpecies(SimHashes.Steam);
                floodPeak = Math.Max(floodPeak, peak);
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
                // After the flood the floor holds five rows of water under the
                // compressed oxygen, about 140 kPa; from the dig on, no cell may
                // reach twice that (a 2026-11-08 pool cell read 5.5 MPa).
                Record("no cell held liquid far above its column's pressure", floodPeak < 300e3, FormattableString.Invariant($"peak {floodPeak / 1000:G4} kPa from the dig on"));
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
        private static int cornerLogged = -1;
        private static void FloodCo2Corner(int frame, OniNativeColony.Status status)
        {
            const int height = 8;
            if (Once(0, "arena"))
            {
                cornerLog.Clear(); cornerLogged = -1;
                Arena((x, y) => x == 0 && y == 0 ? (SimHashes.LiquidCarbonDioxide, 70f, 295f) : y <= 5 ? (SimHashes.Water, 1000f, 295f) : (SimHashes.Oxygen, .6f, 295f), Width, height);
            }
            double Species(SimHashes e) => Enumerable.Range(0, Width).Sum(x => Enumerable.Range(0, height).Sum(y => SpeciesMass(Cell(x, y), e)));
            double Pressure(int cell) => MixturePresentationRuntime.TryGetCell(cell, out var r) && r != null ? (r.MechanicalPressurePa ?? r.TotalPressurePa) : 0;
            if (frame >= 6 && frame % 10 == 6 && frame <= 306 && frame != cornerLogged)
            {
                cornerLogged = frame;
                cornerLog.Add(FormattableString.Invariant($"f{frame} corner {Pressure(Cell(0, 0)) / 1000:G4} kPa {CellMass(Cell(0, 0)):G5} kg; above {Pressure(Cell(0, 1)) / 1000:G4} kPa; liquid CO2 {Species(SimHashes.LiquidCarbonDioxide):G4} kg, CO2 gas {Species(SimHashes.CarbonDioxide):G4} kg"));
            }
            // At 295 K the liquid is far above its boiling point at the pool's
            // pressure (about 270 K at 400 kPa in the mod's EOS; 225 K at 1 atm)
            // and the water around it holds the heat: it flashes within seconds.
            if (Once(106, "boiling")) recorded["liquid at 20 s"] = Species(SimHashes.LiquidCarbonDioxide);
            if (Once(306, "check"))
            {
                Debug.Log("[Mixed Atmospheres] flood-co2-corner: " + string.Join(" | ", cornerLog));
                var liquid = Species(SimHashes.LiquidCarbonDioxide);
                Record("room-temperature liquid carbon dioxide boiled within 20 s", recorded["liquid at 20 s"] < .01 * 70,
                    FormattableString.Invariant($"70 -> {recorded["liquid at 20 s"]:G4} kg"));
                Record("room-temperature liquid carbon dioxide boiled away", liquid < .01 * 70, FormattableString.Invariant($"70 -> {liquid:G4} kg"));
                // The arena is sealed: its 70 kg of carbon dioxide, now gas in
                // the two top rows, raise the whole pool's pressure (about 360
                // kPa at the floor). The corner must sit at its column's
                // pressure, four cells of water (39 kPa) below cell (0, 4).
                var excess = Pressure(Cell(0, 0)) - Pressure(Cell(0, 4));
                Record("the corner returned to its column's pressure", Math.Abs(excess - 4 * 9.81e3) < 10e3,
                    FormattableString.Invariant($"{Pressure(Cell(0, 0)) / 1000:G4} kPa, {excess / 1000:G4} kPa above cell (0, 4)"));
                Record("the corner's ledgers stay closed", status.State == 1 && status.MaximumRelativeMassError <= 1e-12, FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}"));
                Finish();
            }
        }
    }
}
