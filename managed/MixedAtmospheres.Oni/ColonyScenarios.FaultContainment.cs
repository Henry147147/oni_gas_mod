using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Fault containment (docs/colony-owner.md#faults): a throw injected into
    // each per-frame owner and pipe path is contained without stopping the
    // game's sim step (rehearsals, which do not fault). A last one, for real,
    // faults the owner once: it and its pipes freeze, the game keeps stepping
    // (a heater built afterwards gets power from ONI's circuits, which run
    // after the pipes in each step) and saving is refused.
    internal static partial class ColonyScenarios
    {
        private static readonly string[] ContainedPaths =
            { "receipts", "publication", "messages", "sim-step", "pipe-flow", "pipe-machine", "pipe-heat", "pipe-read", "vent", "sensor" };
        private const string RealFaultPath = "pipe-flow";
        private static int containedIndex, containedArmedAt;
        private static float faultSeenAt = -1;
        private static ulong faultFrame;
        private static double faultPipeMass;
        private static GameObject? faultHeater;
        private static readonly List<int> faultPipe = new List<int>();
        internal static bool FaultExpected => Current.Name == "fault-containment" && ColonyFault.InjectedFault;
        private static double FaultPipeMass() => faultPipe.Sum(c => (double)Game.Instance.gasConduitFlow.GetContents(c).mass);

        private static void FaultContainment(int frame, OniNativeColony.Status status)
        {
            if (Once(0, "arena"))
            {
                ColonyFault.Reset(); containedIndex = 0; containedArmedAt = -1; faultSeenAt = -1; faultHeater = null; faultPipe.Clear();
                Arena((x, y) => x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : x > 4 ? (SimHashes.Vacuum, 0f, 0f) :
                    ((x + y) % 2 == 0 ? (SimHashes.Oxygen, 1.2f, 300f) : (SimHashes.CarbonDioxide, 1.6f, 300f)));
            }
            if (Once(6, "build"))
            {
                var pump = Build(GasPumpConfig.ID, Cell(1, 0)); var vent = Build(GasVentConfig.ID, Cell(6, 0));
                faultPipe.AddRange(Route(Grid.OffsetCell(Cell(1, 0), pump.GetComponent<Building>().Def.UtilityOutputOffset),
                    Grid.OffsetCell(Cell(6, 0), vent.GetComponent<Building>().Def.UtilityInputOffset)));
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, faultPipe);
                sensor = Build(LogicElementSensorGasConfig.ID, Cell(3, 4)).GetComponent<LogicElementSensor>();
            }
            if (Once(7, "sensor")) sensor!.GetComponent<Filterable>().SelectedTag = ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).tag;
            if (frame < 20 || faultSeenAt >= 0 && Current.Finished) return;
            // Rehearsals: one path at a time, each given 20 owner frames.
            if (containedIndex < ContainedPaths.Length)
            {
                ColonyFault.Rehearse = true;
                var path = ContainedPaths[containedIndex];
                if (containedArmedAt < 0) { ColonyFault.Arm(path); containedArmedAt = frame; Current.Stage = "rehearse " + path; return; }
                var at = ColonyFault.RehearsedAt(path);
                // An owned cell's heat edit is a message the owner claims.
                if (at.Length == 0 && path == "messages") SimMessages.ModifyEnergy(Cell(2, 4), .001f, 1000, SimMessages.EnergySourceID.DebugHeat);
                if (at.Length == 0 && frame - containedArmedAt <= 20) return;
                if (at.Length == 0) ColonyFault.Arm("");
                Record($"the {path} path contained its injected throw", at.Length != 0 && !ColonyFault.Faulted && status.State == 1,
                    $"contained at {(at.Length != 0 ? at : "-")} after {frame - containedArmedAt} owner frames; owner state {status.State}");
                ++containedIndex; containedArmedAt = -1;
                return;
            }
            // The real fault.
            if (containedArmedAt < 0)
            {
                ColonyFault.Rehearse = false; ColonyFault.Arm(RealFaultPath); containedArmedAt = frame; Current.Stage = "fault " + RealFaultPath; return;
            }
            var clock = GameClock.Instance.GetTime();
            if (faultSeenAt < 0)
            {
                if (status.State != 2) return;
                faultSeenAt = clock; faultFrame = status.CompletedFrames; faultPipeMass = FaultPipeMass();
                faultHeater = Build(SpaceHeaterConfig.ID, Cell(1, 3)); Current.Stage = "faulted; game steps on";
                return;
            }
            if (clock < faultSeenAt + 6) return;
            var heater = faultHeater != null ? faultHeater.GetComponent<EnergyConsumer>() : null;
            Record("the real fault stopped the owner once, with its reason",
                ColonyFault.Faulted && ColonyFault.InjectedFault && status.State == 2 && status.Error.Contains("Injected fault (" + RealFaultPath + ")") &&
                ColonyOwner.NoticedFault == status.Error,
                $"state {status.State}, error '{status.Error}', reason '{ColonyFault.Reason}', noticed '{ColonyOwner.NoticedFault}', contained {ColonyFault.Contained}");
            Record("the owner and its pipes froze", status.CompletedFrames <= faultFrame + 1 && FaultPipeMass() == faultPipeMass,
                $"owner frame {faultFrame} -> {status.CompletedFrames}; pipe {faultPipeMass:R} -> {FaultPipeMass():R} kg");
            Record("the game kept stepping: a heater built after the fault got power", heater != null && heater.IsPowered && clock >= faultSeenAt + 6,
                $"game time {faultSeenAt:F1} -> {clock:F1} s; heater powered {heater?.IsPowered}, circuit {heater?.CircuitID}");
            string refusal;
            try { using var writer = new BinaryWriter(new MemoryStream()); ColonyOwnerPersistence.TryWriteSave(writer, 0, 0); refusal = "accepted"; }
            catch (InvalidOperationException error) { refusal = error.Message; }
            Record("saving is refused", refusal.StartsWith("A faulted colony owner cannot be saved", StringComparison.Ordinal), refusal);
            Finish();
        }
    }
}
