using System;
using System.Linq;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // The default CO2 opt-out, live switch, oxygen requirement and share sensor.
        private static GasShareSensor? shareAbove, shareBelow;
        private static void StartingAir(int frame)
        {
            if (Once(0, "arena"))
            {
                // The pod's air as imported, before breathing and any reserve change it.
                var podAir = Components.Telepads.Count > 0 ? Grid.CellAbove(Grid.PosToCell(Components.Telepads[0])) : -1;
                recorded["pod oxygen share"] = podAir >= 0 ? OxygenShare(podAir) : -1;
                Arena((x, y) => (SimHashes.Oxygen, 1.2f, 300f));
            }
            if (Once(4, "carbon dioxide"))
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.AddRemoveSubstance(Cell(x, y), SimHashes.CarbonDioxide, CellEventLogger.Instance.OxygenModifierSimUpdate, 1.2f, 300, byte.MaxValue, 0);
            if (Once(8, "sensors"))
            {
                shareAbove = Build(GasShareSensorConfig.ID, Cell(2, 2)).GetComponent<GasShareSensor>();
                shareBelow = Build(GasShareSensorConfig.ID, Cell(5, 2)).GetComponent<GasShareSensor>();
            }
            if (Once(9, "select"))
            {
                foreach (var s in new[] { shareAbove!, shareBelow! })
                {
                    s.GetComponent<Filterable>().SelectedTag = ElementLoader.FindElementByHash(SimHashes.Oxygen).tag;
                    s.Threshold = 50;
                }
                shareBelow!.ActivateAboveThreshold = false;
            }
            if (Once(16, "starting air"))
            {
                Record("the starting air bridge settled", ColonyStartingAir.Settled && ColonyStartingAir.Duplicants > 0,
                    $"duplicants {ColonyStartingAir.Duplicants}, shortfall {ColonyStartingAir.ShortfallKg:G6} kg, reserve {ColonyStartingAir.ReserveKg:G6} kg");
                var pod = ColonyStartingAir.PodCell;
                if (Environment.GetEnvironmentVariable("MA_COLONY_STARTING_RESERVE") == "1")
                {
                    Grid.CellToXY(Math.Max(0, pod), out var px, out var py);
                    var oxylite = Components.Pickupables.Items.Where(p => p != null && p.GetComponent<PrimaryElement>()?.ElementID == SimHashes.OxyRock)
                        .Where(p => { Grid.CellToXY(Grid.PosToCell(p), out var ix, out var iy); return Math.Abs(ix - px) <= 13 && Math.Abs(iy - py) <= 3; })
                        .Sum(p => (double)p.GetComponent<PrimaryElement>().Mass);
                    // Oxylite sublimates while its cell is thin, so some has already left.
                    Record("the shortfall lies beside the pod as oxylite", ColonyStartingAir.ShortfallKg == 0 ? ColonyStartingAir.ReserveKg == 0 :
                        Math.Abs(ColonyStartingAir.ReserveKg - ColonyStartingAir.ShortfallKg) < 1e-3 && oxylite > .9 * ColonyStartingAir.ReserveKg && oxylite <= ColonyStartingAir.ReserveKg + 1e-3,
                        $"shortfall {ColonyStartingAir.ShortfallKg:G6} kg, placed {ColonyStartingAir.ReserveKg:G6} kg, beside the pod now {oxylite:G6} kg");
                }
                var above = pod >= 0 ? Grid.CellAbove(pod) : -1;
                double o = SpeciesMass(above, SimHashes.Oxygen) + SpeciesMass(above, SimHashes.ContaminatedOxygen);
                Record("the pod keeps its generated oxygen", o > 0 && recorded["pod oxygen share"] == 1,
                    $"oxygen share at import {recorded["pod oxygen share"]:P2}, oxygen now {o:G4} kg");
                Record("no nitrogen game content is registered", ElementLoader.FindElementByHash((SimHashes)(-1109340048)) == null &&
                    ElementLoader.FindElementByHash((SimHashes)1078696428) == null && ElementLoader.FindElementByHash((SimHashes)418600943) == null, "all three removed element hashes absent");
            }
            if (Once(24, "above"))
            {
                Record("the sensors read the oxygen share", shareAbove != null && shareAbove.IsSwitchedOn && shareBelow != null && !shareBelow.IsSwitchedOn,
                    $"share {shareAbove?.CurrentValue:G4}% (cell {OxygenShare(Cell(2, 2)):P2}); above-sensor {shareAbove?.IsSwitchedOn}, below-sensor {shareBelow?.IsSwitchedOn}");
                Record("CO2 limits are off by default", !ModSettingsRuntime.Breathing.EnforceCarbonDioxideLimits, "new colony setting");
                CheckBreathing(true, "CO2-rich air is breathable with limits off");
                ModSettingsRuntime.SetCarbonDioxideLimits(true);
                CheckBreathing(false, "enabling CO2 limits immediately blocks the same air");
            }
            if (Once(25, "live display"))
            {
                CheckCo2Display(true);
                ModSettingsRuntime.SetCarbonDioxideLimits(false);
            }
            if (Once(28, "display reset")) CheckCo2Display(false);
            if (Once(29, "more carbon dioxide"))
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.AddRemoveSubstance(Cell(x, y), SimHashes.CarbonDioxide, CellEventLogger.Instance.OxygenModifierSimUpdate, 3f, 300, byte.MaxValue, 0);
            if (Once(36, "below"))
            {
                Record("the sensors switch when CO2 dilutes the oxygen", shareAbove != null && !shareAbove.IsSwitchedOn && shareBelow != null && shareBelow.IsSwitchedOn,
                    $"share {shareAbove?.CurrentValue:G4}% (cell {OxygenShare(Cell(2, 2)):P2}); above-sensor {shareAbove?.IsSwitchedOn}, below-sensor {shareBelow?.IsSwitchedOn}");
                CheckBreathing(true, "oxygen remains breathable after further CO2 dilution with limits off");
                Arena((x, y) => (SimHashes.CarbonDioxide, 1.2f, 300f));
            }
            if (Once(44, "oxygen still required"))
            {
                CheckBreathing(false, "pure CO2 remains unbreathable with limits off");
                Finish();
            }
        }
        private static void CheckBreathing(bool expected, string name)
        {
            var breathable = ColonyOwner.TryReadBreathingCell(Cell(4, 3), out var mass, out var offered) && offered != SimHashes.Vacuum && mass > 0;
            Record(name, breathable == expected, $"breathable {breathable}, oxygen share {OxygenShare(Cell(4, 3)):P2}, offered {offered} {mass:G4} kg");
        }
        private static void CheckCo2Display(bool expected)
        {
            var found = MixturePresentationRuntime.TryGetCell(Cell(4, 3), out var view);
            var failed = found && (view!.Failures & (BreathingFailure.CarbonDioxideFractionHigh | BreathingFailure.CarbonDioxidePartialPressureHigh)) != 0;
            Record("display follows the CO2 switch (" + expected + ")", found && failed == expected, $"found {found}, CO2 failure {failed}");
        }
        private static double OxygenShare(int cell) =>
            ColonySensors.GasShare(cell, ElementLoader.FindElementByHash(SimHashes.Oxygen)) + ColonySensors.GasShare(cell, ElementLoader.FindElementByHash(SimHashes.ContaminatedOxygen));
    }
}
