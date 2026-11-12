using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Water lying above a sealed room of air, joined to it by a hole one
        // tile wide, must drain into the room while the room's air rises past
        // it (before, the water stood on the trapped air for good). From the
        // bottom: a sealed room two tiles high, a solid ceiling with one open
        // tile (the hole), three tiles of water, two tiles of oxygen.
        private static double sdWaterStart, sdAirStart, sdPeakSteam, sdWaterBelow, sdAirBelow;
        private static int sdHalfFrame, sdLogged;
        private static readonly List<string> sdLog = new List<string>();
        private static void SealedDrain(int frame, OniNativeColony.Status status)
        {
            const int width = 6, height = 8, roomRows = 2, ceiling = 2, hole = 3, waterTop = 5;
            const int start = 6, end = 606;
            const double capacity = (roomRows * width + 1) * 1000.0;
            if (Once(0, "arena"))
            {
                sdWaterStart = sdAirStart = sdPeakSteam = sdWaterBelow = sdAirBelow = 0; sdHalfFrame = -1; sdLogged = -1; sdLog.Clear();
                pendingShift = width - Width;
                Arena((x, y) => y < ceiling ? (SimHashes.Oxygen, 1.8f, 300f) :
                    y == ceiling ? (x == hole ? (SimHashes.Oxygen, 1.8f, 300f) : (SimHashes.Unobtanium, 20000f, 300f)) :
                    y <= waterTop ? (SimHashes.Water, 1000f, 300f) :
                    (SimHashes.Oxygen, 1.8f, 300f), width, height);
            }
            double Sum(SimHashes element, int rowFrom, int rowTo) =>
                Enumerable.Range(rowFrom, rowTo - rowFrom + 1).Sum(y => Enumerable.Range(0, width).Sum(x => SpeciesMass(Cell(x, y), element)));
            double Steam() => Sum(SimHashes.Steam, 0, height - 1);
            if (Once(start, "start"))
            {
                sdWaterStart = Sum(SimHashes.Water, 0, height - 1) + Steam();
                sdAirStart = Sum(SimHashes.Oxygen, 0, ceiling);
            }
            if (frame >= start && frame <= end && frame != sdLogged)
            {
                sdLogged = frame;
                var below = Sum(SimHashes.Water, 0, ceiling); var above = Sum(SimHashes.Water, ceiling + 1, height - 1);
                var air = Sum(SimHashes.Oxygen, 0, ceiling);
                sdWaterBelow = below; sdAirBelow = air;
                sdPeakSteam = Math.Max(sdPeakSteam, Steam());
                if (sdHalfFrame < 0 && below > capacity / 2) sdHalfFrame = frame - start;
                if ((frame - start) % 50 == 0)
                    sdLog.Add(FormattableString.Invariant($"f{frame} water above {above:G5} kg, below {below:G5} kg, oxygen below {air:G4} kg"));
            }
            if (Once(end, "check"))
            {
                Debug.Log("[Mixed Atmospheres] sealed-drain: " + string.Join(" | ", sdLog));
                Record("the sealed room fills through a one-tile hole", sdWaterBelow >= 0.85 * capacity,
                    FormattableString.Invariant($"water in the room and hole {sdWaterBelow:G5} of {capacity:G5} kg ({sdWaterBelow / capacity:P1})"));
                Record("the room's air rises past the water", sdAirBelow < 0.1 * sdAirStart,
                    FormattableString.Invariant($"oxygen in the room and hole {sdAirStart:G4} -> {sdAirBelow:G4} kg"));
                Record("half the room fills within twenty seconds", sdHalfFrame >= 0 && sdHalfFrame <= 100,
                    FormattableString.Invariant($"half the room's capacity first exceeded {(sdHalfFrame < 0 ? "never" : sdHalfFrame + " frames after the start")} (limit 100)"));
                Record("draining made no steam", sdPeakSteam < 1e-3, FormattableString.Invariant($"most steam in the arena {sdPeakSteam:G4} kg"));
                var water = Sum(SimHashes.Water, 0, height - 1) + Steam();
                Record("the water was retained", Math.Abs(water - sdWaterStart) < 1e-6 * sdWaterStart,
                    FormattableString.Invariant($"{sdWaterStart:G9} -> {water:G9} kg"));
                Record("draining keeps closed ledgers", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                Finish();
            }
        }
    }
}
