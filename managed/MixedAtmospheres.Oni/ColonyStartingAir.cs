using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The ten-cycle life-support bridge (docs/nitrogen.md). Before a new
    // colony's import, the gas connected to the printing pod is counted as the
    // import will hold it (nitrogen air converted). Once the starting
    // duplicants exist, their oxygen use and the colony's breathing rules give
    // the cycles of breathable oxygen and of carbon dioxide headroom, the
    // gas taken as well mixed. An oxygen shortfall over ten cycles is placed
    // as oxylite debris beside the pod (user decision 2026-10-27: every start,
    // generated or nitrogen air); a carbon dioxide shortfall is only told.
    // Validation scenes report the bridge and place the reserve only with
    // MA_COLONY_STARTING_RESERVE=1, so other harness arenas keep their worlds.
    internal static unsafe class ColonyStartingAir
    {
        internal const double BridgeCycles = 10, CycleSeconds = 600, ChunkKg = 300;
        private const int MaximumWaitFrames = 600;

        // The generated gas as counted; Gas(on) gives it with or without the
        // import's same-mass nitrogen air (oni_colony_import.cpp), which is
        // linear in each source gas's mass.
        private sealed class Inventory
        {
            internal int PodCell = -1, World = -1, Cells;
            internal readonly double[] SourceKg = new double[2], SourceMolar = new double[2];
            internal double NitrogenMolar, OtherMol, Co2Mol, NitrogenMol, GasVolume, MeanTemperature;
            internal bool NitrogenAir;
            internal (double oxygen, double co2, double nitrogen, double gas) Gas(bool air)
            {
                double oxygen = 0, nitrogen = NitrogenMol, gas = OtherMol;
                for (var k = 0; k < SourceKg.Length; ++k)
                {
                    if (!(SourceKg[k] > 0)) continue;
                    var share = air && NitrogenMolar > 0 ? MixedAtmospheres.Oni.NitrogenAir.OxygenShare : 1;
                    var moles = share < 1 ? StartingAirBridge.AirMoles(SourceKg[k], SourceMolar[k], NitrogenMolar, share) : SourceKg[k] / SourceMolar[k];
                    oxygen += share * moles; nitrogen += (1 - share) * moles; gas += moles;
                }
                return (oxygen, Co2Mol, nitrogen, gas);
            }
        }
        private static Inventory? pending;
        private static int waited;
        internal static object? Report { get; private set; }
        // For the harness scenario: the settled bridge of this session.
        internal static bool Settled { get; private set; }
        internal static int PodCell { get; private set; } = -1;
        internal static bool NitrogenAirApplied { get; private set; }
        internal static double ShortfallKg { get; private set; }
        internal static double ReserveKg { get; private set; }
        internal static int Duplicants { get; private set; }

        internal static void Reset()
        {
            pending = null; waited = 0; Report = null; Settled = false; PodCell = -1; NitrogenAirApplied = false; ShortfallKg = ReserveKg = 0; Duplicants = 0;
        }

        private static bool PlaceInValidation => Environment.GetEnvironmentVariable("MA_COLONY_STARTING_RESERVE") == "1";

        // Right before the owner imports a new colony. Uses the native frame's
        // own element, mass and temperature arrays (the import's input).
        internal static void Capture(Sim.GameDataUpdate* source, bool nitrogenAir, Func<int, bool> space)
        {
            Reset();
            try
            {
                var telepad = Components.Telepads.Count > 0 ? Components.Telepads[0] : null;
                if (telepad == null) { Report = new { Skipped = "no printing pod" }; return; }
                var pod = Grid.PosToCell(telepad);
                if (!Grid.IsValidCell(pod)) { Report = new { Skipped = "printing pod outside the grid" }; return; }
                var inventory = new Inventory { PodCell = pod, World = Grid.WorldIdx[pod], NitrogenAir = nitrogenAir };
                var oxygen = ElementLoader.FindElementByHash(SimHashes.Oxygen);
                var polluted = ElementLoader.FindElementByHash(SimHashes.ContaminatedOxygen);
                var co2 = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide);
                var nitrogen = ElementLoader.FindElementByHash(OniNitrogenElements.Gas);
                double heat = 0, moles = 0;
                inventory.NitrogenMolar = nitrogen != null ? nitrogen.molarMass / 1000.0 : 0;
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
                        var kind = element == oxygen ? 0 : element == polluted ? 1 : -1;
                        if (kind >= 0) { inventory.SourceKg[kind] += mass; inventory.SourceMolar[kind] = molar; }
                        else
                        {
                            inventory.OtherMol += mass / molar;
                            if (element == co2) inventory.Co2Mol += mass / molar;
                            else if (element == nitrogen) inventory.NitrogenMol += mass / molar;
                        }
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
            // The bridge of the colony's own air, and for comparison of the other air.
            (double oxygenCycles, double co2Cycles, double shortfallKg, object report) Bridge(bool air)
            {
                var (o, c, n, g) = inventory.Gas(air);
                var (oxygenMol, co2Mol) = StartingAirBridge.BreathableMoles(o, c, g, inventory.GasVolume, inventory.MeanTemperature, k, rules.MinimumOxygenFraction,
                    rules.MinimumOxygenPartialPressurePa, rules.MaximumOxygenFraction, rules.MaximumCo2Fraction, rules.MaximumCo2PartialPressurePa);
                var oc = molesPerCycle > 0 ? oxygenMol / molesPerCycle : double.PositiveInfinity;
                var cc = molesPerCycle > 0 ? co2Mol / molesPerCycle : double.PositiveInfinity;
                var shortfall = Math.Max(0, BridgeCycles * CycleSeconds * rateKg - oxygenMol * oxygenMolar);
                return (oc, cc, shortfall, new { NitrogenAir = air, GasMol = g, OxygenMol = o, Co2Mol = c, NitrogenMol = n,
                    BreathableOxygenMol = oxygenMol, Co2HeadroomOxygenMol = co2Mol, OxygenCycles = Finite(oc), Co2Cycles = Finite(cc), ShortfallKg = shortfall });
            }
            var (oxygenCycles, co2Cycles, shortfallKg, own) = Bridge(inventory.NitrogenAir);
            var other = Bridge(!inventory.NitrogenAir).report;
            var (o0, c0, n0, g0) = inventory.Gas(inventory.NitrogenAir);
            var place = shortfallKg > 0 && (ColonyRuntimeValidation.Settings == null || PlaceInValidation);
            var placed = place ? PlaceReserve(inventory, shortfallKg) : new List<(int cell, double kg)>();
            var placedKg = placed.Sum(p => p.kg);
            Settled = true; PodCell = inventory.PodCell; NitrogenAirApplied = inventory.NitrogenAir; ShortfallKg = shortfallKg; ReserveKg = placedKg; Duplicants = minions.Count;
            Report = new
            {
                Duplicants = minions.Count, OxygenKgPerSecond = rateKg, inventory.Cells, inventory.GasVolume, inventory.MeanTemperature,
                Colony = own, OtherAir = other, ReserveKg = placedKg, ReserveCells = placed.Select(p => p.cell).Distinct().Count(), ReservePlaced = place
            };
            string Cycles(double c) => double.IsInfinity(c) ? "unlimited" : c.ToString("0.#", CultureInfo.InvariantCulture);
            Debug.Log(FormattableString.Invariant(
                $"[Mixed Atmospheres] Starting air: {minions.Count} duplicants use {rateKg:G4} kg/s oxygen; {inventory.Cells} connected cells hold {g0:G6} mol gas ({o0:G6} oxygen, {c0:G6} CO2, {n0:G6} nitrogen) at {inventory.MeanTemperature:F1} K; breathable oxygen {Cycles(oxygenCycles)} cycles, CO2 headroom {Cycles(co2Cycles)} cycles; ten-cycle shortfall {shortfallKg:G6} kg; oxylite placed {placedKg:G6} kg."));
            if (minions.Count == 0) return;
            var lines = new List<string>
            {
                "The air connected to the printing pod lets your " + minions.Count + " duplicants breathe for about " + Cycles(oxygenCycles) +
                " cycles before its oxygen runs short, and about " + Cycles(co2Cycles) + " cycles before exhaled carbon dioxide passes the colony's limit."
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
