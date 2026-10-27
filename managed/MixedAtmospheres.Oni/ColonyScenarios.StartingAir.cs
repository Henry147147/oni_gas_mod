using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Starting air (docs/nitrogen.md): the bridge settled for the printed
        // duplicants; with MA_COLONY_STARTING_RESERVE=1 a shortfall lies beside
        // the pod as oxylite; a nitrogen-air colony holds 25% oxygen air at the
        // pod. Then a gas share sensor in the arena reads the mixture's oxygen
        // share: on above 50% while the air is 64% oxygen by moles, off once
        // more nitrogen brings it to 33%; breathing follows the colony's rules
        // at both shares (hard mode refuses 64%).
        private static GasShareSensor? shareAbove, shareBelow;
        private static void StartingAir(int frame)
        {
            if (Once(0, "arena"))
            {
                // The pod's air as imported, before breathing and any reserve change it.
                var podAir = Components.Telepads.Count > 0 ? Grid.CellAbove(Grid.PosToCell(Components.Telepads[0])) : -1;
                recorded["pod oxygen share"] = podAir >= 0 ? OxygenShare(podAir) : -1;
                recorded["pod nitrogen"] = podAir >= 0 ? SpeciesMass(podAir, OniNitrogenElements.Gas) : 0;
                Arena((x, y) => (SimHashes.Oxygen, 1.2f, 300f));
            }
            if (Once(4, "nitrogen"))
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.AddRemoveSubstance(Cell(x, y), OniNitrogenElements.Gas, CellEventLogger.Instance.OxygenModifierSimUpdate, 1.2f, 300, byte.MaxValue, 0);
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
                double o = SpeciesMass(above, SimHashes.Oxygen) + SpeciesMass(above, SimHashes.ContaminatedOxygen), n = SpeciesMass(above, OniNitrogenElements.Gas);
                var share = OxygenShare(above);
                var imported = recorded["pod oxygen share"];
                if (ColonyStartingAir.NitrogenAirApplied)
                    Record("the pod starts in nitrogen air", recorded["pod nitrogen"] > 0 && Math.Abs(imported - NitrogenAir.OxygenShare) < .005,
                        $"oxygen share {imported:P2} at import; now oxygen {o:G4} kg, nitrogen {n:G4} kg, oxygen share {share:P2}");
                else Record("the pod breathes its generated air", n == 0 && o > 0 && imported == 1, $"oxygen share {imported:P2} at import; now oxygen {o:G4} kg, nitrogen {n:G4} kg");
            }
            if (Once(24, "above"))
            {
                Record("the sensors read the oxygen share", shareAbove != null && shareAbove.IsSwitchedOn && shareBelow != null && !shareBelow.IsSwitchedOn,
                    $"share {shareAbove?.CurrentValue:G4}% (cell {OxygenShare(Cell(2, 2)):P2}); above-sensor {shareAbove?.IsSwitchedOn}, below-sensor {shareBelow?.IsSwitchedOn}");
                BreathesByRules(Cell(4, 3), "64% oxygen");
            }
            if (Once(25, "more nitrogen"))
                for (var y = 0; y < Height; ++y) for (var x = 0; x < Width; ++x)
                    SimMessages.AddRemoveSubstance(Cell(x, y), OniNitrogenElements.Gas, CellEventLogger.Instance.OxygenModifierSimUpdate, 3f, 300, byte.MaxValue, 0);
            if (Once(36, "below"))
            {
                Record("the sensors switch when nitrogen dilutes the oxygen", shareAbove != null && !shareAbove.IsSwitchedOn && shareBelow != null && shareBelow.IsSwitchedOn,
                    $"share {shareAbove?.CurrentValue:G4}% (cell {OxygenShare(Cell(2, 2)):P2}); above-sensor {shareAbove?.IsSwitchedOn}, below-sensor {shareBelow?.IsSwitchedOn}");
                BreathesByRules(Cell(4, 3), "33% oxygen");
                Finish();
            }
        }
        // Duplicants breathe by the colony's rules: hard mode refuses oxygen
        // above its maximum share; easy mode has no maximum.
        private static void BreathesByRules(int cell, string air)
        {
            var share = OxygenShare(cell);
            var maximum = ModSettingsRuntime.Breathing.MaximumOxygenFraction;
            var breathable = ColonyOwner.TryReadBreathingCell(cell, out var mass, out var offered) && offered != SimHashes.Vacuum && mass > 0;
            Record("breathing follows the colony's rules in " + air, breathable == (share <= maximum),
                $"oxygen share {share:P2}, maximum {maximum:P0}, breathable {breathable} ({offered} {mass:G4} kg)");
        }
        private static double OxygenShare(int cell) =>
            ColonySensors.GasShare(cell, ElementLoader.FindElementByHash(SimHashes.Oxygen)) + ColonySensors.GasShare(cell, ElementLoader.FindElementByHash(SimHashes.ContaminatedOxygen));
    }
}
