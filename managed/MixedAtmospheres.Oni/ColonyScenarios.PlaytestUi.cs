using System;
using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static GameObject? playtestUiResearch, playtestUiVent;
        private static int playtestUiInlet, playtestUiSuppliedAt;
        private static PipeInventory playtestUiPacket = PipeInventory.Empty;

        // Explicit, short UI diagnostic only. Leave a real research station
        // and an overlapping vent/pipe in view for manual Research/Skills and
        // hover checks. This does not replace any gameplay or physics gate.
        private static void PlaytestUi(int frame)
        {
            var flow = Game.Instance.gasConduitFlow;
            if (Once(0, "sealed UI inspection room"))
            {
                playtestUiResearch = playtestUiVent = null;
                playtestUiInlet = -1; playtestUiSuppliedAt = -1;
                playtestUiPacket = PipeInventory.Empty;
                Arena((x,y) => (SimHashes.Oxygen, 3f, 300f));
            }
            if (Once(6, "real research station and overpressure vent"))
            {
                playtestUiResearch = Build(ResearchCenterConfig.ID, Cell(1,0));
                playtestUiVent = Build(GasVentConfig.ID, Cell(5,1));
            }
            if (Once(8, "overlapping gas pipe and vent input"))
            {
                playtestUiInlet = VentInlet(playtestUiVent!);
                Pipe(Game.Instance.gasConduitSystem, GasConduitConfig.ID, new List<int> { playtestUiInlet });
                // Hold this one real port for the human inspection, rather
                // than repeatedly replenishing a parcel while it is consumed.
                // The separate native IsBlocked check still observes the
                // room's actual overpressure; this is not a vent-rate test.
                AccessTools.Field(typeof(ConduitConsumer), "isOn").SetValue(playtestUiVent!.GetComponent<ConduitConsumer>(), false);
            }
            if (frame >= 10 && playtestUiSuppliedAt < 0)
            {
                if (!flow.HasConduit(playtestUiInlet))
                {
                    Current.Stage = "waiting for UI pipe topology";
                    if (frame >= 40) { Record("UI pipe topology became ready", false, "readiness expired"); Finish(); }
                    return;
                }
                ColonyPipeRuntime.Start();
                playtestUiPacket = PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Oxygen), 9e-17, 300, 0, .125)
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Methane), 8e-18, 300))
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.CarbonDioxide), 2e-18, 300));
                ColonyConduits.SupplyControlInventory(flow, playtestUiInlet, playtestUiPacket);
                playtestUiSuppliedAt = frame;
                Current.Stage = "holding one tiny mixed packet for UI inspection";
            }
            if (playtestUiSuppliedAt >= 0 && Once(playtestUiSuppliedAt + 20, "UI inspection scene ready"))
            {
                var held = ColonyConduits.TryRead(flow, playtestUiInlet, out var packet) ? packet : PipeInventory.Empty;
                Grid.CellToXY(playtestUiInlet, out var vx, out var vy);
                Grid.CellToXY(Grid.PosToCell(playtestUiResearch!), out var rx, out var ry);
                Record("real research station enables the Research screen", playtestUiResearch != null &&
                    playtestUiResearch.GetComponent<ResearchCenter>() != null && Components.ResearchCenters.Count > 0,
                    $"station at {rx},{ry}; registered research centers {Components.ResearchCenters.Count}");
                Record("one overlapping pipe retains the positive three-gas parcel", held.Count == 3 && held.Mass > 0 &&
                    held.Total.Minus(playtestUiPacket.Total).Sign == 0,
                    $"vent/pipe hover at {vx},{vy}, cell {playtestUiInlet}; {held.Mass:R} kg; {held.Count} species; consumer held off once for inspection");
                Record("fractional germ remains visible in the owned packet", held.Germs.Length > 0 && held.Germs[0].Mass > 0,
                    $"{held.Germs.Length} germ identities; first count {(held.Germs.Length == 0 ? 0 : held.Germs[0].Mass):R}");
                Record("real vent sees room overpressure", playtestUiVent!.GetComponent<Vent>().IsBlocked,
                    MachineState(playtestUiVent));
                Record("UI fixture pipe accounts close", ColonyConduits.Audit(out var audit), audit);
                if (CameraController.Instance != null)
                    CameraController.Instance.SnapTo(Grid.CellToPosCCC(Cell(3,2), Grid.SceneLayer.Background), 4);
                Record("UI inspection camera points at the fixture", CameraController.Instance != null,
                    $"camera center {x0 + 3},{y0 + 2}; vent/pipe {vx},{vy}; research station {rx},{ry}; manual Research/Skills and hover checks remain separate");
                Finish();
            }
        }
    }
}
