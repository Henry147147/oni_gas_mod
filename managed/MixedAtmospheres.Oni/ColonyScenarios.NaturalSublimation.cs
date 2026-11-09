using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Natural sublimation (docs/colony-owner.md#natural-sublimation), in
        // three sealed chambers of one 9x6 arena of thin oxygen:
        // A (x 0..1, y 0..1): an oxylite tile on the floor at (0,0) under a
        //   three-cell pocket; its oxygen fills the pocket to ONI's 1.8 kg
        //   and stops. Above the pocket's ceiling an oxylite tile at (0,4) is
        //   enclosed in rock and emits nothing.
        // B (x 3..4): a polluted dirt tile at (3,0) gives off polluted oxygen.
        // C (x 6..8): polluted water (y 0..1) gives off polluted oxygen at
        //   its surface into the oxygen above.
        // Also runs under ONI's own simulation (--mode colony-vanilla), its
        // reference: the same checks hold there.
        private static readonly (int x, int y)[] PocketA = { (1, 0), (0, 1), (1, 1) };
        private static (SimHashes, float, float) NaturalSublimationCell(int x, int y)
        {
            if (x == 2 || x == 5) return (SimHashes.Unobtanium, 20000, 300);
            if (x <= 1)
            {
                if (x == 0 && y == 0) return (SimHashes.OxyRock, 200, 300);
                if (x == 0 && y == 4) return (SimHashes.OxyRock, 200, 300);
                if (y >= 2) return (SimHashes.Unobtanium, 20000, 300);
                return (SimHashes.Oxygen, .1f, 300);
            }
            if (x <= 4) return x == 3 && y == 0 ? (SimHashes.ToxicSand, 500, 300) : (SimHashes.Oxygen, .1f, 300);
            return y <= 1 ? (SimHashes.DirtyWater, 900, 300) : (SimHashes.Oxygen, .1f, 300);
        }
        private static double Chamber(int fromX, int toX, SimHashes element) =>
            Enumerable.Range(fromX, toX - fromX + 1).Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), element)));
        private static double TileMass(int x, int y) => Grid.Solid[Cell(x, y)] ? Grid.Mass[Cell(x, y)] : 0;
        // The game's cell inspector: "Emitting <gas>: <rate>" reads this (kg/s).
        private static double InspectorRate(int x, int y) => Grid.AccumulatedFlow[Cell(x, y)] / 3f;
        private static string PocketDetail() => string.Join(" ", PocketA.Select(p => $"({p.x},{p.y}) {CellMass(Cell(p.x, p.y)):G4}"));

        private static void NaturalSublimation(int frame)
        {
            if (Once(0, "arena")) Arena(NaturalSublimationCell, 9, 6);
            if (Once(8, "baseline"))
            {
                recorded["oxylite"] = TileMass(0, 0); recorded["enclosed"] = TileMass(0, 4); recorded["dirt"] = TileMass(3, 0);
                recorded["A oxygen"] = PocketA.Sum(p => SpeciesMass(Cell(p.x, p.y), SimHashes.Oxygen));
                recorded["B polluted"] = Chamber(3, 4, SimHashes.ContaminatedOxygen);
                recorded["C polluted"] = Chamber(6, 8, SimHashes.ContaminatedOxygen); recorded["C water"] = Chamber(6, 8, SimHashes.DirtyWater);
                recorded["rate"] = InspectorRate(0, 0);
                Debug.Log($"[Mixed Atmospheres] natural sublimation frame {frame}: oxylite {recorded["oxylite"]:G6} kg, pocket {PocketDetail()}, " +
                    $"dirt {recorded["dirt"]:G6} kg, C water {recorded["C water"]:G7} kg polluted {recorded["C polluted"]:G4} kg, inspector {recorded["rate"]:G4} kg/s");
            }
            if (frame > 8 && frame <= 40 && recorded.ContainsKey("rate")) recorded["rate"] = Math.Max(recorded["rate"], InspectorRate(0, 0));
            if (frame > 8 && frame <= 40 && frame % 4 == 0 && Once(frame, "emitting " + frame))
            {
                Debug.Log($"[Mixed Atmospheres] natural sublimation frame {frame}: oxylite {TileMass(0, 0):G6} kg, pocket {PocketDetail()}, inspector {InspectorRate(0, 0):G4} kg/s");
            }
            if (Once(41, "filling"))
            {
                var oxygen = PocketA.Sum(p => SpeciesMass(Cell(p.x, p.y), SimHashes.Oxygen)) - recorded["A oxygen"];
                Record("an exposed oxylite tile releases oxygen into thin gas", oxygen > .5 && TileMass(0, 0) < recorded["oxylite"] - 1,
                    $"+{oxygen:G4} kg oxygen, tile {recorded["oxylite"]:G6} -> {TileMass(0, 0):G6} kg; pocket {PocketDetail()}");
                Record("the cell inspector shows the tile emitting", recorded["rate"] > .05, $"up to {recorded["rate"]:G4} kg/s");
            }
            if (Once(200, "held")) { recorded["held tile"] = TileMass(0, 0); recorded["held oxygen"] = PocketA.Sum(p => SpeciesMass(Cell(p.x, p.y), SimHashes.Oxygen)); }
            if (Once(240, "check"))
            {
                var pocket = PocketA.Select(p => CellMass(Cell(p.x, p.y))).ToArray();
                var adjacent = Math.Max(pocket[0], pocket[1]);
                var late = recorded["held tile"] - TileMass(0, 0);
                Record("emission stops near ONI's 1.8 kg", adjacent >= 1.7 && adjacent <= 1.95 && late < .05,
                    $"pocket {PocketDetail()} kg; tile {recorded["held tile"]:G6} -> {TileMass(0, 0):G6} kg over the last 40 frames; inspector {InspectorRate(0, 0):G4} kg/s");
                Record("an enclosed oxylite tile emits nothing", TileMass(0, 4) == recorded["enclosed"], $"{recorded["enclosed"]:G9} -> {TileMass(0, 4):G9} kg");
                var polluted = Chamber(3, 4, SimHashes.ContaminatedOxygen) - recorded["B polluted"];
                Record("a polluted dirt tile releases polluted oxygen", polluted > 1e-3 && TileMass(3, 0) < recorded["dirt"],
                    $"+{polluted:G4} kg polluted oxygen, tile {recorded["dirt"]:G7} -> {TileMass(3, 0):G7} kg");
                var offgas = Chamber(6, 8, SimHashes.ContaminatedOxygen) - recorded["C polluted"];
                var water = recorded["C water"] - Chamber(6, 8, SimHashes.DirtyWater);
                Record("polluted water off-gasses at its surface", offgas > .1 && Math.Abs(offgas - water) <= 1e-3 * Math.Max(1, water),
                    $"+{offgas:G6} kg polluted oxygen, -{water:G6} kg polluted water; above the surface {string.Join(" ", Enumerable.Range(6, 3).Select(x => $"{SpeciesMass(Cell(x, 2), SimHashes.ContaminatedOxygen):G3}"))}");
                if (!Vanilla)
                {
                    var t = ColonySublimation.Read();
                    Record("the owner booked natural sublimation", ColonySublimation.Supported && t.Events > 0 && t.ProducedKg > 0,
                        $"{ColonySublimation.Rules} rules; {t.Events} events, tiles {t.TileKg:G6} kg, liquid {t.LiquidKg:G6} kg, produced {t.ProducedKg:G6} kg, used up {t.TilesUsedUp}");
                }
                LedgersClosed("natural sublimation");
                Finish();
            }
        }
    }
}
