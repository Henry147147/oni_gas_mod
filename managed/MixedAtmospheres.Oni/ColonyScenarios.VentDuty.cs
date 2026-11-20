using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Two gas pumps on one pipe to a gas vent (docs/mixed-pipes.md#throughput),
        // the layout of the user's report of 2026-11-19. One sealed room of
        // oxygen, 12 by 6 cells at 1.75 kg a cell. Frame 6: gas pumps on the
        // floor at (0,0) and (2,0); one pipe runs from the first pump's outlet
        // past the second's along row 1 to a gas vent at (9,1): a closed
        // loop, so the room stays near 1.7 kg a cell, below the vent's 2 kg
        // limit. From frame 150 to frame 400 (50 pipe ticks):
        // - in owner mode the mixed pipe runtime, its pumps and vents carry
        //   the scenario and every pipe account closes exactly;
        // - the vent releases 23.75 to 26.25 kg: within 5 % of the 25.0 kg
        //   ONI's own simulation releases in this layout (--mode colony-vanilla,
        //   which runs this scenario as its reference; there ONI's flow
        //   scheduling holds the first pump's outlet packet and the second
        //   pump passes 0.5 kg every tick). The playtest build released 13.0 kg;
        // - at every pipe tick, after ONI's outlet checks, no pump outlet may
        //   hold a positive raw mass below a milligram: an outlet that reads
        //   0 g must never stop its pump (the playtest build left sub-float
        //   remainders of moved packets there, and ONI's RequireOutputs held
        //   the pump for the tick);
        // - the room's mean stays between 1.6 and 1.9 kg a cell. This bounds the
        //   mean only: the vent's own cell may pass 2 kg for a moment after
        //   each packet (ONI's own vent cell reached 2.41 to 2.42 kg and its vent
        //   read blocked a frame or two after each packet; the check's detail
        //   reports both).
        // The first run used 2 kg a cell; there ONI's own vent stood at its
        // overpressure limit, so the room moved to 1.75 kg before these
        // bounds were fixed. The upper throughput bound and the runtime check
        // followed a review (2026-11-20).
        private const int VentDutyWidth = 12, VentDutyX = 9;
        private const float VentDutyFill = 1.75f;
        private static readonly List<GameObject> ventDutyBuildings = new List<GameObject>();
        private static readonly List<int> ventDutyOutlets = new List<int>();
        private static readonly List<string> ventDutyTrace = new List<string>();
        private static System.Action<float>? ventDutyProbe;
        private static int ventDutyLast = -1, ventDutyChecks, ventDutyResidues, ventDutyBlocked;
        private static double ventDutyPeak;
        private static double ventDutyLowMean = double.MaxValue, ventDutyHighMean;
        private static string ventDutyFirstResidue = "";
        private static double VentedMass(GameObject vent) => vent.GetComponent<Vent>().lifeTimeVentMass.Values.Sum(v => (double)v);
        private static double VentDutyMean()
        {
            double total = 0;
            for (var y = 0; y < Height; ++y) for (var x = 0; x < VentDutyWidth; ++x) total += CellMass(Cell(x, y));
            return total / (VentDutyWidth * Height);
        }
        // A conduit updater between ONI's outlet checks (RequireOutputs,
        // priority First) and the consumers and dispensers of the tick.
        private static void VentDutyOutlets(float dt)
        {
            var frame = ColonyFrame(); if (frame < 150 || frame >= 400 || ventDutyOutlets.Count != 2) return;
            var flow = Game.Instance.gasConduitFlow;
            foreach (var outlet in ventDutyOutlets)
            {
                ++ventDutyChecks;
                if (flow.IsConduitEmpty(outlet) || flow.GetContents(outlet).mass >= 1e-6f) continue;
                if (ventDutyResidues++ == 0) ventDutyFirstResidue = $"frame {frame}, outlet {outlet}: {flow.GetContents(outlet).mass:R} kg";
            }
        }
        private static void VentDuty(int frame)
        {
            if (Once(0, "one room of oxygen below the vent's limit"))
            {
                ventDutyBuildings.Clear(); ventDutyOutlets.Clear(); ventDutyTrace.Clear(); ventDutyLast = -1;
                ventDutyChecks = ventDutyResidues = ventDutyBlocked = 0; ventDutyPeak = 0; ventDutyFirstResidue = "";
                ventDutyHighMean = 0; ventDutyLowMean = double.MaxValue;
                pendingShift = VentDutyWidth - Width;
                Arena((x, y) => (SimHashes.Oxygen, VentDutyFill, 300f), VentDutyWidth);
            }
            if (Once(6, "two gas pumps into one pipe to a vent"))
            {
                var vent = Build(GasVentConfig.ID, Cell(VentDutyX, 1)); ventDutyBuildings.Add(vent);
                var to = Grid.OffsetCell(Cell(VentDutyX, 1), vent.GetComponent<Building>().Def.UtilityInputOffset);
                foreach (var x in new[] { 0, 2 })
                {
                    var pump = Build(GasPumpConfig.ID, Cell(x, 0)); ventDutyBuildings.Add(pump);
                    var from = Grid.OffsetCell(Cell(x, 0), pump.GetComponent<Building>().Def.UtilityOutputOffset); ventDutyOutlets.Add(from);
                    var path = Route(from, to); Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, path); pipeCells.AddRange(path);
                }
                ventDutyProbe = VentDutyOutlets;
                Game.Instance.gasConduitFlow.AddConduitUpdater(ventDutyProbe, (ConduitFlowPriority)(-50));
            }
            if (ventDutyBuildings.Count == 3 && frame >= 150 && frame < 400 && frame != ventDutyLast)
            {
                ventDutyLast = frame;
                var mean = VentDutyMean();
                if (ventDutyBuildings[0].GetComponent<Vent>().IsBlocked) ++ventDutyBlocked;
                ventDutyPeak = Math.Max(ventDutyPeak, Grid.Mass[Cell(VentDutyX, 1)]);
                ventDutyLowMean = Math.Min(ventDutyLowMean, mean); ventDutyHighMean = Math.Max(ventDutyHighMean, mean);
                if (frame < 166)
                {
                    var flow = Game.Instance.gasConduitFlow; var vent = ventDutyBuildings[0]; var cell = Cell(VentDutyX, 1);
                    string Pump(GameObject p) { var op = p.GetComponent<Operational>(); return $"{(op.IsOperational ? "O" : "-")}{(op.IsActive ? "A" : "-")}{HeldMass(p):0.##}"; }
                    ventDutyTrace.Add($"{frame} vent cell {Grid.Mass[cell]:0.###}{(vent.GetComponent<Vent>().IsBlocked ? " blocked" : "")} vented {VentedMass(vent):0.##} " +
                        $"pipe [{string.Join(" ", pipeCells.Distinct().Select(c => flow.GetContents(c).mass.ToString("0.##")))}] pumps {Pump(ventDutyBuildings[1])} {Pump(ventDutyBuildings[2])}");
                }
            }
            if (Once(150, "start of the measured window"))
            {
                recorded["vented"] = VentedMass(ventDutyBuildings[0]);
                recorded["mean"] = VentDutyMean();
            }
            if (Once(400, "vent output over 50 pipe ticks"))
            {
                if (ventDutyProbe != null) Game.Instance.gasConduitFlow.RemoveConduitUpdater(ventDutyProbe);
                ventDutyProbe = null;
                var vent = ventDutyBuildings[0];
                var vented = VentedMass(vent) - recorded["vented"];
                var flow = Game.Instance.gasConduitFlow;
                var pipe = string.Join(" ", pipeCells.Distinct().Select(c => flow.GetContents(c).mass.ToString("0.##")));
                var detail = Vanilla ? "ONI's own simulation" : ColonyPipeVents.ControlDetails(vent.GetComponent<Storage>());
                Debug.Log($"[Mixed Atmospheres] vent-duty trace (vent cell kg; vented kg; pipe kg from the first pump's outlet to the vent; pumps O operational, A active, held kg):\n{string.Join("\n", ventDutyTrace)}");
                if (!Vanilla)
                    Record("mixed pipes, pumps and vents carry the scenario", ColonyPipeRuntime.Active && ColonyMachinePorts.Enabled && ColonyPipePumps.Enabled && ColonyPipeVents.Enabled,
                        ColonyPipeRuntime.StatusLine);
                Record("the pumps' pipe reaches the vent as in ONI", vented >= 23.75 && vented <= 26.25,
                    $"vent released {vented:F2} kg in 50 s (ONI's own simulation: 25.0 kg); vent read blocked in {ventDutyBlocked} of 250 frames, its cell up to {ventDutyPeak:F3} kg; pipe kg by cell: {pipe}; {detail}; " +
                    $"pumps: {MachineState(ventDutyBuildings[1])} | {MachineState(ventDutyBuildings[2])}");
                Record("an outlet that reads 0 g never stops its pump", ventDutyChecks >= 100 && ventDutyResidues == 0,
                    $"{ventDutyResidues} of {ventDutyChecks} outlet checks held a positive mass below a milligram{(ventDutyResidues > 0 ? "; first " + ventDutyFirstResidue : "")}");
                Record("the room's mean stays below the vent's limit", ventDutyLowMean >= 1.6 && ventDutyHighMean <= 1.9,
                    $"room mean {ventDutyLowMean:F3}..{ventDutyHighMean:F3} kg a cell (at the window's start {recorded["mean"]:F3})");
                if (!Vanilla)
                {
                    Record("the vent run closes every pipe account", ColonyConduits.Audit(out var audit), audit);
                }
                Finish();
            }
        }
    }
}
