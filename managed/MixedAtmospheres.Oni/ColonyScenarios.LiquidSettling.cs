using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // 2026-11-09 playtest: partial surface cells exchange hundreds of kg
        // under quiet air. This sealed basin has no machines or later edits.
        // Its alternating 300/700 kg surface must settle across sharpening
        // and active/remote transitions, without gathering into tall columns.
        private static double settlingHeight;
        private static int settlingLogged;
        private static readonly List<string> settlingLog = new List<string>();
        private static void LiquidSettling(int frame, OniNativeColony.Status status)
        {
            const int width = 16, height = 7;
            if (Once(0, "arena"))
            {
                settlingHeight = 0; settlingLogged = -1; settlingLog.Clear();
                pendingShift = width - Width;
                Arena((x, y) => y < 3 ? (SimHashes.Water, 1000f, 295f) :
                    y == 3 ? (SimHashes.Water, x % 2 == 0 ? 300f : 700f, 295f) :
                    (SimHashes.Oxygen, 1.2f, 295f), width, height);
            }
            double Column(int x) => Enumerable.Range(0, height).Sum(y => SpeciesMass(Cell(x, y), SimHashes.Water));
            if (Once(6, "start")) recorded["settling water"] = Enumerable.Range(0, width).Sum(Column);
            if (frame >= 500 && frame <= 1006 && frame != settlingLogged)
            {
                settlingLogged = frame;
                var columns = Enumerable.Range(0, width).Select(Column).ToArray();
                var range = columns.Max() - columns.Min();
                settlingHeight = Math.Max(settlingHeight, range);
                if (frame % 50 == 0)
                    settlingLog.Add(FormattableString.Invariant($"f{frame} column range {range:G6} kg"));
            }
            if (Once(1006, "check"))
            {
                Debug.Log("[Mixed Atmospheres] liquid-settling: " + string.Join(" | ", settlingLog));
                Record("unforced surface-height variation decays across the late window", settlingHeight < 80,
                    FormattableString.Invariant($"initial column range 400 kg; maximum from frames 500..1006 {settlingHeight:G6} kg"));
                var water = Enumerable.Range(0, width).Sum(Column);
                Record("the basin retained its water", water > .99 * recorded["settling water"],
                    FormattableString.Invariant($"{recorded["settling water"]:G9} -> {water:G9} kg"));
                Record("settling keeps closed ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                Finish();
            }
        }
    }
}
