using System;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Oni.Interop;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Face forces (docs/face-forces.md) in a real colony, through the public
        // class other mods bind. Two sealed rooms of oxygen, columns 0-2 and 4-7,
        // joined by one open tile (column 3, row 1) in the wall between them.
        //  1. A handle on the opening's right face with 120 Pa and 6 m/s must
        //     raise the right room by the stall pressure and then stop moving
        //     gas; closing it lets the rooms equalise.
        //  2. With the Mixed Atmospheres: Fans mod installed (the harness's
        //     --extra-mod), its buildings are built and powered as a player's
        //     are, and found and driven through reflection:
        //     a Swivel Fan standing in the left room with its head beside the
        //     opening must hold its step's stall pressure (Medium: 30 Pa), and
        //     after it is removed a Wall Fan built in the opening its own
        //     (Medium: 60 Pa). A building that cannot be built is a failed
        //     check of that mod, reported with its exception.
        //     Without that mod the second part is reported as not run.
        private const string WallFanId = "MixedAtmospheresWallFan", SwivelFanId = "MixedAtmospheresSwivelFan";
        private static int ffHandle, ffLogged;
        private static double ffWork, ffLastWork, ffPeakWork, ffStartMass;
        private static GameObject? ffFan;
        private static readonly List<string> ffLog = new List<string>();
        private static void FaceForceScenario(int frame, OniNativeColony.Status status)
        {
            const int wall = 3, row = 1;
            const int open = 6, stalled = 306, relaxed = 456, swivelStalled = 856, swivelGone = 1006, wallStalled = 1406;
            const double stall = 120, speed = 6;
            double Pressure(int cell) => MixturePresentationRuntime.TryGetCell(cell, out var r) && r != null ? (r.MechanicalPressurePa ?? r.TotalPressurePa) : double.NaN;
            double Rise() => Enumerable.Range(0, Height).Average(y => Pressure(Cell(6, y))) - Enumerable.Range(0, Height).Average(y => Pressure(Cell(1, y)));
            if (Once(0, "arena"))
            {
                ffHandle = 0; ffLogged = -1; ffWork = ffLastWork = ffPeakWork = ffStartMass = 0; ffFan = null; ffLog.Clear();
                Arena((x, y) => x == wall && y != row ? (SimHashes.Unobtanium, 20000f, 300f) : (SimHashes.Oxygen, 1.8f, 300f));
            }
            if (Once(open, "open"))
            {
                ffStartMass = ArenaMass();
                Record("face forces are active in a Mixed Atmospheres colony", FaceForces.Active && FaceForces.ApiVersion >= 1,
                    $"active {FaceForces.Active}, interface version {FaceForces.ApiVersion}");
                ffHandle = FaceForces.Open();
                var second = FaceForces.Open();
                Record("a handle is opened", ffHandle != 0 && second != 0 && second != ffHandle, $"handles {ffHandle} and {second}");
                var refused = 0;
                foreach (var (dx, dy, pa, mps) in new[] { (1, 1, stall, speed), (0, 0, stall, speed), (2, 0, stall, speed), (1, 0, -1.0, speed),
                    (1, 0, 10001.0, speed), (1, 0, stall, 101.0), (1, 0, double.NaN, speed), (1, 0, stall, double.PositiveInfinity) })
                    try { FaceForces.Set(ffHandle, Cell(wall, row), dx, dy, pa, mps); }
                    catch (ArgumentException) { ++refused; }
                Record("arguments outside the contract are refused", refused == 8, $"{refused} of 8 threw ArgumentException");
                // Faces that cannot be driven are accepted and do nothing.
                var accepted = FaceForces.Set(second, Cell(wall - 1, row + 1), 1, 0, stall, speed) // towards the wall
                    && FaceForces.Set(second, -1, 1, 0, stall, speed) && FaceForces.Set(second, int.MaxValue, 0, 1, stall, speed)
                    && FaceForces.Set(second, Cell(wall, row), 1, 0, 0, speed);
                Record("faces that cannot be driven are accepted", accepted, $"accepted {accepted}");
                FaceForces.Close(second);
                Record("a closed handle is forgotten", !FaceForces.Set(second, Cell(wall, row), 1, 0, stall, speed) && FaceForces.TakeWork(second) == 0, "Set returned false");
                Record("the fan's force is set", FaceForces.Set(ffHandle, Cell(wall, row), 1, 0, stall, speed), "120 Pa, 6 m/s, to the right");
            }
            if (frame > open && frame <= stalled && frame != ffLogged && ffHandle != 0)
            {
                ffLogged = frame;
                // As a fan does on every tick: the unchanged values again, then the work.
                FaceForces.Set(ffHandle, Cell(wall, row), 1, 0, stall, speed);
                ffLastWork = FaceForces.TakeWork(ffHandle); ffWork += ffLastWork; ffPeakWork = Math.Max(ffPeakWork, ffLastWork);
                if ((frame - open) % 50 == 0) ffLog.Add(FormattableString.Invariant($"f{frame} rise {Rise():F1} Pa, work {ffWork:G4} J"));
            }
            if (Once(stalled, "stalled"))
            {
                var rise = Rise();
                Debug.Log("[Mixed Atmospheres] face-forces: " + string.Join(" | ", ffLog));
                Record("the pressure difference settles at the stall pressure", Math.Abs(rise - stall) <= 0.05 * stall,
                    FormattableString.Invariant($"right room minus left room {rise:F2} Pa for a stall pressure of {stall} Pa (limit 5 %)"));
                Record("building it took work, and holding it takes none", ffWork > 0 && Math.Abs(ffLastWork) < 0.02 * stall * speed * 0.2,
                    FormattableString.Invariant($"work {ffWork:G5} J in all, {ffLastWork:G3} J in the last tick"));
                Record("no tick's work exceeds a quarter of stall pressure times free speed", ffPeakWork <= 0.25 * stall * speed * 0.2 * 1.0001,
                    FormattableString.Invariant($"largest {ffPeakWork:G5} J of {0.25 * stall * speed * 0.2:G5} J a tick"));
                FaceForces.Close(ffHandle);
                Record("the handle closes", !FaceForces.Set(ffHandle, Cell(wall, row), 1, 0, stall, speed), "Set returned false after Close");
                ffHandle = 0;
            }
            if (Once(relaxed, "relaxed"))
            {
                var rise = Rise();
                Record("without the force the rooms equalise", Math.Abs(rise) <= 0.05 * stall, FormattableString.Invariant($"right room minus left room {rise:F2} Pa"));
                Record("the gas was retained and the ledgers closed", Math.Abs(ArenaMass() - ffStartMass) <= 1e-6 * ffStartMass &&
                    status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"arena {ffStartMass:G9} -> {ArenaMass():G9} kg; mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
                if (Assets.GetBuildingDef(SwivelFanId) == null && Assets.GetBuildingDef(WallFanId) == null)
                {
                    recorded["fan mod installed"] = 0;
                    Record("the fan mod's buildings (not installed in this run)", true, "not run: Mixed Atmospheres: Fans is not installed");
                    Finish(); return;
                }
                recorded["fan mod installed"] = 1;
                // The Swivel Fan stands on the left room's floor beside the wall;
                // its head, one tile up, faces the opening.
                ffFan = TryBuildFan(SwivelFanId, Cell(wall - 1, row - 1));
            }
            void FanChecks(string name, double expected)
            {
                var rise = Rise();
                var operational = ffFan != null ? ffFan.GetComponent<Operational>() : null;
                var temperature = ffFan != null ? ffFan.GetComponent<PrimaryElement>().Temperature : 0f;
                Record("the " + name + " runs", operational != null && operational.IsOperational && operational.IsActive, MachineState(ffFan));
                Record("the " + name + " holds its stall pressure", Math.Abs(rise - expected) <= 0.1 * expected,
                    FormattableString.Invariant($"right room minus left room {rise:F2} Pa for the fan's {expected:F0} Pa (limit 10 %)"));
                Record("the " + name + "'s unused power warms it", temperature > 300.005f, FormattableString.Invariant($"fan at {temperature:F3} K, built at 300 K"));
                Record("the colony stays closed with the " + name + " running", status.State == 1 && status.MaximumRelativeMassError <= 1e-12 && status.MaximumRelativeEnergyError <= 1e-10,
                    FormattableString.Invariant($"mass {status.MaximumRelativeMassError:G3}, energy {status.MaximumRelativeEnergyError:G3}"));
            }
            if (Once(swivelStalled, "swivel fan"))
            {
                if (ffFan != null) FanChecks("Swivel Fan", 30); // Medium, the step a new fan starts on
                if (ffFan != null) { Util.KDestroyGameObject(ffFan); ffFan = null; }
            }
            if (Once(swivelGone, "wall fan"))
            {
                var rise = Rise();
                Record("removing a fan releases its face", Math.Abs(rise) <= 3, FormattableString.Invariant($"right room minus left room {rise:F2} Pa after the Swivel Fan was removed"));
                ffFan = TryBuildFan(WallFanId, Cell(wall, row));
                if (ffFan == null) { Finish(); return; }
            }
            if (Once(wallStalled, "wall fan stalled"))
            {
                FanChecks("Wall Fan", 60);
                Finish();
            }
        }
        // Builds one of the fan mod's buildings; a failure is that mod's, and
        // is recorded with what it threw.
        private static GameObject? TryBuildFan(string id, int cell)
        {
            if (Assets.GetBuildingDef(id) == null) { Record("the fan mod has " + id, false, "no such building"); return null; }
            try
            {
                var fan = Build(id, cell);
                Record(id + " can be built", fan != null, "built");
                return fan;
            }
            catch (Exception error)
            {
                var first = (error.StackTrace ?? "").Split('\n').FirstOrDefault()?.Trim() ?? "";
                Record(id + " can be built", false, error.GetType().Name + ": " + error.Message + " " + first);
                return null;
            }
        }
    }
}
