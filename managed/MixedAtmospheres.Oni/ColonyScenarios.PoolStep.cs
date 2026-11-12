using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // 2026-11-11 playtest (0.1.4): a 35-tile pool kept a half-tile step
        // that sloshed from side to side for over a minute after a shaft was
        // dug under it, and the rooms beside it held about 2% steam at room
        // temperature. This sealed pool starts with that step: the surface
        // row a quarter full on its left half and three quarters on its
        // right. It must level without changing sides, be flat within twenty
        // seconds, and make no steam on the way.
        private static double poolReversed, poolLate, poolSteam;
        private static int poolLogged;
        private static readonly List<string> poolLog = new List<string>();
        private static void PoolStep(int frame, OniNativeColony.Status status)
        {
            const int width = 35, height = 7;
            if (Once(0, "arena"))
            {
                poolReversed = poolLate = poolSteam = 0; poolLogged = -1; poolLog.Clear();
                pendingShift = width - Width;
                Arena((x, y) => y < 3 ? (SimHashes.Water, 1000f, 295f) :
                    y == 3 ? (SimHashes.Water, x < width / 2 ? 250f : 750f, 295f) :
                    (SimHashes.Oxygen, 1.8f, 295f), width, height);
            }
            double Column(int x) => Enumerable.Range(0, height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Water));
            double Steam() => Enumerable.Range(0, width).Sum(x => Enumerable.Range(0, height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Steam)));
            if (Once(6, "start")) recorded["pool water"] = Enumerable.Range(0, width).Sum(Column) + Steam();
            if (frame >= 6 && frame <= 306 && frame != poolLogged)
            {
                poolLogged = frame;
                var columns = Enumerable.Range(0, width).Select(Column).ToArray();
                var left = columns.Take(width / 2).Average(); var right = columns.Skip(width / 2).Average();
                var range = columns.Max() - columns.Min(); var steam = Steam();
                poolReversed = Math.Min(poolReversed, right - left);
                poolSteam = Math.Max(poolSteam, steam);
                if (frame >= 106) poolLate = Math.Max(poolLate, range);
                if (frame % 25 == 6)
                    poolLog.Add(FormattableString.Invariant($"f{frame} right-left {right - left:G5} kg, range {range:G5} kg, steam {steam:G4} kg"));
            }
            if (Once(306, "check"))
            {
                Debug.Log("[Mixed Atmospheres] pool-step: " + string.Join(" | ", poolLog));
                Record("the step levelled without sloshing to the other side", poolReversed > -25,
                    FormattableString.Invariant($"step 500 kg a column; lowest right-left {poolReversed:G5} kg"));
                Record("the pool was flat within twenty seconds", poolLate < 25,
                    FormattableString.Invariant($"largest column range from frame 106 to 306: {poolLate:G5} kg"));
                Record("levelling made no steam", poolSteam < 1e-3, FormattableString.Invariant($"most steam in the arena {poolSteam:G4} kg"));
                var water = Enumerable.Range(0, width).Sum(Column) + Steam();
                Record("the pool retained its water", Math.Abs(water - recorded["pool water"]) < 1e-6 * recorded["pool water"],
                    FormattableString.Invariant($"{recorded["pool water"]:G9} -> {water:G9} kg"));
                Record("levelling keeps closed ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                Finish();
            }
        }
    }
}
