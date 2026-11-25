using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A Sanishell cleans the water it stands in: while it cleans, its
    // DiseaseEmitter emits a negative germ count for each disease it knows
    // (CrabFreshWaterConfig, CreatureDiseaseCleaner). Two equal pools of
    // water with food poisoning germs, a wall between them, a Sanishell with
    // its emitter on in the left one: the left pool must lose germs the
    // right one keeps. Also runs under ONI's own simulation
    // (--mode colony-vanilla), where it shows what ONI does.
    internal static partial class ColonyScenarios
    {
        private static GameObject? cleaner;
        private static double PoolGerms(byte disease, int from, int to) =>
            Enumerable.Range(from, to - from + 1).Sum(x => Enumerable.Range(0, 3).Sum(y => CellGerms(Cell(x, y), disease)));
        private static void GermCleaner(int frame)
        {
            if (Once(0, "arena"))
            {
                cleaner = null;
                Arena((x, y) => x == 4 ? (SimHashes.Granite, 1000f, 300f) : y <= 2 ? (SimHashes.Water, 800f, 300f) : (SimHashes.Oxygen, 1f, 300f));
            }
            if (Once(8, "germs"))
            {
                var food = Db.Get().Diseases.GetIndex(Db.Get().Diseases.FoodGerms.id); recorded["food"] = food;
                for (var y = 0; y <= 2; ++y) for (var x = 0; x < Width; ++x)
                    if (x != 4) SimMessages.ModifyDiseaseOnCell(Cell(x, y), food, 20000);
            }
            if (Once(16, "sanishell"))
            {
                var food = (byte)recorded["food"];
                recorded["left"] = PoolGerms(food, 0, 3); recorded["right"] = PoolGerms(food, 5, 7);
                cleaner = Util.KInstantiate(Assets.GetPrefab("CrabFreshWater"), Grid.CellToPosCBC(Cell(1, 0), Grid.SceneLayer.Creatures));
                cleaner.SetActive(true);
            }
            // The scenario holds the emitter on, as the Sanishell's cleaning state does, wherever the critter walks in its pool.
            if (frame > 16 && frame < 180 && cleaner != null) cleaner.GetComponent<DiseaseEmitter>().SetEnable(true);
            if (Once(180, "check"))
            {
                var food = (byte)recorded["food"];
                double left = PoolGerms(food, 0, 3), right = PoolGerms(food, 5, 7);
                double leftLost = recorded["left"] - left, rightLost = recorded["right"] - right;
                // Per cell, so that the pools' sizes (four columns and three) do not matter.
                double leftPerCell = leftLost / 12, rightPerCell = rightLost / 9;
                Record("both pools were seeded with germs", recorded["left"] > 100000 && recorded["right"] > 100000, $"left {recorded["left"]:G6}, right {recorded["right"]:G6} germs");
                Record("the Sanishell's pool lost germs that the other pool kept", cleaner != null && leftPerCell > rightPerCell + 2000,
                    $"left pool {recorded["left"]:G6} -> {left:G6} ({leftPerCell:G5} lost per cell); right pool {recorded["right"]:G6} -> {right:G6} ({rightPerCell:G5} lost per cell); " +
                    (cleaner != null ? $"Sanishell at column {Grid.CellToXY(Grid.PosToCell(cleaner)).x - x0}" : "the Sanishell is gone"));
                if (!Vanilla) LedgersClosed("germ cleaner");
                Finish();
            }
        }
    }
}
