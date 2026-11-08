using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using MixedAtmospheres.Core;
using Newtonsoft.Json;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Explicit diagnostic scenes for the Windows video review. They add no
        // ordinary-game work and do not replace any fixed physics/gameplay gate.
        [StructLayout(LayoutKind.Sequential)]
        private struct PlaytestRegionEnergy
        {
            public uint Size, Version;
            public ulong Session, Frame;
            public double Seconds, InternalJ, KineticJ, PotentialJ;
            public double ThermalJ, FormationJ, CapacityJPerK, MassKg;
        }
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_sample_region_energy", CallingConvention = CallingConvention.Cdecl)]
        private static extern int PlaytestSampleEnergy(ulong session, ulong frame, [In] uint[] cells, uint count, ref PlaytestRegionEnergy result);
        private static readonly List<GameObject> playtestMachines = new List<GameObject>();
        private static readonly List<int> playtestProbeCells = new List<int>();
        private static readonly Dictionary<int, bool> playtestMachineActive = new Dictionary<int, bool>();
        private static readonly HashSet<int> playtestEverActive = new HashSet<int>();
        private static readonly List<(MinionIdentity minion, int cell)> playtestResidents = new List<(MinionIdentity, int)>();
        private static uint[] playtestRegion = Array.Empty<uint>();
        private static ulong playtestLastFrame;
        private static int playtestSamples, playtestMismatches, playtestTransitions, playtestEnergySamples, playtestUnsafeSamples;
        private static double playtestMinimumTemperature, playtestMaximumTemperature, playtestMinimumBreath;
        private static string playtestTracePath = "";

        private static void BeginPlaytestReview(int width)
        {
            playtestMachines.Clear(); playtestProbeCells.Clear(); playtestMachineActive.Clear(); playtestEverActive.Clear(); playtestResidents.Clear();
            playtestRegion = Enumerable.Range(0, width).SelectMany(x => Enumerable.Range(0, Height).Select(y => (uint)Cell(x,y))).ToArray();
            playtestLastFrame = ulong.MaxValue;
            playtestSamples = playtestMismatches = playtestTransitions = playtestEnergySamples = playtestUnsafeSamples = 0;
            playtestMinimumTemperature = double.PositiveInfinity; playtestMaximumTemperature = 0; playtestMinimumBreath = 100;
            Directory.CreateDirectory(ColonyRuntimeValidation.OutputDirectory);
            playtestTracePath = Path.Combine(ColonyRuntimeValidation.OutputDirectory, Current.Name + "-trace.jsonl");
            if (File.Exists(playtestTracePath)) throw new IOException("A playtest diagnostic needs a fresh output path.");
        }
        private static void StationPlaytestResident(int index, int cell)
        {
            var minion = Visit(index, cell); playtestResidents.Add((minion, cell));
            Db.Get().Amounts.Breath.Lookup(minion.gameObject).SetValue(100);
        }
        private static object PlaytestCell(int cell)
        {
            var valid = Grid.IsValidCell(cell);
            var owned = ColonyOwner.TryReadView(cell, out var view);
            var intercepted = ColonyOwner.TryReadBreathingCell(cell, out var mass, out var element);
            MixturePresentation? reading = null;
            if (valid) MixturePresentationRuntime.TryGetCell(cell, out reading);
            var consistent = !owned || reading == null || reading.IsBreathable == (mass > 0);
            if (!consistent) ++playtestMismatches;
            if (owned && view.MassKg > 1e-9f && (view.Flags & OniNativeColony.ViewGas) != 0)
            {
                playtestMinimumTemperature = Math.Min(playtestMinimumTemperature, view.TemperatureK);
                playtestMaximumTemperature = Math.Max(playtestMaximumTemperature, view.TemperatureK);
            }
            return new { Cell = cell, Owned = owned, Flags = view.Flags, MassKg = view.MassKg,
                TemperatureK = view.TemperatureK, PressurePa = view.PressurePa, Intercepted = intercepted,
                OfferedOxygenKg = mass, OfferedElement = element.ToString(), PresentationBreathable = reading?.IsBreathable,
                PresentationTemperatureK = reading?.TemperatureKelvin, Consistent = consistent };
        }
        private static void ObservePlaytestReview(int frame)
        {
            var status = ColonyOwner.Status;
            if (playtestLastFrame == status.CompletedFrames) return;
            playtestLastFrame = status.CompletedFrames;
            // Position control is deliberate and confined to these diagnostics:
            // each duplicant breathes in a known six-cell neighbourhood.
            foreach (var resident in playtestResidents) Teleport(resident.minion, resident.cell);
            var duplicants = playtestResidents.Select(resident =>
            {
                var breather = resident.minion.GetComponent<OxygenBreather>();
                var provider = breather.GetCurrentGasProvider();
                var best = GasBreatherFromWorldProvider.GetBestBreathableCellAroundSpecificCell(resident.cell, null, breather, out var total);
                var breath = Db.Get().Amounts.Breath.Lookup(resident.minion.gameObject).value;
                playtestMinimumBreath = Math.Min(playtestMinimumBreath, breath);
                if (breather.IsOutOfOxygen) ++playtestUnsafeSamples;
                var samples = GasBreatherFromWorldProvider.DEFAULT_BREATHABLE_OFFSETS.Select(offset => PlaytestCell(Grid.OffsetCell(resident.cell, offset))).ToArray();
                return new { Id = resident.minion.GetInstanceID(), Cell = resident.cell, Breath = breath,
                    OutOfOxygen = breather.IsOutOfOxygen, Provider = provider?.GetType().Name,
                    ProviderHasOxygen = provider?.HasOxygen(), LowOxygen = provider?.IsLowOxygen(),
                    BestCell = best.Cell, BestOxygenKg = best.Mass, BestElement = best.ElementID.ToString(),
                    BestBreathable = best.IsBreathable, TotalOfferedOxygenKg = total, Cells = samples };
            }).ToArray();
            var machines = playtestMachines.Where(machine => machine != null).Select(machine =>
            {
                var id = machine.GetInstanceID(); var active = machine.GetComponent<Operational>()?.IsActive == true;
                if (playtestMachineActive.TryGetValue(id, out var previous) && previous != active) ++playtestTransitions;
                playtestMachineActive[id] = active;
                if (active) playtestEverActive.Add(id);
                return new { Id = id, Kind = machine.PrefabID().Name, Active = active, State = MachineState(machine) };
            }).ToArray();
            object? energy = null;
            if (frame % 5 == 0)
            {
                var source = OniNativeColony.Status.Empty;
                OniNativeColony.Check(OniNativeColony.CopyStatus(ref source));
                var sample = new PlaytestRegionEnergy { Size = 88, Version = 1 };
                var rc = PlaytestSampleEnergy(source.Session, source.CompletedFrames, playtestRegion, (uint)playtestRegion.Length, ref sample);
                // A newer latest frame can win this read-only race. Keep that
                // refusal explicit; never pair components from different frames.
                if (rc == 0) { ++playtestEnergySamples; energy = sample; }
                else energy = new { Refused = rc, RequestedFrame = source.CompletedFrames };
            }
            File.AppendAllText(playtestTracePath, JsonConvert.SerializeObject(new { Scenario = Current.Name, Frame = frame,
                OwnerFrame = status.CompletedFrames, OwnerTime = status.TimeSeconds, status.MaximumRelativeMassError,
                status.MaximumRelativeEnergyError, status.InjectedKg, status.ExtractedKg, status.NativeHeatJoules,
                Cells = playtestProbeCells.Select(PlaytestCell).ToArray(), Duplicants = duplicants, Machines = machines,
                RegionEnergy = energy }) + "\n");
            ++playtestSamples;
        }
        private static void FinishPlaytestReview(OniNativeColony.Status status, bool requireBreathing)
        {
            Record("diagnostic captured real machine and fluid frames", playtestSamples > 20 && playtestEnergySamples > 0,
                $"{playtestSamples} samples, {playtestEnergySamples} coherent regional U/K/G observations, machine active transitions {playtestTransitions}; " +
                $"gas temperature {playtestMinimumTemperature:R}..{playtestMaximumTemperature:R} K; private trace {Path.GetFileName(playtestTracePath)}. " +
                "Regional body heat/source ledgers are not inferred from the global owner account.");
            var workingMachines = playtestMachines.Where(machine => machine != null && (machine.PrefabID().Name == GasPumpConfig.ID || machine.PrefabID().Name == MineralDeoxidizerConfig.ID)).ToArray();
            Record("both diagnostic source machines actually operated", workingMachines.Length == 2 && workingMachines.All(machine => playtestEverActive.Contains(machine.GetInstanceID())),
                $"{workingMachines.Count(machine => playtestEverActive.Contains(machine.GetInstanceID()))}/{workingMachines.Length} observed active");
            Record("breathing selection agrees with mixture presentation", playtestMismatches == 0, $"mismatches {playtestMismatches}");
            if (requireBreathing) Record("stationed duplicants keep breathing in the diffuser room", playtestResidents.Count > 0 && playtestUnsafeSamples == 0 && playtestMinimumBreath > 90,
                $"out-of-oxygen samples {playtestUnsafeSamples}, minimum breath {playtestMinimumBreath:R}");
            Record("diagnostic keeps existing owner ledger limits", status.State == 1 && status.MaximumRelativeMassError <= 1e-8 && status.MaximumRelativeEnergyError <= 1e-6,
                $"mass {status.MaximumRelativeMassError:R}, energy {status.MaximumRelativeEnergyError:R}");
            SendVisitorsHome(); playtestResidents.Clear(); Finish();
        }
        private static void PlaytestDiffusers(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "diagnostic diffuser pressure cycle"))
            {
                BeginPlaytestReview(Width);
                Arena((x,y) => (SimHashes.Oxygen, 1.7f, 296.15f));
            }
            if (Once(6, "two ordinary oxygen diffusers"))
            {
                foreach (var x in new[] { 2, 5 })
                {
                    var diffuser = Build(MineralDeoxidizerConfig.ID, Cell(x, 0));
                    diffuser.GetComponent<Storage>().AddOre(SimHashes.Algae, 100, 296.15f, byte.MaxValue, 0);
                    playtestMachines.Add(diffuser); playtestProbeCells.Add(Cell(x,1)); playtestProbeCells.Add(Cell(x-1,1));
                }
            }
            if (Once(12, "station breathers in known oxygen neighbourhoods"))
                for (var k = 0; k < Math.Min(3, Components.LiveMinionIdentities.Items.Count); ++k) StationPlaytestResident(k, Cell(new[] { 0, 3, 7 }[k], 0));
            if (frame >= 13) ObservePlaytestReview(frame);
            if (Once(240, "finish diffuser diagnostic")) FinishPlaytestReview(status, true);
        }
        private static void PlaytestPumpVent(int frame, OniNativeColony.Status status)
        {
            const int width = 14;
            if (Once(0, "diagnostic pump and vent filled rooms"))
            {
                BeginPlaytestReview(width); pendingShift += width - Width;
                Arena((x,y) => x == 5 ? (SimHashes.Unobtanium, 20000f, 296.15f) : (SimHashes.Oxygen, x < 5 ? 1.2f : .6f, 296.15f), width);
            }
            if (Once(6, "two real pumps to one ordinary vent"))
            {
                var vent = Build(GasVentConfig.ID, Cell(7, 1)); playtestMachines.Add(vent);
                var to = Grid.OffsetCell(Cell(7,1), vent.GetComponent<Building>().Def.UtilityInputOffset);
                foreach (var x in new[] { 0, 2 })
                {
                    var pump = Build(GasPumpConfig.ID, Cell(x,0)); playtestMachines.Add(pump);
                    var from = Grid.OffsetCell(Cell(x,0), pump.GetComponent<Building>().Def.UtilityOutputOffset);
                    var path = Route(from, to); Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, path); pipeCells.AddRange(path);
                }
                foreach (var point in new[] { (1,1), (3,1), (7,1), (8,1), (7,2), (10,1) }) playtestProbeCells.Add(Cell(point.Item1,point.Item2));
            }
            if (Once(12, "station a breather in the receiving room"))
                if (Components.LiveMinionIdentities.Items.Count > 0) StationPlaytestResident(0, Cell(11,0));
            if (frame >= 13) ObservePlaytestReview(frame);
            if (Once(300, "finish pump and vent diagnostic")) FinishPlaytestReview(status, true);
        }
    }
}
