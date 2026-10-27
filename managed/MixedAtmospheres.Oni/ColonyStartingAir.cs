using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Ten-cycle oxygen reserve for the gas connected to the printing pod.
    // See docs/starting-air.md. CO2 headroom is advisory only when its limits
    // are enabled. Harness arenas place the reserve only on explicit request.
    internal static unsafe class ColonyStartingAir
    {
        internal const double BridgeCycles = 10, CycleSeconds = 600, ChunkKg = 300;
        private const int MaximumWaitFrames = 600;

        private sealed class Inventory
        {
            internal int PodCell = -1, World = -1, Cells;
            internal double OxygenMol, Co2Mol, GasMol, GasVolume, MeanTemperature;
        }
        private static Inventory? pending;
        private static int waited;
        internal static object? Report { get; private set; }
        // For the harness scenario: the settled bridge of this session.
        internal static bool Settled { get; private set; }
        internal static int PodCell { get; private set; } = -1;
        internal static double ShortfallKg { get; private set; }
        internal static double ReserveKg { get; private set; }
        internal static int Duplicants { get; private set; }

        internal static void Reset()
        {
            pending = null; waited = 0; Report = null; Settled = false; PodCell = -1; ShortfallKg = ReserveKg = 0; Duplicants = 0;
        }

        private static bool PlaceInValidation => Environment.GetEnvironmentVariable("MA_COLONY_STARTING_RESERVE") == "1";

        // Right before the owner imports a new colony. Uses the native frame's
        // own element, mass and temperature arrays (the import's input).
        internal static void Capture(Sim.GameDataUpdate* source, Func<int, bool> space)
        {
            Reset();
            try
            {
                var telepad = Components.Telepads.Count > 0 ? Components.Telepads[0] : null;
                if (telepad == null) { Report = new { Skipped = "no printing pod" }; return; }
                var pod = Grid.PosToCell(telepad);
                if (!Grid.IsValidCell(pod)) { Report = new { Skipped = "printing pod outside the grid" }; return; }
                var inventory = new Inventory { PodCell = pod, World = Grid.WorldIdx[pod] };
                var oxygen = ElementLoader.FindElementByHash(SimHashes.Oxygen);
                var polluted = ElementLoader.FindElementByHash(SimHashes.ContaminatedOxygen);
                var co2 = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide);
                double heat = 0, moles = 0;
                // The connected open cells of the pod's world, breadth first.
                var seen = new HashSet<int> { pod };
                var queue = new Queue<int>(); queue.Enqueue(pod);
                while (queue.Count > 0 && seen.Count <= 200000)
                {
                    var cell = queue.Dequeue();
                    var element = ElementLoader.elements[source->elementIdx[cell]];
                    if (element.IsSolid || space(cell)) continue;
                    ++inventory.Cells;
                    var mass = (double)source->mass[cell];
                    if (element.IsGas && mass > 0)
                    {
                        var molar = element.molarMass / 1000.0;
                        inventory.GasMol += mass / molar;
                        if (element == oxygen || element == polluted) inventory.OxygenMol += mass / molar;
                        if (element == co2) inventory.Co2Mol += mass / molar;
                        moles += mass / molar; heat += mass / molar * source->temperature[cell];
                        inventory.GasVolume += 1;
                    }
                    else if (element.IsLiquid) inventory.GasVolume += Math.Max(0, 1 - mass / Math.Max(1e-9, element.maxMass));
                    else inventory.GasVolume += 1; // vacuum fills with the region's gas at import
                    foreach (var next in new[] { Grid.CellLeft(cell), Grid.CellRight(cell), Grid.CellAbove(cell), Grid.CellBelow(cell) })
                        if (Grid.IsValidCell(next) && Grid.WorldIdx[next] == inventory.World && seen.Add(next)) queue.Enqueue(next);
                }
                inventory.MeanTemperature = moles > 0 ? heat / moles : 293.15;
                pending = inventory;
            }
            catch (Exception error)
            {
                Report = new { Skipped = "capture failed: " + error.Message };
                Debug.LogWarning("[Mixed Atmospheres] Starting air was not measured: " + error.Message);
            }
        }

        // After each publication until the starting duplicants exist.
        internal static void Update()
        {
            if (pending == null) return;
            var inventory = pending;
            try
            {
                var minions = Components.LiveMinionIdentities.Items.Where(m => m != null && m.GetMyWorldId() == inventory.World).ToList();
                if (minions.Count == 0 && ++waited < MaximumWaitFrames) return;
                pending = null;
                Settle(inventory, minions);
            }
            catch (Exception error)
            {
                pending = null;
                Report = new { Skipped = "bridge failed: " + error.Message };
                Debug.LogWarning("[Mixed Atmospheres] The starting air bridge failed: " + error.Message);
            }
        }

        private static void Settle(Inventory inventory, List<MinionIdentity> minions)
        {
            var attribute = Db.Get().Attributes.AirConsumptionRate;
            var rates = minions.Select(m => (double)(attribute.Lookup(m)?.GetTotalValue() ?? TUNING.DUPLICANTSTATS.STANDARD.BaseStats.OXYGEN_USED_PER_SECOND)).ToArray();
            var rateKg = rates.Sum();
            var oxygenMolar = ElementLoader.FindElementByHash(SimHashes.Oxygen).molarMass / 1000.0;
            var co2Molar = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).molarMass / 1000.0;
            var k = oxygenMolar * TUNING.DUPLICANTSTATS.STANDARD.BaseStats.OXYGEN_TO_CO2_CONVERSION / co2Molar;
            var rules = ModSettingsRuntime.Breathing;
            var molesPerCycle = rateKg / oxygenMolar * CycleSeconds;
            var o0 = inventory.OxygenMol; var c0 = inventory.Co2Mol; var g0 = inventory.GasMol;
            var (oxygenMol, co2Mol) = StartingAirBridge.BreathableMoles(o0, c0, g0, inventory.GasVolume, inventory.MeanTemperature, k,
                rules.MinimumOxygenFraction, rules.MinimumOxygenPartialPressurePa, rules.MaximumOxygenFraction,
                rules.MaximumCo2Fraction, rules.MaximumCo2PartialPressurePa, rules.EnforceCarbonDioxideLimits);
            var oxygenCycles = molesPerCycle > 0 ? oxygenMol / molesPerCycle : double.PositiveInfinity;
            var co2Cycles = molesPerCycle > 0 ? co2Mol / molesPerCycle : double.PositiveInfinity;
            var shortfallKg = Math.Max(0, BridgeCycles * CycleSeconds * rateKg - oxygenMol * oxygenMolar);
            var place = shortfallKg > 0 && (ColonyRuntimeValidation.Settings == null || PlaceInValidation);
            var placed = place ? PlaceReserve(inventory, shortfallKg) : new List<(int cell, double kg)>();
            var placedKg = placed.Sum(p => p.kg);
            Settled = true; PodCell = inventory.PodCell; ShortfallKg = shortfallKg; ReserveKg = placedKg; Duplicants = minions.Count;
            Report = new
            {
                Duplicants = minions.Count, OxygenKgPerSecond = rateKg, inventory.Cells, inventory.GasVolume, inventory.MeanTemperature,
                Colony = new { GasMol = g0, OxygenMol = o0, Co2Mol = c0, BreathableOxygenMol = oxygenMol,
                    Co2Limits = rules.EnforceCarbonDioxideLimits, OxygenCycles = Finite(oxygenCycles), Co2Cycles = Finite(co2Cycles), ShortfallKg = shortfallKg },
                ReserveKg = placedKg, ReserveCells = placed.Select(p => p.cell).Distinct().Count(), ReservePlaced = place
            };
            string Cycles(double c) => double.IsInfinity(c) ? "unlimited" : c.ToString("0.#", CultureInfo.InvariantCulture);
            Debug.Log(FormattableString.Invariant(
                $"[Mixed Atmospheres] Starting air: {minions.Count} duplicants use {rateKg:G4} kg/s oxygen; {inventory.Cells} connected cells hold {g0:G6} mol gas ({o0:G6} oxygen, {c0:G6} CO2) at {inventory.MeanTemperature:F1} K; breathable oxygen {Cycles(oxygenCycles)} cycles, CO2 headroom {Cycles(co2Cycles)} cycles; ten-cycle shortfall {shortfallKg:G6} kg; oxylite placed {placedKg:G6} kg."));
            if (minions.Count == 0) return;
            var lines = new List<string>
            {
                "The air connected to the printing pod lets your " + minions.Count + " duplicants breathe for about " + Cycles(oxygenCycles) +
                " cycles before its oxygen runs short." + (rules.EnforceCarbonDioxideLimits ?
                    " Exhaled carbon dioxide reaches the colony's limit after about " + Cycles(co2Cycles) + " cycles." :
                    " CO₂ breathing limits are off. You can enable them in Settings → Gameplay.")
            };
            if (placedKg > 0)
                lines.Add(((int)Math.Ceiling(placedKg)).ToString(CultureInfo.InvariantCulture) + " kg of oxylite has been placed beside the printing pod. It releases oxygen while the air around it is thin, covering ten cycles of breathing.");
            if (co2Cycles < BridgeCycles)
                lines.Add("Plan carbon dioxide removal early: dig the starting cave open to more air, or build an algae terrarium or a carbon skimmer.");
            if (placedKg > 0 || co2Cycles < BridgeCycles)
                ColonyNotices.Show("Starting air", string.Join("\n\n", lines));
        }

        private static double? Finite(double value) => double.IsInfinity(value) || double.IsNaN(value) ? (double?)null : value;

        // Oxylite chunks of at most ChunkKg on open floor cells beside the pod,
        // nearest first, alternating sides; deterministic for a given world.
        private static List<(int cell, double kg)> PlaceReserve(Inventory inventory, double kg)
        {
            var element = ElementLoader.FindElementByHash(SimHashes.OxyRock);
            var floor = new List<int>();
            for (var d = 2; d <= 12 && floor.Count < 8; ++d)
                foreach (var side in new[] { -1, 1 })
                {
                    // The right side stops short of the harness arenas (nine cells on).
                    if (side > 0 && d > 6) continue;
                    var cell = Grid.OffsetCell(inventory.PodCell, side * d, 0);
                    if (!Grid.IsValidCell(cell) || Grid.WorldIdx[cell] != inventory.World || Grid.Solid[cell] || Grid.IsSubstantialLiquid(cell)) continue;
                    var below = Grid.CellBelow(cell);
                    if (Grid.IsValidCell(below) && Grid.Solid[below]) floor.Add(cell);
                }
            if (floor.Count == 0) floor.Add(inventory.PodCell);
            var placed = new List<(int, double)>();
            var temperature = (float)Math.Min(330, Math.Max(250, inventory.MeanTemperature));
            for (var i = 0; kg > 1e-6; ++i)
            {
                var chunk = Math.Min(ChunkKg, kg);
                var cell = floor[i % floor.Count];
                element.substance.SpawnResource(Grid.CellToPosCBC(cell, Grid.SceneLayer.Ore), (float)chunk, temperature, byte.MaxValue, 0);
                placed.Add((cell, (double)(float)chunk)); kg -= chunk;
            }
            return placed;
        }
    }
}
