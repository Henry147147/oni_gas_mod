using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Unusual forms of messages the game sends to its simulation, which the
    // owner's message boundary also sees (found by reading every sender of
    // each message, 2026-11-25): a withdrawal from a rectangle instead of a
    // radius (the Pitcher Pump), a withdrawal from a solid tile (a Beeta
    // mining uranium ore) and a dig of the backwall alone. Each check states
    // what ONI's own simulation does, and the scenario also runs under it
    // (--mode colony-vanilla) to prove that.
    internal static partial class ColonyScenarios
    {
        private static float edgeRectangleKg, edgeMinedKg;
        private static int edgeRectangleCalls, edgeMinedCalls;
        private static (SimHashes, float, float) MessageEdgeCell(int x, int y)
        {
            var ore = ElementLoader.FindElementByHash(SimHashes.UraniumOre) is Element uranium && !uranium.disabled ? SimHashes.UraniumOre : SimHashes.Granite;
            if ((x == 1 || x == 4) && y <= 2) return (SimHashes.Granite, 1000f, 300f);
            if ((x == 0 || x == 2 || x == 3) && y <= 1) return (SimHashes.Water, 500f, 300f);
            if (x == 6 && y == 0) return (ore, 800f, 300f);
            if (x == 7 && y == 0) return (SimHashes.Granite, 1000f, 300f);
            return (SimHashes.Oxygen, 1f, 300f);
        }
        private static double EdgeWater(params int[] columns) => columns.Sum(x => Enumerable.Range(0, Height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Water)));
        private static void MessageEdges(int frame)
        {
            if (Once(0, "arena")) { edgeRectangleKg = edgeMinedKg = 0; edgeRectangleCalls = edgeMinedCalls = 0; Arena(MessageEdgeCell); }
            if (Once(12, "send"))
            {
                recorded["side pool"] = EdgeWater(0); recorded["pumped pool"] = EdgeWater(2, 3);
                recorded["ore"] = Grid.Mass[Cell(6, 0)]; recorded["wall"] = Grid.Mass[Cell(7, 0)];
                // The Pitcher Pump's form: two cells wide, two high, anchored at the bottom left of its intake.
                var rectangle = Game.Instance.massConsumedCallbackManager.Add((info, data) => { edgeRectangleKg += info.mass; ++edgeRectangleCalls; }, null, "MessageEdgesRectangle");
                SimMessages.ConsumeMass(Cell(2, 0), SimHashes.Water, 100f, 2, 2, rectangle.index);
                // The Beeta's form: a radius of one around a solid ore tile, the tile's own element.
                var mined = Game.Instance.massConsumedCallbackManager.Add((info, data) => { edgeMinedKg += info.mass; ++edgeMinedCalls; }, null, "MessageEdgesMine");
                SimMessages.ConsumeMass(Cell(6, 0), Grid.Element[Cell(6, 0)].id, 10f, 1, mined.index);
                SimMessages.Dig(Cell(7, 0), -1, skipEvent: true, backwall: true);
            }
            if (Once(24, "check"))
            {
                double side = EdgeWater(0) - recorded["side pool"], pumped = EdgeWater(2, 3) - recorded["pumped pool"];
                Record("a rectangular withdrawal answers with what it took", edgeRectangleCalls == 1 && edgeRectangleKg > 50 && edgeRectangleKg <= 100.001f,
                    $"{edgeRectangleCalls} callback(s), {edgeRectangleKg:G6} kg of the 100 kg asked");
                Record("a rectangular withdrawal takes from its rectangle only", Math.Abs(side) < 1e-3 && Math.Abs(pumped + edgeRectangleKg) < 1e-2,
                    $"pool in the rectangle {pumped:G6} kg, pool two cells to its side behind a wall {side:G6} kg");
                var ore = Grid.Mass[Cell(6, 0)] - recorded["ore"];
                Record("a withdrawal from a solid tile takes the tile's mass (a Beeta mining ore)", edgeMinedCalls == 1 && edgeMinedKg > 1 && ore < -1,
                    $"{edgeMinedCalls} callback(s), {edgeMinedKg:G6} kg; the {Grid.Element[Cell(6, 0)].id} tile {recorded["ore"]:G6} -> {Grid.Mass[Cell(6, 0)]:G6} kg");
                var wall = Cell(7, 0);
                Record("a backwall-only dig leaves the tile in front closed", Grid.Solid[wall] && Grid.Mass[wall] == (float)recorded["wall"] && (Vanilla || !ColonyOwner.OwnsFluidCell(wall)),
                    $"solid {Grid.Solid[wall]}, {Grid.Mass[wall]:G6} kg" + (Vanilla ? "" : $", owner holds it open {ColonyOwner.OwnsFluidCell(wall)}"));
                if (!Vanilla) LedgersClosed("message edges");
                Finish();
            }
        }
    }
}
