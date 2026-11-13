using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Two rooms of different gases at one pressure, joined by an opening one
        // tile high in the wall between them, must exchange gas: the heavy gas
        // runs out along the floor of the opening while the light gas returns.
        // Columns 0-4 hold carbon dioxide, column 5 is a solid wall whose bottom
        // tile (row 0) is open and holds oxygen, columns 6-10 hold oxygen.
        private static double soCo2Start, soO2Start, soCo2Right, soO2Left, soPeakLiquid, soLeftStart, soRightStart, soLeftEnd, soRightEnd;
        private static int soLogged;
        private static readonly List<string> soLog = new List<string>();
        private static void SideOpening(int frame, OniNativeColony.Status status)
        {
            const int width = 11, height = 5, wall = 5;
            const int start = 6, end = 606;
            // Same pressure at one temperature: 1.8 kg x 0.044 / 0.032 of carbon dioxide.
            const float co2Mass = 2.475f;
            if (Once(0, "arena"))
            {
                soCo2Start = soO2Start = soCo2Right = soO2Left = soPeakLiquid = soLeftStart = soRightStart = soLeftEnd = soRightEnd = 0; soLogged = -1; soLog.Clear();
                pendingShift = width - Width;
                Arena((x, y) => x < wall ? (SimHashes.CarbonDioxide, co2Mass, 300f) :
                    x == wall ? (y == 0 ? (SimHashes.Oxygen, 1.8f, 300f) : (SimHashes.Unobtanium, 20000f, 300f)) :
                    (SimHashes.Oxygen, 1.8f, 300f), width, height);
            }
            double Sum(SimHashes element, int colFrom, int colTo) =>
                Enumerable.Range(colFrom, colTo - colFrom + 1).Sum(x => Enumerable.Range(0, height).Sum(y => SpeciesMass(Cell(x, y), element)));
            double Room(int colFrom, int colTo) =>
                Enumerable.Range(colFrom, colTo - colFrom + 1).Sum(x => Enumerable.Range(0, height).Sum(y =>
                    SpeciesMass(Cell(x, y), SimHashes.CarbonDioxide) + SpeciesMass(Cell(x, y), SimHashes.Oxygen)));
            double Liquid() => Sum(SimHashes.LiquidCarbonDioxide, 0, width - 1) + Sum(SimHashes.Water, 0, width - 1);
            if (Once(start, "start"))
            {
                soCo2Start = Sum(SimHashes.CarbonDioxide, 0, width - 1);
                soO2Start = Sum(SimHashes.Oxygen, 0, width - 1);
                soLeftStart = Room(0, wall - 1); soRightStart = Room(wall + 1, width - 1);
            }
            if (frame >= start && frame <= end && frame != soLogged)
            {
                soLogged = frame;
                soCo2Right = Sum(SimHashes.CarbonDioxide, wall + 1, width - 1);
                soO2Left = Sum(SimHashes.Oxygen, 0, wall - 1);
                soPeakLiquid = Math.Max(soPeakLiquid, Liquid());
                if ((frame - start) % 50 == 0)
                    soLog.Add(FormattableString.Invariant($"f{frame} carbon dioxide right {soCo2Right:G4} kg, oxygen left {soO2Left:G4} kg"));
            }
            if (Once(end, "check"))
            {
                Debug.Log("[Mixed Atmospheres] side-opening: " + string.Join(" | ", soLog));
                soLeftEnd = Room(0, wall - 1); soRightEnd = Room(wall + 1, width - 1);
                Record("carbon dioxide passes a one-tile opening into the oxygen room", soCo2Right >= 0.03 * soCo2Start,
                    FormattableString.Invariant($"carbon dioxide in the right room {soCo2Right:G4} of {soCo2Start:G5} kg ({soCo2Right / soCo2Start:P1}, need 3 %)"));
                Record("oxygen returns through the same opening", soO2Left >= 0.03 * soO2Start,
                    FormattableString.Invariant($"oxygen in the left room {soO2Left:G4} of {soO2Start:G5} kg ({soO2Left / soO2Start:P1}, need 3 %)"));
                Record("the rooms stay at one pressure",
                    Math.Abs(soLeftEnd - soLeftStart) <= 0.15 * soLeftStart && Math.Abs(soRightEnd - soRightStart) <= 0.15 * soRightStart,
                    FormattableString.Invariant($"gas mass left room {soLeftStart:G5} -> {soLeftEnd:G5} kg, right room {soRightStart:G5} -> {soRightEnd:G5} kg (limit 15 %)"));
                Record("the exchange made no liquid", soPeakLiquid < 1e-3, FormattableString.Invariant($"most liquid carbon dioxide and water in the arena {soPeakLiquid:G4} kg"));
                var co2 = Sum(SimHashes.CarbonDioxide, 0, width - 1) + Sum(SimHashes.LiquidCarbonDioxide, 0, width - 1);
                var o2 = Sum(SimHashes.Oxygen, 0, width - 1);
                Record("the gases were retained",
                    Math.Abs(co2 - soCo2Start) < 1e-6 * soCo2Start && Math.Abs(o2 - soO2Start) < 1e-6 * soO2Start,
                    FormattableString.Invariant($"carbon dioxide {soCo2Start:G9} -> {co2:G9} kg, oxygen {soO2Start:G9} -> {o2:G9} kg"));
                Record("the exchange keeps closed ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                Finish();
            }
        }
    }
}
